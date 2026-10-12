#include <fstream>
#include <vector>
using namespace std;
/* încearcă să aloce 2 GB -> alocarea eșuează (memorie depășită) */
int main()
{
    ifstream fin("suma.in");
    ofstream fout("suma.out");
    vector<long long> v(250000000LL, 0);
    int n;
    long long s = 0, x;
    fin >> n;
    for (int i = 0; i < n; i++) { fin >> x; s += x; v[i] = x; }
    fout << s << '\n';
}
