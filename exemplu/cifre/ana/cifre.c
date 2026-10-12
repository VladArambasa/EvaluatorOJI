#include "cifre.h"
int suma_cifre(unsigned long long n, int *nr)
{
    int s = 0;
    *nr = 0;
    do { s += (int)(n % 10); n /= 10; (*nr)++; } while (n > 0);
    return s;
}
