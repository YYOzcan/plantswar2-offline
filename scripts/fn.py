import sys, re
src = open('/home/yigit/pw2/full_res.c').read()
for p in re.split(r'(?m)^// ==== ', src):
    if p.startswith(sys.argv[1] + ' @'): print('// ==== ' + p)
