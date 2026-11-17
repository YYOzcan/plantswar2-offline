#include "host.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cerrno>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <sys/stat.h>
#include <io.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <dlfcn.h>
#include <pthread.h>
#include <limits.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace host {
#ifdef _WIN32
static std::wstring W(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::string U8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
#endif

// ------------------------------------------------------------------ memory
void *reserve(size_t len) {
#ifdef _WIN32
    return VirtualAlloc(nullptr, len, MEM_RESERVE, PAGE_NOACCESS);
#else
    int fl = MAP_PRIVATE | MAP_ANON;
#ifdef MAP_NORESERVE
    fl |= MAP_NORESERVE;
#endif
    void *p = mmap(nullptr, len, PROT_NONE, fl, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
#endif
}
bool commit(void *p, size_t len) {
#ifdef _WIN32
    return VirtualAlloc(p, len, MEM_COMMIT, PAGE_READWRITE) != nullptr;
#else
    return mprotect(p, len, PROT_READ | PROT_WRITE) == 0;
#endif
}

// ------------------------------------------------------------------ files
FILE *fopen(const std::string &path, const char *mode) {
#ifdef _WIN32
    return _wfopen(W(path).c_str(), W(mode).c_str());
#else
    return ::fopen(path.c_str(), mode);
#endif
}
bool stat(const std::string &path, bool &is_dir, uint64_t &size, int64_t &mtime) {
#ifdef _WIN32
    struct _stat64 s; if (_wstat64(W(path).c_str(), &s)) return false;
#else
    struct ::stat s; if (::stat(path.c_str(), &s)) return false;
#endif
    is_dir = (s.st_mode & S_IFMT) == S_IFDIR; size = (uint64_t)s.st_size; mtime = (int64_t)s.st_mtime;
    return true;
}
bool fstat(FILE *f, bool &is_dir, uint64_t &size, int64_t &mtime) {
#ifdef _WIN32
    struct _stat64 s; if (_fstat64(_fileno(f), &s)) return false;
#else
    struct ::stat s; if (::fstat(fileno(f), &s)) return false;
#endif
    is_dir = (s.st_mode & S_IFMT) == S_IFDIR; size = (uint64_t)s.st_size; mtime = (int64_t)s.st_mtime;
    return true;
}
bool exists(const std::string &path) { bool d; uint64_t s; int64_t m; return stat(path, d, s, m); }
int remove(const std::string &path) {
#ifdef _WIN32
    std::wstring w = W(path);
    return (DeleteFileW(w.c_str()) || RemoveDirectoryW(w.c_str())) ? 0 : -1;
#else
    return ::remove(path.c_str());
#endif
}
int rename(const std::string &from, const std::string &to) {
#ifdef _WIN32
    return MoveFileExW(W(from).c_str(), W(to).c_str(), MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
#else
    return ::rename(from.c_str(), to.c_str());
#endif
}
static bool mkdir1(const std::string &p) {
#ifdef _WIN32
    return CreateDirectoryW(W(p).c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
#else
    return ::mkdir(p.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}
bool mkdirs(const std::string &path) {
    for (size_t i = 1; i <= path.size(); i++)
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            std::string part = path.substr(0, i);
            if (part.empty() || part.back() == ':') continue;          // "C:"
            if (!exists(part)) mkdir1(part);
        }
    return exists(path);
}
int64_t ftell64(FILE *f) {
#ifdef _WIN32
    return _ftelli64(f);
#else
    return ftello(f);
#endif
}
int fseek64(FILE *f, int64_t off, int whence) {
#ifdef _WIN32
    return _fseeki64(f, off, whence);
#else
    return fseeko(f, off, whence);
#endif
}

// ------------------------------------------------------------------ locations
std::string exe_path() {
    std::string p;
#ifdef _WIN32
    wchar_t b[32768]; DWORD n = GetModuleFileNameW(nullptr, b, 32768);
    b[n] = 0; p = U8(b);
    for (auto &ch : p) if (ch == '\\') ch = '/';
#elif defined(__APPLE__)
    char b[PATH_MAX * 2]; uint32_t n = sizeof b;
    if (_NSGetExecutablePath(b, &n) == 0) { char r[PATH_MAX]; p = realpath(b, r) ? r : b; }
#else
    char b[PATH_MAX]; ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
    if (n > 0) { b[n] = 0; p = b; }
#endif
    return p;
}
std::string exe_dir() {
    std::string p = exe_path();
    size_t k = p.rfind('/');
    return k == std::string::npos ? "." : p.substr(0, k);
}
std::string default_data_dir() {
#ifdef _WIN32
    wchar_t *w = nullptr; std::string base;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &w) == S_OK) { base = U8(w); CoTaskMemFree(w); }
    else { const wchar_t *e = _wgetenv(L"APPDATA"); base = e ? U8(e) : "."; }
    for (auto &ch : base) if (ch == '\\') ch = '/';
    return base + "/PlantsWar2";
#elif defined(__APPLE__)
    const char *h = getenv("HOME");
    return std::string(h ? h : ".") + "/Library/Application Support/PlantsWar2";
#else
    const char *x = getenv("XDG_DATA_HOME");
    if (x && *x) return std::string(x) + "/PlantsWar2";
    const char *h = getenv("HOME");
    return std::string(h ? h : ".") + "/.local/share/PlantsWar2";
#endif
}

// ------------------------------------------------------------------ time
void localtime(time_t t, struct tm &out) {
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
}
void gmtime(time_t t, struct tm &out) {
#ifdef _WIN32
    gmtime_s(&out, &t);
#else
    gmtime_r(&t, &out);
#endif
}
long gmtoff(time_t t) {
#ifdef _WIN32
    struct tm l, g; localtime(t, l); gmtime(t, g);
    l.tm_isdst = 0; g.tm_isdst = 0;
    return (long)difftime(mktime(&l), mktime(&g)) ;
#else
    struct tm l; localtime_r(&t, &l); return l.tm_gmtoff;
#endif
}
int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
int64_t mono_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------------ dynamic libraries
void *dl_open(const char *path) {
#ifdef _WIN32
    return (void *)LoadLibraryW(W(path).c_str());
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
void *dl_sym(void *lib, const char *name) {
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

// ------------------------------------------------------------------ threads
namespace { struct Start { void (*fn)(void *); void *arg; }; }
#ifdef _WIN32
static unsigned __stdcall thr_main(void *p) { Start s = *(Start *)p; delete (Start *)p; s.fn(s.arg); return 0; }
#else
static void *thr_main(void *p) { Start s = *(Start *)p; delete (Start *)p; s.fn(s.arg); return nullptr; }
#endif
void thread_start(void (*fn)(void *), void *arg, size_t stack_bytes) {
    Start *st = new Start{fn, arg};
#ifdef _WIN32
    HANDLE h = CreateThread(nullptr, stack_bytes, (LPTHREAD_START_ROUTINE)(void *)thr_main, st, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (h) CloseHandle(h);
#else
    pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, stack_bytes);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_t t; pthread_create(&t, &a, thr_main, st); pthread_attr_destroy(&a);
#endif
}
void redirect_stderr(const std::string &path) {
#ifdef _WIN32
    _wfreopen(W(path).c_str(), L"w", stderr);
#else
    if (!freopen(path.c_str(), "w", stderr)) return;
#endif
    setvbuf(stderr, nullptr, _IONBF, 0);
}
void sleep_ms(int ms) {
#ifdef _WIN32
    static bool fine = (timeBeginPeriod(1), true); (void)fine;   // 1 ms timer resolution for the frame limiter
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

// ------------------------------------------------------------------ diagnostics
static void (*g_report)(const char *, void *);
#ifdef _WIN32
static LONG WINAPI on_exc(EXCEPTION_POINTERS *e) {
    char what[64]; snprintf(what, sizeof what, "exception 0x%08lx", (unsigned long)e->ExceptionRecord->ExceptionCode);
    void *addr = e->ExceptionRecord->NumberParameters >= 2 ? (void *)e->ExceptionRecord->ExceptionInformation[1] : nullptr;
    if (g_report) g_report(what, addr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#else
static void on_sig(int sig, siginfo_t *si, void *) {
    char what[32]; snprintf(what, sizeof what, "signal %d", sig);
    if (g_report) g_report(what, si->si_addr);
    signal(sig, SIG_DFL); raise(sig);
}
#endif
void install_crash_handler(void (*report)(const char *, void *)) {
    g_report = report;
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_exc);
#else
    struct sigaction sa{}; sa.sa_sigaction = on_sig; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr); sigaction(SIGBUS, &sa, nullptr); sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr); sigaction(SIGABRT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);
#endif
}
void show_error(const char *title, const char *msg) {
#ifdef _WIN32
    MessageBoxW(nullptr, W(msg).c_str(), W(title).c_str(), MB_OK | MB_ICONERROR);
#else
    (void)title; (void)msg;
#endif
}
}  // namespace host
