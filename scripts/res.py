# usage: res.py DAT_addr+offset ...  -> resolves PIC string refs in libGame.so
import struct, sys, re
import os as _os; ROOT = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))
d = open(ROOT + '/apk/lib/armeabi-v7a/libGame.so','rb').read()
phoff = struct.unpack_from('<I', d, 0x1c)[0]; phnum = struct.unpack_from('<H', d, 0x2c)[0]
segs = []
for i in range(phnum):
    t, off, va, pa, fs, ms = struct.unpack_from('<6I', d, phoff + i*32)
    if t == 1: segs.append((va, off, fs))
def v2o(v):
    for va, off, fs in segs:
        if va <= v < va + fs: return v - va + off
def rd(v): return struct.unpack_from('<I', d, v2o(v))[0]
def cstr(v):
    o = v2o(v)
    if o is None: return None
    e = d.index(b'\0', o); return d[o:e].decode('latin1')
B = 0x10000
def resolve(expr):
    m = re.match(r'DAT_([0-9a-f]+)\s*\+\s*0x([0-9a-f]+)', expr)
    a, k = int(m[1], 16) - B, int(m[2], 16) - B
    try: return cstr((rd(a) + k) & 0xffffffff)
    except Exception: return None
if __name__ == '__main__':
    src = sys.stdin.read()
    for m in sorted(set(re.findall(r'DAT_[0-9a-f]+ \+ 0x[0-9a-f]+', src))):
        print(m, '=>', resolve(m))
