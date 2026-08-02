import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from Crypto.Cipher import Blowfish
from ra2lib import GAME_DIR

full = open(os.path.join(GAME_DIR, "RA2.MIX"), "rb").read()
fsize = len(full)
ks = full[4:84]
n = int("681994811107118991598552881669230523074742337494683459234572860554038768387821901289207730765589")
pl = [pow(int.from_bytes(ks[i:i+40], "little"), 65537, n) for i in (0, 40)]
le0 = pl[0].to_bytes(40, "little"); le1 = pl[1].to_bytes(40, "little")
be0 = pl[0].to_bytes(40, "big");    be1 = pl[1].to_bytes(40, "big")

def bswap4(b): return b"".join(b[i:i+4][::-1] for i in range(0, len(b), 4))

variants = {
    "le28": le0[:28] + le1[:28],
    "be28": be0[:28] + be1[:28],
    "be28last": be0[-28:] + be1[-28:],
    "le28last": le0[-28:] + le1[-28:],
}
enc0 = full[84:92]
for vn, key in variants.items():
    assert len(key) == 56
    for kswap in (False, True):
        k2 = bswap4(key) if kswap else key
        c = Blowfish.new(k2, Blowfish.MODE_ECB)
        for ds in (False, True):
            b = enc0
            if ds: b = b[0:4][::-1]+b[4:8][::-1]
            o = c.decrypt(b)
            if ds: o = o[0:4][::-1]+o[4:8][::-1]
            cnt, size = struct.unpack_from("<HI", o, 0)
            note = ""
            if 0 < cnt < 30000 and 0 < size < fsize:
                padded = (6 + 12*cnt + 7) & ~7
                if 84 + padded + size + 20 == fsize or 84 + padded + size == fsize:
                    note = " <<< EXACT MATCH"
                else:
                    note = f" plaus? sum={84+padded+size}"
            print(f"{vn} kswap={kswap} dataswap={ds} count={cnt} size={size}{note}")
