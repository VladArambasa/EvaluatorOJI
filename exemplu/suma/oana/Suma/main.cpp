#include <iostream>
#include <fstream>
using namespace std;
int main()
{
    ifstream cin("suma.in");
    ofstream cout("suma.out");
    int n;
    long long s = 0, x;
    cin >> n;
    while (n--) { cin >> x; s += x; }
    cout << s << endl;
    return 0;
}
