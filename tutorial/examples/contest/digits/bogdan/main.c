#include "master.h"
int main(void)
{
    unsigned long long n;
    int s = 0, count = 0;
    if (scanf("%llu", &n) != 1) return 1;
    do { s += (int)(n % 10); n /= 10; count++; } while (n);
    printf("%d %d\n", s, count);
    return 0;
}
