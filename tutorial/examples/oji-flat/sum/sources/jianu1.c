#include <stdio.h>
/* RECURSION OF DEPTH N: STACK OVERFLOW ON THE BIG TESTS */
FILE *f;
long long rec(int n)
{
    long long x;
    volatile char pad[256];
    pad[0] = 0;
    if (n == 0) return 0;
    if (fscanf(f, "%lld", &x) != 1) x = 0;
    return x + rec(n - 1) + pad[0];
}
int main(void)
{
    int n;
    FILE *g;
    f = fopen("sum.in", "r");
    g = fopen("sum.out", "w");
    if (fscanf(f, "%d", &n) != 1) return 1;
    fprintf(g, "%lld\n", rec(n));
    return 0;
}
