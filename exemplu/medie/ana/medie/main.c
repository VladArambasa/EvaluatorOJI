#include <stdio.h>
/* citește din date/medie.in și scrie în date/medie.out (relativ la rădăcina proiectului) */
int main(void)
{
    FILE *f = fopen("date/medie.in", "r"), *g = fopen("date/medie.out", "w");
    long long a, b;
    if (!f || !g) return 1;
    if (fscanf(f, "%lld %lld", &a, &b) != 2) return 1;
    fprintf(g, "%.2f\n", (a + b) / 2.0);
    return 0;
}
