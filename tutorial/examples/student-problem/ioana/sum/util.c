#include "util.h"
long long read_and_add(FILE *f, int n)
{
    long long s = 0, x;
    int i;
    for (i = 0; i < n; i++) { if (fscanf(f, "%lld", &x) != 1) break; s += x; }
    return s;
}
