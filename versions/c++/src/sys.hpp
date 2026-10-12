// SYS.HPP - SYSTEM LAYER OF THE C++ VERSION (LINUX/MACOS/WINDOWS)
// ALL STRINGS ARE UTF-8; WINDOWS USES THE W (UTF-16) APIS
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace sys {

#ifdef _WIN32
constexpr bool IS_WIN = true;
constexpr char SEP = '\\';
#else
constexpr bool IS_WIN = false;
constexpr char SEP = '/';
#endif
const char *osName();  // LINUX, WINDOWS OR DARWIN

// --- PATHS ---
bool isAbs(const std::string &p);
std::string normPath(const std::string &p);       // LIKE OS.PATH.NORMPATH, NATIVE SEPARATORS
std::string absPath(const std::string &p);        // ABSOLUTE (FROM THE CURRENT FOLDER) + NORMALIZED
std::string resolve(const std::string &base, const std::string &p);  // RELATIVE TO BASE UNLESS ABSOLUTE
std::string joinRel(const std::string &base, const std::string &rel); // REL WITH /, E.G. A/B.C
std::string join(const std::string &a, const std::string &b);
std::string dirName(const std::string &p);
std::string baseName(const std::string &p);
std::string relPath(const std::string &p, const std::string &base);   // P RELATIVE TO BASE (WITH /)

// --- FILES ---
bool isFile(const std::string &p);
bool isDir(const std::string &p);
bool exists(const std::string &p);
long long fileSize(const std::string &p);
long long mtime(const std::string &p);
bool mkdirs(const std::string &p);
struct DirEntry { std::string name; bool dir; long long size; long long mtime; };
bool listDir(const std::string &p, std::vector<DirEntry> &out);   // NO . AND ..; DIR FOLLOWS SYMLINKS
bool readFile(const std::string &p, std::string &out, long long limit = -1);
bool writeFile(const std::string &p, const std::string &data);    // CREATES PARENT FOLDERS
bool copyFile(const std::string &from, const std::string &to);
void removeFile(const std::string &p);
void clearDir(const std::string &p);                              // EMPTY IT (CREATE IF MISSING)
std::string lastError();

// --- ENVIRONMENT, TIME ---
std::string getEnv(const std::string &name);
std::string homeDir();
std::string currentDir();
std::string exeDir();      // EXECUTABLE FOLDER
double nowMs();            // MONOTONIC
std::string nowStr();      // YYYY-MM-DD HH:MM:SS
std::string stampStr();    // YYYYMMDD-HHMMSS
std::string clockStr();    // HH:MM:SS
int pid();

// --- PROCESSES ---
struct RunSpec {
    std::vector<std::string> argv;
    std::string cwd, stdinPath, stdoutPath, stderrPath;  // EMPTY = NULL DEVICE
    std::vector<std::string> env;                        // NAME=VALUE
    long long wallMs = 0, cpuS = 0, memKb = 0, stackKb = 0, fsizeKb = 0;
    bool single = false;
    std::function<bool()> cancel;
};
struct RunResult {
    bool started = false;
    std::string error;
    bool exited = false;
    int exit = 0;
    unsigned winStatus = 0;
    int signal = 0;
    bool timedOut = false, cancelled = false;
    double wallMs = 0, cpuMs = 0;
    long long peakKb = 0, peakCommitKb = 0;
    bool memLimitHit = false;
};
RunResult run(const RunSpec &spec);
std::string findProgram(const std::string &name);
std::string statusText(const RunResult &r);
void openPath(const std::string &path);
void openBrowser(const std::string &url);

// --- THREADS ---
void startThread(std::function<void()> fn);
class Mutex {
public:
    Mutex();
    ~Mutex();
    void lock();
    void unlock();
    Mutex(const Mutex &) = delete;
    Mutex &operator=(const Mutex &) = delete;
private:
    void *h;
};
class Lock {
public:
    explicit Lock(Mutex &m) : m_(m) { m_.lock(); }
    ~Lock() { m_.unlock(); }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
private:
    Mutex &m_;
};

}  // NAMESPACE SYS
