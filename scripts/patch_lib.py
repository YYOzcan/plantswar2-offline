"""patch_lib.py <orig libGame.so> <out libGame.so> [host]

- server URLs -> http://<host>:<port> (default 127.0.0.1: the server built into the APK)
- GLES1 (DT_NEEDED + dlopen) -> libpw2gl.so matrix-tracking shim (glshim/)
- armeabi-v7a: StringManager::GetString miss branch -> readable fallback + logcat (patches/getstring_miss.s)
"""
import sys, os, hashlib, subprocess, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = sys.argv[3] if len(sys.argv) > 3 else "127.0.0.1"
MAP = {
 b"http://udw-v2.four33.co.kr:10030": f"http://{HOST}:10030".encode(),
 b"http://notice.four33.co.kr:10015": f"http://{HOST}:10015".encode(),
 b"http://endingpopup.four33.co.kr:10005": f"http://{HOST}:10005".encode(),
 b"http://vip-qa-pw2-gs.433live.kr:12380": f"http://{HOST}:12380".encode(),
 b"http://dl-g-pw2.four33.com/AUW/1001/_plantswar2_global": f"http://{HOST}:10080/AUW/1001/_plantswar2_global".encode(),
 b"https://cs.433.co.kr/four33/faq/topFaq.jsp": f"http://{HOST}:10015/faq".encode(),
 b"libGLESv1_CM.so": b"libpw2gl.so",
}
d = bytearray(open(sys.argv[1], "rb").read())
for old, new in MAP.items():
    key = old + b"\0"
    n = d.count(key)
    assert n >= 1, old
    assert len(new) <= len(old), new
    i = -1
    for _ in range(n):
        i = d.find(key, i + 1)
        d[i:i + len(key)] = new + b"\0" * (len(key) - len(new))
    print(f"{old.decode()} -> {new.decode()} x{n}")

# GetString miss branch (vaddr == file offset in the text segment of the v7a build)
MISS_START, MISS_END = 0x10ab58, 0x10abd2
ORIG_SHA1 = "e4981957f5cb35f08e934b4840a9c6de2c7a8db6"
if hashlib.sha1(d[MISS_START:MISS_END]).hexdigest() == ORIG_SHA1:
    with tempfile.TemporaryDirectory() as t:
        src = os.path.join(ROOT, "patches", "getstring_miss.s")
        subprocess.run(["llvm-mc", "-triple=thumbv7-linux-androideabi", "-filetype=obj", src, "-o", t + "/m.o"], check=True)
        subprocess.run(["ld.lld", "-m", "armelf", "--image-base=0", "--section-start=.text=0", "-e", "0",
                        "-o", t + "/m.elf", t + "/m.o"], check=True)
        subprocess.run(["llvm-objcopy", "-O", "binary", "-j", ".text", t + "/m.elf", t + "/m.bin"], check=True)
        code = open(t + "/m.bin", "rb").read()[MISS_START:]
    assert MISS_START + len(code) <= MISS_END, "GetString patch too long"
    d[MISS_START:MISS_START + len(code)] = code
    print(f"GetString miss branch patched ({len(code)} bytes)")
elif "armeabi-v7a" in sys.argv[1]:
    sys.exit("unexpected bytes at the GetString miss branch")
open(sys.argv[2], "wb").write(d)
