import sys
HOST = sys.argv[3] if len(sys.argv) > 3 else "10.0.2.2"
MAP = {
 b"http://udw-v2.four33.co.kr:10030": f"http://{HOST}:10030".encode(),
 b"http://notice.four33.co.kr:10015": f"http://{HOST}:10015".encode(),
 b"http://endingpopup.four33.co.kr:10005": f"http://{HOST}:10005".encode(),
 b"http://vip-qa-pw2-gs.433live.kr:12380": f"http://{HOST}:12380".encode(),
 b"http://dl-g-pw2.four33.com/AUW/1001/_plantswar2_global": f"http://{HOST}:10080/AUW/1001/_plantswar2_global".encode(),
 b"https://cs.433.co.kr/four33/faq/topFaq.jsp": f"http://{HOST}:10015/faq".encode(),
}
d = bytearray(open(sys.argv[1], "rb").read())
# route all GLES1 calls (DT_NEEDED + dlopen) through our matrix-tracking shim (glshim/libpw2gl.so)
MAP[b"libGLESv1_CM.so"] = b"libpw2gl.so"
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
open(sys.argv[2], "wb").write(d)
