import sys, re
import os as _os; ROOT = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))
src = open(ROOT + '/full_res.c').read()
for p in re.split(r'(?m)^// ==== ', src):
    if p.startswith(sys.argv[1] + ' @'): print('// ==== ' + p)
