#!/usr/bin/env python3
"""Sincronizează resursele comune între variante. Rulează după orice modificare în gui/ sau common/:

    python3 tools/embed.py

- copiază gui/index.html în python/ și web/public/;
- copiază sondele (common/olieval_probe.c, common/sitecustomize.py) în python/ și web/;
- copiază utilitarul de rulare (common/runner/*) în web/helper/ și c/src/;
- generează antetele cu resursele încorporate pentru C (c/src/embedded.h) și C++ (cpp/src/embedded.hpp).
"""
import os
import shutil

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
RES = [("EMB_GUI", "gui/index.html"), ("EMB_PROBE_C", "common/olieval_probe.c"), ("EMB_PROBE_PY", "common/sitecustomize.py")]
COPIES = [
    ("gui/index.html", ["python/index.html", "web/public/index.html"]),
    ("common/olieval_probe.c", ["python/olieval_probe.c", "web/olieval_probe.c"]),
    ("common/sitecustomize.py", ["python/sitecustomize.py", "web/sitecustomize.py"]),
    ("common/runner/runner.c", ["web/helper/runner.c", "c/src/runner.c"]),
    ("common/runner/runner.h", ["web/helper/runner.h", "c/src/runner.h"]),
    ("common/runner/olieval_run.c", ["web/helper/olieval_run.c"]),
]


def gen():
    out = ["/* Generat automat de tools/embed.py din gui/index.html și common/ — nu edita manual. */",
           "#ifndef OLIEVAL_EMBEDDED_H", "#define OLIEVAL_EMBEDDED_H"]
    for name, rel in RES:
        data = open(os.path.join(ROOT, rel), "rb").read()
        out.append("static const unsigned char %s[] = {" % name)
        for i in range(0, len(data), 24):
            out.append("    " + ",".join("0x%02x" % b for b in data[i:i + 24]) + ",")
        out.append("    0x00};")
        out.append("static const unsigned long %s_LEN = %dUL;" % (name, len(data)))
    out.append("#endif")
    return "\n".join(out) + "\n"


for src, dests in COPIES:
    for d in dests:
        p = os.path.join(ROOT, d)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        shutil.copyfile(os.path.join(ROOT, src), p)
        print("copiat", src, "->", d)

text = gen()
for dest in ("cpp/src/embedded.hpp", "c/src/embedded.h"):
    p = os.path.join(ROOT, dest)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f:
        f.write(text)
    print("scris", dest, len(text), "octeți")
