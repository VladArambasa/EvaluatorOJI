#include "cifre.h"
int cifra(unsigned long long n, int k)
{
    while (k-- > 0) n /= 10;
    return (int)(n % 10);
}
