#!/usr/bin/env python3
"""Client de test pentru API-ul evaluatorului (orice variantă).

  python3 api_client.py --url http://127.0.0.1:8765 --ws ../exemplu [--problems suma,maxim] [--out rezumat.json]

Rulează scanarea + evaluarea și afișează un tabel compact; --out salvează un rezumat
(stări + punctaje) folosit pentru compararea variantelor între ele."""
import argparse
import json
import re
import sys
import time
import urllib.request


def http(url, method="GET", body=None, token=None, timeout=600):
    data = None if body is None else json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data, method=method)
    host = url.split("/")[2]
    req.add_header("Host", host)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("X-Eval-Token", token)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read()
            ct = r.headers.get("Content-Type", "")
    except urllib.error.HTTPError as e:
        raw = e.read()
        ct = e.headers.get("Content-Type", "")
        if "json" in ct:
            return json.loads(raw.decode("utf-8"))
        raise
    if "json" in ct:
        return json.loads(raw.decode("utf-8"))
    return raw.decode("utf-8")


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
    base = a.url.rstrip("/")
    html = http(base + "/")
    m = re.search(r'EVAL_TOKEN\s*=\s*"([0-9a-f]+)"', html)
    if not m:
        print("nu găsesc tokenul în pagină")
        sys.exit(1)
    tok = m.group(1)
    info = http(base + "/api/info", token=tok)
    print("variantă:", info.get("variant"), info.get("os"), "| compilatoare:", info.get("compilers"))
    import os
    # o cale Windows (ex. Z:\\tmp\\ws, pentru varianta Windows rulată prin Wine) se transmite neschimbată
    root = a.ws if re.match(r"^[A-Za-z]:\\", a.ws) else os.path.abspath(a.ws)
    sep = "\\" if re.match(r"^[A-Za-z]:\\", root) else "/"
    wsf = http(base + "/api/fs/read", "POST", {"path": root + sep + "evaluator.json"}, tok)
    ws = json.loads(wsf["text"])
    ws["root"] = root
    probs = [p for p in a.problems.split(",") if p] or [p["name"] for p in ws["problems"]]
    for p in probs:
        sc = http(base + "/api/scan", "POST", {"workspace": ws, "problem": p}, tok)
        if "error" in sc:
            print("scan", p, "EROARE", sc["error"])
            continue
        if not a.quiet:
            print("\n== scan %s: teste=%d (%s) verificator=%r" % (p, len(sc["tests"]), sc["testsDir"], sc.get("checker")))
            for s in sc["students"]:
                print("   %-8s exists=%-5s root=%-40s main=%-14s lang=%-4s size=%s/%s %s" % (
                    s["id"], s["exists"], s["root"][-40:], s["main"], s["lang"], s["sizeMain"], s["sizeAll"],
                    ("; ".join(s["warnings"]))[:80]))
    body = {"workspace": ws, "problems": probs}
    if a.students:
        body["students"] = a.students.split(",")
    if a.tests:
        body["tests"] = a.tests.split(",")
    r = http(base + "/api/eval/start", "POST", body, tok)
    if "error" in r:
        print("start EROARE", r["error"])
        sys.exit(1)
    t0 = time.time()
    logn = 0
    rv = -1
    results = {}
    while True:
        st = http(base + "/api/eval/status?log=%d&rv=%d" % (logn, rv), token=tok)
        logn = st["logNext"]
        if "results" in st:
            results = st["results"]
            rv = st["rv"]
        if not st["running"]:
            st = http(base + "/api/eval/status?log=%d&rv=-5" % logn, token=tok)
            results = st.get("results", results)
            break
        time.sleep(0.3)
    print("\nevaluare: %.1f s" % (time.time() - t0))
    summary = {}
    for p in probs:
        res = results.get(p)
        if not res:
            print("fără rezultate pentru", p)
            continue
        print("\n== %s (max %s) baseline=%s" % (p, res.get("maxScore"), res.get("baseline")))
        summary[p] = {}
        for s in res["students"]:
            cells = []
            for t in s.get("tests", []):
                cells.append("%s:%s=%s(%sms,%sKB,stk%s)" % (t["id"], t["status"], t["score"], int(t.get("timeMs") or 0),
                                                       t.get("memKb"), t.get("stackKb")))
            print(" %-8s %-6s %6s  %s" % (s["id"], s["status"], s["score"], " ".join(cells)))
            if s.get("notes"):
                print("          note:", " | ".join(s["notes"])[:200])
            if s["status"] == "CE":
                print("          compilare:", (s["compile"].get("log") or "")[:160].replace("\n", " "))
            summary[p][s["id"]] = {"status": s["status"], "score": s["score"],
                                   "tests": {t["id"]: [t["status"], t["score"]] for t in s.get("tests", [])}}
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(summary, f, ensure_ascii=False, indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
