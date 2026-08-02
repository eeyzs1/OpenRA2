import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, gi = t.find("gi.shp")
d = gi
off = 0x4748
print("bytes @18248:", d[off:off+64].hex(" "))
# hand-decode first 3 rows (18 px wide)
p = off
for row in range(3):
    rl = struct.unpack_from("<H", d, p)[0]
    print(f"row{row} rowlen={rl}", d[p:p+2+rl-2].hex(" "))
    p += rl  # NOTE: rowlen includes its own 2 bytes
print("--- trying p += 2 + (rowlen-2) == p += rowlen: same thing")
