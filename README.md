# Olympiad Evaluator

An evaluator for olympiad problems in **four versions with the same interface**: **C**, **C++**,
**Python** and **Web** (offline app). It compiles and runs the students' sources on the tests of each
problem, measures time, memory (RAM and stack) and source size, compares the output and computes the
score from your scoring scheme. Everything runs locally: the application starts a small server on
`127.0.0.1` and opens the interface in a browser window (Chrome/Edge, without an address bar).

The interface looks like the programs of 1995–2009, with two looks switched from the monitor button
in the title bar:

- **XP (2009)**, the default, like the OJI Evaluator: beige window, XP buttons, a red "Evaluate";
- **Classic (Windows 95/98)**: grey, raised buttons.

The interface language is chosen in the title bar (English and Romanian are included). Messages
coming from the engines are always English; the interface translates them (see
[Translations](#translations)).

## Folders

```
versions/c/           C version (C11): src/, bin/, build.sh, build.bat, Makefile, start.sh, start.bat, translations/
versions/c++/         C++ version (C++17): same layout
versions/python/      Python version: evaluator.py (+ copies of the interface and probes), start.*, translations/
versions/web/         Web version: server.js, public/, helper/ (olieval-run sources), bin/, package.json, start.*, translations/
common/               shared sources: gui/index.html (the interface), probe/, runner/, sync.py
master-translations/  the language files and the per-OS scripts that copy them into every version
tutorial/             step-by-step tutorial, example workspaces, make_examples.py, the course scripts
tests/                run_all.py (all versions on all examples), api_client.py, compare_summaries.py,
                      check_translations.py, gui_smoke.py
docs/SPECIFICATION.md technical specification: file formats, algorithms, HTTP API, translations
```

## Quick start

| Version | Windows | Linux / macOS | You need |
|---------|---------|---------------|----------|
| **C**   | double-click `versions\c\start.bat` (or `versions\c\bin\evaluator-c.exe`) | `versions/c/start.sh` | nothing else |
| **C++** | double-click `versions\c++\start.bat` (or `versions\c++\bin\evaluator-cpp.exe`) | `versions/c++/start.sh` | nothing else |
| **Python** | double-click `versions\python\start.bat` | `versions/python/start.sh` | Python 3.7 or newer (standard library only) |
| **Web** | double-click `versions\web\start.bat` | `versions/web/start.sh` (or `npm start` in `versions/web`) | Node.js 16 or newer (no npm packages) |

From a Windows console, run the scripts as `.\start.bat` (a plain `start` is the built-in command of
the console). The executables in `versions/c/bin` and `versions/c++/bin` are prebuilt for
**Windows x64** and **Linux x64** (statically linked, they run on any distribution). On macOS (or to
rebuild them) see [Building from source](#building-from-source).

To evaluate C/C++ sources, `gcc`/`g++` must be installed. On Windows the evaluator looks in `PATH`
and in the usual places: MinGW from Code::Blocks (including `C:\OJI\Codeblocks\MinGW`), `C:\MinGW`,
`C:\mingw64`, MSYS2, TDM-GCC, Dev-C++. For Python sources: `python`/`python3`.

Command-line options (all versions):

```
--port 8765      the port (default 8765; when it is busy the next free one is used)
--no-browser     do not open the window; open the address printed in the console yourself
--gui FILE       (C, C++) use another index.html than the embedded one
--no-helper      (Web) run without the olieval-run helper (fallback mode, see below)
```

Close the application with the red button in the title bar of the interface or by closing the
console window.

## Tutorial and examples

[tutorial/README.md](tutorial/README.md) walks through a whole contest, step by step. On the first
start choose **Open the example**: a contest with 4 problems and 16 students that covers every case
(correct, wrong, partial, time limit, memory limit, runtime error, stack overflow, no output file,
compilation error, source too large, CLion/Code::Blocks projects in subfolders, multi-file projects,
the course scripts `compile.sh`/`build.sh`). Press **Evaluate problem** and click any cell of the
table for details.

| problem   | what it shows |
|-----------|---------------|
| `sum`     | `sum.in`/`sum.out` files, every verdict, a source size limit with a penalty |
| `max`     | console (stdin/stdout), an OJI-style external checker (`verif.cpp`), partial scores, `teste.txt` |
| `average` | files inside a project folder (`data/average.in`), real numbers compared with a tolerance |
| `digits`  | multi-file projects built "like the course scripts" (objects in `build/`, executable `release/program`) |

`tutorial/examples/` also holds the same students in the two other folder layouts
(`student-problem`, `oji-flat`). Regenerate everything with `python3 tutorial/make_examples.py`.

## How you work

1. **Workspace**: choose the contest folder. `evaluator.json` (the whole configuration and the
   scoring scheme) is saved there. Choose the folder layout and press **Create the folders** to
   prepare the structure for every student of the list (paste the list as text or take it from the
   folders).
2. **Problem**: the name (it appears in file names: `sum.in`), **the limits of the problem** (time
   in ms, total memory, stack, size on disk measured on the main source file or on the whole
   project), how the program gets its data, how the project is built and how the student's project
   is found inside the student folder. The Workspace tab shows the limits of all problems in one table.
3. **Tests and scoring**: put the tests in the tests folder of the problem (e.g. `0-sum.in` +
   `0-sum.ok`) and press **Detect tests**. Each test has points for correctness, points for time and
   points for memory; a test can also get its own time or memory limit (optional). Set what happens
   when a limit is exceeded and the comparison mode. **Import/Export scoring**.
4. **Evaluation**: **Evaluate problem**, **Evaluate selection** (the ticked students) or **All
   problems**. The table shows the score and verdict of every student on every test; a click on a
   cell opens the input, the correct output and the program's output (with the differing line
   marked), the time, memory, stack, exit code and error messages.
5. **Ranking and reports**: the ranking over all problems and the exports: a printable report
   (HTML), CSV for Excel, **OJI-style TXT score sheets** (one file per student plus `All_Results`),
   JSON results.

Changing the scoring scheme (points, limits, policies) recomputes the scores at once, without running
the programs again. Everything is saved automatically.

## Folder layouts

| layout | student sources | tests |
|--------|-----------------|-------|
| problem, then students (default) | `{root}/{problem}/{student}/…` | `{root}/{problem}/_tests` |
| student, then problems | `{root}/{student}/{problem}/…` | `{root}/_tests/{problem}` |
| shared folder (OJI style) | `{root}/{problem}/sources/{student}{nr}.{ext}` (e.g. `BN_C_152.cpp` = student `BN_C_15`, problem 2) | `{root}/{problem}/tests` |
| custom | any template with `{root}` `{problem}` `{nr}` `{student}` | anywhere |

There is **one tests folder per problem**, shared by all students, and it can live anywhere (the
"Tests folder" field of the problem). The file name patterns are configurable (`{n}-{problem}.in` /
`{n}-{problem}.ok` by default). When the folder has a `teste.txt` (the OJI format: "number points"
on each line), the points are taken from it.

**Import from the OJI Evaluator** reads the old evaluator's folder (`rounds.xml`, `probleme/…/teste`,
`teste.txt`, sources in `surse_concurenti`) and creates the equivalent workspace.

## How the program gets its data

- **Local files (olympiad)**: the program reads `problem.in` and writes `problem.out` in the current
  folder (the names can change, e.g. `data.in` / `data.out`).
- **Console**: the input comes on stdin, the output is taken from stdout.
- **Project folder**: the files are in a folder relative to the root of the student's project
  (e.g. `data/{problem}.in`); the program's current folder can be chosen too (e.g. `cmake-build-debug`).

By default every test runs in an empty temporary folder that mirrors the relative layout (a program
cannot read another student's tests by accident); optionally it runs directly in the student's project.

## Finding the student's project

Students hand in whole projects (CLion, Code::Blocks, VS Code, folders with the course scripts). The
root rules are tried in order; the first match sets the project root, optionally moving up a few
levels. Default: `CMakeLists.txt`, `*.cbp`, `bash-scripts/compile.sh`, `bash-scripts/build.sh`,
`compile.sh`, `build.sh`, `main.cpp`, `main.c`, `*.cpp`, `*.c`, `*.py`. The ignored folders (default
`.* _* cmake-build-* CMakeFiles build bin obj out release debug __MACOSX node_modules venv __pycache__`)
and the main file (`main.cpp main.c {problem}.cpp …`) are configurable the same way.

## Building the students' projects

Three modes, per problem:

| mode | what it does |
|------|--------------|
| **A single command** | `gcc`/`g++` with the options from Settings, on the main file or on all sources of the project |
| **Like the course scripts** | `compile.sh` + `build.sh` redone inside the evaluator: each `.c`/`.cpp` becomes an object, then linked with `g++` when there is any `.cpp` (otherwise `gcc`); works on Windows too |
| **The student's scripts** | runs `bash-scripts/compile.sh && bash-scripts/build.sh` from the project (or `compile.sh`/`build.sh` in the root, the old layout) and tests `release/program` |

**Libraries are added automatically** from `#include`, like in `build.sh` (`check_library`): `math.h` →
`-lm`, `pthread.h` → `-pthread`, `ncurses.h`, SDL2, raylib, OpenGL/GLFW/GLEW, `wx/*` →
`$(wx-config --cxxflags --libs)`, zlib, sqlite3, curl, OpenSSL, GMP and more. The rules are editable in
Settings (the options may contain `$(…)` commands). The compile and run commands of every language
are edited there too (variables: `{sources}` `{exe}` `{libs}` `{main}` `{stack}` `{problem}`).

## What is measured

- **Time**: processor time (or wall time, as you choose) on every test. The program is stopped at
  the hard limit (2 × the limit by default).
- **Memory**: peak RAM. By default the memory of an empty program is subtracted (calibration, like
  OJI), so only what the student uses counts.
- **Stack**: the stack used (through the measuring probe compiled with the source; it does not count
  towards the size).
- **Size on disk**, per problem: the main source file only, or the whole project (every `.c .h .cpp .hpp …`
  file outside the ignored folders).

The limits are enforced while running: on Linux through `setrlimit` (time, memory, stack, output
size), on Windows through a Job Object (memory, time, one process) and `-Wl,--stack=…` when compiling.
On macOS the system does not enforce the memory limit; memory is measured and an overrun is scored
the same way.

## Scoring

For every test: the correctness points (or the score given by the checker, or a fraction), plus the
points for time and for memory when the test stayed within them. When the time or memory limit is
exceeded you choose: **0 points**, **a percentage**, **a linear decrease** up to the hard limit or
**report only**. When the source size is exceeded: 0, a percentage, a fixed penalty or report only. A
problem can also give points for compiling and for staying within the size.

Verdicts: OK, Partial, Wrong answer, Time limit exceeded, Memory limit exceeded, Runtime error (with
the reason: division by zero, invalid access, stack overflow…), No output file, Compilation error,
No source, Checker error.

Comparison: by lines (ignores spaces at the end of lines and empty lines at the end), by words,
exact, real numbers with a tolerance, or an **external checker** (`verif.cpp`/`verif.c`/`verif.py`/an
executable, compiled automatically): the OJI protocol (files `problem.in/.ok/.out`, prints "score
message"), a fraction (0…1) or the exit code.

## Files

```
contest/
  evaluator.json                  the configuration and the scoring scheme
  sum/_tests/0-sum.in, 0-sum.ok, …, teste.txt, verif.cpp
  sum/ana/…                       the student's project
  _results/sum/latest.json        the results (plus a dated copy after every evaluation)
  _results/report-….html, ranking-….csv, score-sheets/…   the exports
  _work/                          builds, runs, kept outputs (_work/outputs/sum/ana/0.out)
```

An exported scoring scheme (`<problem>.scoring.json`) holds one problem and can be imported into
another workspace. Import also accepts `evaluator.json` (you pick the problem), `teste.txt` and
`rounds.xml` (OJI). The application settings (last workspace, look, language) are in
`~/.olympiad-evaluator/settings.json`.

## Translations

- `master-translations/` holds one file per language (`en.json`, `ro.json`) and `messages.txt` (the
  sentences of the `.bat`/`.sh` scripts, always English). After editing them, run the script for your
  system there (`install-windows.bat`, `install-linux.sh`, `install-macos.command`): it copies them
  into the `translations/` folder of every version. Restart the evaluator to load the changes.
- A language file maps the English interface texts (`ui`) and the English engine messages
  (`messages`, templates with `%{name}` placeholders) to the language. See
  [master-translations/README.md](master-translations/README.md) to add a language and
  `python3 tests/check_translations.py` to find missing texts.

## Building from source

```
versions/c/build.sh            # Linux/macOS: bin/evaluator-c          (or: make)
versions/c/build.sh static     # statically linked (any Linux distribution)
versions/c/build.sh windows    # from Linux with MinGW-w64: bin/evaluator-c.exe   (or: make windows)
versions\c\build.bat           # on Windows, with gcc (MinGW-w64) in PATH
```

The same for `versions/c++/` (`build.sh`, `build.bat`, `Makefile`; needs C++17). On Windows use a
MinGW-w64 (Code::Blocks 20.03 or newer, MSYS2, w64devkit). After changing the interface
(`common/gui/index.html`) or anything else in `common/`, run `python3 common/sync.py` first: it copies
the shared files into all versions and regenerates the embedded headers for C and C++.

The Web version uses the `olieval-run` helper (from `common/runner/`) for limits and exact
measurements. It is prebuilt in `versions/web/bin/` for Windows and Linux; otherwise the Web version
compiles it on its first start when it finds `gcc`. Without it, it runs in **fallback mode** (no
memory limit, processor time only through the probe).

## Testing

`python3 tests/run_all.py` runs the four versions on fresh copies of the examples (all three folder
layouts and all three build modes), checks that they give **exactly the same** statuses, scores and
messages, and checks the translation files. See [tests/README.md](tests/README.md).

## Safety

The server listens only on `127.0.0.1`, with a new access code at every start, so other web sites or
other computers cannot use it. The students' programs run with time and memory limits, but **with
your user's rights** (like the OJI Evaluator): they are not isolated from your other files. Evaluate
unknown sources in a separate account or in a virtual machine.
