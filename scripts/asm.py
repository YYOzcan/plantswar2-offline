# usage: asm.py 'Class::Func' [grep]  -> thumb disassembly with PIC string refs resolved
import sys, re, subprocess
sys.path.insert(0, '/home/yigit/pw2/scripts')
import res
LIB = '/home/yigit/pw2/apk/lib/armeabi-v7a/libGame.so'
B = 0x10000
heads = re.findall(r'(?m)^// ==== (.+?) @ ([0-9a-f]+)$', open('/home/yigit/pw2/full.c').read())
addrs = sorted(int(a, 16) for _, a in heads)
name = sys.argv[1]
start = next(int(a, 16) for n, a in heads if n == name) - B
end = next((a - B for a in addrs if a - B > start), start + 0x4000)
out = subprocess.run(['llvm-objdump', '-d', '--triple=thumbv7-linux-androideabi', '--no-show-raw-insn',
                      f'--start-address={start:#x}', f'--stop-address={end:#x}', LIB], capture_output=True, text=True).stdout
regs = {}
def cstr(v):
    try:
        s = res.cstr(v)
        if s and all(32 <= ord(c) < 127 for c in s) and len(s) > 0: return s
    except Exception: pass
for line in out.splitlines():
    m = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', line)
    if not m: continue
    addr, op, args = int(m.group(1), 16), m.group(2), m.group(3)
    note = ''
    mm = re.match(r'(r\d+|lr|r12|ip), \[pc, #(-?(?:0x[0-9a-f]+|\d+))\]', args)
    if op.startswith('ldr') and mm:
        lit = ((addr + 4) & ~3) + int(mm.group(2), 0)
        regs[mm.group(1)] = res.rd(lit)
    else:
        mm = re.match(r'(r\d+|ip), pc$', args)
        if op.startswith('add') and mm and mm.group(1) in regs:
            v = (regs[mm.group(1)] + addr + 4) & 0xffffffff
            s = cstr(v)
            note = f'   ; {mm.group(1)} = {v:#x}' + (f' "{s}"' if s else '')
            regs[mm.group(1)] = v
        else:
            mm = re.match(r'(r\d+), (r\d+|ip), (r\d+|ip)$', args)
            if op.startswith('add') and mm:
                a2, a3 = mm.group(2), mm.group(3)
                # base + offset where one of them is pc-relative computed value
                pass
    print(f'{addr+B:x}: {op} {args}{note}')
