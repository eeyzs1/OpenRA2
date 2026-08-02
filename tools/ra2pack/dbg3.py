import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from Crypto.Cipher import Blowfish
from ra2lib import GAME_DIR

p = os.path.join(GAME_DIR, "RA2.MIX")
d = open(p, "rb").read(1024*1024)
ks = d[4:84]
n = int("681994811107118991598552881669230523074742337494683459234572860554038768387821901289207730765589")

pl = []
for i in (0, 40):
    m = int.from_bytes(ks[i:i+40], "little")
    c = pow(m, 65537, n)
    pl.append(c)

def le(b, ln): return b.to_bytes((b.bit_length()+7)//8 or 1, "little").ljust(ln, b"\x00")
def be(b, ln): return b.to_bytes((b.bit_length()+7)//8 or 1, "big").rjust(ln, b"\x00")

variants = {
    "le28": le(pl[0],28)+le(pl[1],28),
    "be28": be(pl[0],28)+be(pl[1],28),
    "le40trunc": (le(pl[0],40)+le(pl[1],40))[:56],
    "be40trunc": (be(pl[0],40)+be(pl[1],40))[:56],
    "le40rev": (le(pl[0],40)+le(pl[1],40))[::-1][:56],
}

enc0 = d[84:92]
def try_dec(key, swap):
    c = Blowfish.new(key, Blowfish.MODE_ECB)
    b = enc0
    if swap:
        b = b[0:4][::-1]+b[4:8][::-1]
    out = c.decrypt(b)
    if swap:
        out = out[0:4][::-1]+out[4:8][::-1]
    return out

for vn, key in variants.items():
    if not (4 <= len(key) <= 56):
        print(vn, "bad key len", len(key)); continue
    for swap in (False, True):
        out = try_dec(key, swap)
        cnt, size = struct.unpack_from("<HI", out, 0)
        ok = (0 < cnt < 30000) and (0 < size < 282000000)
        print(f"{vn} swap={swap} keylen={len(key)} -> {out.hex()} count={cnt} size={size} {'<<< PLAUSIBLE' if ok else ''}")
