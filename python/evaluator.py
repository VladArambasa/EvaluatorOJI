#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Evaluator Olimpiadă — varianta Python (doar biblioteca standard, Python 3.8+).

Pornire:
    python3 evaluator.py                 (Linux / macOS)
    python  evaluator.py                 (Windows)
Opțiuni:
    --port N        portul local (implicit 8765, următorul liber dacă e ocupat)
    --no-browser    nu deschide automat fereastra

Interfața (index.html) este aceeași pentru toate variantele (C, C++, Python, Web).
Specificația completă: docs/SPECIFICATIE.md
"""
import os
import sys
import json
import time
import math
import shutil
import signal
import functools
import threading
import subprocess
import secrets
import platform
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

APP_NAME = "Evaluator Olimpiadă"
VERSION = "1.0.0"
VARIANT = "Python"
IS_WIN = os.name == "nt"
OS_NAME = "windows" if IS_WIN else ("darwin" if sys.platform == "darwin" else "linux")
HERE = os.path.dirname(os.path.abspath(__file__))
if not IS_WIN:
    import resource

# =====================================================================================
# Utilitare generale
# =====================================================================================

_LOWER = {i: i + 32 for i in range(65, 91)}


def ascii_lower(s):
    return s.translate(_LOWER)


def natcmp(a, b):
    """Comparație „naturală”: test2 < test10; litere fără diferență de majuscule (ASCII)."""
    i = j = 0
    la, lb = len(a), len(b)
    while i < la and j < lb:
        ca, cb = a[i], b[j]
        if "0" <= ca <= "9" and "0" <= cb <= "9":
            si = i
            while i < la and "0" <= a[i] <= "9":
                i += 1
            sj = j
            while j < lb and "0" <= b[j] <= "9":
                j += 1
            ra = a[si:i].lstrip("0")
            rb = b[sj:j].lstrip("0")
            if len(ra) != len(rb):
                return -1 if len(ra) < len(rb) else 1
            if ra != rb:
                return -1 if ra < rb else 1
        else:
            xa, xb = ascii_lower(ca), ascii_lower(cb)
            if xa != xb:
                return -1 if xa < xb else 1
            i += 1
            j += 1
    ra, rb = la - i, lb - j
    if ra != rb:
        return -1 if ra < rb else 1
    if a == b:
        return 0
    return -1 if a < b else 1


nat_key = functools.cmp_to_key(natcmp)


def glob_match(pat, name):
    """`*` = orice (fără /), `?` = un caracter; fără diferență între majuscule (ASCII)."""
    p, n = ascii_lower(pat), ascii_lower(name)
    pi = ni = 0
    star = -1
    mark = 0
    while ni < len(n):
        if pi < len(p) and p[pi] == "*":
            star = pi
            mark = ni
            pi += 1
        elif pi < len(p) and (p[pi] == "?" or p[pi] == n[ni]):
            pi += 1
            ni += 1
        elif star != -1:
            pi = star + 1
            mark += 1
            ni = mark
        else:
            return False
    while pi < len(p) and p[pi] == "*":
        pi += 1
    return pi == len(p)


def path_match(pat, rel):
    """Potrivește tiparul (componente separate prin /) cu ultimele componente din `rel`.
    Întoarce indexul componentei de la care începe potrivirea sau -1."""
    pc = [c for c in pat.replace("\\", "/").split("/") if c]
    rc = rel.split("/")
    if not pc or len(pc) > len(rc):
        return -1
    k = len(rc) - len(pc)
    for x in range(len(pc)):
        if not glob_match(pc[x], rc[k + x]):
            return -1
    return k


def glob_full(pat, s):
    pc = [c for c in pat.replace("\\", "/").split("/") if c]
    sc = [c for c in s.replace("\\", "/").split("/") if c]
    if len(pc) != len(sc):
        return False
    return all(glob_match(pc[i], sc[i]) for i in range(len(pc)))


def split_cmd(s):
    """Împarte o comandă în cuvinte; ghilimelele duble grupează."""
    out, cur, inq, has = [], [], False, False
    for ch in s or "":
        if ch == '"':
            inq = not inq
            has = True
        elif ch in " \t\r\n" and not inq:
            if has:
                out.append("".join(cur))
                cur, has = [], False
        else:
            cur.append(ch)
            has = True
    if has:
        out.append("".join(cur))
    return out


def subst(t, **kv):
    s = t or ""
    for k, v in kv.items():
        if v is not None:
            s = s.replace("{" + k + "}", str(v))
    return s


def resolve(base, p):
    p = (p or "").strip()
    if not p:
        return os.path.normpath(base)
    if IS_WIN:
        p = p.replace("/", "\\")
    else:
        p = p.replace("\\", "/")
    if not os.path.isabs(p):
        p = os.path.join(base, p)
    return os.path.normpath(p)


def join_rel(base, rel):
    return os.path.normpath(os.path.join(base, *[c for c in rel.split("/") if c]))


def ext_of(name):
    b = name.replace("\\", "/").rsplit("/", 1)[-1]
    i = b.rfind(".")
    return ascii_lower(b[i:]) if i > 0 else ""


def fmt_int(x):
    return str(int(math.floor(float(x) + 0.5)))


def round2(x):
    return math.floor(x * 100.0 + 0.5) / 100.0


def num(v, d=0.0):
    try:
        if v is None or v == "":
            return d
        return float(v)
    except (TypeError, ValueError):
        return d


def now_str():
    return time.strftime("%Y-%m-%d %H:%M:%S")


def read_bytes(path, limit=None):
    with open(path, "rb") as f:
        return f.read() if limit is None else f.read(limit)


def read_text(path, limit=None):
    return read_bytes(path, limit).decode("utf-8", errors="replace")


def write_text(path, text):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(text.encode("utf-8"))


def file_size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


def clear_dir(d):
    """Golește un folder (fără a urma legături simbolice)."""
    if not os.path.isdir(d):
        os.makedirs(d, exist_ok=True)
        return
    for name in os.listdir(d):
        p = os.path.join(d, name)
        try:
            if os.path.isdir(p) and not os.path.islink(p):
                shutil.rmtree(p, ignore_errors=True)
            else:
                os.remove(p)
        except OSError:
            pass


def remove_file(p):
    try:
        if os.path.isfile(p) or os.path.islink(p):
            os.remove(p)
    except OSError:
        pass


def walk_bfs(top, max_depth, ignore):
    """Parcurgere în lățime. Întoarce [(rel cu '/', abs, este_folder, adâncime)]."""
    out = []
    queue = [(top, "", 0)]
    qi = 0
    while qi < len(queue):
        d, rel, depth = queue[qi]
        qi += 1
        try:
            names = sorted(os.listdir(d), key=nat_key)
        except OSError:
            continue
        sub = []
        for name in names:
            ap = os.path.join(d, name)
            rp = rel + "/" + name if rel else name
            if os.path.isdir(ap):
                if any(glob_match(g, name) for g in ignore):
                    continue
                out.append((rp, ap, True, depth))
                if depth < max_depth:
                    sub.append((ap, rp, depth + 1))
            else:
                out.append((rp, ap, False, depth))
        queue.extend(sub)
    return out


# =====================================================================================
# Valori implicite (aceleași ca în interfață)
# =====================================================================================

def default_languages():
    py = "python" if IS_WIN else "python3"
    return [
        {"id": "c", "name": "C", "ext": ".c",
         "compile": "gcc -O2 -Wall -std=c11 {sources} -o {exe} {libs}", "run": "{exe}", "probe": True},
        {"id": "cpp", "name": "C++", "ext": ".cpp .cc .cxx",
         "compile": "g++ -O2 -Wall -std=c++17 {sources} -o {exe} {libs}", "run": "{exe}", "probe": True},
        {"id": "py", "name": "Python", "ext": ".py", "compile": "", "run": py + " {main}", "probe": True},
    ]


DEFAULT_AUTOLIBS = [
    {"name": "Math", "headers": "math.h complex.h tgmath.h fenv.h", "flags": "-lm"},
    {"name": "Threads", "headers": "pthread.h threads.h", "flags": "-pthread"},
    {"name": "Ncurses", "headers": "ncurses.h curses.h", "flags": "-lncurses"},
    {"name": "Readline", "headers": "readline/readline.h", "flags": "-lreadline"},
    {"name": "SDL2", "headers": "SDL2/SDL.h", "flags": "-lSDL2"},
    {"name": "SDL2_image", "headers": "SDL2/SDL_image.h", "flags": "-lSDL2_image"},
    {"name": "SDL2_ttf", "headers": "SDL2/SDL_ttf.h", "flags": "-lSDL2_ttf"},
    {"name": "SDL2_mixer", "headers": "SDL2/SDL_mixer.h", "flags": "-lSDL2_mixer"},
    {"name": "Raylib", "headers": "raylib.h", "flags": "-lraylib -lGL -lm -lpthread -ldl -lrt -lX11"},
    {"name": "GLFW", "headers": "GLFW/glfw3.h", "flags": "-lglfw -lGL"},
    {"name": "GLEW", "headers": "GL/glew.h", "flags": "-lGLEW"},
    {"name": "OpenGL", "headers": "GL/*", "flags": "-lGL -lGLU -lglut"},
    {"name": "wxWidgets", "headers": "wx/*", "flags": "$(wx-config --cxxflags --libs)"},
    {"name": "X11", "headers": "X11/Xlib.h", "flags": "-lX11"},
    {"name": "Zlib", "headers": "zlib.h", "flags": "-lz"},
    {"name": "SQLite", "headers": "sqlite3.h", "flags": "-lsqlite3"},
    {"name": "Curl", "headers": "curl/curl.h", "flags": "-lcurl"},
    {"name": "OpenSSL", "headers": "openssl/*.h", "flags": "-lssl -lcrypto"},
    {"name": "GMP", "headers": "gmp.h", "flags": "-lgmp"},
    {"name": "Dynamic loading", "headers": "dlfcn.h", "flags": "-ldl"},
    {"name": "Realtime", "headers": "mqueue.h aio.h", "flags": "-lrt"},
    {"name": "Crypt", "headers": "crypt.h", "flags": "-lcrypt"},
]

DEFAULT_IGNORE = ".* _* cmake-build-* CMakeFiles build bin obj out release debug __MACOSX node_modules venv __pycache__"

DEFAULT_ROOT_RULES = [
    {"pattern": "CMakeLists.txt", "up": 0}, {"pattern": "*.cbp", "up": 0},
    {"pattern": "bash-scripts/compile.sh", "up": 0}, {"pattern": "bash-scripts/build.sh", "up": 0},
    {"pattern": "compile.sh", "up": 0}, {"pattern": "build.sh", "up": 0},
    {"pattern": "main.cpp", "up": 0}, {"pattern": "main.c", "up": 0},
    {"pattern": "*.cpp", "up": 0}, {"pattern": "*.c", "up": 0}, {"pattern": "*.py", "up": 0},
]


def g(obj, key, default):
    if isinstance(obj, dict):
        v = obj.get(key)
        if v is not None:
            return v
    return default


class Cfg:
    """Vedere peste spațiul de lucru + problemă, cu valori implicite."""

    def __init__(self, ws, prob):
        self.ws = ws
        self.prob = prob
        self.root = os.path.normpath(os.path.abspath(g(ws, "root", "") or "."))
        self.name = str(g(prob, "name", ""))
        self.nr = str(int(num(g(prob, "nr", 1), 1)))
        self.layout = g(ws, "layout", "problem-student")
        self.languages = g(ws, "languages", None) or default_languages()
        al = g(ws, "autoLibs", {})
        self.autolibs_on = bool(g(al, "enabled", True))
        self.autolibs = g(al, "rules", None)
        if self.autolibs is None:
            self.autolibs = DEFAULT_AUTOLIBS
        self.ignore = (g(ws, "ignoreDirs", DEFAULT_IGNORE) or "").split()
        opt = g(ws, "options", {})
        self.keep_outputs = bool(g(opt, "keepOutputs", True))
        self.win_stack = bool(g(opt, "windowsStackFlag", True))
        self.compile_timeout = num(g(opt, "compileTimeoutSec", 60), 60)
        self.nonzero_error = bool(g(opt, "nonzeroExitIsError", True))
        self.use_probe = bool(g(opt, "useProbe", True))
        io = g(prob, "io", {})
        self.io_input = g(io, "input", "file")
        self.io_output = g(io, "output", "file")
        self.io_inpath = g(io, "inputPath", "{problem}.in")
        self.io_outpath = g(io, "outputPath", "{problem}.out")
        self.io_cwd = g(io, "cwd", "")
        self.run_in = g(io, "runIn", "sandbox")
        pj = g(prob, "project", {})
        self.root_rules = g(pj, "rootRules", None) or DEFAULT_ROOT_RULES
        self.max_depth = int(num(g(pj, "maxDepth", 6), 6))
        self.main_patterns = (g(pj, "mainPatterns", "main.cpp main.c {problem}.cpp {problem}.c *.cpp *.c *.py") or "").split()
        self.compile_scope = g(pj, "compileScope", "main")
        self.build_mode = g(pj, "buildMode", "command")
        self.build_cmd = str(g(pj, "buildCommand", "auto") or "auto")
        self.build_exe = str(g(pj, "buildExe", "release/program") or "release/program")
        self.size_scope = g(pj, "sizeScope", "main")
        self.size_ext = [ascii_lower(e) for e in (g(pj, "sizeExt", ".c .h .cpp .hpp .cc .cxx .hh .py") or "").split()]
        self.compile_extra = g(prob, "compileExtra", "") or ""
        lim = g(prob, "limits", {})
        self.time_ms = num(g(lim, "timeMs", 1000), 1000)
        self.time_mode = g(lim, "timeMode", "cpu")
        self.hard_time = max(1.0, num(g(lim, "hardTimeFactor", 2), 2))
        self.mem_mb = num(g(lim, "memMb", 64), 64)
        self.hard_mem = max(1.0, num(g(lim, "hardMemFactor", 2), 2))
        self.stack_mb = num(g(lim, "stackMb", 8), 8)
        self.source_kb = num(g(lim, "sourceKb", 0), 0)
        self.output_mb = num(g(lim, "outputMb", 64), 64)
        self.calibrate = bool(g(lim, "calibrate", True))
        cmp_ = g(prob, "compare", {})
        self.cmp_mode = g(cmp_, "mode", "lines")
        self.ignore_case = bool(g(cmp_, "ignoreCase", False))
        self.epsilon = num(g(cmp_, "epsilon", 1e-6), 1e-6)
        self.checker = g(cmp_, "checker", "") or ""
        self.protocol = g(cmp_, "protocol", "oji")
        self.checker_args = g(cmp_, "args", "") or ""
        self.v = dict(root=self.root, problem=self.name, nr=self.nr)
        self.tests_dir = resolve(self.root, subst(g(prob, "testsDir", "") or g(ws, "testsDir", "{root}/{problem}/_teste"), **self.v))
        self.work_dir = resolve(self.root, subst(g(ws, "workDir", "{root}/_lucru"), **self.v))
        self.results_dir = resolve(self.root, subst(g(ws, "resultsDir", "{root}/_rezultate"), **self.v))
        self.exe_suffix = ".exe" if IS_WIN else ""

    def lang_for(self, filename):
        e = ext_of(filename)
        if not e:
            return None
        for L in self.languages:
            if e in [ascii_lower(x) for x in str(g(L, "ext", "")).split()]:
                return L
        return None

    def all_exts(self):
        out = []
        for L in self.languages:
            out.extend(ascii_lower(x) for x in str(g(L, "ext", "")).split())
        return out

    def probe_mode(self, L):
        """'c' = sonda C legată în program, 'py' = sonda Python (sitecustomize), '' = fără sondă."""
        if not self.use_probe or not bool(g(L, "probe", False)):
            return ""
        if str(g(L, "compile", "") or "").strip():
            if self.build_mode == "scripts":
                # nu controlăm legarea: pe Linux sonda se injectează cu LD_PRELOAD
                return "preload" if OS_NAME == "linux" else ""
            return "c"
        if ".py" in [ascii_lower(x) for x in str(g(L, "ext", "")).split()]:
            return "py"
        return ""

    def probe_env(self, L, probe_file, job):
        env = ["OLIEVAL_PROBE=" + probe_file]
        mode = self.probe_mode(L)
        if mode == "py" and job.py_probe_dir:
            old = os.environ.get("PYTHONPATH", "")
            env.append("PYTHONPATH=" + job.py_probe_dir + ((os.pathsep + old) if old else ""))
        if mode == "preload" and job.preload_so:
            old = os.environ.get("LD_PRELOAD", "")
            env.append("LD_PRELOAD=" + job.preload_so + ((" " + old) if old else ""))
        return env + job.env_path_add

    def probe_active(self, L, job):
        mode = self.probe_mode(L)
        return bool(mode) and (mode != "preload" or bool(job.preload_so))


def find_problem(ws, name):
    for p in g(ws, "problems", []) or []:
        if g(p, "name", "") == name:
            return p
    return None


# =====================================================================================
# Căutarea programelor (compilatoare, interpretoare)
# =====================================================================================

WIN_EXTRA_DIRS = [
    r"C:\OJI\Codeblocks\MinGW\bin", r"D:\OJI\Codeblocks\MinGW\bin", r"C:\OJI\CodeBlocks\MinGW\bin",
    r"C:\Program Files\CodeBlocks\MinGW\bin", r"C:\Program Files (x86)\CodeBlocks\MinGW\bin",
    r"C:\MinGW\bin", r"C:\mingw64\bin", r"C:\msys64\ucrt64\bin", r"C:\msys64\mingw64\bin",
    r"C:\TDM-GCC-64\bin", r"C:\TDM-GCC-32\bin",
    r"C:\Program Files\Dev-Cpp\MinGW64\bin", r"C:\Program Files (x86)\Dev-Cpp\MinGW64\bin",
]


def _win_cands(d, n):
    base = os.path.basename(n)
    if "." in base:
        return [os.path.join(d, n)]
    return [os.path.join(d, n + e) for e in (".exe", ".com", ".bat", ".cmd")] + [os.path.join(d, n)]


def _find_python_install():
    bases = []
    la = os.environ.get("LOCALAPPDATA")
    if la:
        bases.append(os.path.join(la, "Programs", "Python"))
    bases += ["C:\\", r"C:\Program Files"]
    for b in bases:
        try:
            names = [n for n in os.listdir(b) if n.lower().startswith("python3") and os.path.isdir(os.path.join(b, n))]
        except OSError:
            continue
        for n in sorted(names, reverse=True):
            c = os.path.join(b, n, "python.exe")
            if os.path.isfile(c):
                return c
    return None


def find_program(name):
    if not name:
        return None
    if "/" in name or "\\" in name or (IS_WIN and len(name) > 1 and name[1] == ":"):
        for c in (_win_cands(os.path.dirname(name) or ".", os.path.basename(name)) if IS_WIN else [name]):
            if os.path.isfile(c) and (IS_WIN or os.access(c, os.X_OK)):
                return os.path.abspath(c)
        return None
    if not IS_WIN:
        p = shutil.which(name)
        return os.path.abspath(p) if p else None
    is_py = name.lower() in ("python3", "python", "python3.exe", "python.exe")
    store = [None]

    def search(n):
        for d in os.environ.get("PATH", "").split(os.pathsep):
            d = d.strip().strip('"')
            if not d:
                continue
            for c in _win_cands(d, n):
                if os.path.isfile(c):
                    if is_py and "\\windowsapps\\" in c.lower():
                        if store[0] is None:
                            store[0] = c
                        continue
                    return c
        return None

    found = search(name)
    if not found and is_py:
        found = search("python") or search("py") or _find_python_install()
    if not found:
        for d in WIN_EXTRA_DIRS:
            for c in _win_cands(d, name):
                if os.path.isfile(c):
                    found = c
                    break
            if found:
                break
    return found or store[0]


# =====================================================================================
# Rularea unui proces cu limite și măsurători
# =====================================================================================

SIGNAL_NAMES = {}
if not IS_WIN:
    SIGNAL_NAMES = {
        signal.SIGSEGV: "SIGSEGV - acces invalid la memorie (vector depășit, pointer greșit sau stivă depășită)",
        signal.SIGFPE: "SIGFPE - eroare aritmetică (ex. împărțire la zero)",
        signal.SIGABRT: "SIGABRT - program oprit cu abort (assert eșuat sau excepție neprinsă)",
        signal.SIGKILL: "SIGKILL - program oprit forțat",
        signal.SIGXCPU: "SIGXCPU - limita de timp procesor depășită",
        signal.SIGXFSZ: "SIGXFSZ - fișier de ieșire prea mare",
        signal.SIGBUS: "SIGBUS - eroare de acces la memorie",
        signal.SIGILL: "SIGILL - instrucțiune ilegală",
        signal.SIGPIPE: "SIGPIPE - scriere într-un canal închis",
        signal.SIGTERM: "SIGTERM - program oprit",
    }

NTSTATUS_NAMES = {
    0xC0000005: "ACCESS_VIOLATION - acces invalid la memorie (vector depășit sau pointer greșit)",
    0xC00000FD: "STACK_OVERFLOW - depășire stivă (recursivitate prea adâncă sau tablouri locale mari)",
    0xC0000094: "INTEGER_DIVIDE_BY_ZERO - împărțire la zero",
    0xC000008E: "FLOAT_DIVIDE_BY_ZERO - împărțire la zero (real)",
    0xC0000095: "INTEGER_OVERFLOW - depășire aritmetică",
    0xC0000409: "STACK_BUFFER_OVERRUN - program oprit forțat (abort / depășire de buffer)",
    0xC0000017: "NO_MEMORY - memorie insuficientă",
    0xC000012D: "COMMITMENT_LIMIT - limita de memorie atinsă",
    0xC000001D: "ILLEGAL_INSTRUCTION - instrucțiune ilegală",
    0xC0000374: "HEAP_CORRUPTION - memorie dinamică coruptă",
    0xC0000135: "DLL_NOT_FOUND - lipsește o bibliotecă DLL (ex. libstdc++-6.dll)",
    0xC0000139: "ENTRYPOINT_NOT_FOUND - bibliotecă DLL incompatibilă",
    0xC0000142: "DLL_INIT_FAILED - inițializare DLL eșuată",
    0xC000013A: "CONTROL_C_EXIT - program întrerupt",
    0xC0000420: "ASSERTION_FAILURE - assert eșuat",
}


def status_text(r):
    if not r["started"]:
        return r["error"]
    if r["timedOut"]:
        return "oprit la limita de timp"
    if r["cancelled"]:
        return "oprit de utilizator"
    if r["signal"]:
        return SIGNAL_NAMES.get(r["signal"], "semnal %d" % r["signal"])
    if IS_WIN and r["winStatus"] >= 0xC0000000:
        return NTSTATUS_NAMES.get(r["winStatus"], "eroare la execuție (cod 0x%08X)" % r["winStatus"])
    return "cod de ieșire %d" % r["exit"]


def _new_result():
    return {"started": False, "error": "", "exited": False, "exit": 0, "winStatus": 0, "signal": 0,
            "timedOut": False, "cancelled": False, "wallMs": 0.0, "cpuMs": 0.0, "peakKb": 0,
            "peakCommitKb": 0, "memLimitHit": False}


if IS_WIN:
    import ctypes
    from ctypes import wintypes

    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _ntdll = ctypes.WinDLL("ntdll")

    class _IO_COUNTERS(ctypes.Structure):
        _fields_ = [(n, ctypes.c_ulonglong) for n in (
            "ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
            "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

    class _JOB_BASIC(ctypes.Structure):
        _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                    ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                    ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                    ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                    ("SchedulingClass", wintypes.DWORD)]

    class _JOB_EXT(ctypes.Structure):
        _fields_ = [("BasicLimitInformation", _JOB_BASIC), ("IoInfo", _IO_COUNTERS),
                    ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                    ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]

    class _PMC(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                    ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                    ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                    ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]

    _k32.CreateJobObjectW.restype = wintypes.HANDLE
    _k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    _k32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    _k32.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
    _k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    _k32.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
    _k32.CloseHandle.argtypes = [wintypes.HANDLE]
    _k32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    _k32.K32GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(_PMC), wintypes.DWORD]
    _k32.SetErrorMode.argtypes = [wintypes.UINT]
    _ntdll.NtResumeProcess.argtypes = [wintypes.HANDLE]

    _JOB_KILL_ON_CLOSE = 0x2000
    _JOB_DIE_ON_UNHANDLED = 0x400
    _JOB_ACTIVE_PROCESS = 0x8
    _JOB_PROCESS_MEMORY = 0x100
    _JOB_PROCESS_TIME = 0x2
    _CREATE_SUSPENDED = 0x4
    _CREATE_NO_WINDOW = 0x08000000
    _CREATE_BREAKAWAY = 0x01000000

    def _ft_ms(ft):
        return ((ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000.0


def run_process(argv, cwd=None, stdin_path=None, stdout_path=None, stderr_path=None, env_add=None,
                wall_ms=0, cpu_s=0, mem_kb=0, stack_kb=0, fsize_kb=0, single_process=False, cancel=None):
    """Rulează `argv` cu limite. Întoarce un dicționar cu măsurătorile (vezi _new_result)."""
    r = _new_result()
    if not argv:
        r["error"] = "Comandă goală"
        return r
    prog = find_program(argv[0])
    if not prog:
        r["error"] = "Programul nu a fost găsit: %s" % argv[0]
        return r
    argv = [prog] + list(argv[1:])
    env = dict(os.environ)
    for kv in env_add or []:
        k, _, v = kv.partition("=")
        if IS_WIN:
            for ek in list(env.keys()):
                if ek.upper() == k.upper():
                    del env[ek]
        env[k] = v
    files = []
    try:
        fin = open(stdin_path if stdin_path else os.devnull, "rb")
        files.append(fin)
        fout = open(stdout_path if stdout_path else os.devnull, "wb")
        files.append(fout)
        if stderr_path and stdout_path and stderr_path == stdout_path:
            ferr = fout
        else:
            ferr = open(stderr_path if stderr_path else os.devnull, "wb")
            files.append(ferr)
    except OSError as e:
        for f in files:
            f.close()
        r["error"] = "Nu pot deschide fișierele de intrare/ieșire: %s" % e
        return r
    try:
        if IS_WIN:
            _run_windows(r, argv, cwd, fin, fout, ferr, env, wall_ms, cpu_s, mem_kb, single_process, cancel)
        else:
            _run_posix(r, argv, cwd, fin, fout, ferr, env, wall_ms, cpu_s, mem_kb, stack_kb, fsize_kb, cancel)
    finally:
        for f in files:
            try:
                f.close()
            except OSError:
                pass
    return r


def _set_limit(res, v):
    try:
        soft, hard = resource.getrlimit(res)
        if hard != resource.RLIM_INFINITY and v > hard:
            v = hard
        try:
            resource.setrlimit(res, (v, v))
        except (ValueError, OSError):
            resource.setrlimit(res, (v, hard))
    except (ValueError, OSError):
        pass


def _run_posix(r, argv, cwd, fin, fout, ferr, env, wall_ms, cpu_s, mem_kb, stack_kb, fsize_kb, cancel):
    def pre():
        os.setpgid(0, 0)
        if cpu_s > 0:
            try:
                resource.setrlimit(resource.RLIMIT_CPU, (int(cpu_s), int(cpu_s) + 1))
            except (ValueError, OSError):
                pass
        if mem_kb > 0:
            _set_limit(resource.RLIMIT_AS, int(mem_kb) * 1024)
        if stack_kb > 0:
            _set_limit(resource.RLIMIT_STACK, int(stack_kb) * 1024)
        if fsize_kb > 0:
            _set_limit(resource.RLIMIT_FSIZE, int(fsize_kb) * 1024)
        try:
            resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        except (ValueError, OSError):
            pass

    t0 = time.perf_counter()
    try:
        p = subprocess.Popen(argv, stdin=fin, stdout=fout, stderr=ferr, cwd=cwd or None, env=env,
                             preexec_fn=pre, close_fds=True)
    except OSError as e:
        r["error"] = ("Nu pot intra în folderul de lucru: %s" % e.strerror) if (cwd and not os.path.isdir(cwd)) \
            else "Nu pot lansa programul: %s" % (e.strerror or e)
        return
    r["started"] = True
    killed = False
    status = 0
    ru = None
    while True:
        try:
            pid, status, ru = os.wait4(p.pid, os.WNOHANG)
        except ChildProcessError:
            break
        except InterruptedError:
            continue
        if pid == p.pid:
            break
        el = (time.perf_counter() - t0) * 1000.0
        if not killed:
            stop = bool(cancel and cancel())
            if stop or (wall_ms > 0 and el > wall_ms):
                if stop:
                    r["cancelled"] = True
                else:
                    r["timedOut"] = True
                for target in (-p.pid, p.pid):
                    try:
                        os.kill(target, signal.SIGKILL)
                    except OSError:
                        pass
                killed = True
        time.sleep(0.0001 if el < 20 else (0.0005 if el < 1000 else 0.002))
    r["wallMs"] = (time.perf_counter() - t0) * 1000.0
    try:
        os.kill(-p.pid, signal.SIGKILL)
    except OSError:
        pass
    p.returncode = 0
    if ru is not None:
        r["cpuMs"] = (ru.ru_utime + ru.ru_stime) * 1000.0
        r["peakKb"] = int(ru.ru_maxrss // 1024) if sys.platform == "darwin" else int(ru.ru_maxrss)
    if os.WIFEXITED(status):
        r["exited"] = not killed
        r["exit"] = os.WEXITSTATUS(status)
    elif os.WIFSIGNALED(status):
        r["signal"] = os.WTERMSIG(status)
        if r["signal"] == signal.SIGXCPU:
            r["timedOut"] = True


def _run_windows(r, argv, cwd, fin, fout, ferr, env, wall_ms, cpu_s, mem_kb, single_process, cancel):
    _k32.SetErrorMode(0x0001 | 0x0002 | 0x8000)
    job = _k32.CreateJobObjectW(None, None)
    if job:
        li = _JOB_EXT()
        li.BasicLimitInformation.LimitFlags = _JOB_KILL_ON_CLOSE | _JOB_DIE_ON_UNHANDLED
        if single_process:
            li.BasicLimitInformation.LimitFlags |= _JOB_ACTIVE_PROCESS
            li.BasicLimitInformation.ActiveProcessLimit = 1
        if mem_kb > 0:
            li.BasicLimitInformation.LimitFlags |= _JOB_PROCESS_MEMORY
            li.ProcessMemoryLimit = int(mem_kb) * 1024
        if cpu_s > 0:
            li.BasicLimitInformation.LimitFlags |= _JOB_PROCESS_TIME
            li.BasicLimitInformation.PerProcessUserTimeLimit = int(cpu_s) * 10000000
        _k32.SetInformationJobObject(job, 9, ctypes.byref(li), ctypes.sizeof(li))
    flags = _CREATE_SUSPENDED | _CREATE_NO_WINDOW | _CREATE_BREAKAWAY
    p = None
    try:
        try:
            p = subprocess.Popen(argv, stdin=fin, stdout=fout, stderr=ferr, cwd=cwd or None, env=env,
                                 creationflags=flags)
        except PermissionError:
            p = subprocess.Popen(argv, stdin=fin, stdout=fout, stderr=ferr, cwd=cwd or None, env=env,
                                 creationflags=flags & ~_CREATE_BREAKAWAY)
    except OSError as e:
        r["error"] = "Nu pot lansa programul: %s" % (e.strerror or e)
        if job:
            _k32.CloseHandle(job)
        return
    r["started"] = True
    h = int(p._handle)
    injob = bool(job) and bool(_k32.AssignProcessToJobObject(job, h))
    t0 = time.perf_counter()
    _ntdll.NtResumeProcess(h)
    killed = False
    while True:
        try:
            p.wait(timeout=0.005)
            break
        except subprocess.TimeoutExpired:
            pass
        el = (time.perf_counter() - t0) * 1000.0
        stop = bool(cancel and cancel())
        if stop or (wall_ms > 0 and el > wall_ms):
            if stop:
                r["cancelled"] = True
            else:
                r["timedOut"] = True
            if injob:
                _k32.TerminateJobObject(job, 1)
            try:
                p.kill()
            except OSError:
                pass
            killed = True
            try:
                p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
            break
    r["wallMs"] = (time.perf_counter() - t0) * 1000.0
    code = p.returncode if p.returncode is not None else 1
    code &= 0xFFFFFFFF
    r["winStatus"] = code
    r["exit"] = code if code < 0x80000000 else code - 0x100000000
    r["exited"] = (not killed) and code < 0xC0000000
    a, b, k, u = wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME(), wintypes.FILETIME()
    if _k32.GetProcessTimes(h, ctypes.byref(a), ctypes.byref(b), ctypes.byref(k), ctypes.byref(u)):
        r["cpuMs"] = _ft_ms(k) + _ft_ms(u)
    pmc = _PMC()
    pmc.cb = ctypes.sizeof(pmc)
    if _k32.K32GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb):
        r["peakKb"] = int(pmc.PeakWorkingSetSize // 1024)
        r["peakCommitKb"] = int(pmc.PeakPagefileUsage // 1024)
    if injob:
        q = _JOB_EXT()
        if _k32.QueryInformationJobObject(job, 9, ctypes.byref(q), ctypes.sizeof(q), None):
            r["peakCommitKb"] = max(r["peakCommitKb"], int(q.PeakProcessMemoryUsed // 1024))
    if mem_kb > 0 and r["peakCommitKb"] * 100 >= mem_kb * 95:
        r["memLimitHit"] = True
    if cpu_s > 0 and r["cpuMs"] >= cpu_s * 1000.0:
        r["timedOut"] = True
    if job:
        _k32.CloseHandle(job)


def read_probe(path):
    out = {}
    try:
        for line in read_text(path, 4096).splitlines():
            parts = line.split()
            if len(parts) == 2:
                out[parts[0]] = num(parts[1], 0)
    except OSError:
        pass
    return out


# =====================================================================================
# Detectare: teste, elevi, proiecte
# =====================================================================================

def list_tests_files(d):
    out = []
    for rp, ap, isdir, depth in walk_bfs(d, 1, []):
        if not isdir:
            out.append(rp)
    return out


def match_n(pattern, rel):
    i = pattern.find("{n}")
    if i < 0:
        return "1" if glob_full(pattern, rel) else None
    pre, suf = pattern[:i], pattern[i + 3:]
    if len(rel) <= len(pre) + len(suf):
        return None
    if ascii_lower(rel[:len(pre)]) != ascii_lower(pre):
        return None
    if suf and ascii_lower(rel[len(rel) - len(suf):]) != ascii_lower(suf):
        return None
    mid = rel[len(pre):len(rel) - len(suf)]
    if "/" in mid or not mid:
        return None
    return mid


CHECKER_NAMES = ["verif.cpp", "verif.c", "verif.py", "verif.exe", "verif"]


def scan_tests(C):
    res = {"testsDir": C.tests_dir, "testsDirExists": os.path.isdir(C.tests_dir), "tests": [], "checker": ""}
    if not res["testsDirExists"]:
        return res
    ip = subst(g(C.prob, "inputPattern", "{n}-{problem}.in"), problem=C.name, nr=C.nr)
    op = subst(g(C.prob, "okPattern", "{n}-{problem}.ok"), problem=C.name, nr=C.nr)
    found = {}
    for rel in list_tests_files(C.tests_dir):
        n = match_n(ip, rel)
        if n is None or n in found:
            continue
        ok = op.replace("{n}", n)
        found[n] = {"id": n, "input": rel, "ok": ok,
                    "inputSize": file_size(join_rel(C.tests_dir, rel)),
                    "okExists": os.path.isfile(join_rel(C.tests_dir, ok)),
                    "okSize": file_size(join_rel(C.tests_dir, ok))}
    res["tests"] = [found[k] for k in sorted(found.keys(), key=nat_key)]
    for c in CHECKER_NAMES:
        if os.path.isfile(os.path.join(C.tests_dir, c)):
            res["checker"] = c
            break
    return res


def match_flat(pattern, fname):
    """Tipar cu {student} și {ext}; întoarce (student, ext) sau None."""
    toks = []
    i = 0
    while i < len(pattern):
        if pattern.startswith("{student}", i):
            toks.append(("S", None))
            i += 9
        elif pattern.startswith("{ext}", i):
            toks.append(("E", None))
            i += 5
        else:
            j = i
            while j < len(pattern) and not pattern.startswith("{student}", j) and not pattern.startswith("{ext}", j):
                j += 1
            toks.append(("L", ascii_lower(pattern[i:j])))
            i = j
    name = fname
    low = ascii_lower(fname)
    caps = {}

    def rec(ti, pos):
        if ti == len(toks):
            return pos == len(name)
        kind, lit = toks[ti]
        if kind == "L":
            if low.startswith(lit, pos):
                return rec(ti + 1, pos + len(lit))
            return False
        for end in range(pos + 1, len(name) + 1):
            piece = name[pos:end]
            if kind == "E" and "." in piece:
                break
            caps[kind] = piece
            if rec(ti + 1, end):
                return True
        return False

    if rec(0, 0) and caps.get("S"):
        return caps.get("S"), caps.get("E", "")
    return None


def discover_students(C):
    reg = {}
    order = []
    for s in g(C.ws, "students", []) or []:
        sid = str(g(s, "id", "")).strip()
        if sid and sid not in reg:
            reg[sid] = s
            order.append(sid)
    out = {}
    if C.layout == "flat":
        fdir = resolve(C.root, subst(g(C.ws, "flatDir", "{root}/{problem}/surse"), **C.v))
        pat = subst(g(C.ws, "flatPattern", "{student}{nr}.{ext}"), problem=C.name, nr=C.nr)
        exts = C.all_exts()
        try:
            names = sorted(os.listdir(fdir), key=nat_key)
        except OSError:
            names = []
        for name in names:
            fp = os.path.join(fdir, name)
            if not os.path.isfile(fp):
                continue
            m = match_flat(pat, name)
            if not m:
                continue
            sid, ext = m
            if ext and ("." + ascii_lower(ext)) not in exts:
                continue
            if sid in out:
                out[sid]["warnings"].append("Mai multe surse găsite; se folosește %s" % os.path.basename(out[sid]["file"]))
                continue
            out[sid] = {"id": sid, "dir": fdir, "exists": True, "file": fp, "warnings": []}
        for sid in order:
            if sid not in out:
                out[sid] = {"id": sid, "dir": fdir, "exists": False, "file": "", "warnings": []}
    else:
        t = subst(g(C.ws, "studentDir", "{root}/{problem}/{student}"), root=C.root, problem=C.name, nr=C.nr)
        t = t.replace("\\", "/")
        k = t.find("{student}")
        if k < 0:
            raise ValueError("Șablonul folderului de elev trebuie să conțină {student}")
        base = resolve(C.root, t[:k] or ".")
        suffix = t[k + 9:]
        special = {os.path.normcase(x) for x in (C.tests_dir, C.work_dir, C.results_dir)}
        try:
            names = sorted(os.listdir(base), key=nat_key)
        except OSError:
            names = []
        for name in names:
            if name.startswith(".") or name.startswith("_"):
                continue
            p = os.path.join(base, name)
            if not os.path.isdir(p) or os.path.normcase(os.path.normpath(p)) in special:
                continue
            d = resolve(base, name + suffix)
            out[name] = {"id": name, "dir": d, "exists": os.path.isdir(d), "file": "", "warnings": []}
        for sid in order:
            if sid not in out:
                d = resolve(base, sid + suffix)
                out[sid] = {"id": sid, "dir": d, "exists": os.path.isdir(d), "file": "", "warnings": []}
    res = []
    for sid in sorted(out.keys(), key=nat_key):
        s = out[sid]
        s["registered"] = sid in reg
        s["name"] = str(g(reg.get(sid, {}), "name", "") or "")
        res.append(s)
    return res


def has_main_func(path):
    try:
        t = read_bytes(path, 2 * 1024 * 1024)
    except OSError:
        return False
    i = 0
    while True:
        i = t.find(b"main", i)
        if i < 0:
            return False
        before = t[i - 1:i] if i > 0 else b" "
        j = i + 4
        while j < len(t) and t[j:j + 1] in (b" ", b"\t", b"\r", b"\n"):
            j += 1
        if not (before.isalnum() or before == b"_") and t[j:j + 1] == b"(":
            return True
        i += 4


def detect_project(C, st):
    """Rădăcina proiectului, fișierul principal, limbajul, sursele, dimensiunile."""
    info = {"root": "", "main": "", "mainRel": "", "lang": "", "langObj": None, "sources": [],
            "sizeMain": 0, "sizeAll": 0, "sizeJudged": 0, "warnings": list(st.get("warnings", []))}
    if C.layout == "flat":
        f = st.get("file") or ""
        info["root"] = st["dir"]
        if not f:
            return info
        info["main"] = f
        info["mainRel"] = os.path.basename(f)
        L = C.lang_for(f)
        if L:
            info["lang"] = g(L, "id", "")
            info["langObj"] = L
        info["sources"] = [os.path.basename(f)]
        info["sizeMain"] = info["sizeAll"] = info["sizeJudged"] = file_size(f)
        return info
    sdir = st["dir"]
    entries = walk_bfs(sdir, C.max_depth, C.ignore)
    root_comps = []
    for rule in C.root_rules:
        pat = subst(str(g(rule, "pattern", "")), problem=C.name, nr=C.nr).strip()
        if not pat:
            continue
        up = int(num(g(rule, "up", 0), 0))
        hit = None
        for rp, ap, isdir, depth in entries:
            k = path_match(pat, rp)
            if k >= 0:
                hit = (rp, k)
                break
        if hit:
            comps = hit[0].split("/")[:hit[1]]
            if up > 0:
                comps = comps[:max(0, len(comps) - up)]
            root_comps = comps
            break
    root = join_rel(sdir, "/".join(root_comps)) if root_comps else os.path.normpath(sdir)
    info["root"] = root
    rentries = walk_bfs(root, C.max_depth, C.ignore)
    files = [(rp, ap) for rp, ap, isdir, depth in rentries if not isdir]
    main = None
    for pat in C.main_patterns:
        pat = subst(pat, problem=C.name, nr=C.nr)
        m = [(rp, ap) for rp, ap in files if path_match(pat, rp) >= 0]
        if not m:
            continue
        chosen = None
        for rp, ap in m:
            if ext_of(rp) == ".py" or has_main_func(ap):
                chosen = (rp, ap)
                break
        main = chosen or m[0]
        if len(m) > 1:
            info["warnings"].append("Mai multe fișiere posibile (%d); ales: %s" % (len(m), main[0]))
        break
    if not main:
        info["warnings"].append("Nu am găsit fișierul principal (main) în %s" % root)
        return info
    info["main"] = main[1]
    info["mainRel"] = main[0]
    L = C.lang_for(main[0])
    if not L:
        info["warnings"].append("Limbaj necunoscut pentru %s" % main[0])
        return info
    info["lang"] = g(L, "id", "")
    info["langObj"] = L
    lexts = [ascii_lower(x) for x in str(g(L, "ext", "")).split()]
    if C.build_mode == "objects":
        info["sources"] = sorted([rp for rp, ap in files if ext_of(rp) in OBJ_EXTS and C.lang_for(rp) is not None])
    elif C.build_mode == "scripts":
        info["sources"] = sorted([rp for rp, ap in files if ext_of(rp) in OBJ_EXTS])
    elif C.compile_scope == "all":
        info["sources"] = sorted([rp for rp, ap in files if ext_of(rp) in lexts])
    else:
        info["sources"] = [main[0]]
    info["sizeMain"] = file_size(main[1])
    info["sizeAll"] = sum(file_size(ap) for rp, ap in files if ext_of(rp) in C.size_ext)
    info["sizeJudged"] = info["sizeAll"] if C.size_scope == "all" else info["sizeMain"]
    return info


def public_student(C, st, info):
    return {"id": st["id"], "name": st.get("name", ""), "dir": st["dir"], "exists": st["exists"],
            "registered": st.get("registered", False), "root": info["root"], "main": info["mainRel"],
            "lang": info["lang"], "sources": info["sources"], "sizeMain": info["sizeMain"],
            "sizeAll": info["sizeAll"], "sizeJudged": info["sizeJudged"], "warnings": info["warnings"]}


# =====================================================================================
# Compilare
# =====================================================================================

def collect_includes(paths):
    names = set()
    for p in paths:
        try:
            text = read_text(p, 4 * 1024 * 1024)
        except OSError:
            continue
        for line in text.splitlines():
            t = line.lstrip()
            if not t.startswith("#"):
                continue
            t = t[1:].lstrip()
            if not t.startswith("include"):
                continue
            t = t[7:].lstrip()
            if t.startswith("<"):
                e = t.find(">")
                if e > 1:
                    names.add(t[1:e].strip())
    return names


def expand_flags(text, job):
    """Opțiuni cu $(comandă) sau `comandă` (ex. $(wx-config --cxxflags --libs)), ca în scripturile cursului."""
    out = []
    i = 0
    text = str(text or "")
    while i < len(text):
        if text.startswith("$(", i) or text[i] == "`":
            end = text.find(")" if text[i] == "$" else "`", i + (2 if text[i] == "$" else 1))
            if end < 0:
                break
            cmd = text[i + (2 if text[i] == "$" else 1):end]
            if cmd not in job.cmd_cache:
                tmp = os.path.join(job.run_dir_any, "_cmd.txt")
                os.makedirs(job.run_dir_any, exist_ok=True)
                r = run_process(split_cmd(cmd), stdout_path=tmp, stderr_path=None, wall_ms=20000)
                val = read_text(tmp, 100000).strip() if (r["started"] and r["exited"] and r["exit"] == 0) else None
                job.cmd_cache[cmd] = val
                if val is None:
                    job.log("  Atenție: comanda „%s” a eșuat (%s); opțiunile ei nu se adaugă" % (cmd, status_text(r)))
            if job.cmd_cache[cmd]:
                out.extend(split_cmd(job.cmd_cache[cmd]))
            i = end + 1
        else:
            j = i
            while j < len(text) and not text.startswith("$(", j) and text[j] != "`":
                j += 1
            out.extend(split_cmd(text[i:j]))
            i = j
    return out


def compile_only_flags(flags):
    """Opțiunile potrivite pentru pasul de compilare (-c): fără -l, -L, -Wl,."""
    return [f for f in flags if not (f.startswith("-l") or f.startswith("-L") or f.startswith("-Wl,"))]


def auto_lib_flags(C, root, source_paths, job):
    if not C.autolibs_on:
        return []
    hdrs = [ap for rp, ap, isdir, d in walk_bfs(root, C.max_depth, C.ignore)
            if not isdir and ext_of(rp) in (".h", ".hpp", ".hh", ".hxx")]
    names = collect_includes(list(source_paths) + hdrs)
    flags = []
    for rule in C.autolibs:
        pats = str(g(rule, "headers", "")).split()
        if any(glob_full(p, n) for p in pats for n in names):
            for f in expand_flags(str(g(rule, "flags", "")), job):
                if f not in flags:
                    flags.append(f)
    return flags


def build_argv(template, sources, libs, exe, main, stack_bytes, problem):
    out = []
    for tok in split_cmd(template):
        if tok == "{sources}":
            out.extend(sources)
        elif tok == "{libs}":
            out.extend(libs)
        else:
            out.append(tok.replace("{exe}", exe).replace("{main}", main)
                       .replace("{stack}", str(stack_bytes)).replace("{problem}", problem))
    return out


def cmd_string(argv):
    return " ".join(('"%s"' % a) if (" " in a or not a) else a for a in argv)


def compile_program(C, L, sources_abs, main_abs, out_dir, cwd, include_probe, job, extra_libs=None):
    """Compilează; întoarce (ok, exe, log, ms, cmd)."""
    os.makedirs(out_dir, exist_ok=True)
    exe = os.path.join(out_dir, "prog" + C.exe_suffix)
    tmpl = str(g(L, "compile", "") or "")
    if not tmpl.strip():
        return True, "", "", 0.0, ""
    if os.path.exists(exe):
        try:
            os.remove(exe)
        except OSError:
            exe = os.path.join(out_dir, "prog-%d%s" % (int(time.time() * 1000) % 100000, C.exe_suffix))
    srcs = list(sources_abs)
    if include_probe and C.probe_mode(L) == "c":
        srcs.append(job.probe_path(C))
    libs = list(extra_libs or [])
    stack_bytes = int(C.stack_mb * 1024 * 1024)
    if IS_WIN and C.win_stack and C.stack_mb > 0:
        libs.append("-Wl,--stack=%d" % stack_bytes)
    libs += split_cmd(C.compile_extra)
    argv = build_argv(tmpl, srcs, libs, exe, main_abs, stack_bytes, C.name)
    log_path = os.path.join(out_dir, "compilare.txt")
    r = run_process(argv, cwd=cwd, stdout_path=log_path, stderr_path=log_path,
                    env_add=job.env_path_add, wall_ms=int(C.compile_timeout * 1000), cancel=job.cancelled)
    log = ""
    try:
        log = read_text(log_path, 20000)
    except OSError:
        pass
    ok = r["started"] and r["exited"] and r["exit"] == 0 and os.path.isfile(exe)
    if not r["started"]:
        log = (r["error"] + "\n" + log).strip()
    elif r["timedOut"]:
        log = ("Compilarea a depășit %d s\n" % C.compile_timeout + log).strip()
    elif not ok and not log:
        log = "Compilatorul s-a terminat cu: %s" % status_text(r)
    return ok, exe, log, r["wallMs"], cmd_string(argv)


def lang_compiler(L, C):
    """Compilatorul și opțiunile unui limbaj, luate din comanda lui de compilare (pentru modul obiecte)."""
    toks = split_cmd(str(g(L, "compile", "")))
    if not toks:
        return None, []
    flags = []
    skip = False
    for t in toks[1:]:
        if skip:
            skip = False
            continue
        if t == "-o":
            skip = True
            continue
        if t in ("{sources}", "{libs}") or "{exe}" in t or "{main}" in t:
            continue
        flags.append(t.replace("{stack}", str(int(C.stack_mb * 1024 * 1024))).replace("{problem}", C.name))
    return toks[0], flags


OBJ_EXTS = (".c", ".cpp", ".cc", ".cxx", ".c++")


def compile_objects(C, L_main, root, out_dir, job, libs):
    """Ca scripturile cursului: compile.sh (fiecare .c/.cpp -> obiect) + build.sh (legare cu gcc/g++)."""
    os.makedirs(out_dir, exist_ok=True)
    exe = os.path.join(out_dir, "prog" + C.exe_suffix)
    if os.path.exists(exe):
        try:
            os.remove(exe)
        except OSError:
            exe = os.path.join(out_dir, "prog-%d%s" % (int(time.time() * 1000) % 100000, C.exe_suffix))
    obj_dir = os.path.join(out_dir, "obj")
    clear_dir(obj_dir)
    files = sorted(rp for rp, ap, isdir, d in walk_bfs(root, C.max_depth, C.ignore) if not isdir)
    srcs = []
    for rp in files:
        if ext_of(rp) not in OBJ_EXTS:
            continue
        L = C.lang_for(rp)
        if L is not None and str(g(L, "compile", "") or "").strip():
            srcs.append((rp, L))
    log = []
    total_ms = 0.0
    ok = True
    objs = []
    cflags_extra = compile_only_flags(libs)
    log_path = os.path.join(out_dir, "compilare.txt")
    linker_lang = None
    for rp, L in srcs:
        comp, flags = lang_compiler(L, C)
        obj = join_rel(obj_dir, os.path.splitext(rp)[0] + ".o")
        os.makedirs(os.path.dirname(obj), exist_ok=True)
        argv = [comp] + flags + cflags_extra + ["-c", join_rel(root, rp), "-o", obj]
        r = run_process(argv, cwd=root, stdout_path=log_path, stderr_path=log_path, env_add=job.env_path_add,
                        wall_ms=int(C.compile_timeout * 1000), cancel=job.cancelled)
        total_ms += r["wallMs"]
        out = read_text(log_path, 20000) if os.path.isfile(log_path) else ""
        log.append("Compiled: %s -> %s" % (rp, os.path.relpath(obj, obj_dir).replace(os.sep, "/")))
        if out.strip():
            log.append(out.rstrip())
        if not (r["started"] and r["exited"] and r["exit"] == 0 and os.path.isfile(obj)):
            ok = False
            if not r["started"]:
                log.append(r["error"])
        objs.append(obj)
        if ext_of(rp) != ".c":
            linker_lang = L
    if not srcs:
        return False, "", "Nu am găsit fișiere .c sau .cpp în %s" % root, 0.0, ""
    if ok and C.probe_mode(L_main) == "c":
        Lc = C.lang_for("x.c") or L_main
        comp, flags = lang_compiler(Lc, C)
        pobj = os.path.join(out_dir, "olieval_probe.o")
        r = run_process([comp] + flags + ["-c", job.probe_path(C), "-o", pobj], cwd=out_dir, stdout_path=log_path,
                        stderr_path=log_path, env_add=job.env_path_add, wall_ms=int(C.compile_timeout * 1000))
        if r["started"] and r["exited"] and r["exit"] == 0 and os.path.isfile(pobj):
            objs.append(pobj)
    cmd = ""
    if ok:
        lk = linker_lang or (C.lang_for("x.c") or L_main)
        linker = lang_compiler(lk, C)[0]
        argv = [linker] + objs + ["-o", exe] + libs
        cmd = cmd_string(argv)
        r = run_process(argv, cwd=root, stdout_path=log_path, stderr_path=log_path, env_add=job.env_path_add,
                        wall_ms=int(C.compile_timeout * 1000), cancel=job.cancelled)
        total_ms += r["wallMs"]
        out = read_text(log_path, 20000) if os.path.isfile(log_path) else ""
        if out.strip():
            log.append(out.rstrip())
        ok = r["started"] and r["exited"] and r["exit"] == 0 and os.path.isfile(exe)
        log.append(("Build completed. Executable created at %s" % exe) if ok else "Legarea (build) a eșuat.")
    else:
        log.append("Compilarea a eșuat; nu se mai face legarea.")
    return ok, exe, "\n".join(log)[:20000], total_ms, "compile.sh + build.sh: " + (cmd or "%d surse" % len(srcs))


def find_scripts(root, build_cmd):
    if build_cmd.strip() and build_cmd.strip() != "auto":
        return build_cmd.strip()
    for d in ("bash-scripts/", "scripts/", ""):
        cs = join_rel(root, d + "compile.sh")
        bs = join_rel(root, d + "build.sh")
        if os.path.isfile(cs) and os.path.isfile(bs):
            return "bash %scompile.sh && bash %sbuild.sh" % (d, d)
    for d in ("bash-scripts/", "scripts/", ""):
        if os.path.isfile(join_rel(root, d + "build.sh")):
            return "bash %sbuild.sh" % d
    return None


def compile_scripts(C, root, job):
    """Rulează scripturile elevului (ca la cbr.sh, fără rulare); executabilul: buildExe (implicit release/program)."""
    cmd = find_scripts(root, C.build_cmd)
    if cmd is None:
        return False, "", "Nu am găsit scripturile compile.sh / build.sh (în bash-scripts/, scripts/ sau în rădăcina proiectului)", 0.0, ""
    exe_rel = subst(C.build_exe, problem=C.name, nr=C.nr)
    exe = join_rel(root, exe_rel)
    for x in (exe, exe + ".exe"):
        remove_file(x)
    out_dir = os.path.join(C.work_dir, "build", C.name, safe_name(job.student_now))
    os.makedirs(out_dir, exist_ok=True)
    log_path = os.path.join(out_dir, "compilare.txt")
    argv = ["cmd", "/c", cmd] if IS_WIN else ["sh", "-c", cmd]
    r = run_process(argv, cwd=root, stdout_path=log_path, stderr_path=log_path, env_add=job.env_path_add,
                    wall_ms=int(C.compile_timeout * 1000), cancel=job.cancelled)
    log = read_text(log_path, 20000) if os.path.isfile(log_path) else ""
    found = exe if os.path.isfile(exe) else (exe + ".exe" if os.path.isfile(exe + ".exe") else "")
    ok = r["started"] and not r["timedOut"] and bool(found)
    if not r["started"]:
        log = (r["error"] + "\n" + log).strip()
    elif r["timedOut"]:
        log = ("Scripturile au depășit %d s\n" % C.compile_timeout + log).strip()
    elif not found:
        log = (log.rstrip() + "\nScripturile nu au produs executabilul %s" % exe_rel).strip()
    return ok, found, log, r["wallMs"], cmd


def build_student(C, L, info, bdir, job, libs):
    """Compilează proiectul după modul ales: command / objects / scripts."""
    if not str(g(L, "compile", "") or "").strip():
        return compile_program(C, L, [], info["main"], bdir, info["root"], False, job)
    if C.build_mode == "objects":
        return compile_objects(C, L, info["root"], bdir, job, libs)
    if C.build_mode == "scripts":
        return compile_scripts(C, info["root"], job)
    srcs = [join_rel(info["root"], s) for s in info["sources"]]
    return compile_program(C, L, srcs, info["main"], bdir, info["root"], True, job, libs)


CALIB_SOURCES = {
    ".c": ('calibrare.c', '#include <stdio.h>\nint main(void)\n{\n    int x = 0;\n    FILE *f = fopen("calibrare.in", "r");\n'
           '    if (f != NULL) {\n        if (fscanf(f, "%d", &x) != 1) x = 0;\n        fclose(f);\n    }\n'
           '    printf("%d\\n", x * 0);\n    return 0;\n}\n'),
    ".cpp": ('calibrare.cpp', '#include <iostream>\n#include <fstream>\nint main()\n{\n    std::ifstream f("calibrare.in");\n'
             '    int x = 0;\n    f >> x;\n    std::cout << x * 0 << std::endl;\n    return 0;\n}\n'),
    ".py": ('calibrare.py', 'x = 0\nprint(x)\n'),
}
CALIB_SOURCES[".cc"] = ("calibrare.cc", CALIB_SOURCES[".cpp"][1])
CALIB_SOURCES[".cxx"] = ("calibrare.cxx", CALIB_SOURCES[".cpp"][1])


def calibrate(C, L, job):
    lid = str(g(L, "id", ""))
    exts = [ascii_lower(x) for x in str(g(L, "ext", "")).split()]
    src = None
    for e in exts:
        if e in CALIB_SOURCES:
            src = CALIB_SOURCES[e]
            break
    if not src:
        return 0
    d = os.path.join(C.work_dir, "calibrare", lid)
    os.makedirs(d, exist_ok=True)
    sp = os.path.join(d, src[0])
    write_text(sp, src[1])
    ok, exe, log, ms, cmd = compile_program(C, L, [sp], sp, d, d, C.build_mode != "scripts", job)
    if not ok:
        job.log("  Calibrare %s: compilarea a eșuat; memoria nu va fi calibrată." % lid)
        return 0
    argv = build_argv(str(g(L, "run", "{exe}")), [sp], [], exe, sp, 0, C.name)
    best = None
    probe = os.path.join(d, "probe.txt")
    for _ in range(3):
        remove_file(probe)
        r = run_process(argv, cwd=d, stdout_path=os.path.join(d, "out.txt"),
                        env_add=C.probe_env(L, probe, job), wall_ms=10000)
        pr = read_probe(probe)
        m = int(pr["rss_kb"]) if "rss_kb" in pr else (0 if C.probe_active(L, job) else int(r["peakKb"]))
        if r["started"] and r["exited"] and m > 0:
            best = m if best is None else min(best, m)
    job.log("  Calibrare %s: program gol = %s KB" % (lid, best if best is not None else "?"))
    return best or 0


# =====================================================================================
# Comparare și verificator
# =====================================================================================

WS_BYTES = b" \t\n\r\x0b\x0c"


def _short(b, n=60):
    s = b.decode("utf-8", errors="replace")
    return s if len(s) <= n else s[:n] + "…"


def _is_number(t):
    i, n = 0, len(t)
    if i < n and t[i:i + 1] in (b"+", b"-"):
        i += 1
    digits = 0
    while i < n and 48 <= t[i] <= 57:
        i += 1
        digits += 1
    if i < n and t[i:i + 1] == b".":
        i += 1
        while i < n and 48 <= t[i] <= 57:
            i += 1
            digits += 1
    if digits == 0:
        return False
    if i < n and t[i:i + 1] in (b"e", b"E"):
        i += 1
        if i < n and t[i:i + 1] in (b"+", b"-"):
            i += 1
        ed = 0
        while i < n and 48 <= t[i] <= 57:
            i += 1
            ed += 1
        if ed == 0:
            return False
    return i == n


def compare_output(C, out_bytes, ok_bytes):
    """Întoarce (f, detaliu)."""
    a = out_bytes.replace(b"\r", b"")
    b = ok_bytes.replace(b"\r", b"")
    if C.ignore_case:
        a, b = a.lower(), b.lower()
    mode = C.cmp_mode
    if mode == "exact":
        if a == b:
            return 1.0, ""
        n = min(len(a), len(b))
        i = 0
        while i < n and a[i] == b[i]:
            i += 1
        line = b[:i].count(b"\n") + 1
        return 0.0, "Diferență la linia %d (octetul %d)" % (line, i + 1)
    if mode in ("tokens", "real"):
        ta, tb = a.split(), b.split()
        for k in range(min(len(ta), len(tb))):
            x, y = ta[k], tb[k]
            if x == y:
                continue
            if mode == "real" and _is_number(x) and _is_number(y):
                fx, fy = float(x), float(y)
                if abs(fx - fy) <= C.epsilon * max(1.0, abs(fy)):
                    continue
            return 0.0, "Elementul %d: așteptat «%s», primit «%s»" % (k + 1, _short(y), _short(x))
        if len(ta) != len(tb):
            return 0.0, "Număr diferit de valori: așteptat %d, primit %d" % (len(tb), len(ta))
        return 1.0, ""
    # lines (implicit)
    la = [l.rstrip(b" \t") for l in a.split(b"\n")]
    lb = [l.rstrip(b" \t") for l in b.split(b"\n")]
    while la and la[-1] == b"":
        la.pop()
    while lb and lb[-1] == b"":
        lb.pop()
    for k in range(min(len(la), len(lb))):
        if la[k] != lb[k]:
            return 0.0, "Linia %d: așteptat «%s», primit «%s»" % (k + 1, _short(lb[k]), _short(la[k]))
    if len(la) != len(lb):
        return 0.0, "Număr diferit de linii: așteptat %d, primit %d" % (len(lb), len(la))
    return 1.0, ""


def prepare_checker(C, job):
    """Întoarce (argv_prefix, eroare)."""
    spec = C.checker.strip() or ""
    if not spec:
        for c in CHECKER_NAMES:
            if os.path.isfile(os.path.join(C.tests_dir, c)):
                spec = c
                break
    if not spec:
        return None, "Nu este setat niciun verificator"
    path = resolve(C.tests_dir, subst(spec, **C.v))
    if not os.path.isfile(path):
        found = find_program(spec) if ("/" not in spec and "\\" not in spec) else None
        if not found:
            return None, "Verificatorul nu există: %s" % path
        path = found
    e = ext_of(path)
    if e in (".c", ".cpp", ".cc", ".cxx"):
        L = C.lang_for(path)
        if not L:
            return None, "Nu știu să compilez verificatorul %s" % path
        d = os.path.join(C.work_dir, "verificator", C.name)
        ok, exe, log, ms, cmd = compile_program(C, L, [path], path, d, os.path.dirname(path), False, job)
        if not ok:
            return None, "Verificatorul nu se compilează:\n" + log
        return [exe], ""
    if e == ".py":
        L = C.lang_for(path)
        runt = str(g(L, "run", "python3 {main}")) if L else ("python {main}" if IS_WIN else "python3 {main}")
        return build_argv(runt, [path], [], "", path, 0, C.name), ""
    return [path], ""


def run_checker(C, job, checker_argv, in_path, ok_path, out_path):
    """Întoarce (f, puncte_sau_None, mesaj, eroare)."""
    cdir = os.path.join(job.run_dir(C), "check")
    clear_dir(cdir)
    names = {"in": C.name + ".in", "ok": C.name + ".ok", "out": C.name + ".out"}
    try:
        shutil.copyfile(in_path, os.path.join(cdir, names["in"]))
        shutil.copyfile(ok_path, os.path.join(cdir, names["ok"]))
        if out_path and os.path.isfile(out_path):
            shutil.copyfile(out_path, os.path.join(cdir, names["out"]))
    except OSError as e:
        return 0.0, None, "", "nu pot pregăti fișierele: %s" % e
    proto = C.protocol
    args_t = C.checker_args.strip()
    if not args_t:
        args_t = "" if proto == "oji" else ("{input} {ok} {output}" if proto == "fraction" else "{input} {output} {ok}")
    extra = [a.replace("{input}", os.path.join(cdir, names["in"])).replace("{ok}", os.path.join(cdir, names["ok"]))
             .replace("{output}", os.path.join(cdir, names["out"])).replace("{problem}", C.name)
             for a in split_cmd(args_t)]
    so, se = os.path.join(cdir, "_stdout.txt"), os.path.join(cdir, "_stderr.txt")
    r = run_process(list(checker_argv) + extra, cwd=cdir, stdout_path=so, stderr_path=se,
                    env_add=job.env_path_add, wall_ms=10000, cancel=job.cancelled)
    out = read_text(so, 10000).strip() if os.path.isfile(so) else ""
    err = read_text(se, 10000).strip() if os.path.isfile(se) else ""
    if not r["started"]:
        return 0.0, None, "", r["error"]
    if r["timedOut"]:
        return 0.0, None, "", "verificatorul a depășit 10 s"
    if proto == "exitcode":
        msg = (out + (" " + err if err else "")).strip()[:300]
        return (1.0 if r["exited"] and r["exit"] == 0 else 0.0), None, msg, ""
    first = out.split(None, 1)
    if not first:
        return 0.0, None, "", "verificatorul nu a afișat nimic" + ((": " + err[:200]) if err else "")
    try:
        val = float(first[0].replace(",", "."))
    except ValueError:
        return 0.0, None, "", "verificatorul nu afișează în formatul 'punctaj mesaj': " + out[:200]
    msg = (first[1] if len(first) > 1 else "").strip()
    if proto == "fraction":
        if err:
            msg = (msg + " " + err).strip()
        return max(0.0, min(1.0, val)), None, msg[:300], ""
    return 0.0, val, msg[:300], ""  # oji: punctele se transformă în fracție la punctare


# =====================================================================================
# Punctare
# =====================================================================================

FATAL = ("RE", "TLE", "MLE", "NOOUT", "CHKERR", "SKIP")


def policy_factor(pol, v, L, H):
    mode = g(pol, "mode", "zero")
    if mode == "ignore":
        return 1.0
    if mode == "percent":
        return max(0.0, min(100.0, num(g(pol, "percent", 50), 50))) / 100.0
    if mode == "linear":
        if H > L:
            return max(0.0, min(1.0, 1.0 - (v - L) / (H - L)))
        return 0.0
    return 0.0


def policy_note(pol, factor):
    mode = g(pol, "mode", "zero")
    if mode == "ignore":
        return " (ignorat)"
    if mode == "zero" or factor <= 0:
        return " (0 puncte)"
    return " (punctaj redus la %s%%)" % fmt_int(factor * 100)


def score_test(C, t, tr):
    P = num(g(t, "points", 0))
    TP = num(g(t, "timePoints", 0))
    MP = num(g(t, "memPoints", 0))
    tr["max"] = round2(P + TP + MP)
    raw = tr.get("raw", "SKIP")
    msg0 = tr.get("msg0", "")
    if raw in FATAL:
        tr["status"] = raw
        tr["score"] = 0
        tr["msg"] = msg0
        return 0.0
    pts = tr.get("pts")
    if pts is not None:
        base = max(0.0, min(pts, P)) if P > 0 else max(0.0, pts)
        f = (base / P) if P > 0 else (1.0 if pts > 0 else 0.0)
    else:
        f = max(0.0, min(1.0, num(tr.get("f"), 0)))
        base = f * P
    Lt = num(g(t, "timeMs", 0)) or C.time_ms
    Lm = num(g(t, "memMb", 0)) or C.mem_mb
    tval = num(tr.get("wallMs")) if C.time_mode == "wall" else num(tr.get("cpuMs"))
    mval = num(tr.get("memKb"))
    over_t = Lt > 0 and tval > Lt
    over_m = Lm > 0 and tr.get("memKb") is not None and mval > Lm * 1024
    pol = g(C.prob, "policy", {})
    ft = policy_factor(g(pol, "time", {}), tval, Lt, Lt * C.hard_time) if over_t else 1.0
    fm = policy_factor(g(pol, "memory", {}), mval, Lm * 1024, Lm * 1024 * C.hard_mem) if over_m else 1.0
    score = base * ft * fm + ((f * TP) if not over_t else 0.0) + ((f * MP) if not over_m else 0.0)
    score = round2(score)
    status = raw
    msg = msg0
    if over_t and g(g(pol, "time", {}), "mode", "zero") != "ignore":
        status = "TLE"
        msg = "Timp depășit: %s ms > %s ms%s" % (fmt_int(tval), fmt_int(Lt), policy_note(g(pol, "time", {}), ft))
        if raw != "OK":
            msg += "; " + msg0
    elif over_m and g(g(pol, "memory", {}), "mode", "zero") != "ignore":
        status = "MLE"
        msg = "Memorie depășită: %s KB > %s KB%s" % (fmt_int(mval), fmt_int(Lm * 1024), policy_note(g(pol, "memory", {}), fm))
        if raw != "OK":
            msg += "; " + msg0
    else:
        notes = []
        if over_t:
            notes.append("timp depășit (ignorat)")
        if over_m:
            notes.append("memorie depășită (ignorat)")
        if notes:
            msg = msg0 + "; " + ", ".join(notes)
    tr["status"] = status
    tr["score"] = score
    tr["msg"] = msg
    return score


def rescore(ws, prob, res):
    C = Cfg(ws, prob)
    tests = g(prob, "tests", []) or []
    pts = g(prob, "points", {})
    p_compile = num(g(pts, "compile", 0))
    p_size = num(g(pts, "size", 0))
    max_score = sum(num(g(t, "points", 0)) + num(g(t, "timePoints", 0)) + num(g(t, "memPoints", 0)) for t in tests)
    max_score = round2(max_score + p_compile + p_size)
    res["maxScore"] = max_score
    res["tests"] = [{"id": str(g(t, "id", "")), "points": num(g(t, "points", 0)),
                     "timePoints": num(g(t, "timePoints", 0)), "memPoints": num(g(t, "memPoints", 0))} for t in tests]
    res["limits"] = {"timeMs": C.time_ms, "memMb": C.mem_mb, "stackMb": C.stack_mb, "sourceKb": C.source_kb,
                     "timeMode": C.time_mode}
    pol_size = g(g(prob, "policy", {}), "size", {})
    for sr in res.get("students", []):
        sr["max"] = max_score
        notes = [n for n in sr.get("notes", []) if not n.startswith("Sursa depășește")]
        if sr.get("status") in ("CE", "NOSRC", "ERR"):
            sr["score"] = 0
            sr["notes"] = notes
            continue
        bytid = {str(tr.get("id")): tr for tr in sr.get("tests", [])}
        newtests = []
        total = 0.0
        for t in tests:
            tid = str(g(t, "id", ""))
            tr = bytid.get(tid)
            if tr is None:
                tr = {"id": tid, "raw": "SKIP", "msg0": "Neevaluat", "f": 0, "pts": None}
            total += score_test(C, t, tr)
            newtests.append(tr)
        sr["tests"] = newtests
        total += p_compile
        size_ok = C.source_kb <= 0 or num(sr.get("sizeJudged")) <= C.source_kb * 1024
        sr["sizeOk"] = size_ok
        if size_ok:
            total += p_size
        else:
            mode = g(pol_size, "mode", "zero")
            lim = "Sursa depășește limita: %s KB > %s KB" % (("%.2f" % (num(sr.get("sizeJudged")) / 1024.0)), ("%g" % C.source_kb))
            if mode == "zero":
                total = 0.0
                lim += " (0 puncte)"
            elif mode == "percent":
                p = max(0.0, min(100.0, num(g(pol_size, "percent", 50), 50)))
                total = total * p / 100.0
                lim += " (punctaj redus la %s%%)" % fmt_int(p)
            elif mode == "penalty":
                pen = num(g(pol_size, "penalty", 10), 10)
                total = max(0.0, total - pen)
                lim += " (penalizare %g puncte)" % pen
            else:
                lim += " (ignorat)"
            notes.append(lim)
        sr["notes"] = notes
        sr["score"] = round2(total)
    return res


# =====================================================================================
# Evaluare (job în fundal)
# =====================================================================================

class Job:
    def __init__(self):
        self.lock = threading.Lock()
        self.running = False
        self.stopping = False
        self.phase = ""
        self.problem = ""
        self.student = ""
        self.test = ""
        self.done = 0
        self.total = 0
        self.logs = []
        self.rv = 0
        self.results = {}
        self.error = ""
        self.env_path_add = []
        self.current_probe = ""
        self.py_probe_dir = ""
        self.cmd_cache = {}
        self.preload_so = ""
        self.run_dir_any = ""
        self.student_now = ""
        self.checker_argv = None
        self.checker_error = ""

    def log(self, msg):
        with self.lock:
            self.logs.append(time.strftime("%H:%M:%S ") + msg)
            if len(self.logs) > 20000:
                self.logs = self.logs[-10000:]

    def cancelled(self):
        return self.stopping

    def set(self, **kv):
        with self.lock:
            for k, v in kv.items():
                setattr(self, k, v)

    def publish(self, pname, res):
        data = json.loads(json.dumps(res))
        with self.lock:
            self.results[pname] = data
            self.rv += 1

    def probe_path(self, C):
        return self.current_probe

    def run_dir(self, C):
        return os.path.join(C.work_dir, "run", C.name)


JOB = Job()


def safe_name(s):
    return "".join(c if (c.isalnum() or c in "-_.") else "_" for c in s) or "_"


def load_results(C):
    p = os.path.join(C.results_dir, C.name, "ultimele.json")
    try:
        data = json.loads(read_text(p).lstrip("\ufeff"))
        if isinstance(data, dict) and isinstance(data.get("students"), list):
            return data
    except (OSError, ValueError):
        pass
    return None


def save_results(C, res, snapshot=False):
    d = os.path.join(C.results_dir, C.name)
    os.makedirs(d, exist_ok=True)
    text = json.dumps(res, ensure_ascii=False, indent=1)
    write_text(os.path.join(d, "ultimele.json"), text)
    if snapshot:
        write_text(os.path.join(d, "rezultate-%s.json" % time.strftime("%Y%m%d-%H%M%S")), text)


def run_test(C, job, st, info, L, exe, t, baseline):
    tid = str(g(t, "id", ""))
    tr = {"id": tid, "raw": "RE", "msg0": "", "detail": "", "f": 0, "pts": None, "cpuMs": 0, "wallMs": 0,
          "timeMs": 0, "memKb": 0, "memRawKb": 0, "stackKb": None, "exit": 0, "signal": 0, "timedOut": False,
          "stderr": "", "out": "", "err": ""}
    in_src = resolve(C.tests_dir, str(g(t, "input", "")))
    ok_src = resolve(C.tests_dir, str(g(t, "ok", "")))
    if not os.path.isfile(in_src):
        tr["raw"], tr["msg0"] = "CHKERR", "Lipsește fișierul de intrare al testului: %s" % in_src
        return tr
    rdir = job.run_dir(C)
    os.makedirs(rdir, exist_ok=True)
    if C.run_in == "project":
        sroot = info["root"]
    else:
        sroot = os.path.join(rdir, "sandbox")
        clear_dir(sroot)
    cwd = resolve(sroot, subst(C.io_cwd, problem=C.name, nr=C.nr, n=tid))
    os.makedirs(cwd, exist_ok=True)
    stdin_path = None
    if C.io_input == "stdin":
        stdin_path = in_src
    else:
        dst = resolve(sroot, subst(C.io_inpath, problem=C.name, nr=C.nr, n=tid))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(in_src, dst)
    so = os.path.join(rdir, "stdout.txt")
    se = os.path.join(rdir, "stderr.txt")
    probe = os.path.join(rdir, "probe.txt")
    remove_file(probe)
    if C.io_output == "stdout":
        out_path = so
    else:
        out_path = resolve(sroot, subst(C.io_outpath, problem=C.name, nr=C.nr, n=tid))
        remove_file(out_path)
    Lt = num(g(t, "timeMs", 0)) or C.time_ms
    Lm = num(g(t, "memMb", 0)) or C.mem_mb
    hard_wall = (Lt * C.hard_time + 500) if Lt > 0 else 60000
    cpu_s = int(math.ceil(hard_wall / 1000.0)) + 1
    mem_hard = int(Lm * 1024 * C.hard_mem + 65536) if Lm > 0 else 0
    stack_kb = int(C.stack_mb * 1024) if C.stack_mb > 0 else 0
    fsize_kb = int(C.output_mb * 1024) if C.output_mb > 0 else 0
    argv = build_argv(str(g(L, "run", "{exe}")), [], [], exe, info["main"], 0, C.name)
    r = run_process(argv, cwd=cwd, stdin_path=stdin_path, stdout_path=so, stderr_path=se,
                    env_add=C.probe_env(L, probe, job),
                    wall_ms=int(hard_wall), cpu_s=cpu_s, mem_kb=mem_hard, stack_kb=stack_kb,
                    fsize_kb=fsize_kb, single_process=True, cancel=job.cancelled)
    pr = read_probe(probe)
    if "rss_kb" in pr:
        mem_raw = int(pr["rss_kb"])
    elif C.probe_active(L, job) and not IS_WIN:
        mem_raw = None  # programul nu s-a terminat normal: măsurarea din exterior nu e de încredere pe Linux
    else:
        mem_raw = int(r["peakKb"])
    base = baseline if C.calibrate else 0
    tr["cpuMs"] = round(r["cpuMs"] if r["cpuMs"] > 0 else num(pr.get("cpu_ms")), 3)
    tr["wallMs"] = round(r["wallMs"], 3)
    tr["timeMs"] = tr["wallMs"] if C.time_mode == "wall" else tr["cpuMs"]
    tr["memRawKb"] = mem_raw
    tr["memKb"] = max(0, mem_raw - base) if mem_raw is not None else None
    tr["stackKb"] = int(pr["stack_kb"]) if "stack_kb" in pr else None
    tr["exit"] = r["exit"]
    tr["signal"] = r["signal"]
    tr["timedOut"] = r["timedOut"]
    try:
        tr["stderr"] = read_text(se, 2000) if os.path.isfile(se) else ""
    except OSError:
        tr["stderr"] = ""
    low_err = tr["stderr"].lower()
    if C.keep_outputs:
        kd = os.path.join(C.work_dir, "iesiri", C.name, st["id"])
        os.makedirs(kd, exist_ok=True)
        ko, ke = os.path.join(kd, safe_name(tid) + ".out"), os.path.join(kd, safe_name(tid) + ".err")
        remove_file(ko)
        remove_file(ke)
        try:
            if os.path.isfile(out_path):
                shutil.copyfile(out_path, ko)
                tr["out"] = ko
            if file_size(se) > 0:
                shutil.copyfile(se, ke)
                tr["err"] = ke
        except OSError:
            pass
    if r["cancelled"]:
        tr["raw"], tr["msg0"] = "SKIP", "Oprit de utilizator"
        return tr
    if not r["started"]:
        tr["raw"], tr["msg0"] = "RE", "Programul nu a putut fi pornit: " + r["error"]
        return tr
    if r["timedOut"]:
        tr["raw"] = "TLE"
        tr["msg0"] = "Timp depășit: oprit după %s ms" % fmt_int(r["wallMs"])
        if r["cpuMs"] < 0.3 * r["wallMs"]:
            tr["msg0"] += " (programul pare să aștepte, ex. citire de la tastatură)"
        return tr
    crashed = (not r["exited"]) or (r["exit"] != 0 and C.nonzero_error)
    if crashed:
        mem_fail = ("bad_alloc" in low_err or "memoryerror" in low_err or "out of memory" in low_err
                    or r["memLimitHit"] or r["winStatus"] in (0xC0000017, 0xC000012D)
                    or (mem_hard > 0 and r["peakKb"] >= 0.95 * mem_hard))
        if mem_fail:
            tr["raw"], tr["msg0"] = "MLE", "Memorie depășită (alocarea a eșuat)"
        else:
            tr["raw"], tr["msg0"] = "RE", "Eroare la execuție: " + status_text(r)
        return tr
    if not os.path.isfile(out_path):
        tr["raw"], tr["msg0"] = "NOOUT", "Lipsește fișierul de ieșire %s" % os.path.basename(out_path)
        return tr
    if C.output_mb > 0 and file_size(out_path) > C.output_mb * 1024 * 1024:
        tr["raw"], tr["msg0"] = "WA", "Ieșire prea mare (peste %g MB)" % C.output_mb
        return tr
    if C.cmp_mode == "checker":
        if job.checker_argv is None:
            tr["raw"], tr["msg0"] = "CHKERR", "Eroare verificator: " + job.checker_error
            return tr
        f, pts, msg, err = run_checker(C, job, job.checker_argv, in_src, ok_src, out_path)
        if err:
            tr["raw"], tr["msg0"] = "CHKERR", "Eroare verificator: " + err
            return tr
        tr["pts"] = pts
        if pts is not None:
            P = num(g(t, "points", 0))
            f = (max(0.0, min(pts, P)) / P) if P > 0 else (1.0 if pts > 0 else 0.0)
        tr["f"] = f
        tr["raw"] = "OK" if f >= 1 else ("PARTIAL" if f > 0 else "WA")
        tr["msg0"] = ("Corect" if f >= 1 else ("Parțial" if f > 0 else "Incorect")) + ((": " + msg) if msg else "")
        return tr
    if not os.path.isfile(ok_src):
        tr["raw"], tr["msg0"] = "CHKERR", "Lipsește fișierul corect al testului: %s" % ok_src
        return tr
    try:
        f, detail = compare_output(C, read_bytes(out_path), read_bytes(ok_src))
    except OSError as e:
        tr["raw"], tr["msg0"] = "CHKERR", "Nu pot citi ieșirea: %s" % e
        return tr
    tr["f"] = f
    tr["raw"] = "OK" if f >= 1 else "WA"
    tr["msg0"] = "Corect" if f >= 1 else "Incorect"
    tr["detail"] = detail
    return tr


def evaluate_problem(job, ws, prob, only_students, only_tests):
    C = Cfg(ws, prob)
    job.set(problem=C.name, phase="pregătire", student="", test="")
    tests = [t for t in (g(prob, "tests", []) or []) if not only_tests or str(g(t, "id", "")) in only_tests]
    job.log("=== Problema %s ===" % C.name)
    if not tests:
        job.log("Nu există teste în barem. Folosește «Detectează testele» în tabul Teste & barem.")
        return
    students = discover_students(C)
    if only_students:
        students = [s for s in students if s["id"] in only_students]
    with job.lock:
        job.total += len(students) * (len(tests) + 1)
    res = load_results(C) or {}
    res.update({"format": "evaluator-olimpiada/rezultate", "version": 1, "problem": C.name,
                "variant": VARIANT, "os": OS_NAME, "updated": now_str()})
    res.setdefault("students", [])
    res["baseline"] = {}
    for d in (C.work_dir, job.run_dir(C)):
        os.makedirs(d, exist_ok=True)
    job.current_probe = os.path.join(C.work_dir, "olieval_probe.c")
    job.run_dir_any = job.run_dir(C)
    job.cmd_cache = {}
    job.py_probe_dir = os.path.join(C.work_dir, "sonda-py")
    try:
        write_text(job.current_probe, PROBE_SOURCE)
        write_text(os.path.join(job.py_probe_dir, "sitecustomize.py"), PY_PROBE_SOURCE)
    except OSError as e:
        job.log("Nu pot scrie sonda: %s" % e)
    job.preload_so = ""
    if C.build_mode == "scripts" and OS_NAME == "linux" and C.use_probe:
        so = os.path.join(C.work_dir, "sonda", "olieval_probe.so")
        os.makedirs(os.path.dirname(so), exist_ok=True)
        Lc = C.lang_for("x.c")
        cc = (lang_compiler(Lc, C)[0] if Lc else None) or "gcc"
        r = run_process([cc, "-O2", "-shared", "-fPIC", "-o", so, job.current_probe],
                        stdout_path=os.path.join(job.run_dir(C), "_sonda.txt"), stderr_path=os.path.join(job.run_dir(C), "_sonda.txt"),
                        wall_ms=60000)
        if r["started"] and r["exited"] and r["exit"] == 0 and os.path.isfile(so):
            job.preload_so = so
        else:
            job.log("Sonda LD_PRELOAD nu s-a putut compila; memoria se măsoară din exterior.")
    # PATH: folderul compilatorului (pentru DLL-urile MinGW pe Windows)
    job.env_path_add = []
    if IS_WIN:
        dirs = []
        for L in C.languages:
            toks = split_cmd(str(g(L, "compile", "")))
            if toks:
                fp = find_program(toks[0])
                if fp and os.path.dirname(fp) not in dirs:
                    dirs.append(os.path.dirname(fp))
        if dirs:
            job.env_path_add = ["PATH=" + os.pathsep.join(dirs) + os.pathsep + os.environ.get("PATH", "")]
    job.checker_argv, job.checker_error = None, ""
    if C.cmp_mode == "checker":
        job.checker_argv, job.checker_error = prepare_checker(C, job)
        if job.checker_argv is None:
            job.log("Verificator: " + job.checker_error)
        else:
            job.log("Verificator: " + cmd_string(job.checker_argv))
    publish_all = lambda: job.publish(C.name, res)
    rescore(ws, prob, res)
    publish_all()
    for st in students:
        if job.stopping:
            break
        job.set(student=st["id"], test="", phase="compilare")
        job.log("Elev %s" % st["id"])
        with job.lock:
            start_done = job.done
        sr = {"id": st["id"], "name": st.get("name", ""), "dir": st["dir"], "root": "", "main": "", "lang": "",
              "sources": [], "sizeMain": 0, "sizeAll": 0, "sizeJudged": 0, "sizeOk": True,
              "compile": {"ok": False, "ms": 0, "log": "", "cmd": ""}, "status": "", "score": 0, "max": 0,
              "notes": [], "evaluatedAt": now_str(), "tests": []}
        try:
            if not st["exists"]:
                sr["status"] = "NOSRC"
                sr["notes"].append("Lipsă sursă (folderul sau fișierul elevului nu există)")
                job.log("  lipsă sursă")
            else:
                info = detect_project(C, st)
                sr.update({"root": info["root"], "main": info["mainRel"], "lang": info["lang"],
                           "sources": info["sources"], "sizeMain": info["sizeMain"], "sizeAll": info["sizeAll"],
                           "sizeJudged": info["sizeJudged"]})
                sr["notes"].extend(info["warnings"])
                L = info["langObj"]
                if not info["main"] or not L:
                    sr["status"] = "NOSRC"
                    job.log("  nu am găsit sursa")
                else:
                    if C.build_mode == "objects":
                        srcs = [ap for rp, ap, isdir, d in walk_bfs(info["root"], C.max_depth, C.ignore)
                                if not isdir and ext_of(rp) in OBJ_EXTS]
                    else:
                        srcs = [join_rel(info["root"], s) for s in info["sources"]]
                    libs = auto_lib_flags(C, info["root"], srcs, job)
                    bdir = os.path.join(C.work_dir, "build", C.name, safe_name(st["id"]))
                    job.student_now = st["id"]
                    ok, exe, log, ms, cmd = build_student(C, L, info, bdir, job, libs)
                    sr["compile"] = {"ok": ok, "ms": round(ms, 1), "log": log, "cmd": cmd}
                    with job.lock:
                        job.done += 1
                    if not ok:
                        sr["status"] = "CE"
                        job.log("  eroare de compilare")
                    else:
                        lid = str(g(L, "id", ""))
                        if C.calibrate and lid not in res["baseline"]:
                            res["baseline"][lid] = calibrate(C, L, job)
                        baseline = int(res["baseline"].get(lid, 0)) if C.calibrate else 0
                        job.set(phase="rulare")
                        for t in tests:
                            if job.stopping:
                                break
                            job.set(test=str(g(t, "id", "")))
                            tr = run_test(C, job, st, info, L, exe, t, baseline)
                            sr["tests"].append(tr)
                            with job.lock:
                                job.done += 1
                            job.log("  test %s: %s %s" % (tr["id"], tr["raw"], tr["msg0"]))
                        sr["status"] = "DONE"
        except Exception as e:  # nu oprim tot jobul din cauza unui elev
            sr["status"] = "ERR"
            sr["notes"].append("Eroare internă: %s" % e)
            job.log("  EROARE: %s" % traceback.format_exc())
        with job.lock:
            job.done = min(job.total, start_done + len(tests) + 1)
        # combinare cu rezultatele existente
        prev = None
        for i, x in enumerate(res["students"]):
            if x.get("id") == sr["id"]:
                prev = i
                break
        if prev is not None and only_tests and sr["status"] == "DONE" and res["students"][prev].get("status") == "DONE":
            old = {str(x.get("id")): x for x in res["students"][prev].get("tests", [])}
            for tr in sr["tests"]:
                old[tr["id"]] = tr
            sr["tests"] = list(old.values())
        if prev is None:
            res["students"].append(sr)
        else:
            res["students"][prev] = sr
        res["students"].sort(key=lambda x: nat_key(str(x.get("id", ""))))
        res["updated"] = now_str()
        rescore(ws, prob, res)
        try:
            save_results(C, res)
        except OSError as e:
            job.log("Nu pot salva rezultatele: %s" % e)
        publish_all()
    try:
        save_results(C, res, snapshot=True)
    except OSError as e:
        job.log("Nu pot salva rezultatele: %s" % e)
    publish_all()


def job_thread(ws, probs, only_students, only_tests):
    try:
        for pname in probs:
            if JOB.stopping:
                break
            prob = find_problem(ws, pname)
            if prob is None:
                JOB.log("Problema %s nu există" % pname)
                continue
            evaluate_problem(JOB, ws, prob, only_students, only_tests)
        JOB.log("Evaluare oprită." if JOB.stopping else "Evaluare terminată.")
    except Exception:
        JOB.log("EROARE: " + traceback.format_exc())
        JOB.set(error="Eroare internă")
    finally:
        JOB.set(running=False, phase="oprit" if JOB.stopping else "gata", student="", test="")
        with JOB.lock:
            JOB.done = JOB.total
            JOB.rv += 1


# =====================================================================================
# Structura de foldere
# =====================================================================================

def structure(ws, create):
    root = os.path.normpath(os.path.abspath(g(ws, "root", "") or "."))
    items = []

    def add(p, kind):
        if all(x["path"] != p for x in items):
            items.append({"path": p, "kind": kind, "exists": os.path.isdir(p)})

    probs = g(ws, "problems", []) or []
    C0 = Cfg(ws, probs[0] if probs else {"name": "_"})
    add(C0.work_dir, "lucru")
    add(C0.results_dir, "rezultate")
    studs = [str(g(s, "id", "")).strip() for s in (g(ws, "students", []) or [])]
    studs = [s for s in studs if s]
    for p in probs:
        C = Cfg(ws, p)
        if C.layout == "flat":
            add(resolve(root, subst(g(ws, "flatDir", "{root}/{problem}/surse"), **C.v)), "surse")
        else:
            for s in studs:
                add(resolve(root, subst(g(ws, "studentDir", "{root}/{problem}/{student}"), student=s, **C.v)), "elev")
        add(C.tests_dir, "teste")
    created, errors = 0, []
    if create:
        for it in items:
            if not it["exists"]:
                try:
                    os.makedirs(it["path"], exist_ok=True)
                    it["exists"] = True
                    created += 1
                except OSError as e:
                    errors.append("%s: %s" % (it["path"], e))
    return {"items": items, "created": created, "errors": errors}


# =====================================================================================
# Server HTTP
# =====================================================================================

TOKEN = secrets.token_hex(16)
PORT = 8765
SERVER = None


def load_file_near(name):
    for p in (os.path.join(HERE, name), os.path.join(HERE, "..", "gui", name), os.path.join(HERE, "..", "common", name)):
        if os.path.isfile(p):
            return read_text(p)
    return None


GUI_HTML = load_file_near("index.html") or "<h1>Lipsește index.html</h1>"
PROBE_SOURCE = load_file_near("olieval_probe.c") or ""
PY_PROBE_SOURCE = load_file_near("sitecustomize.py") or ""


def settings_path():
    return os.path.join(os.path.expanduser("~"), ".evaluator-olimpiada", "setari.json")


def api_info():
    py = "python" if IS_WIN else "python3"
    return {"app": APP_NAME, "version": VERSION, "variant": VARIANT, "os": OS_NAME, "sep": os.sep,
            "home": os.path.expanduser("~"), "settings": settings_path(), "cwd": os.getcwd(),
            "python": py, "pid": os.getpid(),
            "compilers": {"gcc": find_program("gcc") or "", "g++": find_program("g++") or "",
                          "python": find_program(py) or ""},
            "features": {"runner": "native", "probe": bool(PROBE_SOURCE), "memLimit": True, "cpuTime": True},
            "example": find_example()}


def find_example():
    for p in (os.path.join(HERE, "exemplu"), os.path.join(HERE, "..", "exemplu"), os.path.join(HERE, "..", "..", "exemplu")):
        if os.path.isfile(os.path.join(p, "evaluator.json")):
            return os.path.normpath(os.path.abspath(p))
    return ""


def win_roots():
    out = []
    for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        d = c + ":\\"
        if os.path.exists(d):
            out.append(d)
    return out


def api_fs_list(body):
    path = str(body.get("path") or "") or os.path.expanduser("~")
    path = os.path.abspath(os.path.expanduser(path))
    if not os.path.isdir(path):
        raise ValueError("Folderul nu există: %s" % path)
    entries = []
    for name in os.listdir(path):
        p = os.path.join(path, name)
        try:
            stt = os.stat(p)
            isdir = os.path.isdir(p)
            entries.append({"name": name, "dir": isdir, "size": 0 if isdir else stt.st_size, "mtime": int(stt.st_mtime)})
        except OSError:
            continue
    entries.sort(key=lambda e: (0 if e["dir"] else 1, nat_key(e["name"])))
    parent = os.path.dirname(path)
    return {"path": path, "parent": parent if parent != path else "", "sep": os.sep, "entries": entries,
            "roots": win_roots() if IS_WIN else ["/"]}


def api_fs_read(body):
    path = str(body.get("path") or "")
    mx = int(num(body.get("max"), 2000000))
    if not os.path.isfile(path):
        raise ValueError("Fișierul nu există: %s" % path)
    size = os.path.getsize(path)
    data = read_bytes(path, mx)
    return {"path": os.path.abspath(path), "size": size, "text": data.decode("utf-8", errors="replace"),
            "truncated": size > mx}


def api_fs_write(body):
    path = str(body.get("path") or "")
    if not path:
        raise ValueError("Cale lipsă")
    write_text(os.path.abspath(path), str(body.get("text") or ""))
    return {"ok": True}


def api_fs_stat(body):
    path = str(body.get("path") or "")
    try:
        stt = os.stat(path)
        return {"exists": True, "dir": os.path.isdir(path), "size": stt.st_size, "mtime": int(stt.st_mtime)}
    except OSError:
        return {"exists": False, "dir": False, "size": 0, "mtime": 0}


def api_fs_mkdir(body):
    path = str(body.get("path") or "")
    if not path:
        raise ValueError("Cale lipsă")
    os.makedirs(path, exist_ok=True)
    return {"ok": True}


def api_open(body):
    path = str(body.get("path") or "")
    if not os.path.exists(path):
        raise ValueError("Nu există: %s" % path)
    if IS_WIN:
        os.startfile(path)  # type: ignore[attr-defined]
    elif sys.platform == "darwin":
        subprocess.Popen(["open", path])
    else:
        subprocess.Popen(["xdg-open", path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return {"ok": True}


def api_scan(body):
    ws = body.get("workspace") or {}
    pname = body.get("problem") or ""
    prob = find_problem(ws, pname)
    if prob is None:
        raise ValueError("Problema nu există: %s" % pname)
    C = Cfg(ws, prob)
    res = scan_tests(C)
    studs = []
    for st in discover_students(C):
        if st["exists"]:
            info = detect_project(C, st)
        else:
            info = {"root": "", "mainRel": "", "lang": "", "sources": [], "sizeMain": 0, "sizeAll": 0,
                    "sizeJudged": 0, "warnings": ["Folderul/fișierul elevului nu există"]}
        studs.append(public_student(C, st, info))
    res["students"] = studs
    return res


def api_eval_start(body):
    ws = body.get("workspace") or {}
    probs = body.get("problems") or []
    if isinstance(probs, str):
        probs = [probs]
    with JOB.lock:
        if JOB.running:
            raise ValueError("O evaluare este deja în desfășurare")
        JOB.running = True
        JOB.stopping = False
        JOB.phase = "pornire"
        JOB.done = 0
        JOB.total = 0
        JOB.logs = []
        JOB.results = {}
        JOB.error = ""
        JOB.rv += 1
    only_s = body.get("students") or None
    only_t = body.get("tests") or None
    if only_s is not None:
        only_s = [str(x) for x in only_s]
    if only_t is not None:
        only_t = [str(x) for x in only_t]
    th = threading.Thread(target=job_thread, args=(ws, [str(p) for p in probs], only_s, only_t), daemon=True)
    th.start()
    return {"ok": True}


def api_eval_status(query):
    since = int(num((query.get("log") or ["0"])[0], 0))
    rv = int(num((query.get("rv") or ["-1"])[0], -1))
    with JOB.lock:
        out = {"running": JOB.running, "stopping": JOB.stopping, "phase": JOB.phase, "problem": JOB.problem,
               "student": JOB.student, "test": JOB.test, "done": JOB.done, "total": JOB.total,
               "log": JOB.logs[since:], "logNext": len(JOB.logs), "rv": JOB.rv, "error": JOB.error}
        if rv != JOB.rv:
            out["results"] = JOB.results
    return out


def api_rescore(body):
    ws = body.get("workspace") or {}
    prob = find_problem(ws, body.get("problem") or "")
    if prob is None:
        raise ValueError("Problema nu există")
    res = body.get("results") or {}
    return {"results": rescore(ws, prob, res)}


class Handler(BaseHTTPRequestHandler):
    server_version = "EvaluatorOlimpiada"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def _json(self, code, obj):
        self._send(code, json.dumps(obj, ensure_ascii=False))

    def _host_ok(self):
        h = (self.headers.get("Host") or "").strip()
        return h in ("127.0.0.1:%d" % PORT, "localhost:%d" % PORT)

    def _auth(self):
        return self._host_ok() and self.headers.get("X-Eval-Token") == TOKEN

    def do_GET(self):
        u = urlparse(self.path)
        if not self._host_ok():
            return self._send(403, "Acces interzis", "text/plain; charset=utf-8")
        if u.path in ("/", "/index.html"):
            return self._send(200, GUI_HTML.replace("__EVAL_TOKEN__", TOKEN), "text/html; charset=utf-8")
        if u.path == "/api/info" and self._auth():
            return self._json(200, api_info())
        if u.path == "/api/eval/status" and self._auth():
            return self._json(200, api_eval_status(parse_qs(u.query)))
        if u.path.startswith("/api/"):
            return self._json(403, {"error": "Acces interzis"})
        return self._send(404, "Nu există", "text/plain; charset=utf-8")

    def do_POST(self):
        u = urlparse(self.path)
        if not self._auth():
            return self._json(403, {"error": "Acces interzis"})
        try:
            n = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(n) if n > 0 else b"{}"
            body = json.loads(raw.decode("utf-8") or "{}")
            if not isinstance(body, dict):
                body = {}
        except (ValueError, UnicodeDecodeError):
            return self._json(400, {"error": "JSON invalid"})
        routes = {
            "/api/fs/list": api_fs_list, "/api/fs/read": api_fs_read, "/api/fs/write": api_fs_write,
            "/api/fs/stat": api_fs_stat, "/api/fs/mkdir": api_fs_mkdir, "/api/open": api_open,
            "/api/structure": lambda b: structure(b.get("workspace") or {}, bool(b.get("create"))),
            "/api/scan": api_scan, "/api/eval/start": api_eval_start, "/api/rescore": api_rescore,
        }
        if u.path == "/api/eval/stop":
            JOB.set(stopping=True)
            JOB.log("Se oprește evaluarea…")
            return self._json(200, {"ok": True})
        if u.path == "/api/quit":
            self._json(200, {"ok": True})
            threading.Thread(target=lambda: (time.sleep(0.3), os._exit(0)), daemon=True).start()
            return
        fn = routes.get(u.path)
        if fn is None:
            return self._json(404, {"error": "Necunoscut: " + u.path})
        try:
            return self._json(200, fn(body))
        except (ValueError, OSError) as e:
            return self._json(400, {"error": str(e)})
        except Exception as e:
            traceback.print_exc()
            return self._json(500, {"error": "Eroare internă: %s" % e})


def open_browser(url):
    """Deschide interfața ca fereastră de aplicație (Chrome/Edge --app), altfel browserul implicit."""
    candidates = []
    if IS_WIN:
        pf = [os.environ.get(k, "") for k in ("ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA")]
        for b in pf:
            if not b:
                continue
            candidates += [os.path.join(b, "Microsoft", "Edge", "Application", "msedge.exe"),
                           os.path.join(b, "Google", "Chrome", "Application", "chrome.exe")]
    elif sys.platform == "darwin":
        candidates += ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
                       "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"]
    else:
        for n in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge"):
            p = shutil.which(n)
            if p:
                candidates.append(p)
    for c in candidates:
        if c and os.path.isfile(c):
            try:
                subprocess.Popen([c, "--app=" + url, "--window-size=1400,900"], stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL,
                                 **({"start_new_session": True} if not IS_WIN else {}))
                return
            except OSError:
                continue
    try:
        import webbrowser
        webbrowser.open(url)
    except Exception:
        pass


def main():
    global PORT, SERVER
    args = sys.argv[1:]
    port = 8765
    browser = True
    i = 0
    while i < len(args):
        if args[i] == "--port" and i + 1 < len(args):
            port = int(args[i + 1])
            i += 1
        elif args[i] == "--no-browser":
            browser = False
        i += 1
    for p in range(port, port + 30):
        try:
            SERVER = ThreadingHTTPServer(("127.0.0.1", p), Handler)
            PORT = p
            break
        except OSError:
            continue
    if SERVER is None:
        print("Nu găsesc niciun port liber.")
        sys.exit(1)
    SERVER.daemon_threads = True
    url = "http://127.0.0.1:%d/" % PORT
    print("%s %s — varianta %s" % (APP_NAME, VERSION, VARIANT))
    print("Interfața: %s" % url)
    print("Închide această fereastră (sau Ctrl+C) pentru a opri aplicația.")
    sys.stdout.flush()
    if browser:
        threading.Timer(0.4, open_browser, args=(url,)).start()
    try:
        SERVER.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    if IS_WIN:
        try:
            sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
        except Exception:
            pass
    main()
