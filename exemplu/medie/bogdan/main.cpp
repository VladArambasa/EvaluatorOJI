#include <cstdio>
/* afișează mai multe zecimale -> corect la compararea cu toleranță */
int main()
{
    FILE *f = fopen("date/medie.in", "r"), *g = fopen("date/medie.out", "w");
    long long a, b;
    if (!f || !g) return 1;
    if (fscanf(f, "%lld %lld", &a, &b) != 2) return 1;
    fprintf(g, "%.6f\n", (a + b) / 2.0);
    return 0;
}
