#!/usr/bin/env python3
# COMPARES TWO SUMMARIES WRITTEN BY API_CLIENT.PY (STATUSES, SCORES, MESSAGES)
#   PYTHON3 COMPARE_SUMMARIES.PY REFERENCE.JSON OTHER.JSON [--SCORES-ONLY]
# PRINTS EVERY DIFFERENCE AND "IDENTICAL" WHEN THERE IS NONE; EXIT CODE 1 ON DIFFERENCES
import json
import sys


def compare(a, b, scores_only=False, out=print):
    diff = 0
    for p in sorted(set(a) | set(b)):
        for s in sorted(set(a.get(p, {})) | set(b.get(p, {}))):
            x, y = a.get(p, {}).get(s), b.get(p, {}).get(s)
            if x is None or y is None:
                out("MISSING %s %s (first: %s, second: %s)" % (p, s, x is not None, y is not None))
                diff += 1
                continue
            if x["status"] != y["status"] or abs(float(x["score"]) - float(y["score"])) > 1e-9:
                out("DIFFERENT %s %s: %s %s vs %s %s" % (p, s, x["status"], x["score"], y["status"], y["score"]))
                diff += 1
            if not scores_only and x.get("notes") != y.get("notes"):
                out("   notes %s %s: %r vs %r" % (p, s, x.get("notes"), y.get("notes")))
                diff += 1
            for t in sorted(set(x["tests"]) | set(y["tests"])):
                tx, ty = x["tests"].get(t), y["tests"].get(t)
                if tx is None or ty is None:
                    out("   test %s %s %s: %r vs %r" % (p, s, t, tx, ty))
                    diff += 1
                    continue
                same = tx[0] == ty[0] and abs(float(tx[1]) - float(ty[1])) < 1e-9
                if same and not scores_only:
                    same = tx[2:] == ty[2:]
                if not same:
                    out("   test %s %s %s: %r vs %r" % (p, s, t, tx, ty))
                    diff += 1
    return diff


if __name__ == "__main__":
    with open(sys.argv[1], encoding="utf-8") as f:
        A = json.load(f)
    with open(sys.argv[2], encoding="utf-8") as f:
        B = json.load(f)
    n = compare(A, B, "--scores-only" in sys.argv)
    print("IDENTICAL" if n == 0 else "%d difference(s)" % n)
    sys.exit(1 if n else 0)
