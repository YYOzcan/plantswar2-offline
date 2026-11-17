#!/usr/bin/env python3
"""Build the one-click release files from pc/build/* (run pc/build.py windows linux macos first).

    python3 pc/scripts/package.py <PlantsWar2.apk> [outdir]

  PlantsWar2.exe              Windows 10/11: single file (game data + SDL2.dll inside), double-click
  PlantsWar2-x86_64.AppImage  Linux x86_64 (glibc 2.17+): chmod +x, double-click
  PlantsWar2.dmg              macOS 11+ (Intel + Apple Silicon): open, drag to Applications

Tools: pc/toolchain holds SDL2 (mingw dll, macOS framework, Linux .so built for glibc 2.17),
appimagetool, rcodesign (ad-hoc signing on Linux) and libdmg-hfsplus; pycdlib comes from the venv.
"""
import os, sys, shutil, subprocess, struct, plistlib

PC = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TC = os.path.join(PC, "toolchain")
APK = os.path.abspath(sys.argv[1])
OUT = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(PC, "dist"))
STAGE = os.path.join(PC, "build", "stage")
VERSION = "1.0.6"
os.makedirs(OUT, exist_ok=True)

def sh(*cmd, **kw):
    print("+", " ".join(cmd)[:160], flush=True)
    subprocess.run(cmd, check=True, **kw)

def fresh(d):
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d); return d

# ------------------------------------------------------------------ Windows
def windows():
    sh(sys.executable, os.path.join(PC, "scripts", "selfzip.py"), os.path.join(PC, "build/windows/PlantsWar2.exe"), APK,
       os.path.join(OUT, "PlantsWar2.exe"), os.path.join(TC, "SDL2-2.32.10/x86_64-w64-mingw32/bin/SDL2.dll"))

# ------------------------------------------------------------------ Linux
def linux():
    app = fresh(os.path.join(STAGE, "PlantsWar2.AppDir"))
    b = os.path.join(app, "usr/bin"); os.makedirs(os.path.join(b, "lib"))
    shutil.copy2(os.path.join(PC, "build/linux/PlantsWar2"), b)
    shutil.copy2(APK, os.path.join(b, "PlantsWar2.apk"))
    shutil.copy2(os.path.join(TC, "sdl-build/libSDL2-2.0.so.0"), os.path.join(b, "lib/libSDL2-2.0.so.0"))
    with open(os.path.join(app, "AppRun"), "w") as f:
        f.write('#!/bin/sh\nHERE="$(dirname "$(readlink -f "$0")")"\nexec "$HERE/usr/bin/PlantsWar2" "$@"\n')
    os.chmod(os.path.join(app, "AppRun"), 0o755)
    with open(os.path.join(app, "plantswar2.desktop"), "w") as f:
        f.write("[Desktop Entry]\nType=Application\nName=Plants War 2\nComment=Offline PC port\nExec=PlantsWar2\n"
                "Icon=plantswar2\nCategories=Game;\nTerminal=false\n")
    shutil.copy2(os.path.join(PC, "res/pw2.png"), os.path.join(app, "plantswar2.png"))
    out = os.path.join(OUT, "PlantsWar2-x86_64.AppImage")
    env = dict(os.environ, ARCH="x86_64", APPIMAGE_EXTRACT_AND_RUN="1")
    sh(os.path.join(TC, "appimagetool"), "--no-appstream", app, out, env=env)

# ------------------------------------------------------------------ macOS
def icns(png_by_size, path):
    types = {128: b"ic07", 256: b"ic08", 32: b"ic11", 64: b"ic12"}
    body = b"".join(types[z] + struct.pack(">I", 8 + len(d)) + d for z, d in png_by_size.items())
    open(path, "wb").write(b"icns" + struct.pack(">I", 8 + len(body)) + body)

def macos():
    root = fresh(os.path.join(STAGE, "dmg"))
    app = os.path.join(root, "Plants War 2.app", "Contents")
    for d in ("MacOS", "Resources", "Frameworks"): os.makedirs(os.path.join(app, d))
    shutil.copy2(os.path.join(PC, "build/macos/PlantsWar2"), os.path.join(app, "MacOS/PlantsWar2"))
    os.chmod(os.path.join(app, "MacOS/PlantsWar2"), 0o755)
    shutil.copy2(APK, os.path.join(app, "Resources/PlantsWar2.apk"))
    shutil.copy2(os.path.join(TC, "dmg/SDL2/SDL2.framework/Versions/A/SDL2"), os.path.join(app, "Frameworks/libSDL2-2.0.0.dylib"))
    pngs = {}
    for z in (32, 64, 128, 256):
        p = os.path.join(STAGE, f"icon{z}.png")
        sh("magick", os.path.join(PC, "res/pw2.png"), "-resize", f"{z}x{z}", "PNG32:" + p)
        pngs[z] = open(p, "rb").read()
    icns(pngs, os.path.join(app, "Resources/PlantsWar2.icns"))
    plistlib.dump({
        "CFBundleName": "Plants War 2", "CFBundleDisplayName": "Plants War 2", "CFBundleExecutable": "PlantsWar2",
        "CFBundleIdentifier": "io.github.yyozcan.plantswar2", "CFBundleVersion": VERSION, "CFBundleShortVersionString": VERSION,
        "CFBundlePackageType": "APPL", "CFBundleIconFile": "PlantsWar2", "LSMinimumSystemVersion": "11.0",
        "NSHighResolutionCapable": True, "LSApplicationCategoryType": "public.app-category.games",
    }, open(os.path.join(app, "Info.plist"), "wb"))
    sh(os.path.join(TC, "rcs/apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign"), "sign",
       os.path.join(root, "Plants War 2.app"))
    # ISO 9660 + Rock Ridge (keeps the executable bit and the Applications link), wrapped as a compressed UDIF .dmg
    import pycdlib
    iso = pycdlib.PyCdlib(); iso.new(interchange_level=4, rock_ridge="1.09", joliet=3, vol_ident="Plants War 2")
    n = [0]
    def name9660(isdir):
        n[0] += 1; return f"/F{n[0]:06d}" if isdir else f"/F{n[0]:06d}.;1"
    def add(src, iso_parent, rr_parent):
        for e in sorted(os.listdir(src)):
            p = os.path.join(src, e)
            iso_path = (iso_parent.rstrip("/") + name9660(os.path.isdir(p)))
            jol = (rr_parent.rstrip("/") + "/" + e)
            if os.path.isdir(p):
                iso.add_directory(iso_path, rr_name=e, joliet_path=jol, file_mode=0o40755)
                add(p, iso_path, jol)
            else:
                iso.add_file(p, iso_path, rr_name=e, joliet_path=jol, file_mode=0o100755 if os.access(p, os.X_OK) else 0o100644)
    add(root, "/", "/")
    iso.add_symlink("/APPS.;1", rr_symlink_name="Applications", rr_path="/Applications")
    raw = os.path.join(STAGE, "PlantsWar2.iso")
    iso.write(raw); iso.close()
    out = os.path.join(OUT, "PlantsWar2.dmg")
    if os.path.exists(out): os.remove(out)
    sh(os.path.join(TC, "libdmg-hfsplus/build/dmg/dmg"), "dmg", raw, out)

if __name__ == "__main__":
    if not os.path.exists(os.path.join(PC, "res", "pw2.png")):
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import icon; icon.make(APK)
    for step in (sys.argv[3:] or ["windows", "linux", "macos"]):
        globals()[step]()
    print("\n".join(os.path.join(OUT, f) for f in sorted(os.listdir(OUT))))
