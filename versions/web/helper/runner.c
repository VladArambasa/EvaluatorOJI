/* RUNNER.C - SEE RUNNER.H. BUILDS ON POSIX AND WINDOWS (MINGW) */
/* MINIMUM WINDOWS API: WINDOWS 7 (OLD MINGW DEFAULTS TO XP/2003) */
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* === ERROR DESCRIPTIONS === */
#ifndef _WIN32
#include <signal.h>
const char *rn_signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV - invalid memory access (array out of bounds, bad pointer or stack overflow)";
    case SIGFPE:  return "SIGFPE - arithmetic error (e.g. division by zero)";
    case SIGABRT: return "SIGABRT - program aborted (failed assert or uncaught exception)";
    case SIGKILL: return "SIGKILL - program killed";
    case SIGXCPU: return "SIGXCPU - CPU time limit exceeded";
    case SIGXFSZ: return "SIGXFSZ - output file too large";
    case SIGBUS:  return "SIGBUS - memory access error";
    case SIGILL:  return "SIGILL - illegal instruction";
    case SIGPIPE: return "SIGPIPE - write to a closed pipe";
    case SIGTERM: return "SIGTERM - program terminated";
    default:      return "unknown signal";
    }
}
#else
const char *rn_signal_name(int sig) { (void)sig; return "signal"; }
#endif

#ifdef _WIN32
static const char *ntstatus_name(unsigned int c)
{
    switch (c) {
    case 0xC0000005u: return "ACCESS_VIOLATION - invalid memory access (array out of bounds or bad pointer)";
    case 0xC00000FDu: return "STACK_OVERFLOW - stack overflow (recursion too deep or large local arrays)";
    case 0xC0000094u: return "INTEGER_DIVIDE_BY_ZERO - division by zero";
    case 0xC000008Eu: return "FLOAT_DIVIDE_BY_ZERO - division by zero (floating point)";
    case 0xC0000095u: return "INTEGER_OVERFLOW - arithmetic overflow";
    case 0xC0000409u: return "STACK_BUFFER_OVERRUN - program aborted (abort or buffer overrun)";
    case 0xC0000017u: return "NO_MEMORY - not enough memory";
    case 0xC000012Du: return "COMMITMENT_LIMIT - memory limit reached";
    case 0xC000001Du: return "ILLEGAL_INSTRUCTION - illegal instruction";
    case 0xC0000374u: return "HEAP_CORRUPTION - heap corruption";
    case 0xC0000135u: return "DLL_NOT_FOUND - a DLL is missing (e.g. libstdc++-6.dll)";
    case 0xC0000139u: return "ENTRYPOINT_NOT_FOUND - incompatible DLL";
    case 0xC0000142u: return "DLL_INIT_FAILED - DLL initialization failed";
    case 0xC000013Au: return "CONTROL_C_EXIT - program interrupted";
    case 0xC0000420u: return "ASSERTION_FAILURE - failed assert";
    default:          return NULL;
    }
}

#endif

#if defined(_MSC_VER)
#define RN_TLS __declspec(thread)
#else
#define RN_TLS __thread
#endif

const char *rn_status_text(const rn_result *r)
{
    static RN_TLS char buf[160];
    if (!r->started) return r->error;
    if (r->timed_out) return "stopped at the time limit";
    if (r->cancelled) return "stopped by the user";
#ifndef _WIN32
    if (r->signal) {
        const char *n = rn_signal_name(r->signal);
        if (strcmp(n, "unknown signal") != 0) return n;
        snprintf(buf, sizeof buf, "signal %d", r->signal);
        return buf;
    }
#else
    if (r->win_status >= 0xC0000000u) {
        const char *n = ntstatus_name(r->win_status);
        if (n) return n;
        snprintf(buf, sizeof buf, "runtime error (code 0x%08X)", r->win_status);
        return buf;
    }
#endif
    snprintf(buf, sizeof buf, "exit code %d", r->exit_code);
    return buf;
}

/* === POSIX === */
#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

extern char **environ;

static double rn_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int is_exec_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode) && access(p, X_OK) == 0;
}

char *rn_find_program(const char *name)
{
    const char *path, *p;
    size_t nl;
    if (name == NULL || name[0] == '\0') return NULL;
    if (strchr(name, '/') != NULL) {
        char *rp;
        if (!is_exec_file(name)) return NULL;
        rp = realpath(name, NULL);
        return rp ? rp : strdup(name);
    }
    path = getenv("PATH");
    if (path == NULL || path[0] == '\0') path = "/usr/local/bin:/usr/bin:/bin";
    nl = strlen(name);
    p = path;
    for (;;) {
        const char *e = strchr(p, ':');
        size_t dl = e ? (size_t)(e - p) : strlen(p);
        char *cand = (char *)malloc(dl + nl + 3);
        if (cand == NULL) return NULL;
        if (dl == 0) { cand[0] = '.'; dl = 1; } else memcpy(cand, p, dl);
        cand[dl] = '/';
        memcpy(cand + dl + 1, name, nl + 1);
        if (is_exec_file(cand)) {
            if (cand[0] != '/') {
                char *rp = realpath(cand, NULL);
                if (rp) { free(cand); return rp; }
            }
            return cand;
        }
        free(cand);
        if (e == NULL) break;
        p = e + 1;
    }
    return NULL;
}

static int env_name_eq(const char *a, const char *b)
{
    while (*a && *a != '=' && *b && *b != '=') {
        if (*a != *b) return 0;
        a++; b++;
    }
    return (*a == '=' || *a == 0) && (*b == '=' || *b == 0);
}

static char **build_env(const char *const *add)
{
    size_t n = 0, na = 0, i, j, k = 0;
    char **env;
    while (environ && environ[n]) n++;
    while (add && add[na]) na++;
    env = (char **)calloc(n + na + 1, sizeof(char *));
    if (env == NULL) return NULL;
    for (i = 0; i < n; i++) {
        int skip = 0;
        for (j = 0; j < na; j++) if (env_name_eq(environ[i], add[j])) { skip = 1; break; }
        if (!skip) env[k++] = environ[i];
    }
    for (j = 0; j < na; j++) env[k++] = (char *)add[j];
    env[k] = NULL;
    return env;
}

static void set_limit(int res, long long v)
{
    struct rlimit rl;
    rlim_t want = (rlim_t)v;
    if (getrlimit(res, &rl) != 0) return;
    if (rl.rlim_max != RLIM_INFINITY && want > rl.rlim_max) want = rl.rlim_max;
    rl.rlim_cur = want;
    rl.rlim_max = want;
    if (setrlimit(res, &rl) != 0) {
        getrlimit(res, &rl);
        rl.rlim_cur = want;
        setrlimit(res, &rl);
    }
}

static int open_rd(const char *p)
{
    return open(p ? p : "/dev/null", O_RDONLY | O_CLOEXEC);
}

static int open_wr(const char *p)
{
    return open(p ? p : "/dev/null", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
}

int rn_run(const rn_spec *s, rn_result *r)
{
    char *prog;
    char **envp;
    int fin, fout, ferr, ep[2];
    pid_t pid;
    double t0, t1;
    int status = 0, killed = 0;
    struct rusage ru;

    memset(r, 0, sizeof *r);
    if (s->argv == NULL || s->argv[0] == NULL) {
        snprintf(r->error, sizeof r->error, "Empty command");
        return -1;
    }
    prog = rn_find_program(s->argv[0]);
    if (prog == NULL) {
        snprintf(r->error, sizeof r->error, "Program not found: %s", s->argv[0]);
        return -1;
    }
    fin = open_rd(s->stdin_path);
    if (fin < 0) {
        snprintf(r->error, sizeof r->error, "Cannot open the input %s: %s", s->stdin_path, strerror(errno));
        free(prog);
        return -1;
    }
    fout = open_wr(s->stdout_path);
    if (fout < 0) {
        snprintf(r->error, sizeof r->error, "Cannot create %s: %s", s->stdout_path ? s->stdout_path : "/dev/null", strerror(errno));
        close(fin);
        free(prog);
        return -1;
    }
    if (s->stderr_path && s->stdout_path && strcmp(s->stderr_path, s->stdout_path) == 0) ferr = fout;
    else ferr = open_wr(s->stderr_path);
    if (ferr < 0) {
        snprintf(r->error, sizeof r->error, "Cannot create %s: %s", s->stderr_path, strerror(errno));
        close(fin); close(fout);
        free(prog);
        return -1;
    }
    envp = build_env(s->env_add);
    if (envp == NULL || pipe(ep) != 0) {
        snprintf(r->error, sizeof r->error, "Not enough resources to start the program");
        close(fin); close(fout); if (ferr != fout) close(ferr);
        free(prog); free(envp);
        return -1;
    }
    fcntl(ep[0], F_SETFD, FD_CLOEXEC);
    fcntl(ep[1], F_SETFD, FD_CLOEXEC);

    t0 = rn_now_ms();
    pid = fork();
    if (pid < 0) {
        snprintf(r->error, sizeof r->error, "fork failed: %s", strerror(errno));
        close(ep[0]); close(ep[1]);
        close(fin); close(fout); if (ferr != fout) close(ferr);
        free(prog); free(envp);
        return -1;
    }
    if (pid == 0) {
        /* CHILD: ONLY ASYNC-SIGNAL-SAFE CALLS AFTER FORK */
        sigset_t all;
        int e;
        struct rlimit core;
        setpgid(0, 0);
        sigemptyset(&all);
        sigprocmask(SIG_SETMASK, &all, NULL);
        signal(SIGPIPE, SIG_DFL);
        if (s->cpu_s > 0) {
            struct rlimit rl;
            rl.rlim_cur = (rlim_t)s->cpu_s;
            rl.rlim_max = (rlim_t)s->cpu_s + 1;
            setrlimit(RLIMIT_CPU, &rl);
        }
        if (s->mem_kb > 0) set_limit(RLIMIT_AS, s->mem_kb * 1024LL);
        if (s->stack_kb > 0) set_limit(RLIMIT_STACK, s->stack_kb * 1024LL);
        if (s->fsize_kb > 0) set_limit(RLIMIT_FSIZE, s->fsize_kb * 1024LL);
        core.rlim_cur = core.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &core);
        if (dup2(fin, 0) < 0 || dup2(fout, 1) < 0 || dup2(ferr, 2) < 0) {
            e = errno;
            if (write(ep[1], "D", 1) < 0 || write(ep[1], &e, sizeof e) < 0) { /* IGNORE */ }
            _exit(127);
        }
        if (s->cwd && s->cwd[0] && chdir(s->cwd) != 0) {
            e = errno;
            if (write(ep[1], "C", 1) < 0 || write(ep[1], &e, sizeof e) < 0) { /* IGNORE */ }
            _exit(127);
        }
        execve(prog, (char *const *)s->argv, envp);
        e = errno;
        if (write(ep[1], "E", 1) < 0 || write(ep[1], &e, sizeof e) < 0) { /* IGNORE */ }
        _exit(127);
    }
    close(ep[1]);
    close(fin);
    close(fout);
    if (ferr != fout) close(ferr);
    r->started = 1;
    memset(&ru, 0, sizeof ru);
    for (;;) {
        double el;
        struct timespec ts;
        pid_t w = wait4(pid, &status, WNOHANG, &ru);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) break;
        el = rn_now_ms() - t0;
        if (!killed) {
            int cancel = s->cancel && *s->cancel;
            if (cancel || (s->wall_ms > 0 && el > (double)s->wall_ms)) {
                if (cancel) r->cancelled = 1; else r->timed_out = 1;
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                killed = 1;
            }
        }
        ts.tv_sec = 0;
        ts.tv_nsec = el < 20.0 ? 100000L : (el < 1000.0 ? 500000L : 2000000L);
        nanosleep(&ts, NULL);
    }
    t1 = rn_now_ms();
    kill(-pid, SIGKILL); /* KILL ANY LEFTOVER PROCESSES OF THE GROUP */
    r->wall_ms = t1 - t0;
    {
        char tag = 0;
        int e = 0;
        if (read(ep[0], &tag, 1) == 1 && read(ep[0], &e, sizeof e) == (ssize_t)sizeof e) {
            r->started = 0;
            snprintf(r->error, sizeof r->error, "%s: %s",
                     tag == 'C' ? "Cannot enter the working folder" :
                     tag == 'D' ? "Cannot redirect input/output" : "Cannot start the program",
                     strerror(e));
        }
    }
    close(ep[0]);
    r->cpu_ms = (double)(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000.0 +
                (double)(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000.0;
#if defined(__APPLE__)
    r->peak_kb = (long long)ru.ru_maxrss / 1024;
#else
    r->peak_kb = (long long)ru.ru_maxrss;
#endif
    if (WIFEXITED(status)) {
        r->exited = !killed;
        r->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        r->signal = WTERMSIG(status);
        if (r->signal == SIGXCPU) r->timed_out = 1;
    }
    free(prog);
    free(envp);
    return r->started ? 0 : -1;
}

/* === WINDOWS === */
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>
#include <psapi.h>
#include <wchar.h>
#include <wctype.h>

wchar_t *rn_u8_to_w(const char *s)
{
    int n;
    wchar_t *w;
    if (s == NULL) s = "";
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) n = 1;
    w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) <= 0) w[0] = 0;
    return w;
}

char *rn_w_to_u8(const wchar_t *w)
{
    int n;
    char *s;
    if (w == NULL) w = L"";
    n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) n = 1;
    s = (char *)malloc((size_t)n);
    if (s == NULL) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL) <= 0) s[0] = 0;
    return s;
}

/* WIDE CHAR BUFFER */
typedef struct { wchar_t *p; size_t n, cap; } wbuf;

static void wb_putn(wbuf *b, const wchar_t *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256;
        wchar_t *np;
        while (nc < b->n + n + 1) nc *= 2;
        np = (wchar_t *)realloc(b->p, nc * sizeof(wchar_t));
        if (np == NULL) return;
        b->p = np;
        b->cap = nc;
    }
    memcpy(b->p + b->n, s, n * sizeof(wchar_t));
    b->n += n;
    b->p[b->n] = 0;
}
static void wb_put(wbuf *b, const wchar_t *s) { wb_putn(b, s, wcslen(s)); }
static void wb_putc(wbuf *b, wchar_t c) { wb_putn(b, &c, 1); }

/* QUOTING COMPATIBLE WITH COMMANDLINETOARGVW / CRT */
static void append_quoted(wbuf *b, const wchar_t *a)
{
    const wchar_t *p;
    if (a[0] != 0 && wcspbrk(a, L" \t\n\v\"") == NULL) {
        wb_put(b, a);
        return;
    }
    wb_putc(b, L'"');
    for (p = a;; p++) {
        size_t bs = 0, i;
        while (*p == L'\\') { bs++; p++; }
        if (*p == 0) {
            for (i = 0; i < bs * 2; i++) wb_putc(b, L'\\');
            break;
        } else if (*p == L'"') {
            for (i = 0; i < bs * 2 + 1; i++) wb_putc(b, L'\\');
            wb_putc(b, L'"');
        } else {
            for (i = 0; i < bs; i++) wb_putc(b, L'\\');
            wb_putc(b, *p);
        }
    }
    wb_putc(b, L'"');
}

static int wfile_exists(const wchar_t *p)
{
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int has_ext(const wchar_t *name)
{
    const wchar_t *b = name, *p;
    for (p = name; *p; p++) if (*p == L'\\' || *p == L'/') b = p + 1;
    return wcschr(b, L'.') != NULL;
}

/* TRY DIR\NAME(+EXT); RETURN THE MALLOC'D PATH OR NULL */
static wchar_t *try_dir(const wchar_t *dir, const wchar_t *name)
{
    static const wchar_t *exts[] = { L".exe", L".com", L".bat", L".cmd", L"" };
    int i, ne = has_ext(name) ? 1 : 5;
    for (i = 0; i < ne; i++) {
        wbuf b = { 0 };
        const wchar_t *ext = has_ext(name) ? L"" : exts[i];
        if (dir && dir[0]) {
            wb_put(&b, dir);
            if (b.n && b.p[b.n - 1] != L'\\' && b.p[b.n - 1] != L'/') wb_putc(&b, L'\\');
        }
        wb_put(&b, name);
        wb_put(&b, ext);
        if (b.p && wfile_exists(b.p)) return b.p;
        free(b.p);
    }
    return NULL;
}

static wchar_t *search_path_w(const wchar_t *name, int skip_store, wchar_t **store_fallback)
{
    DWORD n = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t *path, *p, *found = NULL;
    if (n == 0) return NULL;
    path = (wchar_t *)malloc((n + 2) * sizeof(wchar_t));
    if (path == NULL) return NULL;
    GetEnvironmentVariableW(L"PATH", path, n + 1);
    p = path;
    while (p && *p) {
        wchar_t *e = wcschr(p, L';'), *c;
        size_t l;
        if (e) *e = 0;
        if (*p == L'"') p++;
        l = wcslen(p);
        if (l && p[l - 1] == L'"') p[l - 1] = 0;
        if (*p) {
            c = try_dir(p, name);
            if (c) {
                if (skip_store && wcsstr(c, L"\\WindowsApps\\") != NULL) {
                    if (store_fallback && *store_fallback == NULL) *store_fallback = c; else free(c);
                } else {
                    found = c;
                    break;
                }
            }
        }
        p = e ? e + 1 : NULL;
    }
    free(path);
    return found;
}

/* FIND PYTHON3* IN THE USUAL INSTALL FOLDERS */
static wchar_t *find_python_install(void)
{
    wchar_t base[3][MAX_PATH];
    int i;
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base[0], MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 20) base[0][0] = 0; else wcscat(base[0], L"\\Programs\\Python");
    wcscpy(base[1], L"C:");
    wcscpy(base[2], L"C:\\Program Files");
    for (i = 0; i < 3; i++) {
        WIN32_FIND_DATAW fd;
        HANDLE h;
        wchar_t pat[MAX_PATH + 20], best[MAX_PATH];
        best[0] = 0;
        if (!base[i][0]) continue;
        wcscpy(pat, base[i]);
        wcscat(pat, L"\\Python3*");
        h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(fd.cFileName, best) > 0 &&
                wcslen(fd.cFileName) < MAX_PATH) wcscpy(best, fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (best[0]) {
            wchar_t dir[MAX_PATH * 2 + 2];
            wchar_t *c;
            wcscpy(dir, base[i]);
            wcscat(dir, L"\\");
            wcscat(dir, best);
            c = try_dir(dir, L"python.exe");
            if (c) return c;
        }
    }
    return NULL;
}

char *rn_find_program(const char *name)
{
    static const wchar_t *extra[] = {
        L"C:\\OJI\\Codeblocks\\MinGW\\bin", L"D:\\OJI\\Codeblocks\\MinGW\\bin",
        L"C:\\OJI\\CodeBlocks\\MinGW\\bin", L"C:\\Program Files\\CodeBlocks\\MinGW\\bin",
        L"C:\\Program Files (x86)\\CodeBlocks\\MinGW\\bin", L"C:\\MinGW\\bin", L"C:\\mingw64\\bin",
        L"C:\\msys64\\ucrt64\\bin", L"C:\\msys64\\mingw64\\bin", L"C:\\TDM-GCC-64\\bin", L"C:\\TDM-GCC-32\\bin",
        L"C:\\Program Files\\Dev-Cpp\\MinGW64\\bin", L"C:\\Program Files (x86)\\Dev-Cpp\\MinGW64\\bin",
        NULL
    };
    wchar_t *w, *found = NULL, *store = NULL;
    char *res;
    int i, is_py;
    if (name == NULL || name[0] == 0) return NULL;
    w = rn_u8_to_w(name);
    if (w == NULL) return NULL;
    if (wcschr(w, L'\\') || wcschr(w, L'/') || (w[0] && w[1] == L':')) {
        found = try_dir(NULL, w);
        free(w);
        if (found == NULL) return NULL;
        res = rn_w_to_u8(found);
        free(found);
        return res;
    }
    is_py = _wcsicmp(w, L"python3") == 0 || _wcsicmp(w, L"python") == 0 ||
            _wcsicmp(w, L"python3.exe") == 0 || _wcsicmp(w, L"python.exe") == 0;
    found = search_path_w(w, is_py, &store);
    if (found == NULL && is_py) {
        found = search_path_w(L"python", 1, &store);
        if (found == NULL) found = search_path_w(L"py", 1, &store);
        if (found == NULL) found = find_python_install();
    }
    for (i = 0; found == NULL && extra[i]; i++) found = try_dir(extra[i], w);
    if (found == NULL) found = store; else free(store);
    free(w);
    if (found == NULL) return NULL;
    res = rn_w_to_u8(found);
    free(found);
    return res;
}

static void win_err(char *out, size_t cap, const char *prefix, DWORD code)
{
    wchar_t *msg = NULL;
    char *u8;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, code, 0, (LPWSTR)&msg, 0, NULL);
    u8 = rn_w_to_u8(msg ? msg : L"");
    if (u8) {
        size_t l = strlen(u8);
        while (l && (u8[l - 1] == '\n' || u8[l - 1] == '\r' || u8[l - 1] == ' ' || u8[l - 1] == '.')) u8[--l] = 0;
    }
    snprintf(out, cap, "%s: %s (code %lu)", prefix, u8 ? u8 : "", (unsigned long)code);
    free(u8);
    if (msg) LocalFree(msg);
}

static HANDLE open_handle(const char *p, int write, SECURITY_ATTRIBUTES *sa)
{
    HANDLE h;
    wchar_t *w = rn_u8_to_w(p ? p : "NUL");
    if (w == NULL) return INVALID_HANDLE_VALUE;
    if (!write)
        h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, sa,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    else
        h = CreateFileW(w, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, sa,
                        p ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(w);
    return h;
}

static int env_name_eq_w(const wchar_t *a, const wchar_t *b)
{
    while (*a && *a != L'=' && *b && *b != L'=') {
        if (towupper(*a) != towupper(*b)) return 0;
        a++; b++;
    }
    return (*a == L'=' || *a == 0) && (*b == L'=' || *b == 0);
}

static wchar_t *build_env_w(const char *const *add)
{
    wchar_t *cur = GetEnvironmentStringsW(), *p;
    wchar_t **wa = NULL;
    size_t na = 0, i;
    wbuf b = { 0 };
    while (add && add[na]) na++;
    if (na) {
        wa = (wchar_t **)calloc(na, sizeof(wchar_t *));
        for (i = 0; i < na; i++) wa[i] = rn_u8_to_w(add[i]);
    }
    for (p = cur; p && *p; p += wcslen(p) + 1) {
        int skip = 0;
        for (i = 0; i < na; i++) if (p[0] != L'=' && wa[i] && env_name_eq_w(p, wa[i])) { skip = 1; break; }
        if (!skip) { wb_put(&b, p); wb_putc(&b, 0); }
    }
    for (i = 0; i < na; i++) { if (wa[i]) { wb_put(&b, wa[i]); wb_putc(&b, 0); free(wa[i]); } }
    wb_putc(&b, 0);
    free(wa);
    if (cur) FreeEnvironmentStringsW(cur);
    return b.p;
}

static double ft_ms(FILETIME f)
{
    unsigned long long v = ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime;
    return (double)v / 10000.0;
}

int rn_run(const rn_spec *s, rn_result *r)
{
    char *prog;
    wchar_t *wprog = NULL, *wcwd = NULL, *envb = NULL;
    wbuf cmd = { 0 };
    SECURITY_ATTRIBUTES sa;
    HANDLE hin, hout, herr, job = NULL, hlist[3];
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    SIZE_T alsz = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST al = NULL;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
    DWORD flags, code = 0, nh = 0;
    LARGE_INTEGER freq, t0, t1;
    BOOL ok;
    int injob = 0, i, killed = 0;

    memset(r, 0, sizeof *r);
    if (s->argv == NULL || s->argv[0] == NULL) {
        snprintf(r->error, sizeof r->error, "Empty command");
        return -1;
    }
    prog = rn_find_program(s->argv[0]);
    if (prog == NULL) {
        snprintf(r->error, sizeof r->error, "Program not found: %s", s->argv[0]);
        return -1;
    }
    wprog = rn_u8_to_w(prog);
    append_quoted(&cmd, wprog);
    for (i = 1; s->argv[i]; i++) {
        wchar_t *wa = rn_u8_to_w(s->argv[i]);
        wb_putc(&cmd, L' ');
        append_quoted(&cmd, wa ? wa : L"");
        free(wa);
    }
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    hin = open_handle(s->stdin_path, 0, &sa);
    if (hin == INVALID_HANDLE_VALUE) {
        win_err(r->error, sizeof r->error, "Cannot open the input", GetLastError());
        free(prog); free(wprog); free(cmd.p);
        return -1;
    }
    hout = open_handle(s->stdout_path, 1, &sa);
    if (hout == INVALID_HANDLE_VALUE) {
        win_err(r->error, sizeof r->error, "Cannot create the output file", GetLastError());
        CloseHandle(hin);
        free(prog); free(wprog); free(cmd.p);
        return -1;
    }
    if (s->stderr_path && s->stdout_path && strcmp(s->stderr_path, s->stdout_path) == 0) herr = hout;
    else herr = open_handle(s->stderr_path, 1, &sa);
    if (herr == INVALID_HANDLE_VALUE) {
        win_err(r->error, sizeof r->error, "Cannot create the error file", GetLastError());
        CloseHandle(hin); CloseHandle(hout);
        free(prog); free(wprog); free(cmd.p);
        return -1;
    }
    hlist[nh++] = hin;
    hlist[nh++] = hout;
    if (herr != hout) hlist[nh++] = herr;

    memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdInput = hin;
    si.StartupInfo.hStdOutput = hout;
    si.StartupInfo.hStdError = herr;
    InitializeProcThreadAttributeList(NULL, 1, 0, &alsz);
    al = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, alsz);
    if (al && InitializeProcThreadAttributeList(al, 1, 0, &alsz) &&
        UpdateProcThreadAttribute(al, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hlist, nh * sizeof(HANDLE), NULL, NULL)) {
        si.lpAttributeList = al;
    }

    job = CreateJobObjectW(NULL, NULL);
    if (job) {
        memset(&li, 0, sizeof li);
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
        if (s->single_process) {
            li.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
            li.BasicLimitInformation.ActiveProcessLimit = 1;
        }
        if (s->mem_kb > 0) {
            li.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
            li.ProcessMemoryLimit = (SIZE_T)(s->mem_kb * 1024LL);
        }
        if (s->cpu_s > 0) {
            li.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME;
            li.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = s->cpu_s * 10000000LL;
        }
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    envb = build_env_w(s->env_add);
    if (s->cwd && s->cwd[0]) wcwd = rn_u8_to_w(s->cwd);
    flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_BREAKAWAY_FROM_JOB;
    if (si.lpAttributeList) flags |= EXTENDED_STARTUPINFO_PRESENT;
    memset(&pi, 0, sizeof pi);
    {
        size_t pl = wcslen(wprog);
        const wchar_t *app = (pl > 4 && _wcsicmp(wprog + pl - 4, L".exe") == 0) ? wprog : NULL;
        ok = CreateProcessW(app, cmd.p, NULL, NULL, TRUE, flags, envb, wcwd, &si.StartupInfo, &pi);
        if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
            flags &= ~(DWORD)CREATE_BREAKAWAY_FROM_JOB;
            ok = CreateProcessW(app, cmd.p, NULL, NULL, TRUE, flags, envb, wcwd, &si.StartupInfo, &pi);
        }
    }
    if (!ok) {
        win_err(r->error, sizeof r->error, "Cannot start the program", GetLastError());
    } else {
        r->started = 1;
        if (job && AssignProcessToJobObject(job, pi.hProcess)) injob = 1;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        ResumeThread(pi.hThread);
        for (;;) {
            double el;
            DWORD w = WaitForSingleObject(pi.hProcess, 5);
            if (w == WAIT_OBJECT_0) break;
            QueryPerformanceCounter(&t1);
            el = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
            if (!killed) {
                int cancel = s->cancel && *s->cancel;
                if (cancel || (s->wall_ms > 0 && el > (double)s->wall_ms)) {
                    if (cancel) r->cancelled = 1; else r->timed_out = 1;
                    if (injob) TerminateJobObject(job, 1);
                    TerminateProcess(pi.hProcess, 1);
                    killed = 1;
                    WaitForSingleObject(pi.hProcess, 5000);
                    break;
                }
            }
        }
        QueryPerformanceCounter(&t1);
        r->wall_ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        GetExitCodeProcess(pi.hProcess, &code);
        r->win_status = code;
        r->exit_code = (int)code;
        r->exited = !killed && code < 0xC0000000u;
        {
            FILETIME a, b, k, u;
            if (GetProcessTimes(pi.hProcess, &a, &b, &k, &u)) r->cpu_ms = ft_ms(k) + ft_ms(u);
        }
        {
            PROCESS_MEMORY_COUNTERS pmc;
            memset(&pmc, 0, sizeof pmc);
            pmc.cb = sizeof pmc;
            if (GetProcessMemoryInfo(pi.hProcess, &pmc, sizeof pmc)) {
                r->peak_kb = (long long)(pmc.PeakWorkingSetSize / 1024);
                r->peak_commit_kb = (long long)(pmc.PeakPagefileUsage / 1024);
            }
        }
        if (injob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION q;
            memset(&q, 0, sizeof q);
            if (QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &q, sizeof q, NULL)) {
                long long pk = (long long)(q.PeakProcessMemoryUsed / 1024);
                if (pk > r->peak_commit_kb) r->peak_commit_kb = pk;
            }
        }
        if (s->mem_kb > 0 && r->peak_commit_kb * 100 >= s->mem_kb * 95) r->mem_limit_hit = 1;
        if (s->cpu_s > 0 && r->cpu_ms >= (double)s->cpu_s * 1000.0) r->timed_out = 1;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (al) {
        DeleteProcThreadAttributeList(al);
        HeapFree(GetProcessHeap(), 0, al);
    }
    CloseHandle(hin);
    CloseHandle(hout);
    if (herr != hout) CloseHandle(herr);
    if (job) CloseHandle(job);
    free(envb);
    free(wcwd);
    free(wprog);
    free(cmd.p);
    free(prog);
    return r->started ? 0 : -1;
}
#endif
