#include <iostream>
using namespace std;
int main()
{
    int n;
    long long x, m = 0;
    int c = 0;
    cin >> n;
    for (int i = 0; i < n; i++) {
        cin >> x;
        if (i == 0 || x > m) { m = x; c = 1; }
        else if (x == m) c++;
    }
    cout << m << ' ' << c << '\n';
}
