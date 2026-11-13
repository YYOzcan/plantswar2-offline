"""List text ids the client may ask StringManager::GetString for that no string table defines."""
import sys, os, re, glob
sys.path.insert(0, os.path.expanduser('~/pw2/server'))
import pw2crypto
ROOT = os.path.expanduser('~/pw2')
def dec(p):
    plain, ok = pw2crypto.decode(open(p, 'rb').read()); return plain[4:].decode('utf-8', 'replace')
have = set()
for p in glob.glob(ROOT + '/apk_smali/assets/Common/String/*.x'):
    have |= set(re.findall(r'<string\s+id\s*=\s*"([^"]+)"', dec(p)))
print('defined ids:', len(have), file=sys.stderr)

# functions in the decompile that call GetString -> literal strings inside them
src = open(ROOT + '/full_res.c', encoding='utf-8', errors='replace').read()
funcs = re.split(r'\n// ==== ', src)
lits = {}
for f in funcs:
    if 'GetString' not in f: continue
    name = f.split(' @', 1)[0]
    for s in re.findall(r"\('([a-z0-9_]{3,80})'\)", f):
        lits.setdefault(s, set()).add(name)
miss = {s: n for s, n in lits.items() if s not in have}
for s in sorted(miss): print(s, '\t', ', '.join(sorted(miss[s]))[:150])
print('candidates:', len(lits), 'missing:', len(miss), file=sys.stderr)
