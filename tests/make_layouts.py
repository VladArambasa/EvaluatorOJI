#!/usr/bin/env python3
"""Construiește, dintr-un exemplu generat de tools/make_example.py, două spații de lucru suplimentare
pentru testarea celorlalte organizări ale folderelor:

  python3 make_layouts.py EXEMPLU DEST

  DEST/invers  — layout „student-problem”:  {root}/{student}/{problem}, teste în {root}/_teste/{problem}
  DEST/comun   — layout „flat” (stil OJI):  {root}/{problem}/surse/{student}{nr}.{ext}, teste în {root}/{problem}/teste
"""
import json
import os
import shutil
import sys

SRC, DEST = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
PROBLEMS = ["suma", "maxim"]
ws = json.load(open(os.path.join(SRC, "evaluator.json"), encoding="utf-8"))
probs = [p for p in ws["problems"] if p["name"] in PROBLEMS]


def students_of(prob):
    d = os.path.join(SRC, prob)
    return sorted(x for x in os.listdir(d) if os.path.isdir(os.path.join(d, x)) and not x.startswith(("_", ".")))


def save(root, cfg):
    cfg = json.loads(json.dumps(cfg))
    cfg["problems"] = [json.loads(json.dumps(p)) for p in probs]
    for p in cfg["problems"]:
        p["testsDir"] = ""
    cfg["current"] = PROBLEMS[0]
    with open(os.path.join(root, "evaluator.json"), "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=1)


# ---------------------------------------------------------------- invers: elev/problemă
inv = os.path.join(DEST, "invers")
shutil.rmtree(inv, ignore_errors=True)
for p in PROBLEMS:
    shutil.copytree(os.path.join(SRC, p, "_teste"), os.path.join(inv, "_teste", p))
    for s in students_of(p):
        shutil.copytree(os.path.join(SRC, p, s), os.path.join(inv, s, p))
cfg = dict(ws, layout="student-problem", studentDir="{root}/{student}/{problem}", testsDir="{root}/_teste/{problem}")
save(inv, cfg)

# ---------------------------------------------------------------- comun: un singur folder de surse (OJI)
flat = os.path.join(DEST, "comun")
shutil.rmtree(flat, ignore_errors=True)
for prob in probs:
    p, nr = prob["name"], int(prob.get("nr", 1))
    shutil.copytree(os.path.join(SRC, p, "_teste"), os.path.join(flat, p, "teste"))
    sd = os.path.join(flat, p, "surse")
    os.makedirs(sd)
    for s in students_of(p):
        main = None
        for dp, dn, fn in sorted(os.walk(os.path.join(SRC, p, s))):
            dn[:] = sorted(x for x in dn if not x.startswith(("cmake-build", ".", "_", "build", "release")))
            for f in sorted(fn):
                if f in ("main.c", "main.cpp", "main.py", p + ".c", p + ".cpp", p + ".py") or (f.endswith(".py") and main is None):
                    main = main or os.path.join(dp, f)
        if main:
            shutil.copy(main, os.path.join(sd, "%s%d%s" % (s, nr, os.path.splitext(main)[1])))
    # cazuri speciale: o sursă dublă, o extensie necunoscută, un fișier fără legătură
    with open(os.path.join(sd, "zoe%d.c" % nr), "w") as f:
        f.write("#include <stdio.h>\nint main(void){return 0;}\n")
    with open(os.path.join(sd, "zoe%d.cpp" % nr), "w") as f:
        f.write("int main(){return 0;}\n")
    with open(os.path.join(sd, "vlad%d.java" % nr), "w") as f:
        f.write("class A {}\n")
    with open(os.path.join(sd, "citeste.txt"), "w") as f:
        f.write("nimic\n")
cfg = dict(ws, layout="flat", flatDir="{root}/{problem}/surse", flatPattern="{student}{nr}.{ext}", testsDir="{root}/{problem}/teste")
save(flat, cfg)
print("creat", inv, "și", flat)
