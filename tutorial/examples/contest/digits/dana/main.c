#include <stdio.h>
int twice(int x);
int main(void)
{
    unsigned long long n;
    int s = 0, count = 0;
    if (scanf("%llu", &n) != 1) return 1;
    do { s += (int)(n % 10); n /= 10; count++; } while (n);
    printf("%d %d\n", twice(s) / 2, count);
    return 0;
}
