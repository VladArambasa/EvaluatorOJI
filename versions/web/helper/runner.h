/*
 * RUNNER.H - RUN A PROCESS WITH LIMITS (TIME, MEMORY, STACK, OUTPUT) AND MEASURE IT
 * POSIX: FORK + SETRLIMIT + EXECVE + WAIT4. WINDOWS: CREATEPROCESSW + JOB OBJECT
 * ALL STRINGS ARE UTF-8
 */
#ifndef OLIEVAL_RUNNER_H
#define OLIEVAL_RUNNER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *const *argv;    /* ARGV[0] = PROGRAM; NULL-TERMINATED */
    const char *cwd;            /* WORKING FOLDER (NULL = CURRENT) */
    const char *stdin_path;     /* NULL = NULL DEVICE */
    const char *stdout_path;    /* NULL = NULL DEVICE */
    const char *stderr_path;    /* NULL = NULL DEVICE; SAME AS STDOUT_PATH = SAME FILE */
    const char *const *env_add; /* NAME=VALUE LIST, NULL-TERMINATED (MAY BE NULL) */
    long long wall_ms;          /* WALL TIME LIMIT (0 = NONE) */
    long long cpu_s;            /* CPU TIME LIMIT IN SECONDS (0 = NONE) */
    long long mem_kb;           /* HARD MEMORY LIMIT (0 = NONE) */
    long long stack_kb;         /* STACK LIMIT, POSIX ONLY (0 = DEFAULT) */
    long long fsize_kb;         /* MAX WRITTEN FILE SIZE, POSIX ONLY (0 = NONE) */
    int single_process;         /* WINDOWS: NO CHILD PROCESSES (E.G. SYSTEM PAUSE) */
    volatile int *cancel;       /* NONZERO = STOP THE PROCESS */
} rn_spec;

typedef struct {
    int started;                /* 1 IF THE PROCESS STARTED */
    char error[512];            /* WHY IT DID NOT START */
    int exited;                 /* 1 = NORMAL EXIT (EXIT CODE VALID) */
    int exit_code;              /* EXIT CODE */
    unsigned int win_status;    /* WINDOWS: RAW STATUS (E.G. 0XC00000FD) */
    int signal;                 /* POSIX: TERMINATING SIGNAL */
    int timed_out;              /* STOPPED AT THE TIME LIMIT */
    int cancelled;              /* STOPPED BY THE USER */
    double wall_ms;             /* WALL TIME */
    double cpu_ms;              /* CPU TIME (USER + SYSTEM) */
    long long peak_kb;          /* PEAK RAM (LINUX RU_MAXRSS, WINDOWS PEAKWORKINGSETSIZE) */
    long long peak_commit_kb;   /* WINDOWS: PEAK COMMITTED MEMORY */
    int mem_limit_hit;          /* WINDOWS: JOB OBJECT LIMIT HIT */
} rn_result;

/* RUN THE PROCESS; RETURN 0 IF STARTED, -1 OTHERWISE (SEE RES->ERROR) */
int rn_run(const rn_spec *spec, rn_result *res);

/* FIND A PROGRAM IN PATH (+ USUAL MINGW/PYTHON FOLDERS ON WINDOWS); MALLOC'D OR NULL */
char *rn_find_program(const char *name);

/* DESCRIBE A SIGNAL / RUN STATUS (ENGLISH) */
const char *rn_signal_name(int sig);
const char *rn_status_text(const rn_result *r);

#ifdef _WIN32
#include <wchar.h>
wchar_t *rn_u8_to_w(const char *s);   /* MALLOC'D */
char *rn_w_to_u8(const wchar_t *s);   /* MALLOC'D */
#endif

#ifdef __cplusplus
}
#endif
#endif
