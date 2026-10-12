/*
 * engine.c — motorul de evaluare al variantei C (port fidel al implementării de referință din varianta Python,
 * prin varianta C++). Vezi docs/SPECIFICATIE.md.
 */
#include "engine.h"
#include "runner.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *VARIANT = "C";
const char *PROBE_C_SOURCE = "";
size_t PROBE_C_LEN = 0;
const char *PROBE_PY_SOURCE = "";
size_t PROBE_PY_LEN = 0;

/* =====================================================================================
 * Utilitare
 * ===================================================================================== */
/* „pool”: pointeri malloc eliberați împreună */
typedef struct { void **v; size_t n, cap; } pool;
static void *pool_add(pool *p, void *x)
{
    if (p->n + 1 > p->cap) {
        p->cap = p->cap ? p->cap * 2 : 32;
        p->v = (void **)xrealloc(p->v, p->cap * sizeof(void *));
    }
    p->v[p->n++] = x;
    return x;
}
#define PS(pl, x) ((char *)pool_add((pl), (x)))
static void pool_drain(pool *p)
{
    size_t i;
    for (i = 0; i < p->n; i++) free(p->v[i]);
    free(p->v);
    p->v = NULL;
    p->n = p->cap = 0;
}

static int is_dig(char c) { return c >= '0' && c <= '9'; }
static int is_sp(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
static double round2(double x) { return floor(x * 100.0 + 0.5) / 100.0; }
static double round3(double x) { return floor(x * 1000.0 + 0.5) / 1000.0; }
static double dmin(double a, double b) { return a < b ? a : b; }
static double dmax(double a, double b) { return a > b ? a : b; }
static char *fmt_int(double x) { return str_printf("%.0f", floor(x + 0.5)); }
static char *fmt_g(double x) { return str_printf("%g", x); }
static char *fmt_2(double x) { return str_printf("%.2f", x); }
static void rtrim_inplace(char *s) { size_t n = strlen(s); while (n && is_sp((unsigned char)s[n - 1])) s[--n] = 0; }
static char *ltrim_dup(const char *s) { while (*s && is_sp((unsigned char)*s)) s++; return xstrdup(s); }
static int eq(const char *a, const char *b) { return strcmp(a, b) == 0; }
static int eq_nocase_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}
static char *slashes_dup(const char *s) { char *p = xstrdup(s), *q; for (q = p; *q; q++) if (*q == '\\') *q = '/'; return p; }
static char *sv_join(const svec *v, const char *sep)
{
    sb o;
    size_t i;
    sb_init(&o);
    for (i = 0; i < v->n; i++) {
        if (i) sb_puts(&o, sep);
        sb_puts(&o, v->v[i]);
    }
    return sb_take(&o);
}
static svec sv_copy(const svec *o) { svec s; sv_init(&s); if (o) sv_extend(&s, o); return s; }

/* înlocuiește {cheie} cu valoare, în ordine; kv = cheie, valoare, ..., NULL */
static char *subst(const char *t, const char *const *kv)
{
    char *s = xstrdup(t ? t : "");
    size_t i;
    for (i = 0; kv && kv[i]; i += 2) {
        char *key = str_printf("{%s}", kv[i]);
        char *r = str_replace(s, key, kv[i + 1] ? kv[i + 1] : "");
        free(key);
        free(s);
        s = r;
    }
    return s;
}

static char *read_text_n(const char *p, long long limit, size_t *len)
{
    char *t = f_read(p, limit, len);
    if (!t) { if (len) *len = 0; return xstrdup(""); }
    return t;
}
static char *read_text(const char *p, long long limit) { return read_text_n(p, limit, NULL); }

/* ---------------------------------------------------------------- parcurgere în lățime */
typedef struct { char *rel, *abs; int dir, depth; } entry;
typedef struct { entry *v; size_t n, cap; } entries;
static void ents_push(entries *e, char *rel, char *abs, int dir, int depth)
{
    if (e->n + 1 > e->cap) {
        e->cap = e->cap ? e->cap * 2 : 64;
        e->v = (entry *)xrealloc(e->v, e->cap * sizeof(entry));
    }
    e->v[e->n].rel = rel;
    e->v[e->n].abs = abs;
    e->v[e->n].dir = dir;
    e->v[e->n].depth = depth;
    e->n++;
}
static void ents_free(entries *e)
{
    size_t i;
    for (i = 0; i < e->n; i++) { free(e->v[i].rel); free(e->v[i].abs); }
    free(e->v);
    e->v = NULL;
    e->n = e->cap = 0;
}
static entries walk_bfs(const char *top, int max_depth, const svec *ignore)
{
    entries out = {0}, queue = {0};
    size_t qi;
    ents_push(&queue, xstrdup(""), xstrdup(top), 1, 0);
    for (qi = 0; qi < queue.n; qi++) {
        dirlist dl = {0};
        size_t i;
        const char *qd = queue.v[qi].abs, *qrel = queue.v[qi].rel;
        int qdepth = queue.v[qi].depth;
        if (!f_listdir(qd, &dl)) { dl_free(&dl); continue; }
        dl_sort_nat(&dl);
        for (i = 0; i < dl.n; i++) {
            const dirent_t *e = &dl.v[i];
            char *ap = path_join(qd, e->name);
            char *rp = qrel[0] ? str_printf("%s/%s", qrel, e->name) : xstrdup(e->name);
            if (e->dir) {
                size_t g;
                int ign = 0;
                for (g = 0; ignore && g < ignore->n; g++) if (glob_match(ignore->v[g], e->name)) { ign = 1; break; }
                if (ign) { free(ap); free(rp); continue; }
                if (qdepth < max_depth) ents_push(&queue, xstrdup(rp), xstrdup(ap), 1, qdepth + 1);
                ents_push(&out, rp, ap, 1, qdepth);
            } else ents_push(&out, rp, ap, 0, qdepth);
        }
        dl_free(&dl);
    }
    ents_free(&queue);
    return out;
}

/* =====================================================================================
 * Valori implicite
 * ===================================================================================== */
static jval *lang_obj(const char *id, const char *name, const char *ext, const char *comp, const char *run)
{
    jval *v = j_obj();
    jset_s(v, "id", id);
    jset_s(v, "name", name);
    jset_s(v, "ext", ext);
    jset_s(v, "compile", comp);
    jset_s(v, "run", run);
    jset_b(v, "probe", 1);
    return v;
}
static jval *default_languages(void)
{
    jval *a = j_arr();
    j_push(a, lang_obj("c", "C", ".c", "gcc -O2 -Wall -std=c11 {sources} -o {exe} {libs}", "{exe}"));
    j_push(a, lang_obj("cpp", "C++", ".cpp .cc .cxx", "g++ -O2 -Wall -std=c++17 {sources} -o {exe} {libs}", "{exe}"));
    j_push(a, lang_obj("py", "Python", ".py", "", IS_WIN ? "python {main}" : "python3 {main}"));
    return a;
}
static jval *default_autolibs(void)
{
    static const char *rules[][3] = {
        {"Math", "math.h complex.h tgmath.h fenv.h", "-lm"}, {"Threads", "pthread.h threads.h", "-pthread"},
        {"Ncurses", "ncurses.h curses.h", "-lncurses"}, {"Readline", "readline/readline.h", "-lreadline"},
        {"SDL2", "SDL2/SDL.h", "-lSDL2"}, {"SDL2_image", "SDL2/SDL_image.h", "-lSDL2_image"}, {"SDL2_ttf", "SDL2/SDL_ttf.h", "-lSDL2_ttf"},
        {"SDL2_mixer", "SDL2/SDL_mixer.h", "-lSDL2_mixer"}, {"Raylib", "raylib.h", "-lraylib -lGL -lm -lpthread -ldl -lrt -lX11"},
        {"GLFW", "GLFW/glfw3.h", "-lglfw -lGL"}, {"GLEW", "GL/glew.h", "-lGLEW"}, {"OpenGL", "GL/*", "-lGL -lGLU -lglut"},
        {"wxWidgets", "wx/*", "$(wx-config --cxxflags --libs)"}, {"X11", "X11/Xlib.h", "-lX11"}, {"Zlib", "zlib.h", "-lz"},
        {"SQLite", "sqlite3.h", "-lsqlite3"}, {"Curl", "curl/curl.h", "-lcurl"}, {"OpenSSL", "openssl/*.h", "-lssl -lcrypto"},
        {"GMP", "gmp.h", "-lgmp"}, {"Dynamic loading", "dlfcn.h", "-ldl"}, {"Realtime", "mqueue.h aio.h", "-lrt"}, {"Crypt", "crypt.h", "-lcrypt"}};
    jval *a = j_arr();
    size_t i;
    for (i = 0; i < sizeof rules / sizeof rules[0]; i++) {
        jval *v = j_obj();
        jset_s(v, "name", rules[i][0]);
        jset_s(v, "headers", rules[i][1]);
        jset_s(v, "flags", rules[i][2]);
        j_push(a, v);
    }
    return a;
}
static const char *DEFAULT_IGNORE = ".* _* cmake-build-* CMakeFiles build bin obj out release debug __MACOSX node_modules venv __pycache__";
static jval *default_root_rules(void)
{
    static const char *pats[] = {"CMakeLists.txt", "*.cbp", "bash-scripts/compile.sh", "bash-scripts/build.sh", "compile.sh", "build.sh",
                                 "main.cpp", "main.c", "*.cpp", "*.c", "*.py"};
    jval *a = j_arr();
    size_t i;
    for (i = 0; i < sizeof pats / sizeof pats[0]; i++) {
        jval *v = j_obj();
        jset_s(v, "pattern", pats[i]);
        jset_n(v, "up", 0);
        j_push(a, v);
    }
    return a;
}

/* =====================================================================================
 * Configurarea unei probleme
 * ===================================================================================== */
typedef struct {
    const jval *ws, *prob;
    char *root, *name, *nr;
    const char *layout;
    const jval *languages, *autolibs, *root_rules;
    jval *languages_own, *autolibs_own, *root_rules_own;
    int autolibs_on;
    svec ignore;
    int keep_outputs, win_stack, nonzero_error, use_probe;
    double compile_timeout;
    const char *io_input, *io_output, *io_inpath, *io_outpath, *io_cwd, *run_in;
    int max_depth;
    svec main_patterns, size_ext;
    const char *compile_scope, *build_mode, *build_cmd, *build_exe, *size_scope, *compile_extra;
    double time_ms, hard_time, mem_mb, hard_mem, stack_mb, source_kb, output_mb;
    const char *time_mode;
    int calibrate;
    const char *cmp_mode, *checker, *protocol, *checker_args;
    int ignore_case;
    double epsilon;
    char *tests_dir, *work_dir, *results_dir;
    const char *exe_suffix;
    const char *v[7];    /* root, problem, nr */
    const char *pv[5];   /* problem, nr */
} cfg;

static void cfg_init(cfg *C, const jval *ws, const jval *prob)
{
    const jval *langs, *al, *opt, *io, *pj, *lim, *cmp, *rr;
    const char *r = gs(ws, "root", ""), *td;
    char *tmp;
    svec se;
    size_t i;
    memset(C, 0, sizeof *C);
    C->ws = ws;
    C->prob = prob;
    C->root = path_abs(r[0] ? r : ".");
    C->name = xstrdup(gs(prob, "name", ""));
    C->nr = str_printf("%lld", (long long)trunc(gn(prob, "nr", 1)));
    C->layout = gs(ws, "layout", "problem-student");
    langs = G(ws, "languages");
    if (j_is(langs, J_ARR) && j_len(langs)) C->languages = langs;
    else C->languages = C->languages_own = default_languages();
    al = G(ws, "autoLibs");
    C->autolibs_on = gb(al, "enabled", 1);
    if (G(al, "rules")->t == J_NULL) C->autolibs = C->autolibs_own = default_autolibs();
    else C->autolibs = G(al, "rules");
    C->ignore = split_ws(gs(ws, "ignoreDirs", DEFAULT_IGNORE));
    opt = G(ws, "options");
    C->keep_outputs = gb(opt, "keepOutputs", 1);
    C->win_stack = gb(opt, "windowsStackFlag", 1);
    C->compile_timeout = gn(opt, "compileTimeoutSec", 60);
    C->nonzero_error = gb(opt, "nonzeroExitIsError", 1);
    C->use_probe = gb(opt, "useProbe", 1);
    io = G(prob, "io");
    C->io_input = gs(io, "input", "file");
    C->io_output = gs(io, "output", "file");
    C->io_inpath = gs(io, "inputPath", "{problem}.in");
    C->io_outpath = gs(io, "outputPath", "{problem}.out");
    C->io_cwd = gs(io, "cwd", "");
    C->run_in = gs(io, "runIn", "sandbox");
    pj = G(prob, "project");
    rr = G(pj, "rootRules");
    if (j_is(rr, J_ARR) && j_len(rr)) C->root_rules = rr;
    else C->root_rules = C->root_rules_own = default_root_rules();
    C->max_depth = (int)trunc(gn(pj, "maxDepth", 6));
    C->main_patterns = split_ws(gs(pj, "mainPatterns", "main.cpp main.c {problem}.cpp {problem}.c *.cpp *.c *.py"));
    C->compile_scope = gs(pj, "compileScope", "main");
    C->build_mode = gs(pj, "buildMode", "command");
    C->build_cmd = gs(pj, "buildCommand", "auto");
    if (!C->build_cmd[0]) C->build_cmd = "auto";
    C->build_exe = gs(pj, "buildExe", "release/program");
    if (!C->build_exe[0]) C->build_exe = "release/program";
    C->size_scope = gs(pj, "sizeScope", "main");
    se = split_ws(gs(pj, "sizeExt", ".c .h .cpp .hpp .cc .cxx .hh .py"));
    sv_init(&C->size_ext);
    for (i = 0; i < se.n; i++) sv_push_own(&C->size_ext, str_lower(se.v[i]));
    sv_free(&se);
    C->compile_extra = gs(prob, "compileExtra", "");
    lim = G(prob, "limits");
    C->time_ms = gn(lim, "timeMs", 1000);
    C->time_mode = gs(lim, "timeMode", "cpu");
    C->hard_time = dmax(1.0, gn(lim, "hardTimeFactor", 2));
    C->mem_mb = gn(lim, "memMb", 64);
    C->hard_mem = dmax(1.0, gn(lim, "hardMemFactor", 2));
    C->stack_mb = gn(lim, "stackMb", 8);
    C->source_kb = gn(lim, "sourceKb", 0);
    C->output_mb = gn(lim, "outputMb", 64);
    C->calibrate = gb(lim, "calibrate", 1);
    cmp = G(prob, "compare");
    C->cmp_mode = gs(cmp, "mode", "lines");
    C->ignore_case = gb(cmp, "ignoreCase", 0);
    C->epsilon = gn(cmp, "epsilon", 1e-6);
    C->checker = gs(cmp, "checker", "");
    C->protocol = gs(cmp, "protocol", "oji");
    C->checker_args = gs(cmp, "args", "");
    C->v[0] = "root"; C->v[1] = C->root; C->v[2] = "problem"; C->v[3] = C->name; C->v[4] = "nr"; C->v[5] = C->nr; C->v[6] = NULL;
    C->pv[0] = "problem"; C->pv[1] = C->name; C->pv[2] = "nr"; C->pv[3] = C->nr; C->pv[4] = NULL;
    td = gs(prob, "testsDir", "");
    if (!td[0]) td = gs(ws, "testsDir", "{root}/{problem}/_teste");
    tmp = subst(td, C->v);
    C->tests_dir = path_resolve(C->root, tmp);
    free(tmp);
    tmp = subst(gs(ws, "workDir", "{root}/_lucru"), C->v);
    C->work_dir = path_resolve(C->root, tmp);
    free(tmp);
    tmp = subst(gs(ws, "resultsDir", "{root}/_rezultate"), C->v);
    C->results_dir = path_resolve(C->root, tmp);
    free(tmp);
    C->exe_suffix = IS_WIN ? ".exe" : "";
}
static void cfg_free(cfg *C)
{
    free(C->root);
    free(C->name);
    free(C->nr);
    j_free(C->languages_own);
    j_free(C->autolibs_own);
    j_free(C->root_rules_own);
    sv_free(&C->ignore);
    sv_free(&C->main_patterns);
    sv_free(&C->size_ext);
    free(C->tests_dir);
    free(C->work_dir);
    free(C->results_dir);
}
static svec lang_exts(const jval *L)
{
    svec raw = split_ws(gs(L, "ext", "")), out;
    size_t i;
    sv_init(&out);
    for (i = 0; i < raw.n; i++) sv_push_own(&out, str_lower(raw.v[i]));
    sv_free(&raw);
    return out;
}
static const jval *lang_for(const cfg *C, const char *filename)
{
    char *e = ext_of(filename);
    size_t i;
    const jval *found = NULL;
    if (e[0] && j_is(C->languages, J_ARR)) {
        for (i = 0; i < C->languages->len && !found; i++) {
            svec ex = lang_exts(C->languages->items[i]);
            if (sv_contains(&ex, e)) found = C->languages->items[i];
            sv_free(&ex);
        }
    }
    free(e);
    return found;
}
static svec all_exts(const cfg *C)
{
    svec out;
    size_t i;
    sv_init(&out);
    for (i = 0; j_is(C->languages, J_ARR) && i < C->languages->len; i++) {
        svec ex = lang_exts(C->languages->items[i]);
        sv_extend(&out, &ex);
        sv_free(&ex);
    }
    return out;
}
static int lang_compiled(const jval *L)
{
    const char *c = gs(L, "compile", "");
    for (; *c; c++) if (*c != ' ' && *c != '\t' && *c != '\r' && *c != '\n') return 1;
    return 0;
}
static const char *probe_mode(const cfg *C, const jval *L)
{
    if (!C->use_probe || !gb(L, "probe", 0)) return "";
    if (lang_compiled(L)) {
        if (eq(C->build_mode, "scripts")) return eq(os_name(), "linux") ? "preload" : "";
        return "c";
    }
    {
        svec ex = lang_exts(L);
        int py = sv_contains(&ex, ".py");
        sv_free(&ex);
        if (py) return "py";
    }
    return "";
}

/* =====================================================================================
 * Starea evaluării
 * ===================================================================================== */
typedef struct {
    mutex_t mu;
    int running;
    volatile int stopping;
    char *phase, *problem, *student, *test, *error;
    long done, total, rv;
    svec logs;
    jval *results;
    svec env_path_add;
    char *current_probe, *py_probe_dir, *preload_so, *run_dir_any, *student_now;
    svec cache_keys, cache_vals;
    int *cache_ok;
    int have_checker;
    svec checker_argv;
    char *checker_error;
} job_t;
static job_t JOB;
static int JOB_READY = 0;

static void set_str(char **dst, const char *v) { char *n = xstrdup(v ? v : ""); free(*dst); *dst = n; }
static void set_own(char **dst, char *v) { free(*dst); *dst = v; }

static void eng_init_once(void)
{
    if (JOB_READY) return;
    memset(&JOB, 0, sizeof JOB);
    mutex_init(&JOB.mu);
    JOB.phase = xstrdup("");
    JOB.problem = xstrdup("");
    JOB.student = xstrdup("");
    JOB.test = xstrdup("");
    JOB.error = xstrdup("");
    JOB.results = j_obj();
    JOB.current_probe = xstrdup("");
    JOB.py_probe_dir = xstrdup("");
    JOB.preload_so = xstrdup("");
    JOB.run_dir_any = xstrdup("");
    JOB.student_now = xstrdup("");
    JOB.checker_error = xstrdup("");
    sv_init(&JOB.logs);
    sv_init(&JOB.env_path_add);
    sv_init(&JOB.cache_keys);
    sv_init(&JOB.cache_vals);
    sv_init(&JOB.checker_argv);
    JOB_READY = 1;
}
void eng_init(void) { eng_init_once(); }

static void job_log(const char *m)
{
    char *c = clock_str();
    char *line = str_printf("%s %s", c, m);
    free(c);
    mutex_lock(&JOB.mu);
    sv_push_own(&JOB.logs, line);
    if (JOB.logs.n > 20000) {
        size_t i;
        for (i = 0; i < 10000; i++) free(JOB.logs.v[i]);
        memmove(JOB.logs.v, JOB.logs.v + 10000, (JOB.logs.n - 10000) * sizeof(char *));
        JOB.logs.n -= 10000;
    }
    mutex_unlock(&JOB.mu);
}
static void job_logf(const char *fmt, ...)
{
    va_list ap;
    sb b;
    sb_init(&b);
    va_start(ap, fmt);
    {
        char small[1024];
        va_list ap2;
        int n;
        va_copy(ap2, ap);
        n = vsnprintf(small, sizeof small, fmt, ap);
        if (n >= 0 && (size_t)n < sizeof small) sb_puts(&b, small);
        else if (n >= 0) {
            char *big = (char *)xmalloc((size_t)n + 1);
            vsnprintf(big, (size_t)n + 1, fmt, ap2);
            sb_puts(&b, big);
            free(big);
        }
        va_end(ap2);
    }
    va_end(ap);
    job_log(b.p ? b.p : "");
    sb_free(&b);
}
static void job_set(const char *field, const char *val)
{
    mutex_lock(&JOB.mu);
    if (eq(field, "phase")) set_str(&JOB.phase, val);
    else if (eq(field, "problem")) set_str(&JOB.problem, val);
    else if (eq(field, "student")) set_str(&JOB.student, val);
    else if (eq(field, "test")) set_str(&JOB.test, val);
    mutex_unlock(&JOB.mu);
}
static void job_add_done(long n)
{
    mutex_lock(&JOB.mu);
    JOB.done += n;
    mutex_unlock(&JOB.mu);
}
static void job_publish(const char *name, const jval *res)
{
    jval *copy = j_clone(res);
    mutex_lock(&JOB.mu);
    j_set(JOB.results, name, copy);
    JOB.rv++;
    mutex_unlock(&JOB.mu);
}
static char *run_dir(const cfg *C)
{
    char *a = path_join(C->work_dir, "run"), *b = path_join(a, C->name);
    free(a);
    return b;
}
static void cache_clear(void)
{
    sv_free(&JOB.cache_keys);
    sv_free(&JOB.cache_vals);
    free(JOB.cache_ok);
    JOB.cache_ok = NULL;
}

static svec probe_env(const cfg *C, const jval *L, const char *probe_file)
{
    svec env;
    const char *mode = probe_mode(C, L);
    sv_init(&env);
    sv_push_own(&env, str_printf("OLIEVAL_PROBE=%s", probe_file));
    if (eq(mode, "py") && JOB.py_probe_dir[0]) {
        char *old = env_get("PYTHONPATH");
        if (old[0]) sv_push_own(&env, str_printf("PYTHONPATH=%s%s%s", JOB.py_probe_dir, IS_WIN ? ";" : ":", old));
        else sv_push_own(&env, str_printf("PYTHONPATH=%s", JOB.py_probe_dir));
        free(old);
    }
    if (eq(mode, "preload") && JOB.preload_so[0]) {
        char *old = env_get("LD_PRELOAD");
        if (old[0]) sv_push_own(&env, str_printf("LD_PRELOAD=%s %s", JOB.preload_so, old));
        else sv_push_own(&env, str_printf("LD_PRELOAD=%s", JOB.preload_so));
        free(old);
    }
    sv_extend(&env, &JOB.env_path_add);
    return env;
}
static int probe_active(const cfg *C, const jval *L)
{
    const char *m = probe_mode(C, L);
    return m[0] && (!eq(m, "preload") || JOB.preload_so[0]);
}

const jval *eng_find_problem(const jval *ws, const char *name)
{
    const jval *ps = G(ws, "problems");
    size_t i;
    if (!j_is(ps, J_ARR)) return NULL;
    for (i = 0; i < ps->len; i++) if (eq(gs(ps->items[i], "name", ""), name)) return ps->items[i];
    return NULL;
}

static rn_result run_p(const svec *argv, const char *cwd, const char *in, const char *out, const char *err, const svec *env,
                       long long wall, int cancellable)
{
    rn_spec s;
    rn_result r;
    const char **av = (const char **)xmalloc((argv->n + 1) * sizeof(char *));
    const char **ev = NULL;
    size_t i;
    for (i = 0; i < argv->n; i++) av[i] = argv->v[i];
    av[argv->n] = NULL;
    if (env && env->n) {
        ev = (const char **)xmalloc((env->n + 1) * sizeof(char *));
        for (i = 0; i < env->n; i++) ev[i] = env->v[i];
        ev[env->n] = NULL;
    }
    memset(&s, 0, sizeof s);
    s.argv = av;
    s.cwd = (cwd && cwd[0]) ? cwd : NULL;
    s.stdin_path = (in && in[0]) ? in : NULL;
    s.stdout_path = (out && out[0]) ? out : NULL;
    s.stderr_path = (err && err[0]) ? err : NULL;
    s.env_add = ev;
    s.wall_ms = wall;
    s.cancel = cancellable ? &JOB.stopping : NULL;
    rn_run(&s, &r);
    free(av);
    free(ev);
    return r;
}
static char *status_text(const rn_result *r) { return xstrdup(rn_status_text(r)); }
static int run_ok(const rn_result *r) { return r->started && r->exited && r->exit_code == 0; }

typedef struct { int has_rss, has_stack, has_cpu; double rss, stack, cpu; } probe_t;
static probe_t read_probe(const char *p)
{
    probe_t pr;
    char *t;
    svec lines;
    size_t i;
    memset(&pr, 0, sizeof pr);
    t = f_read(p, 4096, NULL);
    if (!t) return pr;
    lines = split_char(t, '\n', 1);
    for (i = 0; i < lines.n; i++) {
        svec parts = split_ws(lines.v[i]);
        if (parts.n == 2) {
            char *end = NULL;
            double v = strtod(parts.v[1], &end);
            if (!(end && *end == 0)) v = 0;
            if (eq(parts.v[0], "rss_kb")) { pr.has_rss = 1; pr.rss = v; }
            else if (eq(parts.v[0], "stack_kb")) { pr.has_stack = 1; pr.stack = v; }
            else if (eq(parts.v[0], "cpu_ms")) { pr.has_cpu = 1; pr.cpu = v; }
        }
        sv_free(&parts);
    }
    sv_free(&lines);
    free(t);
    return pr;
}

/* =====================================================================================
 * Detectare
 * ===================================================================================== */
static svec list_tests_files(const char *d)
{
    entries es = walk_bfs(d, 1, NULL);
    svec out;
    size_t i;
    sv_init(&out);
    for (i = 0; i < es.n; i++) if (!es.v[i].dir) sv_push(&out, es.v[i].rel);
    ents_free(&es);
    return out;
}
static int match_n(const char *pattern, const char *rel, char **n)
{
    const char *k = strstr(pattern, "{n}"), *suf;
    size_t pre_n, suf_n, rl = strlen(rel), i;
    if (!k) {
        if (glob_full(pattern, rel)) { *n = xstrdup("1"); return 1; }
        return 0;
    }
    pre_n = (size_t)(k - pattern);
    suf = k + 3;
    suf_n = strlen(suf);
    if (rl <= pre_n + suf_n) return 0;
    if (!eq_nocase_n(rel, pattern, pre_n)) return 0;
    if (suf_n && !eq_nocase_n(rel + rl - suf_n, suf, suf_n)) return 0;
    for (i = pre_n; i < rl - suf_n; i++) if (rel[i] == '/') return 0;
    *n = xstrndup(rel + pre_n, rl - suf_n - pre_n);
    return 1;
}
static const char *CHECKER_NAMES[] = {"verif.cpp", "verif.c", "verif.py", "verif.exe", "verif", NULL};

typedef struct { char *key; jval *v; } keyed;
static int cmp_keyed(const void *a, const void *b) { return natcmp(((const keyed *)a)->key, ((const keyed *)b)->key); }

static jval *scan_tests(const cfg *C)
{
    jval *res = j_obj(), *arr;
    int ex = f_isdir(C->tests_dir);
    char *ip, *op;
    svec files;
    keyed *found = NULL;
    size_t nf = 0, i, k;
    jset_s(res, "testsDir", C->tests_dir);
    jset_b(res, "testsDirExists", ex);
    arr = j_set(res, "tests", j_arr());
    jset_s(res, "checker", "");
    if (!ex) return res;
    ip = subst(gs(C->prob, "inputPattern", "{n}-{problem}.in"), C->pv);
    op = subst(gs(C->prob, "okPattern", "{n}-{problem}.ok"), C->pv);
    files = list_tests_files(C->tests_dir);
    found = (keyed *)xmalloc((files.n + 1) * sizeof(keyed));
    for (i = 0; i < files.n; i++) {
        char *n = NULL, *ok, *pin, *pok;
        jval *t;
        int dup = 0;
        if (!match_n(ip, files.v[i], &n)) continue;
        for (k = 0; k < nf; k++) if (eq(found[k].key, n)) { dup = 1; break; }
        if (dup) { free(n); continue; }
        ok = str_replace(op, "{n}", n);
        pin = path_join(C->tests_dir, files.v[i]);
        pok = path_join(C->tests_dir, ok);
        t = j_obj();
        jset_s(t, "id", n);
        jset_s(t, "input", files.v[i]);
        jset_s(t, "ok", ok);
        jset_n(t, "inputSize", (double)f_size(pin));
        jset_b(t, "okExists", f_isfile(pok));
        jset_n(t, "okSize", (double)f_size(pok));
        found[nf].key = n;
        found[nf].v = t;
        nf++;
        free(ok);
        free(pin);
        free(pok);
    }
    qsort(found, nf, sizeof(keyed), cmp_keyed);
    for (i = 0; i < nf; i++) { j_push(arr, found[i].v); free(found[i].key); }
    free(found);
    for (i = 0; CHECKER_NAMES[i]; i++) {
        char *p = path_join(C->tests_dir, CHECKER_NAMES[i]);
        int is = f_isfile(p);
        free(p);
        if (is) { jset_s(res, "checker", CHECKER_NAMES[i]); break; }
    }
    sv_free(&files);
    free(ip);
    free(op);
    return res;
}

/* ---------------------------------------------------------------- elevi */
typedef struct { char *id, *dir, *file, *name; int exists, registered; svec warnings; } student;
typedef struct { student *v; size_t n, cap; } students;
static student *st_find(students *s, const char *id)
{
    size_t i;
    for (i = 0; i < s->n; i++) if (eq(s->v[i].id, id)) return &s->v[i];
    return NULL;
}
static student *st_add(students *s, const char *id)
{
    student *x;
    if (s->n + 1 > s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->v = (student *)xrealloc(s->v, s->cap * sizeof(student));
    }
    x = &s->v[s->n++];
    memset(x, 0, sizeof *x);
    x->id = xstrdup(id);
    x->dir = xstrdup("");
    x->file = xstrdup("");
    x->name = xstrdup("");
    sv_init(&x->warnings);
    return x;
}
static void st_free_all(students *s)
{
    size_t i;
    for (i = 0; i < s->n; i++) {
        free(s->v[i].id);
        free(s->v[i].dir);
        free(s->v[i].file);
        free(s->v[i].name);
        sv_free(&s->v[i].warnings);
    }
    free(s->v);
    s->v = NULL;
    s->n = s->cap = 0;
}
static int cmp_student(const void *a, const void *b) { return natcmp(((const student *)a)->id, ((const student *)b)->id); }

typedef struct { char kind; char *lit; } ftok;
static int match_flat_rec(const ftok *toks, size_t nt, size_t ti, const char *name, const char *low, size_t pos, char **cap_s, char **cap_e)
{
    size_t nl = strlen(name), end;
    char kind;
    if (ti == nt) return pos == nl;
    kind = toks[ti].kind;
    if (kind == 'L') {
        size_t ll = strlen(toks[ti].lit);
        if (pos + ll <= nl && strncmp(low + pos, toks[ti].lit, ll) == 0) return match_flat_rec(toks, nt, ti + 1, name, low, pos + ll, cap_s, cap_e);
        return 0;
    }
    for (end = pos + 1; end <= nl; end++) {
        char *piece = xstrndup(name + pos, end - pos);
        if (kind == 'E' && strchr(piece, '.')) { free(piece); break; }
        if (kind == 'S') set_own(cap_s, piece); else set_own(cap_e, piece);
        if (match_flat_rec(toks, nt, ti + 1, name, low, end, cap_s, cap_e)) return 1;
    }
    return 0;
}
static int match_flat(const char *pattern, const char *fname, char **sid, char **ext)
{
    ftok toks[64];
    size_t nt = 0, i = 0, pl = strlen(pattern), k;
    char *low, *cs = NULL, *ce = NULL;
    int ok;
    while (i < pl && nt < 64) {
        if (strncmp(pattern + i, "{student}", 9) == 0) { toks[nt].kind = 'S'; toks[nt].lit = NULL; nt++; i += 9; }
        else if (strncmp(pattern + i, "{ext}", 5) == 0) { toks[nt].kind = 'E'; toks[nt].lit = NULL; nt++; i += 5; }
        else {
            size_t j = i;
            char *lit;
            while (j < pl && strncmp(pattern + j, "{student}", 9) != 0 && strncmp(pattern + j, "{ext}", 5) != 0) j++;
            lit = xstrndup(pattern + i, j - i);
            toks[nt].kind = 'L';
            toks[nt].lit = str_lower(lit);
            free(lit);
            nt++;
            i = j;
        }
    }
    low = str_lower(fname);
    ok = match_flat_rec(toks, nt, 0, fname, low, 0, &cs, &ce) && cs && cs[0];
    if (ok) {
        *sid = cs;
        *ext = ce ? ce : xstrdup("");
    } else {
        free(cs);
        free(ce);
    }
    for (k = 0; k < nt; k++) free(toks[k].lit);
    free(low);
    return ok;
}
static char *normcase(const char *p) { return IS_WIN ? str_lower(p) : xstrdup(p); }

/* *err (malloc) la eroare */
static students discover_students(const cfg *C, char **err)
{
    students out = {0};
    svec order;
    const jval *sts = G(C->ws, "students");
    const jval **regv = NULL;
    size_t i;
    pool pl = {0};
    *err = NULL;
    sv_init(&order);
    if (j_is(sts, J_ARR)) {
        regv = (const jval **)xmalloc((sts->len + 1) * sizeof(jval *));
        for (i = 0; i < sts->len; i++) {
            char *sid = str_trim(gs(sts->items[i], "id", ""));
            if (sid[0] && !sv_contains(&order, sid)) { regv[order.n] = sts->items[i]; sv_push(&order, sid); }
            free(sid);
        }
    }
    if (eq(C->layout, "flat")) {
        char *fdir = PS(&pl, path_resolve(C->root, PS(&pl, subst(gs(C->ws, "flatDir", "{root}/{problem}/surse"), C->v))));
        char *pat = PS(&pl, subst(gs(C->ws, "flatPattern", "{student}{nr}.{ext}"), C->pv));
        svec exts = all_exts(C);
        dirlist dl = {0};
        f_listdir(fdir, &dl);
        dl_sort_nat(&dl);
        for (i = 0; i < dl.n; i++) {
            char *fp = path_join(fdir, dl.v[i].name), *sid = NULL, *ext = NULL;
            student *s;
            if (!f_isfile(fp)) { free(fp); continue; }
            if (!match_flat(pat, dl.v[i].name, &sid, &ext)) { free(fp); continue; }
            if (ext[0]) {
                char *le = str_lower(ext), *dot = str_printf(".%s", le);
                int okx = sv_contains(&exts, dot);
                free(le);
                free(dot);
                if (!okx) { free(fp); free(sid); free(ext); continue; }
            }
            s = st_find(&out, sid);
            if (s) {
                sv_push_own(&s->warnings, str_printf("Mai multe surse găsite; se folosește %s", path_base(s->file)));
            } else {
                s = st_add(&out, sid);
                set_str(&s->dir, fdir);
                s->exists = 1;
                set_str(&s->file, fp);
            }
            free(fp);
            free(sid);
            free(ext);
        }
        dl_free(&dl);
        sv_free(&exts);
        for (i = 0; i < order.n; i++) {
            if (!st_find(&out, order.v[i])) {
                student *s = st_add(&out, order.v[i]);
                set_str(&s->dir, fdir);
                s->exists = 0;
            }
        }
    } else {
        const char *kv[] = {"root", C->root, "problem", C->name, "nr", C->nr, NULL};
        char *t = PS(&pl, slashes_dup(PS(&pl, subst(gs(C->ws, "studentDir", "{root}/{problem}/{student}"), kv))));
        char *k = strstr(t, "{student}"), *base, *suffix;
        char *special[3];
        dirlist dl = {0};
        if (!k) {
            *err = xstrdup("Șablonul folderului de elev trebuie să conțină {student}");
            sv_free(&order);
            free(regv);
            pool_drain(&pl);
            return out;
        }
        base = PS(&pl, path_resolve(C->root, k > t ? PS(&pl, xstrndup(t, (size_t)(k - t))) : "."));
        suffix = k + 9;
        special[0] = PS(&pl, normcase(C->tests_dir));
        special[1] = PS(&pl, normcase(C->work_dir));
        special[2] = PS(&pl, normcase(C->results_dir));
        f_listdir(base, &dl);
        dl_sort_nat(&dl);
        for (i = 0; i < dl.n; i++) {
            const char *nm = dl.v[i].name;
            char *p, *np, *nc, *rel;
            int sp;
            student *s;
            if (!nm[0] || nm[0] == '.' || nm[0] == '_') continue;
            p = path_join(base, nm);
            np = path_norm(p);
            nc = normcase(np);
            sp = eq(nc, special[0]) || eq(nc, special[1]) || eq(nc, special[2]);
            if (!f_isdir(p) || sp) { free(p); free(np); free(nc); continue; }
            s = st_add(&out, nm);
            rel = str_printf("%s%s", nm, suffix);
            set_own(&s->dir, path_resolve(base, rel));
            s->exists = f_isdir(s->dir);
            free(rel);
            free(p);
            free(np);
            free(nc);
        }
        dl_free(&dl);
        for (i = 0; i < order.n; i++) {
            if (!st_find(&out, order.v[i])) {
                student *s = st_add(&out, order.v[i]);
                char *rel = str_printf("%s%s", order.v[i], suffix);
                set_own(&s->dir, path_resolve(base, rel));
                s->exists = f_isdir(s->dir);
                free(rel);
            }
        }
    }
    if (out.n > 1) qsort(out.v, out.n, sizeof(student), cmp_student);
    for (i = 0; i < out.n; i++) {
        size_t r;
        for (r = 0; r < order.n; r++) {
            if (eq(order.v[r], out.v[i].id)) {
                out.v[i].registered = 1;
                set_str(&out.v[i].name, gs(regv[r], "name", ""));
                break;
            }
        }
    }
    sv_free(&order);
    free(regv);
    pool_drain(&pl);
    return out;
}

static int has_main_func(const char *p)
{
    size_t n, i = 0;
    char *t = f_read(p, 2 * 1024 * 1024, &n);
    if (!t) return 0;
    for (;;) {
        char *k = NULL;
        size_t j;
        unsigned char before;
        /* căutare care tolerează octeți nuli */
        while (i + 4 <= n) {
            if (memcmp(t + i, "main", 4) == 0) { k = t + i; break; }
            i++;
        }
        if (!k) { free(t); return 0; }
        before = i > 0 ? (unsigned char)t[i - 1] : ' ';
        j = i + 4;
        while (j < n && (t[j] == ' ' || t[j] == '\t' || t[j] == '\r' || t[j] == '\n')) j++;
        if (!((before >= '0' && before <= '9') || (before >= 'A' && before <= 'Z') || (before >= 'a' && before <= 'z') || before >= 128 || before == '_') &&
            j < n && t[j] == '(') {
            free(t);
            return 1;
        }
        i += 4;
    }
}

static const char *OBJ_EXTS[] = {".c", ".cpp", ".cc", ".cxx", ".c++", NULL};
static int is_obj_ext(const char *path)
{
    char *e = ext_of(path);
    int i, r = 0;
    for (i = 0; OBJ_EXTS[i]; i++) if (eq(e, OBJ_EXTS[i])) { r = 1; break; }
    free(e);
    return r;
}

typedef struct {
    char *root, *main, *main_rel;
    const char *lang;
    const jval *lang_obj;
    svec sources, warnings;
    long long size_main, size_all, size_judged;
} info_t;
static void info_init(info_t *I)
{
    memset(I, 0, sizeof *I);
    I->root = xstrdup("");
    I->main = xstrdup("");
    I->main_rel = xstrdup("");
    I->lang = "";
    sv_init(&I->sources);
    sv_init(&I->warnings);
}
static void info_free(info_t *I)
{
    free(I->root);
    free(I->main);
    free(I->main_rel);
    sv_free(&I->sources);
    sv_free(&I->warnings);
}

static info_t detect_project(const cfg *C, const student *st)
{
    info_t info;
    entries es, fe;
    svec root_comps, files_rel, files_abs, lexts;
    size_t i, r;
    long main_idx = -1;
    const jval *L;
    info_init(&info);
    sv_extend(&info.warnings, &st->warnings);
    if (eq(C->layout, "flat")) {
        set_str(&info.root, st->dir);
        if (!st->file[0]) return info;
        set_str(&info.main, st->file);
        set_str(&info.main_rel, path_base(st->file));
        L = lang_for(C, st->file);
        if (L) { info.lang = gs(L, "id", ""); info.lang_obj = L; }
        sv_push(&info.sources, path_base(st->file));
        info.size_main = info.size_all = info.size_judged = f_size(st->file);
        return info;
    }
    es = walk_bfs(st->dir, C->max_depth, &C->ignore);
    sv_init(&root_comps);
    for (r = 0; j_is(C->root_rules, J_ARR) && r < C->root_rules->len; r++) {
        const jval *rule = C->root_rules->items[r];
        char *ps = subst(gs(rule, "pattern", ""), C->pv), *pat = str_trim(ps);
        int up, hit = 0;
        free(ps);
        if (!pat[0]) { free(pat); continue; }
        up = (int)trunc(gn(rule, "up", 0));
        for (i = 0; i < es.n; i++) {
            int k = path_match(pat, es.v[i].rel);
            if (k >= 0) {
                svec comps = split_char(es.v[i].rel, '/', 0);
                size_t keep = (size_t)k;
                if (up > 0) keep = (long)keep - up > 0 ? keep - (size_t)up : 0;
                while (comps.n > keep) free(comps.v[--comps.n]);
                sv_free(&root_comps);
                root_comps = comps;
                hit = 1;
                break;
            }
        }
        free(pat);
        if (hit) break;
    }
    ents_free(&es);
    if (root_comps.n == 0) set_own(&info.root, path_norm(st->dir));
    else {
        char *rc = sv_join(&root_comps, "/");
        set_own(&info.root, path_join(st->dir, rc));
        free(rc);
    }
    sv_free(&root_comps);
    fe = walk_bfs(info.root, C->max_depth, &C->ignore);
    sv_init(&files_rel);
    sv_init(&files_abs);
    for (i = 0; i < fe.n; i++) if (!fe.v[i].dir) { sv_push(&files_rel, fe.v[i].rel); sv_push(&files_abs, fe.v[i].abs); }
    ents_free(&fe);
    for (r = 0; r < C->main_patterns.n; r++) {
        char *pat = subst(C->main_patterns.v[r], C->pv);
        long *m = (long *)xmalloc((files_rel.n + 1) * sizeof(long));
        size_t nm = 0, x;
        for (i = 0; i < files_rel.n; i++) if (path_match(pat, files_rel.v[i]) >= 0) m[nm++] = (long)i;
        free(pat);
        if (!nm) { free(m); continue; }
        for (x = 0; x < nm; x++) {
            char *e = ext_of(files_rel.v[m[x]]);
            int py = eq(e, ".py");
            free(e);
            if (py || has_main_func(files_abs.v[m[x]])) { main_idx = m[x]; break; }
        }
        if (main_idx < 0) main_idx = m[0];
        if (nm > 1) sv_push_own(&info.warnings, str_printf("Mai multe fișiere posibile (%lu); ales: %s", (unsigned long)nm, files_rel.v[main_idx]));
        free(m);
        break;
    }
    if (main_idx < 0) {
        sv_push_own(&info.warnings, str_printf("Nu am găsit fișierul principal (main) în %s", info.root));
        sv_free(&files_rel);
        sv_free(&files_abs);
        return info;
    }
    set_str(&info.main, files_abs.v[main_idx]);
    set_str(&info.main_rel, files_rel.v[main_idx]);
    L = lang_for(C, files_rel.v[main_idx]);
    if (!L) {
        sv_push_own(&info.warnings, str_printf("Limbaj necunoscut pentru %s", files_rel.v[main_idx]));
        sv_free(&files_rel);
        sv_free(&files_abs);
        return info;
    }
    info.lang = gs(L, "id", "");
    info.lang_obj = L;
    lexts = lang_exts(L);
    if (eq(C->build_mode, "objects")) {
        for (i = 0; i < files_rel.n; i++) if (is_obj_ext(files_rel.v[i]) && lang_for(C, files_rel.v[i])) sv_push(&info.sources, files_rel.v[i]);
    } else if (eq(C->build_mode, "scripts")) {
        for (i = 0; i < files_rel.n; i++) if (is_obj_ext(files_rel.v[i])) sv_push(&info.sources, files_rel.v[i]);
    } else if (eq(C->compile_scope, "all")) {
        for (i = 0; i < files_rel.n; i++) {
            char *e = ext_of(files_rel.v[i]);
            if (sv_contains(&lexts, e)) sv_push(&info.sources, files_rel.v[i]);
            free(e);
        }
    } else sv_push(&info.sources, files_rel.v[main_idx]);
    if (eq(C->build_mode, "objects") || eq(C->build_mode, "scripts") || eq(C->compile_scope, "all")) sv_sort_str(&info.sources);
    info.size_main = f_size(files_abs.v[main_idx]);
    for (i = 0; i < files_rel.n; i++) {
        char *e = ext_of(files_rel.v[i]);
        if (sv_contains(&C->size_ext, e)) info.size_all += f_size(files_abs.v[i]);
        free(e);
    }
    info.size_judged = eq(C->size_scope, "all") ? info.size_all : info.size_main;
    sv_free(&lexts);
    sv_free(&files_rel);
    sv_free(&files_abs);
    return info;
}

static jval *str_arr(const svec *v)
{
    jval *a = j_arr();
    size_t i;
    for (i = 0; i < v->n; i++) j_push(a, j_str(v->v[i]));
    return a;
}
static jval *public_student(const student *st, const info_t *info)
{
    jval *o = j_obj();
    jset_s(o, "id", st->id);
    jset_s(o, "name", st->name);
    jset_s(o, "dir", st->dir);
    jset_b(o, "exists", st->exists);
    jset_b(o, "registered", st->registered);
    jset_s(o, "root", info->root);
    jset_s(o, "main", info->main_rel);
    jset_s(o, "lang", info->lang);
    j_set(o, "sources", str_arr(&info->sources));
    jset_n(o, "sizeMain", (double)info->size_main);
    jset_n(o, "sizeAll", (double)info->size_all);
    jset_n(o, "sizeJudged", (double)info->size_judged);
    j_set(o, "warnings", str_arr(&info->warnings));
    return o;
}

/* =====================================================================================
 * Compilare
 * ===================================================================================== */
static void add_unique(svec *s, const char *x) { if (!sv_contains(s, x)) sv_push(s, x); }

static svec collect_includes(const svec *paths)
{
    svec names;
    size_t pi;
    sv_init(&names);
    for (pi = 0; pi < paths->n; pi++) {
        size_t len, i = 0;
        char *text = f_read(paths->v[pi], 4 * 1024 * 1024, &len);
        if (!text) continue;
        while (i <= len) {
            size_t j = i, k;
            const char *line;
            size_t ll;
            while (j < len && text[j] != '\r' && text[j] != '\n') j++;
            line = text + i;
            ll = j - i;
            i = j + 1;
            k = 0;
#define SKIPWS() while (k < ll && (line[k] == ' ' || line[k] == '\t' || line[k] == '\f' || line[k] == '\v')) k++
            SKIPWS();
            if (k >= ll || line[k] != '#') continue;
            k++;
            SKIPWS();
            if (ll - k < 7 || strncmp(line + k, "include", 7) != 0) continue;
            k += 7;
            SKIPWS();
#undef SKIPWS
            if (k < ll && line[k] == '<') {
                size_t e = k + 1;
                while (e < ll && line[e] != '>') e++;
                if (e < ll && e > k + 1) {
                    char *raw = xstrndup(line + k + 1, e - k - 1), *n = str_trim(raw);
                    add_unique(&names, n);
                    free(raw);
                    free(n);
                }
            }
        }
        free(text);
    }
    return names;
}
static long cache_find(const char *cmd)
{
    size_t i;
    for (i = 0; i < JOB.cache_keys.n; i++) if (eq(JOB.cache_keys.v[i], cmd)) return (long)i;
    return -1;
}
static svec expand_flags(const char *text)
{
    svec out;
    size_t i = 0, n = strlen(text);
    sv_init(&out);
    while (i < n) {
        if ((text[i] == '$' && text[i + 1] == '(') || text[i] == '`') {
            int dollar = text[i] == '$';
            const char *start = text + i + (dollar ? 2 : 1);
            const char *end = strchr(start, dollar ? ')' : '`');
            char *cmd;
            long ci;
            if (!end) break;
            cmd = xstrndup(start, (size_t)(end - start));
            ci = cache_find(cmd);
            if (ci < 0) {
                char *tmp, *val;
                svec argv;
                rn_result r;
                int ok;
                f_mkdirs(JOB.run_dir_any);
                tmp = path_join(JOB.run_dir_any, "_cmd.txt");
                argv = split_cmd(cmd);
                r = run_p(&argv, "", "", tmp, "", NULL, 20000, 0);
                ok = run_ok(&r);
                val = ok ? read_text(tmp, 100000) : xstrdup("");
                rtrim_inplace(val);
                sv_push(&JOB.cache_keys, cmd);
                sv_push_own(&JOB.cache_vals, val);
                JOB.cache_ok = (int *)xrealloc(JOB.cache_ok, JOB.cache_keys.n * sizeof(int));
                JOB.cache_ok[JOB.cache_keys.n - 1] = ok;
                ci = (long)JOB.cache_keys.n - 1;
                if (!ok) {
                    char *st = status_text(&r);
                    job_logf("  Atenție: comanda „%s” a eșuat (%s); opțiunile ei nu se adaugă", cmd, st);
                    free(st);
                }
                sv_free(&argv);
                free(tmp);
            }
            if (JOB.cache_ok[ci]) {
                svec f = split_cmd(JOB.cache_vals.v[ci]);
                sv_extend(&out, &f);
                sv_free(&f);
            }
            free(cmd);
            i = (size_t)(end - text) + 1;
        } else {
            size_t j = i;
            char *piece;
            svec f;
            while (j < n && !(text[j] == '$' && text[j + 1] == '(') && text[j] != '`') j++;
            piece = xstrndup(text + i, j - i);
            f = split_cmd(piece);
            sv_extend(&out, &f);
            sv_free(&f);
            free(piece);
            i = j;
        }
    }
    return out;
}
static svec compile_only_flags(const svec *flags)
{
    svec out;
    size_t i;
    sv_init(&out);
    for (i = 0; flags && i < flags->n; i++) {
        const char *f = flags->v[i];
        if (!(str_starts(f, "-l") || str_starts(f, "-L") || str_starts(f, "-Wl,"))) sv_push(&out, f);
    }
    return out;
}
static svec auto_lib_flags(const cfg *C, const char *root, const svec *sources)
{
    svec flags, paths, names;
    entries es;
    size_t i, r;
    sv_init(&flags);
    if (!C->autolibs_on) return flags;
    paths = sv_copy(sources);
    es = walk_bfs(root, C->max_depth, &C->ignore);
    for (i = 0; i < es.n; i++) {
        char *x = ext_of(es.v[i].rel);
        if (!es.v[i].dir && (eq(x, ".h") || eq(x, ".hpp") || eq(x, ".hh") || eq(x, ".hxx"))) sv_push(&paths, es.v[i].abs);
        free(x);
    }
    ents_free(&es);
    names = collect_includes(&paths);
    for (r = 0; j_is(C->autolibs, J_ARR) && r < C->autolibs->len; r++) {
        const jval *rule = C->autolibs->items[r];
        svec hp = split_ws(gs(rule, "headers", ""));
        int hit = 0;
        size_t a, b;
        for (a = 0; a < hp.n && !hit; a++)
            for (b = 0; b < names.n; b++) if (glob_full(hp.v[a], names.v[b])) { hit = 1; break; }
        if (hit) {
            svec f = expand_flags(gs(rule, "flags", ""));
            for (a = 0; a < f.n; a++) add_unique(&flags, f.v[a]);
            sv_free(&f);
        }
        sv_free(&hp);
    }
    sv_free(&names);
    sv_free(&paths);
    return flags;
}
static svec build_argv(const char *tmpl, const svec *sources, const svec *libs, const char *exe, const char *main_f, long long stack_bytes,
                       const char *problem)
{
    svec out, toks = split_cmd(tmpl);
    size_t i;
    char stack[32];
    snprintf(stack, sizeof stack, "%lld", stack_bytes);
    sv_init(&out);
    for (i = 0; i < toks.n; i++) {
        const char *tok = toks.v[i];
        if (eq(tok, "{sources}")) { if (sources) sv_extend(&out, sources); }
        else if (eq(tok, "{libs}")) { if (libs) sv_extend(&out, libs); }
        else {
            char *a = str_replace(tok, "{exe}", exe), *b = str_replace(a, "{main}", main_f), *c = str_replace(b, "{stack}", stack);
            sv_push_own(&out, str_replace(c, "{problem}", problem));
            free(a);
            free(b);
            free(c);
        }
    }
    sv_free(&toks);
    return out;
}
static char *cmd_string(const svec *argv)
{
    sb s;
    size_t i;
    sb_init(&s);
    for (i = 0; i < argv->n; i++) {
        if (i) sb_putc(&s, ' ');
        if (strchr(argv->v[i], ' ') || !argv->v[i][0]) sb_printf(&s, "\"%s\"", argv->v[i]);
        else sb_puts(&s, argv->v[i]);
    }
    return sb_take(&s);
}

typedef struct { int ok; char *exe, *log, *cmd; double ms; } compiled;
static compiled comp_new(void)
{
    compiled c;
    c.ok = 0;
    c.exe = xstrdup("");
    c.log = xstrdup("");
    c.cmd = xstrdup("");
    c.ms = 0;
    return c;
}
static void comp_free(compiled *c) { free(c->exe); free(c->log); free(c->cmd); }

static char *fresh_exe(const char *out_dir, const cfg *C)
{
    char *nm = str_printf("prog%s", C->exe_suffix), *exe = path_join(out_dir, nm);
    free(nm);
    if (f_exists(exe)) {
        f_remove(exe);
        if (f_exists(exe)) {
            free(exe);
            nm = str_printf("prog-%lld%s", (long long)now_ms() % 100000, C->exe_suffix);
            exe = path_join(out_dir, nm);
            free(nm);
        }
    }
    return exe;
}
static compiled compile_program(const cfg *C, const jval *L, const svec *sources_abs, const char *main_abs, const char *out_dir,
                                const char *cwd, int include_probe, const svec *extra_libs)
{
    compiled c = comp_new();
    char *exe, *log_path, *log, *st;
    svec srcs, libs, extra, argv;
    long long stack_bytes;
    rn_result r;
    sb lg;
    f_mkdirs(out_dir);
    if (!lang_compiled(L)) { c.ok = 1; return c; }
    exe = fresh_exe(out_dir, C);
    srcs = sv_copy(sources_abs);
    if (include_probe && eq(probe_mode(C, L), "c")) sv_push(&srcs, JOB.current_probe);
    libs = sv_copy(extra_libs);
    stack_bytes = (long long)(C->stack_mb * 1024 * 1024);
    if (IS_WIN && C->win_stack && C->stack_mb > 0) sv_push_own(&libs, str_printf("-Wl,--stack=%lld", stack_bytes));
    extra = split_cmd(C->compile_extra);
    sv_extend(&libs, &extra);
    sv_free(&extra);
    argv = build_argv(gs(L, "compile", ""), &srcs, &libs, exe, main_abs, stack_bytes, C->name);
    log_path = path_join(out_dir, "compilare.txt");
    r = run_p(&argv, cwd, "", log_path, log_path, &JOB.env_path_add, (long long)(C->compile_timeout * 1000), 1);
    log = read_text(log_path, 20000);
    c.ok = run_ok(&r) && f_isfile(exe);
    sb_init(&lg);
    if (!r.started) { sb_printf(&lg, "%s\n%s", r.error, log); set_own(&log, sb_take(&lg)); }
    else if (r.timed_out) { sb_printf(&lg, "Compilarea a depășit %lld s\n%s", (long long)C->compile_timeout, log); set_own(&log, sb_take(&lg)); }
    else if (!c.ok && !log[0]) { st = status_text(&r); set_own(&log, str_printf("Compilatorul s-a terminat cu: %s", st)); free(st); }
    sb_free(&lg);
    if (!c.ok) rtrim_inplace(log);
    set_own(&c.exe, exe);
    set_own(&c.log, log);
    c.ms = r.wall_ms;
    set_own(&c.cmd, cmd_string(&argv));
    free(log_path);
    sv_free(&argv);
    sv_free(&srcs);
    sv_free(&libs);
    return c;
}
/* compilatorul și opțiunile limbajului (din șablon, fără surse/ieșire) */
static char *lang_compiler(const jval *L, const cfg *C, svec *flags)
{
    svec toks = split_cmd(gs(L, "compile", ""));
    size_t i;
    int skip = 0;
    char stack[32], *comp;
    sv_init(flags);
    if (!toks.n) { sv_free(&toks); return xstrdup(""); }
    snprintf(stack, sizeof stack, "%lld", (long long)(C->stack_mb * 1024 * 1024));
    for (i = 1; i < toks.n; i++) {
        const char *t = toks.v[i];
        char *a;
        if (skip) { skip = 0; continue; }
        if (eq(t, "-o")) { skip = 1; continue; }
        if (eq(t, "{sources}") || eq(t, "{libs}") || strstr(t, "{exe}") || strstr(t, "{main}")) continue;
        a = str_replace(t, "{stack}", stack);
        sv_push_own(flags, str_replace(a, "{problem}", C->name));
        free(a);
    }
    comp = xstrdup(toks.v[0]);
    sv_free(&toks);
    return comp;
}
static compiled compile_objects(const cfg *C, const jval *Lmain, const char *root, const char *out_dir, const svec *libs)
{
    compiled c = comp_new();
    pool pl = {0};
    char *exe, *obj_dir, *log_path, *cmd = NULL;
    entries es;
    svec files, src_rel, logv, objs, cflags_extra;
    const jval **src_lang;
    size_t i, nsrc = 0;
    double total_ms = 0;
    int ok = 1;
    const jval *linker_lang = NULL;
    long long tmo = (long long)(C->compile_timeout * 1000);
    f_mkdirs(out_dir);
    exe = PS(&pl, fresh_exe(out_dir, C));
    obj_dir = PS(&pl, path_join(out_dir, "obj"));
    f_cleardir(obj_dir);
    es = walk_bfs(root, C->max_depth, &C->ignore);
    sv_init(&files);
    for (i = 0; i < es.n; i++) if (!es.v[i].dir) sv_push(&files, es.v[i].rel);
    ents_free(&es);
    sv_sort_str(&files);
    sv_init(&src_rel);
    src_lang = (const jval **)xmalloc((files.n + 1) * sizeof(jval *));
    for (i = 0; i < files.n; i++) {
        const jval *L;
        if (!is_obj_ext(files.v[i])) continue;
        L = lang_for(C, files.v[i]);
        if (L && lang_compiled(L)) { sv_push(&src_rel, files.v[i]); src_lang[nsrc++] = L; }
    }
    sv_init(&logv);
    sv_init(&objs);
    cflags_extra = compile_only_flags(libs);
    log_path = PS(&pl, path_join(out_dir, "compilare.txt"));
    for (i = 0; i < nsrc; i++) {
        svec lflags, argv;
        char *comp = lang_compiler(src_lang[i], C, &lflags), *rel_obj, *dot, *sl, *on, *obj, *od, *out;
        rn_result r;
        rel_obj = xstrdup(src_rel.v[i]);
        dot = strrchr(rel_obj, '.');
        sl = strrchr(rel_obj, '/');
        if (dot && (!sl || dot > sl)) *dot = 0;
        on = str_printf("%s.o", rel_obj);
        obj = path_join(obj_dir, on);
        od = path_dir(obj);
        f_mkdirs(od);
        sv_init(&argv);
        sv_push(&argv, comp);
        sv_extend(&argv, &lflags);
        sv_extend(&argv, &cflags_extra);
        sv_push(&argv, "-c");
        sv_push_own(&argv, path_join(root, src_rel.v[i]));
        sv_push(&argv, "-o");
        sv_push(&argv, obj);
        r = run_p(&argv, root, "", log_path, log_path, &JOB.env_path_add, tmo, 1);
        total_ms += r.wall_ms;
        out = read_text(log_path, 20000);
        {
            char *rp = path_rel(obj, obj_dir);
            sv_push_own(&logv, str_printf("Compiled: %s -> %s", src_rel.v[i], rp));
            free(rp);
        }
        rtrim_inplace(out);
        if (out[0]) sv_push(&logv, out);
        if (!(run_ok(&r) && f_isfile(obj))) {
            ok = 0;
            if (!r.started) sv_push(&logv, r.error);
        }
        sv_push(&objs, obj);
        {
            char *e = ext_of(src_rel.v[i]);
            if (!eq(e, ".c")) linker_lang = src_lang[i];
            free(e);
        }
        free(out);
        free(od);
        free(obj);
        free(on);
        free(rel_obj);
        free(comp);
        sv_free(&lflags);
        sv_free(&argv);
    }
    if (nsrc == 0) {
        set_own(&c.log, str_printf("Nu am găsit fișiere .c sau .cpp în %s", root));
        goto out;
    }
    if (ok && eq(probe_mode(C, Lmain), "c")) {
        const jval *Lc = lang_for(C, "x.c");
        svec lflags, argv;
        char *comp, *pobj;
        rn_result r;
        if (!Lc) Lc = Lmain;
        comp = lang_compiler(Lc, C, &lflags);
        pobj = path_join(out_dir, "olieval_probe.o");
        sv_init(&argv);
        sv_push(&argv, comp);
        sv_extend(&argv, &lflags);
        sv_push(&argv, "-c");
        sv_push(&argv, JOB.current_probe);
        sv_push(&argv, "-o");
        sv_push(&argv, pobj);
        r = run_p(&argv, out_dir, "", log_path, log_path, &JOB.env_path_add, tmo, 0);
        if (run_ok(&r) && f_isfile(pobj)) sv_push(&objs, pobj);
        free(comp);
        free(pobj);
        sv_free(&lflags);
        sv_free(&argv);
    }
    if (ok) {
        const jval *lk = linker_lang ? linker_lang : (lang_for(C, "x.c") ? lang_for(C, "x.c") : Lmain);
        svec lflags, argv;
        char *comp = lang_compiler(lk, C, &lflags), *out;
        rn_result r;
        sv_init(&argv);
        sv_push(&argv, comp);
        sv_extend(&argv, &objs);
        sv_push(&argv, "-o");
        sv_push(&argv, exe);
        if (libs) sv_extend(&argv, libs);
        cmd = cmd_string(&argv);
        r = run_p(&argv, root, "", log_path, log_path, &JOB.env_path_add, tmo, 1);
        total_ms += r.wall_ms;
        out = read_text(log_path, 20000);
        rtrim_inplace(out);
        if (out[0]) sv_push(&logv, out);
        ok = run_ok(&r) && f_isfile(exe);
        if (ok) sv_push_own(&logv, str_printf("Build completed. Executable created at %s", exe));
        else sv_push(&logv, "Legarea (build) a eșuat.");
        free(out);
        free(comp);
        sv_free(&lflags);
        sv_free(&argv);
    } else sv_push(&logv, "Compilarea a eșuat; nu se mai face legarea.");
    {
        char *all = sv_join(&logv, "\n");
        if (strlen(all) > 20000) all[20000] = 0;
        set_own(&c.log, all);
    }
    c.ok = ok;
    set_str(&c.exe, exe);
    c.ms = total_ms;
    if (cmd) set_own(&c.cmd, str_printf("compile.sh + build.sh: %s", cmd));
    else set_own(&c.cmd, str_printf("compile.sh + build.sh: %lu surse", (unsigned long)nsrc));
out:
    free(cmd);
    free(src_lang);
    sv_free(&files);
    sv_free(&src_rel);
    sv_free(&logv);
    sv_free(&objs);
    sv_free(&cflags_extra);
    pool_drain(&pl);
    return c;
}
static char *find_scripts(const char *root, const char *build_cmd)
{
    static const char *dirs[] = {"bash-scripts/", "scripts/", ""};
    char *bc = str_trim(build_cmd);
    int i;
    if (bc[0] && !eq(bc, "auto")) return bc;
    free(bc);
    for (i = 0; i < 3; i++) {
        char *a = str_printf("%scompile.sh", dirs[i]), *b = str_printf("%sbuild.sh", dirs[i]);
        char *pa = path_join(root, a), *pb = path_join(root, b);
        int ok = f_isfile(pa) && f_isfile(pb);
        free(a); free(b); free(pa); free(pb);
        if (ok) return str_printf("bash %scompile.sh && bash %sbuild.sh", dirs[i], dirs[i]);
    }
    for (i = 0; i < 3; i++) {
        char *b = str_printf("%sbuild.sh", dirs[i]), *pb = path_join(root, b);
        int ok = f_isfile(pb);
        free(b); free(pb);
        if (ok) return str_printf("bash %sbuild.sh", dirs[i]);
    }
    return xstrdup("");
}
static char *safe_name(const char *s)
{
    char *out = xstrdup(s), *q;
    for (q = out; *q; q++) {
        unsigned char c = (unsigned char)*q;
        if (!(isalnum(c) || c == '-' || c == '_' || c == '.' || c >= 128)) *q = '_';
    }
    if (!out[0]) { free(out); return xstrdup("_"); }
    return out;
}
static compiled compile_scripts(const cfg *C, const char *root)
{
    compiled c = comp_new();
    pool pl = {0};
    char *cmd = find_scripts(root, C->build_cmd), *exe_rel, *exe, *exe2, *out_dir, *log_path, *log, *found;
    svec argv;
    rn_result r;
    if (!cmd[0]) {
        free(cmd);
        set_str(&c.log, "Nu am găsit scripturile compile.sh / build.sh (în bash-scripts/, scripts/ sau în rădăcina proiectului)");
        return c;
    }
    exe_rel = PS(&pl, subst(C->build_exe, C->pv));
    exe = PS(&pl, path_join(root, exe_rel));
    exe2 = PS(&pl, str_printf("%s.exe", exe));
    f_remove(exe);
    f_remove(exe2);
    {
        char *a = PS(&pl, path_join(C->work_dir, "build")), *b = PS(&pl, path_join(a, C->name)), *sn = PS(&pl, safe_name(JOB.student_now));
        out_dir = PS(&pl, path_join(b, sn));
    }
    f_mkdirs(out_dir);
    log_path = PS(&pl, path_join(out_dir, "compilare.txt"));
    sv_init(&argv);
    if (IS_WIN) { sv_push(&argv, "cmd"); sv_push(&argv, "/c"); }
    else { sv_push(&argv, "sh"); sv_push(&argv, "-c"); }
    sv_push(&argv, cmd);
    r = run_p(&argv, root, "", log_path, log_path, &JOB.env_path_add, (long long)(C->compile_timeout * 1000), 1);
    log = read_text(log_path, 20000);
    found = f_isfile(exe) ? exe : (f_isfile(exe2) ? exe2 : "");
    c.ok = r.started && !r.timed_out && found[0];
    if (!r.started) set_own(&log, str_printf("%s\n%s", r.error, log));
    else if (r.timed_out) set_own(&log, str_printf("Scripturile au depășit %lld s\n%s", (long long)C->compile_timeout, log));
    else if (!found[0]) {
        rtrim_inplace(log);
        set_own(&log, str_printf("%s\nScripturile nu au produs executabilul %s", log, exe_rel));
    }
    set_own(&c.log, ltrim_dup(log));
    free(log);
    set_str(&c.exe, found);
    c.ms = r.wall_ms;
    set_own(&c.cmd, cmd);
    sv_free(&argv);
    pool_drain(&pl);
    return c;
}
static compiled build_student(const cfg *C, const jval *L, const info_t *info, const char *bdir, const svec *libs)
{
    svec srcs;
    size_t i;
    compiled c;
    if (!lang_compiled(L)) return compile_program(C, L, NULL, info->main, bdir, info->root, 0, NULL);
    if (eq(C->build_mode, "objects")) return compile_objects(C, L, info->root, bdir, libs);
    if (eq(C->build_mode, "scripts")) return compile_scripts(C, info->root);
    sv_init(&srcs);
    for (i = 0; i < info->sources.n; i++) sv_push_own(&srcs, path_join(info->root, info->sources.v[i]));
    c = compile_program(C, L, &srcs, info->main, bdir, info->root, 1, libs);
    sv_free(&srcs);
    return c;
}

static const char *CALIB_C =
    "#include <stdio.h>\nint main(void)\n{\n    int x = 0;\n    FILE *f = fopen(\"calibrare.in\", \"r\");\n    if (f != NULL) {\n"
    "        if (fscanf(f, \"%d\", &x) != 1) x = 0;\n        fclose(f);\n    }\n    printf(\"%d\\n\", x * 0);\n    return 0;\n}\n";
static const char *CALIB_CPP =
    "#include <iostream>\n#include <fstream>\nint main()\n{\n    std::ifstream f(\"calibrare.in\");\n    int x = 0;\n    f >> x;\n"
    "    std::cout << x * 0 << std::endl;\n    return 0;\n}\n";

static long long calibrate(const cfg *C, const jval *L)
{
    pool pl = {0};
    const char *lid = gs(L, "id", ""), *src = NULL;
    char *file = NULL, *d, *sp, *probe, *outp;
    svec exts = lang_exts(L), one, argv;
    size_t i;
    compiled cp;
    long long best = -1;
    int k;
    for (i = 0; i < exts.n; i++) {
        const char *e = exts.v[i];
        if (eq(e, ".c")) { file = xstrdup("calibrare.c"); src = CALIB_C; break; }
        if (eq(e, ".cpp") || eq(e, ".cc") || eq(e, ".cxx")) { file = str_printf("calibrare%s", e); src = CALIB_CPP; break; }
        if (eq(e, ".py")) { file = xstrdup("calibrare.py"); src = "x = 0\nprint(x)\n"; break; }
    }
    sv_free(&exts);
    if (!file) return 0;
    PS(&pl, file);
    d = PS(&pl, path_join(PS(&pl, path_join(C->work_dir, "calibrare")), lid));
    f_mkdirs(d);
    sp = PS(&pl, path_join(d, file));
    f_write(sp, src, strlen(src));
    sv_init(&one);
    sv_push(&one, sp);
    cp = compile_program(C, L, &one, sp, d, d, !eq(C->build_mode, "scripts"), NULL);
    if (!cp.ok) {
        job_logf("  Calibrare %s: compilarea a eșuat; memoria nu va fi calibrată.", lid);
        comp_free(&cp);
        sv_free(&one);
        pool_drain(&pl);
        return 0;
    }
    argv = build_argv(gs(L, "run", "{exe}"), &one, NULL, cp.exe, sp, 0, C->name);
    probe = PS(&pl, path_join(d, "probe.txt"));
    outp = PS(&pl, path_join(d, "out.txt"));
    for (k = 0; k < 3; k++) {
        svec env;
        rn_result r;
        probe_t pr;
        long long m;
        f_remove(probe);
        env = probe_env(C, L, probe);
        r = run_p(&argv, d, "", outp, "", &env, 10000, 0);
        pr = read_probe(probe);
        m = pr.has_rss ? (long long)pr.rss : (probe_active(C, L) ? 0 : r.peak_kb);
        if (r.started && r.exited && m > 0) best = best < 0 ? m : (m < best ? m : best);
        sv_free(&env);
    }
    if (best >= 0) job_logf("  Calibrare %s: program gol = %lld KB", lid, best);
    else job_logf("  Calibrare %s: program gol = ? KB", lid);
    comp_free(&cp);
    sv_free(&one);
    sv_free(&argv);
    pool_drain(&pl);
    return best > 0 ? best : 0;
}

/* =====================================================================================
 * Comparare și verificator
 * ===================================================================================== */
typedef struct { const char *p; size_t n; } slice;
typedef struct { slice *v; size_t n, cap; } slices;
static void sl_push(slices *s, const char *p, size_t n)
{
    if (s->n + 1 > s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->v = (slice *)xrealloc(s->v, s->cap * sizeof(slice));
    }
    s->v[s->n].p = p;
    s->v[s->n].n = n;
    s->n++;
}
static int sl_eq(slice a, slice b) { return a.n == b.n && memcmp(a.p, b.p, a.n) == 0; }
static slices split_ws_sl(const char *s, size_t n)
{
    slices out = {0};
    size_t i = 0;
    while (i < n) {
        size_t b;
        while (i < n && is_sp((unsigned char)s[i])) i++;
        b = i;
        while (i < n && !is_sp((unsigned char)s[i])) i++;
        if (i > b) sl_push(&out, s + b, i - b);
    }
    return out;
}
static slices split_lines_rstrip(const char *s, size_t n)
{
    slices out = {0};
    size_t i = 0;
    for (;;) {
        size_t j = i, e;
        while (j < n && s[j] != '\n') j++;
        e = j;
        while (e > i && (s[e - 1] == ' ' || s[e - 1] == '\t')) e--;
        sl_push(&out, s + i, e - i);
        if (j >= n) break;
        i = j + 1;
    }
    return out;
}
/* primele n caractere (puncte de cod) + „…” */
static void utf8_prefix_to(sb *o, const char *s, size_t len, size_t n)
{
    size_t i = 0, cnt = 0;
    while (i < len && cnt < n) {
        unsigned char c = (unsigned char)s[i];
        size_t l = c < 0x80 ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 1)));
        i += l;
        cnt++;
    }
    if (i >= len) { sb_putn(o, s, len); return; }
    sb_putn(o, s, i);
    sb_puts(o, "…");
}
static char *utf8_prefix(const char *s, size_t n)
{
    sb o;
    sb_init(&o);
    utf8_prefix_to(&o, s, strlen(s), n);
    return sb_take(&o);
}
static int is_number_tok(slice t)
{
    size_t i = 0, n = t.n;
    int digits = 0;
    if (i < n && (t.p[i] == '+' || t.p[i] == '-')) i++;
    while (i < n && is_dig(t.p[i])) { i++; digits++; }
    if (i < n && t.p[i] == '.') { i++; while (i < n && is_dig(t.p[i])) { i++; digits++; } }
    if (!digits) return 0;
    if (i < n && (t.p[i] == 'e' || t.p[i] == 'E')) {
        int ed = 0;
        i++;
        if (i < n && (t.p[i] == '+' || t.p[i] == '-')) i++;
        while (i < n && is_dig(t.p[i])) { i++; ed++; }
        if (!ed) return 0;
    }
    return i == n;
}
static double sl_num(slice t)
{
    char buf[512];
    size_t n = t.n < sizeof buf - 1 ? t.n : sizeof buf - 1;
    memcpy(buf, t.p, n);
    buf[n] = 0;
    return strtod(buf, NULL);
}
static char *strip_cr(const char *s, size_t n, size_t *out_n)
{
    char *o = (char *)xmalloc(n + 1);
    size_t i, k = 0;
    for (i = 0; i < n; i++) if (s[i] != '\r') o[k++] = s[i];
    o[k] = 0;
    *out_n = k;
    return o;
}
static void lower_n(char *s, size_t n) { size_t i; for (i = 0; i < n; i++) if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] + 32); }

/* întoarce fracțiunea 0/1; *detail (malloc) */
static double compare_output(const cfg *C, const char *out_b, size_t out_n, const char *ok_b, size_t ok_n, char **detail)
{
    size_t an, bn;
    char *a = strip_cr(out_b, out_n, &an), *b = strip_cr(ok_b, ok_n, &bn);
    double f = 1.0;
    sb d;
    sb_init(&d);
    if (C->ignore_case) { lower_n(a, an); lower_n(b, bn); }
    if (eq(C->cmp_mode, "exact")) {
        if (!(an == bn && memcmp(a, b, an) == 0)) {
            size_t n = an < bn ? an : bn, i = 0, k;
            long line = 1;
            while (i < n && a[i] == b[i]) i++;
            for (k = 0; k < i; k++) if (b[k] == '\n') line++;
            sb_printf(&d, "Diferență la linia %ld (octetul %lu)", line, (unsigned long)(i + 1));
            f = 0.0;
        }
    } else if (eq(C->cmp_mode, "tokens") || eq(C->cmp_mode, "real")) {
        slices ta = split_ws_sl(a, an), tb = split_ws_sl(b, bn);
        size_t k, m = ta.n < tb.n ? ta.n : tb.n;
        for (k = 0; k < m; k++) {
            slice x = ta.v[k], y = tb.v[k];
            if (sl_eq(x, y)) continue;
            if (eq(C->cmp_mode, "real") && is_number_tok(x) && is_number_tok(y)) {
                double fx = sl_num(x), fy = sl_num(y);
                if (fabs(fx - fy) <= C->epsilon * dmax(1.0, fabs(fy))) continue;
            }
            sb_printf(&d, "Elementul %lu: așteptat «", (unsigned long)(k + 1));
            utf8_prefix_to(&d, y.p, y.n, 60);
            sb_puts(&d, "», primit «");
            utf8_prefix_to(&d, x.p, x.n, 60);
            sb_puts(&d, "»");
            f = 0.0;
            break;
        }
        if (f > 0 && ta.n != tb.n) {
            sb_printf(&d, "Număr diferit de valori: așteptat %lu, primit %lu", (unsigned long)tb.n, (unsigned long)ta.n);
            f = 0.0;
        }
        free(ta.v);
        free(tb.v);
    } else {
        slices la = split_lines_rstrip(a, an), lb = split_lines_rstrip(b, bn);
        size_t k, m;
        while (la.n && la.v[la.n - 1].n == 0) la.n--;
        while (lb.n && lb.v[lb.n - 1].n == 0) lb.n--;
        m = la.n < lb.n ? la.n : lb.n;
        for (k = 0; k < m; k++) {
            if (!sl_eq(la.v[k], lb.v[k])) {
                sb_printf(&d, "Linia %lu: așteptat «", (unsigned long)(k + 1));
                utf8_prefix_to(&d, lb.v[k].p, lb.v[k].n, 60);
                sb_puts(&d, "», primit «");
                utf8_prefix_to(&d, la.v[k].p, la.v[k].n, 60);
                sb_puts(&d, "»");
                f = 0.0;
                break;
            }
        }
        if (f > 0 && la.n != lb.n) {
            sb_printf(&d, "Număr diferit de linii: așteptat %lu, primit %lu", (unsigned long)lb.n, (unsigned long)la.n);
            f = 0.0;
        }
        free(la.v);
        free(lb.v);
    }
    free(a);
    free(b);
    *detail = sb_take(&d);
    return f;
}

static int prepare_checker(const cfg *C, svec *argv_out, char **err)
{
    pool pl = {0};
    char *spec = PS(&pl, str_trim(C->checker)), *p, *e;
    int i, r = 1;
    sv_init(argv_out);
    *err = NULL;
    if (!spec[0]) {
        for (i = 0; CHECKER_NAMES[i]; i++) {
            if (f_isfile(PS(&pl, path_join(C->tests_dir, CHECKER_NAMES[i])))) { spec = (char *)CHECKER_NAMES[i]; break; }
        }
    }
    if (!spec[0]) { *err = xstrdup("Nu este setat niciun verificator"); pool_drain(&pl); return 0; }
    p = PS(&pl, path_resolve(C->tests_dir, PS(&pl, subst(spec, C->v))));
    if (!f_isfile(p)) {
        char *found = (!strchr(spec, '/') && !strchr(spec, '\\')) ? rn_find_program(spec) : NULL;
        if (!found) { *err = str_printf("Verificatorul nu există: %s", p); pool_drain(&pl); return 0; }
        p = PS(&pl, found);
    }
    e = PS(&pl, ext_of(p));
    if (eq(e, ".c") || eq(e, ".cpp") || eq(e, ".cc") || eq(e, ".cxx")) {
        const jval *L = lang_for(C, p);
        char *d;
        svec one;
        compiled cp;
        if (!L) { *err = str_printf("Nu știu să compilez verificatorul %s", p); pool_drain(&pl); return 0; }
        d = PS(&pl, path_join(PS(&pl, path_join(C->work_dir, "verificator")), C->name));
        sv_init(&one);
        sv_push(&one, p);
        cp = compile_program(C, L, &one, p, d, PS(&pl, path_dir(p)), 0, NULL);
        if (!cp.ok) { *err = str_printf("Verificatorul nu se compilează:\n%s", cp.log); r = 0; }
        else sv_push(argv_out, cp.exe);
        comp_free(&cp);
        sv_free(&one);
        pool_drain(&pl);
        return r;
    }
    if (eq(e, ".py")) {
        const jval *L = lang_for(C, p);
        const char *runt = L ? gs(L, "run", "python3 {main}") : (IS_WIN ? "python {main}" : "python3 {main}");
        svec one;
        sv_init(&one);
        sv_push(&one, p);
        sv_free(argv_out);
        *argv_out = build_argv(runt, &one, NULL, "", p, 0, C->name);
        sv_free(&one);
        pool_drain(&pl);
        return 1;
    }
    sv_push(argv_out, p);
    pool_drain(&pl);
    return 1;
}

typedef struct { double f; int has_pts; double pts; char *msg, *err; } check_res;
static check_res run_checker(const cfg *C, const char *in_path, const char *ok_path, const char *out_path)
{
    check_res cr;
    pool pl = {0};
    char *rd = PS(&pl, run_dir(C)), *cdir = PS(&pl, path_join(rd, "check"));
    char *n_in = PS(&pl, str_printf("%s.in", C->name)), *n_ok = PS(&pl, str_printf("%s.ok", C->name)), *n_out = PS(&pl, str_printf("%s.out", C->name));
    char *p_in = PS(&pl, path_join(cdir, n_in)), *p_ok = PS(&pl, path_join(cdir, n_ok)), *p_out = PS(&pl, path_join(cdir, n_out));
    char *args_t, *so, *se, *out, *err;
    const char *argsd;
    svec argv, sa, parts;
    size_t i;
    rn_result r;
    memset(&cr, 0, sizeof cr);
    f_cleardir(cdir);
    if (!f_copy(in_path, p_in) || !f_copy(ok_path, p_ok) || (out_path[0] && f_isfile(out_path) && !f_copy(out_path, p_out))) {
        cr.err = str_printf("nu pot pregăti fișierele: %s", f_lasterr());
        pool_drain(&pl);
        return cr;
    }
    args_t = PS(&pl, str_trim(C->checker_args));
    argsd = args_t[0] ? args_t : (eq(C->protocol, "oji") ? "" : (eq(C->protocol, "fraction") ? "{input} {ok} {output}" : "{input} {output} {ok}"));
    argv = sv_copy(&JOB.checker_argv);
    sa = split_cmd(argsd);
    for (i = 0; i < sa.n; i++) {
        char *a = str_replace(sa.v[i], "{input}", p_in), *b = str_replace(a, "{ok}", p_ok), *c = str_replace(b, "{output}", p_out);
        sv_push_own(&argv, str_replace(c, "{problem}", C->name));
        free(a);
        free(b);
        free(c);
    }
    sv_free(&sa);
    so = PS(&pl, path_join(cdir, "_stdout.txt"));
    se = PS(&pl, path_join(cdir, "_stderr.txt"));
    r = run_p(&argv, cdir, "", so, se, &JOB.env_path_add, 10000, 1);
    sv_free(&argv);
    out = PS(&pl, str_trim(PS(&pl, read_text(so, 10000))));
    err = PS(&pl, str_trim(PS(&pl, read_text(se, 10000))));
    if (!r.started) { cr.err = xstrdup(r.error); pool_drain(&pl); return cr; }
    if (r.timed_out) { cr.err = xstrdup("verificatorul a depășit 10 s"); pool_drain(&pl); return cr; }
    if (eq(C->protocol, "exitcode")) {
        char *joined = PS(&pl, err[0] ? str_printf("%s %s", out, err) : xstrdup(out)), *tm = PS(&pl, str_trim(joined));
        char *msg = utf8_prefix(tm, 300);
        size_t ml = strlen(msg);
        cr.f = (r.exited && r.exit_code == 0) ? 1.0 : 0.0;
        if (ml > 3 && strcmp(msg + ml - 3, "…") == 0) msg[ml - 3] = 0;
        cr.msg = msg;
        pool_drain(&pl);
        return cr;
    }
    parts = split_ws(out);
    if (!parts.n) {
        if (err[0]) {
            char *e2 = xstrndup(err, strlen(err) > 200 ? 200 : strlen(err));
            cr.err = str_printf("verificatorul nu a afișat nimic: %s", e2);
            free(e2);
        } else cr.err = xstrdup("verificatorul nu a afișat nimic");
    } else {
        char *first = str_replace(parts.v[0], ",", "."), *end = NULL;
        double val = strtod(first, &end);
        if (!end || *end) {
            char *o2 = xstrndup(out, strlen(out) > 200 ? 200 : strlen(out));
            cr.err = str_printf("verificatorul nu afișează în formatul 'punctaj mesaj': %s", o2);
            free(o2);
        } else {
            const char *pos = strstr(out, parts.v[0]);
            char *msg = str_trim(pos ? pos + strlen(parts.v[0]) : "");
            if (eq(C->protocol, "fraction")) {
                if (err[0]) { char *j2 = str_printf("%s %s", msg, err); free(msg); msg = str_trim(j2); free(j2); }
                cr.f = dmax(0.0, dmin(1.0, val));
            } else {
                cr.has_pts = 1;
                cr.pts = val;
            }
            if (strlen(msg) > 300) msg[300] = 0;
            cr.msg = msg;
        }
        free(first);
    }
    sv_free(&parts);
    pool_drain(&pl);
    return cr;
}

/* =====================================================================================
 * Punctare
 * ===================================================================================== */
static int is_fatal(const char *raw)
{
    return eq(raw, "RE") || eq(raw, "TLE") || eq(raw, "MLE") || eq(raw, "NOOUT") || eq(raw, "CHKERR") || eq(raw, "SKIP");
}
static double policy_factor(const jval *pol, double v, double L, double H)
{
    const char *mode = gs(pol, "mode", "zero");
    if (eq(mode, "ignore")) return 1;
    if (eq(mode, "percent")) return dmax(0.0, dmin(100.0, gn(pol, "percent", 50))) / 100.0;
    if (eq(mode, "linear")) return H > L ? dmax(0.0, dmin(1.0, 1.0 - (v - L) / (H - L))) : 0.0;
    return 0;
}
static char *policy_note(const jval *pol, double factor)
{
    const char *mode = gs(pol, "mode", "zero");
    char *p, *r;
    if (eq(mode, "ignore")) return xstrdup(" (ignorat)");
    if (eq(mode, "zero") || factor <= 0) return xstrdup(" (0 puncte)");
    p = fmt_int(factor * 100);
    r = str_printf(" (punctaj redus la %s%%)", p);
    free(p);
    return r;
}
static double score_test(const cfg *C, const jval *t, jval *tr)
{
    double P = gn(t, "points", 0), TP = gn(t, "timePoints", 0), MP = gn(t, "memPoints", 0);
    double base, f, Lt, Lm, tval, mval, ft, fm, score;
    const jval *pts_v, *pol, *pt, *pm;
    int over_t, over_m;
    char *raw, *msg0, *status, *msg;
    jset_n(tr, "max", round2(P + TP + MP));
    raw = xstrdup(gs(tr, "raw", "SKIP"));
    msg0 = xstrdup(gs(tr, "msg0", ""));
    if (is_fatal(raw)) {
        jset_s(tr, "status", raw);
        jset_n(tr, "score", 0);
        jset_s(tr, "msg", msg0);
        free(raw);
        free(msg0);
        return 0;
    }
    pts_v = G(tr, "pts");
    if (pts_v->t != J_NULL) {
        double pts = j_number(pts_v, 0);
        base = P > 0 ? dmax(0.0, dmin(pts, P)) : dmax(0.0, pts);
        f = P > 0 ? base / P : (pts > 0 ? 1.0 : 0.0);
    } else {
        f = dmax(0.0, dmin(1.0, j_number(G(tr, "f"), 0)));
        base = f * P;
    }
    Lt = gn(t, "timeMs", 0);
    if (Lt == 0) Lt = C->time_ms;
    Lm = gn(t, "memMb", 0);
    if (Lm == 0) Lm = C->mem_mb;
    tval = eq(C->time_mode, "wall") ? j_number(G(tr, "wallMs"), 0) : j_number(G(tr, "cpuMs"), 0);
    mval = j_number(G(tr, "memKb"), 0);
    over_t = Lt > 0 && tval > Lt;
    over_m = Lm > 0 && G(tr, "memKb")->t != J_NULL && mval > Lm * 1024;
    pol = G(C->prob, "policy");
    pt = G(pol, "time");
    pm = G(pol, "memory");
    ft = over_t ? policy_factor(pt, tval, Lt, Lt * C->hard_time) : 1.0;
    fm = over_m ? policy_factor(pm, mval, Lm * 1024, Lm * 1024 * C->hard_mem) : 1.0;
    score = base * ft * fm + (!over_t ? f * TP : 0.0) + (!over_m ? f * MP : 0.0);
    score = round2(score);
    status = xstrdup(raw);
    msg = xstrdup(msg0);
    if (over_t && !eq(gs(pt, "mode", "zero"), "ignore")) {
        char *a = fmt_int(tval), *b = fmt_int(Lt), *n = policy_note(pt, ft);
        set_str(&status, "TLE");
        set_own(&msg, str_printf("Timp depășit: %s ms > %s ms%s", a, b, n));
        if (!eq(raw, "OK")) set_own(&msg, str_printf("%s; %s", msg, msg0));
        free(a);
        free(b);
        free(n);
    } else if (over_m && !eq(gs(pm, "mode", "zero"), "ignore")) {
        char *a = fmt_int(mval), *b = fmt_int(Lm * 1024), *n = policy_note(pm, fm);
        set_str(&status, "MLE");
        set_own(&msg, str_printf("Memorie depășită: %s KB > %s KB%s", a, b, n));
        if (!eq(raw, "OK")) set_own(&msg, str_printf("%s; %s", msg, msg0));
        free(a);
        free(b);
        free(n);
    } else if (over_t || over_m) {
        if (over_t && over_m) set_own(&msg, str_printf("%s; timp depășit (ignorat), memorie depășită (ignorat)", msg0));
        else if (over_t) set_own(&msg, str_printf("%s; timp depășit (ignorat)", msg0));
        else set_own(&msg, str_printf("%s; memorie depășită (ignorat)", msg0));
    }
    jset_s(tr, "status", status);
    jset_n(tr, "score", score);
    jset_s(tr, "msg", msg);
    free(raw);
    free(msg0);
    free(status);
    free(msg);
    return score;
}

static void rescore_inplace(const jval *ws, const jval *prob, jval *res)
{
    cfg C;
    const jval *tests = G(prob, "tests"), *pts = G(prob, "points"), *pol_size;
    double p_compile = gn(pts, "compile", 0), p_size = gn(pts, "size", 0), max_score = 0;
    jval *tl, *lim, *studs;
    size_t i, s;
    cfg_init(&C, ws, prob);
    if (j_is(tests, J_ARR))
        for (i = 0; i < tests->len; i++) max_score += gn(tests->items[i], "points", 0) + gn(tests->items[i], "timePoints", 0) + gn(tests->items[i], "memPoints", 0);
    max_score = round2(max_score + p_compile + p_size);
    jset_n(res, "maxScore", max_score);
    tl = j_arr();
    if (j_is(tests, J_ARR)) {
        for (i = 0; i < tests->len; i++) {
            const jval *t = tests->items[i];
            jval *x = j_obj();
            jset_s(x, "id", gs(t, "id", ""));
            jset_n(x, "points", gn(t, "points", 0));
            jset_n(x, "timePoints", gn(t, "timePoints", 0));
            jset_n(x, "memPoints", gn(t, "memPoints", 0));
            j_push(tl, x);
        }
    }
    j_set(res, "tests", tl);
    lim = j_obj();
    jset_n(lim, "timeMs", C.time_ms);
    jset_n(lim, "memMb", C.mem_mb);
    jset_n(lim, "stackMb", C.stack_mb);
    jset_n(lim, "sourceKb", C.source_kb);
    jset_s(lim, "timeMode", C.time_mode);
    j_set(res, "limits", lim);
    pol_size = G(G(prob, "policy"), "size");
    studs = j_get(res, "students");
    if (!studs || studs->t != J_ARR) studs = j_set(res, "students", j_arr());
    for (s = 0; s < studs->len; s++) {
        jval *sr = studs->items[s], *notes = j_arr(), *new_tests;
        const jval *old_notes = G(sr, "notes"), *old_tests;
        const char *status;
        double total = 0;
        int size_ok;
        jset_n(sr, "max", max_score);
        if (j_is(old_notes, J_ARR))
            for (i = 0; i < old_notes->len; i++)
                if (!str_starts(j_text(old_notes->items[i], ""), "Sursa depășește")) j_push(notes, j_clone(old_notes->items[i]));
        status = gs(sr, "status", "");
        if (eq(status, "CE") || eq(status, "NOSRC") || eq(status, "ERR")) {
            jset_n(sr, "score", 0);
            j_set(sr, "notes", notes);
            continue;
        }
        old_tests = G(sr, "tests");
        new_tests = j_arr();
        if (j_is(tests, J_ARR)) {
            for (i = 0; i < tests->len; i++) {
                const jval *t = tests->items[i];
                const char *tid = gs(t, "id", "");
                const jval *src = NULL;
                jval *tr;
                size_t k;
                /* ultima apariție câștigă (ca într-un dicționar) */
                if (j_is(old_tests, J_ARR))
                    for (k = 0; k < old_tests->len; k++) if (eq(j_text(G(old_tests->items[k], "id"), ""), tid)) src = old_tests->items[k];
                if (src) tr = j_clone(src);
                else {
                    tr = j_obj();
                    jset_s(tr, "id", tid);
                    jset_s(tr, "raw", "SKIP");
                    jset_s(tr, "msg0", "Neevaluat");
                    jset_n(tr, "f", 0);
                    jset_null(tr, "pts");
                }
                total += score_test(&C, t, tr);
                j_push(new_tests, tr);
            }
        }
        j_set(sr, "tests", new_tests);
        total += p_compile;
        size_ok = C.source_kb <= 0 || j_number(G(sr, "sizeJudged"), 0) <= C.source_kb * 1024;
        jset_b(sr, "sizeOk", size_ok);
        if (size_ok) total += p_size;
        else {
            const char *mode = gs(pol_size, "mode", "zero");
            char *a = fmt_2(j_number(G(sr, "sizeJudged"), 0) / 1024.0), *b = fmt_g(C.source_kb);
            char *lims = str_printf("Sursa depășește limita: %s KB > %s KB", a, b);
            free(a);
            free(b);
            if (eq(mode, "zero")) { total = 0; set_own(&lims, str_printf("%s (0 puncte)", lims)); }
            else if (eq(mode, "percent")) {
                double p = dmax(0.0, dmin(100.0, gn(pol_size, "percent", 50)));
                char *ps = fmt_int(p);
                total = total * p / 100.0;
                set_own(&lims, str_printf("%s (punctaj redus la %s%%)", lims, ps));
                free(ps);
            } else if (eq(mode, "penalty")) {
                double pen = gn(pol_size, "penalty", 10);
                char *ps = fmt_g(pen);
                total = dmax(0.0, total - pen);
                set_own(&lims, str_printf("%s (penalizare %s puncte)", lims, ps));
                free(ps);
            } else set_own(&lims, str_printf("%s (ignorat)", lims));
            j_push(notes, j_str_own(lims));
        }
        j_set(sr, "notes", notes);
        jset_n(sr, "score", round2(total));
    }
    cfg_free(&C);
}
jval *eng_rescore(const jval *ws, const jval *prob, const jval *res)
{
    jval *r = j_is(res, J_OBJ) ? j_clone(res) : j_obj();
    rescore_inplace(ws, prob, r);
    return r;
}

/* =====================================================================================
 * Evaluare
 * ===================================================================================== */
static jval *load_results(const cfg *C)
{
    char *d = path_join(C->results_dir, C->name), *p = path_join(d, "ultimele.json"), *t;
    size_t len;
    jval *v;
    const char *err = NULL;
    t = f_read(p, -1, &len);
    free(d);
    free(p);
    if (!t) return NULL;
    v = j_parse(t, len, &err);
    free(t);
    if (!v) return NULL;
    if (v->t != J_OBJ || !j_is(G(v, "students"), J_ARR)) { j_free(v); return NULL; }
    return v;
}
static void save_results(const cfg *C, const jval *res, int snapshot)
{
    char *d = path_join(C->results_dir, C->name), *text, *p;
    f_mkdirs(d);
    text = j_dump(res, 1);
    p = path_join(d, "ultimele.json");
    if (!f_write(p, text, strlen(text))) job_logf("Nu pot salva rezultatele: %s", f_lasterr());
    free(p);
    if (snapshot) {
        char *st = stamp_str(), *nm = str_printf("rezultate-%s.json", st);
        p = path_join(d, nm);
        f_write(p, text, strlen(text));
        free(p);
        free(nm);
        free(st);
    }
    free(text);
    free(d);
}

static void set_raw(jval *tr, const char *raw, const char *msg)
{
    jset_s(tr, "raw", raw);
    jset_s(tr, "msg0", msg);
}
static jval *run_test(const cfg *C, const student *st, const info_t *info, const jval *L, const char *exe, const jval *t, long long baseline)
{
    pool pl = {0};
    const char *tid = gs(t, "id", ""), *pv[7];
    jval *tr = j_obj();
    char *in_src, *ok_src, *rdir, *sroot, *cwd, *so, *se, *probe, *out_path, *stderr_head, *low_err, *m;
    const char *stdin_path = "";
    double Lt, Lm, hard_wall, cpu;
    long long cpu_s, mem_hard, stack_kb, fsize_kb, base;
    svec argv, env;
    rn_spec spec;
    rn_result r;
    probe_t pr;
    const char **av, **ev;
    size_t i, shl;
    int has_mem;
    long long mem_raw = 0;

    jset_s(tr, "id", tid);
    jset_s(tr, "raw", "RE");
    jset_s(tr, "msg0", "");
    jset_s(tr, "detail", "");
    jset_n(tr, "f", 0);
    jset_null(tr, "pts");
    jset_n(tr, "cpuMs", 0);
    jset_n(tr, "wallMs", 0);
    jset_n(tr, "timeMs", 0);
    jset_n(tr, "memKb", 0);
    jset_n(tr, "memRawKb", 0);
    jset_null(tr, "stackKb");
    jset_n(tr, "exit", 0);
    jset_n(tr, "signal", 0);
    jset_b(tr, "timedOut", 0);
    jset_s(tr, "stderr", "");
    jset_s(tr, "out", "");
    jset_s(tr, "err", "");
    in_src = PS(&pl, path_resolve(C->tests_dir, gs(t, "input", "")));
    ok_src = PS(&pl, path_resolve(C->tests_dir, gs(t, "ok", "")));
    if (!f_isfile(in_src)) {
        set_raw(tr, "CHKERR", PS(&pl, str_printf("Lipsește fișierul de intrare al testului: %s", in_src)));
        pool_drain(&pl);
        return tr;
    }
    rdir = PS(&pl, run_dir(C));
    f_mkdirs(rdir);
    if (eq(C->run_in, "project")) sroot = info->root;
    else {
        sroot = PS(&pl, path_join(rdir, "sandbox"));
        f_cleardir(sroot);
    }
    pv[0] = "problem"; pv[1] = C->name; pv[2] = "nr"; pv[3] = C->nr; pv[4] = "n"; pv[5] = tid; pv[6] = NULL;
    cwd = PS(&pl, path_resolve(sroot, PS(&pl, subst(C->io_cwd, pv))));
    f_mkdirs(cwd);
    if (eq(C->io_input, "stdin")) stdin_path = in_src;
    else {
        char *dst = PS(&pl, path_resolve(sroot, PS(&pl, subst(C->io_inpath, pv))));
        f_mkdirs(PS(&pl, path_dir(dst)));
        f_copy(in_src, dst);
    }
    so = PS(&pl, path_join(rdir, "stdout.txt"));
    se = PS(&pl, path_join(rdir, "stderr.txt"));
    probe = PS(&pl, path_join(rdir, "probe.txt"));
    f_remove(probe);
    if (eq(C->io_output, "stdout")) out_path = so;
    else {
        out_path = PS(&pl, path_resolve(sroot, PS(&pl, subst(C->io_outpath, pv))));
        f_remove(out_path);
    }
    Lt = gn(t, "timeMs", 0);
    if (Lt == 0) Lt = C->time_ms;
    Lm = gn(t, "memMb", 0);
    if (Lm == 0) Lm = C->mem_mb;
    hard_wall = Lt > 0 ? Lt * C->hard_time + 500 : 60000;
    cpu_s = (long long)ceil(hard_wall / 1000.0) + 1;
    mem_hard = Lm > 0 ? (long long)(Lm * 1024 * C->hard_mem + 65536) : 0;
    stack_kb = C->stack_mb > 0 ? (long long)(C->stack_mb * 1024) : 0;
    fsize_kb = C->output_mb > 0 ? (long long)(C->output_mb * 1024) : 0;
    argv = build_argv(gs(L, "run", "{exe}"), NULL, NULL, exe, info->main, 0, C->name);
    env = probe_env(C, L, probe);
    av = (const char **)xmalloc((argv.n + 1) * sizeof(char *));
    for (i = 0; i < argv.n; i++) av[i] = argv.v[i];
    av[argv.n] = NULL;
    ev = (const char **)xmalloc((env.n + 1) * sizeof(char *));
    for (i = 0; i < env.n; i++) ev[i] = env.v[i];
    ev[env.n] = NULL;
    memset(&spec, 0, sizeof spec);
    spec.argv = av;
    spec.cwd = cwd;
    spec.stdin_path = stdin_path[0] ? stdin_path : NULL;
    spec.stdout_path = so;
    spec.stderr_path = se;
    spec.env_add = ev;
    spec.wall_ms = (long long)hard_wall;
    spec.cpu_s = cpu_s;
    spec.mem_kb = mem_hard;
    spec.stack_kb = stack_kb;
    spec.fsize_kb = fsize_kb;
    spec.single_process = 1;
    spec.cancel = &JOB.stopping;
    rn_run(&spec, &r);
    free(av);
    free(ev);
    sv_free(&argv);
    sv_free(&env);
    pr = read_probe(probe);
    if (pr.has_rss) { has_mem = 1; mem_raw = (long long)pr.rss; }
    else if (probe_active(C, L) && !IS_WIN) has_mem = 0;
    else { has_mem = 1; mem_raw = r.peak_kb; }
    base = C->calibrate ? baseline : 0;
    cpu = r.cpu_ms > 0 ? r.cpu_ms : (pr.has_cpu ? pr.cpu : 0);
    jset_n(tr, "cpuMs", round3(cpu));
    jset_n(tr, "wallMs", round3(r.wall_ms));
    jset_n(tr, "timeMs", eq(C->time_mode, "wall") ? round3(r.wall_ms) : round3(cpu));
    if (has_mem) {
        jset_n(tr, "memRawKb", (double)mem_raw);
        jset_n(tr, "memKb", (double)(mem_raw - base > 0 ? mem_raw - base : 0));
    } else {
        jset_null(tr, "memRawKb");
        jset_null(tr, "memKb");
    }
    if (pr.has_stack) jset_n(tr, "stackKb", (double)(long long)pr.stack);
    else jset_null(tr, "stackKb");
    jset_n(tr, "exit", r.exit_code);
    jset_n(tr, "signal", r.signal);
    jset_b(tr, "timedOut", r.timed_out);
    stderr_head = f_isfile(se) ? PS(&pl, read_text_n(se, 2000, &shl)) : PS(&pl, xstrdup(""));
    if (!stderr_head[0]) shl = 0;
    j_set(tr, "stderr", j_strn(stderr_head, shl));
    low_err = PS(&pl, str_lower(stderr_head));
    if (C->keep_outputs) {
        char *kd = PS(&pl, path_join(PS(&pl, path_join(PS(&pl, path_join(C->work_dir, "iesiri")), C->name)), st->id));
        char *sn = PS(&pl, safe_name(tid));
        char *ko = PS(&pl, path_join(kd, PS(&pl, str_printf("%s.out", sn)))), *ke = PS(&pl, path_join(kd, PS(&pl, str_printf("%s.err", sn))));
        f_mkdirs(kd);
        f_remove(ko);
        f_remove(ke);
        if (f_isfile(out_path) && f_copy(out_path, ko)) jset_s(tr, "out", ko);
        if (f_size(se) > 0 && f_copy(se, ke)) jset_s(tr, "err", ke);
    }
    if (r.cancelled) { set_raw(tr, "SKIP", "Oprit de utilizator"); goto done; }
    if (!r.started) { set_raw(tr, "RE", PS(&pl, str_printf("Programul nu a putut fi pornit: %s", r.error))); goto done; }
    if (r.timed_out) {
        char *ms = PS(&pl, fmt_int(r.wall_ms));
        m = PS(&pl, str_printf("Timp depășit: oprit după %s ms%s", ms,
                              r.cpu_ms < 0.3 * r.wall_ms ? " (programul pare să aștepte, ex. citire de la tastatură)" : ""));
        set_raw(tr, "TLE", m);
        goto done;
    }
    if (!r.exited || (r.exit_code != 0 && C->nonzero_error)) {
        int mem_fail = strstr(low_err, "bad_alloc") || strstr(low_err, "memoryerror") || strstr(low_err, "out of memory") || r.mem_limit_hit ||
                       r.win_status == 0xC0000017u || r.win_status == 0xC000012Du ||
                       (mem_hard > 0 && (double)r.peak_kb >= 0.95 * (double)mem_hard);
        if (mem_fail) set_raw(tr, "MLE", "Memorie depășită (alocarea a eșuat)");
        else set_raw(tr, "RE", PS(&pl, str_printf("Eroare la execuție: %s", PS(&pl, status_text(&r)))));
        goto done;
    }
    if (!f_isfile(out_path)) { set_raw(tr, "NOOUT", PS(&pl, str_printf("Lipsește fișierul de ieșire %s", path_base(out_path)))); goto done; }
    if (C->output_mb > 0 && (double)f_size(out_path) > C->output_mb * 1024 * 1024) {
        set_raw(tr, "WA", PS(&pl, str_printf("Ieșire prea mare (peste %s MB)", PS(&pl, fmt_g(C->output_mb)))));
        goto done;
    }
    if (eq(C->cmp_mode, "checker")) {
        check_res cr;
        double f;
        if (!JOB.have_checker) { set_raw(tr, "CHKERR", PS(&pl, str_printf("Eroare verificator: %s", JOB.checker_error))); goto done; }
        cr = run_checker(C, in_src, ok_src, out_path);
        if (cr.err) {
            set_raw(tr, "CHKERR", PS(&pl, str_printf("Eroare verificator: %s", cr.err)));
            free(cr.err);
            free(cr.msg);
            goto done;
        }
        f = cr.f;
        if (cr.has_pts) {
            double P = gn(t, "points", 0);
            jset_n(tr, "pts", cr.pts);
            f = P > 0 ? dmax(0.0, dmin(cr.pts, P)) / P : (cr.pts > 0 ? 1.0 : 0.0);
        }
        jset_n(tr, "f", f);
        m = PS(&pl, str_printf("%s%s%s", f >= 1 ? "Corect" : (f > 0 ? "Parțial" : "Incorect"), cr.msg && cr.msg[0] ? ": " : "", cr.msg ? cr.msg : ""));
        set_raw(tr, f >= 1 ? "OK" : (f > 0 ? "PARTIAL" : "WA"), m);
        free(cr.msg);
        goto done;
    }
    if (!f_isfile(ok_src)) { set_raw(tr, "CHKERR", PS(&pl, str_printf("Lipsește fișierul corect al testului: %s", ok_src))); goto done; }
    {
        size_t ol = 0, kl = 0;
        char *ob = f_read(out_path, -1, &ol), *kb = ob ? f_read(ok_src, -1, &kl) : NULL, *detail = NULL;
        double f;
        if (!ob || !kb) {
            set_raw(tr, "CHKERR", PS(&pl, str_printf("Nu pot citi ieșirea: %s", f_lasterr())));
            free(ob);
            free(kb);
            goto done;
        }
        f = compare_output(C, ob, ol, kb, kl, &detail);
        jset_n(tr, "f", f);
        jset_s(tr, "detail", detail);
        set_raw(tr, f >= 1 ? "OK" : "WA", f >= 1 ? "Corect" : "Incorect");
        free(detail);
        free(ob);
        free(kb);
    }
done:
    pool_drain(&pl);
    return tr;
}

static int contains_id(const svec *v, const char *x) { return sv_contains(v, x); }

static void sort_students_stable(jval *studs)
{
    size_t i, j;
    for (i = 1; i < studs->len; i++) {
        jval *x = studs->items[i];
        const char *xi = j_text(G(x, "id"), "");
        j = i;
        while (j > 0 && natcmp(j_text(G(studs->items[j - 1], "id"), ""), xi) > 0) {
            studs->items[j] = studs->items[j - 1];
            j--;
        }
        studs->items[j] = x;
    }
}

/* întoarce 0, sau -1 cu *fatal (malloc) dacă evaluarea nu poate continua */
static int evaluate_problem(const jval *ws, const jval *prob, const svec *only_s, const svec *only_t, char **fatal)
{
    cfg C;
    pool pl = {0};
    const jval *all_tests;
    const jval **tests;
    size_t nt = 0, i, s;
    students sts;
    char *err = NULL, *rd;
    jval *res;
    long n_students;
    cfg_init(&C, ws, prob);
    *fatal = NULL;
    job_set("problem", C.name);
    job_set("phase", "pregătire");
    job_set("student", "");
    job_set("test", "");
    all_tests = G(prob, "tests");
    tests = (const jval **)xmalloc((j_len(all_tests) + 1) * sizeof(jval *));
    if (j_is(all_tests, J_ARR))
        for (i = 0; i < all_tests->len; i++)
            if (!only_t->n || contains_id(only_t, gs(all_tests->items[i], "id", ""))) tests[nt++] = all_tests->items[i];
    job_logf("=== Problema %s ===", C.name);
    if (!nt) {
        job_log("Nu există teste în barem. Folosește «Detectează testele» în tabul Teste & barem.");
        free(tests);
        cfg_free(&C);
        return 0;
    }
    sts = discover_students(&C, &err);
    if (err) {
        *fatal = err;
        free(tests);
        cfg_free(&C);
        return -1;
    }
    if (only_s->n) {
        size_t k = 0;
        for (i = 0; i < sts.n; i++) {
            if (contains_id(only_s, sts.v[i].id)) { if (k != i) sts.v[k] = sts.v[i]; k++; }
            else {
                free(sts.v[i].id); free(sts.v[i].dir); free(sts.v[i].file); free(sts.v[i].name);
                sv_free(&sts.v[i].warnings);
            }
        }
        sts.n = k;
    }
    n_students = (long)sts.n;
    mutex_lock(&JOB.mu);
    JOB.total += n_students * (long)(nt + 1);
    mutex_unlock(&JOB.mu);
    res = load_results(&C);
    if (!res) res = j_obj();
    jset_s(res, "format", "evaluator-olimpiada/rezultate");
    jset_n(res, "version", 1);
    jset_s(res, "problem", C.name);
    jset_s(res, "variant", VARIANT);
    jset_s(res, "os", os_name());
    j_set(res, "updated", j_str_own(now_str()));
    if (!j_is(G(res, "students"), J_ARR)) j_set(res, "students", j_arr());
    j_set(res, "baseline", j_obj());
    f_mkdirs(C.work_dir);
    rd = PS(&pl, run_dir(&C));
    f_mkdirs(rd);
    set_own(&JOB.current_probe, path_join(C.work_dir, "olieval_probe.c"));
    set_str(&JOB.run_dir_any, rd);
    cache_clear();
    set_own(&JOB.py_probe_dir, path_join(C.work_dir, "sonda-py"));
    {
        char *pyp = PS(&pl, path_join(JOB.py_probe_dir, "sitecustomize.py"));
        if (!f_write(JOB.current_probe, PROBE_C_SOURCE, PROBE_C_LEN) || !f_write(pyp, PROBE_PY_SOURCE, PROBE_PY_LEN))
            job_logf("Nu pot scrie sonda: %s", f_lasterr());
    }
    set_str(&JOB.preload_so, "");
    if (eq(C.build_mode, "scripts") && eq(os_name(), "linux") && C.use_probe) {
        char *so = PS(&pl, path_join(PS(&pl, path_join(C.work_dir, "sonda")), "olieval_probe.so"));
        const jval *Lc = lang_for(&C, "x.c");
        char *cc = NULL, *lp;
        svec argv, fl;
        rn_result r;
        f_mkdirs(PS(&pl, path_dir(so)));
        if (Lc) { cc = lang_compiler(Lc, &C, &fl); sv_free(&fl); }
        if (!cc || !cc[0]) { free(cc); cc = xstrdup("gcc"); }
        lp = PS(&pl, path_join(rd, "_sonda.txt"));
        sv_init(&argv);
        sv_push(&argv, cc);
        sv_push(&argv, "-O2");
        sv_push(&argv, "-shared");
        sv_push(&argv, "-fPIC");
        sv_push(&argv, "-o");
        sv_push(&argv, so);
        sv_push(&argv, JOB.current_probe);
        r = run_p(&argv, "", "", lp, lp, NULL, 60000, 0);
        if (run_ok(&r) && f_isfile(so)) set_str(&JOB.preload_so, so);
        else job_log("Sonda LD_PRELOAD nu s-a putut compila; memoria se măsoară din exterior.");
        sv_free(&argv);
        free(cc);
    }
    sv_free(&JOB.env_path_add);
    if (IS_WIN) {
        svec dirs;
        sv_init(&dirs);
        for (i = 0; j_is(C.languages, J_ARR) && i < C.languages->len; i++) {
            svec toks = split_cmd(gs(C.languages->items[i], "compile", ""));
            if (toks.n) {
                char *fp = rn_find_program(toks.v[0]);
                if (fp) {
                    char *d = path_dir(fp);
                    add_unique(&dirs, d);
                    free(d);
                    free(fp);
                }
            }
            sv_free(&toks);
        }
        if (dirs.n) {
            sb p;
            char *path = env_get("PATH");
            sb_init(&p);
            sb_puts(&p, "PATH=");
            for (i = 0; i < dirs.n; i++) { sb_puts(&p, dirs.v[i]); sb_putc(&p, ';'); }
            sb_puts(&p, path);
            sv_push_own(&JOB.env_path_add, sb_take(&p));
            free(path);
        }
        sv_free(&dirs);
    }
    JOB.have_checker = 0;
    sv_free(&JOB.checker_argv);
    set_str(&JOB.checker_error, "");
    if (eq(C.cmp_mode, "checker")) {
        char *cerr = NULL;
        JOB.have_checker = prepare_checker(&C, &JOB.checker_argv, &cerr);
        if (cerr) set_own(&JOB.checker_error, cerr);
        if (JOB.have_checker) {
            char *cs = cmd_string(&JOB.checker_argv);
            job_logf("Verificator: %s", cs);
            free(cs);
        } else job_logf("Verificator: %s", JOB.checker_error);
    }
    rescore_inplace(ws, prob, res);
    job_publish(C.name, res);
    for (s = 0; s < sts.n; s++) {
        const student *st = &sts.v[s];
        long start_done;
        jval *sr, *comp, *notes, *studs;
        long prev = -1;
        if (JOB.stopping) break;
        job_set("student", st->id);
        job_set("test", "");
        job_set("phase", "compilare");
        job_logf("Elev %s", st->id);
        mutex_lock(&JOB.mu);
        start_done = JOB.done;
        mutex_unlock(&JOB.mu);
        sr = j_obj();
        jset_s(sr, "id", st->id);
        jset_s(sr, "name", st->name);
        jset_s(sr, "dir", st->dir);
        jset_s(sr, "root", "");
        jset_s(sr, "main", "");
        jset_s(sr, "lang", "");
        j_set(sr, "sources", j_arr());
        jset_n(sr, "sizeMain", 0);
        jset_n(sr, "sizeAll", 0);
        jset_n(sr, "sizeJudged", 0);
        jset_b(sr, "sizeOk", 1);
        comp = j_obj();
        jset_b(comp, "ok", 0);
        jset_n(comp, "ms", 0);
        jset_s(comp, "log", "");
        jset_s(comp, "cmd", "");
        j_set(sr, "compile", comp);
        jset_s(sr, "status", "");
        jset_n(sr, "score", 0);
        jset_n(sr, "max", 0);
        notes = j_set(sr, "notes", j_arr());
        j_set(sr, "evaluatedAt", j_str_own(now_str()));
        j_set(sr, "tests", j_arr());
        if (!st->exists) {
            jset_s(sr, "status", "NOSRC");
            j_push(notes, j_str("Lipsă sursă (folderul sau fișierul elevului nu există)"));
            job_log("  lipsă sursă");
        } else {
            info_t info = detect_project(&C, st);
            jset_s(sr, "root", info.root);
            jset_s(sr, "main", info.main_rel);
            jset_s(sr, "lang", info.lang);
            j_set(sr, "sources", str_arr(&info.sources));
            jset_n(sr, "sizeMain", (double)info.size_main);
            jset_n(sr, "sizeAll", (double)info.size_all);
            jset_n(sr, "sizeJudged", (double)info.size_judged);
            for (i = 0; i < info.warnings.n; i++) j_push(notes, j_str(info.warnings.v[i]));
            if (!info.main[0] || !info.lang_obj) {
                jset_s(sr, "status", "NOSRC");
                job_log("  nu am găsit sursa");
            } else {
                const jval *L = info.lang_obj;
                svec srcs, libs;
                char *bdir, *sn;
                compiled cp;
                jval *c2;
                sv_init(&srcs);
                if (eq(C.build_mode, "objects")) {
                    entries es = walk_bfs(info.root, C.max_depth, &C.ignore);
                    for (i = 0; i < es.n; i++) if (!es.v[i].dir && is_obj_ext(es.v[i].rel)) sv_push(&srcs, es.v[i].abs);
                    ents_free(&es);
                } else for (i = 0; i < info.sources.n; i++) sv_push_own(&srcs, path_join(info.root, info.sources.v[i]));
                libs = auto_lib_flags(&C, info.root, &srcs);
                sn = safe_name(st->id);
                {
                    char *a = path_join(C.work_dir, "build"), *b = path_join(a, C.name);
                    bdir = path_join(b, sn);
                    free(a);
                    free(b);
                }
                set_str(&JOB.student_now, st->id);
                cp = build_student(&C, L, &info, bdir, &libs);
                c2 = j_obj();
                jset_b(c2, "ok", cp.ok);
                jset_n(c2, "ms", floor(cp.ms * 10 + 0.5) / 10);
                jset_s(c2, "log", cp.log);
                jset_s(c2, "cmd", cp.cmd);
                j_set(sr, "compile", c2);
                job_add_done(1);
                if (!cp.ok) {
                    jset_s(sr, "status", "CE");
                    job_log("  eroare de compilare");
                } else {
                    const char *lid = gs(L, "id", "");
                    jval *bl = j_get(res, "baseline"), *tarr = j_get(sr, "tests");
                    long long baseline;
                    size_t ti;
                    if (C.calibrate && !j_get(bl, lid)) jset_n(bl, lid, (double)calibrate(&C, L));
                    baseline = C.calibrate ? (long long)j_number(G(bl, lid), 0) : 0;
                    job_set("phase", "rulare");
                    for (ti = 0; ti < nt; ti++) {
                        jval *tr;
                        if (JOB.stopping) break;
                        job_set("test", gs(tests[ti], "id", ""));
                        tr = run_test(&C, st, &info, L, cp.exe, tests[ti], baseline);
                        job_logf("  test %s: %s %s", j_text(G(tr, "id"), ""), j_text(G(tr, "raw"), ""), j_text(G(tr, "msg0"), ""));
                        j_push(tarr, tr);
                        job_add_done(1);
                    }
                    jset_s(sr, "status", "DONE");
                }
                comp_free(&cp);
                free(bdir);
                free(sn);
                sv_free(&srcs);
                sv_free(&libs);
            }
            info_free(&info);
        }
        mutex_lock(&JOB.mu);
        {
            long want = start_done + (long)nt + 1;
            JOB.done = want < JOB.total ? want : JOB.total;
        }
        mutex_unlock(&JOB.mu);
        studs = j_get(res, "students");
        for (i = 0; i < studs->len; i++) if (eq(j_text(G(studs->items[i], "id"), ""), st->id)) { prev = (long)i; break; }
        if (prev >= 0 && only_t->n && eq(gs(sr, "status", ""), "DONE") && eq(gs(studs->items[prev], "status", ""), "DONE")) {
            /* păstrează rezultatele testelor neevaluate acum */
            jval *merged = j_arr();
            const jval *lists[2];
            int li;
            lists[0] = G(studs->items[prev], "tests");
            lists[1] = G(sr, "tests");
            for (li = 0; li < 2; li++) {
                size_t k;
                for (k = 0; j_is(lists[li], J_ARR) && k < lists[li]->len; k++) {
                    const jval *x = lists[li]->items[k];
                    const char *xid = j_text(G(x, "id"), "");
                    size_t q;
                    int found = 0;
                    for (q = 0; q < merged->len; q++) {
                        if (eq(j_text(G(merged->items[q], "id"), ""), xid)) { j_replace_at(merged, q, j_clone(x)); found = 1; break; }
                    }
                    if (!found) j_push(merged, j_clone(x));
                }
            }
            j_set(sr, "tests", merged);
        }
        if (prev < 0) j_push(studs, sr);
        else j_replace_at(studs, (size_t)prev, sr);
        sort_students_stable(studs);
        j_set(res, "updated", j_str_own(now_str()));
        rescore_inplace(ws, prob, res);
        save_results(&C, res, 0);
        job_publish(C.name, res);
    }
    save_results(&C, res, 1);
    job_publish(C.name, res);
    j_free(res);
    st_free_all(&sts);
    free(tests);
    pool_drain(&pl);
    cfg_free(&C);
    return 0;
}

typedef struct { jval *ws; svec problems, students, tests; } job_args;

static void job_thread(void *arg)
{
    job_args *a = (job_args *)arg;
    size_t i;
    int failed = 0;
    for (i = 0; i < a->problems.n; i++) {
        const jval *prob;
        char *fatal = NULL;
        if (JOB.stopping) break;
        prob = eng_find_problem(a->ws, a->problems.v[i]);
        if (!prob) { job_logf("Problema %s nu există", a->problems.v[i]); continue; }
        if (evaluate_problem(a->ws, prob, &a->students, &a->tests, &fatal) != 0) {
            job_logf("EROARE: %s", fatal ? fatal : "");
            free(fatal);
            mutex_lock(&JOB.mu);
            set_str(&JOB.error, "Eroare internă");
            mutex_unlock(&JOB.mu);
            failed = 1;
            break;
        }
    }
    if (!failed) job_log(JOB.stopping ? "Evaluare oprită." : "Evaluare terminată.");
    mutex_lock(&JOB.mu);
    JOB.running = 0;
    set_str(&JOB.phase, JOB.stopping ? "oprit" : "gata");
    set_str(&JOB.student, "");
    set_str(&JOB.test, "");
    JOB.done = JOB.total;
    JOB.rv++;
    mutex_unlock(&JOB.mu);
    j_free(a->ws);
    sv_free(&a->problems);
    sv_free(&a->students);
    sv_free(&a->tests);
    free(a);
}

int eng_start(const jval *ws, const svec *problems, const svec *students_f, const svec *tests_f, char **err)
{
    job_args *a;
    eng_init_once();
    *err = NULL;
    mutex_lock(&JOB.mu);
    if (JOB.running) {
        mutex_unlock(&JOB.mu);
        *err = xstrdup("O evaluare este deja în desfășurare");
        return 0;
    }
    JOB.running = 1;
    JOB.stopping = 0;
    set_str(&JOB.phase, "pornire");
    JOB.done = 0;
    JOB.total = 0;
    sv_free(&JOB.logs);
    j_free(JOB.results);
    JOB.results = j_obj();
    set_str(&JOB.error, "");
    JOB.rv++;
    mutex_unlock(&JOB.mu);
    a = (job_args *)xmalloc(sizeof *a);
    a->ws = j_clone(ws);
    a->problems = sv_copy(problems);
    a->students = sv_copy(students_f);
    a->tests = sv_copy(tests_f);
    if (!thread_start(job_thread, a)) {
        j_free(a->ws);
        sv_free(&a->problems);
        sv_free(&a->students);
        sv_free(&a->tests);
        free(a);
        mutex_lock(&JOB.mu);
        JOB.running = 0;
        set_str(&JOB.phase, "gata");
        mutex_unlock(&JOB.mu);
        *err = xstrdup("Nu pot porni firul de evaluare");
        return 0;
    }
    return 1;
}
void eng_stop(void)
{
    eng_init_once();
    JOB.stopping = 1;
    job_log("Se oprește evaluarea…");
}
jval *eng_status(long since, long rv)
{
    jval *o = j_obj(), *lg = j_arr();
    size_t i;
    eng_init_once();
    mutex_lock(&JOB.mu);
    jset_b(o, "running", JOB.running);
    jset_b(o, "stopping", JOB.stopping);
    jset_s(o, "phase", JOB.phase);
    jset_s(o, "problem", JOB.problem);
    jset_s(o, "student", JOB.student);
    jset_s(o, "test", JOB.test);
    jset_n(o, "done", (double)JOB.done);
    jset_n(o, "total", (double)JOB.total);
    for (i = since > 0 ? (size_t)since : 0; i < JOB.logs.n; i++) j_push(lg, j_str(JOB.logs.v[i]));
    j_set(o, "log", lg);
    jset_n(o, "logNext", (double)JOB.logs.n);
    jset_n(o, "rv", (double)JOB.rv);
    jset_s(o, "error", JOB.error);
    if (rv != JOB.rv) j_set(o, "results", j_clone(JOB.results));
    mutex_unlock(&JOB.mu);
    return o;
}

jval *eng_scan(const jval *ws, const char *problem, char **err)
{
    const jval *prob = eng_find_problem(ws, problem);
    cfg C;
    jval *res, *studs;
    students sts;
    size_t i;
    *err = NULL;
    if (!prob) { *err = str_printf("Problema nu există: %s", problem); return NULL; }
    cfg_init(&C, ws, prob);
    res = scan_tests(&C);
    sts = discover_students(&C, err);
    if (*err) {
        char *e = str_printf("Eroare internă: %s", *err);
        free(*err);
        *err = e;
        j_free(res);
        cfg_free(&C);
        return NULL;
    }
    studs = j_arr();
    for (i = 0; i < sts.n; i++) {
        info_t info;
        if (sts.v[i].exists) info = detect_project(&C, &sts.v[i]);
        else {
            info_init(&info);
            sv_push(&info.warnings, "Folderul/fișierul elevului nu există");
        }
        j_push(studs, public_student(&sts.v[i], &info));
        info_free(&info);
    }
    j_set(res, "students", studs);
    st_free_all(&sts);
    cfg_free(&C);
    return res;
}

jval *eng_structure(const jval *ws, int create)
{
    const char *r = gs(ws, "root", "");
    char *root = path_abs(r[0] ? r : ".");
    jval *items = j_arr(), *o, *errors = j_arr(), *dummy = j_obj();
    const jval *probs = G(ws, "problems"), *p0, *reg = G(ws, "students");
    svec studs;
    cfg C0;
    size_t i, k;
    long created = 0;
#define ADD_ITEM(path_, kind_)                                                                    \
    do {                                                                                          \
        const char *pp_ = (path_);                                                                \
        size_t q_;                                                                                \
        int dup_ = 0;                                                                             \
        for (q_ = 0; q_ < items->len; q_++) if (eq(gs(items->items[q_], "path", ""), pp_)) dup_ = 1; \
        if (!dup_) {                                                                              \
            jval *it_ = j_obj();                                                                  \
            jset_s(it_, "path", pp_);                                                             \
            jset_s(it_, "kind", (kind_));                                                         \
            jset_b(it_, "exists", f_isdir(pp_));                                                  \
            j_push(items, it_);                                                                   \
        }                                                                                         \
    } while (0)
    jset_s(dummy, "name", "_");
    p0 = (j_is(probs, J_ARR) && probs->len) ? probs->items[0] : dummy;
    cfg_init(&C0, ws, p0);
    ADD_ITEM(C0.work_dir, "lucru");
    ADD_ITEM(C0.results_dir, "rezultate");
    cfg_free(&C0);
    sv_init(&studs);
    for (i = 0; j_is(reg, J_ARR) && i < reg->len; i++) {
        char *id = str_trim(gs(reg->items[i], "id", ""));
        if (id[0]) sv_push(&studs, id);
        free(id);
    }
    for (i = 0; j_is(probs, J_ARR) && i < probs->len; i++) {
        cfg C;
        cfg_init(&C, ws, probs->items[i]);
        if (eq(C.layout, "flat")) {
            char *t = subst(gs(ws, "flatDir", "{root}/{problem}/surse"), C.v), *p = path_resolve(root, t);
            ADD_ITEM(p, "surse");
            free(t);
            free(p);
        } else {
            for (k = 0; k < studs.n; k++) {
                const char *kv[] = {"student", studs.v[k], "root", C.root, "problem", C.name, "nr", C.nr, NULL};
                char *t = subst(gs(ws, "studentDir", "{root}/{problem}/{student}"), kv), *p = path_resolve(root, t);
                ADD_ITEM(p, "elev");
                free(t);
                free(p);
            }
        }
        ADD_ITEM(C.tests_dir, "teste");
        cfg_free(&C);
    }
#undef ADD_ITEM
    if (create) {
        for (i = 0; i < items->len; i++) {
            jval *it = items->items[i];
            const char *p = gs(it, "path", "");
            if (gb(it, "exists", 0)) continue;
            if (f_mkdirs(p)) { jset_b(it, "exists", 1); created++; }
            else j_push(errors, j_str_own(str_printf("%s: %s", p, f_lasterr())));
        }
    }
    o = j_obj();
    j_set(o, "items", items);
    jset_n(o, "created", (double)created);
    j_set(o, "errors", errors);
    sv_free(&studs);
    j_free(dummy);
    free(root);
    return o;
}
