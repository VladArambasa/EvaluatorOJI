/* UTIL.H - C VERSION UTILITIES: STRINGS, LISTS, PATHS, FILES (UTF-8; W APIS ON WINDOWS), TIME, THREADS */
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

/* --- MEMORY --- */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* --- DYNAMIC STRINGS --- */
typedef struct { char *p; size_t n, cap; } sb;
void sb_init(sb *b);
void sb_free(sb *b);
void sb_putn(sb *b, const char *s, size_t n);
void sb_puts(sb *b, const char *s);
void sb_putc(sb *b, char c);
void sb_printf(sb *b, const char *fmt, ...);
char *sb_take(sb *b);                     /* RETURN THE STRING (MALLOC'D) AND RESET THE BUFFER */
char *str_printf(const char *fmt, ...);   /* MALLOC'D */
char *str_replace(const char *s, const char *a, const char *b);  /* MALLOC'D */
char *str_lower(const char *s);           /* MALLOC'D, ASCII ONLY */
char *str_trim(const char *s);            /* MALLOC'D */
int str_starts(const char *s, const char *pre);
int str_ends(const char *s, const char *suf);

/* --- STRING LISTS --- */
typedef struct { char **v; size_t n, cap; } svec;
void sv_init(svec *s);
void sv_free(svec *s);
void sv_push(svec *s, const char *x);     /* COPIES */
void sv_push_own(svec *s, char *x);       /* TAKES OWNERSHIP */
void sv_extend(svec *s, const svec *o);
int sv_contains(const svec *s, const char *x);
void sv_sort_nat(svec *s);
void sv_sort_str(svec *s);
svec split_cmd(const char *s);            /* WORDS; DOUBLE QUOTES GROUP */
svec split_ws(const char *s);             /* BY WHITESPACE */
svec split_char(const char *s, char c, int skip_empty);

/* --- COMPARISONS --- */
int natcmp(const char *a, const char *b);
int glob_match(const char *pat, const char *name);
int path_match(const char *pat, const char *rel);   /* INDEX OR -1 */
int glob_full(const char *pat, const char *s);
char *ext_of(const char *name);                     /* MALLOC'D, E.G. .CPP OR EMPTY */

/* --- PATHS --- */
int path_isabs(const char *p);
char *path_norm(const char *p);
char *path_abs(const char *p);
char *path_resolve(const char *base, const char *p);
char *path_join(const char *a, const char *b);
char *path_dir(const char *p);
const char *path_base(const char *p);
char *path_rel(const char *p, const char *base);    /* WITH / OR P NORMALIZED */

/* --- FILES --- */
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
char *f_read(const char *p, long long limit, size_t *len);   /* MALLOC'D + TRAILING 0; NULL ON ERROR */
int f_write(const char *p, const char *data, size_t len);     /* CREATES PARENT FOLDERS */
int f_copy(const char *from, const char *to);
void f_remove(const char *p);
void f_cleardir(const char *p);
const char *f_lasterr(void);

/* --- ENVIRONMENT, TIME --- */
char *env_get(const char *name);          /* MALLOC'D (EMPTY IF MISSING) */
char *home_dir(void);
char *current_dir(void);
char *exe_dir(void);
double now_ms(void);
char *now_str(void);                      /* YYYY-MM-DD HH:MM:SS */
char *stamp_str(void);                    /* YYYYMMDD-HHMMSS */
char *clock_str(void);                    /* HH:MM:SS */
int cur_pid(void);
void sleep_ms(int ms);
void open_path(const char *path);
void open_browser(const char *url);

/* --- THREADS --- */
typedef void (*thread_fn)(void *arg);
int thread_start(thread_fn fn, void *arg);
typedef struct { void *h; } mutex_t;
void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);

#endif
