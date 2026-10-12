#include <fstream>
using namespace std;
ifstream fin("sum.in");
ofstream fout("sum.out");
int main()
{
    int n;
    long long s = 0, x;
    fin >> n;
    for (int i = 0; i < n; i++) { fin >> x; s += x; }
    fout << s << '\n';
    return 0;
}
