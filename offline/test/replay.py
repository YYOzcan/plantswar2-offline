"""Differential test: replay recorded client requests through the Python server and the Java port."""
import sys, os, re, json, subprocess, shutil, urllib.parse
import os as _os; ROOT = _os.path.dirname(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))))
sys.path.insert(0, ROOT + "/server")
OUT = sys.argv[1]
reqs = []
lines = open(ROOT + "/server/traffic.log", encoding="utf-8", errors="replace").read().splitlines()
for i, l in enumerate(lines):
    if l.startswith("[12380]") and ("action.php" in l or "login.php" in l) and i + 1 < len(lines) and lines[i+1].startswith("  PD: "):
        reqs.append(lines[i+1][6:])
reqs += sys.argv[3:]
for d in ("py", "java"):
    shutil.rmtree(f"{OUT}/{d}", ignore_errors=True); os.makedirs(f"{OUT}/{d}")
    if sys.argv[2] != "-": shutil.copy(sys.argv[2], f"{OUT}/{d}/save.json")
open(f"{OUT}/reqs.txt", "w", encoding="utf-8").write("\n".join(reqs) + "\n")

import state, handlers
state.PATH = f"{OUT}/py/save.json"; state._db = None
handlers._strip = lambda: set()
def parse_actions(plain):
    acts_part, glob_part = plain.split("&&&", 1) if "&&&" in plain else (plain, "")
    acts = [dict(urllib.parse.parse_qsl(c, keep_blank_values=True)) for c in acts_part.split("&&") if c]
    return acts, dict(urllib.parse.parse_qsl(glob_part, keep_blank_values=True))
py = []
for p in reqs:
    acts, g = parse_actions(p); parts = []
    for a in acts:
        try: x = handlers.dispatch(a, g)
        except Exception: x = None
        parts.append(f'<act v="{a.get("act")}">{x or ""}</act>')
    py.append("<result><msg>1</msg>" + "".join(parts) + "</result>")

J = "java"
jv = subprocess.run([J, "-cp", f"{ROOT}/offline/classes:{OUT}/tcls", "pw2.offline.Replay", f"{OUT}/reqs.txt", f"{OUT}/java"],
                    capture_output=True, text=True, encoding="utf-8").stdout.splitlines()
jv = [l for l in jv if l.startswith("<result>")]
T = re.compile(r'((?:r_t|l_t|time|log_idx|idx_time)="?)>?\d+|\b1[7-9]\d{8}\b')
norm = lambda s: T.sub(lambda m: (m.group(1) or "") + "T", s)
bad = 0
print(len(reqs), "requests,", len(py), "py,", len(jv), "java")
for i, (a, b) in enumerate(zip(py, jv)):
    if norm(a) != norm(b):
        bad += 1
        if bad <= 8: print("DIFF", i, reqs[i][:200], "\n  py:", a[:600], "\n  jv:", b[:600])
def nsave(p):
    s = json.dumps(json.load(open(p)), sort_keys=True)
    return re.sub(r'"(t|last_login|idx_time|created|luckybox|s_partsbox|s_armorbox|s_unitbox)": \d+', r'"\1": T', s)
sp, sj = nsave(f"{OUT}/py/save.json"), nsave(f"{OUT}/java/save.json")
print("response diffs:", bad, "| saves equal:", sp == sj)
if sp != sj:
    import difflib
    for l in list(difflib.unified_diff(json.dumps(json.loads(sp), indent=1).splitlines(), json.dumps(json.loads(sj), indent=1).splitlines(), lineterm=""))[:60]: print(l)
