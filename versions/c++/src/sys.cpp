// SYS.CPP - SYSTEM LAYER FOR POSIX AND WINDOWS
// MINIMUM WINDOWS API: WINDOWS 7 (OLD MINGW DEFAULTS TO XP/2003)
#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "sys.hpp"

#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <wchar.h>
#include <cwctype>
#else
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern char **environ;
#endif

namespace sys {

const char *osName() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "linux";
#endif
}

#ifdef _WIN32
static std::wstring W(const std::string &s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}
static std::string U(const std::wstring &w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}
static std::string winErr(DWORD code) {
    wchar_t *msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::string s = msg ? U(msg) : std::string();
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    return s + " (cod " + std::to_string(code) + ")";
}
#endif

static thread_local std::string g_lastError;
std::string lastError() { return g_lastError; }
static void setErr() {
#ifdef _WIN32
    g_lastError = winErr(GetLastError());
#else
    g_lastError = std::strerror(errno);
#endif
}

// === PATHS ===
bool isAbs(const std::string &p) {
    if (p.empty()) return false;
#ifdef _WIN32
    if (p[0] == '\\' || p[0] == '/') return true;
    return p.size() >= 3 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':' && (p[2] == '\\' || p[2] == '/');
#else
    return p[0] == '/';
#endif
}

std::string normPath(const std::string &in) {
    std::string s = in;
    for (auto &c : s) if (c == '\\') c = '/';
    std::string prefix;
    bool absolute = false;
#ifdef _WIN32
    if (s.size() >= 2 && s[0] == '/' && s[1] == '/') {
        size_t a = s.find('/', 2);
        size_t b = a == std::string::npos ? std::string::npos : s.find('/', a + 1);
        prefix = s.substr(0, b == std::string::npos ? s.size() : b) + "/";
        s = b == std::string::npos ? std::string() : s.substr(b + 1);
        absolute = true;
    } else if (s.size() >= 2 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':') {
        prefix = s.substr(0, 2);
        s = s.substr(2);
        if (!s.empty() && s[0] == '/') { prefix += '/'; absolute = true; }
    } else if (!s.empty() && s[0] == '/') {
        prefix = "/";
        absolute = true;
    }
#else
    if (!s.empty() && s[0] == '/') { prefix = "/"; absolute = true; }
#endif
    std::vector<std::string> st;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        std::string c = s.substr(i, j - i);
        if (c.empty() || c == ".") {
        } else if (c == "..") {
            if (!st.empty() && st.back() != "..") st.pop_back();
            else if (!absolute) st.push_back("..");
        } else st.push_back(c);
        i = j + 1;
    }
    std::string out = prefix;
    for (size_t k = 0; k < st.size(); k++) {
        if (k) out += '/';
        out += st[k];
    }
    if (out.empty()) out = ".";
#ifdef _WIN32
    for (auto &c : out) if (c == '/') c = '\\';
#endif
    return out;
}

std::string currentDir() {
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetCurrentDirectoryW(32768, buf);
    return n ? U(std::wstring(buf, n)) : std::string(".");
#else
    char buf[8192];
    return getcwd(buf, sizeof buf) ? std::string(buf) : std::string(".");
#endif
}

std::string absPath(const std::string &p) { return isAbs(p) ? normPath(p) : normPath(currentDir() + "/" + p); }

std::string resolve(const std::string &base, const std::string &pIn) {
    std::string p = pIn;
    while (!p.empty() && std::isspace(static_cast<unsigned char>(p.back()))) p.pop_back();
    size_t k = 0;
    while (k < p.size() && std::isspace(static_cast<unsigned char>(p[k]))) k++;
    p = p.substr(k);
    if (p.empty()) return normPath(base);
    if (isAbs(p)) return normPath(p);
    return normPath(base + "/" + p);
}
std::string joinRel(const std::string &base, const std::string &rel) { return normPath(base + "/" + rel); }
std::string join(const std::string &a, const std::string &b) { return normPath(a + "/" + b); }
std::string dirName(const std::string &pIn) {
    std::string p = normPath(pIn);
    size_t k = p.find_last_of("/\\");
    if (k == std::string::npos) return IS_WIN && p.size() >= 2 && p[1] == ':' ? p.substr(0, 2) : std::string("");
    if (k == 0) return p.substr(0, 1);
    if (IS_WIN && k == 2 && p[1] == ':') return p.substr(0, 3);
    return p.substr(0, k);
}
std::string baseName(const std::string &p) {
    size_t k = p.find_last_of("/\\");
    return k == std::string::npos ? p : p.substr(k + 1);
}
std::string relPath(const std::string &p, const std::string &base) {
    std::string a = normPath(p), b = normPath(base);
    if (a.size() > b.size() && a.compare(0, b.size(), b) == 0 && (a[b.size()] == '/' || a[b.size()] == '\\')) {
        std::string r = a.substr(b.size() + 1);
        for (auto &c : r) if (c == '\\') c = '/';
        return r;
    }
    return a;
}

// === FILES ===
#ifdef _WIN32
static bool attrs(const std::string &p, WIN32_FILE_ATTRIBUTE_DATA &d) {
    return GetFileAttributesExW(W(p).c_str(), GetFileExInfoStandard, &d) != 0;
}
static long long ftToUnix(FILETIME f) {
    unsigned long long v = (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    return static_cast<long long>(v / 10000000ULL) - 11644473600LL;
}
bool isFile(const std::string &p) { WIN32_FILE_ATTRIBUTE_DATA d; return attrs(p, d) && !(d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY); }
bool isDir(const std::string &p) { WIN32_FILE_ATTRIBUTE_DATA d; return attrs(p, d) && (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY); }
bool exists(const std::string &p) { WIN32_FILE_ATTRIBUTE_DATA d; return attrs(p, d); }
long long fileSize(const std::string &p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!attrs(p, d)) return 0;
    return (static_cast<long long>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}
long long mtime(const std::string &p) { WIN32_FILE_ATTRIBUTE_DATA d; return attrs(p, d) ? ftToUnix(d.ftLastWriteTime) : 0; }
bool mkdirs(const std::string &pIn) {
    std::string p = normPath(pIn);
    if (isDir(p)) return true;
    std::string parent = dirName(p);
    if (!parent.empty() && parent != p && !isDir(parent)) mkdirs(parent);
    if (CreateDirectoryW(W(p).c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    setErr();
    return false;
}
bool listDir(const std::string &p, std::vector<DirEntry> &out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(W(normPath(p) + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { setErr(); return false; }
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        DirEntry e;
        e.name = U(n);
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.size = e.dir ? 0 : ((static_cast<long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
        e.mtime = ftToUnix(fd.ftLastWriteTime);
        out.push_back(e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return true;
}
static FILE *u8open(const std::string &p, const char *mode) { return _wfopen(W(p).c_str(), W(mode).c_str()); }
bool copyFile(const std::string &a, const std::string &b) {
    if (CopyFileW(W(a).c_str(), W(b).c_str(), FALSE)) return true;
    setErr();
    return false;
}
void removeFile(const std::string &p) {
    std::wstring w = W(p);
    DWORD at = GetFileAttributesW(w.c_str());
    if (at == INVALID_FILE_ATTRIBUTES || (at & FILE_ATTRIBUTE_DIRECTORY)) return;
    if (at & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(w.c_str(), at & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
    DeleteFileW(w.c_str());
}
static void removeTree(const std::string &p) {
    std::vector<DirEntry> es;
    if (listDir(p, es)) {
        for (auto &e : es) {
            std::string c = p + "\\" + e.name;
            DWORD at = GetFileAttributesW(W(c).c_str());
            if (e.dir && !(at & FILE_ATTRIBUTE_REPARSE_POINT)) removeTree(c);
            else if (e.dir) RemoveDirectoryW(W(c).c_str());
            else removeFile(c);
        }
    }
    RemoveDirectoryW(W(p).c_str());
}
void clearDir(const std::string &p) {
    if (!isDir(p)) { mkdirs(p); return; }
    std::vector<DirEntry> es;
    if (!listDir(p, es)) return;
    for (auto &e : es) {
        std::string c = normPath(p) + "\\" + e.name;
        DWORD at = GetFileAttributesW(W(c).c_str());
        if (e.dir && !(at & FILE_ATTRIBUTE_REPARSE_POINT)) removeTree(c);
        else if (e.dir) RemoveDirectoryW(W(c).c_str());
        else removeFile(c);
    }
}
#else
bool isFile(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
bool isDir(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
bool exists(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0; }
long long fileSize(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0 ? static_cast<long long>(st.st_size) : 0; }
long long mtime(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0 ? static_cast<long long>(st.st_mtime) : 0; }
bool mkdirs(const std::string &pIn) {
    std::string p = normPath(pIn);
    if (isDir(p)) return true;
    std::string parent = dirName(p);
    if (!parent.empty() && parent != p && !isDir(parent)) mkdirs(parent);
    if (mkdir(p.c_str(), 0755) == 0 || errno == EEXIST) return true;
    setErr();
    return false;
}
bool listDir(const std::string &p, std::vector<DirEntry> &out) {
    DIR *d = opendir(p.c_str());
    if (!d) { setErr(); return false; }
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        DirEntry x;
        x.name = n;
        struct stat st;
        std::string full = p + "/" + n;
        if (stat(full.c_str(), &st) == 0) {
            x.dir = S_ISDIR(st.st_mode);
            x.size = x.dir ? 0 : static_cast<long long>(st.st_size);
            x.mtime = static_cast<long long>(st.st_mtime);
        } else {
            x.dir = false;
            x.size = 0;
            x.mtime = 0;
        }
        out.push_back(x);
    }
    closedir(d);
    return true;
}
static FILE *u8open(const std::string &p, const char *mode) { return std::fopen(p.c_str(), mode); }
bool copyFile(const std::string &a, const std::string &b) {
    FILE *in = std::fopen(a.c_str(), "rb");
    if (!in) { setErr(); return false; }
    FILE *out = std::fopen(b.c_str(), "wb");
    if (!out) { setErr(); std::fclose(in); return false; }
    char buf[65536];
    size_t n;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0) if (std::fwrite(buf, 1, n, out) != n) { ok = false; setErr(); break; }
    std::fclose(in);
    if (std::fclose(out) != 0) ok = false;
    return ok;
}
void removeFile(const std::string &p) {
    struct stat st;
    if (lstat(p.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) unlink(p.c_str());
}
static void removeTree(const std::string &p) {
    DIR *d = opendir(p.c_str());
    if (d) {
        while (struct dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            std::string c = p + "/" + n;
            struct stat st;
            if (lstat(c.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) removeTree(c);
            else unlink(c.c_str());
        }
        closedir(d);
    }
    rmdir(p.c_str());
}
void clearDir(const std::string &p) {
    if (!isDir(p)) { mkdirs(p); return; }
    DIR *d = opendir(p.c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (struct dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    closedir(d);
    for (auto &n : names) {
        std::string c = p + "/" + n;
        struct stat st;
        if (lstat(c.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) removeTree(c);
        else unlink(c.c_str());
    }
}
#endif

bool readFile(const std::string &p, std::string &out, long long limit) {
    out.clear();
    FILE *f = u8open(p, "rb");
    if (!f) { setErr(); return false; }
    char buf[65536];
    for (;;) {
        size_t want = sizeof buf;
        if (limit >= 0) {
            long long left = limit - static_cast<long long>(out.size());
            if (left <= 0) break;
            if (static_cast<long long>(want) > left) want = static_cast<size_t>(left);
        }
        size_t n = std::fread(buf, 1, want, f);
        if (n == 0) break;
        out.append(buf, n);
    }
    std::fclose(f);
    return true;
}
bool writeFile(const std::string &p, const std::string &data) {
    std::string d = dirName(p);
    if (!d.empty()) mkdirs(d);
    FILE *f = u8open(p, "wb");
    if (!f) { setErr(); return false; }
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    if (std::fclose(f) != 0) ok = false;
    if (!ok) setErr();
    return ok;
}

// === ENVIRONMENT AND TIME ===
std::string getEnv(const std::string &name) {
#ifdef _WIN32
    std::wstring w = W(name);
    DWORD n = GetEnvironmentVariableW(w.c_str(), nullptr, 0);
    if (!n) return std::string();
    std::wstring v(n, L'\0');
    n = GetEnvironmentVariableW(w.c_str(), &v[0], n);
    v.resize(n);
    return U(v);
#else
    const char *v = std::getenv(name.c_str());
    return v ? std::string(v) : std::string();
#endif
}
std::string homeDir() {
#ifdef _WIN32
    std::string h = getEnv("USERPROFILE");
    if (h.empty()) h = getEnv("HOMEDRIVE") + getEnv("HOMEPATH");
    return h.empty() ? std::string("C:\\") : normPath(h);
#else
    // NO GETPWUID: THE LINUX BINARY IS STATIC AND HOME IS ALMOST ALWAYS SET
    std::string h = getEnv("HOME");
    return h.empty() ? std::string("/") : normPath(h);
#endif
}
std::string exeDir() {
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    return n ? dirName(U(std::wstring(buf, n))) : currentDir();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t sz = sizeof buf;
    if (_NSGetExecutablePath(buf, &sz) == 0) {
        char real[4096];
        if (realpath(buf, real)) return dirName(real);
        return dirName(buf);
    }
    return currentDir();
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) { buf[n] = 0; return dirName(buf); }
    return currentDir();
#endif
}
double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
static std::tm localNow() {
    std::time_t t = std::time(nullptr);
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return tmv;
}
std::string nowStr() { std::tm t = localNow(); char b[32]; std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &t); return b; }
std::string stampStr() { std::tm t = localNow(); char b[32]; std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &t); return b; }
std::string clockStr() { std::tm t = localNow(); char b[32]; std::strftime(b, sizeof b, "%H:%M:%S", &t); return b; }
int pid() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

// === PROGRAM LOOKUP ===
#ifdef _WIN32
static bool hasExt(const std::string &n) { std::string b = baseName(n); return b.find('.') != std::string::npos; }
static std::vector<std::string> winCands(const std::string &d, const std::string &n) {
    std::vector<std::string> out;
    std::string base = d.empty() ? n : d + "\\" + n;
    if (hasExt(n)) { out.push_back(base); return out; }
    for (const char *e : {".exe", ".com", ".bat", ".cmd", ""}) out.push_back(base + e);
    return out;
}
static std::string lower(std::string s) { for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
static std::string findPythonInstall() {
    std::vector<std::string> bases;
    std::string la = getEnv("LOCALAPPDATA");
    if (!la.empty()) bases.push_back(la + "\\Programs\\Python");
    bases.push_back("C:\\");
    bases.push_back("C:\\Program Files");
    for (auto &b : bases) {
        std::vector<DirEntry> es;
        if (!listDir(b, es)) continue;
        std::string best;
        for (auto &e : es) if (e.dir && lower(e.name).rfind("python3", 0) == 0 && e.name > best) best = e.name;
        if (!best.empty()) {
            std::string c = b + "\\" + best + "\\python.exe";
            if (isFile(c)) return c;
        }
    }
    return std::string();
}
#endif

std::string findProgram(const std::string &name) {
    if (name.empty()) return std::string();
#ifdef _WIN32
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos || (name.size() > 1 && name[1] == ':')) {
        for (auto &c : winCands(dirName(name) == "" ? std::string(".") : dirName(name), baseName(name))) if (isFile(c)) return absPath(c);
        return std::string();
    }
    std::string ln = lower(name);
    bool isPy = ln == "python3" || ln == "python" || ln == "python3.exe" || ln == "python.exe";
    std::string store;
    auto search = [&](const std::string &n) -> std::string {
        std::string path = getEnv("PATH");
        size_t i = 0;
        while (i <= path.size()) {
            size_t j = path.find(';', i);
            if (j == std::string::npos) j = path.size();
            std::string d = path.substr(i, j - i);
            i = j + 1;
            if (!d.empty() && d.front() == '"') d.erase(0, 1);
            if (!d.empty() && d.back() == '"') d.pop_back();
            if (d.empty()) continue;
            for (auto &c : winCands(d, n)) {
                if (isFile(c)) {
                    if (isPy && lower(c).find("\\windowsapps\\") != std::string::npos) { if (store.empty()) store = c; continue; }
                    return c;
                }
            }
        }
        return std::string();
    };
    std::string found = search(name);
    if (found.empty() && isPy) {
        found = search("python");
        if (found.empty()) found = search("py");
        if (found.empty()) found = findPythonInstall();
    }
    static const char *extra[] = {
        "C:\\OJI\\Codeblocks\\MinGW\\bin", "D:\\OJI\\Codeblocks\\MinGW\\bin", "C:\\OJI\\CodeBlocks\\MinGW\\bin",
        "C:\\Program Files\\CodeBlocks\\MinGW\\bin", "C:\\Program Files (x86)\\CodeBlocks\\MinGW\\bin", "C:\\MinGW\\bin",
        "C:\\mingw64\\bin", "C:\\msys64\\ucrt64\\bin", "C:\\msys64\\mingw64\\bin", "C:\\TDM-GCC-64\\bin", "C:\\TDM-GCC-32\\bin",
        "C:\\Program Files\\Dev-Cpp\\MinGW64\\bin", "C:\\Program Files (x86)\\Dev-Cpp\\MinGW64\\bin"};
    for (const char *d : extra) {
        if (!found.empty()) break;
        for (auto &c : winCands(d, name)) if (isFile(c)) { found = c; break; }
    }
    return found.empty() ? store : found;
#else
    auto isExec = [](const std::string &p) { return isFile(p) && access(p.c_str(), X_OK) == 0; };
    if (name.find('/') != std::string::npos) return isExec(name) ? absPath(name) : std::string();
    std::string path = getEnv("PATH");
    if (path.empty()) path = "/usr/local/bin:/usr/bin:/bin";
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = path.find(':', i);
        if (j == std::string::npos) j = path.size();
        std::string d = path.substr(i, j - i);
        i = j + 1;
        std::string c = (d.empty() ? std::string(".") : d) + "/" + name;
        if (isExec(c)) return absPath(c);
    }
    return std::string();
#endif
}

// === DESCRIPTIONS ===
#ifndef _WIN32
static const char *signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV - invalid memory access (array out of bounds, bad pointer or stack overflow)";
    case SIGFPE: return "SIGFPE - arithmetic error (e.g. division by zero)";
    case SIGABRT: return "SIGABRT - program aborted (failed assert or uncaught exception)";
    case SIGKILL: return "SIGKILL - program killed";
    case SIGXCPU: return "SIGXCPU - CPU time limit exceeded";
    case SIGXFSZ: return "SIGXFSZ - output file too large";
    case SIGBUS: return "SIGBUS - memory access error";
    case SIGILL: return "SIGILL - illegal instruction";
    case SIGPIPE: return "SIGPIPE - write to a closed pipe";
    case SIGTERM: return "SIGTERM - program terminated";
    default: return nullptr;
    }
}
#else
static const char *ntstatusName(unsigned c) {
    switch (c) {
    case 0xC0000005u: return "ACCESS_VIOLATION - invalid memory access (array out of bounds or bad pointer)";
    case 0xC00000FDu: return "STACK_OVERFLOW - stack overflow (recursion too deep or large local arrays)";
    case 0xC0000094u: return "INTEGER_DIVIDE_BY_ZERO - division by zero";
    case 0xC000008Eu: return "FLOAT_DIVIDE_BY_ZERO - division by zero (floating point)";
    case 0xC0000095u: return "INTEGER_OVERFLOW - arithmetic overflow";
    case 0xC0000409u: return "STACK_BUFFER_OVERRUN - program aborted (abort or buffer overrun)";
    case 0xC0000017u: return "NO_MEMORY - not enough memory";
    case 0xC000012Du: return "COMMITMENT_LIMIT - memory limit reached";
    case 0xC000001Du: return "ILLEGAL_INSTRUCTION - illegal instruction";
    case 0xC0000374u: return "HEAP_CORRUPTION - heap corruption";
    case 0xC0000135u: return "DLL_NOT_FOUND - a DLL is missing (e.g. libstdc++-6.dll)";
    case 0xC0000139u: return "ENTRYPOINT_NOT_FOUND - incompatible DLL";
    case 0xC0000142u: return "DLL_INIT_FAILED - DLL initialization failed";
    case 0xC000013Au: return "CONTROL_C_EXIT - program interrupted";
    case 0xC0000420u: return "ASSERTION_FAILURE - failed assert";
    default: return nullptr;
    }
}
#endif
std::string statusText(const RunResult &r) {
    if (!r.started) return r.error;
    if (r.timedOut) return "stopped at the time limit";
    if (r.cancelled) return "stopped by the user";
#ifndef _WIN32
    if (r.signal) {
        const char *n = signalName(r.signal);
        return n ? std::string(n) : "signal " + std::to_string(r.signal);
    }
#else
    if (r.winStatus >= 0xC0000000u) {
        const char *n = ntstatusName(r.winStatus);
        if (n) return n;
        char b[64];
        std::snprintf(b, sizeof b, "runtime error (code 0x%08X)", r.winStatus);
        return b;
    }
#endif
    return "exit code " + std::to_string(r.exit);
}

// === RUNNING PROCESSES ===
#ifndef _WIN32
static void setLimit(int res, long long v) {
    struct rlimit rl;
    rlim_t want = static_cast<rlim_t>(v);
    if (getrlimit(res, &rl) != 0) return;
    if (rl.rlim_max != RLIM_INFINITY && want > rl.rlim_max) want = rl.rlim_max;
    rl.rlim_cur = want;
    rl.rlim_max = want;
    if (setrlimit(res, &rl) != 0) {
        getrlimit(res, &rl);
        rl.rlim_cur = want;
        setrlimit(res, &rl);
    }
}
static bool envNameEq(const char *a, const char *b) {
    while (*a && *a != '=' && *b && *b != '=') { if (*a != *b) return false; a++; b++; }
    return (*a == '=' || !*a) && (*b == '=' || !*b);
}

RunResult run(const RunSpec &s) {
    RunResult r;
    if (s.argv.empty()) { r.error = "Empty command"; return r; }
    std::string prog = findProgram(s.argv[0]);
    if (prog.empty()) { r.error = "Program not found: " + s.argv[0]; return r; }
    // PREPARE EVERYTHING THE CHILD NEEDS BEFORE FORK
    std::vector<std::string> args(s.argv);
    args[0] = prog;
    std::vector<char *> argv;
    for (auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    std::vector<char *> envp;
    for (char **e = environ; e && *e; e++) {
        bool skip = false;
        for (auto &x : s.env) if (envNameEq(*e, x.c_str())) { skip = true; break; }
        if (!skip) envp.push_back(*e);
    }
    for (auto &x : s.env) envp.push_back(const_cast<char *>(x.c_str()));
    envp.push_back(nullptr);
    int fin = open(s.stdinPath.empty() ? "/dev/null" : s.stdinPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fin < 0) { r.error = "Cannot open the input " + s.stdinPath + ": " + std::strerror(errno); return r; }
    int fout = open(s.stdoutPath.empty() ? "/dev/null" : s.stdoutPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fout < 0) { r.error = "Cannot create " + s.stdoutPath + ": " + std::strerror(errno); close(fin); return r; }
    int ferr;
    if (!s.stderrPath.empty() && s.stderrPath == s.stdoutPath) ferr = fout;
    else ferr = open(s.stderrPath.empty() ? "/dev/null" : s.stderrPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (ferr < 0) { r.error = "Cannot create " + s.stderrPath + ": " + std::strerror(errno); close(fin); close(fout); return r; }
    int ep[2];
    if (pipe(ep) != 0) { r.error = "pipe failed"; close(fin); close(fout); if (ferr != fout) close(ferr); return r; }
    fcntl(ep[0], F_SETFD, FD_CLOEXEC);
    fcntl(ep[1], F_SETFD, FD_CLOEXEC);
    const char *cwd = s.cwd.empty() ? nullptr : s.cwd.c_str();
    double t0 = nowMs();
    pid_t pid = fork();
    if (pid < 0) {
        r.error = std::string("fork failed: ") + std::strerror(errno);
        close(ep[0]); close(ep[1]); close(fin); close(fout); if (ferr != fout) close(ferr);
        return r;
    }
    if (pid == 0) {
        sigset_t all;
        setpgid(0, 0);
        sigemptyset(&all);
        sigprocmask(SIG_SETMASK, &all, nullptr);
        signal(SIGPIPE, SIG_DFL);
        if (s.cpuS > 0) { struct rlimit rl; rl.rlim_cur = static_cast<rlim_t>(s.cpuS); rl.rlim_max = static_cast<rlim_t>(s.cpuS + 1); setrlimit(RLIMIT_CPU, &rl); }
        if (s.memKb > 0) setLimit(RLIMIT_AS, s.memKb * 1024LL);
        if (s.stackKb > 0) setLimit(RLIMIT_STACK, s.stackKb * 1024LL);
        if (s.fsizeKb > 0) setLimit(RLIMIT_FSIZE, s.fsizeKb * 1024LL);
        struct rlimit core;
        core.rlim_cur = core.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &core);
        int e;
        if (dup2(fin, 0) < 0 || dup2(fout, 1) < 0 || dup2(ferr, 2) < 0) {
            e = errno;
            if (write(ep[1], "D", 1) < 0 || write(ep[1], &e, sizeof e) < 0) {}
            _exit(127);
        }
        if (cwd && chdir(cwd) != 0) {
            e = errno;
            if (write(ep[1], "C", 1) < 0 || write(ep[1], &e, sizeof e) < 0) {}
            _exit(127);
        }
        execve(argv[0], argv.data(), envp.data());
        e = errno;
        if (write(ep[1], "E", 1) < 0 || write(ep[1], &e, sizeof e) < 0) {}
        _exit(127);
    }
    close(ep[1]);
    close(fin);
    close(fout);
    if (ferr != fout) close(ferr);
    r.started = true;
    int status = 0;
    bool killed = false;
    struct rusage ru;
    std::memset(&ru, 0, sizeof ru);
    for (;;) {
        pid_t w = wait4(pid, &status, WNOHANG, &ru);
        if (w == pid) break;
        if (w < 0 && errno != EINTR) break;
        double el = nowMs() - t0;
        if (!killed) {
            bool cancel = s.cancel && s.cancel();
            if (cancel || (s.wallMs > 0 && el > static_cast<double>(s.wallMs))) {
                if (cancel) r.cancelled = true; else r.timedOut = true;
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
                killed = true;
            }
        }
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = el < 20.0 ? 100000L : (el < 1000.0 ? 500000L : 2000000L);
        nanosleep(&ts, nullptr);
    }
    double t1 = nowMs();
    kill(-pid, SIGKILL);
    r.wallMs = t1 - t0;
    char tag = 0;
    int e = 0;
    if (read(ep[0], &tag, 1) == 1 && read(ep[0], &e, sizeof e) == static_cast<ssize_t>(sizeof e)) {
        r.started = false;
        r.error = std::string(tag == 'C' ? "Cannot enter the working folder" : (tag == 'D' ? "Cannot redirect input/output" : "Cannot start the program")) +
                  ": " + std::strerror(e);
    }
    close(ep[0]);
    r.cpuMs = static_cast<double>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000.0 + static_cast<double>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000.0;
#if defined(__APPLE__)
    r.peakKb = static_cast<long long>(ru.ru_maxrss) / 1024;
#else
    r.peakKb = static_cast<long long>(ru.ru_maxrss);
#endif
    if (WIFEXITED(status)) {
        r.exited = !killed;
        r.exit = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        r.signal = WTERMSIG(status);
        if (r.signal == SIGXCPU) r.timedOut = true;
    }
    return r;
}
#else
static void quoteArg(std::wstring &out, const std::wstring &a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) { out += a; return; }
    out += L'"';
    for (size_t i = 0;; i++) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') { bs++; i++; }
        if (i == a.size()) { out.append(bs * 2, L'\\'); break; }
        if (a[i] == L'"') { out.append(bs * 2 + 1, L'\\'); out += L'"'; }
        else { out.append(bs, L'\\'); out += a[i]; }
    }
    out += L'"';
}
static HANDLE openHandle(const std::string &p, bool write, SECURITY_ATTRIBUTES *sa) {
    std::wstring w = W(p.empty() ? std::string("NUL") : p);
    if (!write)
        return CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return CreateFileW(w.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, sa, p.empty() ? OPEN_EXISTING : CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}
static bool envNameEqW(const wchar_t *a, const wchar_t *b) {
    while (*a && *a != L'=' && *b && *b != L'=') { if (towupper(*a) != towupper(*b)) return false; a++; b++; }
    return (*a == L'=' || !*a) && (*b == L'=' || !*b);
}
static double ftMs(FILETIME f) { return static_cast<double>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 10000.0; }

RunResult run(const RunSpec &s) {
    RunResult r;
    if (s.argv.empty()) { r.error = "Empty command"; return r; }
    std::string prog = findProgram(s.argv[0]);
    if (prog.empty()) { r.error = "Program not found: " + s.argv[0]; return r; }
    std::wstring wprog = W(prog), cmd;
    quoteArg(cmd, wprog);
    for (size_t i = 1; i < s.argv.size(); i++) { cmd += L' '; quoteArg(cmd, W(s.argv[i])); }
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;
    HANDLE hin = openHandle(s.stdinPath, false, &sa);
    if (hin == INVALID_HANDLE_VALUE) { r.error = "Cannot open the input: " + winErr(GetLastError()); return r; }
    HANDLE hout = openHandle(s.stdoutPath, true, &sa);
    if (hout == INVALID_HANDLE_VALUE) { r.error = "Cannot create the output file: " + winErr(GetLastError()); CloseHandle(hin); return r; }
    HANDLE herr = (!s.stderrPath.empty() && s.stderrPath == s.stdoutPath) ? hout : openHandle(s.stderrPath, true, &sa);
    if (herr == INVALID_HANDLE_VALUE) { r.error = "Cannot create the error file: " + winErr(GetLastError()); CloseHandle(hin); CloseHandle(hout); return r; }
    HANDLE hlist[3];
    DWORD nh = 0;
    hlist[nh++] = hin;
    hlist[nh++] = hout;
    if (herr != hout) hlist[nh++] = herr;
    STARTUPINFOEXW si;
    std::memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdInput = hin;
    si.StartupInfo.hStdOutput = hout;
    si.StartupInfo.hStdError = herr;
    SIZE_T alsz = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &alsz);
    std::unique_ptr<char[]> albuf(new char[alsz]);
    auto al = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(albuf.get());
    bool haveList = InitializeProcThreadAttributeList(al, 1, 0, &alsz) &&
                    UpdateProcThreadAttribute(al, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hlist, nh * sizeof(HANDLE), nullptr, nullptr);
    if (haveList) si.lpAttributeList = al;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        std::memset(&li, 0, sizeof li);
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
        if (s.single) { li.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS; li.BasicLimitInformation.ActiveProcessLimit = 1; }
        if (s.memKb > 0) { li.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY; li.ProcessMemoryLimit = static_cast<SIZE_T>(s.memKb * 1024LL); }
        if (s.cpuS > 0) { li.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_TIME; li.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = s.cpuS * 10000000LL; }
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // ENVIRONMENT: CURRENT + ADDITIONS
    std::wstring envb;
    {
        std::vector<std::wstring> add;
        for (auto &x : s.env) add.push_back(W(x));
        wchar_t *cur = GetEnvironmentStringsW();
        for (wchar_t *p = cur; p && *p; p += wcslen(p) + 1) {
            bool skip = false;
            for (auto &x : add) if (p[0] != L'=' && envNameEqW(p, x.c_str())) { skip = true; break; }
            if (!skip) { envb += p; envb += L'\0'; }
        }
        if (cur) FreeEnvironmentStringsW(cur);
        for (auto &x : add) { envb += x; envb += L'\0'; }
        envb += L'\0';
    }
    std::wstring wcwd = W(s.cwd);
    DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_BREAKAWAY_FROM_JOB;
    if (haveList) flags |= EXTENDED_STARTUPINFO_PRESENT;
    PROCESS_INFORMATION pi;
    std::memset(&pi, 0, sizeof pi);
    const wchar_t *app = (wprog.size() > 4 && _wcsicmp(wprog.c_str() + wprog.size() - 4, L".exe") == 0) ? wprog.c_str() : nullptr;
    std::vector<wchar_t> cmdbuf(cmd.begin(), cmd.end());
    cmdbuf.push_back(0);
    BOOL ok = CreateProcessW(app, cmdbuf.data(), nullptr, nullptr, TRUE, flags, &envb[0], s.cwd.empty() ? nullptr : wcwd.c_str(), &si.StartupInfo, &pi);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
        flags &= ~static_cast<DWORD>(CREATE_BREAKAWAY_FROM_JOB);
        ok = CreateProcessW(app, cmdbuf.data(), nullptr, nullptr, TRUE, flags, &envb[0], s.cwd.empty() ? nullptr : wcwd.c_str(), &si.StartupInfo, &pi);
    }
    if (!ok) {
        r.error = "Cannot start the program: " + winErr(GetLastError());
    } else {
        r.started = true;
        bool injob = job && AssignProcessToJobObject(job, pi.hProcess);
        LARGE_INTEGER freq, t0, t1;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        ResumeThread(pi.hThread);
        bool killed = false;
        for (;;) {
            if (WaitForSingleObject(pi.hProcess, 5) == WAIT_OBJECT_0) break;
            QueryPerformanceCounter(&t1);
            double el = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
            bool cancel = s.cancel && s.cancel();
            if (cancel || (s.wallMs > 0 && el > static_cast<double>(s.wallMs))) {
                if (cancel) r.cancelled = true; else r.timedOut = true;
                if (injob) TerminateJobObject(job, 1);
                TerminateProcess(pi.hProcess, 1);
                killed = true;
                WaitForSingleObject(pi.hProcess, 5000);
                break;
            }
        }
        QueryPerformanceCounter(&t1);
        r.wallMs = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        r.winStatus = code;
        r.exit = static_cast<int>(code);
        r.exited = !killed && code < 0xC0000000u;
        FILETIME a, b, k, u;
        if (GetProcessTimes(pi.hProcess, &a, &b, &k, &u)) r.cpuMs = ftMs(k) + ftMs(u);
        PROCESS_MEMORY_COUNTERS pmc;
        std::memset(&pmc, 0, sizeof pmc);
        pmc.cb = sizeof pmc;
        if (GetProcessMemoryInfo(pi.hProcess, &pmc, sizeof pmc)) {
            r.peakKb = static_cast<long long>(pmc.PeakWorkingSetSize / 1024);
            r.peakCommitKb = static_cast<long long>(pmc.PeakPagefileUsage / 1024);
        }
        if (injob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION q;
            std::memset(&q, 0, sizeof q);
            if (QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &q, sizeof q, nullptr)) {
                long long pk = static_cast<long long>(q.PeakProcessMemoryUsed / 1024);
                if (pk > r.peakCommitKb) r.peakCommitKb = pk;
            }
        }
        if (s.memKb > 0 && r.peakCommitKb * 100 >= s.memKb * 95) r.memLimitHit = true;
        if (s.cpuS > 0 && r.cpuMs >= static_cast<double>(s.cpuS) * 1000.0) r.timedOut = true;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (haveList) DeleteProcThreadAttributeList(al);
    CloseHandle(hin);
    CloseHandle(hout);
    if (herr != hout) CloseHandle(herr);
    if (job) CloseHandle(job);
    return r;
}
#endif

// === OPENING FILES / BROWSER ===
#ifndef _WIN32
static void spawnDetached(const std::vector<std::string> &args) {
    pid_t p = fork();
    if (p == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); }
        std::vector<char *> av;
        for (auto &a : args) av.push_back(const_cast<char *>(a.c_str()));
        av.push_back(nullptr);
        execvp(av[0], av.data());
        _exit(127);
    }
    if (p > 0) {
        // SETSID CHILD: A SMALL WAITER THREAD AVOIDS ZOMBIES
        startThread([p]() { int st; waitpid(p, &st, 0); });
    }
}
#endif
void openPath(const std::string &path) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", W(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    spawnDetached({"open", path});
#else
    spawnDetached({"xdg-open", path});
#endif
}
void openBrowser(const std::string &url) {
    std::vector<std::string> cands;
#ifdef _WIN32
    for (const char *v : {"ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"}) {
        std::string b = getEnv(v);
        if (b.empty()) continue;
        cands.push_back(b + "\\Microsoft\\Edge\\Application\\msedge.exe");
        cands.push_back(b + "\\Google\\Chrome\\Application\\chrome.exe");
    }
    for (auto &c : cands) {
        if (isFile(c)) {
            std::wstring args = L"--app=" + W(url) + L" --window-size=1400,900";
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", W(c).c_str(), args.c_str(), nullptr, SW_SHOWNORMAL)) > 32) return;
        }
    }
    ShellExecuteW(nullptr, L"open", W(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
#if defined(__APPLE__)
    cands = {"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"};
#else
    for (const char *n : {"google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "microsoft-edge"}) {
        std::string p = findProgram(n);
        if (!p.empty()) cands.push_back(p);
    }
#endif
    for (auto &c : cands) if (isFile(c)) { spawnDetached({c, "--app=" + url, "--window-size=1400,900"}); return; }
#if defined(__APPLE__)
    spawnDetached({"open", url});
#else
    spawnDetached({"xdg-open", url});
#endif
#endif
}

// === THREADS ===
#ifdef _WIN32
static DWORD WINAPI threadMain(LPVOID p) {
    std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()> *>(p));
    (*fn)();
    return 0;
}
void startThread(std::function<void()> fn) {
    auto *p = new std::function<void()>(std::move(fn));
    HANDLE h = CreateThread(nullptr, 0, threadMain, p, 0, nullptr);
    if (h) CloseHandle(h); else delete p;
}
Mutex::Mutex() { auto *cs = new CRITICAL_SECTION; InitializeCriticalSection(cs); h = cs; }
Mutex::~Mutex() { auto *cs = static_cast<CRITICAL_SECTION *>(h); DeleteCriticalSection(cs); delete cs; }
void Mutex::lock() { EnterCriticalSection(static_cast<CRITICAL_SECTION *>(h)); }
void Mutex::unlock() { LeaveCriticalSection(static_cast<CRITICAL_SECTION *>(h)); }
#else
static void *threadMain(void *p) {
    std::unique_ptr<std::function<void()>> fn(static_cast<std::function<void()> *>(p));
    (*fn)();
    return nullptr;
}
void startThread(std::function<void()> fn) {
    auto *p = new std::function<void()>(std::move(fn));
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8 * 1024 * 1024);
    if (pthread_create(&t, &at, threadMain, p) == 0) pthread_detach(t); else delete p;
    pthread_attr_destroy(&at);
}
Mutex::Mutex() { auto *m = new pthread_mutex_t; pthread_mutex_init(m, nullptr); h = m; }
Mutex::~Mutex() { auto *m = static_cast<pthread_mutex_t *>(h); pthread_mutex_destroy(m); delete m; }
void Mutex::lock() { pthread_mutex_lock(static_cast<pthread_mutex_t *>(h)); }
void Mutex::unlock() { pthread_mutex_unlock(static_cast<pthread_mutex_t *>(h)); }
#endif

}  // NAMESPACE SYS
