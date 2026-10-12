/*
 * runner.h — lansează un proces cu limite (timp, memorie, stivă, ieșire) și îl măsoară.
 * Linux/macOS: fork + setrlimit + execve + wait4.   Windows: CreateProcessW + Job Object.
 * Toate șirurile sunt UTF-8.
 */
#ifndef OLIEVAL_RUNNER_H
#define OLIEVAL_RUNNER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *const *argv;    /* argv[0] = programul; listă terminată cu NULL */
    const char *cwd;            /* folderul curent (NULL = cel al evaluatorului) */
    const char *stdin_path;     /* NULL = dispozitivul nul */
    const char *stdout_path;    /* NULL = dispozitivul nul */
    const char *stderr_path;    /* NULL = dispozitivul nul; egal cu stdout_path = același fișier */
    const char *const *env_add; /* "NUME=VALOARE", terminată cu NULL (poate fi NULL) */
    long long wall_ms;          /* limita de timp real (0 = fără) */
    long long cpu_s;            /* limita de timp procesor în secunde (0 = fără) */
    long long mem_kb;           /* limita dură de memorie (0 = fără) */
    long long stack_kb;         /* limita stivei — doar POSIX (0 = implicit) */
    long long fsize_kb;         /* dimensiunea maximă a unui fișier scris — doar POSIX (0 = fără) */
    int single_process;         /* Windows: interzice procesele copil (system("pause") etc.) */
    volatile int *cancel;       /* dacă devine != 0, procesul este oprit */
} rn_spec;

typedef struct {
    int started;                /* 1 dacă procesul a pornit */
    char error[512];            /* motivul pentru care nu a pornit */
    int exited;                 /* 1 = s-a terminat normal (cod de ieșire) */
    int exit_code;              /* codul de ieșire */
    unsigned int win_status;    /* Windows: codul brut (ex. 0xC00000FD) */
    int signal;                 /* POSIX: semnalul care l-a oprit */
    int timed_out;              /* oprit la limita de timp */
    int cancelled;              /* oprit de utilizator */
    double wall_ms;             /* timp real */
    double cpu_ms;              /* timp procesor (user + sistem) */
    long long peak_kb;          /* RAM maxim (Linux ru_maxrss, Windows PeakWorkingSetSize) */
    long long peak_commit_kb;   /* Windows: memoria comisă maximă */
    int mem_limit_hit;          /* Windows: limita Job Object atinsă */
} rn_result;

/* Rulează procesul; întoarce 0 dacă a pornit, -1 altfel (vezi res->error). */
int rn_run(const rn_spec *spec, rn_result *res);

/* Caută un program în PATH (+ locații uzuale MinGW/Python pe Windows). Rezultat malloc sau NULL. */
char *rn_find_program(const char *name);

/* Descrierea unui semnal / cod NTSTATUS, în română. */
const char *rn_signal_name(int sig);
const char *rn_status_text(const rn_result *r);

#ifdef _WIN32
#include <wchar.h>
wchar_t *rn_u8_to_w(const char *s);   /* malloc */
char *rn_w_to_u8(const wchar_t *s);   /* malloc */
#endif

#ifdef __cplusplus
}
#endif
#endif
