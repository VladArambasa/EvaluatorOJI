// OJI-STYLE CHECKER: READS MAX.OK AND MAX.OUT, PRINTS "SCORE MESSAGE"
// 5 POINTS FOR THE MAXIMUM, 5 FOR THE COUNT; DOUBLED ON THE BIG TEST
#include <fstream>
#include <iostream>
using namespace std;
int main()
{
    ifstream fok("max.ok"), fout("max.out"), fin("max.in");
    int n;
    fin >> n;
    int k = (n > 1000) ? 2 : 1;
    if (!fout) { cout << 0 << ' ' << "No output file!"; return 0; }
    long long a, b, x, y;
    fok >> a >> b;
    if (!(fout >> x)) { cout << 0 << ' ' << "Empty output"; return 0; }
    int p = 0;
    if (x == a) p += 5 * k;
    if ((fout >> y) && y == b) p += 5 * k;
    if (p == 10 * k) cout << p << ' ' << "Correct!";
    else if (p > 0) cout << p << ' ' << "Partially correct";
    else cout << 0 << ' ' << "Wrong!";
    return 0;
}
