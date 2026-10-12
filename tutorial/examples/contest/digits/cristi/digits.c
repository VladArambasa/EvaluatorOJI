#include "digits.h"
int digit_at(unsigned long long n, int k)
{
    while (k-- > 0) n /= 10;
    return (int)(n % 10);
}
