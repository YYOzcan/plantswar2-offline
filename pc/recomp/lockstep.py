#!/usr/bin/env python3
"""lockstep.py <libGame.so> <function> r0 r1 r2 r3 : compare per-instruction state with Unicorn, show first divergence."""
import os, sys, subprocess, struct
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *
import capstone as cs
LIB, FN = sys.argv[1], sys.argv[2]; ARGS = [int(x, 16) for x in sys.argv[3:7]]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TB = os.path.join(ROOT, "build", "trace"); os.makedirs(TB, exist_ok=True)
gen = os.path.join(ROOT, "gen")
srcs = sorted(f for f in os.listdir(gen) if f.startswith("f") and f.endswith(".c"))
jobs = []
for f in srcs:
    o = os.path.join(TB, f[:-2] + ".o")
    if not os.path.exists(o) or os.path.getmtime(o) < os.path.getmtime(os.path.join(gen, f)):
        jobs.append(subprocess.Popen(["clang", "-O1", "-w", "-DPW2_TRACE", "-fno-strict-aliasing", "-I", os.path.join(ROOT, "runtime"),
                                      "-I", gen, "-c", os.path.join(gen, f), "-o", o]))
for j in jobs: j.wait()
h = os.path.join(TB, "lock.cpp")
open(h, "w").write(r'''
#include "rt.h"
namespace rt { std::string host_path(const char *p) { return p ? p : ""; } }
extern "C" bool jni_dispatch(CPU *, uint32_t) { return false; }
extern "C" void pw2_trace(CPU *c) {
    printf("%x", c->r[15]); for (int k = 0; k < 15; k++) printf(" %x", c->r[k]);
    printf(" %u%u%u%u\n", c->n, c->z, c->cf, c->v);
}
int main(int argc, char **argv) {
    rt::mem_init(); rt::load_library(argv[1]);
    CPU *c = rt::cpu(); for (int k = 0; k < 4; k++) c->r[k] = strtoul(argv[3 + k], 0, 16);
    for (int k = 4; k < 13; k++) c->r[k] = 0; c->r[14] = 0x3f000000;
    call_addr(c, strtoul(argv[2], 0, 16));
    printf("ret %x %x\n", c->r[0], c->r[1]);
}''')
B = os.path.join(ROOT, "build")
objs = [os.path.join(TB, f[:-2] + ".o") for f in srcs] + [os.path.join(B, x) for x in ("table.o", "imports_gen.o", "dlmalloc.o", "rt_rt_core.o", "rt_rt_libc.o", "rt_rt_gl.o")]
subprocess.run(["clang++", "-O1", "-std=c++17", "-I", os.path.join(ROOT, "runtime"), h, *objs, "-o", os.path.join(TB, "lock"), "-lGL", "-lz", "-lpthread"], check=True)
elf = ELFFile(open(LIB, "rb")); data = open(LIB, "rb").read()
syms = {s.name: s["st_value"] for s in elf.get_section_by_name(".dynsym").iter_symbols() if s["st_value"]}
addr = syms[FN] if FN in syms else int(FN, 16)
BASE = 0x10000000
out = subprocess.run([os.path.join(TB, "lock"), LIB, hex(BASE + addr)] + [hex(a) for a in ARGS], capture_output=True, text=True).stdout.splitlines()
ours = [l for l in out if not l.startswith(("[pw2]", "ret"))]
mu = Uc(UC_ARCH_ARM, UC_MODE_ARM); mu.mem_map(BASE, 0x500000); mu.mem_write(BASE, data[:0x304000]); mu.mem_map(0x38000000, 0x100000); mu.mem_map(0x3f000000, 0x1000)
sp0 = int(ours[0].split()[14], 16) if ours else 0x38080000
try: mu.mem_map((sp0 & ~0xfffff) - 0x100000, 0x300000)
except Exception: pass
mu.reg_write(UC_ARM_REG_SP, sp0)
for k in range(4): mu.reg_write(UC_ARM_REG_R0 + k, ARGS[k])
mu.reg_write(UC_ARM_REG_LR, 0x3f000000); mu.reg_write(UC_ARM_REG_CPSR, mu.reg_read(UC_ARM_REG_CPSR) & 0x0fffffdf)
theirs = []
REG = [UC_ARM_REG_R0 + k for k in range(13)] + [UC_ARM_REG_SP, UC_ARM_REG_LR]
def hook(uc, a, size, _):
    cp = uc.reg_read(UC_ARM_REG_CPSR)
    theirs.append("%x %s %d%d%d%d" % (a - BASE, " ".join("%x" % uc.reg_read(r) for r in REG), cp >> 31 & 1, cp >> 30 & 1, cp >> 29 & 1, cp >> 28 & 1))
mu.hook_add(UC_HOOK_CODE, hook)
mu.emu_start(BASE + addr, 0x3f000000, count=100000)
md = {0: cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_ARM), 1: cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB)}
for k, (a_, b_) in enumerate(zip(ours, theirs)):
    if a_ != b_:
        print("first divergence at step", k)
        for j in range(max(0, k - 3), min(len(ours), k + 2)):
            pa = int(theirs[j].split()[0], 16)
            ins = next(md[addr & 1].disasm(data[pa:pa + 4], pa), None)
            print(f"  ours  {ours[j]}\n  unic  {theirs[j]}   <- {ins.mnemonic + ' ' + ins.op_str if ins else '?'}")
        break
else:
    print("no divergence in", min(len(ours), len(theirs)), "steps;", len(ours), len(theirs))
print([l for l in out if l.startswith("ret")], "unicorn ret", hex(mu.reg_read(UC_ARM_REG_R0)), hex(mu.reg_read(UC_ARM_REG_R1)))
