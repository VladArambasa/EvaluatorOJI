/* UTIL.C - SEE UTIL.H */
/* MINIMUM WINDOWS API: WINDOWS 7 (OLD MINGW DEFAULTS TO XP/2003) */
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "util.h"
#include "runner.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

const char *os_name(void)
{
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "linux";
#endif
}

/* === MEMORY AND STRINGS === */
void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) { fputs("out of memory\n", stderr); exit(1); }
    return p;
}
void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) { fputs("out of memory\n", stderr); exit(1); }
    return q;
}
char *xstrdup(const char *s) { return xstrndup(s ? s : "", strlen(s ? s : "")); }
char *xstrndup(const char *s, size_t n)
{
    char *p = (char *)xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void sb_init(sb *b) { b->p = NULL; b->n = b->cap = 0; }
void sb_free(sb *b) { free(b->p); sb_init(b); }
void sb_putn(sb *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 64;
        while (nc < b->n + n + 1) nc *= 2;
        b->p = (char *)xrealloc(b->p, nc);
        b->cap = nc;
    }
    if (n) memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
void sb_puts(sb *b, const char *s) { if (s) sb_putn(b, s, strlen(s)); }
void sb_putc(sb *b, char c) { sb_putn(b, &c, 1); }
void sb_printf(sb *b, const char *fmt, ...)
{
    va_list ap, ap2;
    int n;
    char small[512];
    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    if ((size_t)n < sizeof small) sb_putn(b, small, (size_t)n);
    else {
        char *big = (char *)xmalloc((size_t)n + 1);
        vsnprintf(big, (size_t)n + 1, fmt, ap2);
        sb_putn(b, big, (size_t)n);
        free(big);
    }
    va_end(ap2);
}
char *sb_take(sb *b)
{
    char *p = b->p ? b->p : xstrdup("");
    sb_init(b);
    return p;
}
char *str_printf(const char *fmt, ...)
{
    va_list ap, ap2;
    int n;
    char *p;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return xstrdup(""); }
    p = (char *)xmalloc((size_t)n + 1);
    vsnprintf(p, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return p;
}
char *str_replace(const char *s, const char *a, const char *b)
{
    sb o;
    size_t la = strlen(a);
    sb_init(&o);
    if (!la) return xstrdup(s);
    while (*s) {
        const char *k = strstr(s, a);
        if (!k) { sb_puts(&o, s); break; }
        sb_putn(&o, s, (size_t)(k - s));
        sb_puts(&o, b);
        s = k + la;
    }
    return sb_take(&o);
}
char *str_lower(const char *s)
{
    char *p = xstrdup(s), *q;
    for (q = p; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
    return p;
}
static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
char *str_trim(const char *s)
{
    size_t n;
    while (*s && is_space((unsigned char)*s)) s++;
    n = strlen(s);
    while (n && is_space((unsigned char)s[n - 1])) n--;
    return xstrndup(s, n);
}
int str_starts(const char *s, const char *pre) { return strncmp(s, pre, strlen(pre)) == 0; }
int str_ends(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && memcmp(s + a - b, suf, b) == 0;
}

/* === STRING LISTS === */
void sv_init(svec *s) { s->v = NULL; s->n = s->cap = 0; }
void sv_free(svec *s)
{
    size_t i;
    for (i = 0; i < s->n; i++) free(s->v[i]);
    free(s->v);
    sv_init(s);
}
void sv_push_own(svec *s, char *x)
{
    if (s->n + 1 > s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->v = (char **)xrealloc(s->v, s->cap * sizeof(char *));
    }
    s->v[s->n++] = x;
}
void sv_push(svec *s, const char *x) { sv_push_own(s, xstrdup(x)); }
void sv_extend(svec *s, const svec *o) { size_t i; for (i = 0; i < o->n; i++) sv_push(s, o->v[i]); }
int sv_contains(const svec *s, const char *x)
{
    size_t i;
    for (i = 0; i < s->n; i++) if (strcmp(s->v[i], x) == 0) return 1;
    return 0;
}
static int cmp_nat(const void *a, const void *b) { return natcmp(*(char *const *)a, *(char *const *)b); }
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
void sv_sort_nat(svec *s) { if (s->n > 1) qsort(s->v, s->n, sizeof(char *), cmp_nat); }
void sv_sort_str(svec *s) { if (s->n > 1) qsort(s->v, s->n, sizeof(char *), cmp_str); }
svec split_cmd(const char *s)
{
    svec out;
    sb cur;
    int inq = 0, has = 0;
    sv_init(&out);
    sb_init(&cur);
    for (; s && *s; s++) {
        char ch = *s;
        if (ch == '"') { inq = !inq; has = 1; }
        else if ((ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') && !inq) {
            if (has) { sv_push_own(&out, sb_take(&cur)); has = 0; }
        } else { sb_putc(&cur, ch); has = 1; }
    }
    if (has) sv_push_own(&out, sb_take(&cur));
    sb_free(&cur);
    return out;
}
svec split_ws(const char *s)
{
    svec out;
    sv_init(&out);
    while (s && *s) {
        const char *b;
        while (*s && is_space((unsigned char)*s)) s++;
        b = s;
        while (*s && !is_space((unsigned char)*s)) s++;
        if (s > b) sv_push_own(&out, xstrndup(b, (size_t)(s - b)));
    }
    return out;
}
svec split_char(const char *s, char c, int skip_empty)
{
    svec out;
    const char *b = s;
    sv_init(&out);
    for (;;) {
        const char *e = strchr(b, c);
        size_t n = e ? (size_t)(e - b) : strlen(b);
        if (!(skip_empty && n == 0)) sv_push_own(&out, xstrndup(b, n));
        if (!e) break;
        b = e + 1;
    }
    return out;
}

/* === COMPARISONS === */
static int is_dig(char c) { return c >= '0' && c <= '9'; }
static char lo(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
int natcmp(const char *a, const char *b)
{
    size_t i = 0, j = 0, la = strlen(a), lb = strlen(b), ra, rb;
    while (i < la && j < lb) {
        if (is_dig(a[i]) && is_dig(b[j])) {
            size_t si = i, sj = j, na, nb;
            const char *pa, *pb;
            int c;
            while (i < la && is_dig(a[i])) i++;
            while (j < lb && is_dig(b[j])) j++;
            pa = a + si; na = i - si;
            pb = b + sj; nb = j - sj;
            while (na && *pa == '0') { pa++; na--; }
            while (nb && *pb == '0') { pb++; nb--; }
            if (na != nb) return na < nb ? -1 : 1;
            c = memcmp(pa, pb, na);
            if (c) return c < 0 ? -1 : 1;
        } else {
            char xa = lo(a[i]), xb = lo(b[j]);
            if (xa != xb) return (unsigned char)xa < (unsigned char)xb ? -1 : 1;
            i++;
            j++;
        }
    }
    ra = la - i;
    rb = lb - j;
    if (ra != rb) return ra < rb ? -1 : 1;
    {
        int c = strcmp(a, b);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
}
int glob_match(const char *pat, const char *name)
{
    size_t pi = 0, ni = 0, mark = 0, pl = strlen(pat), nl = strlen(name);
    long star = -1;
    while (ni < nl) {
        if (pi < pl && pat[pi] == '*') { star = (long)pi; mark = ni; pi++; }
        else if (pi < pl && (pat[pi] == '?' || lo(pat[pi]) == lo(name[ni]))) { pi++; ni++; }
        else if (star != -1) { pi = (size_t)star + 1; mark++; ni = mark; }
        else return 0;
    }
    while (pi < pl && pat[pi] == '*') pi++;
    return pi == pl;
}
static char *slashes(const char *s) { char *p = xstrdup(s), *q; for (q = p; *q; q++) if (*q == '\\') *q = '/'; return p; }
int path_match(const char *pat, const char *rel)
{
    char *ps = slashes(pat);
    svec pc = split_char(ps, '/', 1), rc = split_char(rel, '/', 0);
    int res = -1;
    free(ps);
    if (pc.n > 0 && pc.n <= rc.n) {
        size_t k = rc.n - pc.n, x;
        res = (int)k;
        for (x = 0; x < pc.n; x++) if (!glob_match(pc.v[x], rc.v[k + x])) { res = -1; break; }
    }
    sv_free(&pc);
    sv_free(&rc);
    return res;
}
int glob_full(const char *pat, const char *s)
{
    char *a = slashes(pat), *b = slashes(s);
    svec pc = split_char(a, '/', 1), sc = split_char(b, '/', 1);
    int ok = pc.n == sc.n;
    size_t i;
    for (i = 0; ok && i < pc.n; i++) if (!glob_match(pc.v[i], sc.v[i])) ok = 0;
    free(a);
    free(b);
    sv_free(&pc);
    sv_free(&sc);
    return ok;
}
char *ext_of(const char *name)
{
    const char *b = name, *p, *dot = NULL;
    for (p = name; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    for (p = b; *p; p++) if (*p == '.') dot = p;
    if (!dot || dot == b) return xstrdup("");
    return str_lower(dot);
}

/* === PATHS === */
int path_isabs(const char *p)
{
    if (!p || !*p) return 0;
#ifdef _WIN32
    if (p[0] == '\\' || p[0] == '/') return 1;
    return isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '\\' || p[2] == '/');
#else
    return p[0] == '/';
#endif
}
char *path_norm(const char *in)
{
    char *s = slashes(in), *rest = s, *out;
    sb prefix;
    int absolute = 0;
    svec comps, st;
    size_t i;
    sb_init(&prefix);
#ifdef _WIN32
    if (s[0] == '/' && s[1] == '/') {
        char *a = strchr(s + 2, '/');
        char *b = a ? strchr(a + 1, '/') : NULL;
        if (b) { sb_putn(&prefix, s, (size_t)(b - s)); rest = b + 1; }
        else { sb_puts(&prefix, s); rest = s + strlen(s); }
        sb_putc(&prefix, '/');
        absolute = 1;
    } else if (isalpha((unsigned char)s[0]) && s[1] == ':') {
        sb_putn(&prefix, s, 2);
        rest = s + 2;
        if (*rest == '/') { sb_putc(&prefix, '/'); rest++; absolute = 1; }
    } else if (s[0] == '/') { sb_putc(&prefix, '/'); rest = s + 1; absolute = 1; }
#else
    if (s[0] == '/') { sb_putc(&prefix, '/'); rest = s + 1; absolute = 1; }
#endif
    comps = split_char(rest, '/', 1);
    sv_init(&st);
    for (i = 0; i < comps.n; i++) {
        const char *c = comps.v[i];
        if (strcmp(c, ".") == 0) continue;
        if (strcmp(c, "..") == 0) {
            if (st.n && strcmp(st.v[st.n - 1], "..") != 0) { free(st.v[--st.n]); }
            else if (!absolute) sv_push(&st, "..");
        } else sv_push(&st, c);
    }
    for (i = 0; i < st.n; i++) {
        if (i) sb_putc(&prefix, '/');
        sb_puts(&prefix, st.v[i]);
    }
    if (prefix.n == 0) sb_putc(&prefix, '.');
    out = sb_take(&prefix);
#ifdef _WIN32
    {
        char *q;
        for (q = out; *q; q++) if (*q == '/') *q = '\\';
    }
#endif
    sv_free(&comps);
    sv_free(&st);
    free(s);
    return out;
}
char *path_abs(const char *p)
{
    char *cd, *t, *r;
    if (path_isabs(p)) return path_norm(p);
    cd = current_dir();
    t = str_printf("%s/%s", cd, p);
    r = path_norm(t);
    free(cd);
    free(t);
    return r;
}
char *path_resolve(const char *base, const char *pin)
{
    char *p = str_trim(pin ? pin : ""), *r;
    if (!*p) { free(p); return path_norm(base); }
    if (path_isabs(p)) r = path_norm(p);
    else { char *t = str_printf("%s/%s", base, p); r = path_norm(t); free(t); }
    free(p);
    return r;
}
char *path_join(const char *a, const char *b)
{
    char *t = str_printf("%s/%s", a, b), *r = path_norm(t);
    free(t);
    return r;
}
char *path_dir(const char *pin)
{
    char *p = path_norm(pin);
    char *k = strrchr(p, '/');
    char *k2 = strrchr(p, '\\');
    char *r;
    if (k2 && (!k || k2 > k)) k = k2;
    if (!k) {
        if (IS_WIN && strlen(p) >= 2 && p[1] == ':') { p[2] = 0; return p; }
        free(p);
        return xstrdup("");
    }
    if (k == p) r = xstrndup(p, 1);
    else if (IS_WIN && k == p + 2 && p[1] == ':') r = xstrndup(p, 3);
    else r = xstrndup(p, (size_t)(k - p));
    free(p);
    return r;
}
const char *path_base(const char *p)
{
    const char *b = p, *q;
    for (q = p; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}
char *path_rel(const char *p, const char *base)
{
    char *a = path_norm(p), *b = path_norm(base), *r;
    size_t lb = strlen(b);
    if (strlen(a) > lb && strncmp(a, b, lb) == 0 && (a[lb] == '/' || a[lb] == '\\')) {
        char *q;
        r = xstrdup(a + lb + 1);
        for (q = r; *q; q++) if (*q == '\\') *q = '/';
        free(a);
    } else r = a;
    free(b);
    return r;
}

/* === FILES === */
#ifdef _MSC_VER
static __declspec(thread) char g_err[512];
#else
static __thread char g_err[512];
#endif
const char *f_lasterr(void) { return g_err; }
static void set_err(void)
{
#ifdef _WIN32
    wchar_t *msg = NULL;
    char *u;
    DWORD code = GetLastError();
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, (LPWSTR)&msg, 0, NULL);
    u = rn_w_to_u8(msg ? msg : L"");
    if (u) {
        size_t l = strlen(u);
        while (l && (u[l - 1] == '\n' || u[l - 1] == '\r' || u[l - 1] == ' ' || u[l - 1] == '.')) u[--l] = 0;
        snprintf(g_err, sizeof g_err, "%s (code %lu)", u, (unsigned long)code);
        free(u);
    }
    if (msg) LocalFree(msg);
#else
    snprintf(g_err, sizeof g_err, "%s", strerror(errno));
#endif
}

#ifdef _WIN32
static int attrs(const char *p, WIN32_FILE_ATTRIBUTE_DATA *d)
{
    wchar_t *w = rn_u8_to_w(p);
    int ok = w && GetFileAttributesExW(w, GetFileExInfoStandard, d);
    free(w);
    return ok;
}
static long long ft_unix(FILETIME f)
{
    unsigned long long v = ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime;
    return (long long)(v / 10000000ULL) - 11644473600LL;
}
int f_isfile(const char *p) { WIN32_FILE_ATTRIBUTE_DATA d; return attrs(p, &d) && !(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY); }
int f_isdir(const char *p) { WIN32_FILE_ATTRIBUTE_DATA d; return attrs(p, &d) && (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY); }
int f_exists(const char *p) { WIN32_FILE_ATTRIBUTE_DATA d; return attrs(p, &d); }
long long f_size(const char *p)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!attrs(p, &d)) return 0;
    return ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}
long long f_mtime(const char *p) { WIN32_FILE_ATTRIBUTE_DATA d; return attrs(p, &d) ? ft_unix(d.ftLastWriteTime) : 0; }
static int mkdir1(const char *p)
{
    wchar_t *w = rn_u8_to_w(p);
    int ok = CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
    if (!ok) set_err();
    free(w);
    return ok;
}
int f_listdir(const char *p, dirlist *out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h;
    char *n = path_norm(p), *pat = str_printf("%s\\*", n);
    wchar_t *w = rn_u8_to_w(pat);
    free(n);
    free(pat);
    h = FindFirstFileW(w, &fd);
    free(w);
    if (h == INVALID_HANDLE_VALUE) { set_err(); return 0; }
    do {
        dirent_t e;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        e.name = rn_w_to_u8(fd.cFileName);
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = e.dir ? 0 : (((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
        e.mtime = ft_unix(fd.ftLastWriteTime);
        if (out->n + 1 > out->cap) { out->cap = out->cap ? out->cap * 2 : 16; out->v = (dirent_t *)xrealloc(out->v, out->cap * sizeof(dirent_t)); }
        out->v[out->n++] = e;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 1;
}
static FILE *u8open(const char *p, const char *mode)
{
    wchar_t *w = rn_u8_to_w(p), *m = rn_u8_to_w(mode);
    FILE *f = _wfopen(w, m);
    free(w);
    free(m);
    return f;
}
int f_copy(const char *a, const char *b)
{
    wchar_t *wa = rn_u8_to_w(a), *wb = rn_u8_to_w(b);
    int ok = CopyFileW(wa, wb, FALSE) != 0;
    if (!ok) set_err();
    free(wa);
    free(wb);
    return ok;
}
void f_remove(const char *p)
{
    wchar_t *w = rn_u8_to_w(p);
    DWORD at = GetFileAttributesW(w);
    if (at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY)) {
        if (at & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(w, at & ~(DWORD)FILE_ATTRIBUTE_READONLY);
        DeleteFileW(w);
    }
    free(w);
}
static void remove_tree(const char *p)
{
    dirlist d = {0};
    size_t i;
    wchar_t *w;
    if (f_listdir(p, &d)) {
        for (i = 0; i < d.n; i++) {
            char *c = str_printf("%s\\%s", p, d.v[i].name);
            wchar_t *wc = rn_u8_to_w(c);
            DWORD at = GetFileAttributesW(wc);
            if (d.v[i].dir && !(at & FILE_ATTRIBUTE_REPARSE_POINT)) remove_tree(c);
            else if (d.v[i].dir) RemoveDirectoryW(wc);
            else f_remove(c);
            free(wc);
            free(c);
        }
    }
    dl_free(&d);
    w = rn_u8_to_w(p);
    RemoveDirectoryW(w);
    free(w);
}
void f_cleardir(const char *p)
{
    dirlist d = {0};
    size_t i;
    char *n;
    if (!f_isdir(p)) { f_mkdirs(p); return; }
    if (!f_listdir(p, &d)) return;
    n = path_norm(p);
    for (i = 0; i < d.n; i++) {
        char *c = str_printf("%s\\%s", n, d.v[i].name);
        wchar_t *wc = rn_u8_to_w(c);
        DWORD at = GetFileAttributesW(wc);
        if (d.v[i].dir && !(at & FILE_ATTRIBUTE_REPARSE_POINT)) remove_tree(c);
        else if (d.v[i].dir) RemoveDirectoryW(wc);
        else f_remove(c);
        free(wc);
        free(c);
    }
    free(n);
    dl_free(&d);
}
#else
int f_isfile(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }
int f_isdir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
int f_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
long long f_size(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long long)st.st_size : 0; }
long long f_mtime(const char *p) { struct stat st; return stat(p, &st) == 0 ? (long long)st.st_mtime : 0; }
static int mkdir1(const char *p)
{
    if (mkdir(p, 0755) == 0 || errno == EEXIST) return 1;
    set_err();
    return 0;
}
int f_listdir(const char *p, dirlist *out)
{
    DIR *d = opendir(p);
    struct dirent *e;
    if (!d) { set_err(); return 0; }
    while ((e = readdir(d)) != NULL) {
        dirent_t x;
        struct stat st;
        char *full;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        x.name = xstrdup(e->d_name);
        full = str_printf("%s/%s", p, e->d_name);
        if (stat(full, &st) == 0) {
            x.dir = S_ISDIR(st.st_mode);
            x.size = x.dir ? 0 : (long long)st.st_size;
            x.mtime = (long long)st.st_mtime;
        } else { x.dir = 0; x.size = 0; x.mtime = 0; }
        free(full);
        if (out->n + 1 > out->cap) { out->cap = out->cap ? out->cap * 2 : 16; out->v = (dirent_t *)xrealloc(out->v, out->cap * sizeof(dirent_t)); }
        out->v[out->n++] = x;
    }
    closedir(d);
    return 1;
}
static FILE *u8open(const char *p, const char *mode) { return fopen(p, mode); }
int f_copy(const char *a, const char *b)
{
    FILE *in = fopen(a, "rb"), *out;
    char buf[65536];
    size_t n;
    int ok = 1;
    if (!in) { set_err(); return 0; }
    out = fopen(b, "wb");
    if (!out) { set_err(); fclose(in); return 0; }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, n, out) != n) { ok = 0; set_err(); break; }
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    return ok;
}
void f_remove(const char *p)
{
    struct stat st;
    if (lstat(p, &st) == 0 && !S_ISDIR(st.st_mode)) unlink(p);
}
static void remove_tree(const char *p)
{
    DIR *d = opendir(p);
    struct dirent *e;
    if (d) {
        while ((e = readdir(d)) != NULL) {
            struct stat st;
            char *c;
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            c = str_printf("%s/%s", p, e->d_name);
            if (lstat(c, &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(c);
            else unlink(c);
            free(c);
        }
        closedir(d);
    }
    rmdir(p);
}
void f_cleardir(const char *p)
{
    DIR *d;
    struct dirent *e;
    svec names;
    size_t i;
    if (!f_isdir(p)) { f_mkdirs(p); return; }
    d = opendir(p);
    if (!d) return;
    sv_init(&names);
    while ((e = readdir(d)) != NULL) if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) sv_push(&names, e->d_name);
    closedir(d);
    for (i = 0; i < names.n; i++) {
        struct stat st;
        char *c = str_printf("%s/%s", p, names.v[i]);
        if (lstat(c, &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(c);
        else unlink(c);
        free(c);
    }
    sv_free(&names);
}
#endif

int f_mkdirs(const char *pin)
{
    char *p = path_norm(pin), *parent;
    int ok;
    if (f_isdir(p)) { free(p); return 1; }
    parent = path_dir(p);
    if (*parent && strcmp(parent, p) != 0 && !f_isdir(parent)) f_mkdirs(parent);
    ok = mkdir1(p);
    free(parent);
    free(p);
    return ok;
}
void dl_free(dirlist *d)
{
    size_t i;
    for (i = 0; i < d->n; i++) free(d->v[i].name);
    free(d->v);
    d->v = NULL;
    d->n = d->cap = 0;
}
static int cmp_dirent(const void *a, const void *b) { return natcmp(((const dirent_t *)a)->name, ((const dirent_t *)b)->name); }
void dl_sort_nat(dirlist *d) { if (d->n > 1) qsort(d->v, d->n, sizeof(dirent_t), cmp_dirent); }
char *f_read(const char *p, long long limit, size_t *len)
{
    FILE *f = u8open(p, "rb");
    sb o;
    char buf[65536];
    if (!f) { set_err(); return NULL; }
    sb_init(&o);
    for (;;) {
        size_t want = sizeof buf, n;
        if (limit >= 0) {
            long long left = limit - (long long)o.n;
            if (left <= 0) break;
            if ((long long)want > left) want = (size_t)left;
        }
        n = fread(buf, 1, want, f);
        if (n == 0) break;
        sb_putn(&o, buf, n);
    }
    fclose(f);
    if (len) *len = o.n;
    return sb_take(&o);
}
int f_write(const char *p, const char *data, size_t len)
{
    char *d = path_dir(p);
    FILE *f;
    int ok;
    if (*d) f_mkdirs(d);
    free(d);
    f = u8open(p, "wb");
    if (!f) { set_err(); return 0; }
    ok = fwrite(data, 1, len, f) == len;
    if (fclose(f) != 0) ok = 0;
    if (!ok) set_err();
    return ok;
}

/* === ENVIRONMENT, TIME === */
char *env_get(const char *name)
{
#ifdef _WIN32
    wchar_t *w = rn_u8_to_w(name), *v;
    DWORD n = GetEnvironmentVariableW(w, NULL, 0);
    char *r;
    if (!n) { free(w); return xstrdup(""); }
    v = (wchar_t *)xmalloc((n + 1) * sizeof(wchar_t));
    GetEnvironmentVariableW(w, v, n + 1);
    r = rn_w_to_u8(v);
    free(v);
    free(w);
    return r;
#else
    const char *v = getenv(name);
    return xstrdup(v ? v : "");
#endif
}
char *home_dir(void)
{
#ifdef _WIN32
    char *h = env_get("USERPROFILE"), *r;
    if (!*h) {
        char *d = env_get("HOMEDRIVE"), *pth = env_get("HOMEPATH");
        free(h);
        h = str_printf("%s%s", d, pth);
        free(d);
        free(pth);
    }
    if (!*h) { free(h); return xstrdup("C:\\"); }
    r = path_norm(h);
    free(h);
    return r;
#else
    /* NO GETPWUID: THE LINUX BINARY IS STATIC AND HOME IS ALMOST ALWAYS SET */
    char *h = env_get("HOME"), *r;
    if (!*h) { free(h); return xstrdup("/"); }
    r = path_norm(h);
    free(h);
    return r;
#endif
}
char *current_dir(void)
{
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetCurrentDirectoryW(32768, buf);
    if (!n) return xstrdup(".");
    return rn_w_to_u8(buf);
#else
    char buf[8192];
    return xstrdup(getcwd(buf, sizeof buf) ? buf : ".");
#endif
}
char *exe_dir(void)
{
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(NULL, buf, 32768);
    char *u, *d;
    if (!n) return current_dir();
    buf[n] = 0;
    u = rn_w_to_u8(buf);
    d = path_dir(u);
    free(u);
    return d;
#elif defined(__APPLE__)
    char buf[4096], real[4096];
    uint32_t sz = sizeof buf;
    if (_NSGetExecutablePath(buf, &sz) == 0) return path_dir(realpath(buf, real) ? real : buf);
    return current_dir();
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) { buf[n] = 0; return path_dir(buf); }
    return current_dir();
#endif
}
double now_ms(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}
static char *fmt_now(const char *fmt)
{
    time_t t = time(NULL);
    struct tm tmv;
    char b[64];
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(b, sizeof b, fmt, &tmv);
    return xstrdup(b);
}
char *now_str(void) { return fmt_now("%Y-%m-%d %H:%M:%S"); }
char *stamp_str(void) { return fmt_now("%Y%m%d-%H%M%S"); }
char *clock_str(void) { return fmt_now("%H:%M:%S"); }
int cur_pid(void)
{
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}
void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

#ifndef _WIN32
static void reap_thread(void *arg) { int st; waitpid((pid_t)(long)arg, &st, 0); }
static void spawn_detached(const char *const *args)
{
    pid_t p = fork();
    if (p == 0) {
        int dn;
        setsid();
        dn = open("/dev/null", O_RDWR);
        if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); }
        execvp(args[0], (char *const *)args);
        _exit(127);
    }
    if (p > 0) thread_start(reap_thread, (void *)(long)p);
}
#endif
void open_path(const char *path)
{
#ifdef _WIN32
    wchar_t *w = rn_u8_to_w(path);
    ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
#elif defined(__APPLE__)
    const char *a[] = {"open", path, NULL};
    spawn_detached(a);
#else
    const char *a[] = {"xdg-open", path, NULL};
    spawn_detached(a);
#endif
}
void open_browser(const char *url)
{
#ifdef _WIN32
    const char *vars[] = {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"};
    const char *rel[] = {"\\Microsoft\\Edge\\Application\\msedge.exe", "\\Google\\Chrome\\Application\\chrome.exe"};
    int i, j;
    wchar_t *wurl;
    for (i = 0; i < 3; i++) {
        char *b = env_get(vars[i]);
        for (j = 0; *b && j < 2; j++) {
            char *c = str_printf("%s%s", b, rel[j]);
            if (f_isfile(c)) {
                char *args = str_printf("--app=%s --window-size=1400,900", url);
                wchar_t *wc = rn_u8_to_w(c), *wa = rn_u8_to_w(args);
                INT_PTR r = (INT_PTR)ShellExecuteW(NULL, L"open", wc, wa, NULL, SW_SHOWNORMAL);
                free(wc);
                free(wa);
                free(args);
                if (r > 32) { free(c); free(b); return; }
            }
            free(c);
        }
        free(b);
    }
    wurl = rn_u8_to_w(url);
    ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL);
    free(wurl);
#else
    char *app = str_printf("--app=%s", url);
    const char *names[] = {"google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge", NULL};
    int i;
#if defined(__APPLE__)
    const char *mac[] = {"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"};
    for (i = 0; i < 2; i++) if (f_isfile(mac[i])) { const char *a[] = {mac[i], app, "--window-size=1400,900", NULL}; spawn_detached(a); free(app); return; }
#endif
    for (i = 0; names[i]; i++) {
        char *p = rn_find_program(names[i]);
        if (p) {
            const char *a[] = {p, app, "--window-size=1400,900", NULL};
            spawn_detached(a);
            free(p);
            free(app);
            return;
        }
    }
    free(app);
    {
#if defined(__APPLE__)
        const char *a[] = {"open", url, NULL};
#else
        const char *a[] = {"xdg-open", url, NULL};
#endif
        spawn_detached(a);
    }
#endif
}

/* === THREADS === */
typedef struct { thread_fn fn; void *arg; } thr_box;
#ifdef _WIN32
static DWORD WINAPI thr_main(LPVOID p)
{
    thr_box b = *(thr_box *)p;
    free(p);
    b.fn(b.arg);
    return 0;
}
int thread_start(thread_fn fn, void *arg)
{
    thr_box *b = (thr_box *)xmalloc(sizeof *b);
    HANDLE h;
    b->fn = fn;
    b->arg = arg;
    h = CreateThread(NULL, 0, thr_main, b, 0, NULL);
    if (!h) { free(b); return 0; }
    CloseHandle(h);
    return 1;
}
void mutex_init(mutex_t *m) { CRITICAL_SECTION *cs = (CRITICAL_SECTION *)xmalloc(sizeof *cs); InitializeCriticalSection(cs); m->h = cs; }
void mutex_lock(mutex_t *m) { EnterCriticalSection((CRITICAL_SECTION *)m->h); }
void mutex_unlock(mutex_t *m) { LeaveCriticalSection((CRITICAL_SECTION *)m->h); }
#else
static void *thr_main(void *p)
{
    thr_box b = *(thr_box *)p;
    free(p);
    b.fn(b.arg);
    return NULL;
}
int thread_start(thread_fn fn, void *arg)
{
    thr_box *b = (thr_box *)xmalloc(sizeof *b);
    pthread_t t;
    pthread_attr_t at;
    int ok;
    b->fn = fn;
    b->arg = arg;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8 * 1024 * 1024);
    ok = pthread_create(&t, &at, thr_main, b) == 0;
    pthread_attr_destroy(&at);
    if (!ok) { free(b); return 0; }
    pthread_detach(t);
    return 1;
}
void mutex_init(mutex_t *m) { pthread_mutex_t *x = (pthread_mutex_t *)xmalloc(sizeof *x); pthread_mutex_init(x, NULL); m->h = x; }
void mutex_lock(mutex_t *m) { pthread_mutex_lock((pthread_mutex_t *)m->h); }
void mutex_unlock(mutex_t *m) { pthread_mutex_unlock((pthread_mutex_t *)m->h); }
#endif
