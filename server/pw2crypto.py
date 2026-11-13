"""Plants War 2 'Encoder/Decoder' transport cipher (reversed from libGame.so)."""
import base64, os

def _swap(b): return ((b & 0xF) << 4) | (b >> 4)

def _steps():
    s = 0x10
    while True:
        yield s
        s = 0 if s == 0xFD else 1 if s == 0xFE else s + 2

def _cksum_add(c, b):
    if c > 0x7FFFFEFE: c = 0
    return c + b

def encode_raw(plain: bytes, key: bytes = None) -> bytes:
    if key is None:
        key = bytes(os.urandom(1)[0] % 0xF0 + 0x10 for _ in range(8))
    out = bytearray(); c = 0; st = 0
    for i, (p, s) in enumerate(zip(plain, _steps())):
        k = key[i % 8]; x = p ^ s; c = _cksum_add(c, p)
        if st in (0, 4): o = _swap(x) ^ k; st = 1
        elif st == 1: o = _swap(_swap(x) ^ k); st = 2
        elif st == 2: o = _swap(x ^ k); st = 3
        else: o = x ^ k; st = 4
        out.append(o)
    h = bytearray(_swap(k ^ 0x69) for k in key)
    h += bytes([(c & 0xFF) ^ 0x2F ^ key[0] ^ key[4], ((c >> 8) & 0xFF) ^ 0xB3 ^ key[1] ^ key[5],
                ((c >> 16) & 0xFF) ^ 0x58 ^ key[2] ^ key[6], ((c >> 24) & 0xFF) ^ 0xD8 ^ key[3] ^ key[7]])
    return bytes(h + out)

def decode_raw(data: bytes):
    h = data[:12]
    key = bytes(_swap(b) ^ 0x69 for b in h[:8])
    want = (h[8] ^ 0x2F ^ key[0] ^ key[4]) | ((h[9] ^ 0xB3 ^ key[1] ^ key[5]) << 8) | \
           ((h[10] ^ 0x58 ^ key[2] ^ key[6]) << 16) | ((h[11] ^ 0xD8 ^ key[3] ^ key[7]) << 24)
    out = bytearray(); c = 0; st = 0
    for i, (b, s) in enumerate(zip(data[12:], _steps())):
        k = key[i % 8]
        if st in (0, 4): v = _swap(b ^ k); st = 1
        elif st == 1: v = _swap(_swap(b) ^ k); st = 2
        elif st == 2: v = _swap(b) ^ k; st = 3
        else: v = b ^ k; st = 4
        v ^= s; out.append(v); c = _cksum_add(c, v)
    return bytes(out), (c & 0xFFFFFFFF) == want

def b64e(b: bytes) -> str: return base64.b64encode(b).decode().replace('+', '-')
def b64d(s) -> bytes:
    if isinstance(s, bytes): s = s.decode()
    s = s.strip().replace('-', '+').replace(' ', '+')
    s += '=' * (-len(s) % 4)
    return base64.b64decode(s)

def decode(s): return decode_raw(b64d(s))
def encode(plain) -> str:
    if isinstance(plain, str): plain = plain.encode()
    return b64e(encode_raw(plain))

if __name__ == '__main__':
    import sys
    for a in sys.argv[1:]: print(decode(a))
