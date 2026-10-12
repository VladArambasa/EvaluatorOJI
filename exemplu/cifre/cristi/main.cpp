#include <iostream>
#include "cifre.h"
using namespace std;
/* greșește pentru n = 0 (afișează 0 cifre) */
int main()
{
    unsigned long long n, m;
    cin >> n;
    int nr = 0, s = 0;
    for (m = n; m > 0; m /= 10) nr++;
    for (int k = 0; k < nr; k++) s += cifra(n, k);
    cout << s << ' ' << nr << '\n';
}
