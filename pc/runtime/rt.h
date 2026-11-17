// Internal runtime API (C++). The recompiled code only sees guest.h.
#pragma once
#include "guest.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define GPTR(x) ((void *)GP(x))
#define GSTR(x) ((const char *)GP(x))
static inline uint32_t G32(const void *p) { return (uint32_t)((const uint8_t *)p - g_mem); }   // host ptr inside guest memory -> guest addr

namespace rt {
// ---- memory
void mem_init();                           // reserve low guest regions, create heap
uint32_t gmalloc(size_t n);
uint32_t gcalloc(size_t n, size_t m);
uint32_t grealloc(uint32_t p, size_t n);
void gfree(uint32_t p);
uint32_t gstrdup(const char *s);
uint32_t gstatic(size_t n);                // never freed (static buffers handed to the guest)

// ---- threads / cpu
CPU *cpu();                                // CPU of the calling host thread (created on demand)
CPU *new_cpu(size_t stack_bytes);
extern thread_local int *guest_errno;          // guest-visible errno of this thread
void set_errno_from_host();

// ---- calling guest code
uint32_t call(uint32_t addr, std::initializer_list<uint32_t> args);   // r0..r3 + stack
uint32_t arg(CPU *c, int i);               // i-th 32-bit argument of an import call
uint64_t arg64(CPU *c, int &i);            // 64-bit argument starting at index i (aligned), advances i
void ret64(CPU *c, uint64_t v);
static inline double argd(CPU *c, int &i) { uint64_t u = arg64(c, i); double d; memcpy(&d, &u, 8); return d; }
static inline void retd(CPU *c, double d) { uint64_t u; memcpy(&u, &d, 8); ret64(c, u); }
static inline float argf(CPU *c, int i) { uint32_t u = arg(c, i); float f; memcpy(&f, &u, 4); return f; }
static inline void retf(CPU *c, float f) { uint32_t u; memcpy(&u, &f, 4); c->r[0] = u; }

// ---- loader / symbols
void load_library(const char *path);
void run_initializers();
uint32_t sym_addr(const char *name);       // exported guest symbol -> guest address (with thumb bit)
const char *sym_name(uint32_t addr);       // nearest guest symbol (diagnostics)
uint32_t import_trap(const char *name);    // trap address for an imported function, 0 if unknown

// ---- guest varargs (printf family)
struct VA {
    CPU *c; int idx; uint32_t ap; bool mem;  // either argument index (regs+stack) or va_list pointer
    uint32_t next32();
    uint64_t next64();
};
std::string gformat(const char *fmt, VA &va);

// ---- import registry: every IMP(name) registers imp_name under "name"
struct ImpReg { ImpReg(const char *name, GuestFnPtr fn); };
#define IMP(name) extern "C" void imp_##name(CPU *c); static rt::ImpReg reg_imp_##name(#name, imp_##name); \
                  extern "C" void imp_##name(CPU *c)

// ---- paths
std::string host_path(const char *guest_path);   // map Android paths to the PC data directory
extern std::string data_dir, apk_path;

[[noreturn]] void fatal(const char *fmt, ...);
void logf(const char *fmt, ...);
}  // namespace rt
