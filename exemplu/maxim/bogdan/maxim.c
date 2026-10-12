#include <stdio.h>
/* maximul e corect, dar numără greșit aparițiile */
int main(void)
{
    int n, i;
    long long x, m = 0;
    if (scanf("%d", &n) != 1) return 0;
    for (i = 0; i < n; i++) { if (scanf("%lld", &x) != 1) break; if (i == 0 || x > m) m = x; }
    printf("%lld %d\n", m, 1);
    return 0;
}
