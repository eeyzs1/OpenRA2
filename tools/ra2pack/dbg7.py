import sys, os, struct, itertools, base64
sys.path.insert(0, os.path.dirname(__file__))
from Crypto.Cipher import Blowfish
from ra2lib import GAME_DIR

full = open(os.path.join(GAME_DIR, "RA2.MIX"), "rb").read()
fsize = len(full)
ks = full[4:84]

moduli = {
    "rust": int("681994811107118991598552881669230523074742337494683459234572860554038768387821901289207730765589"),
    "b64_le": int.from_bytes(base64.b64decode("AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q=="), "little"),
    "b64_be": int.from_bytes(base64.b64decode("AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q=="), "big"),
}

def bswap4(b): return b"".join(b[i:i+4][::-1] for i in range(0, len(b), 4))
def rev8(b): return b[::-1]

enc0 = full[84:92]
found = []
for mn, n in moduli.items():
    for imp in ("little", "big"):
        pl = []
        ok = True
        for i in (0, 40):
            m = int.from_bytes(ks[i:i+40], imp)
            if m >= n:
                ok = False
            pl.append(pow(m, 65537, n))
        if not ok:
            continue
        for explen in (28, 40, 56):
            for endian in ("little", "big"):
                try:
                    b0 = pl[0].to_bytes(explen, endian)
                    b1 = pl[1].to_bytes(explen, endian)
                except OverflowError:
                    continue
                blob = b0 + b1
                asms = {"first56": blob[:56], "last56": blob[-56:]}
                if explen == 28:
                    asms = {"cat": blob}
                for an, key0 in asms.items():
                    if len(key0) < 4 or len(key0) > 56: continue
                    for kt in ("id", "bswap4", "rev"):
                        key = {"id": lambda b: b, "bswap4": bswap4, "rev": lambda b: b[::-1]}[kt](key0)
                        c = Blowfish.new(key, Blowfish.MODE_ECB)
                        for dt in ("id", "wswap", "rev"):
                            b = {"id": enc0, "wswap": bswap4(enc0), "rev": rev8(enc0)}[dt]
                            o = c.decrypt(b)
                            if dt == "wswap": o = bswap4(o)
                            elif dt == "rev": o = o[::-1]
                            cnt, size = struct.unpack_from("<HI", o, 0)
                            if 0 < cnt < 40000 and 0 < size < fsize:
                                padded = (6 + 12*cnt + 7) & ~7
                                if 84 + padded + size + 20 == fsize or 84 + padded + size == fsize:
                                    found.append((mn, imp, explen, endian, an, kt, dt, cnt, size))
                                    print("EXACT:", found[-1])
print("done, found:", len(found))
