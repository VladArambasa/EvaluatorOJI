/*
 * olieval_run — utilitar folosit de varianta Web (Node.js) a Evaluatorului Olimpiadă
 * pentru a rula un program cu limite și a-l măsura (Node.js nu poate seta limite de memorie
 * și nu poate citi timpul procesor al proceselor copil).
 *
 *   olieval-run [opțiuni] -- program argumente...
 *     --cwd DIR  --stdin F  --stdout F  --stderr F   (implicit: dispozitivul nul)
 *     --wall MS  --cpu S  --mem KB  --stack KB  --fsize KB  --single
 *     --env NUME=VALOARE (se poate repeta)   --result FIȘIER (implicit: ieșirea standard)
 *
 * Rezultat: un obiect JSON pe o linie.
 */
/* API Windows minim: Windows 7 (unele MinGW mai vechi pornesc de la XP/2003) */
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include "runner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

/* la SIGTERM/SIGINT (oprire cerută de evaluator) programul elevului este oprit și el */
static volatile int g_cancel = 0;
static void on_signal(int sig) { (void)sig; g_cancel = 1; }

static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
        else if (c == '\n') fputs("\\n", f);
        else if (c == '\r') fputs("\\r", f);
        else if (c == '\t') fputs("\\t", f);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

static int real_main(int argc, char **argv)
{
    rn_spec sp;
    rn_result rr;
    const char *result_path = NULL;
    const char **envs = (const char **)calloc((size_t)argc + 1, sizeof(char *));
    int ne = 0, i;
    FILE *out;
    memset(&sp, 0, sizeof sp);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (strcmp(a, "--") == 0) { i++; break; }
        if (strcmp(a, "--single") == 0) { sp.single_process = 1; continue; }
        if (v == NULL) { fprintf(stderr, "lipsește valoarea pentru %s\n", a); return 2; }
        if (strcmp(a, "--cwd") == 0) sp.cwd = v;
        else if (strcmp(a, "--stdin") == 0) sp.stdin_path = v;
        else if (strcmp(a, "--stdout") == 0) sp.stdout_path = v;
        else if (strcmp(a, "--stderr") == 0) sp.stderr_path = v;
        else if (strcmp(a, "--wall") == 0) sp.wall_ms = atoll(v);
        else if (strcmp(a, "--cpu") == 0) sp.cpu_s = atoll(v);
        else if (strcmp(a, "--mem") == 0) sp.mem_kb = atoll(v);
        else if (strcmp(a, "--stack") == 0) sp.stack_kb = atoll(v);
        else if (strcmp(a, "--fsize") == 0) sp.fsize_kb = atoll(v);
        else if (strcmp(a, "--env") == 0) envs[ne++] = v;
        else if (strcmp(a, "--result") == 0) result_path = v;
        else { fprintf(stderr, "opțiune necunoscută: %s\n", a); return 2; }
        i++;
    }
    if (i >= argc) { fprintf(stderr, "utilizare: olieval-run [opțiuni] -- program [argumente]\n"); return 2; }
    envs[ne] = NULL;
    sp.env_add = envs;
    sp.cancel = &g_cancel;
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    sp.argv = (const char *const *)(argv + i);
    rn_run(&sp, &rr);
    out = result_path ? fopen(result_path, "w") : stdout;
    if (out == NULL) out = stdout;
    fprintf(out, "{\"started\":%s,\"error\":", rr.started ? "true" : "false");
    json_str(out, rr.error);
    fprintf(out, ",\"cancelled\":%s", rr.cancelled ? "true" : "false");
    fprintf(out, ",\"exited\":%s,\"exit\":%d,\"winStatus\":%u,\"signal\":%d,\"timedOut\":%s,"
                 "\"wallMs\":%.3f,\"cpuMs\":%.3f,\"peakKb\":%lld,\"peakCommitKb\":%lld,\"memLimitHit\":%s,\"text\":",
            rr.exited ? "true" : "false", rr.exit_code, rr.win_status, rr.signal, rr.timed_out ? "true" : "false",
            rr.wall_ms, rr.cpu_ms, rr.peak_kb, rr.peak_commit_kb, rr.mem_limit_hit ? "true" : "false");
    json_str(out, rn_status_text(&rr));
    fprintf(out, "}\n");
    if (out != stdout) fclose(out);
    free(envs);
    return 0;
}

#ifdef _WIN32
/* argumentele în UTF-8, indiferent de pagina de cod a consolei */
int main(void)
{
    int argc = 0, i, rc;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    char **argv = (char **)calloc((size_t)argc + 1, sizeof(char *));
    for (i = 0; i < argc; i++) argv[i] = rn_w_to_u8(wargv[i]);
    rc = real_main(argc, argv);
    for (i = 0; i < argc; i++) free(argv[i]);
    free(argv);
    LocalFree(wargv);
    return rc;
}
#else
int main(int argc, char **argv) { return real_main(argc, argv); }
#endif
