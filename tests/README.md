# Tests

| script | what it does |
|--------|--------------|
| `run_all.py` | builds fresh examples for every version, starts the four servers, evaluates every case (the three folder layouts and the three build modes), compares statuses, scores and messages with the Python version, checks the translation files and the message coverage of the results and of the log |
| `api_client.py` | evaluates one workspace through the HTTP API of a running version and prints a table; `--out` writes a summary |
| `compare_summaries.py` | compares two summaries written by `api_client.py` |
| `check_translations.py` | checks the language files against the interface (see `master-translations/README.md`) |
| `gui_smoke.py` | drives the interface in Chromium with Playwright: opens the example, evaluates, visits every tab and saves screenshots; in a non-English language it lists visible texts that are still English |

## Run everything

```
python3 tests/run_all.py                         # all four versions
python3 tests/run_all.py --versions python,c     # only some (the first one is the reference)
python3 tests/run_all.py --keep /tmp/olympiad    # keep the examples, results and summaries
```

It needs `gcc`/`g++`, Node.js and bash (the build mode that runs the students' own scripts); the C
and C++ executables are built with their `build.sh` when they are missing. The output ends with
`ALL OK`, or lists every difference.

## One version by hand

```
versions/python/start.sh --port 8790 --no-browser &
python3 tests/api_client.py --url http://127.0.0.1:8790 --ws /tmp/copy/contest --out py.json
python3 tests/compare_summaries.py py.json c.json
```

Evaluate a **copy** of an example: evaluation writes `_work/` and `_results/` into the workspace.

## Interface screenshots

```
pip install playwright        # Chromium must be available to Playwright
python3 tests/gui_smoke.py http://127.0.0.1:8790/ /tmp/shots --lang ro --theme classic
```
