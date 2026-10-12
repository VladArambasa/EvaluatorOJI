// Verificator în stilul OJI: citește maxim.ok și maxim.out,
// afișează "punctaj mesaj" (5 puncte pentru maxim, 5 pentru numărul de apariții, x2 la testul mare).
#include <fstream>
#include <iostream>
using namespace std;
int main()
{
    ifstream fok("maxim.ok"), fout("maxim.out"), fin("maxim.in");
    int n;
    fin >> n;
    int k = (n > 1000) ? 2 : 1;
    if (!fout) { cout << 0 << ' ' << "Nu exista fisier de iesire!"; return 0; }
    long long a, b, x, y;
    fok >> a >> b;
    if (!(fout >> x)) { cout << 0 << ' ' << "Iesire goala"; return 0; }
    int p = 0;
    if (x == a) p += 5 * k;
    if ((fout >> y) && y == b) p += 5 * k;
    if (p == 10 * k) cout << p << ' ' << "Corect!";
    else if (p > 0) cout << p << ' ' << "Partial";
    else cout << 0 << ' ' << "Gresit!";
    return 0;
}
