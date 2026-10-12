/* JSON.C - SEE JSON.H */
#include "json.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static jval *mk(jtype t)
{
    jval *v = (jval *)xmalloc(sizeof *v);
    memset(v, 0, sizeof *v);
    v->t = t;
    return v;
}
jval *j_null(void) { return mk(J_NULL); }
jval *j_bool(int b) { jval *v = mk(J_BOOL); v->b = b ? 1 : 0; return v; }
jval *j_num(double n) { jval *v = mk(J_NUM); v->n = n; return v; }
jval *j_strn(const char *s, size_t n)
{
    jval *v = mk(J_STR);
    v->s = xstrndup(s ? s : "", s ? n : 0);
    v->sl = s ? n : 0;
    return v;
}
jval *j_str(const char *s) { return j_strn(s ? s : "", s ? strlen(s) : 0); }
jval *j_str_own(char *s)
{
    jval *v = mk(J_STR);
    v->s = s ? s : xstrdup("");
    v->sl = strlen(v->s);
    return v;
}
jval *j_arr(void) { return mk(J_ARR); }
jval *j_obj(void) { return mk(J_OBJ); }

void j_free(jval *v)
{
    size_t i;
    if (!v) return;
    for (i = 0; i < v->len; i++) {
        j_free(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->s);
    free(v);
}

jval *j_clone(const jval *v)
{
    jval *c;
    size_t i;
    if (!v) return j_null();
    c = mk(v->t);
    c->b = v->b;
    c->n = v->n;
    if (v->t == J_STR) { c->s = xstrndup(v->s, v->sl); c->sl = v->sl; }
    if (v->t == J_ARR || v->t == J_OBJ) {
        c->cap = c->len = v->len;
        c->items = (jval **)xmalloc((v->len ? v->len : 1) * sizeof(jval *));
        if (v->t == J_OBJ) c->keys = (char **)xmalloc((v->len ? v->len : 1) * sizeof(char *));
        for (i = 0; i < v->len; i++) {
            c->items[i] = j_clone(v->items[i]);
            if (v->t == J_OBJ) c->keys[i] = xstrdup(v->keys[i]);
        }
    }
    return c;
}

static jval NIL = {J_NULL, 0, 0, NULL, 0, 0, 0, NULL, NULL};
const jval *j_nil(void) { return &NIL; }

jval *j_get(const jval *o, const char *k)
{
    size_t i;
    if (!o || o->t != J_OBJ) return NULL;
    for (i = 0; i < o->len; i++) if (strcmp(o->keys[i], k) == 0) return o->items[i];
    return NULL;
}
const jval *G(const jval *o, const char *k)
{
    const jval *v = j_get(o, k);
    return v ? v : &NIL;
}
const jval *j_at(const jval *a, size_t i)
{
    if (!a || (a->t != J_ARR && a->t != J_OBJ) || i >= a->len) return &NIL;
    return a->items[i];
}
size_t j_len(const jval *v) { return (v && (v->t == J_ARR || v->t == J_OBJ)) ? v->len : 0; }
int j_is(const jval *v, jtype t) { return v && v->t == t; }

char *j_numstr(double x)
{
    char buf[64];
    int prec;
    if (isnan(x) || isinf(x)) return xstrdup("null");
    if (x == floor(x) && fabs(x) < 1e15) {
        snprintf(buf, sizeof buf, "%.0f", x);
        return xstrdup(buf);
    }
    for (prec = 15; prec <= 17; prec++) {
        snprintf(buf, sizeof buf, "%.*g", prec, x);
        if (strtod(buf, NULL) == x) break;
    }
    return xstrdup(buf);
}

const char *j_text(const jval *v, const char *def)
{
    if (!v) return def;
    if (v->t == J_STR) return v->s;
    if (v->t == J_NUM) {
        /* THE TEXT FORM IS CACHED IN THE VALUE */
        jval *m = (jval *)v;
        if (!m->s) m->s = j_numstr(v->n);
        return m->s;
    }
    if (v->t == J_BOOL) return v->b ? "true" : "false";
    return def;
}
double j_number(const jval *v, double def)
{
    if (!v) return def;
    if (v->t == J_NUM) return v->n;
    if (v->t == J_BOOL) return v->b ? 1 : 0;
    if (v->t == J_STR) {
        char *end = NULL;
        double d;
        if (!v->s[0]) return def;
        d = strtod(v->s, &end);
        if (end && *end == 0 && isfinite(d)) return d;
    }
    return def;
}
int j_truthy(const jval *v, int def)
{
    if (!v || v->t == J_NULL) return def;
    if (v->t == J_BOOL) return v->b;
    if (v->t == J_NUM) return v->n != 0;
    if (v->t == J_STR) return v->sl > 0;
    return v->len > 0;
}
const char *gs(const jval *o, const char *k, const char *def)
{
    const jval *v = j_get(o, k);
    if (!v || v->t == J_NULL) return def;
    return j_text(v, def);
}
double gn(const jval *o, const char *k, double def)
{
    const jval *v = j_get(o, k);
    if (!v || v->t == J_NULL) return def;
    return j_number(v, def);
}
int gb(const jval *o, const char *k, int def) { return j_truthy(j_get(o, k), def); }

static void grow(jval *v)
{
    if (v->len + 1 > v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->items = (jval **)xrealloc(v->items, v->cap * sizeof(jval *));
        if (v->t == J_OBJ) v->keys = (char **)xrealloc(v->keys, v->cap * sizeof(char *));
    }
}
static void reset_to(jval *v, jtype t)
{
    size_t i;
    for (i = 0; i < v->len; i++) {
        j_free(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->s);
    v->items = NULL;
    v->keys = NULL;
    v->s = NULL;
    v->len = v->cap = 0;
    v->sl = 0;
    v->t = t;
}
jval *j_set(jval *o, const char *k, jval *v)
{
    size_t i;
    if (o->t != J_OBJ) reset_to(o, J_OBJ);
    for (i = 0; i < o->len; i++) {
        if (strcmp(o->keys[i], k) == 0) {
            j_free(o->items[i]);
            o->items[i] = v;
            return v;
        }
    }
    grow(o);
    o->keys[o->len] = xstrdup(k);
    o->items[o->len++] = v;
    return v;
}
void jset_s(jval *o, const char *k, const char *s) { j_set(o, k, j_str(s)); }
void jset_n(jval *o, const char *k, double n) { j_set(o, k, j_num(n)); }
void jset_b(jval *o, const char *k, int b) { j_set(o, k, j_bool(b)); }
void jset_null(jval *o, const char *k) { j_set(o, k, j_null()); }
jval *j_push(jval *a, jval *v)
{
    if (a->t != J_ARR) reset_to(a, J_ARR);
    grow(a);
    a->items[a->len++] = v;
    return v;
}
void j_replace_at(jval *a, size_t i, jval *v)
{
    if (i >= a->len) { j_free(v); return; }
    j_free(a->items[i]);
    a->items[i] = v;
}
void j_remove(jval *o, const char *k)
{
    size_t i;
    if (!o || o->t != J_OBJ) return;
    for (i = 0; i < o->len; i++) {
        if (strcmp(o->keys[i], k) == 0) {
            j_free(o->items[i]);
            free(o->keys[i]);
            memmove(o->items + i, o->items + i + 1, (o->len - i - 1) * sizeof(jval *));
            memmove(o->keys + i, o->keys + i + 1, (o->len - i - 1) * sizeof(char *));
            o->len--;
            return;
        }
    }
}

/* --- WRITER --- */
static void escape(sb *out, const char *str, size_t n)
{
    const unsigned char *p = (const unsigned char *)str;
    size_t i = 0;
    sb_putc(out, '"');
    while (i < n) {
        unsigned char c = p[i];
        if (c == '"') { sb_puts(out, "\\\""); i++; }
        else if (c == '\\') { sb_puts(out, "\\\\"); i++; }
        else if (c == '\n') { sb_puts(out, "\\n"); i++; }
        else if (c == '\r') { sb_puts(out, "\\r"); i++; }
        else if (c == '\t') { sb_puts(out, "\\t"); i++; }
        else if (c < 0x20) { sb_printf(out, "\\u%04x", c); i++; }
        else if (c < 0x80) { sb_putc(out, (char)c); i++; }
        else {
            size_t len = 0, k;
            unsigned cp = 0;
            int ok;
            if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
            else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
            else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
            ok = len > 0 && i + len <= n;
            for (k = 1; ok && k < len; k++) {
                if ((p[i + k] & 0xC0) != 0x80) ok = 0;
                else cp = (cp << 6) | (p[i + k] & 0x3F);
            }
            if (ok && ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ||
                       (cp >= 0xD800 && cp <= 0xDFFF))) ok = 0;
            if (ok) { sb_putn(out, (const char *)p + i, len); i += len; }
            else { sb_puts(out, "\xEF\xBF\xBD"); i++; }
        }
    }
    sb_putc(out, '"');
}
static void nl(sb *out, int indent, int lv)
{
    int i;
    if (indent < 0) return;
    sb_putc(out, '\n');
    for (i = 0; i < indent * lv; i++) sb_putc(out, ' ');
}
static void dump_to(sb *out, const jval *v, int indent, int level)
{
    size_t i;
    switch (v->t) {
    case J_NULL: sb_puts(out, "null"); break;
    case J_BOOL: sb_puts(out, v->b ? "true" : "false"); break;
    case J_NUM: { char *s = j_numstr(v->n); sb_puts(out, s); free(s); break; }
    case J_STR: escape(out, v->s, v->sl); break;
    case J_ARR:
        sb_putc(out, '[');
        for (i = 0; i < v->len; i++) {
            if (i) sb_putc(out, ',');
            nl(out, indent, level + 1);
            dump_to(out, v->items[i], indent, level + 1);
        }
        if (v->len) nl(out, indent, level);
        sb_putc(out, ']');
        break;
    case J_OBJ:
        sb_putc(out, '{');
        for (i = 0; i < v->len; i++) {
            if (i) sb_putc(out, ',');
            nl(out, indent, level + 1);
            escape(out, v->keys[i], strlen(v->keys[i]));
            sb_puts(out, indent >= 0 ? ": " : ":");
            dump_to(out, v->items[i], indent, level + 1);
        }
        if (v->len) nl(out, indent, level);
        sb_putc(out, '}');
        break;
    }
}
char *j_dump(const jval *v, int indent)
{
    sb out;
    sb_init(&out);
    dump_to(&out, v ? v : &NIL, indent, 0);
    return sb_take(&out);
}

/* --- READER --- */
typedef struct { const char *p, *e; const char *err; } parser;

static void ws(parser *ps) { while (ps->p < ps->e && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++; }
static int fail(parser *ps, const char *m) { if (!ps->err) ps->err = m; return 0; }
static void put_utf8(sb *s, unsigned cp)
{
    if (cp < 0x80) sb_putc(s, (char)cp);
    else if (cp < 0x800) { sb_putc(s, (char)(0xC0 | (cp >> 6))); sb_putc(s, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        sb_putc(s, (char)(0xE0 | (cp >> 12)));
        sb_putc(s, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(s, (char)(0x80 | (cp & 0x3F)));
    } else {
        sb_putc(s, (char)(0xF0 | (cp >> 18)));
        sb_putc(s, (char)(0x80 | ((cp >> 12) & 0x3F)));
        sb_putc(s, (char)(0x80 | ((cp >> 6) & 0x3F)));
        sb_putc(s, (char)(0x80 | (cp & 0x3F)));
    }
}
static int hex4(parser *ps, unsigned *v)
{
    int i;
    if (ps->e - ps->p < 4) return fail(ps, "incomplete \\u");
    *v = 0;
    for (i = 0; i < 4; i++) {
        char c = *ps->p++;
        *v <<= 4;
        if (c >= '0' && c <= '9') *v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') *v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') *v |= (unsigned)(c - 'A' + 10);
        else return fail(ps, "invalid \\u");
    }
    return 1;
}
static int pstr(parser *ps, sb *out)
{
    if (ps->p >= ps->e || *ps->p != '"') return fail(ps, "expected \"");
    ps->p++;
    while (ps->p < ps->e && *ps->p != '"') {
        char c = *ps->p++, x;
        if (c != '\\') {
            const char *q = ps->p;
            /* BULK COPY UP TO THE NEXT SPECIAL CHAR */
            while (q < ps->e && *q != '"' && *q != '\\') q++;
            sb_putc(out, c);
            sb_putn(out, ps->p, (size_t)(q - ps->p));
            ps->p = q;
            continue;
        }
        if (ps->p >= ps->e) return fail(ps, "unterminated string");
        x = *ps->p++;
        switch (x) {
        case '"': sb_putc(out, '"'); break;
        case '\\': sb_putc(out, '\\'); break;
        case '/': sb_putc(out, '/'); break;
        case 'b': sb_putc(out, '\b'); break;
        case 'f': sb_putc(out, '\f'); break;
        case 'n': sb_putc(out, '\n'); break;
        case 'r': sb_putc(out, '\r'); break;
        case 't': sb_putc(out, '\t'); break;
        case 'u': {
            unsigned cp, lo;
            if (!hex4(ps, &cp)) return 0;
            if (cp >= 0xD800 && cp <= 0xDBFF && ps->e - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
                const char *save = ps->p;
                ps->p += 2;
                if (hex4(ps, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                else { ps->p = save; ps->err = NULL; }
            }
            put_utf8(out, cp);
            break;
        }
        default: return fail(ps, "invalid escape");
        }
    }
    if (ps->p >= ps->e) return fail(ps, "unterminated string");
    ps->p++;
    return 1;
}
static jval *pvalue(parser *ps, int depth)
{
    char c;
    if (depth > 200) { fail(ps, "nesting too deep"); return NULL; }
    ws(ps);
    if (ps->p >= ps->e) { fail(ps, "unexpected end"); return NULL; }
    c = *ps->p;
    if (c == '{') {
        jval *v = j_obj();
        ps->p++;
        ws(ps);
        if (ps->p < ps->e && *ps->p == '}') { ps->p++; return v; }
        for (;;) {
            sb k;
            jval *x;
            char *key;
            ws(ps);
            sb_init(&k);
            if (!pstr(ps, &k)) { sb_free(&k); j_free(v); return NULL; }
            key = sb_take(&k);
            ws(ps);
            if (ps->p >= ps->e || *ps->p != ':') { free(key); j_free(v); fail(ps, "expected :"); return NULL; }
            ps->p++;
            x = pvalue(ps, depth + 1);
            if (!x) { free(key); j_free(v); return NULL; }
            j_set(v, key, x);
            free(key);
            ws(ps);
            if (ps->p < ps->e && *ps->p == ',') { ps->p++; continue; }
            if (ps->p < ps->e && *ps->p == '}') { ps->p++; return v; }
            j_free(v);
            fail(ps, "expected , or }");
            return NULL;
        }
    }
    if (c == '[') {
        jval *v = j_arr();
        ps->p++;
        ws(ps);
        if (ps->p < ps->e && *ps->p == ']') { ps->p++; return v; }
        for (;;) {
            jval *x = pvalue(ps, depth + 1);
            if (!x) { j_free(v); return NULL; }
            j_push(v, x);
            ws(ps);
            if (ps->p < ps->e && *ps->p == ',') { ps->p++; continue; }
            if (ps->p < ps->e && *ps->p == ']') { ps->p++; return v; }
            j_free(v);
            fail(ps, "expected , or ]");
            return NULL;
        }
    }
    if (c == '"') {
        sb s;
        jval *v;
        sb_init(&s);
        if (!pstr(ps, &s)) { sb_free(&s); return NULL; }
        v = mk(J_STR);
        v->sl = s.n;
        v->s = sb_take(&s);
        return v;
    }
    if (ps->e - ps->p >= 4 && memcmp(ps->p, "true", 4) == 0) { ps->p += 4; return j_bool(1); }
    if (ps->e - ps->p >= 5 && memcmp(ps->p, "false", 5) == 0) { ps->p += 5; return j_bool(0); }
    if (ps->e - ps->p >= 4 && memcmp(ps->p, "null", 4) == 0) { ps->p += 4; return j_null(); }
    if (c == '-' || (c >= '0' && c <= '9')) {
        const char *s0 = ps->p;
        char tok[128], *end = NULL;
        size_t n;
        double d;
        if (*ps->p == '-') ps->p++;
        while (ps->p < ps->e && ((*ps->p >= '0' && *ps->p <= '9') || *ps->p == '.' || *ps->p == 'e' || *ps->p == 'E' || *ps->p == '+' || *ps->p == '-')) ps->p++;
        n = (size_t)(ps->p - s0);
        if (n >= sizeof tok) { fail(ps, "invalid number"); return NULL; }
        memcpy(tok, s0, n);
        tok[n] = 0;
        d = strtod(tok, &end);
        if (!end || *end) { fail(ps, "invalid number"); return NULL; }
        return j_num(d);
    }
    fail(ps, "invalid value");
    return NULL;
}
jval *j_parse(const char *text, size_t len, const char **err)
{
    parser ps;
    jval *v;
    ps.p = text;
    ps.e = text + len;
    ps.err = NULL;
    if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) ps.p += 3;
    v = pvalue(&ps, 0);
    if (!v) {
        if (err) *err = ps.err ? ps.err : "invalid JSON";
        return NULL;
    }
    ws(&ps);
    if (ps.p != ps.e) {
        j_free(v);
        if (err) *err = "text after JSON";
        return NULL;
    }
    if (err) *err = NULL;
    return v;
}
