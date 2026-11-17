#!/usr/bin/env python3
"""Static recompiler: libGame.so (ARMv7 Thumb-2/ARM, softfp VFP) -> portable C.

Every guest function becomes `void F_<vaddr>(CPU *c)`. Guest memory is identity-mapped by the
runtime at BASE+vaddr in the low 4 GB, so guest pointers are host pointers. Calls are host calls,
returns are host returns; indirect control flow goes through runtime dispatch (call_addr).

usage: recomp.py <libGame.so> <outdir>
"""
import os, re, sys, struct, collections
import capstone as cs
from capstone import arm as A
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

LIB, OUT = sys.argv[1], sys.argv[2]
FILES = 64                          # number of generated .c files (parallel build)

# ----------------------------------------------------------------------------- ELF
f = open(LIB, "rb"); data = f.read(); elf = ELFFile(open(LIB, "rb"))
segs = [(s["p_vaddr"], s["p_offset"], s["p_filesz"]) for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]

def v2o(v):
    for va, off, fs in segs:
        if va <= v < va + fs: return v - va + off
    return None

def rd32(v): return struct.unpack_from("<I", data, v2o(v))[0]
def rd16(v): return struct.unpack_from("<H", data, v2o(v))[0]
def rd8(v): return data[v2o(v)]

text = elf.get_section_by_name(".text"); TEXT_LO = text["sh_addr"]; TEXT_HI = TEXT_LO + text["sh_size"]
plt = elf.get_section_by_name(".plt"); PLT_LO = plt["sh_addr"]; PLT_HI = PLT_LO + plt["sh_size"]
dynsym = elf.get_section_by_name(".dynsym")
relplt = elf.get_section_by_name(".rel.plt")
plt_sym = {}                                            # plt entry address -> imported symbol
for i, r in enumerate(relplt.iter_relocations()):
    plt_sym[PLT_LO + 0x14 + 12 * i] = dynsym.get_symbol(r["r_info_sym"]).name

funcs = {}                                              # vaddr -> (name, size, thumb)
for s in dynsym.iter_symbols():
    if s["st_info"]["type"] == "STT_FUNC" and s["st_shndx"] != "SHN_UNDEF" and s["st_value"]:
        v = s["st_value"]; funcs[v & ~1] = (s.name, s["st_size"], v & 1)

# native replacements for libgcc helpers (faster and exact)
NATIVE = {"__aeabi_idiv", "__aeabi_uidiv", "__aeabi_idivmod", "__aeabi_uidivmod", "__aeabi_ldivmod",
          "__aeabi_uldivmod", "__divsi3", "__udivsi3", "__aeabi_idiv0", "__aeabi_ldiv0"}

# ----------------------------------------------------------------------------- decoding
mdt = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB); mdt.detail = True
mda = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_ARM); mda.detail = True
mdt.skipdata = False

_dcache = {}
def decode_at(addr, thumb):
    k = (addr, thumb)
    if k in _dcache: return _dcache[k]
    o = v2o(addr); r = None
    for i in (mdt if thumb else mda).disasm(data[o:o + 4], addr, 1):
        r = i; break
    _dcache[k] = r
    return r

NORETURN_IMPORTS = {"abort", "exit", "_exit", "__stack_chk_fail", "__assert2", "longjmp", "siglongjmp", "_longjmp"}
NORETURN_FOUND = set()                                  # filled by fixpoint analysis below
def noreturn_target(t):
    if t in NORETURN_FOUND: return True
    if PLT_LO <= t < PLT_HI: return plt_sym.get(t) in NORETURN_IMPORTS
    n = funcs.get(t, ("",))[0]
    return bool(n) and (n in ("abort", "__cxa_throw", "__cxa_rethrow", "__cxa_bad_cast", "__cxa_bad_typeid",
                              "__cxa_pure_virtual", "_ZSt9terminatev", "__gnu_cxx::__verbose_terminate_handler")
                        or "__throw_" in n or n.startswith("_ZSt17__throw") or n.startswith("_ZSt20__throw")
                        or n.startswith("_ZSt19__throw") or n.startswith("_ZSt16__throw") or n.startswith("_ZSt24__throw")
                        or n.startswith("_ZSt21__throw") or n.startswith("_ZSt18__throw") or n.startswith("_ZSt25__throw"))

COND = {A.ARM_CC_EQ: "c->z", A.ARM_CC_NE: "!c->z", A.ARM_CC_HS: "c->cf", A.ARM_CC_LO: "!c->cf",
        A.ARM_CC_MI: "c->n", A.ARM_CC_PL: "!c->n", A.ARM_CC_VS: "c->v", A.ARM_CC_VC: "!c->v",
        A.ARM_CC_HI: "(c->cf && !c->z)", A.ARM_CC_LS: "(!c->cf || c->z)", A.ARM_CC_GE: "(c->n == c->v)",
        A.ARM_CC_LT: "(c->n != c->v)", A.ARM_CC_GT: "(!c->z && c->n == c->v)", A.ARM_CC_LE: "(c->z || c->n != c->v)"}

class Unsupported(Exception): pass

_CC = ("eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al", "cs", "cc")
def sets_flags(i):
    """S bit from the mnemonic (capstone's update_flags is unreliable for some ARM forms)"""
    m = i.mnemonic.split(".")[0]; name = i.insn_name()
    if m.endswith(_CC) and m[:-2] in (name, name + "s"): m = m[:-2]
    return m == name + "s"

INV = {1: 2, 2: 1, 3: 4, 4: 3, 5: 6, 6: 5, 7: 8, 8: 7, 9: 10, 10: 9, 11: 12, 12: 11, 13: 14, 14: 13}
IT_COND = {}        # thumb insn address inside an IT block -> condition code

def note_it(i):
    """record the conditions of the 1-4 instructions governed by an IT instruction"""
    first = i.cc; pat = i.mnemonic[2:]
    a = i.address + i.size
    conds = [first] + [first if ch == "t" else INV[first] for ch in pat]
    for cc in conds:
        IT_COND[a] = cc
        n = decode_at(a, True)
        if n is None: break
        a += n.size

def eff_cc(i):
    return IT_COND.get(i.address, i.cc)

class Func:
    def __init__(self, entry, thumb, end):
        self.entry, self.thumb, self.end = entry, thumb, end
        self.insns = {}          # addr -> insn
        self.labels = set()      # branch targets inside the function
        self.calls = set()       # direct call targets
        self.tails = set()       # tail-call targets outside the function
        self.switch = {}         # addr -> list of case targets (tbb/tbh/add pc)
        self.returns = False     # a return (or tail call) is reachable
        self.bxswitch = {}       # bx address -> (index register, [targets])  (PIC jump tables)
        self.cjump = {}          # computed-jump address -> [targets] (symbolically evaluated switch tables)
        self.local_rets = set()  # return addresses of intra-function "bl" (ARM libgcc local subroutines)

all_entries = {}                 # vaddr -> thumb  (functions to generate)
work = collections.deque()

def want(addr, thumb):
    if addr in all_entries or not (TEXT_LO <= addr < TEXT_HI): return
    all_entries[addr] = thumb; work.append(addr)

for v, (n, sz, th) in funcs.items():
    if TEXT_LO <= v < TEXT_HI: want(v, th)
# code pointers stored in data (vtables, init_array, callback tables): R_ARM_RELATIVE targets inside .text
reldyn = elf.get_section_by_name(".rel.dyn")
n_ptr = 0
for r in reldyn.iter_relocations():
    if r["r_info_type"] == 23:                                    # R_ARM_RELATIVE
        o = v2o(r["r_offset"])
        if o is None: continue
        val = struct.unpack_from("<I", data, o)[0]
        if TEXT_LO <= (val & ~1) < TEXT_HI and (val & ~1) not in all_entries:
            want(val & ~1, val & 1); n_ptr += 1
print("code pointers from relocations:", n_ptr)

import bisect
_starts = sorted(funcs)
def func_end(addr):
    if addr in funcs and funcs[addr][1]: return addr + funcs[addr][1]
    k = bisect.bisect_right(_starts, addr) - 1          # containing symbol, else next symbol start
    if k >= 0:
        v = _starts[k]; sz = funcs[v][1]
        if v <= addr < v + sz: return v + sz
    return _starts[k + 1] if k + 1 < len(_starts) else TEXT_HI

def explore(entry, thumb):
    fn = Func(entry, thumb, func_end(entry))
    todo = [entry]
    lits = {}                                    # reg -> literal loaded by "ldr rX, [pc, #imm]"
    while todo:
        a = todo.pop(); lits = {}
        while True:
            if a in fn.insns or not (TEXT_LO <= a < TEXT_HI): break
            if a >= fn.end:                          # ran past the function: tail into the next one
                if a in all_entries or a in funcs: fn.tails.add(a); want(a, thumb)
                break
            i = decode_at(a, thumb)
            if i is None: fn.insns[a] = None; break
            fn.insns[a] = i
            nxt = a + i.size
            iid = i.id; ops = i.operands; cond = eff_cc(i) not in (A.ARM_CC_AL, A.ARM_CC_INVALID)
            if iid == A.ARM_INS_IT: note_it(i)
            # address-taken code: "ldr rX, [pc, #i]; add rX, pc" or "adr rX, label" pointing into .text
            if iid == A.ARM_INS_LDR and len(ops) == 2 and ops[1].type == A.ARM_OP_MEM and i.reg_name(ops[1].mem.base) == "pc" \
                    and not ops[1].mem.index:
                lits[ops[0].reg] = rd32(((a + (4 if thumb else 8)) & ~3) + ops[1].mem.disp)
            elif iid == A.ARM_INS_ADD and len(ops) == 2 and ops[1].type == A.ARM_OP_REG and i.reg_name(ops[1].reg) == "pc" \
                    and ops[0].reg in lits:
                v = (lits.pop(ops[0].reg) + a + 4) & 0xffffffff
                if TEXT_LO <= (v & ~1) < TEXT_HI and (v & 1) == (1 if thumb else 0): want(v & ~1, v & 1)
            elif iid == A.ARM_INS_ADR and thumb:
                v = ((a + 4) & ~3) + ops[1].imm
                if TEXT_LO <= v < TEXT_HI and v not in fn.insns and v % 2 == 0: pass   # data in text (tables)
            if iid in (A.ARM_INS_B, A.ARM_INS_CBZ, A.ARM_INS_CBNZ):
                t = ops[-1].imm
                if fn.entry <= t < fn.end: fn.labels.add(t); todo.append(t)
                else: fn.tails.add(t); want(t, thumb); fn.returns = True
                if iid == A.ARM_INS_B and not cond: break
            elif iid == A.ARM_INS_BL and ops[0].type == A.ARM_OP_IMM and local_call(fn, ops[0].imm):
                t = ops[0].imm; fn.labels.add(t); todo.append(t); fn.local_rets.add(nxt)
            elif iid == A.ARM_INS_BL and ops[0].type == A.ARM_OP_IMM and far_jump(fn, ops[0].imm):
                t = ops[0].imm; fn.labels.add(t); todo.append(t)          # Thumb-1 "bl" used as a long jump
                if not cond: break
            elif iid in (A.ARM_INS_BL, A.ARM_INS_BLX) and ops[0].type == A.ARM_OP_IMM:
                t = ops[0].imm; tthumb = thumb if iid == A.ARM_INS_BL else (not thumb)
                if not (PLT_LO <= t < PLT_HI): want(t, tthumb)
                fn.calls.add(t)
                if noreturn_target(t) and not cond: break
            elif iid in (A.ARM_INS_TBB, A.ARM_INS_TBH):
                base = a + 4; n = 0; targets = []; lim = None
                idxr = i.operands[0].mem.index; count = None
                for back in range(2, 24, 2):                 # bounds check: cmp idx, #N ; bhi/bcs default
                    p_ = fn.insns.get(a - back)
                    if p_ is not None and p_.id == A.ARM_INS_CMP and p_.operands[0].type == A.ARM_OP_REG \
                            and p_.operands[0].reg == idxr and p_.operands[1].type == A.ARM_OP_IMM:
                        count = p_.operands[1].imm + 1; break
                while (count is not None and n < count) or (count is None and (lim is None or base + n * (1 if iid == A.ARM_INS_TBB else 2) < lim)):
                    e = rd8(base + n) if iid == A.ARM_INS_TBB else rd16(base + 2 * n)
                    t = base + 2 * e; targets.append(t)
                    lim = t if lim is None else min(lim, t); n += 1
                    if n > 4096: raise Unsupported(f"tbb table at {a:x}")
                fn.switch[a] = targets
                for t in targets: fn.labels.add(t); todo.append(t)
                break
            elif iid == A.ARM_INS_BX and thumb and i.reg_name(ops[0].reg) not in ("pc", "lr") and pic_jump_table(fn, i):
                ri, targets = pic_jump_table(fn, i)
                fn.bxswitch[a] = (ri, targets)
                for t in targets: fn.labels.add(t); todo.append(t)
                if not cond: break
            elif (iid == A.ARM_INS_MOV and ops and ops[0].type == A.ARM_OP_REG and i.reg_name(ops[0].reg) == "pc"
                  or iid == A.ARM_INS_ADD and ops and i.reg_name(ops[0].reg) == "pc" and veneer_target(fn.insns, i) is None
                  or iid == A.ARM_INS_BX and i.reg_name(ops[0].reg) not in ("pc", "lr")) and eval_computed_jump(fn, i, thumb):
                targets = eval_computed_jump(fn, i, thumb)
                fn.cjump[a] = targets
                for t in targets:
                    if fn.entry <= t < fn.end: fn.labels.add(t); todo.append(t)
                    else: want(t, thumb)
                if not cond: break
            elif iid == A.ARM_INS_BX and thumb and i.reg_name(ops[0].reg) == "pc":
                t = (a + 4) & ~3; want(t, 0); fn.tails.add(t); fn.returns = True
                if not cond: break
            elif not thumb and veneer_target(fn.insns, i) is not None:
                t = veneer_target(fn.insns, i)
                if not (PLT_LO <= t < PLT_HI): want(t & ~1, t & 1)
                fn.tails.add(t); fn.returns = True
                if not cond: break
            elif is_return_or_jump(i, thumb):
                fn.returns = True
                if not cond: break
            elif iid in (A.ARM_INS_UDF, A.ARM_INS_BKPT) or i.mnemonic.startswith(("stc", "ldc", "cdp", "mrc", "mcr")):
                break                                # garbage / trap: not real code flow
            a = nxt
    return fn

def veneer_target(fn_insns, i):
    """ARM 'add pc, rX, pc' preceded by 'ldr rX, [pc, #imm]' (linker veneer): absolute target or None"""
    if i.id != A.ARM_INS_ADD or len(i.operands) != 3: return None
    o = i.operands
    if not all(x.type == A.ARM_OP_REG for x in o): return None
    if i.reg_name(o[0].reg) != "pc" or i.reg_name(o[2].reg) != "pc": return None
    rx = i.reg_name(o[1].reg)
    p = fn_insns.get(i.address - 4)
    if p is None or p.id != A.ARM_INS_LDR or p.reg_name(p.operands[0].reg) != rx: return None
    m = p.operands[1].mem
    if p.reg_name(m.base) != "pc" or m.index: return None
    lit = rd32(((p.address + 8) & ~3) + m.disp)
    return (lit + i.address + 8) & 0xffffffff

def pic_jump_table(fn, i):
    """thumb: adr rT,#tbl ; ldr.w rO,[rT, rI, lsl #2] ; add rT, rO ; bx rT   -> (rI, targets) or None"""
    a = i.address; rt_ = i.operands[0].reg
    add = fn.insns.get(a - 2); ldr = fn.insns.get(a - 6); adr = fn.insns.get(a - 8)
    if not (add and ldr and adr): return None
    if add.id != A.ARM_INS_ADD or ldr.id != A.ARM_INS_LDR or adr.id != A.ARM_INS_ADR: return None
    if adr.operands[0].reg != rt_ or ldr.operands[1].mem.base != rt_ or not ldr.operands[1].mem.index: return None
    ri = ldr.operands[1].mem.index
    tbl = ((adr.address + 4) & ~3) + adr.operands[1].imm
    count = None
    for back in range(10, 30, 2):
        p_ = fn.insns.get(a - back)
        if p_ is not None and p_.id == A.ARM_INS_CMP and p_.operands[0].type == A.ARM_OP_REG and p_.operands[0].reg == ri \
                and p_.operands[1].type == A.ARM_OP_IMM:
            count = p_.operands[1].imm + 1; break
    if count is None or count > 1024: return None
    targets = [((tbl + struct.unpack("<i", struct.pack("<I", rd32(tbl + 4 * k)))[0]) & 0xffffffff) & ~1 for k in range(count)]
    return adr.reg_name(ri), targets

_cont_cache = {}
def is_continuation(t, thumb):
    """True if the code at t is a block of the *calling* function (GCC thumb-1 'bl' far jump): it has no
    prologue saving lr, yet some reachable path returns through a popped pc / popped register."""
    if (t, thumb) in _cont_cache: return _cont_cache[(t, thumb)]
    # prologue at the entry => real function
    a = t
    for _ in range(6):
        i = decode_at(a, thumb)
        if i is None: break
        regs = [i.reg_name(o.reg) for o in i.operands if o.type == A.ARM_OP_REG]
        if i.id in (A.ARM_INS_PUSH, A.ARM_INS_STMDB) and "lr" in regs:
            _cont_cache[(t, thumb)] = False; return False
        if i.id in (A.ARM_INS_B, A.ARM_INS_BX, A.ARM_INS_POP, A.ARM_INS_CBZ, A.ARM_INS_CBNZ): break
        a += i.size
    # search all reachable code for the way it returns
    todo = [t]; seen = set(); res = False; budget = 6000
    while todo and budget > 0:
        a = todo.pop(); popped = set()
        while budget > 0:
            budget -= 1
            if a in seen or not (TEXT_LO <= a < TEXT_HI): break
            seen.add(a); i = decode_at(a, thumb)
            if i is None: break
            regs = [i.reg_name(o.reg) for o in i.operands if o.type == A.ARM_OP_REG]
            uncond = eff_cc(i) in (A.ARM_CC_AL, A.ARM_CC_INVALID)
            if i.id == A.ARM_INS_BX and regs == ["lr"]: _cont_cache[(t, thumb)] = False; return False
            if i.id == A.ARM_INS_POP:
                if "pc" in regs: res = True; budget = 0; break
                popped |= set(regs)
            if i.id == A.ARM_INS_BX and regs and regs[0] in popped: res = True; budget = 0; break
            if i.id == A.ARM_INS_IT: note_it(i)
            if i.id in (A.ARM_INS_B, A.ARM_INS_CBZ, A.ARM_INS_CBNZ):
                todo.append(i.operands[-1].imm)
                if i.id == A.ARM_INS_B and uncond: break
            elif i.id == A.ARM_INS_BL and i.operands[0].type == A.ARM_OP_IMM:
                tt = i.operands[0].imm
                if not (PLT_LO <= tt < PLT_HI) and tt not in funcs and not noreturn_target(tt):
                    todo.append(tt)                                   # possibly another far jump
                if noreturn_target(tt): break
            elif i.id in (A.ARM_INS_TBB, A.ARM_INS_TBH, A.ARM_INS_BX, A.ARM_INS_UDF, A.ARM_INS_BKPT): break
            elif i.id == A.ARM_INS_MOV and regs and regs[0] == "pc": break
            a += i.size
    _cont_cache[(t, thumb)] = res
    return res

def local_call(fn, t):
    """ARM 'bl' to a label inside the same (symbol-sized) function: libgcc local subroutine that may return
    with 'mov pc, lr' or leave the whole function by popping the caller's frame"""
    return (not fn.thumb) and fn.entry in funcs and funcs[fn.entry][1] > 0 and fn.entry < t < fn.end and t not in funcs

def far_jump(fn, t):
    """bl used as a jump (GCC thumb1 far_jump): the target is a block of the calling function itself"""
    if not fn.thumb or t in funcs or PLT_LO <= t < PLT_HI: return False
    if fn.entry in funcs and funcs[fn.entry][1] > 0:
        return fn.entry < t < fn.end
    return t > fn.entry and t < fn.end and is_continuation(t, True)

def eval_computed_jump(fn, i, thumb):
    """targets of 'mov pc, rX' / 'add pc, ..' / 'bx rX' built from a PIC switch table, via a tiny symbolic
    evaluation of the straight-line code before it. Values: ('c',v) | ('x',s,k)=idx*s+k | ('e',tbl,s,k)=R32(tbl+idx*s)+k"""
    ends = {a + (x.size if x else 2): x for a, x in fn.insns.items() if x is not None}
    seq = []; a = i.address
    while len(seq) < 16 and a in ends:
        p_ = ends[a]
        if p_.id in (A.ARM_INS_B, A.ARM_INS_BL, A.ARM_INS_BLX, A.ARM_INS_BX, A.ARM_INS_CBZ, A.ARM_INS_CBNZ, A.ARM_INS_POP): break
        seq.append(p_); a = p_.address
    seq.reverse()
    pcoff = 4 if thumb else 8
    regs = {}; idxreg = None
    def val(r):
        return regs[r] if r in regs else ("u", r)
    def as_idx(x_, scale):
        nonlocal idxreg
        if x_ and x_[0] == "u": idxreg = x_[1]; return ("x", scale, 0)
        if x_ and x_[0] == "x" and x_[2] == 0: return ("x", x_[1] * scale, 0)
        return None
    def add(x, y):
        if x is None or y is None or x[0] == "u" or y[0] == "u": return None
        if x[0] == "c" and y[0] == "c": return ("c", (x[1] + y[1]) & 0xffffffff)
        if x[0] == "c": x, y = y, x
        if y[0] != "c": return None
        if x[0] == "x": return ("x", x[1], (x[2] + y[1]) & 0xffffffff)
        if x[0] == "e": return ("e", x[1], x[2], (x[3] + y[1]) & 0xffffffff)
        return None
    for p_ in seq + [i]:
        o = p_.operands; nm = lambda k: p_.reg_name(o[k].reg)
        try:
            if p_ is i:
                if p_.id == A.ARM_INS_MOV and len(o) == 2 and o[1].type == A.ARM_OP_REG: e = val(nm(1))
                elif p_.id == A.ARM_INS_BX: e = val(nm(0))
                elif p_.id == A.ARM_INS_ADD and len(o) == 2: e = add(val("pc") if False else ("c", p_.address + pcoff), val(nm(1)))
                else: return None
                if not e or e[0] != "e": return None
                count = None
                for back in range(2, 60, 2):
                    q = fn.insns.get(i.address - back)
                    if q is not None and q.id == A.ARM_INS_CMP and q.operands[0].type == A.ARM_OP_REG \
                            and q.reg_name(q.operands[0].reg) == idxreg and q.operands[1].type == A.ARM_OP_IMM:
                        count = q.operands[1].imm + 1; break
                if count is None or count > 1024: return None
                return [((rd32((e[1] + k * e[2]) & 0xffffffff) + e[3]) & 0xffffffff) & ~1 for k in range(count)]
            if p_.id == A.ARM_INS_LDR and o[1].type == A.ARM_OP_MEM:
                m = o[1].mem; b = p_.reg_name(m.base)
                if b == "pc" and not m.index: regs[nm(0)] = ("c", rd32(((p_.address + pcoff) & ~3) + m.disp)); continue
                base = val(b)
                if m.index:
                    ix = val(p_.reg_name(m.index)); sh_ = o[1].shift.value if o[1].shift.type else 0
                    if base and base[0] == "c" and as_idx(ix, 1 << sh_):
                        regs[nm(0)] = ("e", base[1], as_idx(ix, 1 << sh_)[1], 0); continue
                    if ix and ix[0] == "c" and not sh_ and as_idx(base, 1):
                        regs[nm(0)] = ("e", ix[1], as_idx(base, 1)[1], 0); continue
                elif base and base[0] == "x":
                    regs[nm(0)] = ("e", (base[2] + m.disp) & 0xffffffff, base[1], 0); continue
                regs[nm(0)] = ("u", nm(0)); continue
            if p_.id == A.ARM_INS_ADD:
                if len(o) == 2: regs[nm(0)] = add(val(nm(0)), ("c", p_.address + pcoff) if nm(1) == "pc" else val(nm(1)))
                elif o[2].type == A.ARM_OP_IMM: regs[nm(0)] = add(("c", p_.address + pcoff) if nm(1) == "pc" else val(nm(1)), ("c", o[2].imm))
                elif not o[2].shift.type: regs[nm(0)] = add(val(nm(1)), ("c", p_.address + pcoff) if nm(2) == "pc" else val(nm(2)))
                else:
                    x_ = as_idx(val(nm(2)), 1 << o[2].shift.value) if o[2].shift.type == A.ARM_SFT_LSL else None
                    regs[nm(0)] = add(val(nm(1)), x_) if x_ else None
                continue
            if p_.id == A.ARM_INS_LSL and o[-1].type == A.ARM_OP_IMM:
                regs[nm(0)] = as_idx(val(nm(1) if len(o) == 3 else nm(0)), 1 << o[-1].imm); continue
            if p_.id == A.ARM_INS_MOV and len(o) == 2 and o[1].type == A.ARM_OP_REG: regs[nm(0)] = val(nm(1)); continue
            if p_.id in (A.ARM_INS_CMP, A.ARM_INS_NOP, A.ARM_INS_IT): continue
            if p_.id == A.ARM_INS_STR: continue
            if o and o[0].type == A.ARM_OP_REG: regs[nm(0)] = None
        except Exception:
            return None
    return None

def reg_list(i):
    return [i.reg_name(o.reg) for o in i.operands if o.type == A.ARM_OP_REG]

def is_return_or_jump(i, thumb):
    iid = i.id
    if iid == A.ARM_INS_BX: return True
    if iid == A.ARM_INS_POP and "pc" in reg_list(i): return True
    if iid in (A.ARM_INS_LDM, A.ARM_INS_LDMDB, A.ARM_INS_LDMIB, A.ARM_INS_LDMDA) and "pc" in reg_list(i): return True
    if iid == A.ARM_INS_LDR and i.operands[0].type == A.ARM_OP_REG and i.reg_name(i.operands[0].reg) == "pc": return True
    if iid in (A.ARM_INS_MOV, A.ARM_INS_ADD, A.ARM_INS_SUB) and i.operands and i.operands[0].type == A.ARM_OP_REG \
            and i.reg_name(i.operands[0].reg) == "pc": return True
    if iid in (A.ARM_INS_UDF, A.ARM_INS_BKPT): return True
    return False

# ----------------------------------------------------------------------------- C emission
RN = {f"r{k}": k for k in range(13)}; RN.update({"sb": 9, "sl": 10, "fp": 11, "ip": 12, "sp": 13, "lr": 14, "r13": 13, "r14": 14})

class Gen:
    def __init__(self, fn):
        self.fn = fn; self.thumb = fn.thumb; self.out = []; self.ins = None

    def pcval(self):
        return self.ins.address + (4 if self.thumb else 8)

    def R(self, name):
        if name == "pc": return f"(BASE + 0x{self.pcval():x}u)"
        return f"c->r[{RN[name]}]"

    def W(self, name, expr):
        if name == "pc": raise Unsupported("write pc")
        self.out.append(f"  c->r[{RN[name]}] = {expr};")

    def shifted(self, o, want_carry=False):
        """value (and carry expression) of a register operand with optional shift"""
        v = self.R(self.ins.reg_name(o.reg))
        st, sv = o.shift.type, o.shift.value
        if st == 0: return v, None
        if st in (A.ARM_SFT_LSL_REG, A.ARM_SFT_LSR_REG, A.ARM_SFT_ASR_REG, A.ARM_SFT_ROR_REG):
            amt = f"({self.R(self.ins.reg_name(sv))} & 0xff)"
            kind = {A.ARM_SFT_LSL_REG: 0, A.ARM_SFT_LSR_REG: 1, A.ARM_SFT_ASR_REG: 2, A.ARM_SFT_ROR_REG: 3}[st]
        else:
            amt = str(sv)
            kind = {A.ARM_SFT_LSL: 0, A.ARM_SFT_LSR: 1, A.ARM_SFT_ASR: 2, A.ARM_SFT_ROR: 3, A.ARM_SFT_RRX: 4}[st]
        if want_carry:
            return f"shc(c, {v}, {kind}, {amt}, &sc)", "sc"
        return f"sh({v}, {kind}, {amt}, c->cf)", None

    def op2(self, o, want_carry=False):
        if o.type == A.ARM_OP_IMM:
            imm = o.imm & 0xffffffff
            return f"0x{imm:x}u", (f"{imm >> 31}" if want_carry and imm > 0xff else None)
        if o.type == A.ARM_OP_REG: return self.shifted(o, want_carry)
        raise Unsupported("op2")

    def addr_of(self, mem_op, extra=None):
        """effective address, and the writeback value; handles pre/post index"""
        i = self.ins; m = mem_op.mem
        base = self.R(i.reg_name(m.base)) if i.reg_name(m.base) != "pc" else \
            f"(BASE + 0x{(i.address + (4 if self.thumb else 8)) & ~3:x}u)"
        if m.index:
            idx = self.R(i.reg_name(m.index))
            if mem_op.shift.type == A.ARM_SFT_LSL and mem_op.shift.value: idx = f"({idx} << {mem_op.shift.value})"
            elif mem_op.shift.type: raise Unsupported("mem shift")
            off = f"- {idx}" if mem_op.subtracted else f"+ {idx}"
        else:
            off = f"+ (uint32_t){m.disp}" if m.disp >= 0 else f"- {-m.disp}u"
        if i.post_index:
            if extra is None: return base, None
            if extra.type == A.ARM_OP_IMM: pv = f"{base} + (uint32_t){extra.imm}" if extra.imm >= 0 else f"{base} - {-extra.imm}u"
            else:
                pr = self.R(i.reg_name(extra.reg))
                if extra.shift.type: pr = f"({pr} << {extra.shift.value})"
                pv = f"{base} - {pr}" if extra.subtracted else f"{base} + {pr}"
            return base, pv
        ea = f"({base} {off})" if (m.index or m.disp) else base
        return ea, (ea if i.writeback else None)

    def emit_cond_open(self):
        i = self.ins; cc = eff_cc(i)
        if cc not in (A.ARM_CC_AL, A.ARM_CC_INVALID) and i.id not in (A.ARM_INS_B, A.ARM_INS_IT):
            self.out.append(f"  if ({COND[cc]}) {{"); return True
        return False

    def gen(self):
        fn = self.fn
        name = funcs.get(fn.entry, ("",))[0]
        self.out.append(f"/* {name} */")
        self.out.append(f"void F_{fn.entry:08x}(CPU *c) {{")
        self.out.append("  uint32_t t, t2, sc; uint64_t u64; (void)t; (void)t2; (void)sc; (void)u64;")
        self.out.append("  const uint32_t elr = c->r[14] | 1u; (void)elr;      /* return address at entry */")
        for a in sorted(k for k, v in fn.insns.items()):
            i = fn.insns[a]
            self.out.append(f"L_{a:x}:;")
            if i is None:
                self.out.append(f"  unimpl(c, 0x{a:x}u, \"undecodable\"); return;"); continue
            self.ins = i
            self.out.append(f"  /* {a:x}: {i.mnemonic} {i.op_str} */ PC(0x{a:x}u);")
            try:
                opened = self.emit_cond_open()
                self.one(i)
                if opened: self.out.append("  }")
            except Unsupported as e:
                self.out.append(f"  unimpl(c, 0x{a:x}u, \"{i.mnemonic} {i.op_str} ({e})\"); return;")
                unsupported[f"{i.mnemonic} ({e})"] += 1
            # fall-through to an instruction that was not decoded (end of reachable code)
            nxt = a + i.size
            if nxt not in fn.insns and not self.ends_flow(i):
                if nxt in all_entries: self.out.append(f"  F_{nxt:08x}(c); return;")
                else: self.out.append(f"  unimpl(c, 0x{nxt:x}u, \"fallthrough\"); return;")
        self.out.append("}")
        return "\n".join(self.out)

    def ret(self, val=None):
        """emit a return; in functions with local subroutines, dispatch on the target value"""
        if not self.fn.local_rets: return "return;" if val is None else f"JUMP({val});"
        v = val or "c->r[14]"
        cases = " ".join(f"case 0x{r:x}u: goto L_{r:x};" for r in sorted(self.fn.local_rets))
        return f"{{ uint32_t _r = {v}; switch ((_r - BASE) & ~1u) {{ {cases} default: JUMP(_r); }} }}"

    def ends_flow(self, i):
        cond = eff_cc(i) not in (A.ARM_CC_AL, A.ARM_CC_INVALID)
        if cond: return False
        if i.id == A.ARM_INS_B: return True
        if i.id == A.ARM_INS_BL and i.operands[0].type == A.ARM_OP_IMM and far_jump(self.fn, i.operands[0].imm): return True
        if i.id in (A.ARM_INS_TBB, A.ARM_INS_TBH): return True
        return is_return_or_jump(i, self.thumb)

    # ------------------------------------------------------------------ branches
    def goto(self, t):
        fn = self.fn
        if t in fn.insns: return f"goto L_{t:x};"
        if t in all_entries: return f"{{ F_{t:08x}(c); return; }}"
        return f"{{ unimpl(c, 0x{t & 0xffffffff:x}u, \"bad branch\"); return; }}"

    def call(self, t, thumb_target):
        ret = self.ins.address + self.ins.size
        lr = (ret | 1) if self.thumb else ret
        self.out.append(f"  c->r[14] = BASE + 0x{lr:x}u;")
        if PLT_LO <= t < PLT_HI:
            sym = plt_sym[t]; imports.add(sym)
            if sym in ("setjmp", "sigsetjmp", "_setjmp"):
                self.out.append(f"  GUEST_SETJMP(c);")
            else:
                self.out.append(f"  imp_{sym}(c);")
        elif funcs.get(t, ("",))[0] in NATIVE:
            self.out.append(f"  nat_{funcs[t][0]}(c);")
        elif t in all_entries:
            self.out.append(f"  F_{t:08x}(c);")
        else:
            self.out.append(f"  unimpl(c, 0x{t & 0xffffffff:x}u, \"call outside text\"); return;")

    def one(self, i):
        iid = i.id; ops = i.operands; o = self.out
        if i.address in self.fn.cjump:
            r = i.reg_name(ops[1].reg) if iid == A.ARM_INS_MOV else i.reg_name(ops[0].reg)
            if iid == A.ARM_INS_ADD: raise Unsupported("add-pc switch")
            o.append(f"  t = {self.R(r)} - BASE; switch (t & ~1u) {{")
            for t in sorted(set(self.fn.cjump[i.address])):
                tgt = (f"goto L_{t:x};" if t in self.fn.insns else f"{{ F_{t:08x}(c); return; }}" if t in all_entries
                       else f"{{ unimpl(c, 0x{t:x}u, \"switch target\"); return; }}")
                o.append(f"    case 0x{t:x}u: {tgt}")
            o.append(f"    default: JUMP(t + BASE);\n  }}")
            return
        mn = i.mnemonic.split(".")[0]
        S = sets_flags(i)
        if i.address in IT_COND and i.size == 2 and iid not in (A.ARM_INS_CMP, A.ARM_INS_CMN, A.ARM_INS_TST, A.ARM_INS_TEQ):
            S = False                                   # 16-bit encodings do not set flags inside an IT block
        if iid == A.ARM_INS_IT or iid in (A.ARM_INS_NOP, A.ARM_INS_DMB, A.ARM_INS_DSB, A.ARM_INS_ISB, A.ARM_INS_PLD,
                                          A.ARM_INS_PLI, A.ARM_INS_YIELD, A.ARM_INS_HINT, A.ARM_INS_CLREX):
            return
        if iid == A.ARM_INS_B:
            t = ops[0].imm; cc = eff_cc(i)
            if cc not in (A.ARM_CC_AL, A.ARM_CC_INVALID): o.append(f"  if ({COND[cc]}) {self.goto(t)}")
            else: o.append("  " + self.goto(t))
            return
        if iid in (A.ARM_INS_CBZ, A.ARM_INS_CBNZ):
            r = self.R(i.reg_name(ops[0].reg)); t = ops[1].imm
            o.append(f"  if ({r} {'==' if iid == A.ARM_INS_CBZ else '!='} 0) {self.goto(t)}"); return
        if iid in (A.ARM_INS_BL, A.ARM_INS_BLX):
            if iid == A.ARM_INS_BL and ops[0].type == A.ARM_OP_IMM and local_call(self.fn, ops[0].imm):
                o.append(f"  c->r[14] = BASE + 0x{i.address + 4:x}u; goto L_{ops[0].imm:x};"); return
            if iid == A.ARM_INS_BL and ops[0].type == A.ARM_OP_IMM and far_jump(self.fn, ops[0].imm):
                o.append(f"  c->r[14] = BASE + 0x{(i.address + i.size) | (1 if self.thumb else 0):x}u; goto L_{ops[0].imm:x};"); return
            if ops[0].type == A.ARM_OP_IMM:
                self.call(ops[0].imm, None); return
            r = self.R(i.reg_name(ops[0].reg)); ret = i.address + i.size
            o.append(f"  t = {r}; c->r[14] = BASE + 0x{(ret | 1) if self.thumb else ret:x}u; call_addr(c, t);"); return
        if iid == A.ARM_INS_BX and self.thumb and i.reg_name(ops[0].reg) == "pc":
            o.append(f"  F_{(i.address + 4) & ~3:08x}(c); return;"); return
        if iid == A.ARM_INS_BX and i.address in self.fn.bxswitch:
            ri, targets = self.fn.bxswitch[i.address]
            o.append(f"  switch ({self.R(ri)}) {{")
            for k, t in enumerate(targets): o.append(f"    case {k}: goto L_{t:x};")
            o.append(f"    default: call_addr(c, {self.R(i.reg_name(ops[0].reg))}); return;\n  }}")
            return
        vt = veneer_target(self.fn.insns, i) if not self.thumb else None
        if vt is not None:
            if PLT_LO <= vt < PLT_HI:
                sym = plt_sym[vt]; imports.add(sym); o.append(f"  imp_{sym}(c); return;")
            elif funcs.get(vt & ~1, ("",))[0] in NATIVE: o.append(f"  nat_{funcs[vt & ~1][0]}(c); return;")
            else: o.append(f"  F_{vt & ~1:08x}(c); return;")
            return
        if iid == A.ARM_INS_BX:
            r = i.reg_name(ops[0].reg)
            if r == "lr": o.append("  " + self.ret())
            else: o.append("  " + self.ret(self.R(r)))
            return
        if iid in (A.ARM_INS_TBB, A.ARM_INS_TBH):
            idx = self.R(i.reg_name(ops[0].mem.index))
            o.append(f"  switch ({idx}) {{")
            for k, t in enumerate(self.fn.switch[i.address]): o.append(f"    case {k}: goto L_{t:x};")
            o.append(f"    default: unimpl(c, 0x{i.address:x}u, \"switch range\"); return;\n  }}")
            return
        if iid in (A.ARM_INS_PUSH, A.ARM_INS_STMDB) and (iid == A.ARM_INS_PUSH or i.reg_name(ops[0].reg) == "sp" and i.writeback):
            regs = reg_list(i) if iid == A.ARM_INS_PUSH else reg_list(i)[1:]
            o.append(f"  t = c->r[13] - {4 * len(regs)};")
            for k, r in enumerate(regs):
                v = f"(BASE + 0x{self.pcval():x}u)" if r == "pc" else self.R(r)
                o.append(f"  W32(t + {4 * k}, {v});")
            o.append("  c->r[13] = t;"); return
        if iid == A.ARM_INS_POP or (iid == A.ARM_INS_LDM and i.reg_name(ops[0].reg) == "sp" and i.writeback):
            regs = reg_list(i) if iid == A.ARM_INS_POP else reg_list(i)[1:]
            o.append("  t = c->r[13];")
            for k, r in enumerate(regs):
                if r == "pc": o.append(f"  t2 = R32(t + {4 * k});"); continue
                o.append(f"  c->r[{RN[r]}] = R32(t + {4 * k});")
            o.append(f"  c->r[13] = t + {4 * len(regs)};")
            if "pc" in regs: o.append("  " + (self.ret("t2") if self.fn.local_rets else "return;"))
            return
        if iid in (A.ARM_INS_LDM, A.ARM_INS_LDMDB, A.ARM_INS_LDMIB, A.ARM_INS_LDMDA,
                   A.ARM_INS_STM, A.ARM_INS_STMDB, A.ARM_INS_STMIB, A.ARM_INS_STMDA):
            base = i.reg_name(ops[0].reg); regs = reg_list(i)[1:]; n = len(regs)
            load = iid in (A.ARM_INS_LDM, A.ARM_INS_LDMDB, A.ARM_INS_LDMIB, A.ARM_INS_LDMDA)
            start = {A.ARM_INS_LDM: 0, A.ARM_INS_STM: 0, A.ARM_INS_LDMIB: 4, A.ARM_INS_STMIB: 4,
                     A.ARM_INS_LDMDB: -4 * n, A.ARM_INS_STMDB: -4 * n, A.ARM_INS_LDMDA: -4 * n + 4,
                     A.ARM_INS_STMDA: -4 * n + 4}[iid]
            wb = 4 * n if iid in (A.ARM_INS_LDM, A.ARM_INS_STM, A.ARM_INS_LDMIB, A.ARM_INS_STMIB) else -4 * n
            o.append(f"  t = {self.R(base)} + (uint32_t)({start});")
            for k, r in enumerate(regs):
                if load:
                    if r == "pc": o.append(f"  t2 = R32(t + {4 * k});")
                    else: o.append(f"  c->r[{RN[r]}] = R32(t + {4 * k});")
                else:
                    v = f"(BASE + 0x{self.pcval():x}u)" if r == "pc" else self.R(r)
                    o.append(f"  W32(t + {4 * k}, {v});")
            if i.writeback and not (load and base in regs):
                o.append(f"  {self.R(base)} = {self.R(base)} + (uint32_t)({wb});")
            if load and "pc" in regs: o.append("  JUMP(t2);")
            return
        if iid in (A.ARM_INS_LDR, A.ARM_INS_LDRB, A.ARM_INS_LDRH, A.ARM_INS_LDRSB, A.ARM_INS_LDRSH,
                   A.ARM_INS_LDRT, A.ARM_INS_LDRBT, A.ARM_INS_LDRHT, A.ARM_INS_LDREX, A.ARM_INS_LDREXB, A.ARM_INS_LDREXH):
            dst = i.reg_name(ops[0].reg); mem = ops[1]; extra = ops[2] if len(ops) > 2 else None
            ea, wbv = self.addr_of(mem, extra)
            fn = {A.ARM_INS_LDR: "R32", A.ARM_INS_LDRT: "R32", A.ARM_INS_LDREX: "R32", A.ARM_INS_LDRB: "R8",
                  A.ARM_INS_LDRBT: "R8", A.ARM_INS_LDREXB: "R8", A.ARM_INS_LDRH: "R16", A.ARM_INS_LDRHT: "R16",
                  A.ARM_INS_LDREXH: "R16", A.ARM_INS_LDRSB: "RS8", A.ARM_INS_LDRSH: "RS16"}[iid]
            o.append(f"  t = {ea};")
            if wbv: o.append(f"  t2 = {wbv if i.post_index else 't'};")
            if dst == "pc":
                o.append(f"  t = {fn}(t);")
                if wbv: o.append(f"  {self.R(i.reg_name(mem.mem.base))} = t2;")
                if i.reg_name(mem.mem.base) == "sp" and i.post_index: o.append("  return;")
                else: o.append("  JUMP(t);")
                return
            o.append(f"  {self.R(dst)} = {fn}(t);")
            if wbv and i.reg_name(mem.mem.base) != dst: o.append(f"  {self.R(i.reg_name(mem.mem.base))} = t2;")
            return
        if iid in (A.ARM_INS_STR, A.ARM_INS_STRB, A.ARM_INS_STRH, A.ARM_INS_STRT, A.ARM_INS_STRBT, A.ARM_INS_STRHT):
            src = i.reg_name(ops[0].reg); mem = ops[1]; extra = ops[2] if len(ops) > 2 else None
            ea, wbv = self.addr_of(mem, extra)
            fn = {A.ARM_INS_STR: "W32", A.ARM_INS_STRT: "W32", A.ARM_INS_STRB: "W8", A.ARM_INS_STRBT: "W8",
                  A.ARM_INS_STRH: "W16", A.ARM_INS_STRHT: "W16"}[iid]
            v = f"(BASE + 0x{self.pcval():x}u)" if src == "pc" else self.R(src)
            o.append(f"  t = {ea};")
            if wbv: o.append(f"  t2 = {wbv if i.post_index else 't'};")
            o.append(f"  {fn}(t, {v});")
            if wbv: o.append(f"  {self.R(i.reg_name(mem.mem.base))} = t2;")
            return
        if iid in (A.ARM_INS_STREX, A.ARM_INS_STREXB, A.ARM_INS_STREXH):
            st = i.reg_name(ops[0].reg); src = i.reg_name(ops[1].reg); ea, _ = self.addr_of(ops[2])
            fn = {A.ARM_INS_STREX: "W32", A.ARM_INS_STREXB: "W8", A.ARM_INS_STREXH: "W16"}[iid]
            o.append(f"  {fn}({ea}, {self.R(src)}); {self.R(st)} = 0;"); return
        if iid in (A.ARM_INS_LDRD, A.ARM_INS_STRD):
            r1, r2 = i.reg_name(ops[0].reg), i.reg_name(ops[1].reg); mem = ops[2]; extra = ops[3] if len(ops) > 3 else None
            ea, wbv = self.addr_of(mem, extra)
            o.append(f"  t = {ea};")
            if wbv: o.append(f"  t2 = {wbv if i.post_index else 't'};")
            if iid == A.ARM_INS_LDRD:
                o.append(f"  sc = R32(t + 4); {self.R(r1)} = R32(t); {self.R(r2)} = sc;")
            else:
                o.append(f"  W32(t, {self.R(r1)}); W32(t + 4, {self.R(r2)});")
            if wbv: o.append(f"  {self.R(i.reg_name(mem.mem.base))} = t2;")
            return
        if iid == A.ARM_INS_MOVS: iid = A.ARM_INS_MOV; S = True
        if iid in (A.ARM_INS_MOV, A.ARM_INS_MVN):
            dst = i.reg_name(ops[0].reg)
            if len(ops) == 2 and ops[1].type == A.ARM_OP_REG and i.reg_name(ops[1].reg) == "lr" and dst == "pc":
                o.append("  " + self.ret()); return
            v, car = self.op2(ops[1], S)
            if iid == A.ARM_INS_MVN: v = f"~({v})"
            if dst == "pc": o.append(f"  t = {v}; JUMP(t);"); return
            if S:
                o.append(f"  t = {v};"); self.W(dst, "t")
                o.append(f"  c->n = t >> 31; c->z = t == 0;" + (f" c->cf = {car};" if car else ""))
            else: self.W(dst, v)
            return
        if iid == A.ARM_INS_MOVT:
            dst = i.reg_name(ops[0].reg)
            o.append(f"  {self.R(dst)} = ({self.R(dst)} & 0xffffu) | (0x{ops[1].imm & 0xffff:x}u << 16);"); return
        if iid == A.ARM_INS_ADR:
            dst = i.reg_name(ops[0].reg)
            self.W(dst, f"(BASE + 0x{((i.address + (4 if self.thumb else 8)) & ~3) + ops[1].imm:x}u)" if self.thumb
                   else f"(BASE + 0x{i.address + 8 + ops[1].imm:x}u)"); return
        if iid in (A.ARM_INS_ADD, A.ARM_INS_ADC, A.ARM_INS_SUB, A.ARM_INS_SBC, A.ARM_INS_RSB, A.ARM_INS_RSC,
                   A.ARM_INS_ADDW, A.ARM_INS_SUBW):
            dst = i.reg_name(ops[0].reg)
            if len(ops) == 2: a_, b_ = self.R(dst), self.op2(ops[1])[0]
            else:
                a_, b_ = self.R(i.reg_name(ops[1].reg)), self.op2(ops[2])[0]
                if i.reg_name(ops[1].reg) == "pc" and ops[2].type == A.ARM_OP_IMM:   # ADR forms use Align(PC,4)
                    a_ = f"(BASE + 0x{(i.address + (4 if self.thumb else 8)) & ~3:x}u)"
            if iid == A.ARM_INS_ADR: pass
            if iid in (A.ARM_INS_RSB, A.ARM_INS_RSC): a_, b_ = b_, a_
            if iid in (A.ARM_INS_ADD, A.ARM_INS_ADDW): ex = (a_, b_, "0")
            elif iid == A.ARM_INS_ADC: ex = (a_, b_, "c->cf")
            elif iid in (A.ARM_INS_SUB, A.ARM_INS_SUBW, A.ARM_INS_RSB): ex = (a_, f"~({b_})", "1")
            else: ex = (a_, f"~({b_})", "c->cf")
            if dst == "pc":
                if S: raise Unsupported("flags to pc")
                o.append(f"  t = {ex[0]} + {ex[1]} + {ex[2]};")
                self.computed_jump("t"); return
            if S: o.append(f"  t = addc(c, {ex[0]}, {ex[1]}, {ex[2]});")
            else: o.append(f"  t = {ex[0]} + {ex[1]} + {ex[2]};")
            self.W(dst, "t"); return
        if iid in (A.ARM_INS_CMP, A.ARM_INS_CMN):
            a_ = self.R(i.reg_name(ops[0].reg)); b_ = self.op2(ops[1])[0]
            if iid == A.ARM_INS_CMP: o.append(f"  addc(c, {a_}, ~({b_}), 1);")
            else: o.append(f"  addc(c, {a_}, {b_}, 0);")
            return
        if iid in (A.ARM_INS_AND, A.ARM_INS_ORR, A.ARM_INS_EOR, A.ARM_INS_BIC, A.ARM_INS_ORN,
                   A.ARM_INS_TST, A.ARM_INS_TEQ):
            test = iid in (A.ARM_INS_TST, A.ARM_INS_TEQ)
            if test: dst = None; a_ = self.R(i.reg_name(ops[0].reg)); b_, car = self.op2(ops[1], True)
            else:
                dst = i.reg_name(ops[0].reg)
                if len(ops) == 2: a_, (b_, car) = self.R(dst), self.op2(ops[1], S)
                else: a_, (b_, car) = self.R(i.reg_name(ops[1].reg)), self.op2(ops[2], S)
            opx = {A.ARM_INS_AND: f"{a_} & {b_}", A.ARM_INS_TST: f"{a_} & {b_}", A.ARM_INS_ORR: f"{a_} | {b_}",
                   A.ARM_INS_EOR: f"{a_} ^ {b_}", A.ARM_INS_TEQ: f"{a_} ^ {b_}", A.ARM_INS_BIC: f"{a_} & ~({b_})",
                   A.ARM_INS_ORN: f"{a_} | ~({b_})"}[iid]
            o.append(f"  t = {opx};")
            if dst: self.W(dst, "t")
            if S or test: o.append(f"  c->n = t >> 31; c->z = t == 0;" + (f" c->cf = {car};" if car else ""))
            return
        if iid in (A.ARM_INS_LSL, A.ARM_INS_LSR, A.ARM_INS_ASR, A.ARM_INS_ROR, A.ARM_INS_RRX):
            dst = i.reg_name(ops[0].reg)
            kind = {A.ARM_INS_LSL: 0, A.ARM_INS_LSR: 1, A.ARM_INS_ASR: 2, A.ARM_INS_ROR: 3, A.ARM_INS_RRX: 4}[iid]
            if len(ops) == 2 and ops[1].type == A.ARM_OP_REG and ops[1].shift.type:   # ARM "mov rd, rm, <shift>" alias
                v, car = self.shifted(ops[1], S)
                if S:
                    o.append(f"  sc = c->cf; t = {v};"); self.W(dst, "t")
                    o.append("  c->n = t >> 31; c->z = t == 0; c->cf = sc;")
                else: self.W(dst, v)
                return
            if iid == A.ARM_INS_RRX: src, amt = self.R(i.reg_name(ops[1].reg)), "1"
            elif len(ops) == 2: src = self.R(dst); amt = self.amt(ops[1])
            else: src = self.R(i.reg_name(ops[1].reg)); amt = self.amt(ops[2])
            if S:
                o.append(f"  sc = c->cf; t = shc(c, {src}, {kind}, {amt}, &sc);"); self.W(dst, "t")
                o.append("  c->n = t >> 31; c->z = t == 0; c->cf = sc;")
            else:
                self.W(dst, f"sh({src}, {kind}, {amt}, c->cf)")
            return
        if iid in (A.ARM_INS_MUL, A.ARM_INS_MLA, A.ARM_INS_MLS):
            dst = i.reg_name(ops[0].reg); a_ = self.R(i.reg_name(ops[1].reg))
            b_ = self.R(i.reg_name(ops[2].reg)) if len(ops) > 2 else self.R(dst)
            if len(ops) == 2: a_, b_ = self.R(dst), self.R(i.reg_name(ops[1].reg))
            ex = f"{a_} * {b_}"
            if iid == A.ARM_INS_MLA: ex = f"{ex} + {self.R(i.reg_name(ops[3].reg))}"
            if iid == A.ARM_INS_MLS: ex = f"{self.R(i.reg_name(ops[3].reg))} - {ex}"
            o.append(f"  t = {ex};"); self.W(dst, "t")
            if S: o.append("  c->n = t >> 31; c->z = t == 0;")
            return
        if iid in (A.ARM_INS_UMULL, A.ARM_INS_SMULL, A.ARM_INS_UMLAL, A.ARM_INS_SMLAL):
            lo, hi, a_, b_ = [self.R(i.reg_name(x.reg)) for x in ops[:4]]
            if iid in (A.ARM_INS_UMULL, A.ARM_INS_UMLAL): ex = f"(uint64_t){a_} * (uint64_t){b_}"
            else: ex = f"(uint64_t)((int64_t)(int32_t){a_} * (int64_t)(int32_t){b_})"
            if iid in (A.ARM_INS_UMLAL, A.ARM_INS_SMLAL): ex = f"{ex} + (((uint64_t){hi} << 32) | {lo})"
            o.append(f"  u64 = {ex}; {lo} = (uint32_t)u64; {hi} = (uint32_t)(u64 >> 32);")
            if S: o.append(f"  c->n = {hi} >> 31; c->z = u64 == 0;")
            return
        if iid in (A.ARM_INS_SMMUL, A.ARM_INS_SMMLA, A.ARM_INS_SMMLS, A.ARM_INS_SMMULR, A.ARM_INS_SMMLAR, A.ARM_INS_SMMLSR):
            d, a_, b_ = [self.R(i.reg_name(x.reg)) for x in ops[:3]]
            p = f"((int64_t)(int32_t){a_} * (int32_t){b_})"
            acc = f"((int64_t)(int32_t){self.R(i.reg_name(ops[3].reg))} << 32)" if len(ops) > 3 else "0"
            rnd = "0x80000000LL" if iid in (A.ARM_INS_SMMULR, A.ARM_INS_SMMLAR, A.ARM_INS_SMMLSR) else "0"
            ex = {A.ARM_INS_SMMUL: f"{p}", A.ARM_INS_SMMULR: f"{p}", A.ARM_INS_SMMLA: f"{acc} + {p}",
                  A.ARM_INS_SMMLAR: f"{acc} + {p}", A.ARM_INS_SMMLS: f"{acc} - {p}", A.ARM_INS_SMMLSR: f"{acc} - {p}"}[iid]
            o.append(f"  {d} = (uint32_t)(((uint64_t)({ex} + {rnd})) >> 32);"); return
        if iid in (A.ARM_INS_SDIV, A.ARM_INS_UDIV):
            d, a_, b_ = [self.R(i.reg_name(x.reg)) for x in ops[:3]]
            if iid == A.ARM_INS_UDIV: o.append(f"  {d} = {b_} ? {a_} / {b_} : 0;")
            else: o.append(f"  {d} = {b_} ? (uint32_t)((int32_t){a_} / (int32_t){b_}) : 0;")
            return
        if iid in (A.ARM_INS_UXTB, A.ARM_INS_UXTH, A.ARM_INS_SXTB, A.ARM_INS_SXTH, A.ARM_INS_UXTAB, A.ARM_INS_UXTAH,
                   A.ARM_INS_SXTAB, A.ARM_INS_SXTAH):
            dst = i.reg_name(ops[0].reg)
            add = iid in (A.ARM_INS_UXTAB, A.ARM_INS_UXTAH, A.ARM_INS_SXTAB, A.ARM_INS_SXTAH)
            src_op = ops[2] if add else ops[1]
            v = self.R(i.reg_name(src_op.reg))
            if src_op.shift.type: v = f"ror32({v}, {src_op.shift.value})"
            ex = {A.ARM_INS_UXTB: f"({v} & 0xffu)", A.ARM_INS_UXTAB: f"({v} & 0xffu)", A.ARM_INS_UXTH: f"({v} & 0xffffu)",
                  A.ARM_INS_UXTAH: f"({v} & 0xffffu)", A.ARM_INS_SXTB: f"(uint32_t)(int8_t){v}", A.ARM_INS_SXTAB: f"(uint32_t)(int8_t){v}",
                  A.ARM_INS_SXTH: f"(uint32_t)(int16_t){v}", A.ARM_INS_SXTAH: f"(uint32_t)(int16_t){v}"}[iid]
            if add: ex = f"{self.R(i.reg_name(ops[1].reg))} + {ex}"
            self.W(dst, ex); return
        if iid in (A.ARM_INS_UBFX, A.ARM_INS_SBFX):
            dst, src = i.reg_name(ops[0].reg), self.R(i.reg_name(ops[1].reg)); lsb, w = ops[2].imm, ops[3].imm
            if iid == A.ARM_INS_UBFX: self.W(dst, f"(({src} >> {lsb}) & 0x{(1 << w) - 1:x}u)")
            else: self.W(dst, f"(uint32_t)(((int32_t)({src} << {32 - lsb - w})) >> {32 - w})")
            return
        if iid in (A.ARM_INS_BFI, A.ARM_INS_BFC):
            dst = i.reg_name(ops[0].reg)
            if iid == A.ARM_INS_BFI: src = self.R(i.reg_name(ops[1].reg)); lsb, w = ops[2].imm, ops[3].imm
            else: src = "0"; lsb, w = ops[1].imm, ops[2].imm
            mask = ((1 << w) - 1) << lsb
            self.W(dst, f"(({self.R(dst)} & 0x{~mask & 0xffffffff:x}u) | (({src} << {lsb}) & 0x{mask:x}u))"); return
        if iid in (A.ARM_INS_CLZ, A.ARM_INS_RBIT, A.ARM_INS_REV, A.ARM_INS_REV16, A.ARM_INS_REVSH):
            dst, src = i.reg_name(ops[0].reg), self.R(i.reg_name(ops[1].reg))
            ex = {A.ARM_INS_CLZ: f"clz32({src})", A.ARM_INS_RBIT: f"rbit32({src})", A.ARM_INS_REV: f"__builtin_bswap32({src})",
                  A.ARM_INS_REV16: f"(((({src}) >> 8) & 0x00ff00ffu) | ((({src}) << 8) & 0xff00ff00u))",
                  A.ARM_INS_REVSH: f"(uint32_t)(int16_t)__builtin_bswap16((uint16_t){src})"}[iid]
            self.W(dst, ex); return
        if iid in (A.ARM_INS_USAT, A.ARM_INS_SSAT):
            dst = i.reg_name(ops[0].reg); bits = ops[1].imm; v, _ = self.op2(ops[2])
            if iid == A.ARM_INS_USAT: self.W(dst, f"usat({v}, {bits})")
            else: self.W(dst, f"ssat({v}, {bits})")
            return
        if iid in (A.ARM_INS_SMULBB, A.ARM_INS_SMULBT, A.ARM_INS_SMULTB, A.ARM_INS_SMULTT,
                   A.ARM_INS_SMLABB, A.ARM_INS_SMLABT, A.ARM_INS_SMLATB, A.ARM_INS_SMLATT):
            d, a_, b_ = [self.R(i.reg_name(x.reg)) for x in ops[:3]]
            m = i.mnemonic[-2:]
            ha = f"(int16_t)({a_} >> 16)" if m[0] == "t" else f"(int16_t){a_}"
            hb = f"(int16_t)({b_} >> 16)" if m[1] == "t" else f"(int16_t){b_}"
            ex = f"(uint32_t)((int32_t){ha} * (int32_t){hb})"
            if len(ops) > 3: ex = f"{ex} + {self.R(i.reg_name(ops[3].reg))}"
            o.append(f"  {d} = {ex};"); return
        if i.mnemonic.startswith("v") or iid in (A.ARM_INS_VMRS, A.ARM_INS_VMSR):
            self.vfp(i); return
        if iid in (A.ARM_INS_UDF, A.ARM_INS_BKPT, A.ARM_INS_SVC):
            o.append(f"  unimpl(c, 0x{i.address:x}u, \"{i.mnemonic}\"); return;"); return
        raise Unsupported("insn")

    def amt(self, op):
        if op.type == A.ARM_OP_IMM: return str(op.imm)
        return f"({self.R(self.ins.reg_name(op.reg))} & 0xff)"

    def computed_jump(self, expr):
        """add pc, ... (ARM jump table): dispatch to the instruction addresses that follow"""
        i = self.ins
        o = self.out
        o.append(f"  t = {expr} - BASE;")
        cases = sorted(a for a in self.fn.insns if a > i.address and a < i.address + 8 + 4 * 256)
        o.append("  switch (t) {")
        for a in cases: o.append(f"    case 0x{a:x}u: goto L_{a:x};")
        o.append(f"    default: call_addr(c, t + BASE); return;\n  }}")
        for a in cases: self.fn.labels.add(a)

    # ------------------------------------------------------------------ VFP
    def vr(self, name):
        if name.startswith("s"): return f"c->s[{int(name[1:])}]"
        if name.startswith("d"): return f"c->d[{int(name[1:])}]"
        if name.startswith("q"): raise Unsupported("neon q")
        return self.R(name)

    def vfp(self, i):
        o = self.out; ops = i.operands; mn = i.mnemonic; base = mn.split(".")[0]
        dt = mn.split(".")[1:] if "." in mn else []
        names = [i.reg_name(x.reg) if x.type == A.ARM_OP_REG else None for x in ops]
        if base in ("vpush", "vpop", "vstmdb", "vldmia", "vldm", "vstmia", "vstm", "vldmdb"):
            if base in ("vpush", "vpop"): regs = names; bn = "sp"; wb = True
            else: bn = names[0]; regs = names[1:]; wb = i.writeback
            sz = 8 if regs[0].startswith("d") else 4
            n = len(regs) * sz
            store = base in ("vpush", "vstmdb", "vstmia", "vstm")
            dec = base in ("vpush", "vstmdb", "vldmdb")
            o.append(f"  t = {self.R(bn)}" + (f" - {n};" if dec else ";"))
            for k, r in enumerate(regs):
                acc = f"c->d[{int(r[1:])}]" if sz == 8 else f"c->s[{int(r[1:])}]"
                fn = "64" if sz == 8 else "F"
                if store: o.append(f"  WV{fn}(t + {k * sz}, {acc});")
                else: o.append(f"  {acc} = RV{fn}(t + {k * sz});")
            if wb: o.append(f"  {self.R(bn)} = " + ("t;" if dec else f"t + {n};"))
            return
        if base in ("vldr", "vstr"):
            r = names[0]; ea, _ = self.addr_of(ops[1])
            sz = "64" if r.startswith("d") else "F"
            if base == "vldr": o.append(f"  {self.vr(r)} = RV{sz}({ea});")
            else: o.append(f"  WV{sz}({ea}, {self.vr(r)});")
            return
        if base == "vmov":
            if len(ops) == 2 and ops[1].type in (A.ARM_OP_FP, A.ARM_OP_IMM):
                v = ops[1].fp if ops[1].type == A.ARM_OP_FP else ops[1].imm
                o.append(f"  {self.vr(names[0])} = {float(v)!r};"); return
            if len(ops) == 2:
                d, s = names
                if d[0] == "r" or d in RN:
                    o.append(f"  {self.R(d)} = fbits({self.vr(s)});")
                elif s[0] == "r" or s in RN:
                    o.append(f"  {self.vr(d)} = fromb({self.R(s)});")
                else: o.append(f"  {self.vr(d)} = {self.vr(s)};")
                return
            if len(ops) == 3:      # vmov r0, r1, d0  /  vmov d0, r0, r1
                if names[2].startswith("d"):
                    d = int(names[2][1:])
                    o.append(f"  u64 = dbits(c->d[{d}]); {self.R(names[0])} = (uint32_t)u64; {self.R(names[1])} = (uint32_t)(u64 >> 32);")
                else:
                    d = int(names[0][1:])
                    o.append(f"  c->d[{d}] = fromd(((uint64_t){self.R(names[2])} << 32) | {self.R(names[1])});")
                return
            if len(ops) == 4:      # vmov r0, r1, s0, s1 / vmov s0, s1, r0, r1
                if names[0].startswith("s"):
                    o.append(f"  {self.vr(names[0])} = fromb({self.R(names[2])}); {self.vr(names[1])} = fromb({self.R(names[3])});")
                else:
                    o.append(f"  {self.R(names[0])} = fbits({self.vr(names[2])}); {self.R(names[1])} = fbits({self.vr(names[3])});")
                return
            raise Unsupported("vmov form")
        if base == "vmrs":
            if names[0] == "apsr_nzcv":
                o.append("  c->n = (c->fpscr >> 31) & 1; c->z = (c->fpscr >> 30) & 1; c->cf = (c->fpscr >> 29) & 1; c->v = (c->fpscr >> 28) & 1;")
            else: o.append(f"  {self.R(names[0])} = c->fpscr;")
            return
        if base == "vmsr": o.append(f"  c->fpscr = {self.R(names[1])};"); return
        if base in ("vcmp", "vcmpe"):
            a_ = self.vr(names[0]); b_ = "0.0" if ops[1].type in (A.ARM_OP_IMM, A.ARM_OP_FP) else self.vr(names[1])
            o.append(f"  fcmp(c, {a_}, {b_});"); return
        if base in ("vadd", "vsub", "vmul", "vdiv", "vnmul"):
            d = self.vr(names[0])
            a_, b_ = (self.vr(names[1]), self.vr(names[2])) if len(ops) == 3 else (d, self.vr(names[1]))
            opx = {"vadd": "+", "vsub": "-", "vmul": "*", "vdiv": "/", "vnmul": "*"}[base]
            ex = f"{a_} {opx} {b_}"
            if base == "vnmul": ex = f"-({ex})"
            o.append(f"  {d} = {'dnan' if d.startswith('c->d') else 'fnan'}({ex});"); return
        if base in ("vmla", "vmls", "vnmla", "vnmls", "vfma", "vfms"):
            d, a_, b_ = self.vr(names[0]), self.vr(names[1]), self.vr(names[2])
            ex = {"vmla": f"{d} + {a_} * {b_}", "vfma": f"{d} + {a_} * {b_}", "vmls": f"{d} - {a_} * {b_}",
                  "vfms": f"{d} - {a_} * {b_}", "vnmla": f"-{d} - {a_} * {b_}", "vnmls": f"-{d} + {a_} * {b_}"}[base]
            o.append(f"  {d} = {'dnan' if d.startswith('c->d') else 'fnan'}({ex});"); return
        if base in ("vneg", "vabs", "vsqrt"):
            d, s = self.vr(names[0]), self.vr(names[1])
            fx = {"vneg": f"-{s}", "vabs": f"({'fabsf' if s.startswith('c->s') else 'fabs'})({s})",
                  "vsqrt": (f"fnan(sqrtf({s}))" if s.startswith('c->s') else f"dnan(sqrt({s}))")}[base]
            o.append(f"  {d} = {fx};"); return
        if base in ("vcvt", "vcvtr"):
            d, s = names[0], names[1]
            if len(ops) > 2: raise Unsupported("vcvt fixed")
            to, frm = dt[0], dt[1]
            dv = self.vr(d); sv = self.vr(s)
            if to in ("f32", "f64") and frm in ("f32", "f64"): o.append(f"  {dv} = {sv};")
            elif to in ("f32", "f64"):          # from int (bits stored in an s register)
                cast = "(int32_t)" if frm == "s32" else "(uint32_t)"
                o.append(f"  {dv} = ({'float' if to == 'f32' else 'double'}){cast}fbits({sv});")
            else:                                # to int, round toward zero (vcvtr: nearest)
                fnc = ("f2s" if to == "s32" else "f2u") + ("r" if base == "vcvtr" else "")
                o.append(f"  {dv} = fromb({fnc}({sv}));")
            return
        raise Unsupported("vfp " + base)

# ----------------------------------------------------------------------------- main
unsupported = collections.Counter()
imports = set()
fns = {}
roots = dict(all_entries)
for it in range(6):
    fns = {}; all_entries.clear(); all_entries.update(roots); work.clear(); work.extend(roots)
    while work:
        a = work.popleft(); th = all_entries[a]
        try:
            fns[a] = explore(a, th)
        except Unsupported as e:
            print("explore fail", hex(a), e); fns[a] = None
    nr = {a for a, f in fns.items() if f is not None and not f.returns}
    print(f"pass {it}: {len(fns)} functions, {len(nr)} never return")
    if nr <= NORETURN_FOUND: break
    NORETURN_FOUND |= nr; _dcache.clear() if False else None

os.makedirs(OUT, exist_ok=True)
order = sorted(a for a in fns if fns[a] is not None)
chunks = [order[k::FILES] for k in range(FILES)]
for k, chunk in enumerate(chunks):
    with open(os.path.join(OUT, f"f{k:02d}.c"), "w") as fo:
        fo.write('#include "guest.h"\n#include "decls.h"\n\n')
        for a in chunk:
            fo.write(Gen(fns[a]).gen()); fo.write("\n\n")

with open(os.path.join(OUT, "decls.h"), "w") as fo:
    for a in order: fo.write(f"void F_{a:08x}(CPU *c);\n")
    for s in sorted(imports): fo.write(f"void imp_{s}(CPU *c);\n")
    for n in sorted(NATIVE): fo.write(f"void nat_{n}(CPU *c);\n")
with open(os.path.join(OUT, "table.c"), "w") as fo:
    fo.write('#include "guest.h"\n#include "decls.h"\n\nconst GuestFn guest_fns[] = {\n')
    for a in order: fo.write(f"  {{0x{a:x}u, F_{a:08x}}},\n")
    fo.write("};\nconst unsigned guest_fn_count = %d;\n" % len(order))
    fo.write("const GuestSym guest_syms[] = {\n")
    for a in order:
        if a in funcs: fo.write(f"  {{0x{a:x}u, \"{funcs[a][0]}\"}},\n")
    fo.write("};\nconst unsigned guest_sym_count = %d;\n" % sum(1 for a in order if a in funcs))
with open(os.path.join(OUT, "imports.txt"), "w") as fo:
    fo.write("\n".join(sorted(imports)) + "\n")
print(f"functions {len(order)}, imports used {len(imports)}")
for k, v in unsupported.most_common(60): print(f"  {v:6d} {k}")
