#include "digits.h"
int digit_sum(unsigned long long n, int *count)
{
    int s = 0;
    *count = 0;
    do { s += (int)(n % 10); n /= 10; (*count)++; } while (n > 0);
    return s;
}
