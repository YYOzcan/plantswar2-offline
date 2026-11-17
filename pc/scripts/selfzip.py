#!/usr/bin/env python3
"""selfzip.py <exe> <apk> <out> [extra files...]
Append the APK (plus extra files added as zip entries) to an executable and fix the zip offsets,
so the output runs as a program AND is a valid zip/APK (like `zip -A` for self-extracting archives)."""
import sys, os, shutil, struct, zipfile, tempfile
exe, apk, out, extras = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
tmp = tempfile.mktemp(suffix=".zip", dir=os.path.dirname(os.path.abspath(out)))
shutil.copyfile(apk, tmp)
with zipfile.ZipFile(tmp, "a", zipfile.ZIP_DEFLATED) as z:
    for e in extras: z.write(e, os.path.basename(e))
data = bytearray(open(tmp, "rb").read()); os.remove(tmp)
pre = open(exe, "rb").read(); shift = len(pre)
e = data.rfind(b"PK\x05\x06")
cnt, cdsize, cdoff = struct.unpack_from("<HII", data, e + 10)
p = cdoff
for _ in range(cnt):
    assert data[p:p + 4] == b"PK\x01\x02"
    lo, = struct.unpack_from("<I", data, p + 42)
    struct.pack_into("<I", data, p + 42, lo + shift)
    nl, el, cl = struct.unpack_from("<HHH", data, p + 28)
    p += 46 + nl + el + cl
struct.pack_into("<I", data, e + 16, cdoff + shift)
with open(out, "wb") as f: f.write(pre); f.write(data)
os.chmod(out, 0o755)
print(f"{out}: {len(pre) + len(data)} bytes ({cnt} zip entries)")
