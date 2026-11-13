import sys, re
txt = open('/home/yigit/pw2/nm_res.c').read().split('\n')
want = [int(x, 0) for x in sys.argv[1:]]
# find "case N:" lines at the switch indentation (8 spaces)
idx = {}
for i, l in enumerate(txt):
    m = re.match(r'^        case (0x[0-9a-f]+|\d+):', l)
    if m: idx.setdefault(int(m.group(1), 0), i)
starts = sorted(idx.values())
noise = re.compile(r"FUN_000aa90c|_M_deallocate|operator_delete|__stack_chk|^\s*$")
for w in want:
    if w not in idx: print(f'== case {w}: not found'); continue
    s = idx[w]; e = next((x for x in starts if x > s), len(txt))
    # include fallthrough labels
    print(f'== case {w} ({hex(w)})')
    for l in txt[s:e]:
        if noise.search(l) or re.match(r'^\s*(if \(\(local_|if \(\(uint\)\(local)', l): continue
        print(l)
