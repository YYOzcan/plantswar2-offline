"""Find literal text ids passed to StringManager::GetString (whole-binary thumb disassembly, PIC strings resolved)."""
import sys, re, subprocess
sys.path.insert(0, '/home/yigit/pw2/scripts')
import res
LIB = '/home/yigit/pw2/apk/lib/armeabi-v7a/libGame.so'
TARGETS = {0x11aaf0 - 0x10000}           # StringManager::GetString(std::string)
out = subprocess.run(['llvm-objdump', '-d', '--triple=thumbv7-linux-androideabi', '--no-show-raw-insn', '-j', '.text', LIB],
                     capture_output=True, text=True).stdout
regs, recent, calls, nocall = {}, [], 0, 0
found = {}
idlike = re.compile(r'^[A-Za-z0-9_]{2,100}$')
for line in out.splitlines():
    m = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', line)
    if not m:
        if line.endswith('>:'): regs.clear(); recent.clear()
        continue
    addr, op, args = int(m.group(1), 16), m.group(2), m.group(3)
    mm = re.match(r'(r\d+|lr|r12|ip), \[pc, #(-?(?:0x[0-9a-f]+|\d+))\]', args)
    if op.startswith('ldr') and mm:
        try: regs[mm.group(1)] = res.rd(((addr + 4) & ~3) + int(mm.group(2), 0))
        except Exception: pass
        continue
    mm = re.match(r'(r\d+|ip), pc$', args)
    if op.startswith('add') and mm and mm.group(1) in regs:
        v = (regs[mm.group(1)] + addr + 4) & 0xffffffff
        try: s = res.cstr(v)
        except Exception: s = None
        if s and idlike.match(s): recent.append((addr, s))
        continue
    if op in ('bl', 'blx'):
        t = re.match(r'0x([0-9a-f]+)|([0-9a-f]+)', args)
        tgt = int(t.group(0).replace('0x', ''), 16) if t else None
        if tgt in TARGETS:
            calls += 1
            cand = [s for a, s in recent if addr - a < 0x80]
            if cand: found.setdefault(cand[-1], set()).add(hex(addr + 0x10000))
            else: nocall += 1
for s in sorted(found): print(s)
print(f'calls={calls} resolved={calls-nocall} ids={len(found)}', file=sys.stderr)
