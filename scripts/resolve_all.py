import sys, re
sys.path.insert(0, '/home/yigit/pw2/scripts')
import res
src = open(sys.argv[1], encoding='latin1').read()
def ok(r): return r and all(32 <= ord(c) < 127 for c in r)
cache = {}
def R(dat, k):
    key = (dat, k)
    if key not in cache:
        r = res.resolve(f'{dat} + {k}')
        cache[key] = repr(r) if ok(r) else None
    return cache[key]
ASSIGN = re.compile(r'\b(\w+) = (DAT_[0-9a-f]+)\s*[;,)]')
def fix(fn):
    vars_ = {}
    out = []
    for line in fn.split('\n'):
        line = re.sub(r'DAT_[0-9a-f]+ \+ 0x[0-9a-f]+', lambda m: R(*m.group(0).split(' + ')) or m.group(0), line)
        def sub2(m):
            v, k = m.group(1), m.group(2)
            if v in vars_:
                r = R(vars_[v], k)
                if r: return r
            return m.group(0)
        line = re.sub(r'\b(\w+) \+ (0x[0-9a-f]{5,6})\b', sub2, line)
        for v, d in ASSIGN.findall(line): vars_[v] = d
        out.append(line)
    # second pass: assignments that appear later than use (loops) -> fallback with first-seen
    return '\n'.join(out)
parts = re.split(r'(?m)(?=^// ==== )', src)
open(sys.argv[2], 'w').write(''.join(fix(p) for p in parts))
