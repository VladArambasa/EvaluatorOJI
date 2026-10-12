/*
 * util.h — utilitare ale variantei C: șiruri, liste, căi, fișiere (UTF-8; pe Windows API-urile „W”),
 * timp, fire de execuție.
 */
#ifndef OLIEVAL_UTIL_H
#define OLIEVAL_UTIL_H

#include <stddef.h>
#include <stdarg.h>

#ifdef _WIN32
#define IS_WIN 1
#define SEPC '\\'
#define SEPS "\\"
#else
#define IS_WIN 0
#define SEPC '/'
#define SEPS "/"
#endif

const char *os_name(void);

/* ---------------------------------------------------------------- memorie */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* ---------------------------------------------------------------- șiruri dinamice */
typedef struct { char *p; size_t n, cap; } sb;
void sb_init(sb *b);
void sb_free(sb *b);
void sb_putn(sb *b, const char *s, size_t n);
void sb_puts(sb *b, const char *s);
void sb_putc(sb *b, char c);
void sb_printf(sb *b, const char *fmt, ...);
char *sb_take(sb *b);                     /* întoarce șirul (malloc) și golește bufferul */
char *str_printf(const char *fmt, ...);   /* malloc */
char *str_replace(const char *s, const char *a, const char *b);  /* malloc */
char *str_lower(const char *s);           /* malloc, doar ASCII */
char *str_trim(const char *s);            /* malloc */
int str_starts(const char *s, const char *pre);
int str_ends(const char *s, const char *suf);

/* ---------------------------------------------------------------- liste de șiruri */
typedef struct { char **v; size_t n, cap; } svec;
void sv_init(svec *s);
void sv_free(svec *s);
void sv_push(svec *s, const char *x);     /* copiază */
void sv_push_own(svec *s, char *x);       /* preia */
void sv_extend(svec *s, const svec *o);
int sv_contains(const svec *s, const char *x);
void sv_sort_nat(svec *s);
void sv_sort_str(svec *s);
svec split_cmd(const char *s);            /* cuvinte; ghilimelele grupează */
svec split_ws(const char *s);             /* după spații albe */
svec split_char(const char *s, char c, int skip_empty);

/* ---------------------------------------------------------------- comparații */
int natcmp(const char *a, const char *b);
int glob_match(const char *pat, const char *name);
int path_match(const char *pat, const char *rel);   /* index sau -1 */
int glob_full(const char *pat, const char *s);
char *ext_of(const char *name);                     /* malloc, ".cpp" sau "" */

/* ---------------------------------------------------------------- căi */
int path_isabs(const char *p);
char *path_norm(const char *p);
char *path_abs(const char *p);
char *path_resolve(const char *base, const char *p);
char *path_join(const char *a, const char *b);
char *path_dir(const char *p);
const char *path_base(const char *p);
char *path_rel(const char *p, const char *base);    /* cu '/' sau p normalizat */

/* ---------------------------------------------------------------- fișiere */
int f_isfile(const char *p);
int f_isdir(const char *p);
int f_exists(const char *p);
long long f_size(const char *p);
long long f_mtime(const char *p);
int f_mkdirs(const char *p);
typedef struct { char *name; int dir; long long size; long long mtime; } dirent_t;
typedef struct { dirent_t *v; size_t n, cap; } dirlist;
int f_listdir(const char *p, dirlist *out);
void dl_free(dirlist *d);
void dl_sort_nat(dirlist *d);
char *f_read(const char *p, long long limit, size_t *len);   /* malloc + 0 la final; NULL la eroare */
int f_write(const char *p, const char *data, size_t len);     /* creează folderele părinte */
int f_copy(const char *from, const char *to);
void f_remove(const char *p);
void f_cleardir(const char *p);
const char *f_lasterr(void);

/* ---------------------------------------------------------------- mediu, timp */
char *env_get(const char *name);          /* malloc ("" dacă lipsește) */
char *home_dir(void);
char *current_dir(void);
char *exe_dir(void);
double now_ms(void);
char *now_str(void);                      /* AAAA-LL-ZZ HH:MM:SS */
char *stamp_str(void);                    /* AAAALLZZ-HHMMSS */
char *clock_str(void);                    /* HH:MM:SS */
int cur_pid(void);
void sleep_ms(int ms);
void open_path(const char *path);
void open_browser(const char *url);

/* ---------------------------------------------------------------- fire de execuție */
typedef void (*thread_fn)(void *arg);
int thread_start(thread_fn fn, void *arg);
typedef struct { void *h; } mutex_t;
void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#endif
