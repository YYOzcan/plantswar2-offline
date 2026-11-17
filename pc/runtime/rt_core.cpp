// Core runtime: guest memory, heap, loader, dispatch, threads, guest calls.
#include "rt.h"
#include "host.h"
#include "elf32.h"
#include <cstdarg>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <map>
#include <algorithm>

extern "C" {
typedef void *mspace;
mspace create_mspace_with_base(void *base, size_t capacity, int locked);
void *mspace_malloc(mspace msp, size_t bytes);
void mspace_free(mspace msp, void *mem);
void *mspace_realloc(mspace msp, void *mem, size_t newsize);
void *mspace_calloc(mspace msp, size_t n_elements, size_t elem_size);
void *mspace_memalign(mspace msp, size_t alignment, size_t bytes);

extern const GuestFn guest_fns[];
extern const unsigned guest_fn_count;
extern const GuestSym guest_syms[];
extern const unsigned guest_sym_count;

extern const char *const import_names[];        // generated: every import the library needs
extern const unsigned import_count;
}
uint8_t *g_mem;

namespace rt {
static thread_local CPU *t_cpu;
std::string data_dir, apk_path;

static const uint32_t TRAP_BASE = 0x0F000000u, TRAP_END = 0x0F100000u;
static const uint32_t HEAP_LO = 0x20000000u, HEAP_HI = 0xD0000000u;
static mspace heap;

void fatal(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[pw2] FATAL: "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n");
    va_end(ap); fflush(stderr);
    char msg[1024]; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    host::show_error("Plants War 2", msg);
    abort();
}
void logf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "[pw2] "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n");
    va_end(ap);
}

#ifdef _WIN32
static const uint32_t HEAP_HI_ = 0x80000000u;   // committed up front on Windows: keep the commit charge sane
#else
static const uint32_t HEAP_HI_ = HEAP_HI;
#endif
static void map_fixed(uint32_t addr, size_t len) {
    if (!host::commit(GPTR(addr), len)) fatal("cannot commit guest memory at 0x%08x (%zu bytes)", addr, len);
}

static void on_crash(const char *what, void *addr) {
    CPU *c = t_cpu;
    long long ga = (uint8_t *)addr >= g_mem && (uint8_t *)addr < g_mem + 0x100000000ull ? (long long)((uint8_t *)addr - g_mem) : -1;
    fprintf(stderr, "[pw2] CRASH %s at host addr %p (guest 0x%llx)\n", what, addr, ga);
    if (c) {
        fprintf(stderr, "[pw2] guest pc=0x%x (%s) lr=%s\n", c->r[15], sym_name(BASE + c->r[15]), sym_name(c->r[14]));
        for (int k = 0; k < 16; k++) fprintf(stderr, "  r%-2d=%08x%s", k, c->r[k], k % 4 == 3 ? "\n" : "");
        if (const char *dump = getenv("PW2_DUMP")) {        // "reg+off[:n],*reg+off+off2[:n]" (hex), e.g. "r13:4,*r4+7c+4:3"
            std::string spec = dump; size_t p = 0;
            while (p < spec.size()) {
                size_t q = spec.find(',', p); if (q == std::string::npos) q = spec.size();
                std::string it = spec.substr(p, q - p); p = q + 1;
                unsigned n = 4; size_t colon = it.find(':'); if (colon != std::string::npos) { n = (unsigned)atoi(it.c_str() + colon + 1); it = it.substr(0, colon); }
                bool deref = !it.empty() && it[0] == '*'; if (deref) it = it.substr(1);
                unsigned reg = 0, o1 = 0, o2 = 0; sscanf(it.c_str(), "r%u+%x+%x", &reg, &o1, &o2);
                uint32_t a = c->r[reg & 15] + o1; if (deref) a = R32(a) + o2;
                fprintf(stderr, "[pw2] %s =", it.c_str());
                for (unsigned k = 0; k < n && a; k++) { uint32_t v = R32(a + 4 * k); float f; memcpy(&f, &v, 4); fprintf(stderr, " %08x(%g)", v, f); }
                fprintf(stderr, "\n");
            }
        }
    }
    fflush(stderr);
}

void mem_init() {
    if (g_mem) return;
    g_mem = (uint8_t *)host::reserve(0x100000000ull + 0x10000);
    if (!g_mem) fatal("cannot reserve 4 GB of address space for the game");
    host::install_crash_handler(on_crash);
    map_fixed(HEAP_LO, HEAP_HI_ - HEAP_LO);
    heap = create_mspace_with_base(GPTR(HEAP_LO), HEAP_HI_ - HEAP_LO, 1);
    if (!heap) fatal("heap");
}
uint32_t gmalloc(size_t n) { return G32(mspace_malloc(heap, n ? n : 1)); }
uint32_t gcalloc(size_t n, size_t m) { return G32(mspace_calloc(heap, n, m)); }
uint32_t grealloc(uint32_t p, size_t n) { return G32(mspace_realloc(heap, p ? GPTR(p) : nullptr, n)); }
void gfree(uint32_t p) { if (p) mspace_free(heap, GPTR(p)); }
uint32_t gstrdup(const char *s) { size_t n = strlen(s) + 1; uint32_t p = gmalloc(n); memcpy(GPTR(p), s, n); return p; }
uint32_t gstatic(size_t n) { uint32_t p = gmalloc(n); memset(GPTR(p), 0, n); return p; }

// ---------------------------------------------------------------- threads
thread_local int *guest_errno;
CPU *new_cpu(size_t stack_bytes) {
    CPU *c = (CPU *)calloc(1, sizeof(CPU));
    uint32_t stk = gmalloc(stack_bytes);
    c->r[13] = (stk + (uint32_t)stack_bytes - 64) & ~15u;
    return c;
}
CPU *cpu() {
    if (!t_cpu) {
        t_cpu = new_cpu(8u << 20);
        guest_errno = (int *)GPTR(gstatic(4));
    }
    return t_cpu;
}

// ---------------------------------------------------------------- dispatch
static GuestFnPtr find_fn(uint32_t vaddr) {
    unsigned lo = 0, hi = guest_fn_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (guest_fns[mid].vaddr < vaddr) lo = mid + 1; else hi = mid;
    }
    if (lo < guest_fn_count && guest_fns[lo].vaddr == vaddr) return guest_fns[lo].fn;
    return nullptr;
}

const char *sym_name(uint32_t addr) {
    uint32_t v = (addr - BASE) & ~1u; const char *best = "?"; uint32_t bv = 0;
    for (unsigned k = 0; k < guest_sym_count; k++)
        if (guest_syms[k].vaddr <= v && guest_syms[k].vaddr >= bv) { bv = guest_syms[k].vaddr; best = guest_syms[k].name; }
    static thread_local char buf[512];
    snprintf(buf, sizeof buf, "%s+0x%x", best, v - bv);
    return buf;
}

static std::map<std::string, GuestFnPtr> &imp_registry() { static std::map<std::string, GuestFnPtr> m; return m; }
ImpReg::ImpReg(const char *name, GuestFnPtr fn) { imp_registry()[name] = fn; }
static std::vector<GuestFnPtr> imp_fns;
static void resolve_imports() {
    if (!imp_fns.empty()) return;
    imp_fns.resize(import_count);
    for (unsigned k = 0; k < import_count; k++) {
        auto it = imp_registry().find(import_names[k]);
        imp_fns[k] = it == imp_registry().end() ? nullptr : it->second;
    }
}
uint32_t import_trap(const char *name) {
    resolve_imports();
    for (unsigned k = 0; k < import_count; k++)
        if (!strcmp(import_names[k], name)) return TRAP_BASE + 4 * k;
    return 0;
}

}  // namespace rt
using namespace rt;

extern "C" bool jni_dispatch(CPU *c, uint32_t addr);
extern "C" uint32_t rt_sF();
extern "C" void missing_import(CPU *c, const char *name);
extern "C" void call_addr(CPU *c, uint32_t addr) {
    if (addr >= TRAP_BASE && addr < BASE && jni_dispatch(c, addr)) return;
    if (addr >= TRAP_BASE && addr < TRAP_END) {
        unsigned k = (addr - TRAP_BASE) / 4;
        if (k >= import_count) fatal("bad trap 0x%08x", addr);
        if (!imp_fns[k]) missing_import(c, import_names[k]);
        imp_fns[k](c); return;
    }
    GuestFnPtr f = find_fn((addr - BASE) & ~1u);
    if (!f) fatal("call to unknown guest address 0x%08x (%s), lr=0x%08x (%s)", addr, sym_name(addr), c->r[14],
                  sym_name(c->r[14]));
    f(c);
}

extern "C" void unimpl(CPU *c, uint32_t vaddr, const char *what) {
    fatal("untranslated code reached at 0x%x (%s): %s  lr=%s", vaddr, sym_name(BASE + vaddr), what, sym_name(c->r[14]));
}

extern "C" void missing_import(CPU *c, const char *name) {
    fatal("import not implemented: %s (called from %s)", name, sym_name(c->r[14]));
}

namespace rt {
uint32_t arg(CPU *c, int i) { return i < 4 ? c->r[i] : R32(c->r[13] + 4 * (i - 4)); }
uint64_t arg64(CPU *c, int &i) {
    if (i < 4 && (i & 1)) i++;
    uint32_t lo, hi;
    if (i + 1 < 4) { lo = c->r[i]; hi = c->r[i + 1]; i += 2; }
    else {
        if (i < 4) i = 4;                                     // 64-bit values are never split
        int so = (i - 4) * 4; if (so & 7) { so += 4; i++; }   // 8-byte aligned on the stack
        lo = R32(c->r[13] + so); hi = R32(c->r[13] + so + 4); i += 2;
    }
    return ((uint64_t)hi << 32) | lo;
}
void ret64(CPU *c, uint64_t v) { c->r[0] = (uint32_t)v; c->r[1] = (uint32_t)(v >> 32); }

uint32_t call(uint32_t addr, std::initializer_list<uint32_t> args) {
    CPU *c = cpu();
    uint32_t saved_sp = c->r[13], saved_lr = c->r[14];
    std::vector<uint32_t> a(args);
    if (a.size() > 4) {
        uint32_t n = (uint32_t)(a.size() - 4);
        c->r[13] = (c->r[13] - 4 * n) & ~7u;
        for (uint32_t k = 0; k < n; k++) W32(c->r[13] + 4 * k, a[4 + k]);
    }
    for (size_t k = 0; k < a.size() && k < 4; k++) c->r[k] = a[k];
    c->r[14] = 0;
    call_addr(c, addr);
    uint32_t r = c->r[0];
    c->r[13] = saved_sp; c->r[14] = saved_lr;
    return r;
}

// ---------------------------------------------------------------- loader
static std::vector<uint8_t> elf_data;
static std::map<std::string, uint32_t> exports;

void load_library(const char *path) {
    FILE *f = host::fopen(path, "rb"); if (!f) fatal("cannot open %s", path);
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    elf_data.resize(n); if (fread(elf_data.data(), 1, n, f) != (size_t)n) fatal("read %s", path); fclose(f);
    auto *eh = (Elf32_Ehdr *)elf_data.data();
    auto *ph = (Elf32_Phdr *)(elf_data.data() + eh->e_phoff);
    uint32_t hi = 0;
    for (int k = 0; k < eh->e_phnum; k++)
        if (ph[k].p_type == PT_LOAD) hi = std::max(hi, ph[k].p_vaddr + ph[k].p_memsz);
    resolve_imports();
    map_fixed(BASE, (hi + 0xffff) & ~0xffffu);
    Elf32_Dyn *dyn = nullptr;
    for (int k = 0; k < eh->e_phnum; k++) {
        if (ph[k].p_type == PT_LOAD) memcpy(GPTR(BASE + ph[k].p_vaddr), elf_data.data() + ph[k].p_offset, ph[k].p_filesz);
        if (ph[k].p_type == PT_DYNAMIC) dyn = (Elf32_Dyn *)GPTR(BASE + ph[k].p_vaddr);
    }
    uint32_t rel = 0, relsz = 0, jmprel = 0, pltrelsz = 0, symtab = 0, strtab = 0, hash = 0;
    for (Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_REL: rel = d->d_un.d_ptr; break;
        case DT_RELSZ: relsz = d->d_un.d_val; break;
        case DT_JMPREL: jmprel = d->d_un.d_ptr; break;
        case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
        case DT_SYMTAB: symtab = d->d_un.d_ptr; break;
        case DT_STRTAB: strtab = d->d_un.d_ptr; break;
        case DT_HASH: hash = d->d_un.d_ptr; break;
        }
    }
    auto *syms = (Elf32_Sym *)GPTR(BASE + symtab);
    const char *strs = GSTR(BASE + strtab);
    uint32_t nsyms = R32(BASE + hash + 4);
    for (uint32_t k = 1; k < nsyms; k++)
        if (syms[k].st_shndx != SHN_UNDEF && syms[k].st_value) exports[strs + syms[k].st_name] = BASE + syms[k].st_value;

    auto resolve = [&](uint32_t symidx) -> uint32_t {
        Elf32_Sym &s = syms[symidx];
        const char *name = strs + s.st_name;
        if (s.st_shndx != SHN_UNDEF) return BASE + s.st_value;
        if (!strcmp(name, "__sF")) return rt_sF();
        if (!strcmp(name, "__stack_chk_guard")) { static uint32_t g; if (!g) { g = gstatic(4); W32(g, 0x5057c0deu); } return g; }
        uint32_t t = import_trap(name);
        if (!t) logf("unresolved import %s", name);
        return t;
    };
    auto apply = [&](uint32_t start, uint32_t size) {
        for (uint32_t o = 0; o < size; o += 8) {
            auto *r = (Elf32_Rel *)GPTR(BASE + start + o);
            uint32_t P = BASE + r->r_offset, type = ELF32_R_TYPE(r->r_info), si = ELF32_R_SYM(r->r_info);
            switch (type) {
            case R_ARM_RELATIVE: W32(P, R32(P) + BASE); break;
            case R_ARM_ABS32: W32(P, R32(P) + resolve(si)); break;
            case R_ARM_GLOB_DAT: case R_ARM_JUMP_SLOT: W32(P, resolve(si)); break;
            default: fatal("relocation type %u", type);
            }
        }
    };
    apply(rel, relsz);
    apply(jmprel, pltrelsz);
    logf("loaded %s at 0x%08x (%zu exports)", path, BASE, exports.size());
}

uint32_t sym_addr(const char *name) {
    auto it = exports.find(name);
    return it == exports.end() ? 0 : it->second;
}

void run_initializers() {
    auto *eh = (Elf32_Ehdr *)elf_data.data();
    auto *ph = (Elf32_Phdr *)(elf_data.data() + eh->e_phoff);
    for (int k = 0; k < eh->e_phnum; k++) {
        if (ph[k].p_type != PT_DYNAMIC) continue;
        uint32_t ia = 0, iasz = 0;
        for (auto *d = (Elf32_Dyn *)GPTR(BASE + ph[k].p_vaddr); d->d_tag != DT_NULL; d++) {
            if (d->d_tag == DT_INIT_ARRAY) ia = d->d_un.d_ptr;
            if (d->d_tag == DT_INIT_ARRAYSZ) iasz = d->d_un.d_val;
        }
        for (uint32_t o = 0; o < iasz; o += 4) {
            uint32_t fn = R32(BASE + ia + o);
            if (fn && fn != 0xffffffffu) call(fn, {});
        }
        logf("ran %u initializers", iasz / 4);
    }
}
}  // namespace rt

// ---------------------------------------------------------------- setjmp / longjmp
struct JmpSlot { pw2_jmp_buf jb; uint32_t r[16]; double d[16]; uint32_t val; };
static std::mutex jmp_mu;
static std::unordered_map<uint64_t, JmpSlot *> jmp_slots;   // (thread cpu, guest buffer) -> slot

static JmpSlot *slot_for(CPU *c, uint32_t gb) {
    std::lock_guard<std::mutex> g(jmp_mu);
    uint64_t key = ((uint64_t)(uintptr_t)c << 32) ^ gb;
    auto &s = jmp_slots[key];
    if (!s) s = new JmpSlot();
    return s;
}
extern "C" pw2_jmp_buf *guest_jmpbuf_for(CPU *c, uint32_t gb) { return &slot_for(c, gb)->jb; }
extern "C" uint32_t guest_jmp_value(CPU *c, uint32_t gb) { return slot_for(c, gb)->val; }
extern "C" void guest_jmp_saved(CPU *c, uint32_t gb) {
    JmpSlot *s = slot_for(c, gb);
    memcpy(s->r, c->r, sizeof s->r);
    memcpy(s->d, c->d, sizeof s->d);
    W32(gb, 0x50573231u);                                   // mark the guest buffer as used
}
static void do_longjmp(CPU *c, uint32_t gb, uint32_t val) {
    JmpSlot *s = slot_for(c, gb);
    memcpy(c->r, s->r, sizeof s->r);
    memcpy(c->d, s->d, sizeof s->d);
    s->val = val ? val : 1;
    PW2_LONGJMP(s->jb);
}
IMP(longjmp) { do_longjmp(c, c->r[0], c->r[1]); }
IMP(siglongjmp) { do_longjmp(c, c->r[0], c->r[1]); }
IMP(_longjmp) { do_longjmp(c, c->r[0], c->r[1]); }
// setjmp reached through a function pointer (not a direct call): cannot return twice safely
IMP(setjmp) { guest_jmp_saved(c, c->r[0]); c->r[0] = 0; }
IMP(sigsetjmp) { guest_jmp_saved(c, c->r[0]); c->r[0] = 0; }

// ---------------------------------------------------------------- native libgcc helpers
extern "C" {
void nat___aeabi_idiv(CPU *c) { int32_t a = c->r[0], b = c->r[1]; c->r[0] = b ? (uint32_t)(a / b) : 0; }
void nat___divsi3(CPU *c) { nat___aeabi_idiv(c); }
void nat___aeabi_uidiv(CPU *c) { uint32_t a = c->r[0], b = c->r[1]; c->r[0] = b ? a / b : 0; }
void nat___udivsi3(CPU *c) { nat___aeabi_uidiv(c); }
void nat___aeabi_idivmod(CPU *c) {
    int32_t a = c->r[0], b = c->r[1];
    if (!b) { c->r[0] = 0; c->r[1] = 0; return; }
    if (a == INT32_MIN && b == -1) { c->r[0] = a; c->r[1] = 0; return; }
    c->r[0] = (uint32_t)(a / b); c->r[1] = (uint32_t)(a % b);
}
void nat___aeabi_uidivmod(CPU *c) {
    uint32_t a = c->r[0], b = c->r[1];
    if (!b) { c->r[0] = 0; c->r[1] = 0; return; }
    c->r[0] = a / b; c->r[1] = a % b;
}
void nat___aeabi_ldivmod(CPU *c) {
    int64_t a = (int64_t)(((uint64_t)c->r[1] << 32) | c->r[0]), b = (int64_t)(((uint64_t)c->r[3] << 32) | c->r[2]);
    int64_t q = b ? a / b : 0, r = b ? a % b : 0;
    c->r[0] = (uint32_t)q; c->r[1] = (uint32_t)((uint64_t)q >> 32); c->r[2] = (uint32_t)r; c->r[3] = (uint32_t)((uint64_t)r >> 32);
}
void nat___aeabi_uldivmod(CPU *c) {
    uint64_t a = ((uint64_t)c->r[1] << 32) | c->r[0], b = ((uint64_t)c->r[3] << 32) | c->r[2];
    uint64_t q = b ? a / b : 0, r = b ? a % b : 0;
    c->r[0] = (uint32_t)q; c->r[1] = (uint32_t)(q >> 32); c->r[2] = (uint32_t)r; c->r[3] = (uint32_t)(r >> 32);
}
void nat___aeabi_idiv0(CPU *c) { c->r[0] = 0; }
void nat___aeabi_ldiv0(CPU *c) { c->r[0] = 0; c->r[1] = 0; }
}
