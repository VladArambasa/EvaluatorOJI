#include "master.h"
int main(void)
{
    unsigned long long n;
    int s = 0, nr = 0;
    if (scanf("%llu", &n) != 1) return 1;
    do { s += (int)(n % 10); n /= 10; nr++; } while (n);
    printf("%d %d\n", s, nr);
    return 0;
}
