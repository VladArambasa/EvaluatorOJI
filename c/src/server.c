/*
 * server.c — serverul HTTP local și API-ul variantei C (vezi docs/SPECIFICATIE.md).
 * Pornire: evaluator-c [--port 8765] [--no-browser] [--gui cale/index.html]
 */
/* API Windows minim: Windows 7 (unele MinGW mai vechi pornesc de la XP/2003) */
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#ifdef _WIN32
#define _CRT_RAND_S
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

#include "embedded.h"
#include "engine.h"
#include "json.h"
#include "runner.h"
#include "util.h"

static const char *APP_NAME = "Evaluator Olimpiadă";
static const char *APP_VERSION = "1.0.0";
static int PORT = 8765;
static char TOKEN[40];
static char *GUI_HTML;
static size_t GUI_LEN;

/* ---------------------------------------------------------------- utilitare API */
typedef struct { int code; char *msg; } apierr;   /* code 0 = fără eroare */
static jval *fail(apierr *e, int code, char *msg) { e->code = code; e->msg = msg; return NULL; }

static char *settings_path(void)
{
    char *h = home_dir(), *d = path_join(h, ".evaluator-olimpiada"), *p = path_join(d, "setari.json");
    free(h);
    free(d);
    return p;
}
static char *find_example(void)
{
    static const char *rels[] = {"exemplu", "../exemplu", "../../exemplu"};
    char *d = exe_dir();
    int i;
    for (i = 0; i < 3; i++) {
        char *p = path_resolve(d, rels[i]), *f = path_join(p, "evaluator.json");
        int ok = f_isfile(f);
        free(f);
        if (ok) { free(d); return p; }
        free(p);
    }
    free(d);
    return xstrdup("");
}
static jval *found_or_empty(const char *name)
{
    char *p = rn_find_program(name);
    return p ? j_str_own(p) : j_str("");
}

static jval *api_info(void)
{
    const char *py = IS_WIN ? "python" : "python3";
    jval *o = j_obj(), *comp = j_obj(), *feat = j_obj();
    char sep[2] = {SEPC, 0};
    jset_s(o, "app", APP_NAME);
    jset_s(o, "version", APP_VERSION);
    jset_s(o, "variant", VARIANT);
    jset_s(o, "os", os_name());
    jset_s(o, "sep", sep);
    j_set(o, "home", j_str_own(home_dir()));
    j_set(o, "settings", j_str_own(settings_path()));
    j_set(o, "cwd", j_str_own(current_dir()));
    jset_s(o, "python", py);
    jset_n(o, "pid", cur_pid());
    j_set(comp, "gcc", found_or_empty("gcc"));
    j_set(comp, "g++", found_or_empty("g++"));
    j_set(comp, "python", found_or_empty(py));
    j_set(o, "compilers", comp);
    jset_s(feat, "runner", "native");
    jset_b(feat, "probe", PROBE_C_LEN > 0);
    jset_b(feat, "memLimit", 1);
    jset_b(feat, "cpuTime", 1);
    j_set(o, "features", feat);
    j_set(o, "example", j_str_own(find_example()));
    return o;
}

static int cmp_dirent_gui(const void *a, const void *b)
{
    const dirent_t *x = (const dirent_t *)a, *y = (const dirent_t *)b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return natcmp(x->name, y->name);
}
static jval *api_fs_list(const jval *b, apierr *e)
{
    const char *in = gs(b, "path", "");
    char *p, *parent;
    dirlist dl = {0};
    jval *arr, *roots, *o;
    size_t i;
    char sep[2] = {SEPC, 0};
    if (!in[0]) p = home_dir();
    else if (in[0] == '~') {
        char *h = home_dir();
        p = str_printf("%s%s", h, in + 1);
        free(h);
    } else p = xstrdup(in);
    {
        char *a = path_abs(p);
        free(p);
        p = a;
    }
    if (!f_isdir(p)) {
        char *m = str_printf("Folderul nu există: %s", p);
        free(p);
        return fail(e, 400, m);
    }
    f_listdir(p, &dl);
    if (dl.n > 1) qsort(dl.v, dl.n, sizeof(dirent_t), cmp_dirent_gui);
    arr = j_arr();
    for (i = 0; i < dl.n; i++) {
        jval *x = j_obj();
        jset_s(x, "name", dl.v[i].name);
        jset_b(x, "dir", dl.v[i].dir);
        jset_n(x, "size", (double)dl.v[i].size);
        jset_n(x, "mtime", (double)dl.v[i].mtime);
        j_push(arr, x);
    }
    dl_free(&dl);
    parent = path_dir(p);
    roots = j_arr();
#ifdef _WIN32
    {
        DWORD mask = GetLogicalDrives();
        int k;
        for (k = 0; k < 26; k++) {
            if (mask & (1u << k)) {
                char d[4] = {(char)('A' + k), ':', '\\', 0};
                j_push(roots, j_str(d));
            }
        }
    }
#else
    j_push(roots, j_str("/"));
#endif
    o = j_obj();
    jset_s(o, "path", p);
    jset_s(o, "parent", (!parent[0] || strcmp(parent, p) == 0) ? "" : parent);
    jset_s(o, "sep", sep);
    j_set(o, "entries", arr);
    j_set(o, "roots", roots);
    free(parent);
    free(p);
    return o;
}
static jval *api_fs_read(const jval *b, apierr *e)
{
    const char *p = gs(b, "path", "");
    long long mx = (long long)gn(b, "max", 2000000), size;
    char *text;
    size_t len;
    jval *o;
    if (!f_isfile(p)) return fail(e, 400, str_printf("Fișierul nu există: %s", p));
    size = f_size(p);
    text = f_read(p, mx, &len);
    if (!text) return fail(e, 400, str_printf("Nu pot citi %s: %s", p, f_lasterr()));
    o = j_obj();
    j_set(o, "path", j_str_own(path_abs(p)));
    jset_n(o, "size", (double)size);
    j_set(o, "text", j_strn(text, len));
    jset_b(o, "truncated", size > mx);
    free(text);
    return o;
}
static jval *ok_obj(void) { jval *o = j_obj(); jset_b(o, "ok", 1); return o; }
static jval *api_fs_write(const jval *b, apierr *e)
{
    const char *p = gs(b, "path", "");
    const jval *t = G(b, "text");
    char *ap;
    int ok;
    if (!p[0]) return fail(e, 400, xstrdup("Cale lipsă"));
    ap = path_abs(p);
    if (t->t == J_STR) ok = f_write(ap, t->s, t->sl);
    else {
        const char *s = j_text(t, "");
        ok = f_write(ap, s, strlen(s));
    }
    free(ap);
    if (!ok) return fail(e, 400, str_printf("Nu pot scrie %s: %s", p, f_lasterr()));
    return ok_obj();
}
static jval *api_fs_stat(const jval *b)
{
    const char *p = gs(b, "path", "");
    int ex = p[0] && f_exists(p);
    jval *o = j_obj();
    jset_b(o, "exists", ex);
    jset_b(o, "dir", ex && f_isdir(p));
    jset_n(o, "size", ex ? (double)f_size(p) : 0);
    jset_n(o, "mtime", ex ? (double)f_mtime(p) : 0);
    return o;
}
static jval *api_fs_mkdir(const jval *b, apierr *e)
{
    const char *p = gs(b, "path", "");
    if (!p[0]) return fail(e, 400, xstrdup("Cale lipsă"));
    if (!f_mkdirs(p)) return fail(e, 400, str_printf("Nu pot crea %s: %s", p, f_lasterr()));
    return ok_obj();
}
static jval *api_open(const jval *b, apierr *e)
{
    const char *p = gs(b, "path", "");
    if (!f_exists(p)) return fail(e, 400, str_printf("Nu există: %s", p));
    open_path(p);
    return ok_obj();
}
static svec str_list(const jval *v)
{
    svec out;
    size_t i;
    sv_init(&out);
    if (j_is(v, J_ARR)) for (i = 0; i < v->len; i++) sv_push(&out, j_text(v->items[i], ""));
    else if (j_is(v, J_STR)) sv_push(&out, v->s);
    return out;
}

/* ---------------------------------------------------------------- HTTP */
typedef struct {
    char *method, *path, *query, *body;
    size_t body_len;
    svec hk, hv;   /* antete: nume (litere mici) și valori */
} request;
static void req_free(request *r)
{
    free(r->method);
    free(r->path);
    free(r->query);
    free(r->body);
    sv_free(&r->hk);
    sv_free(&r->hv);
}
static const char *header(const request *r, const char *name)
{
    size_t i;
    for (i = 0; i < r->hk.n; i++) if (strcmp(r->hk.v[i], name) == 0) return r->hv.v[i];
    return "";
}
static int recv_all(sock_t c, request *req)
{
    sb buf;
    char tmp[16384];
    char *hdr_end = NULL, *line_end, *s1, *s2, *target, *q, *pos;
    size_t head_len, rest_len;
    long long len;
    memset(req, 0, sizeof *req);
    sv_init(&req->hk);
    sv_init(&req->hv);
    sb_init(&buf);
    while (!hdr_end) {
        int n = (int)recv(c, tmp, sizeof tmp, 0);
        if (n <= 0) { sb_free(&buf); return 0; }
        sb_putn(&buf, tmp, (size_t)n);
        hdr_end = strstr(buf.p, "\r\n\r\n");
        if (buf.n > 65536 && !hdr_end) { sb_free(&buf); return 0; }
    }
    head_len = (size_t)(hdr_end - buf.p);
    *hdr_end = 0;
    line_end = strstr(buf.p, "\r\n");
    if (line_end) *line_end = 0;
    s1 = strchr(buf.p, ' ');
    s2 = s1 ? strchr(s1 + 1, ' ') : NULL;
    if (!s1 || !s2) { sb_free(&buf); return 0; }
    req->method = xstrndup(buf.p, (size_t)(s1 - buf.p));
    target = xstrndup(s1 + 1, (size_t)(s2 - s1 - 1));
    q = strchr(target, '?');
    if (q) {
        req->path = xstrndup(target, (size_t)(q - target));
        req->query = xstrdup(q + 1);
    } else {
        req->path = xstrdup(target);
        req->query = xstrdup("");
    }
    free(target);
    pos = line_end ? line_end + 2 : buf.p + head_len;
    while (pos < buf.p + head_len) {
        char *e = strstr(pos, "\r\n"), *colon;
        if (!e) e = buf.p + head_len;
        *e = 0;
        colon = strchr(pos, ':');
        if (colon) {
            char *k = xstrndup(pos, (size_t)(colon - pos)), *v = colon + 1, *kk;
            size_t vl;
            for (kk = k; *kk; kk++) *kk = (char)tolower((unsigned char)*kk);
            while (*v == ' ' || *v == '\t') v++;
            vl = strlen(v);
            while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) vl--;
            sv_push_own(&req->hk, k);
            sv_push_own(&req->hv, xstrndup(v, vl));
        }
        pos = e + 2;
    }
    len = atoll(header(req, "content-length"));
    if (len < 0 || len > 200LL * 1024 * 1024) { sb_free(&buf); return 0; }
    rest_len = buf.n - head_len - 4;
    {
        sb body;
        sb_init(&body);
        sb_putn(&body, buf.p + head_len + 4, rest_len);
        sb_free(&buf);
        while ((long long)body.n < len) {
            int n = (int)recv(c, tmp, sizeof tmp, 0);
            if (n <= 0) { sb_free(&body); return 0; }
            sb_putn(&body, tmp, (size_t)n);
        }
        if ((long long)body.n > len) body.n = (size_t)len;
        req->body_len = body.n;
        if (body.p) body.p[body.n] = 0;
        req->body = sb_take(&body);
    }
    return 1;
}
static void send_all(sock_t c, const char *data, size_t n)
{
    size_t off = 0;
    while (off < n) {
        size_t chunk = n - off > (1u << 20) ? (1u << 20) : n - off;
        int k = (int)send(c, data + off, (int)chunk, 0);
        if (k <= 0) return;
        off += (size_t)k;
    }
}
static void respond(sock_t c, int code, const char *ctype, const char *body, size_t n)
{
    const char *reason = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" :
                         code == 405 ? "Method Not Allowed" : "Internal Server Error";
    char *head = str_printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                            code, reason, ctype, (unsigned long)n);
    sb all;
    sb_init(&all);
    sb_puts(&all, head);
    sb_putn(&all, body, n);
    send_all(c, all.p, all.n);
    sb_free(&all);
    free(head);
}
static void respond_text(sock_t c, int code, const char *text) { respond(c, code, "text/plain; charset=utf-8", text, strlen(text)); }
static void respond_json(sock_t c, int code, jval *v)
{
    char *t = j_dump(v, -1);
    respond(c, code, "application/json; charset=utf-8", t, strlen(t));
    free(t);
    j_free(v);
}
static jval *err_obj(const char *m) { jval *o = j_obj(); jset_s(o, "error", m); return o; }
static char *qparam(const char *q, const char *k)
{
    svec parts = split_char(q, '&', 0);
    size_t i, kl = strlen(k);
    char *r = xstrdup("");
    for (i = 0; i < parts.n; i++) {
        const char *kv = parts.v[i], *eqs = strchr(kv, '=');
        size_t nl = eqs ? (size_t)(eqs - kv) : strlen(kv);
        if (nl == kl && strncmp(kv, k, kl) == 0) {
            free(r);
            r = xstrdup(eqs ? eqs + 1 : "");
            break;
        }
    }
    sv_free(&parts);
    return r;
}

static void quit_later(void *arg)
{
    (void)arg;
    sleep_ms(300);
    exit(0);
}

static void route_post(sock_t c, const request *req, const jval *body)
{
    const char *p = req->path;
    apierr e = {0, NULL};
    jval *r = NULL;
    if (strcmp(p, "/api/fs/list") == 0) r = api_fs_list(body, &e);
    else if (strcmp(p, "/api/fs/read") == 0) r = api_fs_read(body, &e);
    else if (strcmp(p, "/api/fs/write") == 0) r = api_fs_write(body, &e);
    else if (strcmp(p, "/api/fs/stat") == 0) r = api_fs_stat(body);
    else if (strcmp(p, "/api/fs/mkdir") == 0) r = api_fs_mkdir(body, &e);
    else if (strcmp(p, "/api/open") == 0) r = api_open(body, &e);
    else if (strcmp(p, "/api/structure") == 0) r = eng_structure(G(body, "workspace"), gb(body, "create", 0));
    else if (strcmp(p, "/api/scan") == 0) {
        char *err = NULL;
        r = eng_scan(G(body, "workspace"), gs(body, "problem", ""), &err);
        if (!r) { e.code = str_starts(err, "Eroare internă") ? 500 : 400; e.msg = err; }
    } else if (strcmp(p, "/api/eval/start") == 0) {
        svec ps = str_list(G(body, "problems")), ss = str_list(G(body, "students")), ts = str_list(G(body, "tests"));
        char *err = NULL;
        if (eng_start(G(body, "workspace"), &ps, &ss, &ts, &err)) r = ok_obj();
        else { e.code = 400; e.msg = err; }
        sv_free(&ps);
        sv_free(&ss);
        sv_free(&ts);
    } else if (strcmp(p, "/api/eval/stop") == 0) {
        eng_stop();
        r = ok_obj();
    } else if (strcmp(p, "/api/rescore") == 0) {
        const jval *prob = eng_find_problem(G(body, "workspace"), gs(body, "problem", ""));
        if (!prob) { e.code = 400; e.msg = xstrdup("Problema nu există"); }
        else {
            r = j_obj();
            j_set(r, "results", eng_rescore(G(body, "workspace"), prob, G(body, "results")));
        }
    } else if (strcmp(p, "/api/quit") == 0) {
        respond_json(c, 200, ok_obj());
        thread_start(quit_later, NULL);
        return;
    } else {
        char *m = str_printf("Necunoscut: %s", p);
        respond_json(c, 404, err_obj(m));
        free(m);
        return;
    }
    if (r) respond_json(c, 200, r);
    else {
        respond_json(c, e.code ? e.code : 500, err_obj(e.msg ? e.msg : "Eroare"));
        free(e.msg);
    }
}

static void handle_conn(void *arg)
{
    sock_t c = (sock_t)(size_t)arg;
    request req;
    char want1[64], want2[64];
    const char *host;
    int host_ok, auth;
    if (!recv_all(c, &req)) {
        req_free(&req);
        CLOSESOCK(c);
        return;
    }
    snprintf(want1, sizeof want1, "127.0.0.1:%d", PORT);
    snprintf(want2, sizeof want2, "localhost:%d", PORT);
    host = header(&req, "host");
    host_ok = strcmp(host, want1) == 0 || strcmp(host, want2) == 0;
    auth = host_ok && strcmp(header(&req, "x-eval-token"), TOKEN) == 0;
    if (!host_ok) respond_text(c, 403, "Acces interzis");
    else if (strcmp(req.method, "GET") == 0) {
        if (strcmp(req.path, "/") == 0 || strcmp(req.path, "/index.html") == 0) {
            char *html = str_replace(GUI_HTML, "__EVAL_TOKEN__", TOKEN);
            respond(c, 200, "text/html; charset=utf-8", html, strlen(html));
            free(html);
        } else if (strcmp(req.path, "/api/info") == 0 && auth) respond_json(c, 200, api_info());
        else if (strcmp(req.path, "/api/eval/status") == 0 && auth) {
            char *l = qparam(req.query, "log"), *r = qparam(req.query, "rv");
            respond_json(c, 200, eng_status(l[0] ? atol(l) : 0, r[0] ? atol(r) : -1));
            free(l);
            free(r);
        } else if (str_starts(req.path, "/api/")) respond_json(c, 403, err_obj("Acces interzis"));
        else respond_text(c, 404, "Nu există");
    } else if (strcmp(req.method, "POST") != 0) respond_text(c, 405, "Metodă nepermisă");
    else if (!auth) respond_json(c, 403, err_obj("Acces interzis"));
    else {
        const char *perr = NULL;
        jval *body = req.body_len ? j_parse(req.body, req.body_len, &perr) : j_obj();
        if (!body) respond_json(c, 400, err_obj("JSON invalid"));
        else {
            if (body->t != J_OBJ) { j_free(body); body = j_obj(); }
            route_post(c, &req, body);
            j_free(body);
        }
    }
    req_free(&req);
    CLOSESOCK(c);
}

static void make_token(void)
{
    unsigned char raw[16];
    int i, got = 0;
#ifdef _WIN32
    for (i = 0; i < 4; i++) {
        unsigned int v = 0;
        if (rand_s(&v) != 0) break;
        memcpy(raw + i * 4, &v, 4);
        got = (i == 3);
    }
#else
    {
        FILE *f = fopen("/dev/urandom", "rb");
        if (f) {
            got = fread(raw, 1, sizeof raw, f) == sizeof raw;
            fclose(f);
        }
    }
#endif
    if (!got) {
        /* rezervă: amestec din timp, pid și adresă (splitmix64) */
        unsigned long long x = (unsigned long long)time(NULL) ^ ((unsigned long long)cur_pid() << 32) ^ (unsigned long long)(size_t)&raw;
        for (i = 0; i < 2; i++) {
            unsigned long long z = (x += 0x9E3779B97F4A7C15ULL);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            z ^= z >> 31;
            memcpy(raw + i * 8, &z, 8);
        }
    }
    for (i = 0; i < 16; i++) snprintf(TOKEN + i * 2, 3, "%02x", raw[i]);
}

static int real_main(int argc, char **argv)
{
    int port = 8765, browser = 1, i, p;
    const char *gui_file = NULL;
    sock_t ls = INVALID_SOCKET;
    char *url;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-browser") == 0) browser = 0;
        else if (strcmp(argv[i], "--gui") == 0 && i + 1 < argc) gui_file = argv[++i];
    }
    eng_init();
    GUI_HTML = xstrndup((const char *)EMB_GUI, (size_t)EMB_GUI_LEN);
    GUI_LEN = (size_t)EMB_GUI_LEN;
    PROBE_C_SOURCE = (const char *)EMB_PROBE_C;
    PROBE_C_LEN = (size_t)EMB_PROBE_C_LEN;
    PROBE_PY_SOURCE = (const char *)EMB_PROBE_PY;
    PROBE_PY_LEN = (size_t)EMB_PROBE_PY_LEN;
    if (gui_file) {
        size_t n;
        char *t = f_read(gui_file, -1, &n);
        if (t) { free(GUI_HTML); GUI_HTML = t; GUI_LEN = n; }
    }
    make_token();
#ifdef _WIN32
    {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        SetConsoleOutputCP(CP_UTF8);
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    for (p = port; p < port + 30; p++) {
        struct sockaddr_in a;
        ls = socket(AF_INET, SOCK_STREAM, 0);
        if (ls == INVALID_SOCKET) continue;
#ifndef _WIN32
        {
            int one = 1;
            fcntl(ls, F_SETFD, FD_CLOEXEC);
            setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        }
#else
        SetHandleInformation((HANDLE)ls, HANDLE_FLAG_INHERIT, 0);
#endif
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons((unsigned short)p);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(ls, (struct sockaddr *)&a, sizeof a) == 0 && listen(ls, 64) == 0) { PORT = p; break; }
        CLOSESOCK(ls);
        ls = INVALID_SOCKET;
    }
    if (ls == INVALID_SOCKET) {
        printf("Nu găsesc niciun port liber.\n");
        return 1;
    }
    url = str_printf("http://127.0.0.1:%d/", PORT);
    printf("%s %s — varianta %s\nInterfața: %s\nÎnchide această fereastră (sau Ctrl+C) pentru a opri aplicația.\n", APP_NAME, APP_VERSION, VARIANT, url);
    fflush(stdout);
    if (browser) open_browser(url);
    free(url);
    for (;;) {
        sock_t c = accept(ls, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
#ifndef _WIN32
        fcntl(c, F_SETFD, FD_CLOEXEC);
#else
        SetHandleInformation((HANDLE)c, HANDLE_FLAG_INHERIT, 0);
#endif
        if (!thread_start(handle_conn, (void *)(size_t)c)) CLOSESOCK(c);
    }
    return 0;
}

#ifdef _WIN32
int main(void)
{
    int argc = 0, i, r;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    char **argv = (char **)xmalloc((size_t)(argc + 1) * sizeof(char *));
    for (i = 0; i < argc; i++) argv[i] = rn_w_to_u8(wargv[i]);
    argv[argc] = NULL;
    LocalFree(wargv);
    r = real_main(argc, argv);
    return r;
}
#else
int main(int argc, char **argv) { return real_main(argc, argv); }
#endif
