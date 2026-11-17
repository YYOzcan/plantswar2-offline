#!/usr/bin/env python3
"""Differential test of the recompiler's instruction semantics against Unicorn (real ARM emulation).

Samples every distinct instruction form the recompiler emits for libGame, translates each one with the
recompiler's own Gen.one(), runs both on random register/flag/VFP/memory states and compares.

usage: insntest.py <libGame.so> <workdir> [trials]
"""
import os, sys, struct, random, subprocess, re, math
LIB, WORK = sys.argv[1], sys.argv[2]
TRIALS = int(sys.argv[3]) if len(sys.argv) > 3 else 6
os.makedirs(WORK, exist_ok=True)

# ---- reuse the recompiler (everything above its "main" section)
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "recomp.py")).read()
src = src[:src.index("# ----------------------------------------------------------------------------- main")]
sys.argv = [sys.argv[0], LIB, WORK]
G = {"__name__": "recomp_lib"}
exec(compile(src, "recomp.py", "exec"), G)
A = G["A"]; Gen = G["Gen"]; Func = G["Func"]; explore = G["explore"]; all_entries = G["all_entries"]
work = G["work"]; IT_COND = G["IT_COND"]; Unsupported = G["Unsupported"]

fns = {}
while work:
    a = work.popleft()
    try: fns[a] = explore(a, all_entries[a])
    except Exception: pass

SKIP_IDS = {A.ARM_INS_B, A.ARM_INS_BL, A.ARM_INS_BLX, A.ARM_INS_BX, A.ARM_INS_CBZ, A.ARM_INS_CBNZ, A.ARM_INS_TBB,
            A.ARM_INS_TBH, A.ARM_INS_IT, A.ARM_INS_SVC, A.ARM_INS_UDF, A.ARM_INS_BKPT, A.ARM_INS_NOP, A.ARM_INS_PLD,
            A.ARM_INS_DMB, A.ARM_INS_LDREX, A.ARM_INS_STREX, A.ARM_INS_ADR}
def norm(i):
    s = re.sub(r"\b(r\d+|sb|sl|fp|ip|lr|sp|pc)\b", "R", i.op_str)
    s = re.sub(r"\b[sd]\d+\b", "V", s)
    s = re.sub(r"#-?0x[0-9a-f]+|#-?\d+(\.\d+)?(e[+-]?\d+)?", "#", s)
    return (i.mnemonic, s)

samples = {}
for fa, fn in fns.items():
    if fn is None: continue
    for a, i in fn.insns.items():
        if i is None or i.id in SKIP_IDS or a in IT_COND: continue
        if "pc" in i.op_str.split(",")[0] or i.mnemonic.startswith(("stc", "ldc", "mrc", "mcr", "cdp", "vld", "vst", "fld", "fst")): continue
        if i.mnemonic.startswith(("push", "pop", "ldm", "stm", "vpush", "vpop")) and "pc" in i.op_str: continue
        if i.mnemonic.startswith("v") and ("q" in i.op_str or re.search(r"\bd(1[6-9]|2\d|3[01])\b", i.op_str)): continue
        k = (fn.thumb,) + norm(i)
        lst = samples.setdefault(k, [])
        if len(lst) < 3: lst.append((i, fn))
print(f"{len(samples)} instruction forms")

# ---- generate the C translations
tests = []; bodies = []
for k, lst in sorted(samples.items(), key=lambda kv: str(kv[0])):
    for i, fn in lst:
        f1 = Func(i.address, fn.thumb, i.address + i.size); f1.insns = {i.address: i}
        g = Gen(f1); g.ins = i
        g.out.append(f"  /* {i.address:x}: {i.mnemonic} {i.op_str} */")
        try:
            opened = g.emit_cond_open()
            g.one(i)
            if opened: g.out.append("  }")
        except Unsupported:
            continue
        body = "\n".join(g.out)
        if "goto" in body or "call_addr" in body or "imp_" in body or "return;" in body: continue
        tests.append((i, fn.thumb)); bodies.append(body)
print(f"{len(tests)} testable instructions")

c = ['#include "guest.h"', '#include <stdio.h>', '#include <stdlib.h>', '#include <sys/mman.h>',
     'void call_addr(CPU *c, uint32_t a) { (void)c; (void)a; }', 'void unimpl(CPU *c, uint32_t v, const char *w) { (void)c; (void)v; (void)w; }',
     'jmp_buf *guest_jmpbuf_for(CPU *c, uint32_t g) { static jmp_buf b; (void)c; (void)g; return &b; }',
     'void guest_jmp_saved(CPU *c, uint32_t g) { (void)c; (void)g; }']
for n, b in enumerate(bodies):
    c.append(f"static void T{n}(CPU *c) {{ uint32_t t, t2, sc; uint64_t u64; const uint32_t elr = 0; (void)t; (void)t2; (void)sc; (void)u64; (void)elr;\n{b}\n}}")
c.append("typedef void (*TF)(CPU *); static TF tests[] = {" + ",".join(f"T{n}" for n in range(len(bodies))) + "};")
c.append(r'''
#define SB 0x30000000u
#define SBN 0x10000u
static uint32_t rnd_state;
static uint32_t xs(void) { rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5; return rnd_state; }
static void fill(uint32_t seed) { rnd_state = seed | 1; for (uint32_t k = 0; k < SBN; k += 4) *(uint32_t *)(uintptr_t)(SB + k) = xs(); }
int main(int argc, char **argv) {
    if (mmap((void *)(uintptr_t)SB, SBN, 3, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) return 2;
    FILE *lib = fopen(argv[1], "rb"); fseek(lib, 0, SEEK_END); long ln = ftell(lib); fseek(lib, 0, SEEK_SET);
    if (mmap((void *)(uintptr_t)BASE, 0x500000, 3, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) return 3;
    unsigned char *img = malloc(ln); fread(img, 1, ln, lib);
    memcpy((void *)(uintptr_t)BASE, img, ln > 0x304000 ? 0x304000 : ln);   /* text + rodata (same offsets) */
    FILE *v = fopen(argv[2], "rb"); unsigned char *snap = malloc(SBN);
    int bad = 0, total = 0;
    for (;;) {
        uint32_t hdr[2]; if (fread(hdr, 4, 2, v) != 2) break;
        CPU in; memset(&in, 0, sizeof in);
        uint32_t st[22]; fread(st, 4, 22, v); fread(in.d, 8, 16, v);
        memcpy(in.r, st, 64); in.n = st[16]; in.z = st[17]; in.cf = st[18]; in.v = st[19]; in.fpscr = st[20];
        uint32_t ex[22]; double exd[16]; fread(ex, 4, 22, v); fread(exd, 8, 16, v);
        uint32_t nch; fread(&nch, 4, 1, v);
        uint32_t *chg = malloc(8 * (nch + 1)); fread(chg, 8, nch, v);
        fill(hdr[1]); memcpy(snap, (void *)(uintptr_t)SB, SBN);
        CPU c = in;
        tests[hdr[0]](&c);
        int ok = 1; char why[256] = "";
        for (int k = 0; k < 15; k++) if (c.r[k] != ex[k]) { ok = 0; snprintf(why, sizeof why, "r%d=%08x want %08x", k, c.r[k], ex[k]); break; }
        if (ok && (c.n != ex[16] || c.z != ex[17] || c.cf != ex[18] || c.v != ex[19])) { ok = 0; snprintf(why, sizeof why, "nzcv=%u%u%u%u want %u%u%u%u", c.n, c.z, c.cf, c.v, ex[16], ex[17], ex[18], ex[19]); }
        if (ok && ((c.fpscr ^ ex[20]) & 0xf0000000u)) { ok = 0; snprintf(why, sizeof why, "fpscr %08x want %08x", c.fpscr, ex[20]); }
        for (int k = 0; ok && k < 16; k++) { uint64_t a, b; memcpy(&a, &c.d[k], 8); memcpy(&b, &exd[k], 8);
            if (a != b && !(c.d[k] != c.d[k] && exd[k] != exd[k])) { ok = 0; snprintf(why, sizeof why, "d%d=%016llx want %016llx", k, (unsigned long long)a, (unsigned long long)b); } }
        for (uint32_t k = 0; ok && k < nch; k++) { uint32_t a = chg[2 * k], val = chg[2 * k + 1];
            if (*(uint8_t *)(uintptr_t)a != (uint8_t)val) { ok = 0; snprintf(why, sizeof why, "mem %08x=%02x want %02x", a, *(uint8_t *)(uintptr_t)a, val); } }
        for (uint32_t k = 0; ok && k < SBN; k++) {
            if (((uint8_t *)(uintptr_t)SB)[k] != snap[k]) {
                int listed = 0; for (uint32_t q = 0; q < nch; q++) if (chg[2 * q] == SB + k) listed = 1;
                if (!listed) { ok = 0; snprintf(why, sizeof why, "unexpected write at %08x", SB + k); }
            }
        }
        total++;
        if (!ok) { bad++; printf("FAIL %u %s\n", hdr[0], why); }
        free(chg);
    }
    printf("%d/%d passed\n", total - bad, total);
    return 0;
}
''')
open(os.path.join(WORK, "insntest.c"), "w").write("\n".join(c))
rt = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "runtime")
r = subprocess.run(["clang", "-O1", "-w", "-I", rt, os.path.join(WORK, "insntest.c"), "-o", os.path.join(WORK, "insntest"), "-lm"],
                   capture_output=True, text=True)
if r.returncode: print(r.stderr[:3000]); sys.exit(1)

# ---- reference results with Unicorn
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_MODE_THUMB
from unicorn.arm_const import *
data = open(LIB, "rb").read()
SB, SBN = 0x30000000, 0x10000
BASE = 0x10000000
def xs_fill(seed):
    s = seed | 1; out = bytearray(SBN)
    for k in range(0, SBN, 4):
        s ^= (s << 13) & 0xffffffff; s ^= s >> 17; s ^= (s << 5) & 0xffffffff
        struct.pack_into("<I", out, k, s)
    return bytes(out)
REGS = [UC_ARM_REG_R0 + k for k in range(13)] + [UC_ARM_REG_SP, UC_ARM_REG_LR]
mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(BASE, 0x500000); mu.mem_write(BASE, data[:0x304000])
mu.mem_map(SB, SBN)
try: mu.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
except Exception as e: print("fpexc:", e)
EDGE = [0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe, 0x100, 0xff, 31, 32, 33]
FEDGE = [0.0, -0.0, 1.0, -1.5, 3.75, 1e10, -1e-10, 2147483648.0, -2147483649.0, 0.5, 255.9]
rng = random.Random(1234)
fmt_in = struct.Struct("<2I22I16d")
out = open(os.path.join(WORK, "vectors.bin"), "wb")
for tid, (i, is_thumb) in enumerate(tests):
    mem_insn = "[" in i.op_str or i.mnemonic.startswith(("push", "pop", "ldm", "stm", "vpush", "vpop", "vldm", "vstm"))
    for trial in range(TRIALS):
        seed = rng.getrandbits(32) | 1
        regs = []
        for k in range(15):
            if mem_insn: regs.append(SB + 0x8000 + rng.randrange(-0x800, 0x800) * 4)
            elif rng.random() < 0.3: regs.append(rng.choice(EDGE))
            else: regs.append(rng.getrandbits(32) if rng.random() < 0.5 else rng.randrange(0, 64))
        if mem_insn: regs[13] = SB + 0x8000
        nzcv = [rng.getrandbits(1) for _ in range(4)]
        ds = []
        for k in range(16):
            if rng.random() < 0.5:
                a_, b_ = rng.choice(FEDGE), rng.choice(FEDGE)
                ds.append(struct.unpack("<d", struct.pack("<ff", a_, b_))[0])
            else: ds.append(rng.choice(FEDGE) * rng.uniform(-3, 3))
        init = xs_fill(seed)
        mu.mem_write(SB, init)
        for k, r in enumerate(REGS): mu.reg_write(r, regs[k])
        cpsr = (nzcv[0] << 31) | (nzcv[1] << 30) | (nzcv[2] << 29) | (nzcv[3] << 28)
        for k in range(16): mu.reg_write(UC_ARM_REG_D0 + k, struct.unpack("<Q", struct.pack("<d", ds[k]))[0])
        mu.reg_write(UC_ARM_REG_FPSCR, 0)
        mu.reg_write(UC_ARM_REG_CPSR, (mu.reg_read(UC_ARM_REG_CPSR) & 0x0fffffdf) | (cpsr & 0xf0000000) | (0x20 if is_thumb else 0))
        mu.reg_write(UC_ARM_REG_APSR, cpsr & 0xf0000000)
        try:
            if (mu.reg_read(UC_ARM_REG_CPSR) >> 28) != (cpsr >> 28): raise RuntimeError("flags not applied")
            mu.emu_start(BASE + i.address | (1 if is_thumb else 0), BASE + i.address + i.size, count=1)
        except Exception as e:
            continue
        exr = [mu.reg_read(r) for r in REGS] + [0]
        cp = mu.reg_read(UC_ARM_REG_CPSR)
        exflags = [(cp >> 31) & 1, (cp >> 30) & 1, (cp >> 29) & 1, (cp >> 28) & 1]
        fps = mu.reg_read(UC_ARM_REG_FPSCR)
        exd = [struct.unpack("<d", struct.pack("<Q", mu.reg_read(UC_ARM_REG_D0 + k)))[0] for k in range(16)]
        fin = bytes(mu.mem_read(SB, SBN))
        changes = [(SB + k, fin[k]) for k in range(SBN) if fin[k] != init[k]]
        out.write(struct.pack("<2I", tid, seed))
        out.write(struct.pack("<22I", *(regs + [0] + nzcv + [0, 0])))
        out.write(struct.pack("<16d", *ds))
        out.write(struct.pack("<22I", *(exr[:15] + [0] + exflags + [fps, 0])))
        out.write(struct.pack("<16d", *exd))
        out.write(struct.pack("<I", len(changes)))
        for a_, v_ in changes: out.write(struct.pack("<II", a_, v_))
out.close()
r = subprocess.run([os.path.join(WORK, "insntest"), LIB, os.path.join(WORK, "vectors.bin")], capture_output=True, text=True)
fails = {}
for line in r.stdout.splitlines():
    if line.startswith("FAIL"):
        _, tid, why = line.split(" ", 2); tid = int(tid)
        fails.setdefault(tid, why)
for tid, why in sorted(fails.items(), key=lambda kv: tests[kv[0]][0].mnemonic):
    i = tests[tid][0]
    print(f"{i.address:08x} {i.mnemonic:10} {i.op_str:34} {why}")
print(r.stdout.splitlines()[-1] if r.stdout else r.stderr[:500])
print(f"{len(fails)} failing instructions")
