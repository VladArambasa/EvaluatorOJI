#!/usr/bin/env python3
# RUNS EVERY VERSION ON EVERY EXAMPLE AND COMPARES THE RESULTS WITH THE FIRST VERSION (PYTHON)
#   PYTHON3 TESTS/RUN_ALL.PY [--VERSIONS PYTHON,WEB,C,C++] [--SERIAL] [--KEEP DIR]
# STEPS: TRANSLATION FILES CHECK, FRESH EXAMPLES FOR EACH VERSION, START THE SERVERS,
# EVALUATE EVERY CASE, COMPARE STATUSES / SCORES / MESSAGES, CHECK THE MESSAGE TRANSLATIONS, STOP
# EXIT CODE 1 WHEN ANYTHING DIFFERS OR FAILS
import glob
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import api_client  # NOQA: E402
import check_translations  # NOQA: E402
import compare_summaries  # NOQA: E402

IS_WIN = os.name == "nt"
EXE = ".exe" if IS_WIN else ""
COMMANDS = {
    "python": [sys.executable, os.path.join(ROOT, "versions", "python", "evaluator.py")],
    "web": ["node", os.path.join(ROOT, "versions", "web", "server.js")],
    "c": [os.path.join(ROOT, "versions", "c", "bin", "evaluator-c" + EXE)],
    "c++": [os.path.join(ROOT, "versions", "c++", "bin", "evaluator-cpp" + EXE)],
}


def scripts_mode(ws):
    # DIGITS BUILT WITH THE STUDENTS' OWN COMPILE.SH + BUILD.SH (NEEDS BASH)
    for p in ws["problems"]:
        if p["name"] == "digits":
            p["project"]["buildMode"] = "scripts"
    return ws


# (CASE NAME, EXAMPLE FOLDER, PROBLEMS OR NONE FOR ALL, WORKSPACE CHANGE)
CASES = [("contest", "contest", None, None),
         ("student-problem", "student-problem", None, None),
         ("oji-flat", "oji-flat", None, None)]
if not IS_WIN:
    CASES.append(("scripts-mode", "contest", ["digits"], scripts_mode))


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def start_server(name, workdir):
    cmd = COMMANDS[name]
    if name in ("c", "c++") and not os.path.isfile(cmd[0]):
        # BUILD THE MISSING EXECUTABLE WITH THE VERSION'S OWN SCRIPT
        d = os.path.join(ROOT, "versions", name)
        subprocess.run(["cmd", "/c", "build.bat", "nopause"] if IS_WIN else ["sh", "build.sh"], cwd=d, check=True)
    port = free_port()
    log = open(os.path.join(workdir, "server-%s.log" % name), "w")
    proc = subprocess.Popen(cmd + ["--port", str(port), "--no-browser"], cwd=workdir, stdout=log, stderr=subprocess.STDOUT)
    url = "http://127.0.0.1:%d" % port
    for _ in range(200):
        time.sleep(0.1)
        try:
            urllib.request.urlopen(url + "/", timeout=2).read()
            return proc, url
        except OSError:
            if proc.poll() is not None:
                break
    proc.kill()
    raise RuntimeError("%s did not start (see %s)" % (name, log.name))


def run_version(name, base, report):
    # EVALUATES EVERY CASE ON ITS OWN COPY OF THE EXAMPLES
    ex = os.path.join(base, name)
    subprocess.run([sys.executable, os.path.join(ROOT, "tutorial", "make_examples.py"), ex], check=True, stdout=subprocess.DEVNULL)
    proc, url = start_server(name, base)
    try:
        c = api_client.Client(url)
        for case, folder, probs, change in CASES:
            ws = c.load_ws(os.path.join(ex, folder))
            if change:
                ws = change(ws)
            results, log, secs = c.evaluate(ws, probs)
            report[(name, case)] = (api_client.summarize(results, ws["root"]), secs, log)
    finally:
        proc.terminate()
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()


def main():
    args = sys.argv[1:]
    names = args[args.index("--versions") + 1].split(",") if "--versions" in args else ["python", "web", "c", "c++"]
    keep = args[args.index("--keep") + 1] if "--keep" in args else ""
    base = os.path.abspath(keep) if keep else tempfile.mkdtemp(prefix="olympiad-tests-")
    os.makedirs(base, exist_ok=True)
    problems = 0
    print("== translation files")
    problems += check_translations.check()
    report, errors = {}, {}

    def worker(n):
        try:
            run_version(n, base, report)
        except Exception as e:  # REPORTED BELOW
            errors[n] = e

    threads = [threading.Thread(target=worker, args=(n,)) for n in names]
    for th in threads:
        th.start()
        if "--serial" in args:
            th.join()
    for th in threads:
        th.join()
    for n, e in errors.items():
        print("== %s FAILED: %s" % (n, e))
        problems += 1
    ref = names[0]
    for case, _, _, _ in CASES:
        if (ref, case) not in report:
            continue
        print("== %s (reference: %s, %.1f s)" % (case, ref, report[(ref, case)][1]))
        for n in names[1:]:
            if (n, case) not in report:
                continue
            lines = []
            d = compare_summaries.compare(report[(ref, case)][0], report[(n, case)][0], out=lines.append)
            print("   %-7s %s (%.1f s)" % (n, "identical" if not d else "%d difference(s)" % d, report[(n, case)][1]))
            for ln in lines[:20]:
                print("      " + ln)
            problems += d
    print("== message translations (%s results and log)" % ref)
    files = glob.glob(os.path.join(base, ref, "*", "_results", "*", "latest.json"))
    # LOG LINES WITHOUT STACK TRACES (THOSE FOLLOW AN ERROR LINE AND ARE NOT TRANSLATED)
    log = [ln for k, v in report.items() if k[0] == ref for ln in v[2] if not ln.startswith((" File", "Traceback"))]
    for code in [f[:-5] for f in sorted(os.listdir(os.path.join(ROOT, "master-translations"))) if f.endswith(".json") and f != "en.json"]:
        if files:
            problems += check_translations.check_results(files, code)
        problems += check_translations.check_lines(log, code, "log")
    if not keep:
        shutil.rmtree(base, ignore_errors=True)
    else:
        with open(os.path.join(base, "summaries.json"), "w", encoding="utf-8") as f:
            json.dump({"%s/%s" % k: v[0] for k, v in report.items()}, f, ensure_ascii=False, indent=1)
    print("ALL OK" if not problems else "%d PROBLEM(S)" % problems)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
