/*
 * olieval_probe.c — sonda de măsurare a Evaluatorului Olimpiadă.
 *
 * Evaluatorul compilează acest fișier împreună cu sursa elevului (nu modifică sursa
 * elevului și nu intră în calculul dimensiunii). La terminarea NORMALĂ a programului
 * (return din main sau exit), sonda scrie în fișierul indicat de variabila de mediu
 * OLIEVAL_PROBE:
 *     rss_kb    memoria RAM maximă folosită (KB)
 *     stack_kb  dimensiunea maximă a stivei (KB)
 *     cpu_ms    timpul procesor (ms)
 *     vm_kb / commit_kb  (informativ)
 * Dacă variabila nu există (programul rulat în afara evaluatorului), sonda nu face nimic.
 * Fișierul se compilează curat atât ca C (C89+) cât și ca C++ (g++ tratează .c ca C++).
 */
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef PSAPI_VERSION
#    define PSAPI_VERSION 2
#  endif
#  include <windows.h>
#  include <psapi.h>
#else
#  include <sys/time.h>
#  include <sys/resource.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void olieval_probe_dump(void)
{
    const char *path = getenv("OLIEVAL_PROBE");
    FILE *f;
    if (path == NULL || path[0] == '\0') return;
    f = fopen(path, "w");
    if (f == NULL) return;
#if defined(_WIN32)
    {
        NT_TIB *tib;
        unsigned long long stk;
        PROCESS_MEMORY_COUNTERS pmc;
        FILETIME tc, te, tk, tu;
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif
        tib = (NT_TIB *)NtCurrentTeb();
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        /* stiva comisă = StackBase - StackLimit (nu scade niciodată) */
        stk = (unsigned long long)((char *)tib->StackBase - (char *)tib->StackLimit);
        memset(&pmc, 0, sizeof pmc);
        pmc.cb = sizeof pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) {
            fprintf(f, "rss_kb %llu\n", (unsigned long long)(pmc.PeakWorkingSetSize / 1024));
            fprintf(f, "commit_kb %llu\n", (unsigned long long)(pmc.PeakPagefileUsage / 1024));
        }
        fprintf(f, "stack_kb %llu\n", stk / 1024ULL);
        if (GetProcessTimes(GetCurrentProcess(), &tc, &te, &tk, &tu)) {
            unsigned long long k = ((unsigned long long)tk.dwHighDateTime << 32) | tk.dwLowDateTime;
            unsigned long long u = ((unsigned long long)tu.dwHighDateTime << 32) | tu.dwLowDateTime;
            fprintf(f, "cpu_ms %.3f\n", (double)(k + u) / 10000.0);
        }
    }
#else
    {
        struct rusage ru;
        int have_hwm = 0;
#if defined(__linux__)
        FILE *s = fopen("/proc/self/status", "r");
        if (s != NULL) {
            char line[256];
            while (fgets(line, sizeof line, s) != NULL) {
                if (strncmp(line, "VmHWM:", 6) == 0) {
                    fprintf(f, "rss_kb %ld\n", strtol(line + 6, NULL, 10));
                    have_hwm = 1;
                } else if (strncmp(line, "VmStk:", 6) == 0) {
                    fprintf(f, "stack_kb %ld\n", strtol(line + 6, NULL, 10));
                } else if (strncmp(line, "VmPeak:", 7) == 0) {
                    fprintf(f, "vm_kb %ld\n", strtol(line + 7, NULL, 10));
                }
            }
            fclose(s);
        }
#endif
        if (getrusage(RUSAGE_SELF, &ru) == 0) {
            double ms = (double)(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000.0
                      + (double)(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000.0;
            fprintf(f, "cpu_ms %.3f\n", ms);
            if (!have_hwm) {
#if defined(__APPLE__)
                fprintf(f, "rss_kb %ld\n", (long)(ru.ru_maxrss / 1024)); /* macOS: octeți */
#else
                fprintf(f, "rss_kb %ld\n", (long)ru.ru_maxrss);
#endif
            }
        }
    }
#endif
    fclose(f);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((destructor)) static void olieval_probe_fini(void)
{
    olieval_probe_dump();
}
#endif
