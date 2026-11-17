// Thin OS layer: everything that differs between Linux, Windows and macOS lives behind this.
// All paths are UTF-8 (converted to UTF-16 on Windows).
#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace host {
// memory
void *reserve(size_t len);                       // address space only
bool commit(void *p, size_t len);                // make read/write

// files
FILE *fopen(const std::string &path, const char *mode);
bool stat(const std::string &path, bool &is_dir, uint64_t &size, int64_t &mtime);
bool fstat(FILE *f, bool &is_dir, uint64_t &size, int64_t &mtime);
bool exists(const std::string &path);
int remove(const std::string &path);
int rename(const std::string &from, const std::string &to);
bool mkdirs(const std::string &path);           // like mkdir -p
int64_t ftell64(FILE *f);
int fseek64(FILE *f, int64_t off, int whence);

// locations
std::string exe_dir();
std::string exe_path();
std::string default_data_dir();                  // per-user writable directory for saves/logs

// time
void localtime(time_t t, struct tm &out);
void gmtime(time_t t, struct tm &out);
long gmtoff(time_t t);                           // seconds east of UTC for local time at t
int64_t now_us();                                // wall clock, microseconds since epoch
int64_t mono_ns();                               // monotonic clock

// dynamic libraries
void *dl_open(const char *path);
void *dl_sym(void *lib, const char *name);

// diagnostics
void install_crash_handler(void (*report)(const char *what, void *addr));
void show_error(const char *title, const char *msg);
}  // namespace host

namespace host {
// threads with an explicit host stack size (recompiled code recurses deeply on the host stack)
void thread_start(void (*fn)(void *), void *arg, size_t stack_bytes);
void sleep_ms(int ms);
void redirect_stderr(const std::string &path);
}
