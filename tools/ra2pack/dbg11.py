import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, gi = t.find("gi.shp")
_, ga = t.find("gacnst.shp")
_, ic = t.find("mtnkicon.shp")

for nm, d in (("gi.shp", gi), ("gacnst.shp", ga), ("mtnkicon.shp", ic)):
    z, w, h, nf = struct.unpack_from("<IHHH", d, 0)
    off0_12 = struct.unpack_from("<I", d, 10 + 12)[0]
    off0_16 = struct.unpack_from("<I", d, 10 + 16)[0]
    off0_20 = struct.unpack_from("<I", d, 10 + 20)[0]
    print(f"{nm}: zero={z} w={w} h={h} frames={nf} size={len(d)}")
    print(f"   u32@+12={off0_12} @+16={off0_16} @+20={off0_20}")
    for fh in (16, 20, 24):
        if nf == 0:
            break
        ok = True
        offs = []
        for i in range(nf):
            base = 10 + i * fh
            if base + fh > len(d):
                ok = False
                break
            x, y, fw, fhh = struct.unpack_from("<HHHH", d, base)
            comp = struct.unpack_from("<I", d, base + 8)[0]
            off = struct.unpack_from("<I", d, base + fh - 4)[0]  # assume offset is LAST dword of header
            offs.append((fw, fhh, comp, off))
        if ok:
            mono = all(offs[i][3] <= offs[i + 1][3] or offs[i + 1][3] == 0 for i in range(min(len(offs) - 1, 50)))
            inb = all(o[3] < len(d) or o[3] == 0 for o in offs)
            print(f"   fh={fh}: first={offs[0]} last_off={offs[-1][3]} size={len(d)} mono~{mono} inbounds={inb}")

# infantry sequences in art.ini
_, art = t.find("art.ini")
at = art.decode("latin-1")
import re
m = re.findall(r"^\[(.+?)\]", at, re.M)
seq = [s for s in m if "seq" in s.lower()]
print("sequence sections sample:", seq[:10], " total:", len(seq))
i = at.find("[E1Sequence]")
print(at[i:i+700] if i >= 0 else "no [E1Sequence]")
