#include <stdio.h>
/* READS FROM THE KEYBOARD INSTEAD OF SUM.IN */
int main(void)
{
    int n = 0, i;
    long long s = 0, x;
    if (scanf("%d", &n) != 1) n = 0;
    for (i = 0; i < n; i++) { if (scanf("%lld", &x) != 1) break; s += x; }
    printf("%lld\n", s);
    return 0;
}
