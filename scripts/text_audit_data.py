"""Data-table attributes that hold text ids: report values with no string-table entry."""
import sys, os, re, glob
sys.path.insert(0, os.path.expanduser('~/pw2/server')); import pw2crypto
R = os.path.expanduser('~/pw2/apk_smali/assets/Common/')
def dec(p):
    x, ok = pw2crypto.decode(open(p, 'rb').read()); return x[4:].decode('utf-8', 'replace')
have = set()
for p in glob.glob(R + 'String/*.x'): have |= set(re.findall(r'<string\s+id\s*=\s*"([^"]+)"', dec(p)))
for p in sorted(glob.glob(R + '**/*.x', recursive=True)):
    if '/String/' in p: continue
    try: x = dec(p)
    except Exception: continue
    vals = {}
    for tag, body in re.findall(r'<(\w+)\s([^<>]*?)/?>', x):
        for k, v in re.findall(r'(\w+)\s*=\s*"([^"]*)"', body):
            vals.setdefault((tag, k), []).append(v)
    for (tag, k), vs in vals.items():
        u = set(vs); hit = [v for v in u if v in have]
        if len(hit) >= 2 and len(hit) >= 0.3 * len(u):
            miss = sorted(v for v in u if v not in have and v not in ('', '0', 'none', 'null'))
            if miss: print(f'{os.path.relpath(p, R)} <{tag} {k}>: {len(hit)}/{len(u)} ok, missing {len(miss)}: {" ".join(miss[:40])}')
