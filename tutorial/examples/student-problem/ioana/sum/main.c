#include <stdio.h>
#include <math.h>
#include "util.h"
int main(void)
{
    FILE *f = fopen("sum.in", "r"), *g = fopen("sum.out", "w");
    int n;
    if (fscanf(f, "%d", &n) != 1) return 1;
    fprintf(g, "%lld\n", read_and_add(f, n) + (long long)floor(sqrt(0.0)));
    return 0;
}
