@ Replacement for the miss branch of StringManager::GetString(std::string) in libGame.so (armeabi-v7a).
@ Original: result = "Can not find text id : " + id   (shown on screen as an error)
@ New:      result = id with '_' -> ' ' and first letter upper-cased  (e.g. "superman_title" -> "Superman title")
@           and  __android_log_print(WARN, "PW2Text", "%s", id)  so missing ids can be found and added.
@ The section mirrors the library's virtual address space (offset == vaddr) so branches resolve here.
@ Registers at entry (0x10ab58): r4 = result std::string*, r7 = key std::string*, r5/sp frame of GetString.
@ STLport std::string: +0x10 = _M_finish, +0x14 = _M_start (both point at the object for an empty SSO string).
    .syntax unified
    .arch armv7-a
    .eabi_attribute Tag_CPU_arch, 10
    .eabi_attribute Tag_THUMB_ISA_use, 2
    .text
    .org 0x80a98
    .arm
.Lmemcpy_plt:
    nop
    .org 0x80f6c
.Llog_plt:
    nop
    .thumb
    .org 0x846b0
.Lreserve:                      @ std::string::reserve-like helper used by GetString (thumb)
    nop
    .org 0x10ab48
.Lexit:                         @ common epilogue of GetString (stack guard check + return result)
    nop
    .org 0x10ab58
.Lmiss:
    ldr r6, [r7, #0x14]         @ key begin
    ldr.w r8, [r7, #0x10]       @ key end
    movs r0, #5                 @ ANDROID_LOG_WARN
    adr r1, .Ltag
    adr r2, .Lfmt
    mov r3, r6
    blx .Llog_plt
    str r4, [r4, #0x10]
    str r4, [r4, #0x14]
    sub.w r9, r8, r6
    mov r0, r4
    add.w r1, r9, #1
    bl .Lreserve
    ldr r0, [r4, #0x14]
    mov r1, r6
    mov r2, r9
    blx .Lmemcpy_plt
    add r0, r9
    str r0, [r4, #0x10]
    movs r2, #0
    strb r2, [r0]
    ldr r1, [r4, #0x14]         @ r1 = p, r0 = end
    cmp r1, r0
    beq .Ldone
    ldrb r2, [r1]
    sub.w r3, r2, #0x61
    cmp r3, #25
    bhi .Lloop
    subs r2, #0x20
    strb r2, [r1]
.Lloop:
    cmp r1, r0
    bhs .Ldone
    ldrb r2, [r1]
    cmp r2, #0x5f
    bne .Lnext
    movs r2, #0x20
    strb r2, [r1]
.Lnext:
    adds r1, #1
    b .Lloop
.Ldone:
    b.w .Lexit
    .align 2
.Ltag:
    .asciz "PW2Text"
    .align 2
.Lfmt:
    .asciz "%s"
    .align 2
.Lend:                          @ must stay below 0x10abd2 (checked by scripts/patch_lib.py)
