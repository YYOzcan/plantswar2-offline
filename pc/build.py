#!/usr/bin/env python3
"""Build the native PC port of Plants War 2.

    python3 pc/build.py [target ...]        targets: native (default), linux, windows, macos-x64, macos-arm64, macos

  native     host clang, for development on this machine
  linux      x86_64 Linux, glibc >= 2.17 (any distro from the last ~10 years)    \\
  windows    x86_64 Windows 10/11                                                  > zig toolchain (cross)
  macos-*    macOS 11+, Intel / Apple Silicon; "macos" = universal (lipo'd)        /

The ARM library is recompiled to C once (pc/gen, needs the Python venv with capstone); every target
then compiles the same generated C + runtime. SDL2 and OpenGL are loaded at run time, so nothing
but the C/C++ runtime is linked.
Environment: LIB=<libGame.so> (default: the patched one in apk_smali), APK=<game apk, for the icon>,
ZIG=<zig binary>, JOBS=<n>.
"""
import os, sys, subprocess, shutil, concurrent.futures as cf

PC = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(PC)
LIB = os.environ.get("LIB", os.path.join(ROOT, "apk_smali/lib/armeabi-v7a/libGame.so"))
GEN = os.path.join(PC, "gen")
JOBS = int(os.environ.get("JOBS", os.cpu_count() or 4))
VENV_PY = os.path.join(ROOT, ".venv-pc", "bin", "python")
PY = VENV_PY if os.path.exists(VENV_PY) else sys.executable

def find_zig():
    z = os.environ.get("ZIG") or shutil.which("zig")
    if z: return z
    tc = os.path.join(PC, "toolchain")
    for d in sorted(os.listdir(tc)) if os.path.isdir(tc) else []:
        p = os.path.join(tc, d, "zig")
        if d.startswith("zig-") and os.path.exists(p): return p
    sys.exit("zig not found: run pc/scripts/get-toolchain.sh or set ZIG=/path/to/zig")

TARGETS = {
    "native":      dict(zig=None, exe="PlantsWar2"),
    "linux":       dict(zig="x86_64-linux-gnu.2.17", exe="PlantsWar2"),
    "windows":     dict(zig="x86_64-windows-gnu", exe="PlantsWar2.exe"),
    "macos-x64":   dict(zig="x86_64-macos.11.0", exe="PlantsWar2"),
    "macos-arm64": dict(zig="aarch64-macos.11.0", exe="PlantsWar2"),
}

def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.stderr.write(" ".join(cmd)[:300] + "\n" + r.stdout[-4000:] + r.stderr[-6000:])
        raise SystemExit(1)
    return r

def stale(out, *deps):
    if not os.path.exists(out): return True
    t = os.path.getmtime(out)
    return any(os.path.getmtime(d) > t for d in deps if os.path.exists(d))

def recompile():
    os.makedirs(GEN, exist_ok=True)
    stamp = os.path.join(GEN, ".stamp")
    if stale(stamp, LIB, os.path.join(PC, "recomp/recomp.py")):
        print("== recompiling", LIB, flush=True)
        for f in os.listdir(GEN):
            if f.startswith("f") and f.endswith(".c"): os.remove(os.path.join(GEN, f))
        os.makedirs(os.path.join(PC, "build"), exist_ok=True)
        with open(os.path.join(PC, "build/recomp.log"), "w") as log:
            subprocess.run([PY, os.path.join(PC, "recomp/recomp.py"), LIB, GEN], stdout=log, check=True)
        open(stamp, "w").close()
    imp = os.path.join(GEN, "imports_gen.c")
    if stale(imp, LIB, os.path.join(PC, "recomp/gen_imports.py")):
        run([PY, os.path.join(PC, "recomp/gen_imports.py"), LIB, imp])

def build(name):
    t = TARGETS[name]
    out = os.path.join(PC, "build", name); obj = os.path.join(out, "obj"); os.makedirs(obj, exist_ok=True)
    env = dict(os.environ)
    if t["zig"]:
        zig = find_zig()
        env.setdefault("ZIG_GLOBAL_CACHE_DIR", os.path.join(PC, "toolchain", "zig-cache"))
        cc = [zig, "cc", "-target", t["zig"]]; cxx = [zig, "c++", "-target", t["zig"]]
    else:
        cc, cxx = ["clang"], ["clang++"]
    inc = ["-I", os.path.join(PC, "runtime"), "-I", GEN]
    zinc = ["-I", os.path.join(PC, "third_party/zlib")]
    jobs = []
    for f in sorted(os.listdir(GEN)):
        if f.endswith(".c"):
            jobs.append((cc + ["-O2", "-w", "-fno-strict-aliasing", "-DPW2_DEBUG"] + inc, os.path.join(GEN, f), "g_" + f[:-2]))
    zsrc = os.path.join(PC, "third_party/zlib")
    for f in sorted(os.listdir(zsrc)):
        if f.endswith(".c"): jobs.append((cc + ["-O2", "-w"] + zinc, os.path.join(zsrc, f), "z_" + f[:-2]))
    jobs.append((cc + ["-O2", "-w", "-DONLY_MSPACES=1", "-DUSE_LOCKS=1", "-DHAVE_MMAP=0", "-DHAVE_MORECORE=0", "-DMSPACES=1"],
                 os.path.join(PC, "third_party/malloc.c"), "dlmalloc"))
    rt = os.path.join(PC, "runtime")
    hdrs = [os.path.join(rt, h) for h in os.listdir(rt) if h.endswith(".h")]
    for f in sorted(os.listdir(rt)):
        if f.endswith(".cpp"):
            jobs.append((cxx + ["-O2", "-g", "-std=c++17", "-Wall", "-Wno-unused", "-fno-strict-aliasing"] + inc + zinc,
                         os.path.join(rt, f), "rt_" + f[:-4]))
    objs = []
    def one(j):
        flags, src, base = j
        o = os.path.join(obj, base + ".o")
        deps = [src] + (hdrs if base.startswith("rt_") or base.startswith("g_") else [])
        if stale(o, *deps): run(flags + ["-c", src, "-o", o], env=env)
        return o
    print(f"== {name}: compiling {len(jobs)} files", flush=True)
    with cf.ThreadPoolExecutor(JOBS) as ex:
        objs = list(ex.map(one, jobs))
    exe = os.path.join(out, t["exe"])
    link = cxx + ["-o", exe] + objs
    if name == "windows":
        ico, apk = os.path.join(PC, "res", "pw2.ico"), os.environ.get("APK", os.path.join(ROOT, "build", "pw2.apk"))
        if not os.path.exists(ico) and os.path.exists(apk) and shutil.which("magick"):
            sys.path.insert(0, os.path.join(PC, "scripts")); import icon; icon.make(apk)
        if os.path.exists(ico): link += [os.path.join(PC, "res", "app.rc")]
        link += ["-Wl,--subsystem,windows", "-Wl,--stack,67108864", "-lshell32", "-lole32", "-luser32"]
    elif name.startswith("macos"):
        link += ["-Wl,-stack_size,0x4000000", "-Wl,-headerpad,0x4000"]
    else:
        link += ["-ldl", "-lpthread", "-lm"]
        if name == "native": link += ["-rdynamic"]
    print(f"== {name}: linking", flush=True)
    run(link, env=env, cwd=os.path.join(PC, "res"))
    print("built", exe)
    return exe

def main():
    names = sys.argv[1:] or ["native"]
    if "all" in names: names = ["linux", "windows", "macos"]
    recompile()
    for n in names:
        if n == "macos":
            a, b = build("macos-x64"), build("macos-arm64")
            out = os.path.join(PC, "build", "macos"); os.makedirs(out, exist_ok=True)
            lipo = shutil.which("llvm-lipo") or shutil.which("lipo")
            if lipo:
                run([lipo, "-create", a, b, "-output", os.path.join(out, "PlantsWar2")])
                print("built", os.path.join(out, "PlantsWar2"), "(universal)")
            else:
                print("no lipo: keeping separate x64/arm64 binaries")
        elif n in TARGETS:
            build(n)
        else:
            sys.exit(f"unknown target {n}; choose from {', '.join(TARGETS)}, macos, all")

if __name__ == "__main__":
    main()
