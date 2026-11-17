#!/usr/bin/env python3
"""Function-level differential test: pure leaf-ish guest functions (libgcc soft-float / int helpers)
run under Unicorn and as recompiled code (linked with the real runtime), compared on random inputs.

usage: funtest.py <libGame.so> <workdir> [names...]
"""
import os, sys, struct, random, subprocess, math
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UcError
from unicorn.arm_const import *

LIB, WORK = sys.argv[1], sys.argv[2]
NAMES = sys.argv[3:] or """__aeabi_dadd __aeabi_dsub __aeabi_dmul __aeabi_ddiv __aeabi_drsub __aeabi_dcmplt __aeabi_dcmple
__aeabi_dcmpgt __aeabi_dcmpge __aeabi_dcmpeq __aeabi_dcmpun __aeabi_d2iz __aeabi_d2uiz __aeabi_d2f __aeabi_f2d __aeabi_i2d
__aeabi_ui2d __aeabi_l2d __aeabi_ul2d __aeabi_d2lz __aeabi_d2ulz __fixdfdi __fixunsdfdi __aeabi_fadd __aeabi_fsub __aeabi_fmul
__aeabi_fdiv __aeabi_fcmplt __aeabi_fcmpeq __aeabi_f2iz __aeabi_i2f __aeabi_lmul __aeabi_llsl __aeabi_llsr __aeabi_lasr
__aeabi_uldivmod __aeabi_ldivmod __udivdi3 __divdi3 __umoddi3 __moddi3 __aeabi_uidiv __aeabi_idiv __aeabi_uidivmod
__aeabi_idivmod __clzsi2 __popcountsi2""".split()
os.makedirs(WORK, exist_ok=True)
elf = ELFFile(open(LIB, "rb")); data = open(LIB, "rb").read()
syms = {s.name: s["st_value"] for s in elf.get_section_by_name(".dynsym").iter_symbols() if s["st_value"]}
BASE = 0x10000000; STK = 0x38000000; MAGIC = 0x3f000000
rng = random.Random(7)
def rdouble():
    return rng.choice([0.0, -0.0, 1.0, -1.0, 0.5, 1e300, -1e-300, 1e15, 2.0**52, 2.0**63, -2.0**63, 4294967296.0,
                       float("inf"), float("-inf"), 3.0, 1000.0, 123456.789]) if rng.random() < 0.4 else rng.uniform(-1e9, 1e9)
def dbits(d): return struct.unpack("<II", struct.pack("<d", d))
def fbits(f): return struct.unpack("<I", struct.pack("<f", f))[0]

mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(BASE, 0x500000); mu.mem_write(BASE, data[:0x304000])
mu.mem_map(STK, 0x100000); mu.mem_map(MAGIC, 0x1000)
try: mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
except Exception: pass

cases = []
for name in NAMES:
    if name not in syms: continue
    addr = syms[name]
    for _ in range(300):
        kind = rng.random()
        if "f" in name.split("_")[-1][:2] and "d" not in name.split("_")[-1]:
            regs = [fbits(rng.uniform(-1e6, 1e6) if kind < .7 else rng.choice([0.0, 1.0, -2.5, 3e38, float("inf")])),
                    fbits(rng.uniform(-1e6, 1e6)), 0, 0]
        elif any(k in name for k in ("i2d", "ui2d", "i2f", "uidiv", "idiv", "clz", "popcount")):
            regs = [rng.getrandbits(32), rng.getrandbits(32) | (1 if rng.random() < .9 else 0), 0, 0]
            if rng.random() < .3: regs[1] = rng.randrange(1, 100)
        elif any(k in name for k in ("l2d", "ul2d", "lmul", "llsl", "llsr", "lasr", "divmod", "divdi", "moddi")):
            regs = [rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32)]
            if "ll" in name or "lasr" in name: regs[2] = rng.randrange(0, 64)
            if rng.random() < .5: regs[3] = 0; regs[2] = rng.randrange(1, 1 << 20)
        else:
            a, b = rdouble(), rdouble()
            regs = list(dbits(a)) + list(dbits(b))
        mu.reg_write(UC_ARM_REG_SP, STK + 0x80000); mu.reg_write(UC_ARM_REG_LR, MAGIC)
        for k in range(4): mu.reg_write(UC_ARM_REG_R0 + k, regs[k])
        for k in range(4, 13): mu.reg_write(UC_ARM_REG_R0 + k, 0)
        mu.reg_write(UC_ARM_REG_CPSR, mu.reg_read(UC_ARM_REG_CPSR) & 0x0fffffdf)
        try:
            mu.emu_start(BASE + addr, MAGIC, count=200000)
        except UcError as e:
            continue
        if mu.reg_read(UC_ARM_REG_PC) & ~1 != MAGIC: continue
        out = [mu.reg_read(UC_ARM_REG_R0 + k) for k in range(4)]
        cp = mu.reg_read(UC_ARM_REG_CPSR) >> 28
        cases.append((addr, name, regs, out, cp))
print(len(cases), "reference cases")

vec = open(os.path.join(WORK, "fvec.bin"), "wb")
names_idx = {}
for addr, name, regs, out, cp in cases:
    vec.write(struct.pack("<I4I4II", BASE + addr, *regs, *out, cp))
vec.close()

harness = r'''
#include "rt.h"
#include <cstdio>
namespace rt { std::string host_path(const char *p) { return p ? p : ""; } }
extern "C" bool jni_dispatch(CPU *, uint32_t) { return false; }
int main(int argc, char **argv) {
    rt::mem_init(); rt::load_library(argv[1]);
    FILE *f = fopen(argv[2], "rb"); uint32_t v[10]; int bad = 0, n = 0;
    while (fread(v, 4, 10, f) == 10) {
        CPU *c = rt::cpu(); uint32_t sp = c->r[13];
        for (int k = 0; k < 4; k++) c->r[k] = v[1 + k];
        for (int k = 4; k < 13; k++) c->r[k] = 0;
        if (getenv("FT_TRACE")) fprintf(stderr, "case %s\n", rt::sym_name(v[0]));
        c->r[14] = 0; call_addr(c, v[0]); c->r[13] = sp; n++;
        uint32_t nz = (c->n << 3) | (c->z << 2) | (c->cf << 1) | c->v;
        if (c->r[0] != v[5] || c->r[1] != v[6]) {
            bad++;
            printf("FAIL %s in %08x %08x %08x %08x got %08x %08x want %08x %08x (nzcv %x/%x)\n", rt::sym_name(v[0]), v[1], v[2], v[3], v[4],
                   c->r[0], c->r[1], v[5], v[6], nz, v[9]);
        }
    }
    printf("%d/%d passed\n", n - bad, n);
}
'''
open(os.path.join(WORK, "funtest.cpp"), "w").write(harness)
B = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build")
R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "runtime")
objs = [os.path.join(B, f) for f in os.listdir(B) if (f.startswith("f") and f.endswith(".o")) and f != "funtest.o"]
objs += [os.path.join(B, x) for x in ("table.o", "imports_gen.o", "dlmalloc.o", "rt_rt_core.o", "rt_rt_libc.o", "rt_rt_gl.o")]
r = subprocess.run(["clang++", "-O1", "-std=c++17", "-I", R, os.path.join(WORK, "funtest.cpp"), *objs, "-o",
                    os.path.join(WORK, "funtest"), "-lGL", "-lz", "-lpthread"], capture_output=True, text=True)
if r.returncode: print(r.stderr[-3000:]); sys.exit(1)
r = subprocess.run([os.path.join(WORK, "funtest"), LIB, os.path.join(WORK, "fvec.bin")], capture_output=True, text=True)
lines = r.stdout.splitlines()
seen = set()
for l in lines:
    if l.startswith("FAIL"):
        k = l.split()[1].split("+")[0]
        if k in seen: continue
        seen.add(k); print(l)
print(lines[-1] if lines else r.stderr[-2000:])
