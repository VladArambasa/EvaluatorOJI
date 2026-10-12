#include <stdio.h>
int main(void)
{
    FILE *f = fopen("sum.in", "r"), *g = fopen("sum.out", "w");
    int n, i;
    long long s = 0, x;
    if (fscanf(f, "%d", &n) != 1) return 1;
    for (i = 0; i < n; i++) { if (fscanf(f, "%lld", &x) != 1) break; s += x; }
    fprintf(g, "%lld\n", s);
    fclose(f);
    fclose(g);
    return 0;
}
