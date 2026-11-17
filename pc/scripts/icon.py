#!/usr/bin/env python3
"""icon.py <apk>: take the launcher icon out of the user's APK -> pc/res/pw2.png + pw2.ico (Windows)."""
import os, sys, struct, zipfile, subprocess, tempfile
RES = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "res")

def make(apk):
    png = os.path.join(RES, "pw2.png")
    with zipfile.ZipFile(apk) as z:
        open(png, "wb").write(z.read("res/drawable-xxxhdpi-v4/icon.png"))
    sizes, blobs = [256, 64, 48, 32, 16], []
    with tempfile.TemporaryDirectory() as td:
        for s in sizes:
            p = os.path.join(td, f"{s}.png")
            subprocess.run(["magick", png, "-resize", f"{s}x{s}", "PNG32:" + p], check=True)
            blobs.append(open(p, "rb").read())
    off = 6 + 16 * len(sizes); ent = b""
    for s, b in zip(sizes, blobs):
        ent += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(b), off); off += len(b)
    open(os.path.join(RES, "pw2.ico"), "wb").write(struct.pack("<HHH", 0, 1, len(sizes)) + ent + b"".join(blobs))

if __name__ == "__main__":
    make(sys.argv[1])
