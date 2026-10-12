/* ENGINE.H - EVALUATION ENGINE OF THE C VERSION (FAITHFUL PORT OF THE PYTHON REFERENCE) */
#ifndef OLIEVAL_ENGINE_H
#define OLIEVAL_ENGINE_H

#include "json.h"
#include "util.h"

extern const char *VARIANT;
extern const char *PROBE_C_SOURCE;
extern size_t PROBE_C_LEN;
extern const char *PROBE_PY_SOURCE;
extern size_t PROBE_PY_LEN;

void eng_init(void);                                                     /* ONCE, AT STARTUP */
const jval *eng_find_problem(const jval *ws, const char *name);
jval *eng_scan(const jval *ws, const char *problem, char **err);          /* NULL + *ERR (MALLOC'D) */
jval *eng_structure(const jval *ws, int create);
jval *eng_rescore(const jval *ws, const jval *prob, const jval *res);     /* NEW RESULT */
int eng_start(const jval *ws, const svec *problems, const svec *students, const svec *tests, char **err);
void eng_stop(void);
jval *eng_status(long since, long rv);

#endif
