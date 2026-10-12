#include <fstream>
#include <vector>
using namespace std;
/* CORRECT BUT QUADRATIC: TIME LIMIT ON THE BIG TESTS */
int main()
{
    ifstream fin("sum.in");
    ofstream fout("sum.out");
    int n;
    fin >> n;
    vector<long long> v(n);
    for (auto &x : v) fin >> x;
    long long s = 0;
    for (int i = 0; i < n; i++) {
        long long t = 0;
        for (int j = 0; j <= i; j++) t += (j == i) ? v[j] : 0;
        s += t;
    }
    fout << s << '\n';
}
