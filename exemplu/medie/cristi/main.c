#include <stdio.h>
/* împărțire întreagă -> greșit când suma e impară */
int main(void)
{
    FILE *f = fopen("date/medie.in", "r"), *g = fopen("date/medie.out", "w");
    long long a, b;
    if (!f || !g) return 1;
    if (fscanf(f, "%lld %lld", &a, &b) != 2) return 1;
    fprintf(g, "%lld\n", (a + b) / 2);
    return 0;
}
