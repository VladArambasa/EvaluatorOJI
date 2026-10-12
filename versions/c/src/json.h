/*
 * JSON.H - MINIMAL JSON FOR THE C VERSION; OBJECTS KEEP KEY ORDER; UTF-8 STRINGS
 * (INVALID BYTES BECOME U+FFFD ON OUTPUT) THAT MAY HOLD NUL BYTES (EXPLICIT LENGTH)
 * OWNERSHIP: J_SET/J_PUSH TAKE THE VALUE; J_FREE FREES THE WHOLE SUBTREE
 */
#ifndef OLIEVAL_JSON_H
#define OLIEVAL_JSON_H

#include <stddef.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;

typedef struct jval jval;
struct jval {
    jtype t;
    int b;
    double n;
    char *s;          /* J_STR: TEXT; J_NUM: CACHED TEXT FORM */
    size_t sl;        /* LENGTH OF S FOR J_STR */
    size_t len, cap;  /* J_ARR / J_OBJ */
    jval **items;
    char **keys;      /* J_OBJ ONLY */
};

/* CONSTRUCTORS */
jval *j_null(void);
jval *j_bool(int b);
jval *j_num(double n);
jval *j_str(const char *s);
jval *j_strn(const char *s, size_t n);
jval *j_str_own(char *s);          /* TAKES THE STRING (MALLOC'D) */
jval *j_arr(void);
jval *j_obj(void);
jval *j_clone(const jval *v);
void j_free(jval *v);

/* ACCESS */
const jval *j_nil(void);                         /* THE STATIC NULL VALUE */
jval *j_get(const jval *o, const char *k);       /* NULL IF MISSING OR O IS NOT AN OBJECT */
const jval *G(const jval *o, const char *k);     /* NEVER NULL (STATIC NULL) */
const jval *j_at(const jval *a, size_t i);
size_t j_len(const jval *v);
int j_is(const jval *v, jtype t);

/* VALUES WITH DEFAULTS: MISSING/NULL -> DEFAULT (LIKE G() IN PYTHON) */
const char *j_text(const jval *v, const char *def);    /* STR/NUM/BOOL -> TEXT; ELSE DEF */
double j_number(const jval *v, double def);
int j_truthy(const jval *v, int def);
const char *gs(const jval *o, const char *k, const char *def);
double gn(const jval *o, const char *k, double def);
int gb(const jval *o, const char *k, int def);

/* MODIFICATION */
jval *j_set(jval *o, const char *k, jval *v);    /* RETURNS V */
void jset_s(jval *o, const char *k, const char *s);
void jset_n(jval *o, const char *k, double n);
void jset_b(jval *o, const char *k, int b);
void jset_null(jval *o, const char *k);
jval *j_push(jval *a, jval *v);                  /* RETURNS V */
void j_replace_at(jval *a, size_t i, jval *v);   /* FREES THE OLD VALUE */
void j_remove(jval *o, const char *k);

/* TEXT */
char *j_numstr(double x);                        /* MALLOC'D */
char *j_dump(const jval *v, int indent);         /* MALLOC'D; INDENT < 0 = COMPACT */
jval *j_parse(const char *text, size_t len, const char **err);   /* NULL + *ERR ON ERROR */

#endif
