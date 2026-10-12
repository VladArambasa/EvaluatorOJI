// ENGINE.CPP - EVALUATION ENGINE (FAITHFUL PORT OF THE PYTHON REFERENCE)
#include "engine.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "sys.hpp"

namespace eng {

const char *VARIANT = "C++";
std::string PROBE_C_SOURCE;
std::string PROBE_PY_SOURCE;

using js::Value;

// === UTILITIES ===
static std::string asciiLower(std::string s) {
    for (auto &c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return s;
}
static bool isDigit(char c) { return c >= '0' && c <= '9'; }

int natcmp(const std::string &a, const std::string &b) {
    size_t i = 0, j = 0, la = a.size(), lb = b.size();
    while (i < la && j < lb) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            size_t si = i, sj = j;
            while (i < la && isDigit(a[i])) i++;
            while (j < lb && isDigit(b[j])) j++;
            std::string ra = a.substr(si, i - si), rb = b.substr(sj, j - sj);
            ra.erase(0, std::min(ra.find_first_not_of('0'), ra.size()));
            rb.erase(0, std::min(rb.find_first_not_of('0'), rb.size()));
            if (ra.size() != rb.size()) return ra.size() < rb.size() ? -1 : 1;
            if (ra != rb) return ra < rb ? -1 : 1;
        } else {
            char xa = a[i], xb = b[j];
            if (xa >= 'A' && xa <= 'Z') xa = static_cast<char>(xa + 32);
            if (xb >= 'A' && xb <= 'Z') xb = static_cast<char>(xb + 32);
            if (xa != xb) return static_cast<unsigned char>(xa) < static_cast<unsigned char>(xb) ? -1 : 1;
            i++;
            j++;
        }
    }
    size_t ra = la - i, rb = lb - j;
    if (ra != rb) return ra < rb ? -1 : 1;
    if (a == b) return 0;
    return a < b ? -1 : 1;
}
static bool natLess(const std::string &a, const std::string &b) { return natcmp(a, b) < 0; }

static bool globMatch(const std::string &patIn, const std::string &nameIn) {
    std::string p = asciiLower(patIn), n = asciiLower(nameIn);
    size_t pi = 0, ni = 0, mark = 0;
    long star = -1;
    while (ni < n.size()) {
        if (pi < p.size() && p[pi] == '*') { star = static_cast<long>(pi); mark = ni; pi++; }
        else if (pi < p.size() && (p[pi] == '?' || p[pi] == n[ni])) { pi++; ni++; }
        else if (star != -1) { pi = static_cast<size_t>(star) + 1; mark++; ni = mark; }
        else return false;
    }
    while (pi < p.size() && p[pi] == '*') pi++;
    return pi == p.size();
}
static std::vector<std::string> splitChar(const std::string &s, char sep, bool skipEmpty) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(sep, i);
        if (j == std::string::npos) j = s.size();
        std::string c = s.substr(i, j - i);
        if (!(skipEmpty && c.empty())) out.push_back(c);
        i = j + 1;
    }
    return out;
}
static std::string slashes(std::string s) { for (auto &c : s) if (c == '\\') c = '/'; return s; }
static int pathMatch(const std::string &pat, const std::string &rel) {
    std::vector<std::string> pc = splitChar(slashes(pat), '/', true);
    std::vector<std::string> rc = splitChar(rel, '/', false);
    if (pc.empty() || pc.size() > rc.size()) return -1;
    size_t k = rc.size() - pc.size();
    for (size_t x = 0; x < pc.size(); x++) if (!globMatch(pc[x], rc[k + x])) return -1;
    return static_cast<int>(k);
}
static bool globFull(const std::string &pat, const std::string &s) {
    std::vector<std::string> pc = splitChar(slashes(pat), '/', true), sc = splitChar(slashes(s), '/', true);
    if (pc.size() != sc.size()) return false;
    for (size_t i = 0; i < pc.size(); i++) if (!globMatch(pc[i], sc[i])) return false;
    return true;
}
static std::vector<std::string> splitCmd(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false, has = false;
    for (char ch : s) {
        if (ch == '"') { inq = !inq; has = true; }
        else if ((ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') && !inq) { if (has) { out.push_back(cur); cur.clear(); has = false; } }
        else { cur += ch; has = true; }
    }
    if (has) out.push_back(cur);
    return out;
}
static std::vector<std::string> splitWs(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
static std::string replaceAll(std::string s, const std::string &a, const std::string &b) {
    if (a.empty()) return s;
    size_t i = 0;
    while ((i = s.find(a, i)) != std::string::npos) { s.replace(i, a.size(), b); i += b.size(); }
    return s;
}
using KV = std::vector<std::pair<std::string, std::string>>;
static std::string subst(const std::string &t, const KV &kv) {
    std::string s = t;
    for (auto &p : kv) s = replaceAll(s, "{" + p.first + "}", p.second);
    return s;
}
static std::string extOf(const std::string &name) {
    std::string b = slashes(name);
    size_t k = b.rfind('/');
    if (k != std::string::npos) b = b.substr(k + 1);
    size_t i = b.rfind('.');
    return (i != std::string::npos && i > 0) ? asciiLower(b.substr(i)) : std::string();
}
static std::string fmtInt(double x) { char b[64]; std::snprintf(b, sizeof b, "%.0f", std::floor(x + 0.5)); return b; }
static double round2(double x) { return std::floor(x * 100.0 + 0.5) / 100.0; }
static double round3(double x) { return std::floor(x * 1000.0 + 0.5) / 1000.0; }
static std::string fmtG(double x) { char b[64]; std::snprintf(b, sizeof b, "%g", x); return b; }
static std::string fmt2(double x) { char b[64]; std::snprintf(b, sizeof b, "%.2f", x); return b; }
static bool contains(const std::vector<std::string> &v, const std::string &x) { return std::find(v.begin(), v.end(), x) != v.end(); }

// ACCESS WITH DEFAULTS (LIKE G() IN PYTHON: MISSING/NULL -> DEFAULT)
static const Value &G(const Value &o, const char *k) { return o[k]; }
static std::string gs(const Value &o, const char *k, const std::string &def) { const Value &v = o[k]; return v.isNull() ? def : v.str(def); }
static double gn(const Value &o, const char *k, double def) { const Value &v = o[k]; return v.isNull() ? def : v.num(def); }
static bool gb(const Value &o, const char *k, bool def) { return o[k].truthy(def); }

struct Entry { std::string rel, abs; bool dir; int depth; };
static std::vector<Entry> walkBfs(const std::string &top, int maxDepth, const std::vector<std::string> &ignore) {
    std::vector<Entry> out;
    struct Q { std::string d, rel; int depth; };
    std::vector<Q> queue{{top, "", 0}};
    for (size_t qi = 0; qi < queue.size(); qi++) {
        Q q = queue[qi];
        std::vector<sys::DirEntry> es;
        if (!sys::listDir(q.d, es)) continue;
        std::sort(es.begin(), es.end(), [](const sys::DirEntry &a, const sys::DirEntry &b) { return natcmp(a.name, b.name) < 0; });
        std::vector<Q> sub;
        for (auto &e : es) {
            std::string ap = sys::join(q.d, e.name);
            std::string rp = q.rel.empty() ? e.name : q.rel + "/" + e.name;
            if (e.dir) {
                bool ign = false;
                for (auto &g : ignore) if (globMatch(g, e.name)) { ign = true; break; }
                if (ign) continue;
                out.push_back({rp, ap, true, q.depth});
                if (q.depth < maxDepth) sub.push_back({ap, rp, q.depth + 1});
            } else out.push_back({rp, ap, false, q.depth});
        }
        for (auto &s : sub) queue.push_back(s);
    }
    return out;
}
static std::string readText(const std::string &p, long long limit = -1) { std::string s; sys::readFile(p, s, limit); return s; }

// === DEFAULTS ===
static Value defaultLanguages() {
    Value a = Value::array();
    auto L = [](const char *id, const char *name, const char *ext, const char *comp, const char *run) {
        Value v = Value::object();
        v.set("id", id); v.set("name", name); v.set("ext", ext); v.set("compile", comp); v.set("run", run); v.set("probe", true);
        return v;
    };
    a.push(L("c", "C", ".c", "gcc -O2 -Wall -std=c11 {sources} -o {exe} {libs}", "{exe}"));
    a.push(L("cpp", "C++", ".cpp .cc .cxx", "g++ -O2 -Wall -std=c++17 {sources} -o {exe} {libs}", "{exe}"));
    a.push(L("py", "Python", ".py", "", sys::IS_WIN ? "python {main}" : "python3 {main}"));
    return a;
}
static Value defaultAutolibs() {
    static const char *rules[][3] = {
        {"Math", "math.h complex.h tgmath.h fenv.h", "-lm"}, {"Threads", "pthread.h threads.h", "-pthread"},
        {"Ncurses", "ncurses.h curses.h", "-lncurses"}, {"Readline", "readline/readline.h", "-lreadline"},
        {"SDL2", "SDL2/SDL.h", "-lSDL2"}, {"SDL2_image", "SDL2/SDL_image.h", "-lSDL2_image"}, {"SDL2_ttf", "SDL2/SDL_ttf.h", "-lSDL2_ttf"},
        {"SDL2_mixer", "SDL2/SDL_mixer.h", "-lSDL2_mixer"}, {"Raylib", "raylib.h", "-lraylib -lGL -lm -lpthread -ldl -lrt -lX11"},
        {"GLFW", "GLFW/glfw3.h", "-lglfw -lGL"}, {"GLEW", "GL/glew.h", "-lGLEW"}, {"OpenGL", "GL/*", "-lGL -lGLU -lglut"},
        {"wxWidgets", "wx/*", "$(wx-config --cxxflags --libs)"}, {"X11", "X11/Xlib.h", "-lX11"}, {"Zlib", "zlib.h", "-lz"},
        {"SQLite", "sqlite3.h", "-lsqlite3"}, {"Curl", "curl/curl.h", "-lcurl"}, {"OpenSSL", "openssl/*.h", "-lssl -lcrypto"},
        {"GMP", "gmp.h", "-lgmp"}, {"Dynamic loading", "dlfcn.h", "-ldl"}, {"Realtime", "mqueue.h aio.h", "-lrt"}, {"Crypt", "crypt.h", "-lcrypt"}};
    Value a = Value::array();
    for (auto &r : rules) { Value v = Value::object(); v.set("name", r[0]); v.set("headers", r[1]); v.set("flags", r[2]); a.push(v); }
    return a;
}
static const char *DEFAULT_IGNORE = ".* _* cmake-build-* CMakeFiles build bin obj out release debug __MACOSX node_modules venv __pycache__";
static Value defaultRootRules() {
    Value a = Value::array();
    for (const char *p : {"CMakeLists.txt", "*.cbp", "bash-scripts/compile.sh", "bash-scripts/build.sh", "compile.sh", "build.sh", "main.cpp", "main.c",
                          "*.cpp", "*.c", "*.py"}) {
        Value v = Value::object();
        v.set("pattern", p);
        v.set("up", 0);
        a.push(v);
    }
    return a;
}

// === PROBLEM CONFIGURATION ===
struct Job;
struct Cfg {
    const Value &ws, &prob;
    std::string root, name, nr, layout;
    Value languages, autolibs;
    bool autolibsOn;
    std::vector<std::string> ignore;
    bool keepOutputs, winStack, nonzeroError, useProbe;
    double compileTimeout;
    std::string ioInput, ioOutput, ioInpath, ioOutpath, ioCwd, runIn;
    Value rootRules;
    int maxDepth;
    std::vector<std::string> mainPatterns, sizeExt;
    std::string compileScope, buildMode, buildCmd, buildExe, sizeScope, compileExtra;
    double timeMs, hardTime, memMb, hardMem, stackMb, sourceKb, outputMb;
    std::string timeMode;
    bool calibrate;
    std::string cmpMode, checker, protocol, checkerArgs;
    bool ignoreCase;
    double epsilon;
    std::string testsDir, workDir, resultsDir, exeSuffix;
    KV v;

    Cfg(const Value &w, const Value &p) : ws(w), prob(p) {
        root = sys::absPath(gs(ws, "root", "").empty() ? std::string(".") : gs(ws, "root", "."));
        name = gs(prob, "name", "");
        { char b[32]; std::snprintf(b, sizeof b, "%lld", static_cast<long long>(std::trunc(gn(prob, "nr", 1)))); nr = b; }
        layout = gs(ws, "layout", "problem-student");
        const Value &langs = G(ws, "languages");
        languages = (langs.isArr() && langs.size()) ? langs : defaultLanguages();
        const Value &al = G(ws, "autoLibs");
        autolibsOn = gb(al, "enabled", true);
        autolibs = G(al, "rules").isNull() ? defaultAutolibs() : G(al, "rules");
        ignore = splitWs(gs(ws, "ignoreDirs", DEFAULT_IGNORE));
        const Value &opt = G(ws, "options");
        keepOutputs = gb(opt, "keepOutputs", true);
        winStack = gb(opt, "windowsStackFlag", true);
        compileTimeout = gn(opt, "compileTimeoutSec", 60);
        nonzeroError = gb(opt, "nonzeroExitIsError", true);
        useProbe = gb(opt, "useProbe", true);
        const Value &io = G(prob, "io");
        ioInput = gs(io, "input", "file");
        ioOutput = gs(io, "output", "file");
        ioInpath = gs(io, "inputPath", "{problem}.in");
        ioOutpath = gs(io, "outputPath", "{problem}.out");
        ioCwd = gs(io, "cwd", "");
        runIn = gs(io, "runIn", "sandbox");
        const Value &pj = G(prob, "project");
        rootRules = (G(pj, "rootRules").isArr() && G(pj, "rootRules").size()) ? G(pj, "rootRules") : defaultRootRules();
        maxDepth = static_cast<int>(std::trunc(gn(pj, "maxDepth", 6)));
        mainPatterns = splitWs(gs(pj, "mainPatterns", "main.cpp main.c {problem}.cpp {problem}.c *.cpp *.c *.py"));
        compileScope = gs(pj, "compileScope", "main");
        buildMode = gs(pj, "buildMode", "command");
        buildCmd = gs(pj, "buildCommand", "auto");
        if (buildCmd.empty()) buildCmd = "auto";
        buildExe = gs(pj, "buildExe", "release/program");
        if (buildExe.empty()) buildExe = "release/program";
        sizeScope = gs(pj, "sizeScope", "main");
        for (auto &e : splitWs(gs(pj, "sizeExt", ".c .h .cpp .hpp .cc .cxx .hh .py"))) sizeExt.push_back(asciiLower(e));
        compileExtra = gs(prob, "compileExtra", "");
        const Value &lim = G(prob, "limits");
        timeMs = gn(lim, "timeMs", 1000);
        timeMode = gs(lim, "timeMode", "cpu");
        hardTime = std::max(1.0, gn(lim, "hardTimeFactor", 2));
        memMb = gn(lim, "memMb", 64);
        hardMem = std::max(1.0, gn(lim, "hardMemFactor", 2));
        stackMb = gn(lim, "stackMb", 8);
        sourceKb = gn(lim, "sourceKb", 0);
        outputMb = gn(lim, "outputMb", 64);
        calibrate = gb(lim, "calibrate", true);
        const Value &cmp = G(prob, "compare");
        cmpMode = gs(cmp, "mode", "lines");
        ignoreCase = gb(cmp, "ignoreCase", false);
        epsilon = gn(cmp, "epsilon", 1e-6);
        checker = gs(cmp, "checker", "");
        protocol = gs(cmp, "protocol", "oji");
        checkerArgs = gs(cmp, "args", "");
        v = {{"root", root}, {"problem", name}, {"nr", nr}};
        std::string td = gs(prob, "testsDir", "");
        if (td.empty()) td = gs(ws, "testsDir", "{root}/{problem}/_tests");
        testsDir = sys::resolve(root, subst(td, v));
        workDir = sys::resolve(root, subst(gs(ws, "workDir", "{root}/_work"), v));
        resultsDir = sys::resolve(root, subst(gs(ws, "resultsDir", "{root}/_results"), v));
        exeSuffix = sys::IS_WIN ? ".exe" : "";
    }
    std::vector<std::string> langExts(const Value &L) const {
        std::vector<std::string> out;
        for (auto &e : splitWs(gs(L, "ext", ""))) out.push_back(asciiLower(e));
        return out;
    }
    const Value *langFor(const std::string &filename) const {
        std::string e = extOf(filename);
        if (e.empty()) return nullptr;
        for (auto &L : languages.a) if (contains(langExts(L), e)) return &L;
        return nullptr;
    }
    std::vector<std::string> allExts() const {
        std::vector<std::string> out;
        for (auto &L : languages.a) for (auto &e : langExts(L)) out.push_back(e);
        return out;
    }
    static bool compiled(const Value &L) {
        std::string c = gs(L, "compile", "");
        return c.find_first_not_of(" \t\r\n") != std::string::npos;
    }
    std::string probeMode(const Value &L) const {
        if (!useProbe || !gb(L, "probe", false)) return "";
        if (compiled(L)) {
            if (buildMode == "scripts") return std::string(sys::osName()) == "linux" ? "preload" : "";
            return "c";
        }
        if (contains(langExts(L), ".py")) return "py";
        return "";
    }
    std::vector<std::string> probeEnv(const Value &L, const std::string &probeFile, const Job &job) const;
    bool probeActive(const Value &L, const Job &job) const;
};

// === EVALUATION STATE ===
struct Job {
    sys::Mutex mu;
    bool running = false;
    std::atomic<bool> stopping{false};
    std::string phase, problem, student, test, error;
    long done = 0, total = 0, rv = 0;
    std::vector<std::string> logs;
    Value results = Value::object();
    std::vector<std::string> envPathAdd;
    std::string currentProbe, pyProbeDir, preloadSo, runDirAny, studentNow;
    std::map<std::string, std::pair<bool, std::string>> cmdCache;
    bool haveChecker = false;
    std::vector<std::string> checkerArgv;
    std::string checkerError;

    void log(const std::string &m) {
        sys::Lock l(mu);
        logs.push_back(sys::clockStr() + " " + m);
        if (logs.size() > 20000) logs.erase(logs.begin(), logs.begin() + 10000);
    }
    void set(const char *field, const std::string &val) {
        sys::Lock l(mu);
        if (!std::strcmp(field, "phase")) phase = val;
        else if (!std::strcmp(field, "problem")) problem = val;
        else if (!std::strcmp(field, "student")) student = val;
        else if (!std::strcmp(field, "test")) test = val;
    }
    void addDone(long n) { sys::Lock l(mu); done += n; }
    void publish(const std::string &name, const Value &res) {
        sys::Lock l(mu);
        results.set(name, res);
        rv++;
    }
    std::string runDir(const Cfg &C) const { return sys::join(sys::join(C.workDir, "run"), C.name); }
};
static Job JOB;
static std::function<bool()> cancelFn() { return []() { return JOB.stopping.load(); }; }

std::vector<std::string> Cfg::probeEnv(const Value &L, const std::string &probeFile, const Job &job) const {
    std::vector<std::string> env{"OLIEVAL_PROBE=" + probeFile};
    std::string mode = probeMode(L);
    if (mode == "py" && !job.pyProbeDir.empty()) {
        std::string old = sys::getEnv("PYTHONPATH");
        env.push_back("PYTHONPATH=" + job.pyProbeDir + (old.empty() ? std::string() : std::string(sys::IS_WIN ? ";" : ":") + old));
    }
    if (mode == "preload" && !job.preloadSo.empty()) {
        std::string old = sys::getEnv("LD_PRELOAD");
        env.push_back("LD_PRELOAD=" + job.preloadSo + (old.empty() ? std::string() : " " + old));
    }
    for (auto &e : job.envPathAdd) env.push_back(e);
    return env;
}
bool Cfg::probeActive(const Value &L, const Job &job) const {
    std::string m = probeMode(L);
    return !m.empty() && (m != "preload" || !job.preloadSo.empty());
}

const Value *findProblem(const Value &ws, const std::string &name) {
    const Value &ps = G(ws, "problems");
    if (!ps.isArr()) return nullptr;
    for (auto &p : ps.a) if (gs(p, "name", "") == name) return &p;
    return nullptr;
}

static sys::RunResult runP(const std::vector<std::string> &argv, const std::string &cwd, const std::string &in, const std::string &out,
                           const std::string &err, const std::vector<std::string> &env, long long wall, bool cancellable = false) {
    sys::RunSpec s;
    s.argv = argv;
    s.cwd = cwd;
    s.stdinPath = in;
    s.stdoutPath = out;
    s.stderrPath = err;
    s.env = env;
    s.wallMs = wall;
    if (cancellable) s.cancel = cancelFn();
    return sys::run(s);
}

static std::map<std::string, double> readProbe(const std::string &p) {
    std::map<std::string, double> out;
    std::string t;
    if (!sys::readFile(p, t, 4096)) return out;
    for (auto &line : splitChar(t, '\n', true)) {
        auto parts = splitWs(line);
        if (parts.size() == 2) {
            char *end = nullptr;
            double v = std::strtod(parts[1].c_str(), &end);
            out[parts[0]] = (end && *end == 0) ? v : 0;
        }
    }
    return out;
}

// === DETECTION ===
static std::vector<std::string> listTestsFiles(const std::string &d) {
    std::vector<std::string> out;
    for (auto &e : walkBfs(d, 1, {})) if (!e.dir) out.push_back(e.rel);
    return out;
}
static bool matchN(const std::string &pattern, const std::string &rel, std::string &n) {
    size_t i = pattern.find("{n}");
    if (i == std::string::npos) { if (globFull(pattern, rel)) { n = "1"; return true; } return false; }
    std::string pre = pattern.substr(0, i), suf = pattern.substr(i + 3);
    if (rel.size() <= pre.size() + suf.size()) return false;
    if (asciiLower(rel.substr(0, pre.size())) != asciiLower(pre)) return false;
    if (!suf.empty() && asciiLower(rel.substr(rel.size() - suf.size())) != asciiLower(suf)) return false;
    std::string mid = rel.substr(pre.size(), rel.size() - suf.size() - pre.size());
    if (mid.empty() || mid.find('/') != std::string::npos) return false;
    n = mid;
    return true;
}
static const char *CHECKER_NAMES[] = {"verif.cpp", "verif.c", "verif.py", "verif.exe", "verif"};

static Value scanTests(const Cfg &C) {
    Value res = Value::object();
    res.set("testsDir", C.testsDir);
    bool ex = sys::isDir(C.testsDir);
    res.set("testsDirExists", ex);
    res.set("tests", Value::array());
    res.set("checker", "");
    if (!ex) return res;
    KV pv{{"problem", C.name}, {"nr", C.nr}};
    std::string ip = subst(gs(C.prob, "inputPattern", "{n}-{problem}.in"), pv), op = subst(gs(C.prob, "okPattern", "{n}-{problem}.ok"), pv);
    std::map<std::string, Value> found;
    std::vector<std::string> keys;
    for (auto &rel : listTestsFiles(C.testsDir)) {
        std::string n;
        if (!matchN(ip, rel, n) || found.count(n)) continue;
        std::string ok = replaceAll(op, "{n}", n);
        Value t = Value::object();
        t.set("id", n);
        t.set("input", rel);
        t.set("ok", ok);
        t.set("inputSize", sys::fileSize(sys::joinRel(C.testsDir, rel)));
        t.set("okExists", sys::isFile(sys::joinRel(C.testsDir, ok)));
        t.set("okSize", sys::fileSize(sys::joinRel(C.testsDir, ok)));
        found[n] = t;
        keys.push_back(n);
    }
    std::sort(keys.begin(), keys.end(), natLess);
    Value &arr = *res.find("tests");
    for (auto &k : keys) arr.push(found[k]);
    for (const char *c : CHECKER_NAMES) if (sys::isFile(sys::join(C.testsDir, c))) { res.set("checker", c); break; }
    return res;
}

struct Student { std::string id, dir, file, name; bool exists = false, registered = false; std::vector<std::string> warnings; };

static bool matchFlatRec(const std::vector<std::pair<char, std::string>> &toks, size_t ti, const std::string &name, const std::string &low, size_t pos,
                         std::map<char, std::string> &caps) {
    if (ti == toks.size()) return pos == name.size();
    char kind = toks[ti].first;
    if (kind == 'L') {
        const std::string &lit = toks[ti].second;
        if (low.compare(pos, lit.size(), lit) == 0 && pos + lit.size() <= low.size()) return matchFlatRec(toks, ti + 1, name, low, pos + lit.size(), caps);
        return false;
    }
    for (size_t end = pos + 1; end <= name.size(); end++) {
        std::string piece = name.substr(pos, end - pos);
        if (kind == 'E' && piece.find('.') != std::string::npos) break;
        caps[kind] = piece;
        if (matchFlatRec(toks, ti + 1, name, low, end, caps)) return true;
    }
    return false;
}
static bool matchFlat(const std::string &pattern, const std::string &fname, std::string &sid, std::string &ext) {
    std::vector<std::pair<char, std::string>> toks;
    size_t i = 0;
    while (i < pattern.size()) {
        if (pattern.compare(i, 9, "{student}") == 0) { toks.push_back({'S', ""}); i += 9; }
        else if (pattern.compare(i, 5, "{ext}") == 0) { toks.push_back({'E', ""}); i += 5; }
        else {
            size_t j = i;
            while (j < pattern.size() && pattern.compare(j, 9, "{student}") != 0 && pattern.compare(j, 5, "{ext}") != 0) j++;
            toks.push_back({'L', asciiLower(pattern.substr(i, j - i))});
            i = j;
        }
    }
    std::map<char, std::string> caps;
    if (matchFlatRec(toks, 0, fname, asciiLower(fname), 0, caps) && !caps['S'].empty()) {
        sid = caps['S'];
        ext = caps.count('E') ? caps['E'] : std::string();
        return true;
    }
    return false;
}
static std::string normcase(const std::string &p) { return sys::IS_WIN ? asciiLower(p) : p; }

static std::vector<Student> discoverStudents(const Cfg &C) {
    std::map<std::string, const Value *> reg;
    std::vector<std::string> order;
    const Value &sts = G(C.ws, "students");
    if (sts.isArr()) for (auto &s : sts.a) {
        std::string sid = gs(s, "id", "");
        while (!sid.empty() && std::isspace(static_cast<unsigned char>(sid.back()))) sid.pop_back();
        while (!sid.empty() && std::isspace(static_cast<unsigned char>(sid.front()))) sid.erase(0, 1);
        if (!sid.empty() && !reg.count(sid)) { reg[sid] = &s; order.push_back(sid); }
    }
    std::map<std::string, Student> out;
    if (C.layout == "flat") {
        std::string fdir = sys::resolve(C.root, subst(gs(C.ws, "flatDir", "{root}/{problem}/sources"), C.v));
        std::string pat = subst(gs(C.ws, "flatPattern", "{student}{nr}.{ext}"), {{"problem", C.name}, {"nr", C.nr}});
        std::vector<std::string> exts = C.allExts();
        std::vector<sys::DirEntry> es;
        sys::listDir(fdir, es);
        std::sort(es.begin(), es.end(), [](const sys::DirEntry &a, const sys::DirEntry &b) { return natcmp(a.name, b.name) < 0; });
        for (auto &e : es) {
            std::string fp = sys::join(fdir, e.name);
            if (!sys::isFile(fp)) continue;
            std::string sid, ext;
            if (!matchFlat(pat, e.name, sid, ext)) continue;
            if (!ext.empty() && !contains(exts, "." + asciiLower(ext))) continue;
            if (out.count(sid)) { out[sid].warnings.push_back("Several sources found; using " + sys::baseName(out[sid].file)); continue; }
            Student s;
            s.id = sid; s.dir = fdir; s.exists = true; s.file = fp;
            out[sid] = s;
        }
        for (auto &sid : order) if (!out.count(sid)) { Student s; s.id = sid; s.dir = fdir; s.exists = false; out[sid] = s; }
    } else {
        std::string t = slashes(subst(gs(C.ws, "studentDir", "{root}/{problem}/{student}"), {{"root", C.root}, {"problem", C.name}, {"nr", C.nr}}));
        size_t k = t.find("{student}");
        if (k == std::string::npos) throw std::runtime_error("The student folder template must contain {student}");
        std::string base = sys::resolve(C.root, k ? t.substr(0, k) : std::string("."));
        std::string suffix = t.substr(k + 9);
        std::set<std::string> special{normcase(C.testsDir), normcase(C.workDir), normcase(C.resultsDir)};
        std::vector<sys::DirEntry> es;
        sys::listDir(base, es);
        std::sort(es.begin(), es.end(), [](const sys::DirEntry &a, const sys::DirEntry &b) { return natcmp(a.name, b.name) < 0; });
        for (auto &e : es) {
            if (e.name.empty() || e.name[0] == '.' || e.name[0] == '_') continue;
            std::string p = sys::join(base, e.name);
            if (!sys::isDir(p) || special.count(normcase(sys::normPath(p)))) continue;
            Student s;
            s.id = e.name;
            s.dir = sys::resolve(base, e.name + suffix);
            s.exists = sys::isDir(s.dir);
            out[e.name] = s;
        }
        for (auto &sid : order) if (!out.count(sid)) {
            Student s;
            s.id = sid;
            s.dir = sys::resolve(base, sid + suffix);
            s.exists = sys::isDir(s.dir);
            out[sid] = s;
        }
    }
    std::vector<std::string> ids;
    for (auto &kv : out) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end(), natLess);
    std::vector<Student> res;
    for (auto &id : ids) {
        Student s = out[id];
        s.registered = reg.count(id) > 0;
        s.name = reg.count(id) ? gs(*reg[id], "name", "") : std::string();
        res.push_back(s);
    }
    return res;
}

static bool hasMainFunc(const std::string &p) {
    std::string t;
    if (!sys::readFile(p, t, 2 * 1024 * 1024)) return false;
    auto alnum = [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 128; };
    size_t i = 0;
    for (;;) {
        i = t.find("main", i);
        if (i == std::string::npos) return false;
        unsigned char before = i > 0 ? static_cast<unsigned char>(t[i - 1]) : ' ';
        size_t j = i + 4;
        while (j < t.size() && (t[j] == ' ' || t[j] == '\t' || t[j] == '\r' || t[j] == '\n')) j++;
        if (!(alnum(before) || before == '_') && j < t.size() && t[j] == '(') return true;
        i += 4;
    }
}

static const std::vector<std::string> OBJ_EXTS{".c", ".cpp", ".cc", ".cxx", ".c++"};

struct Info {
    std::string root, main, mainRel, lang;
    const Value *langObj = nullptr;
    std::vector<std::string> sources, warnings;
    long long sizeMain = 0, sizeAll = 0, sizeJudged = 0;
};

static Info detectProject(const Cfg &C, const Student &st) {
    Info info;
    info.warnings = st.warnings;
    if (C.layout == "flat") {
        info.root = st.dir;
        if (st.file.empty()) return info;
        info.main = st.file;
        info.mainRel = sys::baseName(st.file);
        if (const Value *L = C.langFor(st.file)) { info.lang = gs(*L, "id", ""); info.langObj = L; }
        info.sources = {sys::baseName(st.file)};
        info.sizeMain = info.sizeAll = info.sizeJudged = sys::fileSize(st.file);
        return info;
    }
    auto entries = walkBfs(st.dir, C.maxDepth, C.ignore);
    std::vector<std::string> rootComps;
    for (auto &rule : C.rootRules.a) {
        std::string pat = subst(gs(rule, "pattern", ""), {{"problem", C.name}, {"nr", C.nr}});
        while (!pat.empty() && std::isspace(static_cast<unsigned char>(pat.back()))) pat.pop_back();
        while (!pat.empty() && std::isspace(static_cast<unsigned char>(pat.front()))) pat.erase(0, 1);
        if (pat.empty()) continue;
        int up = static_cast<int>(std::trunc(gn(rule, "up", 0)));
        bool hit = false;
        for (auto &e : entries) {
            int k = pathMatch(pat, e.rel);
            if (k >= 0) {
                auto comps = splitChar(e.rel, '/', false);
                comps.resize(static_cast<size_t>(k));
                if (up > 0) comps.resize(static_cast<size_t>(std::max(0, static_cast<int>(comps.size()) - up)));
                rootComps = comps;
                hit = true;
                break;
            }
        }
        if (hit) break;
    }
    std::string rc;
    for (size_t i = 0; i < rootComps.size(); i++) rc += (i ? "/" : "") + rootComps[i];
    info.root = rootComps.empty() ? sys::normPath(st.dir) : sys::joinRel(st.dir, rc);
    std::vector<std::pair<std::string, std::string>> files;
    for (auto &e : walkBfs(info.root, C.maxDepth, C.ignore)) if (!e.dir) files.push_back({e.rel, e.abs});
    const std::pair<std::string, std::string> *mainF = nullptr;
    for (auto patIn : C.mainPatterns) {
        std::string pat = subst(patIn, {{"problem", C.name}, {"nr", C.nr}});
        std::vector<const std::pair<std::string, std::string> *> m;
        for (auto &f : files) if (pathMatch(pat, f.first) >= 0) m.push_back(&f);
        if (m.empty()) continue;
        for (auto *f : m) if (extOf(f->first) == ".py" || hasMainFunc(f->second)) { mainF = f; break; }
        if (!mainF) mainF = m[0];
        if (m.size() > 1) info.warnings.push_back("Several possible main files (" + std::to_string(m.size()) + "); chosen: " + mainF->first);
        break;
    }
    if (!mainF) { info.warnings.push_back("Main file not found in " + info.root); return info; }
    info.main = mainF->second;
    info.mainRel = mainF->first;
    const Value *L = C.langFor(mainF->first);
    if (!L) { info.warnings.push_back("Unknown language for " + mainF->first); return info; }
    info.lang = gs(*L, "id", "");
    info.langObj = L;
    auto lexts = C.langExts(*L);
    if (C.buildMode == "objects") { for (auto &f : files) if (contains(OBJ_EXTS, extOf(f.first)) && C.langFor(f.first)) info.sources.push_back(f.first); }
    else if (C.buildMode == "scripts") { for (auto &f : files) if (contains(OBJ_EXTS, extOf(f.first))) info.sources.push_back(f.first); }
    else if (C.compileScope == "all") { for (auto &f : files) if (contains(lexts, extOf(f.first))) info.sources.push_back(f.first); }
    else info.sources = {mainF->first};
    if (C.buildMode == "objects" || C.buildMode == "scripts" || C.compileScope == "all") std::sort(info.sources.begin(), info.sources.end());
    info.sizeMain = sys::fileSize(mainF->second);
    for (auto &f : files) if (contains(C.sizeExt, extOf(f.first))) info.sizeAll += sys::fileSize(f.second);
    info.sizeJudged = C.sizeScope == "all" ? info.sizeAll : info.sizeMain;
    return info;
}

static Value strArr(const std::vector<std::string> &v) { Value a = Value::array(); for (auto &s : v) a.push(s); return a; }

static Value publicStudent(const Student &st, const Info &info) {
    Value o = Value::object();
    o.set("id", st.id); o.set("name", st.name); o.set("dir", st.dir); o.set("exists", st.exists); o.set("registered", st.registered);
    o.set("root", info.root); o.set("main", info.mainRel); o.set("lang", info.lang); o.set("sources", strArr(info.sources));
    o.set("sizeMain", info.sizeMain); o.set("sizeAll", info.sizeAll); o.set("sizeJudged", info.sizeJudged); o.set("warnings", strArr(info.warnings));
    return o;
}

// === COMPILATION ===
static std::set<std::string> collectIncludes(const std::vector<std::string> &paths) {
    std::set<std::string> names;
    for (auto &p : paths) {
        std::string text;
        if (!sys::readFile(p, text, 4 * 1024 * 1024)) continue;
        size_t i = 0;
        while (i <= text.size()) {
            size_t j = text.find_first_of("\r\n", i);
            if (j == std::string::npos) j = text.size();
            std::string line = text.substr(i, j - i);
            i = j + 1;
            size_t k = 0;
            auto skipWs = [&]() { while (k < line.size() && (line[k] == ' ' || line[k] == '\t' || line[k] == '\f' || line[k] == '\v')) k++; };
            skipWs();
            if (k >= line.size() || line[k] != '#') continue;
            k++;
            skipWs();
            if (line.compare(k, 7, "include") != 0) continue;
            k += 7;
            skipWs();
            if (k < line.size() && line[k] == '<') {
                size_t e = line.find('>', k);
                if (e != std::string::npos && e > k + 1) {
                    std::string n = line.substr(k + 1, e - k - 1);
                    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.back()))) n.pop_back();
                    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.front()))) n.erase(0, 1);
                    names.insert(n);
                }
            }
        }
    }
    return names;
}
static std::vector<std::string> expandFlags(const std::string &text, Job &job) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, 2, "$(") == 0 || text[i] == '`') {
            bool dollar = text[i] == '$';
            size_t end = text.find(dollar ? ')' : '`', i + (dollar ? 2 : 1));
            if (end == std::string::npos) break;
            std::string cmd = text.substr(i + (dollar ? 2 : 1), end - i - (dollar ? 2 : 1));
            if (!job.cmdCache.count(cmd)) {
                sys::mkdirs(job.runDirAny);
                std::string tmp = sys::join(job.runDirAny, "_cmd.txt");
                sys::RunResult r = runP(splitCmd(cmd), "", "", tmp, "", {}, 20000);
                bool ok = r.started && r.exited && r.exit == 0;
                std::string val = ok ? readText(tmp, 100000) : std::string();
                while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) val.pop_back();
                job.cmdCache[cmd] = {ok, val};
                if (!ok) job.log("  Warning: command \"" + cmd + "\" failed (" + sys::statusText(r) + "); its options are not added");
            }
            auto &cv = job.cmdCache[cmd];
            if (cv.first) for (auto &f : splitCmd(cv.second)) out.push_back(f);
            i = end + 1;
        } else {
            size_t j = i;
            while (j < text.size() && text.compare(j, 2, "$(") != 0 && text[j] != '`') j++;
            for (auto &f : splitCmd(text.substr(i, j - i))) out.push_back(f);
            i = j;
        }
    }
    return out;
}
static std::vector<std::string> compileOnlyFlags(const std::vector<std::string> &flags) {
    std::vector<std::string> out;
    for (auto &f : flags) if (!(f.rfind("-l", 0) == 0 || f.rfind("-L", 0) == 0 || f.rfind("-Wl,", 0) == 0)) out.push_back(f);
    return out;
}
static std::vector<std::string> autoLibFlags(const Cfg &C, const std::string &root, const std::vector<std::string> &sources, Job &job) {
    std::vector<std::string> flags;
    if (!C.autolibsOn) return flags;
    std::vector<std::string> paths = sources;
    for (auto &e : walkBfs(root, C.maxDepth, C.ignore)) {
        std::string x = extOf(e.rel);
        if (!e.dir && (x == ".h" || x == ".hpp" || x == ".hh" || x == ".hxx")) paths.push_back(e.abs);
    }
    auto names = collectIncludes(paths);
    for (auto &rule : C.autolibs.a) {
        bool hit = false;
        for (auto &p : splitWs(gs(rule, "headers", ""))) {
            for (auto &n : names) if (globFull(p, n)) { hit = true; break; }
            if (hit) break;
        }
        if (hit) for (auto &f : expandFlags(gs(rule, "flags", ""), job)) if (!contains(flags, f)) flags.push_back(f);
    }
    return flags;
}
static std::vector<std::string> buildArgv(const std::string &tmpl, const std::vector<std::string> &sources, const std::vector<std::string> &libs,
                                          const std::string &exe, const std::string &mainF, long long stackBytes, const std::string &problem) {
    std::vector<std::string> out;
    for (auto &tok : splitCmd(tmpl)) {
        if (tok == "{sources}") out.insert(out.end(), sources.begin(), sources.end());
        else if (tok == "{libs}") out.insert(out.end(), libs.begin(), libs.end());
        else out.push_back(replaceAll(replaceAll(replaceAll(replaceAll(tok, "{exe}", exe), "{main}", mainF), "{stack}", std::to_string(stackBytes)), "{problem}", problem));
    }
    return out;
}
static std::string cmdString(const std::vector<std::string> &argv) {
    std::string s;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i) s += ' ';
        if (argv[i].find(' ') != std::string::npos || argv[i].empty()) s += "\"" + argv[i] + "\""; else s += argv[i];
    }
    return s;
}
struct Compiled { bool ok = false; std::string exe, log; double ms = 0; std::string cmd; };

static std::string freshExe(const std::string &outDir, const Cfg &C) {
    std::string exe = sys::join(outDir, "prog" + C.exeSuffix);
    if (sys::exists(exe)) {
        sys::removeFile(exe);
        if (sys::exists(exe)) exe = sys::join(outDir, "prog-" + std::to_string(static_cast<long long>(sys::nowMs()) % 100000) + C.exeSuffix);
    }
    return exe;
}
static Compiled compileProgram(const Cfg &C, const Value &L, const std::vector<std::string> &sourcesAbs, const std::string &mainAbs,
                               const std::string &outDir, const std::string &cwd, bool includeProbe, Job &job,
                               const std::vector<std::string> &extraLibs = {}) {
    Compiled c;
    sys::mkdirs(outDir);
    std::string tmpl = gs(L, "compile", "");
    if (!Cfg::compiled(L)) { c.ok = true; return c; }
    std::string exe = freshExe(outDir, C);
    std::vector<std::string> srcs = sourcesAbs;
    if (includeProbe && C.probeMode(L) == "c") srcs.push_back(job.currentProbe);
    std::vector<std::string> libs = extraLibs;
    long long stackBytes = static_cast<long long>(C.stackMb * 1024 * 1024);
    if (sys::IS_WIN && C.winStack && C.stackMb > 0) libs.push_back("-Wl,--stack=" + std::to_string(stackBytes));
    for (auto &f : splitCmd(C.compileExtra)) libs.push_back(f);
    auto argv = buildArgv(tmpl, srcs, libs, exe, mainAbs, stackBytes, C.name);
    std::string logPath = sys::join(outDir, "compile.txt");
    sys::RunResult r = runP(argv, cwd, "", logPath, logPath, job.envPathAdd, static_cast<long long>(C.compileTimeout * 1000), true);
    std::string log = readText(logPath, 20000);
    c.ok = r.started && r.exited && r.exit == 0 && sys::isFile(exe);
    if (!r.started) log = r.error + "\n" + log;
    else if (r.timedOut) log = "Compilation exceeded " + std::to_string(static_cast<long long>(C.compileTimeout)) + " s\n" + log;
    else if (!c.ok && log.empty()) log = "The compiler ended with: " + sys::statusText(r);
    while (!log.empty() && std::isspace(static_cast<unsigned char>(log.back())) && !c.ok) log.pop_back();
    c.exe = exe;
    c.log = log;
    c.ms = r.wallMs;
    c.cmd = cmdString(argv);
    return c;
}
static std::pair<std::string, std::vector<std::string>> langCompiler(const Value &L, const Cfg &C) {
    auto toks = splitCmd(gs(L, "compile", ""));
    if (toks.empty()) return {std::string(), {}};
    std::vector<std::string> flags;
    bool skip = false;
    for (size_t i = 1; i < toks.size(); i++) {
        const std::string &t = toks[i];
        if (skip) { skip = false; continue; }
        if (t == "-o") { skip = true; continue; }
        if (t == "{sources}" || t == "{libs}" || t.find("{exe}") != std::string::npos || t.find("{main}") != std::string::npos) continue;
        flags.push_back(replaceAll(replaceAll(t, "{stack}", std::to_string(static_cast<long long>(C.stackMb * 1024 * 1024))), "{problem}", C.name));
    }
    return {toks[0], flags};
}
static Compiled compileObjects(const Cfg &C, const Value &Lmain, const std::string &root, const std::string &outDir, Job &job,
                               const std::vector<std::string> &libs) {
    Compiled c;
    sys::mkdirs(outDir);
    std::string exe = freshExe(outDir, C);
    std::string objDir = sys::join(outDir, "obj");
    sys::clearDir(objDir);
    std::vector<std::string> files;
    for (auto &e : walkBfs(root, C.maxDepth, C.ignore)) if (!e.dir) files.push_back(e.rel);
    std::sort(files.begin(), files.end());
    std::vector<std::pair<std::string, const Value *>> srcs;
    for (auto &rp : files) {
        if (!contains(OBJ_EXTS, extOf(rp))) continue;
        const Value *L = C.langFor(rp);
        if (L && Cfg::compiled(*L)) srcs.push_back({rp, L});
    }
    std::vector<std::string> log, objs;
    double totalMs = 0;
    bool ok = true;
    const Value *linkerLang = nullptr;
    auto cflagsExtra = compileOnlyFlags(libs);
    std::string logPath = sys::join(outDir, "compile.txt");
    for (auto &s : srcs) {
        auto lc = langCompiler(*s.second, C);
        std::string relObj = s.first;
        size_t dot = relObj.rfind('.');
        size_t sl = relObj.rfind('/');
        if (dot != std::string::npos && (sl == std::string::npos || dot > sl)) relObj = relObj.substr(0, dot);
        std::string obj = sys::joinRel(objDir, relObj + ".o");
        sys::mkdirs(sys::dirName(obj));
        std::vector<std::string> argv{lc.first};
        argv.insert(argv.end(), lc.second.begin(), lc.second.end());
        argv.insert(argv.end(), cflagsExtra.begin(), cflagsExtra.end());
        argv.push_back("-c");
        argv.push_back(sys::joinRel(root, s.first));
        argv.push_back("-o");
        argv.push_back(obj);
        sys::RunResult r = runP(argv, root, "", logPath, logPath, job.envPathAdd, static_cast<long long>(C.compileTimeout * 1000), true);
        totalMs += r.wallMs;
        std::string out = readText(logPath, 20000);
        log.push_back("Compiled: " + s.first + " -> " + sys::relPath(obj, objDir));
        while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
        if (!out.empty()) log.push_back(out);
        if (!(r.started && r.exited && r.exit == 0 && sys::isFile(obj))) { ok = false; if (!r.started) log.push_back(r.error); }
        objs.push_back(obj);
        if (extOf(s.first) != ".c") linkerLang = s.second;
    }
    if (srcs.empty()) { c.log = "No .c or .cpp files found in " + root; return c; }
    if (ok && C.probeMode(Lmain) == "c") {
        const Value *Lc = C.langFor("x.c");
        if (!Lc) Lc = &Lmain;
        auto lc = langCompiler(*Lc, C);
        std::string pobj = sys::join(outDir, "olieval_probe.o");
        std::vector<std::string> argv{lc.first};
        argv.insert(argv.end(), lc.second.begin(), lc.second.end());
        argv.push_back("-c");
        argv.push_back(job.currentProbe);
        argv.push_back("-o");
        argv.push_back(pobj);
        sys::RunResult r = runP(argv, outDir, "", logPath, logPath, job.envPathAdd, static_cast<long long>(C.compileTimeout * 1000));
        if (r.started && r.exited && r.exit == 0 && sys::isFile(pobj)) objs.push_back(pobj);
    }
    std::string cmd;
    if (ok) {
        const Value *lk = linkerLang ? linkerLang : (C.langFor("x.c") ? C.langFor("x.c") : &Lmain);
        std::vector<std::string> argv{langCompiler(*lk, C).first};
        argv.insert(argv.end(), objs.begin(), objs.end());
        argv.push_back("-o");
        argv.push_back(exe);
        argv.insert(argv.end(), libs.begin(), libs.end());
        cmd = cmdString(argv);
        sys::RunResult r = runP(argv, root, "", logPath, logPath, job.envPathAdd, static_cast<long long>(C.compileTimeout * 1000), true);
        totalMs += r.wallMs;
        std::string out = readText(logPath, 20000);
        while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
        if (!out.empty()) log.push_back(out);
        ok = r.started && r.exited && r.exit == 0 && sys::isFile(exe);
        log.push_back(ok ? "Build completed. Executable created at " + exe : std::string("Linking (build) failed."));
    } else log.push_back("Compilation failed; linking skipped.");
    std::string all;
    for (size_t i = 0; i < log.size(); i++) all += (i ? "\n" : "") + log[i];
    if (all.size() > 20000) all.resize(20000);
    c.ok = ok;
    c.exe = exe;
    c.log = all;
    c.ms = totalMs;
    c.cmd = "compile.sh + build.sh: " + (cmd.empty() ? std::to_string(srcs.size()) + " sources" : cmd);
    return c;
}
static std::string findScripts(const std::string &root, const std::string &buildCmd) {
    std::string bc = buildCmd;
    while (!bc.empty() && std::isspace(static_cast<unsigned char>(bc.back()))) bc.pop_back();
    while (!bc.empty() && std::isspace(static_cast<unsigned char>(bc.front()))) bc.erase(0, 1);
    if (!bc.empty() && bc != "auto") return bc;
    for (const char *d : {"bash-scripts/", "scripts/", ""}) {
        if (sys::isFile(sys::joinRel(root, std::string(d) + "compile.sh")) && sys::isFile(sys::joinRel(root, std::string(d) + "build.sh")))
            return std::string("bash ") + d + "compile.sh && bash " + d + "build.sh";
    }
    for (const char *d : {"bash-scripts/", "scripts/", ""}) if (sys::isFile(sys::joinRel(root, std::string(d) + "build.sh"))) return std::string("bash ") + d + "build.sh";
    return std::string();
}
static std::string safeName(const std::string &s) {
    std::string out;
    for (unsigned char c : s) out += (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c >= 128) ? static_cast<char>(c) : '_';
    return out.empty() ? std::string("_") : out;
}
static Compiled compileScripts(const Cfg &C, const std::string &root, Job &job) {
    Compiled c;
    std::string cmd = findScripts(root, C.buildCmd);
    if (cmd.empty()) { c.log = "Scripts compile.sh / build.sh not found (in bash-scripts/, scripts/ or the project root)"; return c; }
    std::string exeRel = subst(C.buildExe, {{"problem", C.name}, {"nr", C.nr}});
    std::string exe = sys::joinRel(root, exeRel);
    sys::removeFile(exe);
    sys::removeFile(exe + ".exe");
    std::string outDir = sys::join(sys::join(sys::join(C.workDir, "build"), C.name), safeName(job.studentNow));
    sys::mkdirs(outDir);
    std::string logPath = sys::join(outDir, "compile.txt");
    std::vector<std::string> argv = sys::IS_WIN ? std::vector<std::string>{"cmd", "/c", cmd} : std::vector<std::string>{"sh", "-c", cmd};
    sys::RunResult r = runP(argv, root, "", logPath, logPath, job.envPathAdd, static_cast<long long>(C.compileTimeout * 1000), true);
    std::string log = readText(logPath, 20000);
    std::string found = sys::isFile(exe) ? exe : (sys::isFile(exe + ".exe") ? exe + ".exe" : std::string());
    c.ok = r.started && !r.timedOut && !found.empty();
    if (!r.started) log = r.error + "\n" + log;
    else if (r.timedOut) log = "The scripts exceeded " + std::to_string(static_cast<long long>(C.compileTimeout)) + " s\n" + log;
    else if (found.empty()) {
        while (!log.empty() && std::isspace(static_cast<unsigned char>(log.back()))) log.pop_back();
        log += "\nThe scripts did not produce the executable " + exeRel;
    }
    while (!log.empty() && std::isspace(static_cast<unsigned char>(log.front()))) log.erase(0, 1);
    c.exe = found;
    c.log = log;
    c.ms = r.wallMs;
    c.cmd = cmd;
    return c;
}
static Compiled buildStudent(const Cfg &C, const Value &L, const Info &info, const std::string &bdir, Job &job, const std::vector<std::string> &libs) {
    if (!Cfg::compiled(L)) return compileProgram(C, L, {}, info.main, bdir, info.root, false, job);
    if (C.buildMode == "objects") return compileObjects(C, L, info.root, bdir, job, libs);
    if (C.buildMode == "scripts") return compileScripts(C, info.root, job);
    std::vector<std::string> srcs;
    for (auto &s : info.sources) srcs.push_back(sys::joinRel(info.root, s));
    return compileProgram(C, L, srcs, info.main, bdir, info.root, true, job, libs);
}

static const char *CALIB_C =
    "#include <stdio.h>\nint main(void)\n{\n    int x = 0;\n    FILE *f = fopen(\"calibration.in\", \"r\");\n    if (f != NULL) {\n"
    "        if (fscanf(f, \"%d\", &x) != 1) x = 0;\n        fclose(f);\n    }\n    printf(\"%d\\n\", x * 0);\n    return 0;\n}\n";
static const char *CALIB_CPP =
    "#include <iostream>\n#include <fstream>\nint main()\n{\n    std::ifstream f(\"calibration.in\");\n    int x = 0;\n    f >> x;\n"
    "    std::cout << x * 0 << std::endl;\n    return 0;\n}\n";

static long long calibrate(const Cfg &C, const Value &L, Job &job) {
    std::string lid = gs(L, "id", "");
    std::string file, src;
    for (auto &e : C.langExts(L)) {
        if (e == ".c") { file = "calibration.c"; src = CALIB_C; break; }
        if (e == ".cpp" || e == ".cc" || e == ".cxx") { file = "calibration" + e; src = CALIB_CPP; break; }
        if (e == ".py") { file = "calibration.py"; src = "x = 0\nprint(x)\n"; break; }
    }
    if (file.empty()) return 0;
    std::string d = sys::join(sys::join(C.workDir, "calibration"), lid);
    sys::mkdirs(d);
    std::string sp = sys::join(d, file);
    sys::writeFile(sp, src);
    Compiled cp = compileProgram(C, L, {sp}, sp, d, d, C.buildMode != "scripts", job);
    if (!cp.ok) { job.log("  Calibration " + lid + ": compilation failed; memory will not be calibrated."); return 0; }
    auto argv = buildArgv(gs(L, "run", "{exe}"), {sp}, {}, cp.exe, sp, 0, C.name);
    long long best = -1;
    std::string probe = sys::join(d, "probe.txt");
    for (int k = 0; k < 3; k++) {
        sys::removeFile(probe);
        sys::RunResult r = runP(argv, d, "", sys::join(d, "out.txt"), "", C.probeEnv(L, probe, job), 10000);
        auto pr = readProbe(probe);
        long long m = pr.count("rss_kb") ? static_cast<long long>(pr["rss_kb"]) : (C.probeActive(L, job) ? 0 : r.peakKb);
        if (r.started && r.exited && m > 0) best = best < 0 ? m : std::min(best, m);
    }
    job.log("  Calibration " + lid + ": empty program = " + (best >= 0 ? std::to_string(best) : std::string("?")) + " KB");
    return best > 0 ? best : 0;
}

// === COMPARISON AND CHECKER ===
static std::string stripCR(const std::string &s) { std::string o; o.reserve(s.size()); for (char c : s) if (c != '\r') o += c; return o; }
static std::string utf8Prefix(const std::string &s, size_t n) {
    // FIRST N CODE POINTS + ELLIPSIS
    size_t i = 0, cnt = 0;
    while (i < s.size() && cnt < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 1)));
        i += len;
        cnt++;
    }
    if (i >= s.size()) return s;
    return s.substr(0, i) + "…";
}
static std::string shortS(const std::string &b) { return utf8Prefix(b, 60); }
static bool isNumberTok(const std::string &t) {
    size_t i = 0, n = t.size();
    if (i < n && (t[i] == '+' || t[i] == '-')) i++;
    int digits = 0;
    while (i < n && isDigit(t[i])) { i++; digits++; }
    if (i < n && t[i] == '.') { i++; while (i < n && isDigit(t[i])) { i++; digits++; } }
    if (!digits) return false;
    if (i < n && (t[i] == 'e' || t[i] == 'E')) {
        i++;
        if (i < n && (t[i] == '+' || t[i] == '-')) i++;
        int ed = 0;
        while (i < n && isDigit(t[i])) { i++; ed++; }
        if (!ed) return false;
    }
    return i == n;
}
static std::vector<std::string> splitLinesRstrip(const std::string &s) {
    std::vector<std::string> out = splitChar(s, '\n', false);
    for (auto &l : out) { size_t e = l.size(); while (e > 0 && (l[e - 1] == ' ' || l[e - 1] == '\t')) e--; l.resize(e); }
    return out;
}
static std::pair<double, std::string> compareOutput(const Cfg &C, const std::string &outB, const std::string &okB) {
    std::string a = stripCR(outB), b = stripCR(okB);
    if (C.ignoreCase) { a = asciiLower(a); b = asciiLower(b); }
    if (C.cmpMode == "exact") {
        if (a == b) return {1.0, ""};
        size_t n = std::min(a.size(), b.size()), i = 0;
        while (i < n && a[i] == b[i]) i++;
        long line = 1;
        for (size_t k = 0; k < i; k++) if (b[k] == '\n') line++;
        return {0.0, "Difference at line " + std::to_string(line) + " (byte " + std::to_string(i + 1) + ")"};
    }
    if (C.cmpMode == "tokens" || C.cmpMode == "real") {
        auto ta = splitWs(a), tb = splitWs(b);
        for (size_t k = 0; k < std::min(ta.size(), tb.size()); k++) {
            const std::string &x = ta[k], &y = tb[k];
            if (x == y) continue;
            if (C.cmpMode == "real" && isNumberTok(x) && isNumberTok(y)) {
                double fx = std::strtod(x.c_str(), nullptr), fy = std::strtod(y.c_str(), nullptr);
                if (std::fabs(fx - fy) <= C.epsilon * std::max(1.0, std::fabs(fy))) continue;
            }
            return {0.0, "Token " + std::to_string(k + 1) + ": expected \"" + shortS(y) + "\", got \"" + shortS(x) + "\""};
        }
        if (ta.size() != tb.size()) return {0.0, "Different number of values: expected " + std::to_string(tb.size()) + ", got " + std::to_string(ta.size())};
        return {1.0, ""};
    }
    auto la = splitLinesRstrip(a), lb = splitLinesRstrip(b);
    while (!la.empty() && la.back().empty()) la.pop_back();
    while (!lb.empty() && lb.back().empty()) lb.pop_back();
    for (size_t k = 0; k < std::min(la.size(), lb.size()); k++)
        if (la[k] != lb[k]) return {0.0, "Line " + std::to_string(k + 1) + ": expected \"" + shortS(lb[k]) + "\", got \"" + shortS(la[k]) + "\""};
    if (la.size() != lb.size()) return {0.0, "Different number of lines: expected " + std::to_string(lb.size()) + ", got " + std::to_string(la.size())};
    return {1.0, ""};
}
static bool prepareChecker(const Cfg &C, Job &job, std::vector<std::string> &argvOut, std::string &err) {
    std::string spec = C.checker;
    while (!spec.empty() && std::isspace(static_cast<unsigned char>(spec.back()))) spec.pop_back();
    while (!spec.empty() && std::isspace(static_cast<unsigned char>(spec.front()))) spec.erase(0, 1);
    if (spec.empty()) for (const char *c : CHECKER_NAMES) if (sys::isFile(sys::join(C.testsDir, c))) { spec = c; break; }
    if (spec.empty()) { err = "No checker is set"; return false; }
    std::string p = sys::resolve(C.testsDir, subst(spec, C.v));
    if (!sys::isFile(p)) {
        std::string found = (spec.find('/') == std::string::npos && spec.find('\\') == std::string::npos) ? sys::findProgram(spec) : std::string();
        if (found.empty()) { err = "Checker not found: " + p; return false; }
        p = found;
    }
    std::string e = extOf(p);
    if (e == ".c" || e == ".cpp" || e == ".cc" || e == ".cxx") {
        const Value *L = C.langFor(p);
        if (!L) { err = "Unknown language for the checker " + p; return false; }
        std::string d = sys::join(sys::join(C.workDir, "checker"), C.name);
        Compiled cp = compileProgram(C, *L, {p}, p, d, sys::dirName(p), false, job);
        if (!cp.ok) { err = "The checker does not compile:\n" + cp.log; return false; }
        argvOut = {cp.exe};
        return true;
    }
    if (e == ".py") {
        const Value *L = C.langFor(p);
        std::string runt = L ? gs(*L, "run", "python3 {main}") : std::string(sys::IS_WIN ? "python {main}" : "python3 {main}");
        argvOut = buildArgv(runt, {p}, {}, "", p, 0, C.name);
        return true;
    }
    argvOut = {p};
    return true;
}
struct CheckRes { double f = 0; bool hasPts = false; double pts = 0; std::string msg, err; };
static std::string trimS(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t k = 0;
    while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) k++;
    return s.substr(k);
}
static CheckRes runChecker(const Cfg &C, Job &job, const std::string &inPath, const std::string &okPath, const std::string &outPath) {
    CheckRes cr;
    std::string cdir = sys::join(job.runDir(C), "check");
    sys::clearDir(cdir);
    std::string nIn = C.name + ".in", nOk = C.name + ".ok", nOut = C.name + ".out";
    if (!sys::copyFile(inPath, sys::join(cdir, nIn)) || !sys::copyFile(okPath, sys::join(cdir, nOk)) ||
        (!outPath.empty() && sys::isFile(outPath) && !sys::copyFile(outPath, sys::join(cdir, nOut)))) {
        cr.err = "cannot prepare the files: " + sys::lastError();
        return cr;
    }
    std::string argsT = trimS(C.checkerArgs);
    if (argsT.empty()) argsT = C.protocol == "oji" ? "" : (C.protocol == "fraction" ? "{input} {ok} {output}" : "{input} {output} {ok}");
    std::vector<std::string> argv = job.checkerArgv;
    for (auto &a : splitCmd(argsT))
        argv.push_back(replaceAll(replaceAll(replaceAll(replaceAll(a, "{input}", sys::join(cdir, nIn)), "{ok}", sys::join(cdir, nOk)), "{output}", sys::join(cdir, nOut)), "{problem}", C.name));
    std::string so = sys::join(cdir, "_stdout.txt"), se = sys::join(cdir, "_stderr.txt");
    sys::RunResult r = runP(argv, cdir, "", so, se, job.envPathAdd, 10000, true);
    std::string out = trimS(readText(so, 10000)), err = trimS(readText(se, 10000));
    if (!r.started) { cr.err = r.error; return cr; }
    if (r.timedOut) { cr.err = "the checker exceeded 10 s"; return cr; }
    if (C.protocol == "exitcode") {
        cr.f = (r.exited && r.exit == 0) ? 1.0 : 0.0;
        cr.msg = utf8Prefix(trimS(out + (err.empty() ? "" : " " + err)), 300);
        if (cr.msg.size() > 3 && cr.msg.compare(cr.msg.size() - 3, 3, "…") == 0) cr.msg.resize(cr.msg.size() - 3);
        return cr;
    }
    auto parts = splitWs(out);
    if (parts.empty()) { cr.err = "the checker printed nothing" + (err.empty() ? std::string() : ": " + err.substr(0, 200)); return cr; }
    std::string first = replaceAll(parts[0], ",", ".");
    char *end = nullptr;
    double val = std::strtod(first.c_str(), &end);
    if (!end || *end) { cr.err = "the checker output is not in the 'score message' format: " + out.substr(0, 200); return cr; }
    size_t pos = out.find(parts[0]) + parts[0].size();
    std::string msg = trimS(out.substr(pos));
    if (C.protocol == "fraction") {
        if (!err.empty()) msg = trimS(msg + " " + err);
        cr.f = std::max(0.0, std::min(1.0, val));
        cr.msg = msg.substr(0, 300);
        return cr;
    }
    cr.hasPts = true;
    cr.pts = val;
    cr.msg = msg.substr(0, 300);
    return cr;
}

// === SCORING ===
static bool isFatal(const std::string &raw) { return raw == "RE" || raw == "TLE" || raw == "MLE" || raw == "NOOUT" || raw == "CHKERR" || raw == "SKIP"; }
static double policyFactor(const Value &pol, double v, double L, double H) {
    std::string mode = gs(pol, "mode", "zero");
    if (mode == "ignore") return 1;
    if (mode == "percent") return std::max(0.0, std::min(100.0, gn(pol, "percent", 50))) / 100.0;
    if (mode == "linear") return H > L ? std::max(0.0, std::min(1.0, 1.0 - (v - L) / (H - L))) : 0.0;
    return 0;
}
static std::string policyNote(const Value &pol, double factor) {
    std::string mode = gs(pol, "mode", "zero");
    if (mode == "ignore") return " (ignored)";
    if (mode == "zero" || factor <= 0) return " (0 points)";
    return " (score reduced to " + fmtInt(factor * 100) + "%)";
}
static double scoreTest(const Cfg &C, const Value &t, Value &tr) {
    double P = gn(t, "points", 0), TP = gn(t, "timePoints", 0), MP = gn(t, "memPoints", 0);
    tr.set("max", round2(P + TP + MP));
    std::string raw = gs(tr, "raw", "SKIP");
    std::string msg0 = gs(tr, "msg0", "");
    if (isFatal(raw)) { tr.set("status", raw); tr.set("score", 0); tr.set("msg", msg0); return 0; }
    const Value &ptsV = tr["pts"];
    double base, f;
    if (!ptsV.isNull()) {
        double pts = ptsV.num(0);
        base = P > 0 ? std::max(0.0, std::min(pts, P)) : std::max(0.0, pts);
        f = P > 0 ? base / P : (pts > 0 ? 1.0 : 0.0);
    } else {
        f = std::max(0.0, std::min(1.0, tr["f"].num(0)));
        base = f * P;
    }
    double Lt = gn(t, "timeMs", 0);
    if (Lt == 0) Lt = C.timeMs;
    double Lm = gn(t, "memMb", 0);
    if (Lm == 0) Lm = C.memMb;
    double tval = C.timeMode == "wall" ? tr["wallMs"].num(0) : tr["cpuMs"].num(0);
    double mval = tr["memKb"].num(0);
    bool overT = Lt > 0 && tval > Lt;
    bool overM = Lm > 0 && !tr["memKb"].isNull() && mval > Lm * 1024;
    const Value &pol = G(C.prob, "policy");
    double ft = overT ? policyFactor(G(pol, "time"), tval, Lt, Lt * C.hardTime) : 1.0;
    double fm = overM ? policyFactor(G(pol, "memory"), mval, Lm * 1024, Lm * 1024 * C.hardMem) : 1.0;
    double score = base * ft * fm + (!overT ? f * TP : 0.0) + (!overM ? f * MP : 0.0);
    score = round2(score);
    std::string status = raw, msg = msg0;
    if (overT && gs(G(pol, "time"), "mode", "zero") != "ignore") {
        status = "TLE";
        msg = "Time limit exceeded: " + fmtInt(tval) + " ms > " + fmtInt(Lt) + " ms" + policyNote(G(pol, "time"), ft);
        if (raw != "OK") msg += "; " + msg0;
    } else if (overM && gs(G(pol, "memory"), "mode", "zero") != "ignore") {
        status = "MLE";
        msg = "Memory limit exceeded: " + fmtInt(mval) + " KB > " + fmtInt(Lm * 1024) + " KB" + policyNote(G(pol, "memory"), fm);
        if (raw != "OK") msg += "; " + msg0;
    } else {
        std::vector<std::string> notes;
        if (overT) notes.push_back("time limit exceeded (ignored)");
        if (overM) notes.push_back("memory limit exceeded (ignored)");
        if (!notes.empty()) {
            msg = msg0 + "; ";
            for (size_t i = 0; i < notes.size(); i++) msg += (i ? ", " : "") + notes[i];
        }
    }
    tr.set("status", status);
    tr.set("score", score);
    tr.set("msg", msg);
    return score;
}

Value rescore(const Value &ws, const Value &prob, Value res) {
    Cfg C(ws, prob);
    const Value &tests = G(prob, "tests");
    const Value &pts = G(prob, "points");
    double pCompile = gn(pts, "compile", 0), pSize = gn(pts, "size", 0);
    double maxScore = 0;
    if (tests.isArr()) for (auto &t : tests.a) maxScore += gn(t, "points", 0) + gn(t, "timePoints", 0) + gn(t, "memPoints", 0);
    maxScore = round2(maxScore + pCompile + pSize);
    if (!res.isObj()) res = Value::object();
    res.set("maxScore", maxScore);
    Value tl = Value::array();
    if (tests.isArr()) for (auto &t : tests.a) {
        Value x = Value::object();
        x.set("id", gs(t, "id", ""));
        x.set("points", gn(t, "points", 0));
        x.set("timePoints", gn(t, "timePoints", 0));
        x.set("memPoints", gn(t, "memPoints", 0));
        tl.push(x);
    }
    res.set("tests", tl);
    Value lim = Value::object();
    lim.set("timeMs", C.timeMs); lim.set("memMb", C.memMb); lim.set("stackMb", C.stackMb); lim.set("sourceKb", C.sourceKb); lim.set("timeMode", C.timeMode);
    res.set("limits", lim);
    const Value &polSize = G(G(prob, "policy"), "size");
    Value *studs = res.find("students");
    if (!studs || !studs->isArr()) { res.set("students", Value::array()); studs = res.find("students"); }
    for (auto &sr : studs->a) {
        sr.set("max", maxScore);
        Value notes = Value::array();
        if (sr["notes"].isArr()) for (auto &n : sr["notes"].a) if (n.str().rfind("Source exceeds", 0) != 0) notes.push(n);
        std::string status = gs(sr, "status", "");
        if (status == "CE" || status == "NOSRC" || status == "ERR") { sr.set("score", 0); sr.set("notes", notes); continue; }
        std::map<std::string, Value> byId;
        if (sr["tests"].isArr()) for (auto &tr : sr["tests"].a) byId[tr["id"].str()] = tr;
        Value newTests = Value::array();
        double total = 0;
        if (tests.isArr()) for (auto &t : tests.a) {
            std::string tid = gs(t, "id", "");
            Value tr;
            auto it = byId.find(tid);
            if (it != byId.end()) tr = it->second;
            else {
                tr = Value::object();
                tr.set("id", tid); tr.set("raw", "SKIP"); tr.set("msg0", "Not evaluated"); tr.set("f", 0); tr.set("pts", Value());
            }
            total += scoreTest(C, t, tr);
            newTests.push(tr);
        }
        sr.set("tests", newTests);
        total += pCompile;
        if (sr.find("sizeMain")) sr.set("sizeJudged", C.sizeScope == "all" ? sr["sizeAll"].num(0) : sr["sizeMain"].num(0)); // FOLLOW THE CURRENT SIZE CHOICE
        bool sizeOk = C.sourceKb <= 0 || sr["sizeJudged"].num(0) <= C.sourceKb * 1024;
        sr.set("sizeOk", sizeOk);
        if (sizeOk) total += pSize;
        else {
            std::string mode = gs(polSize, "mode", "zero");
            std::string limS = "Source exceeds the limit: " + fmt2(sr["sizeJudged"].num(0) / 1024.0) + " KB > " + fmtG(C.sourceKb) + " KB";
            if (mode == "zero") { total = 0; limS += " (0 points)"; }
            else if (mode == "percent") { double p = std::max(0.0, std::min(100.0, gn(polSize, "percent", 50))); total = total * p / 100.0; limS += " (score reduced to " + fmtInt(p) + "%)"; }
            else if (mode == "penalty") { double pen = gn(polSize, "penalty", 10); total = std::max(0.0, total - pen); limS += " (penalty " + fmtG(pen) + " points)"; }
            else limS += " (ignored)";
            notes.push(limS);
        }
        sr.set("notes", notes);
        sr.set("score", round2(total));
    }
    return res;
}

// === EVALUATION ===
static Value loadResults(const Cfg &C) {
    std::string t;
    if (!sys::readFile(sys::join(sys::join(C.resultsDir, C.name), "latest.json"), t)) return Value();
    std::string err;
    Value v = Value::parse(t, &err);
    if (!err.empty() || !v.isObj() || !v["students"].isArr()) return Value();
    return v;
}
static void saveResults(const Cfg &C, const Value &res, bool snapshot, Job &job) {
    std::string d = sys::join(C.resultsDir, C.name);
    sys::mkdirs(d);
    std::string text = res.dump(1);
    if (!sys::writeFile(sys::join(d, "latest.json"), text)) job.log("Cannot save the results: " + sys::lastError());
    if (snapshot) sys::writeFile(sys::join(d, "results-" + sys::stampStr() + ".json"), text);
}

static Value runTest(const Cfg &C, Job &job, const Student &st, const Info &info, const Value &L, const std::string &exe, const Value &t, long long baseline) {
    std::string tid = gs(t, "id", "");
    Value tr = Value::object();
    tr.set("id", tid); tr.set("raw", "RE"); tr.set("msg0", ""); tr.set("detail", ""); tr.set("f", 0); tr.set("pts", Value());
    tr.set("cpuMs", 0); tr.set("wallMs", 0); tr.set("timeMs", 0); tr.set("memKb", 0); tr.set("memRawKb", 0); tr.set("stackKb", Value());
    tr.set("exit", 0); tr.set("signal", 0); tr.set("timedOut", false); tr.set("stderr", ""); tr.set("out", ""); tr.set("err", "");
    auto setRaw = [&](const char *raw, const std::string &msg) { tr.set("raw", raw); tr.set("msg0", msg); return tr; };
    std::string inSrc = sys::resolve(C.testsDir, gs(t, "input", "")), okSrc = sys::resolve(C.testsDir, gs(t, "ok", ""));
    if (!sys::isFile(inSrc)) return setRaw("CHKERR", "Test input file missing: " + inSrc);
    std::string rdir = job.runDir(C);
    sys::mkdirs(rdir);
    std::string sroot;
    if (C.runIn == "project") sroot = info.root;
    else { sroot = sys::join(rdir, "sandbox"); sys::clearDir(sroot); }
    KV pv{{"problem", C.name}, {"nr", C.nr}, {"n", tid}};
    std::string cwd = sys::resolve(sroot, subst(C.ioCwd, pv));
    sys::mkdirs(cwd);
    std::string stdinPath;
    if (C.ioInput == "stdin") stdinPath = inSrc;
    else {
        std::string dst = sys::resolve(sroot, subst(C.ioInpath, pv));
        sys::mkdirs(sys::dirName(dst));
        sys::copyFile(inSrc, dst);
    }
    std::string so = sys::join(rdir, "stdout.txt"), se = sys::join(rdir, "stderr.txt"), probe = sys::join(rdir, "probe.txt");
    sys::removeFile(probe);
    std::string outPath;
    if (C.ioOutput == "stdout") outPath = so;
    else { outPath = sys::resolve(sroot, subst(C.ioOutpath, pv)); sys::removeFile(outPath); }
    double Lt = gn(t, "timeMs", 0);
    if (Lt == 0) Lt = C.timeMs;
    double Lm = gn(t, "memMb", 0);
    if (Lm == 0) Lm = C.memMb;
    double hardWall = Lt > 0 ? Lt * C.hardTime + 500 : 60000;
    long long cpuS = static_cast<long long>(std::ceil(hardWall / 1000.0)) + 1;
    long long memHard = Lm > 0 ? static_cast<long long>(Lm * 1024 * C.hardMem + 65536) : 0;
    long long stackKb = C.stackMb > 0 ? static_cast<long long>(C.stackMb * 1024) : 0;
    long long fsizeKb = C.outputMb > 0 ? static_cast<long long>(C.outputMb * 1024) : 0;
    sys::RunSpec spec;
    spec.argv = buildArgv(gs(L, "run", "{exe}"), {}, {}, exe, info.main, 0, C.name);
    spec.cwd = cwd;
    spec.stdinPath = stdinPath;
    spec.stdoutPath = so;
    spec.stderrPath = se;
    spec.env = C.probeEnv(L, probe, job);
    spec.wallMs = static_cast<long long>(hardWall);
    spec.cpuS = cpuS;
    spec.memKb = memHard;
    spec.stackKb = stackKb;
    spec.fsizeKb = fsizeKb;
    spec.single = true;
    spec.cancel = cancelFn();
    sys::RunResult r = sys::run(spec);
    auto pr = readProbe(probe);
    Value memRaw;
    if (pr.count("rss_kb")) memRaw = Value(static_cast<long long>(pr["rss_kb"]));
    else if (C.probeActive(L, job) && !sys::IS_WIN) memRaw = Value();
    else memRaw = Value(r.peakKb);
    long long base = C.calibrate ? baseline : 0;
    double cpu = r.cpuMs > 0 ? r.cpuMs : (pr.count("cpu_ms") ? pr["cpu_ms"] : 0);
    tr.set("cpuMs", round3(cpu));
    tr.set("wallMs", round3(r.wallMs));
    tr.set("timeMs", C.timeMode == "wall" ? round3(r.wallMs) : round3(cpu));
    tr.set("memRawKb", memRaw);
    tr.set("memKb", memRaw.isNull() ? Value() : Value(std::max(0LL, static_cast<long long>(memRaw.n) - base)));
    tr.set("stackKb", pr.count("stack_kb") ? Value(static_cast<long long>(pr["stack_kb"])) : Value());
    tr.set("exit", r.exit);
    tr.set("signal", r.signal);
    tr.set("timedOut", r.timedOut);
    std::string stderrHead = sys::isFile(se) ? readText(se, 2000) : std::string();
    tr.set("stderr", stderrHead);
    std::string lowErr = asciiLower(stderrHead);
    if (C.keepOutputs) {
        std::string kd = sys::join(sys::join(sys::join(C.workDir, "outputs"), C.name), st.id);
        sys::mkdirs(kd);
        std::string ko = sys::join(kd, safeName(tid) + ".out"), ke = sys::join(kd, safeName(tid) + ".err");
        sys::removeFile(ko);
        sys::removeFile(ke);
        if (sys::isFile(outPath) && sys::copyFile(outPath, ko)) tr.set("out", ko);
        if (sys::fileSize(se) > 0 && sys::copyFile(se, ke)) tr.set("err", ke);
    }
    if (r.cancelled) return setRaw("SKIP", "Stopped by the user");
    if (!r.started) return setRaw("RE", "The program could not be started: " + r.error);
    if (r.timedOut) {
        std::string m = "Time limit exceeded: stopped after " + fmtInt(r.wallMs) + " ms";
        if (r.cpuMs < 0.3 * r.wallMs) m += " (the program seems to be waiting, e.g. reading from the keyboard)";
        return setRaw("TLE", m);
    }
    bool crashed = !r.exited || (r.exit != 0 && C.nonzeroError);
    if (crashed) {
        bool memFail = lowErr.find("bad_alloc") != std::string::npos || lowErr.find("memoryerror") != std::string::npos ||
                       lowErr.find("out of memory") != std::string::npos || r.memLimitHit || r.winStatus == 0xC0000017u || r.winStatus == 0xC000012Du ||
                       (memHard > 0 && static_cast<double>(r.peakKb) >= 0.95 * static_cast<double>(memHard));
        if (memFail) return setRaw("MLE", "Memory limit exceeded (allocation failed)");
        return setRaw("RE", "Runtime error: " + sys::statusText(r));
    }
    if (!sys::isFile(outPath)) return setRaw("NOOUT", "Output file missing: " + sys::baseName(outPath));
    if (C.outputMb > 0 && static_cast<double>(sys::fileSize(outPath)) > C.outputMb * 1024 * 1024)
        return setRaw("WA", "Output too large (over " + fmtG(C.outputMb) + " MB)");
    if (C.cmpMode == "checker") {
        if (!job.haveChecker) return setRaw("CHKERR", "Checker error: " + job.checkerError);
        CheckRes cr = runChecker(C, job, inSrc, okSrc, outPath);
        if (!cr.err.empty()) return setRaw("CHKERR", "Checker error: " + cr.err);
        double f = cr.f;
        if (cr.hasPts) {
            tr.set("pts", cr.pts);
            double P = gn(t, "points", 0);
            f = P > 0 ? std::max(0.0, std::min(cr.pts, P)) / P : (cr.pts > 0 ? 1.0 : 0.0);
        }
        tr.set("f", f);
        std::string m = f >= 1 ? "Correct" : (f > 0 ? "Partial" : "Incorrect");
        if (!cr.msg.empty()) m += ": " + cr.msg;
        return setRaw(f >= 1 ? "OK" : (f > 0 ? "PARTIAL" : "WA"), m);
    }
    if (!sys::isFile(okSrc)) return setRaw("CHKERR", "Test answer file missing: " + okSrc);
    std::string ob, kb;
    if (!sys::readFile(outPath, ob) || !sys::readFile(okSrc, kb)) return setRaw("CHKERR", "Cannot read the output: " + sys::lastError());
    auto cmp = compareOutput(C, ob, kb);
    tr.set("f", cmp.first);
    tr.set("detail", cmp.second);
    return setRaw(cmp.first >= 1 ? "OK" : "WA", cmp.first >= 1 ? "Correct" : "Incorrect");
}

static void evaluateProblem(Job &job, const Value &ws, const Value &prob, const std::vector<std::string> &onlyS, const std::vector<std::string> &onlyT) {
    Cfg C(ws, prob);
    job.set("problem", C.name);
    job.set("phase", "preparing");
    job.set("student", "");
    job.set("test", "");
    std::vector<const Value *> tests;
    if (G(prob, "tests").isArr())
        for (auto &t : G(prob, "tests").a) if (onlyT.empty() || contains(onlyT, gs(t, "id", ""))) tests.push_back(&t);
    job.log("=== Problem " + C.name + " ===");
    if (tests.empty()) { job.log("The scoring scheme has no tests. Use \"Detect tests\" in the Tests and scoring tab."); return; }
    std::vector<Student> students = discoverStudents(C);
    if (!onlyS.empty()) {
        std::vector<Student> f;
        for (auto &s : students) if (contains(onlyS, s.id)) f.push_back(s);
        students = f;
    }
    { sys::Lock l(job.mu); job.total += static_cast<long>(students.size() * (tests.size() + 1)); }
    Value res = loadResults(C);
    if (!res.isObj()) res = Value::object();
    res.set("format", "olympiad-evaluator/results");
    res.set("version", 1);
    res.set("problem", C.name);
    res.set("variant", VARIANT);
    res.set("os", sys::osName());
    res.set("updated", sys::nowStr());
    if (!res["students"].isArr()) res.set("students", Value::array());
    res.set("baseline", Value::object());
    sys::mkdirs(C.workDir);
    sys::mkdirs(job.runDir(C));
    job.currentProbe = sys::join(C.workDir, "olieval_probe.c");
    job.runDirAny = job.runDir(C);
    job.cmdCache.clear();
    job.pyProbeDir = sys::join(C.workDir, "probe-py");
    if (!sys::writeFile(job.currentProbe, PROBE_C_SOURCE) || !sys::writeFile(sys::join(job.pyProbeDir, "sitecustomize.py"), PROBE_PY_SOURCE))
        job.log("Cannot write the probe: " + sys::lastError());
    job.preloadSo.clear();
    if (C.buildMode == "scripts" && std::string(sys::osName()) == "linux" && C.useProbe) {
        std::string so = sys::join(sys::join(C.workDir, "probe"), "olieval_probe.so");
        sys::mkdirs(sys::dirName(so));
        const Value *Lc = C.langFor("x.c");
        std::string cc = Lc ? langCompiler(*Lc, C).first : std::string();
        if (cc.empty()) cc = "gcc";
        std::string lp = sys::join(job.runDir(C), "_probe.txt");
        sys::RunResult r = runP({cc, "-O2", "-shared", "-fPIC", "-o", so, job.currentProbe}, "", "", lp, lp, {}, 60000);
        if (r.started && r.exited && r.exit == 0 && sys::isFile(so)) job.preloadSo = so;
        else job.log("The LD_PRELOAD probe could not be compiled; memory is measured from outside.");
    }
    job.envPathAdd.clear();
    if (sys::IS_WIN) {
        std::vector<std::string> dirs;
        for (auto &L : C.languages.a) {
            auto toks = splitCmd(gs(L, "compile", ""));
            if (toks.empty()) continue;
            std::string fp = sys::findProgram(toks[0]);
            if (!fp.empty() && !contains(dirs, sys::dirName(fp))) dirs.push_back(sys::dirName(fp));
        }
        if (!dirs.empty()) {
            std::string p = "PATH=";
            for (auto &d : dirs) p += d + ";";
            job.envPathAdd.push_back(p + sys::getEnv("PATH"));
        }
    }
    job.haveChecker = false;
    job.checkerArgv.clear();
    job.checkerError.clear();
    if (C.cmpMode == "checker") {
        job.haveChecker = prepareChecker(C, job, job.checkerArgv, job.checkerError);
        job.log(job.haveChecker ? "Checker: " + cmdString(job.checkerArgv) : "Checker: " + job.checkerError);
    }
    res = rescore(ws, prob, res);
    job.publish(C.name, res);
    for (auto &st : students) {
        if (job.stopping) break;
        job.set("student", st.id);
        job.set("test", "");
        job.set("phase", "compiling");
        job.log("Student " + st.id);
        long startDone;
        { sys::Lock l(job.mu); startDone = job.done; }
        Value sr = Value::object();
        sr.set("id", st.id); sr.set("name", st.name); sr.set("dir", st.dir); sr.set("root", ""); sr.set("main", ""); sr.set("lang", "");
        sr.set("sources", Value::array()); sr.set("sizeMain", 0); sr.set("sizeAll", 0); sr.set("sizeJudged", 0); sr.set("sizeOk", true);
        Value comp = Value::object();
        comp.set("ok", false); comp.set("ms", 0); comp.set("log", ""); comp.set("cmd", "");
        sr.set("compile", comp); sr.set("status", ""); sr.set("score", 0); sr.set("max", 0); sr.set("notes", Value::array());
        sr.set("evaluatedAt", sys::nowStr()); sr.set("tests", Value::array());
        try {
            if (!st.exists) {
                sr.set("status", "NOSRC");
                sr.find("notes")->push("No source (the student folder or file does not exist)");
                job.log("  no source");
            } else {
                Info info = detectProject(C, st);
                sr.set("root", info.root); sr.set("main", info.mainRel); sr.set("lang", info.lang); sr.set("sources", strArr(info.sources));
                sr.set("sizeMain", info.sizeMain); sr.set("sizeAll", info.sizeAll); sr.set("sizeJudged", info.sizeJudged);
                for (auto &w : info.warnings) sr.find("notes")->push(w);
                if (info.main.empty() || !info.langObj) { sr.set("status", "NOSRC"); job.log("  source not found"); }
                else {
                    const Value &L = *info.langObj;
                    std::vector<std::string> srcs;
                    if (C.buildMode == "objects") { for (auto &e : walkBfs(info.root, C.maxDepth, C.ignore)) if (!e.dir && contains(OBJ_EXTS, extOf(e.rel))) srcs.push_back(e.abs); }
                    else for (auto &s : info.sources) srcs.push_back(sys::joinRel(info.root, s));
                    auto libs = autoLibFlags(C, info.root, srcs, job);
                    std::string bdir = sys::join(sys::join(sys::join(C.workDir, "build"), C.name), safeName(st.id));
                    job.studentNow = st.id;
                    Compiled cp = buildStudent(C, L, info, bdir, job, libs);
                    Value c2 = Value::object();
                    c2.set("ok", cp.ok); c2.set("ms", std::floor(cp.ms * 10 + 0.5) / 10); c2.set("log", cp.log); c2.set("cmd", cp.cmd);
                    sr.set("compile", c2);
                    job.addDone(1);
                    if (!cp.ok) { sr.set("status", "CE"); job.log("  compilation error"); }
                    else {
                        std::string lid = gs(L, "id", "");
                        Value &bl = *res.find("baseline");
                        if (C.calibrate && !bl.find(lid)) bl.set(lid, calibrate(C, L, job));
                        long long baseline = C.calibrate ? static_cast<long long>(bl[lid].num(0)) : 0;
                        job.set("phase", "running");
                        for (auto *t : tests) {
                            if (job.stopping) break;
                            job.set("test", gs(*t, "id", ""));
                            Value tr = runTest(C, job, st, info, L, cp.exe, *t, baseline);
                            job.log("  test " + tr["id"].str() + ": " + tr["raw"].str() + " " + tr["msg0"].str());
                            sr.find("tests")->push(tr);
                            job.addDone(1);
                        }
                        sr.set("status", "DONE");
                    }
                }
            }
        } catch (const std::exception &e) {
            sr.set("status", "ERR");
            sr.find("notes")->push(std::string("Internal error: ") + e.what());
            job.log(std::string("  ERROR: ") + e.what());
        }
        { sys::Lock l(job.mu); job.done = std::min(job.total, startDone + static_cast<long>(tests.size()) + 1); }
        Value &studs = *res.find("students");
        long prev = -1;
        for (size_t i = 0; i < studs.a.size(); i++) if (studs.a[i]["id"].str() == st.id) { prev = static_cast<long>(i); break; }
        if (prev >= 0 && !onlyT.empty() && sr["status"].str() == "DONE" && studs.a[static_cast<size_t>(prev)]["status"].str() == "DONE") {
            Value merged = Value::array();
            std::vector<std::string> order;
            std::map<std::string, Value> m;
            for (auto &x : studs.a[static_cast<size_t>(prev)]["tests"].a) { std::string id = x["id"].str(); if (!m.count(id)) order.push_back(id); m[id] = x; }
            for (auto &x : sr["tests"].a) { std::string id = x["id"].str(); if (!m.count(id)) order.push_back(id); m[id] = x; }
            for (auto &id : order) merged.push(m[id]);
            sr.set("tests", merged);
        }
        if (prev < 0) studs.push(sr); else studs.a[static_cast<size_t>(prev)] = sr;
        std::stable_sort(studs.a.begin(), studs.a.end(), [](const Value &a, const Value &b) { return natcmp(a["id"].str(), b["id"].str()) < 0; });
        res.set("updated", sys::nowStr());
        res = rescore(ws, prob, res);
        saveResults(C, res, false, job);
        job.publish(C.name, res);
    }
    saveResults(C, res, true, job);
    job.publish(C.name, res);
}

bool startJob(const Value &ws, const std::vector<std::string> &problems, const std::vector<std::string> &onlyS, const std::vector<std::string> &onlyT,
              std::string &err) {
    {
        sys::Lock l(JOB.mu);
        if (JOB.running) { err = "An evaluation is already running"; return false; }
        JOB.running = true;
        JOB.stopping = false;
        JOB.phase = "starting";
        JOB.done = 0;
        JOB.total = 0;
        JOB.logs.clear();
        JOB.results = Value::object();
        JOB.error.clear();
        JOB.rv++;
    }
    Value wsCopy = ws;
    sys::startThread([wsCopy, problems, onlyS, onlyT]() {
        try {
            for (auto &pname : problems) {
                if (JOB.stopping) break;
                const Value *prob = findProblem(wsCopy, pname);
                if (!prob) { JOB.log("Problem " + pname + " does not exist"); continue; }
                evaluateProblem(JOB, wsCopy, *prob, onlyS, onlyT);
            }
            JOB.log(JOB.stopping ? "Evaluation stopped." : "Evaluation finished.");
        } catch (const std::exception &e) {
            JOB.log(std::string("ERROR: ") + e.what());
            sys::Lock l(JOB.mu);
            JOB.error = "Internal error";
        }
        sys::Lock l(JOB.mu);
        JOB.running = false;
        JOB.phase = JOB.stopping ? "stopped" : "done";
        JOB.student.clear();
        JOB.test.clear();
        JOB.done = JOB.total;
        JOB.rv++;
    });
    return true;
}
void stopJob() {
    JOB.stopping = true;
    JOB.log("Stopping the evaluation...");
}
Value status(long since, long rv) {
    sys::Lock l(JOB.mu);
    Value o = Value::object();
    o.set("running", JOB.running); o.set("stopping", JOB.stopping.load()); o.set("phase", JOB.phase); o.set("problem", JOB.problem);
    o.set("student", JOB.student); o.set("test", JOB.test); o.set("done", JOB.done); o.set("total", JOB.total);
    Value lg = Value::array();
    for (size_t i = static_cast<size_t>(std::max(0L, since)); i < JOB.logs.size(); i++) lg.push(JOB.logs[i]);
    o.set("log", lg);
    o.set("logNext", static_cast<long long>(JOB.logs.size()));
    o.set("rv", JOB.rv);
    o.set("error", JOB.error);
    if (rv != JOB.rv) o.set("results", JOB.results);
    return o;
}

Value scan(const Value &ws, const std::string &problem, std::string &err) {
    const Value *prob = findProblem(ws, problem);
    if (!prob) { err = "Problem not found: " + problem; return Value(); }
    Cfg C(ws, *prob);
    Value res = scanTests(C);
    Value studs = Value::array();
    for (auto &st : discoverStudents(C)) {
        Info info;
        if (st.exists) info = detectProject(C, st);
        else info.warnings.push_back("The student folder/file does not exist");
        studs.push(publicStudent(st, info));
    }
    res.set("students", studs);
    return res;
}

Value structure(const Value &ws, bool create) {
    std::string root = sys::absPath(gs(ws, "root", "").empty() ? std::string(".") : gs(ws, "root", "."));
    Value items = Value::array();
    auto add = [&](const std::string &p, const char *kind) {
        for (auto &x : items.a) if (x["path"].str() == p) return;
        Value it = Value::object();
        it.set("path", p);
        it.set("kind", kind);
        it.set("exists", sys::isDir(p));
        items.push(it);
    };
    const Value &probs = G(ws, "problems");
    Value dummy = Value::object();
    dummy.set("name", "_");
    const Value &p0 = (probs.isArr() && probs.size()) ? probs.a[0] : dummy;
    Cfg C0(ws, p0);
    add(C0.workDir, "work");
    add(C0.resultsDir, "results");
    std::vector<std::string> studs;
    if (G(ws, "students").isArr()) for (auto &s : G(ws, "students").a) { std::string id = trimS(gs(s, "id", "")); if (!id.empty()) studs.push_back(id); }
    if (probs.isArr()) for (auto &p : probs.a) {
        Cfg C(ws, p);
        if (C.layout == "flat") add(sys::resolve(root, subst(gs(ws, "flatDir", "{root}/{problem}/sources"), C.v)), "sources");
        else for (auto &s : studs) {
            KV kv = C.v;
            kv.insert(kv.begin(), {"student", s});
            add(sys::resolve(root, subst(gs(ws, "studentDir", "{root}/{problem}/{student}"), kv)), "student");
        }
        add(C.testsDir, "tests");
    }
    long created = 0;
    Value errors = Value::array();
    if (create) for (auto &it : items.a) {
        if (it["exists"].truthy(false)) continue;
        if (sys::mkdirs(it["path"].str())) { it.set("exists", true); created++; }
        else errors.push(it["path"].str() + ": " + sys::lastError());
    }
    Value o = Value::object();
    o.set("items", items);
    o.set("created", created);
    o.set("errors", errors);
    return o;
}

}  // NAMESPACE ENG
