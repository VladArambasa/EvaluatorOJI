# Olympiad Evaluator: technical specification (shared by all versions)

This document describes **exactly** how the evaluation engine behaves, the format of the
configuration and result files, the HTTP API and the translation files. The C, C++, Python
and Web (Node.js) versions all implement this same specification, and the graphical interface
(`common/gui/index.html`) is **one file** served unchanged by every version.

```
 ┌────────────── common/gui/index.html (the same interface) ─────────────┐
 │  fetch("/api/...")  JSON           translations/*.json (GUI language) │
 └───────────────┬───────────────────────────────────────────────────────┘
                 │ http://127.0.0.1:8765   (local only)
   ┌─────────────┼──────────────┬───────────────┬───────────────┐
   │ C (C11)     │ C++ (C++17)  │ Python 3      │ Web (Node.js) │   <- engines
   └─────────────┴──────────────┴───────────────┴───────────────┘
        compile · run with limits · measure · compare · score
```

Engines always speak **English**: every error, verdict text, note and log line they produce
is English. Only the interface changes language (section 8).

## 1. Workspace

A workspace is a folder (the root, `{root}`) that contains the file `evaluator.json`.
Every path in the configuration is a **template** with these variables:

| variable    | meaning                                            |
|-------------|----------------------------------------------------|
| `{root}`    | the workspace folder                               |
| `{problem}` | the problem name (e.g. `sum`)                      |
| `{nr}`      | the problem number (1, 2, …), OJI style            |
| `{student}` | the student id (the folder name)                   |
| `{n}`       | the test number / id (in the test patterns)        |

A resulting path that is not absolute is relative to `{root}`. Both `/` and `\` separators are accepted.

### 1.1 `evaluator.json`

```json
{
  "format": "olympiad-evaluator",
  "version": 1,
  "name": "Computer club",
  "layout": "problem-student",
  "studentDir": "{root}/{problem}/{student}",
  "flatDir": "{root}/{problem}/sources",
  "flatPattern": "{student}{nr}.{ext}",
  "testsDir": "{root}/{problem}/_tests",
  "workDir": "{root}/_work",
  "resultsDir": "{root}/_results",
  "students": [ { "id": "ana", "name": "Ana Popescu", "group": "7A" } ],
  "problems": [ /* Problem objects, see 1.2 */ ],
  "current": "sum",
  "languages": [
    { "id": "c",   "name": "C",      "ext": ".c",
      "compile": "gcc -O2 -Wall -std=c11 {sources} -o {exe} {libs}", "run": "{exe}", "probe": true },
    { "id": "cpp", "name": "C++",    "ext": ".cpp .cc .cxx",
      "compile": "g++ -O2 -Wall -std=c++17 {sources} -o {exe} {libs}", "run": "{exe}", "probe": true },
    { "id": "py",  "name": "Python", "ext": ".py",
      "compile": "", "run": "python3 {main}", "probe": true }
  ],
  "autoLibs": { "enabled": true, "rules": [ { "name": "Math", "headers": "math.h complex.h tgmath.h fenv.h", "flags": "-lm" } ] },
  "ignoreDirs": ".* _* cmake-build-* CMakeFiles build bin obj out release debug __MACOSX node_modules venv __pycache__",
  "options": { "keepOutputs": true, "windowsStackFlag": true, "compileTimeoutSec": 60,
               "nonzeroExitIsError": true, "useProbe": true }
}
```

`layout` is only a preset for the templates; the engine uses the templates:

| layout            | default `studentDir`               | default `testsDir`           |
|-------------------|------------------------------------|------------------------------|
| `problem-student` | `{root}/{problem}/{student}`       | `{root}/{problem}/_tests`    |
| `student-problem` | `{root}/{student}/{problem}`       | `{root}/_tests/{problem}`    |
| `flat` (OJI style)| files `flatDir`/`flatPattern` (`{root}/{problem}/sources`) | `{root}/{problem}/tests` |
| `custom`          | any template with `{student}` as a whole folder name | any |

### 1.2 Problem (scoring scheme)

```json
{
  "name": "sum", "nr": 1, "title": "",
  "testsDir": "",
  "inputPattern": "{n}-{problem}.in",
  "okPattern": "{n}-{problem}.ok",
  "tests": [
    { "id": "0", "input": "0-sum.in", "ok": "0-sum.ok",
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

* `tests[].input`/`ok` are relative to the tests folder (or absolute).
  `timeMs`/`memMb` = 0 means "the problem limit".
* `limits.timeMs`, `memMb`, `stackMb`, `sourceKb`: 0 = no limit.

A scoring scheme exported for one problem (`<problem>.scoring.json`):
`{ "format": "olympiad-evaluator/scoring", "version": 1, "problem": { … } }`.
Import also accepts the older format id `evaluator-olimpiada/barem`, a whole `evaluator.json`
(the user picks the problem), an OJI `teste.txt` and an OJI `rounds.xml`.

## 2. Detection

### 2.1 Name matching (glob)
`*` = any text without `/`, `?` = one character; the comparison **ignores letter case**.
A pattern that contains `/` matches the last components of the relative path.

### 2.2 Tests
The files of `testsDir` are listed (depth ≤ 2). A file is an input when it matches
`inputPattern` (after replacing `{problem}`, `{nr}`); `{n}` = 1+ characters without `/`.
The answer file = `okPattern` with the same `{n}`. Natural sort by `{n}` (2 < 10).
A `verif.cpp`, `verif.c`, `verif.py`, `verif.exe` or `verif` file in the tests folder is
reported as a checker found.

### 2.3 Students
*Folders*: the `studentDir` template is split at `{student}`; the folders of the part before it
are listed, skipping names that start with `.` or `_`. The students of `students` are added
(even without a folder: "missing"). Natural sort by id.
*Shared folder (`flat`)*: the files of `flatDir` that match `flatPattern`
(`{student}` = 1+ characters, `{ext}` = the extension, `{nr}`/`{problem}` literal).

### 2.4 Root of the student's project
The student folder is walked breadth-first (BFS, depth ≤ `maxDepth`, skipping the folders that
match `ignoreDirs`). For each rule of `rootRules`, in order: the first match (shallowest, then
alphabetical) gives the root = the folder where the pattern starts, moved up `up` levels (never
above the student folder). No match: the student folder.

### 2.5 Main file, language, sources, size
* `mainPatterns`, in order: the first pattern with matches under the root; among the matches the
  files that contain `main(` win, then the shallowest, then alphabetical order.
* Language = by the extension of the main file (`languages[].ext`).
* Compiled sources: `compileScope = main` → only the main file; `all` → every file with the
  language's extensions under the root (without the ignored folders), sorted.
* Size: `sizeMain` (the main file), `sizeAll` (every file with an extension from `sizeExt`).
  `sizeScope` chooses which one is judged.

## 3. Compilation

`project.buildMode` picks the mode (languages without a compile command, e.g. Python, are not compiled):

| mode       | what it does |
|------------|--------------|
| `command`  | one command: `languages[].compile` (details below) |
| `objects`  | like the course scripts `compile.sh` + `build.sh`, redone inside the evaluator: every `.c/.cpp/.cc/.cxx` under the root (without the ignored folders) is compiled on its own with `<compiler> <options> -c source -o obj/…/source.o` (compiler and options = the language command of that file without `{sources}`, `-o {exe}`, `{libs}`); objects go to `{workDir}/build/{problem}/{student}/obj`; the probe is compiled as an object with the C compiler; linking: the C++ compiler when there is any C++ file, otherwise the C compiler: `<linker> objects… -o exe <libs>`. Options of `{libs}` other than `-l`, `-L`, `-Wl,` are also given to the `-c` step. The log has one `Compiled: source -> object` line per file. |
| `scripts`  | runs the student's scripts with the system shell (`sh -c`, Windows `cmd /c`) in the project root: `buildCommand`; `auto` = the first that exists of `bash bash-scripts/compile.sh && bash bash-scripts/build.sh`, the same for `scripts/` and for the root, then `build.sh` alone. `buildExe` (and `buildExe.exe`) is deleted first; the build succeeds when the executable exists afterwards (the course scripts always return 0). The probe cannot be linked; on Linux `olieval_probe.so` is compiled (`gcc -O2 -shared -fPIC`) and injected with `LD_PRELOAD`, calibration included. |

### 3.1 The `command` mode

The command (`languages[].compile`) is split into words (quotes group). Replacements:
`{sources}` → the sources (absolute paths) + the probe (`olieval_probe.c`, when `useProbe` and
`probe`); `{libs}` → the automatically detected libraries + `-Wl,--stack=<bytes>` (Windows only,
when `windowsStackFlag` and `stackMb>0`) + `compileExtra`; `{exe}`, `{main}`, `{stack}`
(bytes), `{problem}`. The command runs in the project root; the executable goes to
`{workDir}/build/{problem}/{student}/prog[.exe]`. When the compiler is not in `PATH`, on Windows
the usual places are searched (Code::Blocks / MinGW / MSYS2 / OJI).

**Automatic libraries** (like `build.sh` / `check_library`): the `#include <…>` lines of the compiled
sources and of the `.h/.hpp` files under the root are read; every rule `{headers, flags}` with a
matching header (glob) adds its `flags`. In `flags`, `$(command)` or `` `command` `` is replaced by
the output of the command (e.g. `$(wx-config --cxxflags --libs)`), run once per evaluation.

**Memory calibration** (`calibrate`): for each language in use an "empty" program (C: `stdio`,
C++: `iostream`/`fstream`) is compiled and run 3 times; the smallest memory measured becomes
`baseline[language]`; the judged memory = `max(0, measured − baseline)`.

## 4. Running one test

* `runIn = sandbox`: run root = `{workDir}/run/{problem}/sandbox` (emptied before every test).
  `runIn = project`: run root = the root of the student's project.
* Current folder = run root + `io.cwd`.
* `io.input = file`: the test is copied to `root/inputPath`; standard input = the null device.
  `io.input = stdin`: standard input = the test file.
* `io.output = file`: `root/outputPath` is read (deleted before the run);
  `io.output = stdout`: the standard output is read.
* Limits: hard (wall clock) time = `L × max(1, hardTimeFactor) + 500 ms`; CPU = ⌈hard/1000⌉+1 s;
  hard memory = `memMb × 1024 × max(1, hardMemFactor) + 65536` KB (Linux `RLIMIT_AS`,
  Windows Job Object); stack = `stackMb` (Linux `RLIMIT_STACK`, Windows `-Wl,--stack`);
  output ≤ `outputMb`.
* Measurements: CPU time, wall time, peak memory (RAM), peak stack. For C/C++ the probe
  `olieval_probe.c` (linked into the program; in `scripts` mode on Linux through `LD_PRELOAD`)
  writes `rss_kb`, `stack_kb`, `cpu_ms` on a normal exit to the file named by the `OLIEVAL_PROBE`
  variable; it wins over the outside measurement. For Python the probe is `sitecustomize.py`
  (its folder `{workDir}/probe-py` goes into `PYTHONPATH`). When the probe was active but wrote
  nothing (program killed or crashed), on Linux the memory is unknown (`null`); on Windows the
  outside measurement is used.
* Kept outputs (`keepOutputs`): `{workDir}/outputs/{problem}/{student}/{test}.out` (and `.err`).

### 4.1 Raw verdict
1. not started → `RE`; 2. stopped at the hard time limit (or `SIGXCPU`) → `TLE`;
3. stopped by a signal / NTSTATUS code → `MLE` when the error output contains `bad_alloc`,
   `MemoryError` or `out of memory`, or the memory is ≥ 95% of the hard limit; otherwise `RE`;
4. exit code ≠ 0 and `nonzeroExitIsError` → `RE`;
5. no output file → `NOOUT`; output larger than `outputMb` → `WA`;
6. comparison / checker → the fraction `f ∈ [0,1]`: `OK` (f=1), `PARTIAL`, `WA` (f=0),
   `CHKERR` (broken checker).

### 4.2 Comparison (`compare.mode`)
Every `\r` is removed before comparing.
* `lines` (default, like OJI): line by line, ignoring spaces/tabs at the end of a line and empty
  lines at the end of the file.
* `tokens`: the sequences of whitespace-separated "words" must match.
* `exact`: identical content.
* `real`: like `tokens`, but numbers are compared with the tolerance `|a−b| ≤ ε·max(1,|b|)`.
* `ignoreCase`: upper case = lower case (ASCII).
* `checker`: an external program (`compare.checker`, relative to the tests folder; `.c/.cpp` are
  compiled automatically into `{workDir}/checker/{problem}`, `.py` runs with Python):
  * `oji`: `{problem}.in`, `{problem}.ok`, `{problem}.out` are put in `{workDir}/run/{problem}/check`;
    the checker prints `score message`; `f = min(score, P)/P`.
  * `fraction`: default arguments `{input} {ok} {output}`; prints a number in [0,1].
  * `exitcode`: default arguments `{input} {output} {ok}` (testlib); exit code 0 = correct.

## 5. Scoring

For a test with `P = points`, `TP = timePoints`, `MP = memPoints`:

```
if the raw verdict is in {RE, TLE, MLE, NOOUT, CHKERR}: score = 0
else:
  base = f·P              (oji protocol: min(checker score, P))
  Lt = timeMs of the test or of the problem;  t = cpuMs (timeMode=cpu) or wallMs
  Lm = memMb of the test or of the problem;   m = judged memKb
  over_time = Lt>0 and t > Lt          over_mem = Lm>0 and m > Lm·1024
  ft = 1, or factor(policy.time,   t, Lt, Lt·hardTimeFactor)          when over_time
  fm = 1, or factor(policy.memory, m, Lm·1024, Lm·1024·hardMemFactor) when over_mem
  score = base·ft·fm + (not over_time ? f·TP : 0) + (not over_mem ? f·MP : 0)
  status = TLE when over_time (policy ≠ ignore), else MLE when over_mem, else the raw verdict

factor(zero)=0 · factor(percent)=percent/100 · factor(ignore)=1
factor(linear)= H>L ? clamp(1 − (v−L)/(H−L), 0, 1) : 0
```

Student total = Σ test scores + `points.compile` + (`sizeOk` ? `points.size` : 0); then, when the
source exceeds `sourceKb`: `zero` → 0, `percent` → total·p/100, `penalty` → max(0, total − penalty),
`ignore` → unchanged. Compilation error → total 0 (`CE`). No source → `NOSRC`.
Maximum = Σ(P+TP+MP) + `points.compile` + `points.size`. Rounded to 2 decimals.

Final test statuses: `OK`, `PARTIAL`, `WA`, `TLE`, `MLE`, `RE`, `NOOUT`, `CHKERR`, `SKIP`.

## 6. Results

Saved by the engine in `{resultsDir}/{problem}/latest.json` (after every student) and as a snapshot
`results-YYYYMMDD-HHMMSS.json` at the end. On a partial re-evaluation (some students / some tests),
the new results are merged into the existing ones (per test).

```json
{ "format": "olympiad-evaluator/results", "version": 1, "problem": "sum",
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
                   "msg": "Correct", "detail": "", "stderr": "", "out": "…/0.out", "err": "…/0.err" } ] } ] }
```

The interface writes its exports next to the results: `report-STAMP.html`, `ranking-STAMP.csv`,
`score-sheets/<student>-res.txt` + `score-sheets/All_Results-res.txt` (OJI style).

## 7. HTTP API

The server listens **only** on `127.0.0.1` (default port 8765, the next free one when busy).
Every `/api/*` request must carry the `X-Eval-Token` header (the value is injected into the page in
place of the text `__EVAL_TOKEN__`), and the `Host` header must be `127.0.0.1:port` or
`localhost:port`. Bodies are JSON (UTF-8). Errors: `{ "error": "message" }` with code 400/500;
the message is always English.

| method | path                  | body → answer |
|--------|-----------------------|---------------|
| GET    | `/`                   | the interface (HTML) |
| GET    | `/api/info`           | `{app, version, variant, os, sep, home, settings, cwd, python, compilers, features, example}` |
| GET    | `/api/translations`   | `{dir, languages: {code: file content}, errors}` (section 8) |
| POST   | `/api/fs/list`        | `{path}` → `{path, parent, sep, entries:[{name, dir, size, mtime}], roots}` |
| POST   | `/api/fs/read`        | `{path, max?}` → `{path, size, text, truncated}` |
| POST   | `/api/fs/write`       | `{path, text}` → `{ok}` (creates the parent folders) |
| POST   | `/api/fs/stat`        | `{path}` → `{exists, dir, size, mtime}` |
| POST   | `/api/fs/mkdir`       | `{path}` → `{ok}` |
| POST   | `/api/open`           | `{path}` → `{ok}` (opens it in the file manager) |
| POST   | `/api/structure`      | `{workspace, create}` → `{items:[{path, kind, exists}], created, errors}`; `kind` ∈ `tests`, `student`, `work`, `results`, `sources` |
| POST   | `/api/scan`           | `{workspace, problem}` → `{testsDir, testsDirExists, tests, checker, students}` |
| POST   | `/api/eval/start`     | `{workspace, problems, students?, tests?}` → `{ok}` |
| GET    | `/api/eval/status?log=N&rv=V` | `{running, stopping, phase, problem, student, test, done, total, log, logNext, rv, results?}`; `phase` ∈ `starting`, `preparing`, `compiling`, `running`, `done`, `stopped`; log lines start with `HH:MM:SS ` |
| POST   | `/api/eval/stop`      | `{}` → `{ok}` |
| POST   | `/api/rescore`        | `{workspace, problem, results}` → `{results}` |
| POST   | `/api/quit`           | `{}` → `{ok}` and stops the server |

`/api/info.example` is the path of `tutorial/examples/contest` when the engine finds it (it looks
up to 5 folders above itself), otherwise empty.

The application settings (last workspace, recent list, look `"theme": "xp" | "classic"`,
`"language"`) live in `~/.olympiad-evaluator/settings.json` (read and written through `/api/fs/*`).
The old value `"clasic"` is read as `"classic"`.

## 8. Translations

* Each version has a `translations/` folder (C and C++ look in `bin/../translations`, then next to
  the executable; Python and Web next to their main file). It holds one `<code>.json` per language
  plus `messages.txt`. The master copies live in `master-translations/` at the project root; the
  scripts `install-windows.bat`, `install-linux.sh` and `install-macos.command` copy them into every
  version (and remove languages that no longer exist there).
* `GET /api/translations` returns every `*.json` of the folder, parsed. A file that is not valid
  JSON is skipped and reported in `errors`.
* Language file:

  ```json
  { "language": "ro", "name": "Română", "decimal": ",",
    "ui":       { "Evaluate problem": "Evaluează problema", "Maximum score: %{score} points": "Punctaj maxim: %{score} puncte" },
    "messages": { "Runtime error: %{status}": "Eroare la execuție: %{status}", "exit code %{n}": "cod de ieșire %{n}" } }
  ```

  * `ui`: interface text; the key is the English text of the interface (it may contain HTML and
    `%{name}` placeholders, which the translation must keep). A missing key shows the English text.
  * `messages`: templates for the English messages of the engines. `%{name}` captures any text.
    Captured values are translated again (up to 4 levels), except names that start with `raw`
    (student data such as expected/received output), which are copied unchanged.
  * `decimal`: the decimal mark used to show numbers (and the CSV separator: `;` with `,`).
* Message translation in the interface (`tm`), applied per line: the time prefix of log lines is
  kept; leading/trailing spaces are kept; an exact template wins; otherwise the templates with
  placeholders are tried, the one with the longest literal text first, each as an anchored match;
  otherwise a text with `; ` is split there and each part is translated. Text that matches nothing
  stays English.
* Language choice: the saved `"language"` setting, else the first browser language with a file,
  else English. The selector is in the title bar.
* `messages.txt` (`KEY=Text`, `#` comments, `{0}` = a value) holds the sentences printed by the
  build, start and install scripts (`.bat` and `.sh`); they are always English.
* `tests/check_translations.py` lists missing, unused and broken keys, checks that the copies in the
  versions match the master files, and (`--results`) checks real result files against the templates.

## 9. The versions (implementation)

| version | files | process running | notes |
|---------|-------|-----------------|-------|
| C (C11) | `versions/c/src/server.c` (HTTP + API), `engine.c` (engine), `json.c`, `util.c` (strings, paths, files, threads), `runner.c` | `runner.c` directly (fork + setrlimit + wait4 / CreateProcessW + Job Object) | the interface and probes are embedded in `embedded.h` |
| C++ (C++17) | `versions/c++/src/server.cpp`, `engine.cpp`, `sys.cpp` (system + running), `json.hpp` | C++ port of `runner.c` in `sys.cpp` | same, `embedded.hpp` |
| Python 3.7+ | `versions/python/evaluator.py` (standard library only) | `subprocess` + `preexec_fn` (rlimit) + `os.wait4` / ctypes + Job Object | the reference implementation |
| Web (Node.js 16+) | `versions/web/server.js` (no npm packages) + `versions/web/public/index.html` | the `olieval-run` helper (built from `runner.c`, prebuilt in `versions/web/bin/`) | without the helper: fallback mode, no memory limit |

All four versions give the **same** statuses, scores, messages and notes on the example workspaces
(`tests/run_all.py`: the three folder layouts and the three build modes).

The shared files are synchronized with `python3 common/sync.py` (it copies `common/gui/index.html`,
the probes and `runner.c` into the versions and regenerates `embedded.h` / `embedded.hpp`).
