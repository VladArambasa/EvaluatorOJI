#include <stdio.h>
#include <math.h>
#include "util.h"
int main(void)
{
    FILE *f = fopen("suma.in", "r"), *g = fopen("suma.out", "w");
    int n;
    if (fscanf(f, "%d", &n) != 1) return 1;
    fprintf(g, "%lld\n", citeste_si_aduna(f, n) + (long long)floor(sqrt(0.0)));
    return 0;
}
