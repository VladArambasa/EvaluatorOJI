#include <stdio.h>
/* INTEGER DIVISION: WRONG WHEN THE SUM IS ODD */
int main(void)
{
    FILE *f = fopen("data/average.in", "r"), *g = fopen("data/average.out", "w");
    long long a, b;
    if (!f || !g) return 1;
    if (fscanf(f, "%lld %lld", &a, &b) != 2) return 1;
    fprintf(g, "%lld\n", (a + b) / 2);
    return 0;
}
