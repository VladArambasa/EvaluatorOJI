#!/usr/bin/env python3
"""Generează spațiul de lucru exemplu (exemplu/) — un concurs cu 4 probleme (suma, maxim, medie, cifre)
și 16 elevi care acoperă toate situațiile: corect, greșit, timp depășit, memorie depășită, eroare la
execuție, eroare de compilare, stivă depășită, sursă prea mare, proiecte în subfoldere etc."""
import json
import os
import random
import shutil
import sys

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "exemplu"))


def w(rel, text, crlf=False):
    p = os.path.join(ROOT, *rel.split("/"))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    data = text.replace("\n", "\r\n") if crlf else text
    with open(p, "wb") as f:
        f.write(data.encode("utf-8"))


if os.path.isdir(ROOT):
    shutil.rmtree(ROOT)
os.makedirs(ROOT)
rnd = random.Random(2026)

# ------------------------------------------------------------------ problema 1: suma (fișiere)
tests = [(3, 1, 9), (10, 1, 100), (1000, 1, 10 ** 9), (100000, 1, 10 ** 9), (200000, 1, 10 ** 9), (1000, -10 ** 6, 10 ** 6)]
pts = [10, 10, 15, 20, 25, 20]
for i, (n, lo, hi) in enumerate(tests):
    vals = [rnd.randint(lo, hi) for _ in range(n)]
    w("suma/_teste/%d-suma.in" % i, "%d\n%s\n" % (n, " ".join(map(str, vals))))
    w("suma/_teste/%d-suma.ok" % i, "%d\n" % sum(vals))

w("suma/ana/main.cpp", """#include <fstream>
using namespace std;
ifstream fin("suma.in");
ofstream fout("suma.out");
int main()
{
    int n;
    long long s = 0, x;
    fin >> n;
    for (int i = 0; i < n; i++) { fin >> x; s += x; }
    fout << s << '\\n';
    return 0;
}
""")
w("suma/bogdan/suma_clion/CMakeLists.txt", """cmake_minimum_required(VERSION 3.20)
project(suma C)
set(CMAKE_C_STANDARD 11)
add_executable(suma main.c)
""")
w("suma/bogdan/suma_clion/main.c", """#include <stdio.h>
int main(void)
{
    FILE *f = fopen("suma.in", "r"), *g = fopen("suma.out", "w");
    int n, i;
    long long s = 0, x;
    if (fscanf(f, "%d", &n) != 1) return 1;
    for (i = 0; i < n; i++) { if (fscanf(f, "%lld", &x) != 1) break; s += x; }
    fprintf(g, "%lld\\n", s);
    fclose(f);
    fclose(g);
    return 0;
}
""")
w("suma/bogdan/suma_clion/cmake-build-debug/CMakeFiles/3.28.1/CompilerIdC/CMakeCCompilerId.c",
  "/* fișier generat de CMake — trebuie ignorat */\nint main(int argc, char* argv[]) { return 0; }\n")
w("suma/cristi/main.c", """#include <stdio.h>
/* folosește int pentru sumă -> depășire pe testele mari */
int main(void)
{
    FILE *f = fopen("suma.in", "r"), *g = fopen("suma.out", "w");
    int n, i, x, s = 0;
    if (fscanf(f, "%d", &n) != 1) return 1;
    for (i = 0; i < n; i++) { if (fscanf(f, "%d", &x) != 1) break; s += x; }
    fprintf(g, "%d\\n", s);
    return 0;
}
""")
w("suma/dana/main.cpp", """#include <fstream>
#include <vector>
using namespace std;
/* corect, dar pătratic -> timp depășit pe testele mari */
int main()
{
    ifstream fin("suma.in");
    ofstream fout("suma.out");
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
    fout << s << '\\n';
}
""")
w("suma/emil/main.c", """#include <stdio.h>
int main(void)
{
    FILE *f = fopen("suma.in", "r"), *g = fopen("suma.out", "w");
    int n, i;
    long long s = 0, x;
    if (fscanf(f, "%d", &n) != 1) return 1;
    for (i = 0; i < n; i++) { if (fscanf(f, "%lld", &x) != 1) break; s += x; }
    volatile int k = n - 10;
    volatile int q = 100 / k;   /* împărțire la zero când n = 10 */
    fprintf(g, "%lld\\n", s + (q - q));
    return 0;
}
""")
w("suma/flavia/main.cpp", """#include <fstream>
using namespace std;
int main()
{
    ifstream fin("suma.in")
    ofstream fout("suma.out");
    long long s = 0;
    fout << s;
}
""")
w("suma/george/main.cpp", """#include <fstream>
#include <vector>
using namespace std;
/* corect, dar folosește ~100 MB (limita este 64 MB) */
int main()
{
    ifstream fin("suma.in");
    ofstream fout("suma.out");
    vector<long long> big(13000000, 1);
    int n;
    long long s = 0, x;
    fin >> n;
    for (int i = 0; i < n; i++) { fin >> x; s += x; big[i % big.size()] = x; }
    fout << s + (long long)(big[12999999] - 1) << '\\n';
}
""")
w("suma/horia/main.c", """#include <stdio.h>
/* citește de la tastatură în loc de suma.in */
int main(void)
{
    int n = 0, i;
    long long s = 0, x;
    if (scanf("%d", &n) != 1) n = 0;
    for (i = 0; i < n; i++) { if (scanf("%lld", &x) != 1) break; s += x; }
    printf("%lld\\n", s);
    return 0;
}
""")
w("suma/ioana/main.c", """#include <stdio.h>
#include <math.h>
#include "util.h"
int main(void)
{
    FILE *f = fopen("suma.in", "r"), *g = fopen("suma.out", "w");
    int n;
    if (fscanf(f, "%d", &n) != 1) return 1;
    fprintf(g, "%lld\\n", citeste_si_aduna(f, n) + (long long)floor(sqrt(0.0)));
    return 0;
}
""")
w("suma/ioana/util.h", "#ifndef UTIL_H\n#define UTIL_H\n#include <stdio.h>\nlong long citeste_si_aduna(FILE *f, int n);\n#endif\n")
w("suma/ioana/util.c", """#include "util.h"
long long citeste_si_aduna(FILE *f, int n)
{
    long long s = 0, x;
    int i;
    for (i = 0; i < n; i++) { if (fscanf(f, "%lld", &x) != 1) break; s += x; }
    return s;
}
""")
w("suma/jianu/main.c", """#include <stdio.h>
/* recursivitate de adâncime n -> depășire de stivă pe testele mari */
FILE *f;
long long rec(int n)
{
    long long x;
    volatile char pad[256];
    pad[0] = 0;
    if (n == 0) return 0;
    if (fscanf(f, "%lld", &x) != 1) x = 0;
    return x + rec(n - 1) + pad[0];
}
int main(void)
{
    int n;
    FILE *g;
    f = fopen("suma.in", "r");
    g = fopen("suma.out", "w");
    if (fscanf(f, "%d", &n) != 1) return 1;
    fprintf(g, "%lld\\n", rec(n));
    return 0;
}
""")
w("suma/kira/main.py", """with open("suma.in") as f:
    date = f.read().split()
n = int(date[0])
with open("suma.out", "w") as g:
    g.write(str(sum(map(int, date[1:n + 1]))) + "\\n")
""")
table = ",".join(str(rnd.randint(0, 10 ** 9)) for _ in range(1500))
w("suma/lia/main.cpp", """#include <fstream>
using namespace std;
/* corect, dar sursa depășește 10 KB (tabel inutil) */
const long long T[] = {%s};
int main()
{
    ifstream fin("suma.in");
    ofstream fout("suma.out");
    int n;
    long long s = 0, x;
    fin >> n;
    for (int i = 0; i < n; i++) { fin >> x; s += x; }
    fout << s + T[0] * 0 << '\\n';
}
""" % table)
w("suma/mihai/main.cpp", """#include <cstdio>
/* scrie cu CRLF și spații la final de linie -> tot corect */
int main()
{
    FILE *f = fopen("suma.in", "r"), *g = fopen("suma.out", "wb");
    int n;
    long long s = 0, x;
    if (fscanf(f, "%d", &n) != 1) return 1;
    for (int i = 0; i < n; i++) { if (fscanf(f, "%lld", &x) != 1) break; s += x; }
    fprintf(g, "%lld   \\r\\n\\r\\n", s);
    return 0;
}
""")
os.makedirs(os.path.join(ROOT, "suma", "nicu"), exist_ok=True)
w("suma/nicu/citeste-ma.txt", "Elevul nu a trimis nicio sursă.\n")
w("suma/oana/Suma/Suma.cbp", "<?xml version=\"1.0\"?>\n<CodeBlocks_project_file><Project><Option title=\"Suma\" /></Project></CodeBlocks_project_file>\n")
w("suma/oana/Suma/main.cpp", """#include <iostream>
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
""")
w("suma/oana/Suma/obj/Debug/main.o", "obiect vechi\n")
w("suma/petra/main.cpp", """#include <fstream>
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
    fout << s << '\\n';
}
""")

# ------------------------------------------------------------------ problema 2: maxim (consolă + verificator OJI)
mtests = [[5, 1, 5, 3, 5], [7, 7, 7], [-3, -1, -2, -1], list(rnd.randint(-10 ** 9, 10 ** 9) for _ in range(50000))]
for i, v in enumerate(mtests, start=1):
    w("maxim/_teste/%d-maxim.in" % i, "%d\n%s\n" % (len(v), " ".join(map(str, v))))
    m = max(v)
    w("maxim/_teste/%d-maxim.ok" % i, "%d %d\n" % (m, v.count(m)))
w("maxim/_teste/teste.txt", "1 10\n2 10\n3 10\n4 20\n")
w("maxim/_teste/verif.cpp", """// Verificator în stilul OJI: citește maxim.ok și maxim.out,
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
""")
w("maxim/ana/maxim.cpp", """#include <iostream>
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
    cout << m << ' ' << c << '\\n';
}
""")
w("maxim/bogdan/maxim.c", """#include <stdio.h>
/* maximul e corect, dar numără greșit aparițiile */
int main(void)
{
    int n, i;
    long long x, m = 0;
    if (scanf("%d", &n) != 1) return 0;
    for (i = 0; i < n; i++) { if (scanf("%lld", &x) != 1) break; if (i == 0 || x > m) m = x; }
    printf("%lld %d\\n", m, 1);
    return 0;
}
""")
w("maxim/cristi/maxim.cpp", """#include <iostream>
using namespace std;
/* pornește maximul de la 0 -> greșit pentru numere negative */
int main()
{
    int n; long long x, m = 0; int c = 0;
    cin >> n;
    for (int i = 0; i < n; i++) { cin >> x; if (x > m) { m = x; c = 1; } else if (x == m) c++; }
    cout << m << ' ' << c << '\\n';
}
""")
w("maxim/dana/maxim.py", """import sys
d = sys.stdin.read().split()
n = int(d[0]); v = list(map(int, d[1:n + 1])); m = max(v)
print(m, v.count(m))
""")

# ------------------------------------------------------------------ problema 3: medie (folder relativ la proiect)
for i, (a, b) in enumerate([(1, 2), (10, 20), (7, 8), (1000000, 3)], start=1):
    w("medie/_teste/%d-medie.in" % i, "%d %d\n" % (a, b))
    w("medie/_teste/%d-medie.ok" % i, "%.2f\n" % ((a + b) / 2))
w("medie/ana/medie/main.c", """#include <stdio.h>
/* citește din date/medie.in și scrie în date/medie.out (relativ la rădăcina proiectului) */
int main(void)
{
    FILE *f = fopen("date/medie.in", "r"), *g = fopen("date/medie.out", "w");
    long long a, b;
    if (!f || !g) return 1;
    if (fscanf(f, "%lld %lld", &a, &b) != 2) return 1;
    fprintf(g, "%.2f\\n", (a + b) / 2.0);
    return 0;
}
""")
w("medie/ana/medie/date/citeste-ma.txt", "Aici pune evaluatorul medie.in\n")
w("medie/bogdan/main.cpp", """#include <cstdio>
/* afișează mai multe zecimale -> corect la compararea cu toleranță */
int main()
{
    FILE *f = fopen("date/medie.in", "r"), *g = fopen("date/medie.out", "w");
    long long a, b;
    if (!f || !g) return 1;
    if (fscanf(f, "%lld %lld", &a, &b) != 2) return 1;
    fprintf(g, "%.6f\\n", (a + b) / 2.0);
    return 0;
}
""")
w("medie/cristi/main.c", """#include <stdio.h>
/* împărțire întreagă -> greșit când suma e impară */
int main(void)
{
    FILE *f = fopen("date/medie.in", "r"), *g = fopen("date/medie.out", "w");
    long long a, b;
    if (!f || !g) return 1;
    if (fscanf(f, "%lld %lld", &a, &b) != 2) return 1;
    fprintf(g, "%lld\\n", (a + b) / 2);
    return 0;
}
""")


# ------------------------------------------------------------------ problema 4: cifre (proiecte ca la curs: compile.sh + build.sh)
SCRIPTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "course-scripts")


def copy_scripts(kind, dest_rel):
    src = os.path.join(SCRIPTS, kind)
    for name in sorted(os.listdir(src)):
        with open(os.path.join(src, name), "rb") as f:
            data = f.read().decode("utf-8")
        w(dest_rel + "/" + name, data)
        os.chmod(os.path.join(ROOT, *(dest_rel + "/" + name).split("/")), 0o755)


for i, n in enumerate([0, 7, 12345, 999999999999999999, 1000000000000000000], start=1):
    w("cifre/_teste/%d-cifre.in" % i, "%d\n" % n)
    w("cifre/_teste/%d-cifre.ok" % i, "%d %d\n" % (sum(map(int, str(n))), len(str(n))))
# ana: structura nouă (bash-scripts/), mai multe fișiere, folosește math.h (build.sh adaugă -lm)
copy_scripts("bash-scripts", "cifre/ana/bash-scripts")
w("cifre/ana/master.h", "#ifndef MASTER_H\n#define MASTER_H\n#include <stdio.h>\n#include <math.h>\n#endif\n")
w("cifre/ana/cifre.h", "#ifndef CIFRE_H\n#define CIFRE_H\nint suma_cifre(unsigned long long n, int *nr);\n#endif\n")
w("cifre/ana/cifre.c", """#include "cifre.h"
int suma_cifre(unsigned long long n, int *nr)
{
    int s = 0;
    *nr = 0;
    do { s += (int)(n % 10); n /= 10; (*nr)++; } while (n > 0);
    return s;
}
""")
w("cifre/ana/main.c", """#include "master.h"
#include "cifre.h"
int main(void)
{
    unsigned long long n;
    int nr, s;
    if (scanf("%llu", &n) != 1) return 1;
    s = suma_cifre(n, &nr);
    volatile double radical = sqrt((double)s); /* are nevoie de -lm */
    (void)radical;
    printf("%d %d\\n", s, nr);
    return 0;
}
""")
w("cifre/ana/build/citeste-ma.txt", "Aici compile.sh pune fisierele .o\n")
w("cifre/ana/release/citeste-ma.txt", "Aici build.sh pune executabilul program\n")
# bogdan: structura veche (compile.sh și build.sh în rădăcină)
copy_scripts("vechi", "cifre/bogdan")
w("cifre/bogdan/master.h", "#ifndef MASTER_H\n#define MASTER_H\n#include <stdio.h>\n#endif\n")
w("cifre/bogdan/main.c", """#include "master.h"
int main(void)
{
    unsigned long long n;
    int s = 0, nr = 0;
    if (scanf("%llu", &n) != 1) return 1;
    do { s += (int)(n % 10); n /= 10; nr++; } while (n);
    printf("%d %d\\n", s, nr);
    return 0;
}
""")
# cristi: proiect mixt C + C++ (gcc pentru .c, g++ pentru .cpp, legare cu g++)
copy_scripts("bash-scripts", "cifre/cristi/bash-scripts")
w("cifre/cristi/cifre.h", "#ifndef CIFRE_H\n#define CIFRE_H\n#ifdef __cplusplus\nextern \"C\" {\n#endif\nint cifra(unsigned long long n, int k);\n#ifdef __cplusplus\n}\n#endif\n#endif\n")
w("cifre/cristi/cifre.c", """#include "cifre.h"
int cifra(unsigned long long n, int k)
{
    while (k-- > 0) n /= 10;
    return (int)(n % 10);
}
""")
w("cifre/cristi/main.cpp", """#include <iostream>
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
    cout << s << ' ' << nr << '\\n';
}
""")
# dana: eroare de compilare într-un fișier secundar
copy_scripts("bash-scripts", "cifre/dana/bash-scripts")
w("cifre/dana/util.c", "int dublu(int x)\n{\n    return 2 * x\n}\n")
w("cifre/dana/main.c", """#include <stdio.h>
int dublu(int x);
int main(void)
{
    unsigned long long n;
    int s = 0, nr = 0;
    if (scanf("%llu", &n) != 1) return 1;
    do { s += (int)(n % 10); n /= 10; nr++; } while (n);
    printf("%d %d\\n", dublu(s) / 2, nr);
    return 0;
}
""")

def test_list(ids, points, pattern_in, pattern_ok):
    return [{"id": str(i), "input": pattern_in % i, "ok": pattern_ok % i, "points": p,
             "timePoints": 0, "memPoints": 0, "timeMs": 0, "memMb": 0} for i, p in zip(ids, points)]


base_project = {
    "rootRules": [{"pattern": "CMakeLists.txt", "up": 0}, {"pattern": "*.cbp", "up": 0},
                  {"pattern": "bash-scripts/compile.sh", "up": 0}, {"pattern": "bash-scripts/build.sh", "up": 0},
                  {"pattern": "compile.sh", "up": 0}, {"pattern": "build.sh", "up": 0},
                  {"pattern": "main.cpp", "up": 0}, {"pattern": "main.c", "up": 0},
                  {"pattern": "*.cpp", "up": 0}, {"pattern": "*.c", "up": 0}, {"pattern": "*.py", "up": 0}],
    "maxDepth": 6, "mainPatterns": "main.cpp main.c {problem}.cpp {problem}.c *.cpp *.c *.py",
    "compileScope": "main", "sizeScope": "main", "sizeExt": ".c .h .cpp .hpp .cc .cxx .hh .py",
    "buildMode": "command", "buildCommand": "auto", "buildExe": "release/program"}


def problem(name, nr, tests, io, compare, limits=None, policy=None, project=None, points=None):
    p = {"name": name, "nr": nr, "title": "", "testsDir": "", "inputPattern": "{n}-{problem}.in",
         "okPattern": "{n}-{problem}.ok", "tests": tests, "io": io,
         "project": dict(base_project, **(project or {})), "compileExtra": "",
         "limits": dict({"timeMs": 1000, "timeMode": "cpu", "hardTimeFactor": 2, "memMb": 64, "hardMemFactor": 2,
                         "stackMb": 8, "sourceKb": 0, "outputMb": 64, "calibrate": True}, **(limits or {})),
         "policy": dict({"time": {"mode": "zero", "percent": 50}, "memory": {"mode": "zero", "percent": 50},
                         "size": {"mode": "zero", "percent": 50, "penalty": 10}}, **(policy or {})),
         "points": dict({"compile": 0, "size": 0}, **(points or {})),
         "compare": dict({"mode": "lines", "ignoreCase": False, "epsilon": 0.000001, "checker": "",
                          "protocol": "oji", "args": ""}, **compare)}
    return p


ws = {
    "format": "evaluator-olimpiada", "version": 1, "name": "Exemplu — concurs de antrenament",
    "layout": "problem-student", "studentDir": "{root}/{problem}/{student}", "flatDir": "{root}/{problem}/surse",
    "flatPattern": "{student}{nr}.{ext}", "testsDir": "{root}/{problem}/_teste", "workDir": "{root}/_lucru",
    "resultsDir": "{root}/_rezultate",
    "students": [{"id": s, "name": n, "group": "7A"} for s, n in [
        ("ana", "Ana Popescu"), ("bogdan", "Bogdan Ionescu"), ("cristi", "Cristian Pop"), ("dana", "Dana Marin"),
        ("emil", "Emil Stan"), ("flavia", "Flavia Dobre"), ("george", "George Radu"), ("horia", "Horia Matei"),
        ("ioana", "Ioana Lazăr"), ("jianu", "Jianu Ene"), ("kira", "Kira Neagu"), ("lia", "Lia Ștefan"),
        ("mihai", "Mihai Toma"), ("nicu", "Nicu Vlad"), ("oana", "Oana Țurcanu"), ("petra", "Petra Iliescu")]],
    "problems": [
        problem("suma", 1, test_list(range(6), pts, "%d-suma.in", "%d-suma.ok"),
                {"input": "file", "output": "file", "inputPath": "{problem}.in", "outputPath": "{problem}.out",
                 "cwd": "", "runIn": "sandbox"},
                {"mode": "lines"}, limits={"sourceKb": 10}, project={"compileScope": "all"},
                policy={"size": {"mode": "percent", "percent": 50, "penalty": 10}}),
        problem("maxim", 2, test_list(range(1, 5), [10, 10, 10, 20], "%d-maxim.in", "%d-maxim.ok"),
                {"input": "stdin", "output": "stdout", "inputPath": "{problem}.in", "outputPath": "{problem}.out",
                 "cwd": "", "runIn": "sandbox"},
                {"mode": "checker", "checker": "verif.cpp", "protocol": "oji"}),
        problem("medie", 3, test_list(range(1, 5), [25, 25, 25, 25], "%d-medie.in", "%d-medie.ok"),
                {"input": "file", "output": "file", "inputPath": "date/{problem}.in",
                 "outputPath": "date/{problem}.out", "cwd": "", "runIn": "project"},
                {"mode": "real", "epsilon": 0.001}),
        problem("cifre", 4, test_list(range(1, 6), [20, 20, 20, 20, 20], "%d-cifre.in", "%d-cifre.ok"),
                {"input": "stdin", "output": "stdout", "inputPath": "{problem}.in", "outputPath": "{problem}.out",
                 "cwd": "", "runIn": "sandbox"},
                {"mode": "tokens"}, project={"buildMode": "objects"}),
    ],
    "current": "suma",
    "autoLibs": {"enabled": True, "rules": [
        {"name": "Math", "headers": "math.h complex.h tgmath.h fenv.h", "flags": "-lm"},
        {"name": "Threads", "headers": "pthread.h threads.h", "flags": "-pthread"},
        {"name": "OpenGL", "headers": "GL/*", "flags": "-lGL -lGLU -lglut"},
        {"name": "wxWidgets", "headers": "wx/*", "flags": "$(wx-config --cxxflags --libs)"}]},
    "ignoreDirs": ".* _* cmake-build-* CMakeFiles build bin obj out release debug __MACOSX node_modules venv __pycache__",
    "options": {"keepOutputs": True, "windowsStackFlag": True, "compileTimeoutSec": 60,
                "nonzeroExitIsError": True, "useProbe": True},
}
with open(os.path.join(ROOT, "evaluator.json"), "w", encoding="utf-8") as f:
    json.dump(ws, f, ensure_ascii=False, indent=2)
print("Exemplu generat în", ROOT)
