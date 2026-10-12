// server.cpp — serverul HTTP local și API-ul variantei C++ (vezi docs/SPECIFICATIE.md).
// Pornire: evaluator-cpp [--port 8765] [--no-browser] [--gui cale/index.html]
// API Windows minim: Windows 7 (unele MinGW mai vechi pornesc de la XP/2003)
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

#include "embedded.hpp"
#include "engine.hpp"
#include "json.hpp"
#include "sys.hpp"

using js::Value;

static const char *APP_NAME = "Evaluator Olimpiadă";
static const char *VERSION = "1.0.0";
static int PORT = 8765;
static std::string TOKEN;
static std::string GUI_HTML;

struct UserError { std::string msg; };

static std::string settingsPath() { return sys::join(sys::join(sys::homeDir(), ".evaluator-olimpiada"), "setari.json"); }
static std::string findExample() {
    std::string d = sys::exeDir();
    for (const char *rel : {"exemplu", "../exemplu", "../../exemplu"}) {
        std::string p = sys::resolve(d, rel);
        if (sys::isFile(sys::join(p, "evaluator.json"))) return p;
    }
    return std::string();
}

static Value apiInfo() {
    std::string py = sys::IS_WIN ? "python" : "python3";
    Value o = Value::object();
    o.set("app", APP_NAME); o.set("version", VERSION); o.set("variant", eng::VARIANT); o.set("os", sys::osName());
    o.set("sep", std::string(1, sys::SEP)); o.set("home", sys::homeDir()); o.set("settings", settingsPath()); o.set("cwd", sys::currentDir());
    o.set("python", py); o.set("pid", sys::pid());
    Value comp = Value::object();
    comp.set("gcc", sys::findProgram("gcc")); comp.set("g++", sys::findProgram("g++")); comp.set("python", sys::findProgram(py));
    o.set("compilers", comp);
    Value feat = Value::object();
    feat.set("runner", "native"); feat.set("probe", !eng::PROBE_C_SOURCE.empty()); feat.set("memLimit", true); feat.set("cpuTime", true);
    o.set("features", feat);
    o.set("example", findExample());
    return o;
}

static Value apiFsList(const Value &b) {
    std::string p = b["path"].str();
    if (p.empty()) p = sys::homeDir();
    if (p[0] == '~') p = sys::homeDir() + p.substr(1);
    p = sys::absPath(p);
    if (!sys::isDir(p)) throw UserError{"Folderul nu există: " + p};
    std::vector<sys::DirEntry> es;
    sys::listDir(p, es);
    std::sort(es.begin(), es.end(), [](const sys::DirEntry &a, const sys::DirEntry &c) {
        if (a.dir != c.dir) return a.dir;
        return eng::natcmp(a.name, c.name) < 0;
    });
    Value arr = Value::array();
    for (auto &e : es) {
        Value x = Value::object();
        x.set("name", e.name); x.set("dir", e.dir); x.set("size", e.size); x.set("mtime", e.mtime);
        arr.push(x);
    }
    std::string parent = sys::dirName(p);
    Value roots = Value::array();
#ifdef _WIN32
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) if (mask & (1u << i)) roots.push(std::string(1, static_cast<char>('A' + i)) + ":\\");
#else
    roots.push("/");
#endif
    Value o = Value::object();
    o.set("path", p); o.set("parent", (parent.empty() || parent == p) ? std::string() : parent); o.set("sep", std::string(1, sys::SEP));
    o.set("entries", arr); o.set("roots", roots);
    return o;
}
static Value apiFsRead(const Value &b) {
    std::string p = b["path"].str();
    long long mx = static_cast<long long>(b["max"].num(2000000));
    if (!sys::isFile(p)) throw UserError{"Fișierul nu există: " + p};
    long long size = sys::fileSize(p);
    std::string text;
    if (!sys::readFile(p, text, mx)) throw UserError{"Nu pot citi " + p + ": " + sys::lastError()};
    Value o = Value::object();
    o.set("path", sys::absPath(p)); o.set("size", size); o.set("text", text); o.set("truncated", size > mx);
    return o;
}
static Value okObj() { Value o = Value::object(); o.set("ok", true); return o; }
static Value apiFsWrite(const Value &b) {
    std::string p = b["path"].str();
    if (p.empty()) throw UserError{"Cale lipsă"};
    if (!sys::writeFile(sys::absPath(p), b["text"].str())) throw UserError{"Nu pot scrie " + p + ": " + sys::lastError()};
    return okObj();
}
static Value apiFsStat(const Value &b) {
    std::string p = b["path"].str();
    Value o = Value::object();
    bool ex = !p.empty() && sys::exists(p);
    o.set("exists", ex); o.set("dir", ex && sys::isDir(p)); o.set("size", ex ? sys::fileSize(p) : 0); o.set("mtime", ex ? sys::mtime(p) : 0);
    return o;
}
static Value apiFsMkdir(const Value &b) {
    std::string p = b["path"].str();
    if (p.empty()) throw UserError{"Cale lipsă"};
    if (!sys::mkdirs(p)) throw UserError{"Nu pot crea " + p + ": " + sys::lastError()};
    return okObj();
}
static Value apiOpen(const Value &b) {
    std::string p = b["path"].str();
    if (!sys::exists(p)) throw UserError{"Nu există: " + p};
    sys::openPath(p);
    return okObj();
}
static std::vector<std::string> strList(const Value &v) {
    std::vector<std::string> out;
    if (v.isArr()) for (auto &x : v.a) out.push_back(x.str());
    else if (v.isStr()) out.push_back(v.s);
    return out;
}

// ---------------------------------------------------------------- HTTP
struct Request { std::string method, path, query; std::vector<std::pair<std::string, std::string>> headers; std::string body; };
static std::string header(const Request &r, const char *name) {
    for (auto &h : r.headers) if (h.first == name) return h.second;
    return std::string();
}
static bool recvAll(sock_t c, Request &req) {
    std::string buf;
    char tmp[16384];
    size_t hdrEnd = std::string::npos;
    while (hdrEnd == std::string::npos) {
        int n = static_cast<int>(recv(c, tmp, sizeof tmp, 0));
        if (n <= 0) return false;
        buf.append(tmp, static_cast<size_t>(n));
        hdrEnd = buf.find("\r\n\r\n");
        if (buf.size() > 65536 && hdrEnd == std::string::npos) return false;
    }
    std::string head = buf.substr(0, hdrEnd);
    std::string rest = buf.substr(hdrEnd + 4);
    size_t le = head.find("\r\n");
    std::string line = head.substr(0, le);
    size_t s1 = line.find(' '), s2 = line.find(' ', s1 + 1);
    if (s1 == std::string::npos || s2 == std::string::npos) return false;
    req.method = line.substr(0, s1);
    std::string target = line.substr(s1 + 1, s2 - s1 - 1);
    size_t q = target.find('?');
    req.path = target.substr(0, q);
    req.query = q == std::string::npos ? std::string() : target.substr(q + 1);
    size_t pos = le == std::string::npos ? head.size() : le + 2;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos) e = head.size();
        std::string h = head.substr(pos, e - pos);
        size_t colon = h.find(':');
        if (colon != std::string::npos) {
            std::string k = h.substr(0, colon), v = h.substr(colon + 1);
            for (auto &ch : k) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
            req.headers.push_back({k, v});
        }
        pos = e + 2;
    }
    long long len = std::atoll(header(req, "content-length").c_str());
    if (len < 0 || len > 200LL * 1024 * 1024) return false;
    req.body = rest;
    while (static_cast<long long>(req.body.size()) < len) {
        int n = static_cast<int>(recv(c, tmp, sizeof tmp, 0));
        if (n <= 0) return false;
        req.body.append(tmp, static_cast<size_t>(n));
    }
    if (static_cast<long long>(req.body.size()) > len) req.body.resize(static_cast<size_t>(len));
    return true;
}
static void sendAll(sock_t c, const std::string &data) {
    size_t off = 0;
    while (off < data.size()) {
        int n = static_cast<int>(send(c, data.data() + off, static_cast<int>(std::min<size_t>(data.size() - off, 1 << 20)), 0));
        if (n <= 0) return;
        off += static_cast<size_t>(n);
    }
}
static void respond(sock_t c, int code, const std::string &ctype, const std::string &body) {
    const char *reason = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" : "Internal Server Error";
    std::string head = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\nContent-Type: " + ctype + "\r\nContent-Length: " + std::to_string(body.size()) +
                       "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    sendAll(c, head + body);
}
static void respondJson(sock_t c, int code, const Value &v) { respond(c, code, "application/json; charset=utf-8", v.dump()); }
static Value errObj(const std::string &m) { Value o = Value::object(); o.set("error", m); return o; }
static std::string qparam(const std::string &q, const std::string &k) {
    size_t i = 0;
    while (i <= q.size()) {
        size_t e = q.find('&', i);
        if (e == std::string::npos) e = q.size();
        std::string kv = q.substr(i, e - i);
        size_t eq = kv.find('=');
        if (kv.substr(0, eq) == k) return eq == std::string::npos ? std::string() : kv.substr(eq + 1);
        i = e + 1;
    }
    return std::string();
}

static void handleConn(sock_t c) {
    Request req;
    if (!recvAll(c, req)) { CLOSESOCK(c); return; }
    std::string host = header(req, "host");
    bool hostOk = host == "127.0.0.1:" + std::to_string(PORT) || host == "localhost:" + std::to_string(PORT);
    bool auth = hostOk && header(req, "x-eval-token") == TOKEN;
    try {
        if (!hostOk) respond(c, 403, "text/plain; charset=utf-8", "Acces interzis");
        else if (req.method == "GET") {
            if (req.path == "/" || req.path == "/index.html") {
                std::string html = GUI_HTML;
                size_t k;
                while ((k = html.find("__EVAL_TOKEN__")) != std::string::npos) html.replace(k, 14, TOKEN);
                respond(c, 200, "text/html; charset=utf-8", html);
            } else if (req.path == "/api/info" && auth) respondJson(c, 200, apiInfo());
            else if (req.path == "/api/eval/status" && auth) {
                std::string l = qparam(req.query, "log"), r = qparam(req.query, "rv");
                respondJson(c, 200, eng::status(l.empty() ? 0 : std::atol(l.c_str()), r.empty() ? -1 : std::atol(r.c_str())));
            } else if (req.path.rfind("/api/", 0) == 0) respondJson(c, 403, errObj("Acces interzis"));
            else respond(c, 404, "text/plain; charset=utf-8", "Nu există");
        } else if (req.method != "POST") respond(c, 405, "text/plain; charset=utf-8", "Metodă nepermisă");
        else if (!auth) respondJson(c, 403, errObj("Acces interzis"));
        else {
            std::string perr;
            Value body = Value::parse(req.body.empty() ? std::string("{}") : req.body, &perr);
            if (!perr.empty()) respondJson(c, 400, errObj("JSON invalid"));
            else {
                if (!body.isObj()) body = Value::object();
                const std::string &p = req.path;
                if (p == "/api/fs/list") respondJson(c, 200, apiFsList(body));
                else if (p == "/api/fs/read") respondJson(c, 200, apiFsRead(body));
                else if (p == "/api/fs/write") respondJson(c, 200, apiFsWrite(body));
                else if (p == "/api/fs/stat") respondJson(c, 200, apiFsStat(body));
                else if (p == "/api/fs/mkdir") respondJson(c, 200, apiFsMkdir(body));
                else if (p == "/api/open") respondJson(c, 200, apiOpen(body));
                else if (p == "/api/structure") respondJson(c, 200, eng::structure(body["workspace"], body["create"].truthy(false)));
                else if (p == "/api/scan") {
                    std::string err;
                    Value r = eng::scan(body["workspace"], body["problem"].str(), err);
                    if (!err.empty()) throw UserError{err};
                    respondJson(c, 200, r);
                } else if (p == "/api/eval/start") {
                    std::string err;
                    if (!eng::startJob(body["workspace"], strList(body["problems"]), strList(body["students"]), strList(body["tests"]), err)) throw UserError{err};
                    respondJson(c, 200, okObj());
                } else if (p == "/api/eval/stop") { eng::stopJob(); respondJson(c, 200, okObj()); }
                else if (p == "/api/rescore") {
                    const Value *prob = eng::findProblem(body["workspace"], body["problem"].str());
                    if (!prob) throw UserError{"Problema nu există"};
                    Value o = Value::object();
                    o.set("results", eng::rescore(body["workspace"], *prob, body["results"]));
                    respondJson(c, 200, o);
                } else if (p == "/api/quit") {
                    respondJson(c, 200, okObj());
                    CLOSESOCK(c);
                    sys::startThread([]() {
#ifdef _WIN32
                        Sleep(300);
#else
                        usleep(300000);
#endif
                        std::exit(0);
                    });
                    return;
                } else respondJson(c, 404, errObj("Necunoscut: " + p));
            }
        }
    } catch (const UserError &e) {
        respondJson(c, 400, errObj(e.msg));
    } catch (const std::exception &e) {
        respondJson(c, 500, errObj(std::string("Eroare internă: ") + e.what()));
    }
    CLOSESOCK(c);
}

static std::string makeToken() {
    std::random_device rd;
    std::mt19937_64 g(rd() ^ (static_cast<unsigned long long>(std::time(nullptr)) << 20) ^ static_cast<unsigned long long>(sys::pid()));
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", static_cast<unsigned long long>(g()), static_cast<unsigned long long>(g()));
    return buf;
}

static int realMain(const std::vector<std::string> &args) {
    int port = 8765;
    bool browser = true;
    std::string guiFile;
    for (size_t i = 1; i < args.size(); i++) {
        if (args[i] == "--port" && i + 1 < args.size()) port = std::atoi(args[++i].c_str());
        else if (args[i] == "--no-browser") browser = false;
        else if (args[i] == "--gui" && i + 1 < args.size()) guiFile = args[++i];
    }
    GUI_HTML.assign(reinterpret_cast<const char *>(EMB_GUI), EMB_GUI_LEN);
    eng::PROBE_C_SOURCE.assign(reinterpret_cast<const char *>(EMB_PROBE_C), EMB_PROBE_C_LEN);
    eng::PROBE_PY_SOURCE.assign(reinterpret_cast<const char *>(EMB_PROBE_PY), EMB_PROBE_PY_LEN);
    if (!guiFile.empty()) { std::string t; if (sys::readFile(guiFile, t)) GUI_HTML = t; }
    TOKEN = makeToken();
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SetConsoleOutputCP(CP_UTF8);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    sock_t ls = INVALID_SOCKET;
    for (int p = port; p < port + 30; p++) {
        ls = socket(AF_INET, SOCK_STREAM, 0);
        if (ls == INVALID_SOCKET) continue;
#ifndef _WIN32
        fcntl(ls, F_SETFD, FD_CLOEXEC);
        int one = 1;
        setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#else
        SetHandleInformation(reinterpret_cast<HANDLE>(ls), HANDLE_FLAG_INHERIT, 0);
#endif
        sockaddr_in a;
        std::memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<unsigned short>(p));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(ls, reinterpret_cast<sockaddr *>(&a), sizeof a) == 0 && listen(ls, 64) == 0) { PORT = p; break; }
        CLOSESOCK(ls);
        ls = INVALID_SOCKET;
    }
    if (ls == INVALID_SOCKET) { std::printf("Nu găsesc niciun port liber.\n"); return 1; }
    std::string url = "http://127.0.0.1:" + std::to_string(PORT) + "/";
    std::printf("%s %s — varianta %s\nInterfața: %s\nÎnchide această fereastră (sau Ctrl+C) pentru a opri aplicația.\n", APP_NAME, VERSION, eng::VARIANT, url.c_str());
    std::fflush(stdout);
    if (browser) sys::openBrowser(url);
    for (;;) {
        sock_t c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
#ifndef _WIN32
        fcntl(c, F_SETFD, FD_CLOEXEC);
#else
        SetHandleInformation(reinterpret_cast<HANDLE>(c), HANDLE_FLAG_INHERIT, 0);
#endif
        sys::startThread([c]() { handleConn(c); });
    }
}

#ifdef _WIN32
int main() {
    int argc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], n, nullptr, nullptr);
        args.push_back(s);
    }
    LocalFree(wargv);
    return realMain(args);
}
#else
int main(int argc, char **argv) { return realMain(std::vector<std::string>(argv, argv + argc)); }
#endif
