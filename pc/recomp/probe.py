import capstone as cs
from capstone import arm
import keystone as ks
kt = ks.Ks(ks.KS_ARCH_ARM, ks.KS_MODE_THUMB); ka = ks.Ks(ks.KS_ARCH_ARM, ks.KS_MODE_ARM)
md = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB); md.detail = True
mda = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_ARM); mda.detail = True
def show(src, thumb=True):
    enc, _ = (kt if thumb else ka).asm(src, 0x1000)
    for i in (md if thumb else mda).disasm(bytes(enc), 0x1000):
        ops = []
        for o in i.operands:
            if o.type == arm.ARM_OP_REG: s = f"REG({i.reg_name(o.reg)})"
            elif o.type == arm.ARM_OP_IMM: s = f"IMM({o.imm})"
            elif o.type == arm.ARM_OP_MEM: s = f"MEM(b={i.reg_name(o.mem.base)},i={i.reg_name(o.mem.index) if o.mem.index else None},sc={o.mem.scale},d={o.mem.disp},lsl={o.mem.lshift})"
            elif o.type == arm.ARM_OP_FP: s = f"FP({o.fp})"
            else: s = f"T{o.type}"
            if o.shift.type: s += f"<sh{o.shift.type}:{o.shift.value}>"
            if o.subtracted: s += "<sub>"
            ops.append(s)
        print(f"{i.mnemonic:8} {i.op_str:28} cc={i.cc} upd={i.update_flags} wb={i.writeback} post={getattr(i,'post_index',None)} id={i.id} ops={ops}")
for s in ["ldr r0, [r1], #4", "ldr r0, [r1, #-8]!", "ldr r0, [r1, r2, lsl #2]", "str r0, [sp, #-4]!", "ldrd r0, r1, [r2, #8]",
          "it eq\nmoveq r0, #1", "ite ne\nmovne r0, r1\nmoveq r0, r2", "adds r0, r1, r2", "add.w r0, r1, r2, lsl #3", "subs r0, #1",
          "push {r4, r5, lr}", "pop {r4, pc}", "ldm r0!, {r1, r2}", "tbb [pc, r0]", "tbh [pc, r1, lsl #1]", "cbz r0, #0x1010",
          "vldr s0, [r0, #4]", "vadd.f32 s0, s1, s2", "vmov r0, s0", "vmov s0, r0", "vmov r0, r1, d0", "vcvt.s32.f32 s0, s1",
          "vmrs APSR_nzcv, fpscr", "vcmpe.f32 s0, #0", "vpush {d8, d9}", "mov.w r0, #0x10001", "ldr r0, [pc, #8]", "add r0, pc",
          "ubfx r0, r1, #3, #5", "bfi r0, r1, #2, #4", "mla r0, r1, r2, r3", "smull r0, r1, r2, r3", "uxtb r0, r1", "rsb r0, r1, #0",
          "lsls r0, r1, #2", "lsls r0, r1", "asrs r0, r1, #32", "orr r0, r1, #0xff00", "movw r0, #0x1234", "movt r0, #0x1234"]:
    show(s)
print("ARM:")
for s in ["ldrne r0, [r1], #4", "ldr pc, [pc, r0, lsl #2]", "addeq r0, r1, r2, lsl r3", "stmdb sp!, {r4, lr}", "ldmia sp!, {r4, pc}", "bx lr"]:
    show(s, False)
