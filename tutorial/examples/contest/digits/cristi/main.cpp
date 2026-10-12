#include <iostream>
#include "digits.h"
using namespace std;
/* WRONG FOR N = 0 (PRINTS 0 DIGITS) */
int main()
{
    unsigned long long n, m;
    cin >> n;
    int count = 0, s = 0;
    for (m = n; m > 0; m /= 10) count++;
    for (int k = 0; k < count; k++) s += digit_at(n, k);
    cout << s << ' ' << count << '\n';
}
