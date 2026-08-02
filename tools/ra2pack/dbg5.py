import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from Crypto.Cipher import Blowfish
from ra2lib import GAME_DIR

full = open(os.path.join(GAME_DIR, "RA2.MIX"), "rb").read()
fsize = len(full)
ks = full[4:84]
n = int("681994811107118991598552881669230523074742337494683459234572860554038768387821901289207730765589")

def le(b, ln): return b.to_bytes((b.bit_length()+7)//8 or 1, "little").ljust(ln, b"\x00")
pl = [pow(int.from_bytes(ks[i:i+40], "little"), 65537, n) for i in (0, 40)]

def bswap4(b):
    return b"".join(b[i:i+4][::-1] for i in range(0, len(b), 4))

base = (le(pl[0], 40) + le(pl[1], 40))[:56]
variants = {
    "base": base,
    "bswap4": bswap4(base),
    "rev": base[::-1],
}
enc0 = full[84:92]
enc1 = full[92:100]

def tryit(key, dataswap):
    c = Blowfish.new(key, Blowfish.MODE_ECB)
    b = enc0
    if dataswap: b = b[0:4][::-1]+b[4:8][::-1]
    o = c.decrypt(b)
    if dataswap: o = o[0:4][::-1]+o[4:8][::-1]
    cnt, size = struct.unpack_from("<HI", o, 0)
    return cnt, size, o

for vn, key in variants.items():
    for ds in (False, True):
        cnt, size, o = tryit(key, ds)
        # validate fully if plausible
        note = ""
        if 0 < cnt < 30000 and 0 < size < fsize:
            padded = (6 + 12*cnt + 7) & ~7
            if 84 + padded + size + 20 == fsize or 84 + padded + size == fsize:
                note = " <<< EXACT MATCH"
            else:
                note = f" plausible-ish (sum {84+padded+size} vs {fsize})"
        print(f"{vn} dataswap={ds} count={cnt} size={size}{note}")
