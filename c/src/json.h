/*
 * json.h — JSON minimal pentru varianta C. Obiectele păstrează ordinea cheilor; șirurile sunt UTF-8
 * (secvențele invalide devin U+FFFD la scriere) și pot conține octeți nuli (lungime explicită).
 *
 * Proprietate: j_set/j_push preiau valoarea primită; j_free eliberează tot subarborele.
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
    char *s;          /* J_STR: textul; J_NUM: reprezentarea text (calculată la cerere) */
    size_t sl;        /* lungimea lui s pentru J_STR */
    size_t len, cap;  /* J_ARR / J_OBJ */
    jval **items;
    char **keys;      /* doar J_OBJ */
};

/* constructori */
jval *j_null(void);
jval *j_bool(int b);
jval *j_num(double n);
jval *j_str(const char *s);
jval *j_strn(const char *s, size_t n);
jval *j_str_own(char *s);          /* preia șirul (malloc) */
jval *j_arr(void);
jval *j_obj(void);
jval *j_clone(const jval *v);
void j_free(jval *v);

/* acces */
const jval *j_nil(void);                         /* valoarea null statică */
jval *j_get(const jval *o, const char *k);       /* NULL dacă lipsește sau o nu e obiect */
const jval *G(const jval *o, const char *k);     /* niciodată NULL (null static) */
const jval *j_at(const jval *a, size_t i);
size_t j_len(const jval *v);
int j_is(const jval *v, jtype t);

/* valori cu implicit: lipsă/null -> implicit (ca g() din Python) */
const char *j_text(const jval *v, const char *def);    /* str/num/bool -> text; altfel def */
double j_number(const jval *v, double def);
int j_truthy(const jval *v, int def);
const char *gs(const jval *o, const char *k, const char *def);
double gn(const jval *o, const char *k, double def);
int gb(const jval *o, const char *k, int def);

/* modificare */
jval *j_set(jval *o, const char *k, jval *v);    /* întoarce v */
void jset_s(jval *o, const char *k, const char *s);
void jset_n(jval *o, const char *k, double n);
void jset_b(jval *o, const char *k, int b);
void jset_null(jval *o, const char *k);
jval *j_push(jval *a, jval *v);                  /* întoarce v */
void j_replace_at(jval *a, size_t i, jval *v);   /* eliberează vechea valoare */
void j_remove(jval *o, const char *k);

/* text */
char *j_numstr(double x);                        /* malloc */
char *j_dump(const jval *v, int indent);         /* malloc; indent < 0 = compact */
jval *j_parse(const char *text, size_t len, const char **err);   /* NULL + *err la eroare */

#endif
