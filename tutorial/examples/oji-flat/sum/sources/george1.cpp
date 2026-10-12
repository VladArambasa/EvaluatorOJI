#include <fstream>
#include <vector>
using namespace std;
/* CORRECT BUT USES ABOUT 100 MB (THE LIMIT IS 64 MB) */
int main()
{
    ifstream fin("sum.in");
    ofstream fout("sum.out");
    vector<long long> big(13000000, 1);
    int n;
    long long s = 0, x;
    fin >> n;
    for (int i = 0; i < n; i++) { fin >> x; s += x; big[i % big.size()] = x; }
    fout << s + (long long)(big[12999999] - 1) << '\n';
}
