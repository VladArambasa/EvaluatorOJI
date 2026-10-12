# Tutorial

This tutorial takes you from the first start to a finished contest with a ranking and score sheets.
It uses the example workspaces in `tutorial/examples/`; every step works the same in the C, C++,
Python and Web versions, because they share one interface.

```
tutorial/
  README.md            this tutorial
  make_examples.py     rebuilds the examples (python3 tutorial/make_examples.py)
  examples/contest/    4 problems, 16 students, every verdict (layout: problem, then students)
  examples/student-problem/   sum + max with the layout: student, then problems
  examples/oji-flat/          sum + max with one shared sources folder per problem (OJI style)
  course-scripts/      the course scripts copied into the example projects
                       (bash-scripts/: compile.sh, build.sh, cbr.sh, run.sh, copycbp.sh; old/: the older layout)
```

## 1. Start the evaluator

Pick any version (the results are identical):

| | Windows | Linux / macOS |
|-|---------|---------------|
| C | double-click `versions\c\start.bat` | `versions/c/start.sh` |
| C++ | double-click `versions\c++\start.bat` | `versions/c++/start.sh` |
| Python | double-click `versions\python\start.bat` | `versions/python/start.sh` |
| Web | double-click `versions\web\start.bat` | `versions/web/start.sh` |

A console window opens and the interface appears in a browser window. Keep the console open while
you work; closing it (or the red button in the title bar of the interface) stops the evaluator.

To compile C/C++ sources the evaluator needs `gcc`/`g++` (on Windows: the MinGW of Code::Blocks,
MSYS2, TDM-GCC…; it is searched for in the usual places). **Settings → Compilers and languages**
shows what was found.

## 2. Choose the language and the look

The title bar holds the **language selector** (English, Română) and the **monitor button**, which
switches between the XP look (2009) and the Classic look (Windows 95/98). Both choices are remembered.

## 3. Open the example

On the start screen press **Open the example**. The `contest` workspace opens on the **Evaluation**
tab, with problem `sum` selected in the problem list on the left.

| problem   | what it shows |
|-----------|---------------|
| `sum`     | `sum.in`/`sum.out` files, every verdict, a 10 KB source limit with a 50% penalty |
| `max`     | console input/output, an OJI-style checker (`verif.cpp`), partial scores, `teste.txt` |
| `average` | files inside the project (`data/average.in`), real numbers compared with a tolerance |
| `digits`  | multi-file projects built like the course scripts (`compile.sh` + `build.sh`) |

## 4. Evaluate

1. Press **Detect**: the evaluator finds every student folder, the project root inside it, the main
   file and the language. A ⚠ next to a name means something worth a look (hover over it).
2. Press **Evaluate problem**. The progress bar fills; the table fills in as each student finishes.
3. Read the table: one row per student, one column per test. Each cell shows the points and the
   verdict (OK, PAR, WA, TLE, MLE, RE, NOUT, CHK); the legend is under the table. The last column is
   the total.

In the example, for `sum`:

| student | what happens |
|---------|--------------|
| ana, bogdan, ioana, kira, mihai, oana | correct (bogdan: CLion project in a subfolder; ioana: several files + `math.h`; kira: Python; mihai: CRLF and trailing spaces; oana: Code::Blocks project) |
| cristi | an `int` sum overflows on the big tests → WA |
| dana   | quadratic algorithm → TLE on the big tests |
| emil   | division by zero on one test → RE |
| flavia | compilation error → CE |
| george | uses about 100 MB with a 64 MB limit → MLE |
| horia  | reads the keyboard instead of `sum.in` → no output file |
| jianu  | deep recursion → stack overflow (RE) on the big tests |
| lia    | correct but the source is over 10 KB → score halved |
| nicu   | no source at all |
| petra  | tries to allocate 2 GB → MLE |

4. Click a cell, e.g. **cristi / test 2**. The window shows the verdict and message, CPU and wall
   time, memory, stack, exit code, and the input, the correct output and the program's output side by
   side, with the first differing line marked. **Re-evaluate this test** runs only that test again.
5. Click **yes** / **error** in the *Compilation* column to see the exact command and the compiler
   messages (flavia's error is there).
6. Click a student's name for the project details: student folder, project root, main file, compiled
   sources, size, notes and the source itself.
7. Open **Log** under the table to read what the engine did, line by line.

Select **max** in the problem list and evaluate it too: bogdan gets partial scores from the checker
(he prints the right maximum but a wrong count), cristi fails only the test with negative numbers.

## 5. Set the limits of a problem

Every problem has its own limits. Open the **Problem** tab: right under the name, **Limits of
problem sum** holds

- **Time** in ms (processor time, or real time if you choose so);
- **Total memory** in MB and **Stack** in MB;
- **Size on disk** in KB, measured on **the main source file** or on **the whole project** (all its
  source files; the extensions counted can be changed, the ignored folders are never counted);
- the hard limits (when a program is stopped), the maximum output and the memory calibration.

The **Workspace** tab lists every problem with its time, memory, stack and size limits in one table,
so a whole contest can be set up at once. The Evaluation tab shows the limits of the current problem
above the table. Changing a limit recomputes the scores at once from the measured time, memory and
size; a program that was stopped at the old hard limit is not run again by itself, so after raising
a time or memory limit evaluate again.

On **Tests and scoring**, tick **Some tests have their own time or memory limit** only when a test
needs a different limit than its problem; two more columns then appear in the tests table (empty =
the limit of the problem).

## 6. Change the scoring scheme

Open **Tests and scoring**:

- Change the points of a test, or press **Split 100 points equally**. Back on **Evaluation**, the
  scores are already recomputed: changing the scoring scheme never runs the programs again.
- In **What happens when a limit is exceeded**, set *Time limit exceeded* to *percentage of the
  points* and keep 50%: a test that finishes over the time limit (but before the hard limit,
  2 × the limit) now keeps half of its points. A program stopped at the hard limit still gets 0.
- **Export scoring…** saves the scheme as `sum.scoring.json`; **Import scoring…** loads a scheme,
  an `evaluator.json`, an OJI `teste.txt` or `rounds.xml`.

The **Problem** tab also holds how the program gets its data (local files, console, a folder inside
the project), how the project is built and the rules that find the project root. **Check the
detection** shows what the rules find for every student.

## 7. Ranking and reports

Open **Ranking and reports** (evaluate all problems first with **All problems** on the Evaluation tab):

- **Full report (printable)**: an HTML page with the ranking, the problems and one score sheet per
  student; it opens in a new window and is saved in `_results/`.
- **Export the ranking (CSV for Excel)**.
- **TXT score sheets (OJI style)**: `score-sheets/<student>-res.txt` and `All_Results-res.txt`.
- **Export the results (JSON)**.

## 8. Your own contest

1. Make an empty folder, e.g. `C:\Contests\Club-October`.
2. In the title bar press the folder button (or **Open a workspace** on the start screen), choose
   the folder and accept creating a new workspace.
3. **Workspace** tab: give it a name, choose the **Folder layout**:
   - *Problem, then students*: `sum/ana/…`, tests in `sum/_tests/`;
   - *Student, then problems*: `ana/sum/…`, tests in `_tests/sum/`;
   - *Shared folder (OJI style)*: `sum/sources/ana1.cpp`, tests in `sum/tests/`;
   - *Custom*: any template that contains `{student}`.
4. Add the students: **Add from text…** accepts one `id; name; group` per line, the OJI
   `contestants.txt` format, or a column pasted from Excel. The id is the folder name.
5. Add the problems with **Add a problem** (the name is used in file names, e.g. `sum.in`).
6. Press **Create the folders**: one tests folder per problem and one folder per student.
7. Copy the tests into each tests folder as `0-sum.in` / `0-sum.ok`, `1-sum.in` / `1-sum.ok`… and
   press **Detect tests** on the **Tests and scoring** tab. Points come from `teste.txt` when it
   exists, otherwise 10 per test.
8. Copy each student's project into the student's folder. Whole IDE projects are fine (CLion,
   Code::Blocks, VS Code): the root rules find the real project inside.
9. Set the limits of each problem (the table on the **Workspace** tab, or the **Problem** tab), the
   input/output mode (**Problem** tab) and the comparison (**Tests and scoring**), then evaluate.

Everything is saved automatically in `evaluator.json`; **Ctrl+S** saves at once.

## 9. Projects built with the course scripts

The students of the course build their projects with `bash-scripts/compile.sh` (every `.c`/`.cpp`
into an object in `build/`) and `bash-scripts/build.sh` (links `release/program` and adds libraries
such as `-lm` from the `#include` lines). On the **Problem** tab, *Building the project* offers:

- **A single command**: one `gcc`/`g++` command (Settings → Compilers and languages);
- **Like the course scripts**: the same steps redone inside the evaluator, so it also works on
  Windows and the stack can be measured (problem `digits` of the example uses it);
- **The student's scripts**: runs the student's own `compile.sh` and `build.sh` (needs bash) and
  tests `release/program`.

## 10. Coming from the OJI Evaluator

**Import from the OJI Evaluator…** (start screen or Workspace tab) reads the old evaluator's folder:
`rounds.xml` (the limits of each problem; a time written in seconds, such as `0.1`, becomes 100 ms),
`probleme/<problem>/teste` (tests and `teste.txt`), the `verif` checker,
`contestants.txt` and the sources in `surse_concurenti`, and creates the same contest as a workspace
with the shared-folder layout. The TXT score sheets have the familiar `-res.txt` format.

## 11. Translations

The interface language is chosen in the title bar. To change a text, or to add a language, edit the
files in `master-translations/` and run the install script for your system there; see
[master-translations/README.md](../master-translations/README.md).

## 12. When something goes wrong

| symptom | what to do |
|---------|------------|
| every C/C++ student gets CE with "Program not found: gcc" | install a MinGW-w64 / gcc, or set the compile command in Settings to its full path |
| the interface says the application is not responding | the console window was closed; start the version again |
| memory shows "—" | the program was killed or crashed before the probe could report; the time and verdict are still exact |
| a student's project is not found | open **Problem → Check the detection**; add a root rule or raise the maximum depth |
| in a Windows console, `start.bat` does not start the evaluator | type `.\start.bat` (or double-click the file) |
| an engine message is still English in another language | add its template to the language file (`messages`), see the master-translations README |
