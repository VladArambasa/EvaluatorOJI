#include <fstream>
#include <vector>
using namespace std;
/* TRIES TO ALLOCATE 2 GB: THE ALLOCATION FAILS (MEMORY LIMIT) */
int main()
{
    ifstream fin("sum.in");
    ofstream fout("sum.out");
    vector<long long> v(250000000LL, 0);
    int n;
    long long s = 0, x;
    fin >> n;
    for (int i = 0; i < n; i++) { fin >> x; s += x; v[i] = x; }
    fout << s << '\n';
}
