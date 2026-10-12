#include <cstdio>
/* PRINTS MORE DECIMALS: CORRECT WITH A TOLERANCE COMPARISON */
int main()
{
    FILE *f = fopen("data/average.in", "r"), *g = fopen("data/average.out", "w");
    long long a, b;
    if (!f || !g) return 1;
    if (fscanf(f, "%lld %lld", &a, &b) != 2) return 1;
    fprintf(g, "%.6f\n", (a + b) / 2.0);
    return 0;
}
