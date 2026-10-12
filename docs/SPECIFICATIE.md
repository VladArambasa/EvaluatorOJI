# Evaluator Olimpiadă — specificație tehnică (comună tuturor variantelor)

Acest document descrie **exact** comportamentul motorului de evaluare, formatul fișierelor
de configurare și API-ul HTTP. Variantele C, C++, Python și Web (Node.js) implementează
toate aceeași specificație, iar interfața grafică (`gui/index.html`) este **un singur fișier**
servit identic de fiecare variantă.

```
 ┌──────────────── gui/index.html (aceeași interfață) ────────────────┐
 │  fetch("/api/...")  JSON                                          │
 └───────────────┬────────────────────────────────────────────────────┘
                 │ http://127.0.0.1:8765   (doar local)
   ┌─────────────┼──────────────┬───────────────┬───────────────┐
   │ C (C11)     │ C++ (C++17)  │ Python 3      │ Web (Node.js) │   ← motoare
   └─────────────┴──────────────┴───────────────┴───────────────┘
        compilează · rulează cu limite · măsoară · compară · punctează
```

## 1. Spațiul de lucru (workspace)

Un spațiu de lucru este un folder (rădăcina, `{root}`) care conține fișierul
`evaluator.json`. Toate căile din configurare sunt **șabloane** cu variabilele:

| variabilă   | semnificație                                       |
|-------------|----------------------------------------------------|
| `{root}`    | folderul spațiului de lucru                         |
| `{problem}` | numele problemei (ex. `suma`)                       |
| `{nr}`      | numărul problemei (1, 2, …) — stil OJI              |
| `{student}` | identificatorul elevului (numele folderului)        |
| `{n}`       | numărul / id-ul testului (în tiparele de teste)     |

O cale rezultată care nu e absolută este considerată relativă la `{root}`.
Separatorii `/` și `\` sunt acceptați amândoi.

### 1.1 `evaluator.json`

```json
{
  "format": "evaluator-olimpiada",
  "version": 1,
  "name": "Cerc informatică",
  "layout": "problem-student",
  "studentDir": "{root}/{problem}/{student}",
  "flatDir": "{root}/{problem}/surse",
  "flatPattern": "{student}{nr}.{ext}",
  "testsDir": "{root}/{problem}/_teste",
  "workDir": "{root}/_lucru",
  "resultsDir": "{root}/_rezultate",
  "students": [ { "id": "ana", "name": "Ana Popescu", "group": "7A" } ],
  "problems": [ /* obiecte Problemă, vezi 1.2 */ ],
  "current": "suma",
  "languages": [
    { "id": "c",   "name": "C",      "ext": ".c",
      "compile": "gcc -O2 -Wall -std=c11 {sources} -o {exe} {libs}", "run": "{exe}", "probe": true },
    { "id": "cpp", "name": "C++",    "ext": ".cpp .cc .cxx",
      "compile": "g++ -O2 -Wall -std=c++17 {sources} -o {exe} {libs}", "run": "{exe}", "probe": true },
    { "id": "py",  "name": "Python", "ext": ".py",
      "compile": "", "run": "python3 {main}", "probe": false }
  ],
  "autoLibs": { "enabled": true, "rules": [ { "name": "Math", "headers": "math.h complex.h tgmath.h fenv.h", "flags": "-lm" } ] },
  "ignoreDirs": ".* _* cmake-build-* CMakeFiles build bin obj out release debug __MACOSX node_modules venv __pycache__",
  "options": { "keepOutputs": true, "windowsStackFlag": true, "compileTimeoutSec": 60,
               "nonzeroExitIsError": true, "useProbe": true }
}
```

`layout` (doar o presetare pentru șabloane; motorul folosește șabloanele):

| layout            | `studentDir` implicit             | `testsDir` implicit          |
|-------------------|-----------------------------------|------------------------------|
| `problem-student` | `{root}/{problem}/{student}`      | `{root}/{problem}/_teste`    |
| `student-problem` | `{root}/{student}/{problem}`      | `{root}/_teste/{problem}`    |
| `flat` (stil OJI) | — fișiere `flatDir`/`flatPattern` | `{root}/{problem}/teste`     |
| `custom`          | orice șablon care conține `{student}` ca un nume de folder întreg |

### 1.2 Problemă (barem)

```json
{
  "name": "suma", "nr": 1, "title": "",
  "testsDir": "",
  "inputPattern": "{n}-{problem}.in",
  "okPattern": "{n}-{problem}.ok",
  "tests": [
    { "id": "0", "input": "0-suma.in", "ok": "0-suma.ok",
      "points": 10, "timePoints": 0, "memPoints": 0, "timeMs": 0, "memMb": 0 }
  ],
  "io": { "input": "file", "output": "file",
          "inputPath": "{problem}.in", "outputPath": "{problem}.out",
          "cwd": "", "runIn": "sandbox" },
  "project": {
    "rootRules": [ { "pattern": "CMakeLists.txt", "up": 0 }, { "pattern": "*.cbp", "up": 0 },
                   { "pattern": "bash-scripts/compile.sh", "up": 0 }, { "pattern": "bash-scripts/build.sh", "up": 0 },
                   { "pattern": "compile.sh", "up": 0 }, { "pattern": "build.sh", "up": 0 },
                   { "pattern": "main.cpp", "up": 0 }, { "pattern": "main.c", "up": 0 },
                   { "pattern": "*.cpp", "up": 0 }, { "pattern": "*.c", "up": 0 }, { "pattern": "*.py", "up": 0 } ],
    "maxDepth": 6,
    "mainPatterns": "main.cpp main.c {problem}.cpp {problem}.c *.cpp *.c *.py",
    "compileScope": "main",
    "sizeScope": "main",
    "sizeExt": ".c .h .cpp .hpp .cc .cxx .hh .py",
    "buildMode": "command",
    "buildCommand": "auto",
    "buildExe": "release/program"
  },
  "compileExtra": "",
  "limits": { "timeMs": 1000, "timeMode": "cpu", "hardTimeFactor": 2,
              "memMb": 64, "hardMemFactor": 2, "stackMb": 8,
              "sourceKb": 0, "outputMb": 64, "calibrate": true },
  "policy": { "time":   { "mode": "zero", "percent": 50 },
              "memory": { "mode": "zero", "percent": 50 },
              "size":   { "mode": "zero", "percent": 50, "penalty": 10 } },
  "points": { "compile": 0, "size": 0 },
  "compare": { "mode": "lines", "ignoreCase": false, "epsilon": 0.000001,
               "checker": "", "protocol": "oji", "args": "" }
}
```

* `tests[].input`/`ok` sunt relative la folderul de teste (sau absolute).
  `timeMs`/`memMb` = 0 înseamnă „limita problemei”.
* `limits.timeMs`, `memMb`, `stackMb`, `sourceKb`: 0 = fără limită.

Fișierul exportat pentru o singură problemă („barem”):
`{ "format": "evaluator-olimpiada/barem", "version": 1, "problem": { … } }`.

## 2. Detectare

### 2.1 Potrivire de nume (glob)
`*` = orice șir fără `/`, `?` = un caracter; comparație **fără diferență între majuscule**.
Un tipar care conține `/` se potrivește cu ultimele componente ale căii relative.

### 2.2 Teste
În `testsDir` se listează fișierele (adâncime ≤ 2). Un fișier e intrare dacă se potrivește cu
`inputPattern` (după înlocuirea `{problem}`, `{nr}`); `{n}` = 1+ caractere fără `/`.
Fișierul corect = `okPattern` cu același `{n}`. Sortare naturală după `{n}` (2 < 10).
Un fișier `verif.cpp`, `verif.c`, `verif.py`, `verif.exe` sau `verif` din folderul de teste
este raportat ca verificator găsit.

### 2.3 Elevi
*Foldere*: șablonul `studentDir` se împarte la `{student}`; se listează folderele din partea
dinaintea lui, ignorând numele care încep cu `.` sau `_`. Se adaugă elevii din `students`
(chiar dacă nu au folder → „lipsă”). Sortare naturală după id.
*Folder comun (`flat`)*: fișierele din `flatDir` care se potrivesc cu `flatPattern`
(`{student}` = 1+ caractere, `{ext}` = extensia, `{nr}`/`{problem}` literale).

### 2.4 Rădăcina proiectului elevului
Se parcurge folderul elevului în lățime (BFS, adâncime ≤ `maxDepth`, se sar folderele care se
potrivesc cu `ignoreDirs`). Pentru fiecare regulă din `rootRules`, în ordine: prima potrivire
(cea mai puțin adâncă, apoi alfabetic) dă rădăcina = folderul în care începe tiparul, urcat
cu `up` niveluri (nu mai sus de folderul elevului). Fără potrivire → folderul elevului.

### 2.5 Fișierul principal, limbajul, sursele, dimensiunea
* `mainPatterns`, în ordine: primul tipar cu potriviri sub rădăcină; dintre potriviri se
  preferă fișierele care conțin `main(`, apoi cele mai puțin adânci, apoi alfabetic.
* Limbajul = după extensia fișierului principal (`languages[].ext`).
* Surse compilate: `compileScope = main` → doar fișierul principal; `all` → toate fișierele
  cu extensiile limbajului de sub rădăcină (fără folderele ignorate), sortate.
* Dimensiune: `sizeMain` (fișierul principal), `sizeAll` (toate fișierele cu extensiile din
  `sizeExt`). Se judecă `sizeScope`.

## 3. Compilare

`project.buildMode` alege modul (limbajele fără comandă de compilare, ex. Python, nu se compilează):

| mod        | ce face |
|------------|---------|
| `command`  | o singură comandă: `languages[].compile` (detalii mai jos) |
| `objects`  | ca scripturile cursului `compile.sh` + `build.sh`, refăcute în evaluator: fiecare `.c/.cpp/.cc/.cxx` de sub rădăcină (fără folderele ignorate) se compilează separat cu `<compilator> <opțiuni> -c sursă -o obj/…/sursă.o` (compilatorul și opțiunile = comanda limbajului fișierului, fără `{sources}`, `-o {exe}`, `{libs}`); obiectele stau în `{workDir}/build/{problem}/{student}/obj`; sonda se compilează ca obiect cu compilatorul C; legarea: compilatorul C++ dacă există vreun fișier C++, altfel C: `<linker> obiecte… -o exe <libs>`. Opțiunile din `{libs}` fără `-l`, `-L`, `-Wl,` se dau și la pasul `-c`. |
| `scripts`  | rulează scripturile elevului cu shell-ul sistemului (`sh -c`, Windows `cmd /c`), în rădăcina proiectului: `buildCommand`; `auto` = primul care există dintre `bash bash-scripts/compile.sh && bash bash-scripts/build.sh`, același pentru `scripts/` și pentru rădăcină, apoi doar `build.sh`. Înainte se șterge `buildExe` (și `buildExe.exe`); compilarea reușește dacă executabilul există după rulare (scripturile cursului întorc mereu 0). Sonda nu poate fi legată; pe Linux se compilează `olieval_probe.so` (`gcc -O2 -shared -fPIC`) și se injectează cu `LD_PRELOAD`, inclusiv la calibrare. |

### 3.1 Modul `command`

Comanda (`languages[].compile`) se împarte în cuvinte (ghilimelele grupează). Înlocuiri:
`{sources}` → sursele (căi absolute) + sonda (`olieval_probe.c`, dacă `useProbe` și
`probe`); `{libs}` → bibliotecile detectate automat + `-Wl,--stack=<octeți>` (numai pe Windows,
dacă `windowsStackFlag` și `stackMb>0`) + `compileExtra`; `{exe}`, `{main}`, `{stack}`
(octeți), `{problem}`. Comanda rulează cu folderul curent = rădăcina proiectului; executabilul
ajunge în `{workDir}/build/{problem}/{student}/prog[.exe]`. Dacă programul compilatorului
nu e în `PATH`, pe Windows se caută în locațiile uzuale (Code::Blocks/MinGW/MSYS2/OJI).

**Biblioteci automate** (ca în `build.sh` / `check_library`): se citesc liniile `#include <…>` din
sursele compilate și din `.h/.hpp` de sub rădăcină; fiecare regulă `{headers, flags}` care are un
antet potrivit (glob) adaugă `flags`. În `flags`, `$(comandă)` sau `` `comandă` `` se înlocuiește cu
ieșirea comenzii (ex. `$(wx-config --cxxflags --libs)`), rulată o singură dată pe evaluare.

**Calibrarea memoriei** (`calibrate`): pentru fiecare limbaj folosit se compilează și se rulează
de 3 ori un program „gol” (C: `stdio`, C++: `iostream`/`fstream`), iar minimul memoriei măsurate
devine `baseline[limbaj]`; memoria judecată = `max(0, măsurată − baseline)`.

## 4. Rulare pe un test

* `runIn = sandbox`: rădăcina rulării = `{workDir}/run/{problem}/sandbox` (golit înainte de
  fiecare test). `runIn = project`: rădăcina rulării = rădăcina proiectului elevului.
* Folderul curent = rădăcina rulării + `io.cwd`.
* `io.input = file`: testul se copiază în `rădăcină/inputPath`; intrarea standard = dispozitivul nul.
  `io.input = stdin`: intrarea standard = fișierul de test.
* `io.output = file`: se citește `rădăcină/outputPath` (șters înainte de rulare);
  `io.output = stdout`: se citește ieșirea standard.
* Limite: timp „dur” (ceas) = `L × max(1, hardTimeFactor) + 500 ms`; CPU = ⌈dur/1000⌉+1 s;
  memorie „dură” = `memMb × 1024 × max(1, hardMemFactor) + 65536` KB (Linux `RLIMIT_AS`,
  Windows Job Object); stivă = `stackMb` (Linux `RLIMIT_STACK`, Windows `-Wl,--stack`);
  ieșire ≤ `outputMb`.
* Măsurători: timp CPU, timp real, memorie (RAM) maximă, stivă maximă. Pentru C/C++ sonda
  `olieval_probe.c` (legată în program; în modul `scripts` pe Linux prin `LD_PRELOAD`) scrie la
  ieșirea normală `rss_kb`, `stack_kb`, `cpu_ms` în fișierul din variabila `OLIEVAL_PROBE`; are
  prioritate față de măsurarea din exterior. Pentru Python, sonda este `sitecustomize.py` (folderul
  ei se pune în `PYTHONPATH`). Dacă sonda era activă dar nu a scris nimic (program oprit/căzut), pe
  Linux memoria este necunoscută (`null`); pe Windows se folosește măsurarea din exterior.

### 4.1 Verdict brut
1. nu a pornit → `RE`; 2. oprit la limita dură de timp (sau `SIGXCPU`) → `TLE`;
3. oprit de un semnal / cod NTSTATUS → `MLE` dacă ieșirea de eroare conține `bad_alloc`,
   `MemoryError` sau `out of memory`, ori memoria ≥ 95% din limita dură; altfel `RE`;
4. cod de ieșire ≠ 0 și `nonzeroExitIsError` → `RE`;
5. lipsă fișier de ieșire → `NOOUT`; ieșire mai mare decât `outputMb` → `WA`;
6. comparare / verificator → fracția `f ∈ [0,1]`: `OK` (f=1), `PARTIAL`, `WA` (f=0),
   `CHKERR` (verificator defect).

### 4.2 Comparare (`compare.mode`)
Înainte de comparare se elimină toate caracterele `\r`.
* `lines` (implicit, ca la OJI): linie cu linie, ignorând spațiile/tab-urile de la final de
  linie și liniile goale de la sfârșitul fișierului.
* `tokens`: șirurile de „cuvinte” separate prin spații albe trebuie să coincidă.
* `exact`: conținut identic.
* `real`: ca `tokens`, dar numerele se compară cu toleranța `|a−b| ≤ ε·max(1,|b|)`.
* `ignoreCase`: litere mari = mici (ASCII).
* `checker`: program extern (`compare.checker`, relativ la folderul de teste; `.c/.cpp` se
  compilează automat, `.py` rulează cu Python):
  * `oji`: în `{workDir}/run/{problem}/check` se pun `{problem}.in`, `{problem}.ok`,
    `{problem}.out`; verificatorul afișează `punctaj mesaj`; `f = min(punctaj, P)/P`.
  * `fraction`: argumente implicite `{input} {ok} {output}`; afișează un număr în [0,1].
  * `exitcode`: argumente implicite `{input} {output} {ok}` (testlib); cod 0 = corect.

## 5. Punctaj

Pentru un test cu `P = points`, `TP = timePoints`, `MP = memPoints`:

```
dacă verdict brut ∈ {RE, TLE, MLE, NOOUT, CHKERR}: scor = 0
altfel:
  bază = f·P              (la protocolul oji: min(punctaj_verif, P))
  Lt = timeMs test sau problemă;  t = cpuMs (timeMode=cpu) sau wallMs
  Lm = memMb  test sau problemă;  m = memKb judecată
  peste_timp = Lt>0 și t > Lt          peste_mem = Lm>0 și m > Lm·1024
  ft = 1, sau factor(policy.time,   t, Lt, Lt·hardTimeFactor)        dacă peste_timp
  fm = 1, sau factor(policy.memory, m, Lm·1024, Lm·1024·hardMemFactor) dacă peste_mem
  scor = bază·ft·fm + (nu peste_timp ? f·TP : 0) + (nu peste_mem ? f·MP : 0)
  stare = TLE dacă peste_timp (politica ≠ ignore), altfel MLE dacă peste_mem, altfel brut

factor(zero)=0 · factor(percent)=percent/100 · factor(ignore)=1
factor(linear)= H>L ? clamp(1 − (v−L)/(H−L), 0, 1) : 0
```

Total elev = Σ scor teste + `points.compile` + (`sizeOk` ? `points.size` : 0), apoi dacă sursa
depășește `sourceKb`: `zero` → 0, `percent` → total·p/100, `penalty` → max(0, total − penalty),
`ignore` → neschimbat. Eroare de compilare → total 0 (`CE`). Lipsă sursă → `NOSRC`.
Maximul = Σ(P+TP+MP) + `points.compile` + `points.size`. Rotunjire la 2 zecimale.

Stări finale ale testelor: `OK`, `PARTIAL`, `WA`, `TLE`, `MLE`, `RE`, `NOOUT`, `CHKERR`, `SKIP`.

## 6. Rezultate

Salvate de motor în `{resultsDir}/{problem}/ultimele.json` (după fiecare elev) și ca instantaneu
`rezultate-AAAALLZZ-HHMMSS.json` la final. La o reevaluare parțială (unii elevi / unele teste),
rezultatele noi se combină cu cele existente (la nivel de test).

```json
{ "format": "evaluator-olimpiada/rezultate", "version": 1, "problem": "suma",
  "variant": "C", "os": "linux", "updated": "2026-10-12 10:30:00",
  "baseline": { "cpp": 3400 }, "maxScore": 100,
  "tests": [ { "id": "0", "points": 10, "timePoints": 0, "memPoints": 0 } ],
  "students": [ {
      "id": "ana", "name": "Ana Popescu", "dir": "…", "root": "…", "main": "main.cpp", "lang": "cpp",
      "sources": ["main.cpp"], "sizeMain": 412, "sizeAll": 412, "sizeJudged": 412, "sizeOk": true,
      "compile": { "ok": true, "ms": 380, "log": "", "cmd": "g++ …" },
      "status": "DONE", "score": 90, "max": 100, "notes": [], "evaluatedAt": "…",
      "tests": [ { "id": "0", "raw": "OK", "status": "OK", "f": 1, "pts": null, "score": 10, "max": 10,
                   "cpuMs": 3.1, "wallMs": 4.2, "timeMs": 3.1, "memKb": 120, "memRawKb": 3520,
                   "stackKb": 132, "exit": 0, "signal": 0, "timedOut": false,
                   "msg": "Corect", "detail": "", "stderr": "", "out": "…/0.out", "err": "…/0.err" } ] } ] }
```

## 7. API HTTP

Serverul ascultă **doar** pe `127.0.0.1` (port implicit 8765, următorul liber dacă e ocupat).
Orice cerere `/api/*` trebuie să aibă antetul `X-Eval-Token` (valoarea e injectată în pagină în
locul textului `__EVAL_TOKEN__`), iar antetul `Host` trebuie să fie `127.0.0.1:port` sau
`localhost:port`. Corpurile sunt JSON (UTF-8). Erorile: `{ "error": "mesaj" }` cu cod 400/500.

| metodă | cale                  | corp → răspuns |
|--------|-----------------------|----------------|
| GET    | `/`                   | interfața (HTML) |
| GET    | `/api/info`           | `{app, version, variant, os, sep, home, settings, cwd, python, compilers, features}` |
| POST   | `/api/fs/list`        | `{path}` → `{path, parent, sep, entries:[{name, dir, size, mtime}], roots}` |
| POST   | `/api/fs/read`        | `{path, max?}` → `{path, size, text, truncated}` |
| POST   | `/api/fs/write`       | `{path, text}` → `{ok}` (creează folderele părinte) |
| POST   | `/api/fs/stat`        | `{path}` → `{exists, dir, size, mtime}` |
| POST   | `/api/fs/mkdir`       | `{path}` → `{ok}` |
| POST   | `/api/open`           | `{path}` → `{ok}` (deschide în managerul de fișiere) |
| POST   | `/api/structure`      | `{workspace, create}` → `{items:[{path, kind, exists}], created, errors}` |
| POST   | `/api/scan`           | `{workspace, problem}` → `{testsDir, testsDirExists, tests, checker, students}` |
| POST   | `/api/eval/start`     | `{workspace, problems, students?, tests?}` → `{ok}` |
| GET    | `/api/eval/status?log=N&rv=V` | `{running, stopping, phase, problem, student, test, done, total, log, logNext, rv, results?}` |
| POST   | `/api/eval/stop`      | `{}` → `{ok}` |
| POST   | `/api/rescore`        | `{workspace, problem, results}` → `{results}` |
| POST   | `/api/quit`           | `{}` → `{ok}` și oprește serverul |

Setările aplicației (ultimul spațiu de lucru, aspectul `"theme": "xp" | "clasic"`) sunt în
`~/.evaluator-olimpiada/setari.json` (citit/scris prin `/api/fs/*`).

## 8. Variantele (implementare)

| variantă | fișiere | rulare procese | observații |
|----------|---------|----------------|------------|
| C (C11)  | `c/src/server.c` (HTTP + API), `engine.c` (motorul), `json.c`, `util.c` (șiruri, căi, fișiere, fire), `runner.c` | `runner.c` direct (fork + setrlimit + wait4 / CreateProcessW + Job Object) | interfața și sondele sunt încorporate în `embedded.h` |
| C++ (C++17) | `cpp/src/server.cpp`, `engine.cpp`, `sys.cpp` (sistem + rulare), `json.hpp` | portare C++ a lui `runner.c` în `sys.cpp` | idem, `embedded.hpp` |
| Python 3.7+ | `python/evaluator.py` (doar biblioteca standard) | `subprocess` + `preexec_fn` (rlimit) + `os.wait4` / ctypes + Job Object | implementarea de referință |
| Web (Node.js 16+) | `web/server.js` (fără pachete npm) + `web/public/index.html` | utilitarul `olieval-run` (din `runner.c`, precompilat în `web/bin/`) | fără utilitar: mod de rezervă, fără limită de memorie |

Toate cele patru variante produc **aceleași** stări, punctaje, mesaje și note pe spațiile de lucru de
test (`tests/compare_summaries.py`, verificat pe toate cele 3 organizări de foldere și cele 3 moduri de compilare).

Resursele comune se sincronizează cu `python3 tools/embed.py` (copiază `gui/index.html`, sondele și
`runner.c` în variante și regenerează `embedded.h` / `embedded.hpp`).
