// libc / libm / pthread / socket / zlib imports of libGame (bionic, 32-bit ARM ABI) on a 64-bit host.
#include "rt.h"
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cctype>
#include <cwctype>
#include <ctime>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <map>
#include <zlib.h>
#include "host.h"
#include "server.h"

using namespace rt;
#define A(i) arg(c, i)

// bionic (= Linux) errno values seen by the guest
enum { G_EPERM = 1, G_ENOENT = 2, G_EINTR = 4, G_EIO = 5, G_EBADF = 9, G_EAGAIN = 11, G_ENOMEM = 12, G_EACCES = 13,
       G_EEXIST = 17, G_ENOTDIR = 20, G_EISDIR = 21, G_EINVAL = 22, G_ENOSPC = 28, G_ESPIPE = 29, G_ENOTSOCK = 88,
       G_EAFNOSUPPORT = 97, G_ECONNREFUSED = 111, G_EINPROGRESS = 115 };
static void gerr(int e) { cpu(); if (guest_errno) *guest_errno = e; }
void rt::set_errno_from_host() {
    int e = errno, g;
    switch (e) {
    case ENOENT: g = G_ENOENT; break; case EACCES: g = G_EACCES; break; case EEXIST: g = G_EEXIST; break;
    case ENOTDIR: g = G_ENOTDIR; break; case EISDIR: g = G_EISDIR; break; case EINVAL: g = G_EINVAL; break;
    case ENOMEM: g = G_ENOMEM; break; case ENOSPC: g = G_ENOSPC; break; case EBADF: g = G_EBADF; break;
    case EINTR: g = G_EINTR; break; case EPERM: g = G_EPERM; break; default: g = G_EIO;
    }
    gerr(g);
}
#define RET(v) (c->r[0] = (uint32_t)(v))

// ---------------------------------------------------------------- printf machinery
uint32_t VA::next32() {
    if (mem) { uint32_t v = R32(ap); ap += 4; return v; }
    return arg(c, idx++);
}
uint64_t VA::next64() {
    if (mem) { ap = (ap + 7) & ~7u; uint64_t v = (uint64_t)R32(ap) | ((uint64_t)R32(ap + 4) << 32); ap += 8; return v; }
    return arg64(c, idx);
}

std::string rt::gformat(const char *fmt, VA &va) {
    std::string out; char spec[64], buf[512];
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { out += *p; continue; }
        const char *s = p++;
        if (*p == '%') { out += '%'; continue; }
        std::string flags;
        while (*p && strchr("-+ #0'", *p)) flags += *p++;
        std::string width, prec;
        if (*p == '*') { width = std::to_string((int32_t)va.next32()); p++; } else while (isdigit((unsigned char)*p)) width += *p++;
        if (*p == '.') { p++; prec = "."; if (*p == '*') { prec += std::to_string((int32_t)va.next32()); p++; } else while (isdigit((unsigned char)*p)) prec += *p++; }
        int lng = 0;   // 0 int, 1 long, 2 long long, -1 short, -2 char, 3 size/ptrdiff
        while (*p && strchr("hlLqjzt", *p)) {
            if (*p == 'h') lng = lng == -1 ? -2 : -1;
            else if (*p == 'l') lng = lng == 1 ? 2 : 1;
            else if (*p == 'q' || *p == 'L' || *p == 'j') lng = 2;
            else lng = 3;
            p++;
        }
        char conv = *p;
        if (!conv) break;
        std::string base = "%" + flags + width + prec;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'c': {
            if (lng == 2) {
                uint64_t v = va.next64();
                snprintf(spec, sizeof spec, "%sll%c", base.c_str(), conv);
                snprintf(buf, sizeof buf, spec, v);
            } else {
                uint32_t v = va.next32();
                snprintf(spec, sizeof spec, "%s%c", base.c_str(), conv);
                if (conv == 'd' || conv == 'i') snprintf(buf, sizeof buf, spec, (int)(int32_t)(lng == -1 ? (int16_t)v : lng == -2 ? (int8_t)v : (int32_t)v));
                else snprintf(buf, sizeof buf, spec, (unsigned)(lng == -1 ? (uint16_t)v : lng == -2 ? (uint8_t)v : v));
            }
            out += buf; break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
            uint64_t u = va.next64(); double d; memcpy(&d, &u, 8);
            snprintf(spec, sizeof spec, "%s%c", base.c_str(), conv);
            snprintf(buf, sizeof buf, spec, d); out += buf; break;
        }
        case 's': {
            uint32_t sp = va.next32();
            const char *str = sp ? GSTR(sp) : "(null)";
            snprintf(spec, sizeof spec, "%ss", base.c_str());
            int n = snprintf(nullptr, 0, spec, str);
            std::string tmp(n + 1, 0); snprintf(&tmp[0], n + 1, spec, str); tmp.resize(n); out += tmp; break;
        }
        case 'p': snprintf(buf, sizeof buf, "0x%x", va.next32()); out += buf; break;
        case 'n': { uint32_t ptr = va.next32(); if (ptr) W32(ptr, (uint32_t)out.size()); break; }
        default: out.append(s, p + 1); break;
        }
    }
    return out;
}

static uint32_t put_string(uint32_t buf, uint32_t size, const std::string &s) {
    if (size) { size_t n = std::min<size_t>(s.size(), size - 1); memcpy(GPTR(buf), s.data(), n); W8(buf + n, 0); }
    return (uint32_t)s.size();
}
IMP(sprintf) { VA va{c, 2, 0, false}; std::string s = gformat(GSTR(A(1)), va); memcpy(GPTR(A(0)), s.c_str(), s.size() + 1); RET(s.size()); }
IMP(snprintf) { VA va{c, 3, 0, false}; RET(put_string(A(0), A(1), gformat(GSTR(A(2)), va))); }
IMP(vsnprintf) { VA va{c, 0, A(3), true}; RET(put_string(A(0), A(1), gformat(GSTR(A(2)), va))); }
IMP(printf) { VA va{c, 1, 0, false}; std::string s = gformat(GSTR(A(0)), va); fputs(s.c_str(), stdout); RET(s.size()); }
IMP(puts) { puts(GSTR(A(0))); RET(1); }
IMP(putchar) { putchar((int)A(0)); RET(A(0)); }
IMP(__android_log_print) {
    VA va{c, 3, 0, false}; std::string s = gformat(GSTR(A(2)), va);
    fprintf(stderr, "[%s] %s\n", A(1) ? GSTR(A(1)) : "?", s.c_str()); RET(1);
}

// sscanf: one host sscanf per conversion, pointers come from the guest varargs
IMP(sscanf) {
    const char *str = GSTR(A(0)), *fmt = GSTR(A(1)); VA va{c, 2, 0, false};
    int pos = 0, count = 0;
    for (const char *p = fmt; *p;) {
        if (*p != '%') {
            if (isspace((unsigned char)*p)) { while (isspace((unsigned char)str[pos])) pos++; p++; continue; }
            if (str[pos] != *p) break; pos++; p++; continue;
        }
        const char *s = p++;
        if (*p == '%') { if (str[pos] != '%') break; pos++; p++; continue; }
        bool suppress = false; if (*p == '*') { suppress = true; p++; }
        while (isdigit((unsigned char)*p)) p++;
        std::string len; while (*p && strchr("hlLqjzt", *p)) len += *p++;
        char conv = *p;
        if (conv == '[') { while (*p && *p != ']') p++; }
        p++;
        std::string piece(s, p); piece += "%n";
        int used = 0, got;
        if (suppress) { std::string pc = piece; got = sscanf(str + pos, pc.c_str(), &used); if (used == 0 && got < 0) break; pos += used; continue; }
        uint32_t dst = va.next32();
        if (strchr("dioux", conv) || conv == 'X') {
            if (len == "ll" || len == "q" || len == "j") { long long v = 0; got = sscanf(str + pos, piece.c_str(), &v, &used); if (got >= 1) { W32(dst, (uint32_t)v); W32(dst + 4, (uint32_t)((uint64_t)v >> 32)); } }
            else if (len == "h") { short v = 0; got = sscanf(str + pos, piece.c_str(), &v, &used); if (got >= 1) W16(dst, (uint16_t)v); }
            else if (len == "hh") { char v = 0; got = sscanf(str + pos, piece.c_str(), &v, &used); if (got >= 1) W8(dst, (uint8_t)v); }
            else { std::string pc = piece; size_t lp = pc.find('l'); if (len == "l" && lp != std::string::npos) pc.erase(lp, 1);
                   int v = 0; got = sscanf(str + pos, pc.c_str(), &v, &used); if (got >= 1) W32(dst, (uint32_t)v); }
        } else if (strchr("feEgGaA", conv)) {
            if (len == "l" || len == "L") { double v = 0; got = sscanf(str + pos, piece.c_str(), &v, &used); if (got >= 1) WV64(dst, v); }
            else { float v = 0; got = sscanf(str + pos, piece.c_str(), &v, &used); if (got >= 1) WVF(dst, v); }
        } else if (conv == 's' || conv == '[' || conv == 'c') {
            got = sscanf(str + pos, piece.c_str(), (char *)GPTR(dst), &used);
        } else if (conv == 'n') { W32(dst, pos); continue; }
        else break;
        if (got < 1) break;
        count++; pos += used;
    }
    RET(count == 0 && !*str ? -1 : count);
}

// ---------------------------------------------------------------- strings / ctype / stdlib
IMP(strlen) { RET(strlen(GSTR(A(0)))); }
IMP(strcmp) { RET(strcmp(GSTR(A(0)), GSTR(A(1)))); }
IMP(strncmp) { RET(strncmp(GSTR(A(0)), GSTR(A(1)), A(2))); }
static int lc(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch; }   // ASCII only (no Turkish-locale surprises)
static int ncase(const char *a, const char *b, size_t n) {
    for (size_t k = 0; k < n; k++) { int x = lc((unsigned char)a[k]), y = lc((unsigned char)b[k]); if (x != y) return x - y; if (!x) return 0; }
    return 0;
}
IMP(strcasecmp) { RET(ncase(GSTR(A(0)), GSTR(A(1)), (size_t)-1)); }
IMP(strncasecmp) { RET(ncase(GSTR(A(0)), GSTR(A(1)), A(2))); }
IMP(strcpy) { strcpy((char *)GPTR(A(0)), GSTR(A(1))); RET(A(0)); }
IMP(strncpy) { strncpy((char *)GPTR(A(0)), GSTR(A(1)), A(2)); RET(A(0)); }
IMP(strlcat) { const char *s = GSTR(A(1)); char *d = (char *)GPTR(A(0)); size_t sz = A(2), dl = strnlen(d, sz), sl = strlen(s);
               if (dl < sz) { size_t n = std::min(sl, sz - dl - 1); memcpy(d + dl, s, n); d[dl + n] = 0; } RET(dl + sl); }
IMP(strchr) { const char *r = strchr(GSTR(A(0)), (int)A(1)); RET(r ? G32(r) : 0); }
IMP(strrchr) { const char *r = strrchr(GSTR(A(0)), (int)A(1)); RET(r ? G32(r) : 0); }
IMP(strstr) { const char *r = strstr(GSTR(A(0)), GSTR(A(1))); RET(r ? G32(r) : 0); }
IMP(memchr) { const void *r = memchr(GPTR(A(0)), (int)A(1), A(2)); RET(r ? G32(r) : 0); }
IMP(memcmp) { RET(memcmp(GPTR(A(0)), GPTR(A(1)), A(2))); }
IMP(memcpy) { memcpy(GPTR(A(0)), GPTR(A(1)), A(2)); RET(A(0)); }
IMP(memmove) { memmove(GPTR(A(0)), GPTR(A(1)), A(2)); RET(A(0)); }
IMP(memset) { memset(GPTR(A(0)), (int)A(1), A(2)); RET(A(0)); }
IMP(bcopy) { memmove(GPTR(A(1)), GPTR(A(0)), A(2)); }
IMP(strdup) { RET(gstrdup(GSTR(A(0)))); }
IMP(strtok_r) {
    uint32_t s = A(0) ? A(0) : R32(A(2)); const char *delim = GSTR(A(1));
    if (!s) { RET(0); return; }
    while (R8(s) && strchr(delim, (int)R8(s))) s++;
    if (!R8(s)) { W32(A(2), s); RET(0); return; }
    uint32_t tok = s;
    while (R8(s) && !strchr(delim, (int)R8(s))) s++;
    if (R8(s)) { W8(s, 0); s++; }
    W32(A(2), s); RET(tok);
}
static void set_end(uint32_t endp, const char *base, char *e) { if (endp) W32(endp, G32(e ? e : base)); }
IMP(strtol) { char *e; long v = strtol(GSTR(A(0)), &e, A(2)); set_end(A(1), GSTR(A(0)), e);
              if (v > INT32_MAX) v = INT32_MAX; if (v < INT32_MIN) v = INT32_MIN; RET((int32_t)v); }
IMP(strtoul) { char *e; unsigned long v = strtoul(GSTR(A(0)), &e, A(2)); set_end(A(1), GSTR(A(0)), e); RET(v > UINT32_MAX ? UINT32_MAX : v); }
IMP(strtoll) { char *e; long long v = strtoll(GSTR(A(0)), &e, A(2)); set_end(A(1), GSTR(A(0)), e); ret64(c, (uint64_t)v); }
IMP(strtoull) { char *e; unsigned long long v = strtoull(GSTR(A(0)), &e, A(2)); set_end(A(1), GSTR(A(0)), e); ret64(c, v); }
IMP(strtod) { char *e; double v = strtod(GSTR(A(0)), &e); set_end(A(1), GSTR(A(0)), e); retd(c, v); }
IMP(atoi) { RET(atoi(GSTR(A(0)))); }
IMP(atoll) { ret64(c, (uint64_t)atoll(GSTR(A(0)))); }
static const char *gstrerror(int e) {
    switch (e) { case 0: return "Success"; case G_ENOENT: return "No such file or directory"; case G_EACCES: return "Permission denied";
    case G_EEXIST: return "File exists"; case G_EAGAIN: return "Try again"; case G_ECONNREFUSED: return "Connection refused";
    case G_EINPROGRESS: return "Operation now in progress"; case G_EINVAL: return "Invalid argument"; default: return "I/O error"; }
}
IMP(strerror) { static thread_local uint32_t buf; if (!buf) buf = gstatic(128); snprintf((char *)GPTR(buf), 128, "%s", gstrerror((int)A(0))); RET(buf); }
IMP(strerror_r) { snprintf((char *)GPTR(A(1)), A(2), "%s", gstrerror((int)A(0))); RET(0); }
IMP(basename) { const char *s = GSTR(A(0)); const char *r = strrchr(s, '/'); RET(G32(r ? r + 1 : s)); }
// ctype: "C" locale semantics on every host (bionic's ctype is ASCII-only)
static bool asc(uint32_t v) { return v < 128; }
IMP(tolower) { uint32_t v = A(0); RET(v >= 'A' && v <= 'Z' ? v + 32 : v); }
IMP(toupper) { uint32_t v = A(0); RET(v >= 'a' && v <= 'z' ? v - 32 : v); }
#define CT(f) IMP(f) { uint32_t v = A(0); RET(asc(v) && f((int)v) ? 1 : 0); }
CT(isalnum) CT(isalpha) CT(iscntrl) CT(islower) CT(isprint) CT(ispunct) CT(isspace) CT(isupper) CT(isxdigit)
// wide chars: bionic wchar_t is 32-bit (Windows' is 16-bit), so never hand guest strings to host wcs* functions
static bool bmp(uint32_t v) { return v < 0x10000; }
IMP(towlower) { uint32_t v = A(0); RET(bmp(v) ? (uint32_t)towlower((wint_t)v) : v); }
IMP(towupper) { uint32_t v = A(0); RET(bmp(v) ? (uint32_t)towupper((wint_t)v) : v); }
#define WCT(f) IMP(f) { uint32_t v = A(0); RET(bmp(v) && f((wint_t)v) ? 1 : 0); }
WCT(iswalpha) WCT(iswcntrl) WCT(iswdigit) WCT(iswlower) WCT(iswprint) WCT(iswpunct) WCT(iswspace) WCT(iswupper) WCT(iswxdigit)
IMP(wcslen) { uint32_t p = A(0), n = 0; while (R32(p + 4 * n)) n++; RET(n); }
IMP(wcscmp) {
    uint32_t a = A(0), b = A(1);
    for (;; a += 4, b += 4) { int32_t x = (int32_t)R32(a), y = (int32_t)R32(b); if (x != y) { RET(x < y ? -1 : 1); return; } if (!x) break; }
    RET(0);
}
IMP(wcsncpy) {
    uint32_t d = A(0), s = A(1), n = A(2), k = 0;
    for (; k < n && R32(s + 4 * k); k++) W32(d + 4 * k, R32(s + 4 * k));
    for (; k < n; k++) W32(d + 4 * k, 0);
    RET(A(0));
}
IMP(wmemcpy) { memcpy(GPTR(A(0)), GPTR(A(1)), (size_t)A(2) * 4); RET(A(0)); }
IMP(wmemmove) { memmove(GPTR(A(0)), GPTR(A(1)), (size_t)A(2) * 4); RET(A(0)); }
IMP(wmemset) { for (uint32_t k = 0; k < A(2); k++) W32(A(0) + 4 * k, A(1)); RET(A(0)); }

IMP(malloc) { RET(gmalloc(A(0))); }
IMP(calloc) { RET(gcalloc(A(0), A(1))); }
IMP(realloc) { RET(grealloc(A(0), A(1))); }
IMP(free) { gfree(A(0)); }
IMP(abort) { fatal("guest abort() from %s", sym_name(c->r[14])); }
IMP(exit) { logf("guest exit(%d)", (int)A(0)); fflush(nullptr); std::_Exit((int)A(0)); }
IMP(__assert2) { fatal("assert %s:%d %s: %s", GSTR(A(0)), (int)A(1), GSTR(A(2)), GSTR(A(3))); }
IMP(__stack_chk_fail) { fatal("stack smashing detected in %s", sym_name(c->r[14])); }
IMP(__cxa_atexit) { RET(0); }
IMP(__aeabi_atexit) { RET(0); }
IMP(__cxa_finalize) {}
IMP(__gnu_Unwind_Find_exidx) { RET(0); }
static std::atomic<uint64_t> rand48_state{0x1234ABCD330Eull};
IMP(lrand48) {
    uint64_t x = rand48_state.load(), n;
    do n = (x * 0x5DEECE66Dull + 0xB) & 0xFFFFFFFFFFFFull; while (!rand48_state.compare_exchange_weak(x, n));
    RET((uint32_t)(n >> 17));
}
IMP(srand48) { rand48_state = ((uint64_t)A(0) << 16) | 0x330E; }
IMP(raise) { logf("guest raise(%d)", (int)A(0)); RET(0); }
IMP(alarm) { RET(0); }
IMP(sigaction) { if (A(2)) memset(GPTR(A(2)), 0, 16); RET(0); }
IMP(setlocale) { static uint32_t s; if (!s) s = gstrdup("C"); RET(s); }
IMP(getenv) {
    const char *n = GSTR(A(0)); static std::map<std::string, uint32_t> cache;
    if (!strcmp(n, "HOME") || !strcmp(n, "TMPDIR")) { auto &v = cache[n]; if (!v) v = gstrdup(data_dir.c_str()); RET(v); }
    else RET(0);
}
IMP(geteuid) { RET(10123); }
IMP(getpwuid) { RET(0); }
IMP(sysconf) { int n = (int)A(0); RET(n == 0x27 || n == 0x28 ? 4096 : n == 0x60 || n == 0x61 ? 4 : (uint32_t)-1); }

static uint32_t cmp_fn;
static int qsort_trampoline(const void *a, const void *b) { return (int)(int32_t)call(cmp_fn, {G32(a), G32(b)}); }
IMP(qsort) {
    uint32_t saved = cmp_fn; cmp_fn = A(3);
    qsort(GPTR(A(0)), A(1), A(2), qsort_trampoline);
    cmp_fn = saved;
}

// ---------------------------------------------------------------- math (softfp)
#define M1(f) IMP(f) { int i = 0; double x = argd(c, i); retd(c, f(x)); }
#define M2(f) IMP(f) { int i = 0; double x = argd(c, i); double y = argd(c, i); retd(c, f(x, y)); }
#define MF(f) IMP(f) { retf(c, f(argf(c, 0))); }
M1(sin) M1(cos) M1(tan) M1(asin) M1(acos) M1(atan) M1(round) M2(atan2) M2(pow)
MF(sinf) MF(cosf) MF(asinf) MF(acosf)

// ---------------------------------------------------------------- time
IMP(time) { uint32_t t = (uint32_t)::time(nullptr); if (A(0)) W32(A(0), t); RET(t); }
IMP(gettimeofday) { int64_t us = host::now_us(); if (A(0)) { W32(A(0), (uint32_t)(us / 1000000)); W32(A(0) + 4, (uint32_t)(us % 1000000)); } RET(0); }
IMP(clock_gettime) {
    int64_t ns = A(0) == 0 ? host::now_us() * 1000 : host::mono_ns();     // 0 = CLOCK_REALTIME, others monotonic
    W32(A(1), (uint32_t)(ns / 1000000000)); W32(A(1) + 4, (uint32_t)(ns % 1000000000)); RET(0);
}
static void tm_to_guest(const struct tm &t, uint32_t g, long gmtoff) {
    int v[9] = {t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, t.tm_year, t.tm_wday, t.tm_yday, t.tm_isdst};
    for (int k = 0; k < 9; k++) W32(g + 4 * k, v[k]);
    W32(g + 36, (uint32_t)gmtoff); W32(g + 40, 0);
}
static struct tm tm_from_guest(uint32_t g) {
    struct tm t{}; int *f[9] = {&t.tm_sec, &t.tm_min, &t.tm_hour, &t.tm_mday, &t.tm_mon, &t.tm_year, &t.tm_wday, &t.tm_yday, &t.tm_isdst};
    for (int k = 0; k < 9; k++) *f[k] = (int)R32(g + 4 * k);
    return t;
}
IMP(localtime) { static thread_local uint32_t buf; if (!buf) buf = gstatic(44); time_t t = (int32_t)R32(A(0)); struct tm r; host::localtime(t, r); tm_to_guest(r, buf, host::gmtoff(t)); RET(buf); }
IMP(gmtime_r) { time_t t = (int32_t)R32(A(0)); struct tm r; host::gmtime(t, r); tm_to_guest(r, A(1), 0); RET(A(1)); }
IMP(mktime) { struct tm t = tm_from_guest(A(0)); time_t r = mktime(&t); tm_to_guest(t, A(0), host::gmtoff(r)); RET((uint32_t)r); }
IMP(strftime) { struct tm t = tm_from_guest(A(3)); RET(strftime((char *)GPTR(A(0)), A(1), GSTR(A(2)), &t)); }

// ---------------------------------------------------------------- descriptors
// Guest file descriptors are virtual: files map to host FILE*, sockets are in-process pipes to the
// embedded game server (server.cpp), so nothing ever touches the host network stack.
namespace {
struct VSock {
    int port = 0; bool connected = false;
    std::string out, in; size_t in_pos = 0; bool answered = false;
    uint32_t flags = 0;
};
struct Fd { FILE *f = nullptr; VSock *s = nullptr; bool dir = false; };
std::mutex fd_mu;
std::map<int, Fd> fds;
int next_fd = 3;
int fd_new(const Fd &d) { std::lock_guard<std::mutex> l(fd_mu); int n = next_fd++; fds[n] = d; return n; }
bool fd_get(int n, Fd &d) { std::lock_guard<std::mutex> l(fd_mu); auto it = fds.find(n); if (it == fds.end()) return false; d = it->second; return true; }
}  // namespace

// ---------------------------------------------------------------- files
// bionic FILE* handed to the guest = guest address of an 84-byte dummy; map to host FILE*
static std::mutex file_mu;
static std::unordered_map<uint32_t, FILE *> files;
static uint32_t sF;                                   // bionic __sF[3]
extern "C" uint32_t rt_sF() { if (!sF) { sF = gstatic(84 * 3); } return sF; }
static FILE *hostf(uint32_t g) {
    if (g == rt_sF()) return stdin;
    if (g == rt_sF() + 84) return stdout;
    if (g == rt_sF() + 168) return stderr;
    std::lock_guard<std::mutex> l(file_mu); auto it = files.find(g); return it == files.end() ? nullptr : it->second;
}
static std::string bin_mode(const char *m) {           // Windows needs "b" or it mangles \r\n
    std::string r; for (const char *p = m; *p; p++) if (*p != 'b' && *p != 't') r += *p;
    size_t plus = r.find('+'); if (plus == std::string::npos) r += 'b'; else r.insert(plus, "b");
    return r;
}
IMP(fopen) {
    std::string p = host_path(GSTR(A(0)));
    FILE *f = host::fopen(p, bin_mode(GSTR(A(1))).c_str());
    if (!f) { set_errno_from_host(); RET(0); return; }
    uint32_t g = gstatic(84);
    std::lock_guard<std::mutex> l(file_mu); files[g] = f; RET(g);
}
IMP(fclose) { FILE *f = hostf(A(0)); { std::lock_guard<std::mutex> l(file_mu); files.erase(A(0)); } RET(f ? fclose(f) : -1); }
IMP(fread) { FILE *f = hostf(A(3)); RET(f ? fread(GPTR(A(0)), A(1), A(2), f) : 0); }
IMP(fwrite) { FILE *f = hostf(A(3)); RET(f ? fwrite(GPTR(A(0)), A(1), A(2), f) : 0); }
IMP(fseek) { FILE *f = hostf(A(0)); RET(f ? host::fseek64(f, (int32_t)A(1), (int)A(2)) : -1); }
IMP(fseeko) { FILE *f = hostf(A(0)); RET(f ? host::fseek64(f, (int32_t)A(1), (int)A(2)) : -1); }
IMP(ftell) { FILE *f = hostf(A(0)); RET(f ? (uint32_t)host::ftell64(f) : (uint32_t)-1); }
IMP(ftello) { FILE *f = hostf(A(0)); RET(f ? (uint32_t)host::ftell64(f) : (uint32_t)-1); }
IMP(fgetpos) { FILE *f = hostf(A(0)); if (!f) { RET(-1); return; } W32(A(1), (uint32_t)host::ftell64(f)); RET(0); }
IMP(fsetpos) { FILE *f = hostf(A(0)); RET(f ? host::fseek64(f, R32(A(1)), SEEK_SET) : -1); }
IMP(fflush) { FILE *f = A(0) ? hostf(A(0)) : nullptr; RET(fflush(f)); }
IMP(fgets) { FILE *f = hostf(A(2)); char *r = f ? fgets((char *)GPTR(A(0)), (int)A(1), f) : nullptr; RET(r ? A(0) : 0); }
IMP(fputc) { FILE *f = hostf(A(1)); RET(f ? fputc((int)A(0), f) : -1); }
IMP(putc) { FILE *f = hostf(A(1)); RET(f ? fputc((int)A(0), f) : -1); }
IMP(getc) { FILE *f = hostf(A(0)); RET(f ? fgetc(f) : -1); }
IMP(ungetc) { FILE *f = hostf(A(1)); RET(f ? ungetc((int)A(0), f) : -1); }
IMP(setvbuf) { RET(0); }
IMP(__swbuf) { FILE *f = hostf(A(1)); RET(f ? fputc((int)A(0), f) : -1); }
IMP(clearerr) { FILE *f = hostf(A(0)); if (f) clearerr(f); }
IMP(fprintf) { VA va{c, 2, 0, false}; std::string s = gformat(GSTR(A(1)), va); FILE *f = hostf(A(0)); if (f) fputs(s.c_str(), f); RET(s.size()); }

IMP(open) {   // bionic flags: O_WRONLY 1, O_RDWR 2, O_CREAT 0100, O_EXCL 0200, O_TRUNC 01000, O_APPEND 02000
    std::string p = host_path(GSTR(A(0))); uint32_t fl = A(1);
    bool is_dir; uint64_t sz; int64_t mt;
    bool ex = host::stat(p, is_dir, sz, mt);
    if (ex && is_dir) { RET(fd_new(Fd{nullptr, nullptr, true})); return; }
    if (ex && (fl & 0300) == 0300) { gerr(G_EEXIST); RET(-1); return; }
    if (!ex && !(fl & 0100)) { gerr(G_ENOENT); RET(-1); return; }
    const char *mode = (fl & 3) == 0 ? "rb" : (fl & 02000) ? "ab" : (fl & 01000) || !ex ? ((fl & 3) == 2 ? "w+b" : "wb") : "r+b";
    FILE *f = host::fopen(p, mode);
    if (!f) { set_errno_from_host(); RET(-1); return; }
    RET(fd_new(Fd{f, nullptr, false}));
}
static bool netlog();
IMP(close) {
    if (netlog()) logf("close fd=%d", (int)A(0));
    Fd d; std::lock_guard<std::mutex> l(fd_mu);
    auto it = fds.find((int)A(0)); if (it == fds.end()) { gerr(G_EBADF); RET(-1); return; }
    d = it->second; fds.erase(it);
    if (d.f) fclose(d.f);
    delete d.s;
    RET(0);
}
IMP(read) {
    Fd d; if (!fd_get((int)A(0), d) || !d.f) { gerr(G_EBADF); RET(-1); return; }
    size_t n = fread(GPTR(A(1)), 1, A(2), d.f); RET(n);
}
IMP(write) {
    int fd = (int)A(0);
    if (fd == 1 || fd == 2) { fwrite(GPTR(A(1)), 1, A(2), fd == 1 ? stdout : stderr); RET(A(2)); return; }
    Fd d; if (!fd_get(fd, d) || !d.f) { gerr(G_EBADF); RET(-1); return; }
    RET(fwrite(GPTR(A(1)), 1, A(2), d.f));
}
IMP(lseek) {
    Fd d; if (!fd_get((int)A(0), d) || !d.f) { gerr(G_ESPIPE); RET(-1); return; }
    if (host::fseek64(d.f, (int32_t)A(1), (int)A(2))) { gerr(G_EINVAL); RET(-1); return; }
    RET((uint32_t)host::ftell64(d.f));
}
IMP(access) { std::string p = host_path(GSTR(A(0))); if (host::exists(p)) RET(0); else { gerr(G_ENOENT); RET(-1); } }
IMP(remove) { std::string p = host_path(GSTR(A(0))); int r = host::remove(p); if (r) gerr(G_ENOENT); RET(r); }
IMP(rename) { std::string a = host_path(GSTR(A(0))), b = host_path(GSTR(A(1))); int r = host::rename(a, b); if (r) gerr(G_ENOENT); RET(r); }
IMP(chdir) { RET(0); }
IMP(getcwd) { const std::string &d = data_dir; if (A(0)) { put_string(A(0), A(1), d); RET(A(0)); } else RET(gstrdup(d.c_str())); }
static void stat_to_guest(bool is_dir, uint64_t size, int64_t mtime, uint32_t g) {   // bionic struct stat (104 bytes)
    memset(GPTR(g), 0, 104);
    W32(g + 16, is_dir ? 040755 : 0100644); W32(g + 20, 1);
    W32(g + 48, (uint32_t)size); W32(g + 52, (uint32_t)(size >> 32)); W32(g + 56, 4096); W32(g + 64, (uint32_t)((size + 511) / 512));
    W32(g + 72, (uint32_t)mtime); W32(g + 80, (uint32_t)mtime); W32(g + 88, (uint32_t)mtime);
}
IMP(stat) {
    bool d; uint64_t sz; int64_t mt; std::string p = host_path(GSTR(A(0)));
    if (!host::stat(p, d, sz, mt)) { gerr(G_ENOENT); RET(-1); return; }
    stat_to_guest(d, sz, mt, A(1)); RET(0);
}
IMP(fstat) {
    Fd fd; if (!fd_get((int)A(0), fd)) { gerr(G_EBADF); RET(-1); return; }
    bool d = fd.dir; uint64_t sz = 0; int64_t mt = 0;
    if (fd.f) host::fstat(fd.f, d, sz, mt);
    stat_to_guest(d, sz, mt, A(1)); RET(0);
}
// mmap: private copies inside the guest heap (the game only maps files read-only / anonymous scratch)
static std::mutex map_mu;
static std::unordered_map<uint32_t, uint32_t> maps;
IMP(mmap) {
    uint32_t len = A(1), flags = A(3), off = A(5); int fd = (int)A(4);
    uint32_t p = gmalloc(len + 4096); uint32_t al = (p + 4095) & ~4095u;
    memset(GPTR(al), 0, len);
    if (!(flags & 0x20)) {                                   // not MAP_ANONYMOUS: read the file contents
        Fd d; if (!fd_get(fd, d) || !d.f) { gfree(p); gerr(G_EBADF); RET(0xffffffffu); return; }
        int64_t pos = host::ftell64(d.f); host::fseek64(d.f, off, SEEK_SET);
        size_t n = fread(GPTR(al), 1, len, d.f); (void)n; host::fseek64(d.f, pos, SEEK_SET);
    }
    std::lock_guard<std::mutex> l(map_mu); maps[al] = p; RET(al);
}
IMP(munmap) {
    std::lock_guard<std::mutex> l(map_mu);
    auto it = maps.find(A(0)); if (it != maps.end()) { gfree(it->second); maps.erase(it); }
    RET(0);
}

// ---------------------------------------------------------------- errno
IMP(__errno) { cpu(); RET(G32(guest_errno)); }

// ---------------------------------------------------------------- pthread
struct GMutex { std::recursive_mutex m; };
struct GCond { std::condition_variable_any cv; };
static std::mutex sync_mu;
static std::unordered_map<uint32_t, GMutex *> mutexes;
static std::unordered_map<uint32_t, GCond *> conds;
static GMutex *mtx(uint32_t g) { std::lock_guard<std::mutex> l(sync_mu); auto &m = mutexes[g]; if (!m) m = new GMutex(); return m; }
static GCond *cnd(uint32_t g) { std::lock_guard<std::mutex> l(sync_mu); auto &m = conds[g]; if (!m) m = new GCond(); return m; }
IMP(pthread_mutex_init) { mtx(A(0)); RET(0); }
IMP(pthread_mutex_destroy) { RET(0); }
IMP(pthread_mutex_lock) { mtx(A(0))->m.lock(); RET(0); }
IMP(pthread_mutex_unlock) { mtx(A(0))->m.unlock(); RET(0); }
IMP(pthread_cond_wait) { GMutex *m = mtx(A(1)); std::unique_lock<std::recursive_mutex> lk(m->m, std::adopt_lock); cnd(A(0))->cv.wait(lk); lk.release(); RET(0); }
IMP(pthread_cond_broadcast) { cnd(A(0))->cv.notify_all(); RET(0); }
IMP(pthread_cond_signal) { cnd(A(0))->cv.notify_one(); RET(0); }
static std::atomic<uint32_t> next_tid{2};
namespace { struct ThreadStart { uint32_t fn, arg; }; }
static void guest_thread(void *p) {
    ThreadStart t = *(ThreadStart *)p; delete (ThreadStart *)p;
    cpu(); call(t.fn, {t.arg});
}
IMP(pthread_create) {
    uint32_t tid = next_tid++;
    if (A(0)) W32(A(0), tid);
    host::thread_start(guest_thread, new ThreadStart{A(2), A(3)}, 64u << 20);
    RET(0);
}
IMP(pthread_self) { RET(1); }
static std::mutex key_mu;
static uint32_t next_key = 1;
static thread_local std::unordered_map<uint32_t, uint32_t> *tls;
IMP(pthread_key_create) { std::lock_guard<std::mutex> l(key_mu); W32(A(0), next_key++); RET(0); }
IMP(pthread_key_delete) { RET(0); }
IMP(pthread_getspecific) { if (!tls) tls = new std::unordered_map<uint32_t, uint32_t>(); auto it = tls->find(A(0)); RET(it == tls->end() ? 0 : it->second); }
IMP(pthread_setspecific) { if (!tls) tls = new std::unordered_map<uint32_t, uint32_t>(); (*tls)[A(0)] = A(1); RET(0); }

// ---------------------------------------------------------------- sockets -> embedded server
// bionic sockaddr_in: family(2) port(2, big endian) addr(4)
static bool parse_ipv4(const char *s, uint8_t out[4]) {
    unsigned v[4]; char tail;
    if (sscanf(s, "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &tail) != 4) return false;
    for (int k = 0; k < 4; k++) { if (v[k] > 255) return false; out[k] = (uint8_t)v[k]; }
    return true;
}
static bool local_port(int port) { return port == 12380 || port == 10005 || port == 10015 || port == 10030 || port == 10080; }
IMP(socket) {
    if (A(0) != 2) { gerr(G_EAFNOSUPPORT); RET(-1); return; }                 // AF_INET only
    RET(fd_new(Fd{nullptr, new VSock(), false}));
}
IMP(connect) {
    Fd d; if (!fd_get((int)A(0), d) || !d.s) { gerr(G_ENOTSOCK); RET(-1); return; }
    uint32_t sa = A(1); int port = (int)(R8(sa + 2) << 8 | R8(sa + 3));
    bool loop = R8(sa + 4) == 127;
    if (!loop || !local_port(port)) { logf("offline: refused connection to %u.%u.%u.%u:%d", R8(sa + 4), R8(sa + 5), R8(sa + 6), R8(sa + 7), port);
                                      gerr(G_ECONNREFUSED); RET(-1); return; }
    d.s->port = port; d.s->connected = true;
    if (netlog()) logf("connect fd=%d port=%d", (int)A(0), port);
    RET(0);
}
IMP(bind) { RET(0); }
IMP(send) {
    Fd d; if (!fd_get((int)A(0), d) || !d.s) { gerr(G_ENOTSOCK); RET(-1); return; }
    VSock *s = d.s;
    s->out.append((const char *)GPTR(A(1)), A(2));
    std::string resp;
    if (!s->answered && server_http(s->port, s->out, resp)) {
        if (getenv("PW2_NETLOG")) logf("net :%d %s -> %zu bytes", s->port, s->out.substr(0, s->out.find('\r')).c_str(), resp.size());
        s->in = resp; s->in_pos = 0; s->answered = true; s->out.clear();
    }
    RET(A(2));
}
static bool netlog() { static int v = getenv("PW2_NETLOG") ? atoi(getenv("PW2_NETLOG")) : 0; return v > 1; }
IMP(recv) {
    Fd d; if (!fd_get((int)A(0), d) || !d.s) { gerr(G_ENOTSOCK); RET(-1); return; }
    VSock *s = d.s;
    if (netlog()) logf("recv fd=%d len=%u answered=%d avail=%zu", (int)A(0), A(2), s->answered, s->in.size() - s->in_pos);
    if (!s->answered) { gerr(G_EAGAIN); RET(-1); return; }
    size_t n = std::min<size_t>(A(2), s->in.size() - s->in_pos);
    memcpy(GPTR(A(1)), s->in.data() + s->in_pos, n);
    if (!(A(3) & 2)) s->in_pos += n;                                     // MSG_PEEK
    RET(n);                                                              // 0 = server closed (Connection: close)
}
IMP(poll) {   // struct pollfd { int fd; short events; short revents; }  POLLIN 1, POLLOUT 4, POLLNVAL 0x20
    uint32_t p = A(0), n = A(1); int timeout = (int)A(2), ready = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t e = p + 8 * k; int fd = (int)R32(e); uint16_t ev = (uint16_t)R16(e + 4), rev = 0;
        Fd d;
        if (fd < 0) { W16(e + 6, 0); continue; }
        if (!fd_get(fd, d)) rev = 0x20;
        else if (d.s) { if (ev & 4) rev |= 4; if ((ev & 1) && d.s->answered) rev |= 1; }
        else rev = ev & 5;
        W16(e + 6, rev); if (rev) ready++;
    }
    if (netlog()) logf("poll n=%u timeout=%d ready=%d", n, timeout, ready);
    if (!ready && timeout != 0) host::sleep_ms(timeout < 0 || timeout > 20 ? 20 : timeout);
    RET(ready);
}
IMP(fcntl) {   // F_GETFL 3, F_SETFL 4
    Fd d; int fd = (int)A(0); if (!fd_get(fd, d)) { gerr(G_EBADF); RET(-1); return; }
    if (A(1) == 3) { RET(d.s ? d.s->flags : 2); return; }
    if (A(1) == 4 && d.s) d.s->flags = A(2);
    RET(0);
}
IMP(ioctl) { if (A(1) == 0x541B && A(2)) { Fd d; uint32_t n = 0; if (fd_get((int)A(0), d) && d.s) n = (uint32_t)(d.s->in.size() - d.s->in_pos); W32(A(2), n); } RET(0); }
IMP(setsockopt) { RET(0); }
IMP(getsockopt) { if (A(3)) W32(A(3), 0); if (A(4)) W32(A(4), 4); RET(0); }      // SO_ERROR = 0
static void fill_sockaddr(uint32_t sa, uint32_t lenp, int port) {
    if (!sa) return;
    uint8_t b[16] = {2, 0, (uint8_t)(port >> 8), (uint8_t)port, 127, 0, 0, 1};
    uint32_t l = lenp ? R32(lenp) : 16; memcpy(GPTR(sa), b, std::min<uint32_t>(l, 16)); if (lenp) W32(lenp, 16);
}
IMP(getpeername) { Fd d; fill_sockaddr(A(1), A(2), fd_get((int)A(0), d) && d.s ? d.s->port : 0); RET(0); }
IMP(getsockname) { fill_sockaddr(A(1), A(2), 40000); RET(0); }
IMP(inet_pton) { uint8_t b[4]; if (A(0) != 2 || !parse_ipv4(GSTR(A(1)), b)) { RET(0); return; } memcpy(GPTR(A(2)), b, 4); RET(1); }
IMP(inet_ntop) {
    if (A(0) != 2) { RET(0); return; }
    char b[32]; snprintf(b, sizeof b, "%u.%u.%u.%u", R8(A(1)), R8(A(1) + 1), R8(A(1) + 2), R8(A(1) + 3));
    put_string(A(2), A(3), b); RET(A(2));
}
IMP(getaddrinfo) {   // bionic addrinfo: flags family socktype protocol addrlen canonname addr next (32 bytes)
    const char *node = A(0) ? GSTR(A(0)) : "127.0.0.1"; uint8_t ip[4];
    if (!strcmp(node, "localhost")) node = "127.0.0.1";
    if (!parse_ipv4(node, ip)) { logf("offline: no DNS for %s", node); RET(8); return; }  // EAI_NONAME
    int port = A(1) ? atoi(GSTR(A(1))) : 0;
    uint32_t g = gcalloc(1, 48);
    W32(g + 4, 2); W32(g + 8, A(2) ? R32(A(2) + 8) : 1); W32(g + 12, A(2) ? R32(A(2) + 12) : 6); W32(g + 16, 16); W32(g + 24, g + 32);
    uint8_t sa[16] = {2, 0, (uint8_t)(port >> 8), (uint8_t)port, ip[0], ip[1], ip[2], ip[3]};
    memcpy(GPTR(g + 32), sa, 16);
    W32(A(3), g); RET(0);
}
IMP(freeaddrinfo) { for (uint32_t g = A(0); g;) { uint32_t n = R32(g + 28); gfree(g); g = n; } }
IMP(gethostbyname) { RET(0); }

// ---------------------------------------------------------------- zlib (32-bit z_stream <-> host z_stream)
static std::mutex z_mu;
static std::unordered_map<uint32_t, z_stream *> zs;
static z_stream *zin(uint32_t g) {
    std::lock_guard<std::mutex> l(z_mu); z_stream *&s = zs[g]; if (!s) s = (z_stream *)calloc(1, sizeof(z_stream));
    s->next_in = (Bytef *)GPTR(R32(g)); s->avail_in = R32(g + 4); s->next_out = (Bytef *)GPTR(R32(g + 12)); s->avail_out = R32(g + 16);
    return s;
}
static void zout(uint32_t g, z_stream *s) {
    W32(g, G32(s->next_in)); W32(g + 4, s->avail_in); W32(g + 8, (uint32_t)s->total_in);
    W32(g + 12, G32(s->next_out)); W32(g + 16, s->avail_out); W32(g + 20, (uint32_t)s->total_out);
    W32(g + 24, 0); W32(g + 44, s->data_type); W32(g + 48, (uint32_t)s->adler);
}
IMP(inflateInit_) { z_stream *s = zin(A(0)); int r = inflateInit(s); zout(A(0), s); RET(r); }
IMP(inflateInit2_) { z_stream *s = zin(A(0)); int r = inflateInit2(s, (int)A(1)); zout(A(0), s); RET(r); }
IMP(inflate) { z_stream *s = zin(A(0)); int r = inflate(s, (int)A(1)); zout(A(0), s); RET(r); }
IMP(inflateReset) { z_stream *s = zin(A(0)); int r = inflateReset(s); zout(A(0), s); RET(r); }
IMP(inflateEnd) {
    z_stream *s = zin(A(0)); int r = inflateEnd(s); zout(A(0), s);
    std::lock_guard<std::mutex> l(z_mu); zs.erase(A(0)); free(s); RET(r);
}
IMP(crc32) { RET(crc32(A(0), (const Bytef *)GPTR(A(1)), A(2))); }
IMP(zlibVersion) { static uint32_t v; if (!v) v = gstrdup(zlibVersion()); RET(v); }

// ---------------------------------------------------------------- dl*
IMP(dlopen) { RET(0x0D0D0001u); }
IMP(dlclose) { RET(0); }
IMP(dlsym) { uint32_t t = import_trap(GSTR(A(1))); if (!t) t = sym_addr(GSTR(A(1))); if (!t) logf("dlsym(%s) not found", GSTR(A(1))); RET(t); }
IMP(dlerror) { RET(0); }
