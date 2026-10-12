#include "master.h"
#include "cifre.h"
int main(void)
{
    unsigned long long n;
    int nr, s;
    if (scanf("%llu", &n) != 1) return 1;
    s = suma_cifre(n, &nr);
    volatile double radical = sqrt((double)s); /* are nevoie de -lm */
    (void)radical;
    printf("%d %d\n", s, nr);
    return 0;
}
