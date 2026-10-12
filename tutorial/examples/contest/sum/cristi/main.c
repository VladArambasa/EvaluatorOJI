#include <stdio.h>
/* INT SUM: OVERFLOWS ON THE BIG TESTS */
int main(void)
{
    FILE *f = fopen("sum.in", "r"), *g = fopen("sum.out", "w");
    int n, i, x, s = 0;
    if (fscanf(f, "%d", &n) != 1) return 1;
    for (i = 0; i < n; i++) { if (fscanf(f, "%d", &x) != 1) break; s += x; }
    fprintf(g, "%d\n", s);
    return 0;
}
