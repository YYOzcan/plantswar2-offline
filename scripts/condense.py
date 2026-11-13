import sys, re
txt = sys.stdin.read()
keep = re.compile(r"FindElem|GetAttrib|x_GetData|case |switch|IntoElem|OutOfElem|::Load|::Make|::Join|::Login|::Receive|::Set|goto|^LAB_|^\s*if \(|while|else|AddParam|'")
skip = re.compile(r"FUN_000aa90c|_M_deallocate|operator_delete|__stack_chk|piVar\d+ = \*\(int \*\*\)")
for l in txt.splitlines():
    s = l.rstrip()
    if not s.strip(): continue
    if skip.search(s): continue
    if keep.search(s) or s.strip() in ('{', '}', 'break;'):
        print(s)
