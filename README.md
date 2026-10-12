# Evaluator Olimpiadă

Evaluator pentru probleme de olimpiadă, în **patru variante cu aceeași interfață**: **C**, **C++**,
**Python** și **Web** (aplicație offline). Compilează și rulează sursele elevilor pe testele fiecărei
probleme, măsoară timpul, memoria (RAM și stivă) și dimensiunea sursei, compară ieșirea și calculează
punctajul după baremul tău. Totul rulează local: aplicația pornește un mic server pe `127.0.0.1` și
deschide interfața într-o fereastră de browser (Chrome/Edge, fără bară de adrese).

Interfața are aspectul programelor din anii '90–2009, cu două variante, schimbate din butonul cu
monitor din bara de titlu:

- **XP (2009)**, implicit, ca Evaluatorul OJI: fereastră bej, butoane XP, „Evaluează” cu roșu;
- **Clasic (Windows 95/98)**: gri, butoane în relief.

## Pornire rapidă

| Variantă | Windows | Linux / macOS | Ai nevoie de |
|----------|---------|---------------|--------------|
| **C**    | `c\bin\evaluator-c.exe` (sau `c\porneste.bat`) | `c/porneste.sh` (sau `c/bin/evaluator-c`) | nimic în plus |
| **C++**  | `cpp\bin\evaluator-cpp.exe` (sau `cpp\porneste.bat`) | `cpp/porneste.sh` (sau `cpp/bin/evaluator-cpp`) | nimic în plus |
| **Python** | `python\porneste.bat` | `python/porneste.sh` | Python 3.7 sau mai nou (doar biblioteca standard) |
| **Web**  | `web\porneste.bat` | `web/porneste.sh` (sau `npm start` în `web/`) | Node.js 16 sau mai nou (fără pachete npm) |

Executabilele din `c/bin` și `cpp/bin` sunt gata compilate pentru **Windows x64** și **Linux x64**
(legate static, merg pe orice distribuție). Pe macOS (sau dacă vrei să le recompilezi) vezi
[Compilarea din surse](#compilarea-din-surse).

Pentru a evalua surse C/C++ trebuie să existe `gcc`/`g++` pe calculator. Pe Windows, evaluatorul îl
caută în `PATH` și în locurile obișnuite: MinGW din Code::Blocks (inclusiv `C:\OJI\Codeblocks\MinGW`),
`C:\MinGW`, `C:\mingw64`, MSYS2, TDM-GCC, Dev-C++. Pentru surse Python, `python`/`python3`.

Opțiuni în linia de comandă (la toate variantele):

```
--port 8765      portul (implicit 8765; dacă e ocupat, se ia următorul liber)
--no-browser     nu deschide fereastra; deschide manual adresa afișată în consolă
--gui fișier     (C, C++) folosește alt index.html decât cel încorporat
--no-helper      (Web) rulează fără utilitarul olieval-run (mod de rezervă, vezi mai jos)
```

Închide aplicația din butonul roșu din bara de titlu a interfeței sau închizând fereastra consolei.

### Exemplul

La prima pornire alege **Deschide exemplul**: un concurs cu 4 probleme și 16 elevi care acoperă toate
situațiile (corect, greșit, parțial, timp depășit, memorie depășită, eroare la execuție, stivă depășită,
fără fișier de ieșire, eroare de compilare, sursă prea mare, proiecte CLion/Code::Blocks în subfoldere,
proiecte cu mai multe fișiere, scripturile `compile.sh`/`build.sh` ale cursului). Apasă
**Evaluează problema** și deschide orice celulă din tabel pentru detalii.

| problemă | ce arată |
|----------|----------|
| `suma`   | fișiere `suma.in`/`suma.out`, toate tipurile de verdict, limită de dimensiune a sursei cu penalizare |
| `maxim`  | consolă (stdin/stdout), verificator extern în stil OJI (`verif.cpp`), punctaj parțial, `teste.txt` |
| `medie`  | fișiere într-un folder al proiectului (`date/medie.in`), comparare de numere reale cu toleranță |
| `cifre`  | proiecte cu mai multe fișiere compilate „ca scripturile cursului” (obiecte în `build/`, executabil în `release/program`) |

Exemplul se poate regenera oricând: `python3 tools/make_example.py` (scrie în `exemplu/`).

## Cum lucrezi

1. **Spațiu de lucru** — alege folderul concursului. Aici se salvează `evaluator.json` (toată
   configurația și baremul). Alegi organizarea folderelor și apeși **Creează folderele** ca să
   pregătești structura pentru toți elevii din listă (lista se poate lipi din text sau lua din foldere).
2. **Problemă** — numele (apare în numele fișierelor: `suma.in`), cum primește programul datele,
   cum se compilează proiectul și cum se găsește proiectul elevului în folderul lui.
3. **Teste și barem** — pune testele în folderul de teste al problemei (ex. `0-suma.in` + `0-suma.ok`)
   și apasă **Detectează testele**. Fiecare test are puncte de corectitudine, puncte pentru timp și
   puncte pentru memorie, plus limite proprii (opțional). Setezi limitele (timp, memorie, stivă,
   dimensiunea sursei), ce se întâmplă la depășire și modul de comparare. **Importă/Exportă barem**.
4. **Evaluare** — **Evaluează problema**, **Evaluează selecția** (elevii bifați) sau **Toate
   problemele**. Tabelul arată, pentru fiecare elev și test, punctajul și verdictul; un clic pe celulă
   deschide intrarea, ieșirea corectă și ieșirea programului (cu linia diferită marcată), timpul,
   memoria, stiva, codul de ieșire și mesajele de eroare.
5. **Clasament și rapoarte** — clasamentul pe toate problemele și exporturile: raport complet de
   tipărit (HTML), CSV pentru Excel, **borderouri TXT în stil OJI** (câte un fișier pe elev plus
   `All_Results`), rezultate JSON.

Modificarea baremului (puncte, limite, politici) recalculează imediat punctajele, fără să mai rulezi
programele. Totul se salvează automat.

## Organizarea folderelor

| organizare | sursele elevilor | teste |
|------------|------------------|-------|
| problemă, apoi elevi (implicit) | `{root}/{problem}/{student}/…` | `{root}/{problem}/_teste` |
| elev, apoi probleme | `{root}/{student}/{problem}/…` | `{root}/_teste/{problem}` |
| folder comun (stil OJI) | `{root}/{problem}/surse/{student}{nr}.{ext}` (ex. `BN_C_152.cpp` = elevul `BN_C_15`, problema 2) | `{root}/{problem}/teste` |
| personalizat | orice șablon cu `{root}` `{problem}` `{nr}` `{student}` | oriunde |

Folderul de teste e **unul singur pe problemă**, comun tuturor elevilor, și poate fi mutat oriunde
(câmpul „Folder teste” al problemei). Tiparele numelor sunt configurabile (`{n}-{problem}.in` /
`{n}-{problem}.ok` implicit). Dacă în folder există `teste.txt` (formatul OJI: „număr punctaj” pe
fiecare linie), punctele se iau de acolo.

**Importă din Evaluatorul OJI** citește folderul vechiului evaluator (`rounds.xml`, `probleme/…/teste`,
`teste.txt`, surse în `surse_concurenti`) și creează spațiul de lucru echivalent.

## Cum primește programul datele

- **Fișiere locale (olimpiadă)**: programul citește `problema.in` și scrie `problema.out` în folderul
  curent (numele se pot schimba, ex. `date.in` / `date.out`).
- **Consolă**: intrarea vine pe stdin, ieșirea se ia de pe stdout.
- **Folder din proiect**: fișierele sunt într-un folder relativ la rădăcina proiectului elevului
  (ex. `date/{problem}.in`); poți alege și folderul curent al programului (ex. `cmake-build-debug`).

Implicit fiecare test rulează într-un folder temporar gol care reproduce structura relativă (programul
nu poate citi accidental testele altui elev); opțional, poate rula direct în proiectul elevului.

## Găsirea proiectului elevului

Elevii predau proiecte întregi (CLion, Code::Blocks, VS Code, foldere cu scripturile cursului). Regulile
de rădăcină se încearcă în ordine; prima potrivire stabilește rădăcina proiectului, opțional urcând
câteva niveluri. Implicit: `CMakeLists.txt`, `*.cbp`, `bash-scripts/compile.sh`, `bash-scripts/build.sh`,
`compile.sh`, `build.sh`, `main.cpp`, `main.c`, `*.cpp`, `*.c`, `*.py`. Folderele ignorate (implicit
`.* _* cmake-build-* CMakeFiles build bin obj out release debug __MACOSX node_modules venv __pycache__`)
și fișierul principal (`main.cpp main.c {problem}.cpp …`) se configurează la fel.

## Compilare

Trei moduri, pe problemă:

| mod | ce face |
|-----|---------|
| **O singură comandă** | `gcc`/`g++` cu opțiunile din Setări, pe fișierul principal sau pe toate sursele proiectului |
| **Ca scripturile cursului** | refăcut în evaluator după `compile.sh` + `build.sh`: fiecare `.c`/`.cpp` devine obiect, apoi legare cu `g++` dacă există `.cpp` (altfel `gcc`); merge și pe Windows |
| **Scripturile elevului** | rulează chiar `bash-scripts/compile.sh && bash-scripts/build.sh` din proiect (sau `compile.sh`/`build.sh` din rădăcină, structura veche) și testează `release/program` |

**Bibliotecile se adaugă automat** după `#include`, ca în `build.sh` (`check_library`): `math.h` → `-lm`,
`pthread.h` → `-pthread`, `ncurses.h`, SDL2, raylib, OpenGL/GLFW/GLEW, `wx/*` →
`$(wx-config --cxxflags --libs)`, zlib, sqlite3, curl, OpenSSL, GMP etc. Regulile sunt editabile în
Setări (opțiunile pot conține comenzi `$(…)`). Comenzile de compilare și rulare ale fiecărui limbaj se
editează tot acolo (variabile: `{sources}` `{exe}` `{libs}` `{main}` `{stack}` `{problem}`).

## Ce se măsoară

- **Timpul** procesorului (sau timpul real, la alegere), pe fiecare test. Programul este oprit la
  limita dură (implicit 2 × limita).
- **Memoria RAM** maximă. Implicit se scade memoria unui program gol (calibrare, ca la OJI), ca să
  conteze doar ce folosește elevul.
- **Stiva** folosită (prin sonda de măsurare compilată alături de sursă; nu se numără la dimensiune).
- **Dimensiunea sursei**: doar fișierul principal sau toate fișierele `.c .h .cpp .hpp …` ale
  proiectului.

Limitele se aplică la rulare: pe Linux prin `setrlimit` (timp, memorie, stivă, dimensiunea ieșirii),
pe Windows prin Job Object (memorie, timp, un singur proces) și `-Wl,--stack=…` la compilare. Pe macOS
sistemul nu impune limita de memorie; memoria se măsoară, iar depășirea se punctează la fel.

## Punctaj

Pentru fiecare test: punctele de corectitudine (sau punctajul dat de verificator, ori o fracțiune),
plus punctele pentru timp și pentru memorie, dacă testul s-a încadrat. La depășirea limitei de timp
sau de memorie, poți alege: **0 puncte**, **un procent**, **scădere liniară** până la limita dură
sau **ignoră**. La depășirea dimensiunii sursei: 0, procent, penalizare fixă sau ignoră. Pe problemă
se pot da și puncte pentru compilare și pentru încadrarea în dimensiune.

Verdicte: OK, Parțial, Greșit, Timp depășit, Memorie depășită, Eroare la execuție (cu motivul:
împărțire la zero, acces invalid, stivă depășită…), Fără fișier de ieșire, Eroare de compilare,
Lipsă sursă, Eroare verificator.

Comparare: pe linii (ignoră spațiile de la final și liniile goale de la sfârșit), pe cuvinte, exactă,
numere reale cu toleranță, sau **verificator extern** (`verif.cpp`/`verif.c`/`verif.py`/executabil,
compilat automat): protocolul OJI (fișierele `problema.in/.ok/.out`, afișează „punctaj mesaj”),
fracțiune (0…1) sau cod de ieșire.

## Fișierele

```
concurs/
  evaluator.json                  configurația și baremul
  suma/_teste/0-suma.in, 0-suma.ok, …, teste.txt, verif.cpp
  suma/ana/…                      proiectul elevului
  _rezultate/suma/ultimele.json   rezultatele (plus câte o copie datată la fiecare evaluare)
  _lucru/                         compilări, rulări, ieșirile păstrate (_lucru/iesiri/suma/ana/0.out)
```

Baremul exportat (`.barem.json`) conține o singură problemă și se poate importa în alt spațiu de
lucru. La import se acceptă și `evaluator.json` (alegi problema), `teste.txt` și `rounds.xml` (OJI).
Setările aplicației (ultimul spațiu de lucru, aspectul) sunt în `~/.evaluator-olimpiada/setari.json`.

## Compilarea din surse

```
cd c   && ./build.sh           # Linux/macOS: bin/evaluator-c          (sau: make)
cd c   && ./build.sh static    # legat static (orice distribuție Linux)
cd c   && ./build.sh windows   # de pe Linux, cu MinGW-w64: bin/evaluator-c.exe   (sau: make windows)
c\build.bat                    # pe Windows, cu gcc (MinGW-w64) în PATH
```

La fel pentru `cpp/` (`build.sh`, `build.bat`, `Makefile`; necesită C++17). Pe Windows folosește un
MinGW-w64 (Code::Blocks 20.03 sau mai nou, MSYS2, w64devkit). Dacă modifici interfața
(`gui/index.html`) sau fișierele din `common/`, rulează întâi `python3 tools/embed.py`: copiază
resursele în toate variantele și regenerează antetele încorporate pentru C și C++.

Varianta Web folosește utilitarul `olieval-run` (din `common/runner/`) pentru limite și măsurători
exacte. Este precompilat în `web/bin/` pentru Windows și Linux; altfel îl compilează singură la prima
pornire, dacă găsește `gcc`. Fără el merge în **mod de rezervă** (fără limită de memorie, timpul
procesor doar prin sondă).

## Structura

```
gui/index.html        interfața (una singură, servită identic de toate variantele)
c/                    varianta C (C11):   src/, bin/, build.sh, build.bat, Makefile, porneste.*
cpp/                  varianta C++ (C++17): src/, bin/, build.sh, build.bat, Makefile, porneste.*
python/               varianta Python: evaluator.py (+ copii ale interfeței și sondelor), porneste.*
web/                  varianta Web: server.js, public/, helper/ (sursele olieval-run), bin/, package.json, porneste.*
common/               sonda de măsurare (C și Python) și utilitarul de rulare cu limite (runner.c)
docs/SPECIFICATIE.md  specificația tehnică: formatul fișierelor, algoritmii, API-ul HTTP
exemplu/              spațiul de lucru exemplu
tools/                embed.py (sincronizarea resurselor), make_example.py, scripturile cursului folosite în exemplu
tests/                api_client.py, compare_summaries.py, make_layouts.py, gui_smoke.py
```

Cele patru variante dau **exact aceleași** rezultate (stări, punctaje, mesaje) pe exemplu, pe toate
cele trei organizări de foldere și pe cele trei moduri de compilare; verificarea:
`python3 tests/api_client.py --url http://127.0.0.1:PORT --ws COPIE_EXEMPLU --out rezumat.json`, apoi
`python3 tests/compare_summaries.py rezumat1.json rezumat2.json`.

## Siguranță

Serverul ascultă doar pe `127.0.0.1`, cu un cod de acces nou la fiecare pornire, deci alte site-uri
sau alte calculatoare nu îl pot folosi. Programele elevilor rulează cu limite de timp și memorie, dar
**cu drepturile utilizatorului tău** (ca la Evaluatorul OJI): nu sunt izolate de restul fișierelor.
Evaluează surse necunoscute într-un cont separat sau într-o mașină virtuală.
