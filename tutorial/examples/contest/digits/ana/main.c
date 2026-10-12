#include "master.h"
#include "digits.h"
int main(void)
{
    unsigned long long n;
    int count, s;
    if (scanf("%llu", &n) != 1) return 1;
    s = digit_sum(n, &count);
    volatile double root = sqrt((double)s); /* NEEDS -LM */
    (void)root;
    printf("%d %d\n", s, count);
    return 0;
}
