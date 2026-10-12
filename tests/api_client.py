#!/usr/bin/env python3
# TEST CLIENT FOR THE EVALUATOR API (ANY VERSION)
#   PYTHON3 API_CLIENT.PY --URL HTTP://127.0.0.1:8765 --WS ../TUTORIAL/EXAMPLES/CONTEST [--PROBLEMS SUM,MAX] [--OUT SUMMARY.JSON]
# SCANS, EVALUATES AND PRINTS A COMPACT TABLE; --OUT SAVES A SUMMARY (STATUSES, SCORES, MESSAGES)
# THAT COMPARE_SUMMARIES.PY USES TO CHECK THE VERSIONS AGAINST EACH OTHER
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request


def http(url, method="GET", body=None, token=None, timeout=600):
    data = None if body is None else json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Host", url.split("/")[2])
    if data is not None:
        req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("X-Eval-Token", token)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw, ct = r.read(), r.headers.get("Content-Type", "")
    except urllib.error.HTTPError as e:
        raw, ct = e.read(), e.headers.get("Content-Type", "")
        if "json" not in ct:
            raise
    if "json" in ct:
        return json.loads(raw.decode("utf-8"))
    return raw.decode("utf-8")


class Client:
    def __init__(self, url):
        self.base = url.rstrip("/")
        html = http(self.base + "/")
        m = re.search(r'EVAL_TOKEN\s*=\s*"([0-9a-f]+)"', html)
        if not m:
            raise RuntimeError("no token in the page served by " + url)
        self.tok = m.group(1)
        self.info = self.get("/api/info")

    def get(self, path):
        return http(self.base + path, token=self.tok)

    def post(self, path, body):
        return http(self.base + path, "POST", body, self.tok)

    def load_ws(self, root):
        # A WINDOWS PATH (E.G. Z:\TMP\WS FOR THE WINDOWS BUILD UNDER WINE) IS PASSED AS IT IS
        win = re.match(r"^[A-Za-z]:\\", root)
        root = root if win else os.path.abspath(root)
        r = self.post("/api/fs/read", {"path": root + ("\\" if win else "/") + "evaluator.json"})
        if "error" in r:
            raise RuntimeError(r["error"])
        ws = json.loads(r["text"].lstrip("\ufeff"))
        ws["root"] = root
        return ws

    def evaluate(self, ws, problems=None, students=None, tests=None):
        # STARTS AN EVALUATION AND WAITS FOR IT; RETURNS (RESULTS BY PROBLEM, LOG LINES, SECONDS)
        body = {"workspace": ws, "problems": problems or [p["name"] for p in ws["problems"]]}
        if students:
            body["students"] = students
        if tests:
            body["tests"] = tests
        r = self.post("/api/eval/start", body)
        if "error" in r:
            raise RuntimeError(r["error"])
        t0, logn, rv, results, log = time.time(), 0, -1, {}, []
        while True:
            st = self.get("/api/eval/status?log=%d&rv=%d" % (logn, rv))
            log += st.get("log", [])
            logn = st["logNext"]
            if "results" in st:
                results, rv = st["results"], st["rv"]
            if not st["running"]:
                st = self.get("/api/eval/status?log=%d&rv=-5" % logn)
                log += st.get("log", [])
                results = st.get("results", results)
                break
            time.sleep(0.3)
        return results, log, time.time() - t0


def summarize(results, root):
    # STATUSES, SCORES AND MESSAGES; THE WORKSPACE PATH BECOMES ROOT, MEASURED NUMBERS BECOME #
    def norm(s, measured=False):
        s = str(s or "").replace(root, "ROOT").replace("\\", "/")
        return re.sub(r"\d+", "#", s) if measured else s
    out = {}
    for p, res in sorted(results.items()):
        out[p] = {}
        for s in res.get("students", []):
            out[p][s["id"]] = {
                "status": s["status"], "score": s["score"],
                "notes": [norm(n) for n in s.get("notes", [])],
                "tests": {t["id"]: [t["status"], t["score"], norm(t.get("msg"), t["status"] in ("TLE", "MLE")), norm(t.get("detail"))]
                          for t in s.get("tests", [])}}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8765")
    ap.add_argument("--ws", required=True)
    ap.add_argument("--problems", default="")
    ap.add_argument("--students", default="")
    ap.add_argument("--tests", default="")
    ap.add_argument("--out", default="")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    c = Client(a.url)
    print("version:", c.info.get("variant"), c.info.get("os"), "| compilers:", c.info.get("compilers"))
    ws = c.load_ws(a.ws)
    probs = [p for p in a.problems.split(",") if p] or [p["name"] for p in ws["problems"]]
    if not a.quiet:
        for p in probs:
            sc = c.post("/api/scan", {"workspace": ws, "problem": p})
            if "error" in sc:
                print("scan", p, "ERROR", sc["error"])
                continue
            print("\n== scan %s: tests=%d (%s) checker=%r" % (p, len(sc["tests"]), sc["testsDir"], sc.get("checker")))
            for s in sc["students"]:
                print("   %-8s exists=%-5s root=%-40s main=%-14s lang=%-4s size=%s/%s %s" % (
                    s["id"], s["exists"], s["root"][-40:], s["main"], s["lang"], s["sizeMain"], s["sizeAll"],
                    ("; ".join(s["warnings"]))[:80]))
    results, _, secs = c.evaluate(ws, probs, [x for x in a.students.split(",") if x] or None, [x for x in a.tests.split(",") if x] or None)
    print("\nevaluation: %.1f s" % secs)
    for p in probs:
        res = results.get(p)
        if not res:
            print("no results for", p)
            continue
        print("\n== %s (max %s) baseline=%s" % (p, res.get("maxScore"), res.get("baseline")))
        for s in res["students"]:
            cells = ["%s:%s=%s(%sms,%sKB,stk%s)" % (t["id"], t["status"], t["score"], int(t.get("timeMs") or 0), t.get("memKb"), t.get("stackKb"))
                     for t in s.get("tests", [])]
            print(" %-8s %-6s %6s  %s" % (s["id"], s["status"], s["score"], " ".join(cells)))
            if s.get("notes"):
                print("          notes:", " | ".join(s["notes"])[:200])
            if s["status"] == "CE":
                print("          compile:", (s["compile"].get("log") or "")[:160].replace("\n", " "))
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(summarize(results, ws["root"]), f, ensure_ascii=False, indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
