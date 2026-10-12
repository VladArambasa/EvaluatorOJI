#!/usr/bin/env python3
"""Compară rezumatele (stări + punctaje) produse de două variante."""
import json, sys
a = json.load(open(sys.argv[1])); b = json.load(open(sys.argv[2]))
diff = 0
for p in sorted(set(a) | set(b)):
    for s in sorted(set(a.get(p, {})) | set(b.get(p, {}))):
        x, y = a.get(p, {}).get(s), b.get(p, {}).get(s)
        if x is None or y is None:
            print("LIPSĂ", p, s, x is None, y is None); diff += 1; continue
        if x["status"] != y["status"] or abs(float(x["score"]) - float(y["score"])) > 1e-9:
            print("DIFERIT", p, s, x["status"], x["score"], "vs", y["status"], y["score"]); diff += 1
        for t in sorted(set(x["tests"]) | set(y["tests"])):
            tx, ty = x["tests"].get(t), y["tests"].get(t)
            if tx != ty and not (tx and ty and tx[0] == ty[0] and abs(float(tx[1]) - float(ty[1])) < 1e-9):
                print("   test", p, s, t, tx, "vs", ty); diff += 1
print("IDENTIC" if diff == 0 else "%d diferențe" % diff)
sys.exit(1 if diff else 0)
