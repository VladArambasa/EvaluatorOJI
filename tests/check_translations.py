#!/usr/bin/env python3
# CHECKS THE TRANSLATION FILES AGAINST THE GUI
#   PYTHON3 TESTS/CHECK_TRANSLATIONS.PY                  REPORT MISSING / UNUSED / BROKEN KEYS
#   PYTHON3 TESTS/CHECK_TRANSLATIONS.PY --SKELETON DE    PRINT A STARTING FILE FOR A NEW LANGUAGE
#   PYTHON3 TESTS/CHECK_TRANSLATIONS.PY --RESULTS F...   TRANSLATE THE MESSAGES IN RESULT FILES (LATEST.JSON)
#                                                        AND LIST THE ONES NO TEMPLATE COVERS
# EXIT CODE 1 WHEN A PROBLEM IS FOUND
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GUI = os.path.join(ROOT, "common", "gui", "index.html")
MASTER = os.path.join(ROOT, "master-translations")
VERSIONS = ["c", "c++", "python", "web"]
PH = re.compile(r"%\{(\w+)\}")


def gui_keys():
    # KEYS ARE THE STRING LITERALS PASSED TO T('...') AND N_('...')
    text = open(GUI, encoding="utf-8").read()
    keys = []
    for m in re.finditer(r"(?<![\w.$])(?:t|N_)\(\s*'((?:[^'\\\n]|\\.)*)'", text):
        k = re.sub(r"\\(.)", lambda x: {"n": "\n", "t": "\t"}.get(x.group(1), x.group(1)), m.group(1))
        if k not in keys:
            keys.append(k)
    return keys


def load_lang(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def check():
    bad = 0
    keys = gui_keys()
    print("GUI keys: %d" % len(keys))
    files = sorted(f for f in os.listdir(MASTER) if f.endswith(".json"))
    for name in files:
        try:
            L = load_lang(os.path.join(MASTER, name))
        except ValueError as e:
            print("%s: invalid JSON: %s" % (name, e))
            bad += 1
            continue
        code = name[:-5]
        ui, msgs = L.get("ui", {}), L.get("messages", {})
        if L.get("language") != code:
            print("%s: \"language\" should be \"%s\"" % (name, code))
            bad += 1
        if not L.get("name"):
            print("%s: \"name\" is missing" % name)
            bad += 1
        if code == "en":
            continue
        missing = [k for k in keys if k not in ui]
        unused = [k for k in ui if k not in keys]
        for k in missing:
            print("%s: missing ui key: %s" % (name, k))
        for k in unused:
            print("%s: unused ui key: %s" % (name, k))
        bad += len(missing)
        for group, d in (("ui", ui), ("messages", msgs)):
            for k, v in d.items():
                if not isinstance(v, str) or not v.strip():
                    print("%s: empty %s value for: %s" % (name, group, k))
                    bad += 1
                elif sorted(PH.findall(k)) != sorted(PH.findall(v)):
                    print("%s: placeholders differ in %s: %s" % (name, group, k))
                    bad += 1
            for k in d:
                if group == "messages" and re.match(r"\s*%\{", k):
                    print("%s: a message template must not start with a placeholder: %s" % (name, k))
                    bad += 1
        print("%s: %d ui, %d messages" % (name, len(ui), len(msgs)))
    # COPIES IN THE VERSIONS MUST MATCH THE MASTER FILES
    master_files = sorted(f for f in os.listdir(MASTER) if f.endswith((".json", ".txt")))
    for v in VERSIONS:
        d = os.path.join(ROOT, "versions", v, "translations")
        for f in master_files:
            p = os.path.join(d, f)
            if not os.path.isfile(p) or open(p, "rb").read() != open(os.path.join(MASTER, f), "rb").read():
                print("versions/%s/translations/%s is missing or out of date (run the install script in master-translations)" % (v, f))
                bad += 1
    print("OK" if not bad else "%d problem(s)" % bad)
    return bad


# ---------------------------------------------------------------- SAME ALGORITHM AS TM() IN THE GUI
class Translator:
    def __init__(self, messages):
        self.exact, self.rules = {}, []
        for src, dst in messages.items():
            key = src.strip()
            if not key or not isinstance(dst, str) or not dst.strip():
                continue
            if not PH.search(key):
                self.exact[key] = dst.strip()
                continue
            names, body = [], ""
            for part in re.split(r"(%\{\w+\})", key):
                m = re.match(r"^%\{(\w+)\}$", part)
                if m:
                    names.append(m.group(1))
                    body += r"([\s\S]*?)"
                else:
                    body += re.escape(part)
            self.rules.append((re.compile("^" + body + "$"), names, dst.strip(), len(PH.sub("", key))))
        self.rules.sort(key=lambda r: -r[3])

    def line(self, s, depth=0):
        m = re.match(r"^(\s*)([\s\S]*?)(\s*)$", s)
        core = m.group(2)
        if not core:
            return s, True
        if core in self.exact:
            return m.group(1) + self.exact[core] + m.group(3), True
        if depth < 4:
            for rx, names, dst, _ in self.rules:
                x = rx.match(core)
                if not x:
                    continue
                vals = {}
                for i, n in enumerate(names):
                    vals[n] = x.group(i + 1) if n.lower().startswith("raw") else self.line(x.group(i + 1), depth + 1)[0]
                return m.group(1) + PH.sub(lambda q: vals.get(q.group(1), q.group(0)), dst) + m.group(3), True
            if "; " in core:
                parts = [self.line(p, depth + 1) for p in core.split("; ")]
                return m.group(1) + "; ".join(p[0] for p in parts) + m.group(3), all(p[1] for p in parts)
        return s, False


def messages_of(res):
    # EVERY ENGINE TEXT A RESULT FILE CAN SHOW IN THE GUI
    for st in res.get("students", []):
        for n in st.get("notes", []) or []:
            yield n
        for n in st.get("warnings", []) or []:
            yield n
        comp = st.get("compile") or {}
        if re.match(r"compile\.sh \+ build\.sh: \d+ sources$", comp.get("cmd", "")):
            yield comp["cmd"]
        for ln in (comp.get("log") or "").split("\n"):
            # EVALUATOR LINES IN THE COMPILE LOG (COMPILER OUTPUT ITSELF IS NOT TRANSLATED)
            if re.match(r"(Compiled:|Compilation (exceeded|failed)|The compiler|No \.c|Build completed|Linking|Scripts|The scripts)", ln):
                yield ln
        for tr in st.get("tests", []) or []:
            for k in ("msg", "msg0", "detail"):
                if tr.get(k):
                    yield tr[k]


def check_lines(lines, code="ro", label="message"):
    # TRANSLATES EVERY LINE AND LISTS THE ONES NO TEMPLATE COVERS
    T = Translator(load_lang(os.path.join(MASTER, code + ".json")).get("messages", {}))
    seen, miss = set(), 0
    for msg in lines:
        for ln in str(msg).split("\n"):
            if label == "log":
                ln = re.sub(r"^\d\d:\d\d:\d\d ", "", ln)  # LOG LINES START WITH THE TIME
            if ln in seen or not ln.strip():
                continue
            seen.add(ln)
            if not T.line(ln)[1]:
                miss += 1
                print("%s: %s not translated: %s" % (code, label, ln))
    print("%s: %d distinct %s lines, %d not translated" % (code, len(seen), label, miss))
    return miss


def check_results(paths, code="ro"):
    msgs = []
    for p in paths:
        msgs += list(messages_of(load_lang(p)))
    return check_lines(msgs, code)


def skeleton(code):
    # MESSAGE KEYS: EVERY TEMPLATE KNOWN TO ANY LANGUAGE FILE
    msgs = []
    for name in sorted(os.listdir(MASTER)):
        if name.endswith(".json"):
            msgs += [k for k in load_lang(os.path.join(MASTER, name)).get("messages", {}) if k not in msgs]
    out = {"language": code, "name": code, "decimal": ".", "ui": {k: k for k in gui_keys()},
           "messages": {k: k for k in msgs}}
    print(json.dumps(out, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["--skeleton"] and len(a) == 2:
        skeleton(a[1])
    elif a[:1] == ["--results"] and len(a) > 1:
        sys.exit(1 if check_results(a[1:]) else 0)
    elif not a:
        sys.exit(1 if check() else 0)
    else:
        print("usage: check_translations.py [--skeleton CODE | --results FILE...]")
        sys.exit(2)
