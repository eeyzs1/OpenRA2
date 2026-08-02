# mask7f + 全 palette 扫描
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db, load_pal, shp_frame_to_rgba, ShpFrame, name_hash
from PIL import Image

T = MixTree()
db = name_db()
pal_names = [n for n in set(db.values()) if n.endswith(".pal")]
found = {}
for mf in T.mixes:
    for pn in pal_names:
        e = mf.index.get(name_hash(pn))
        if e:
            data = mf.data[mf.body_base + e.offset: mf.body_base + e.offset + e.size]
            if len(data) == 768 and pn not in found:
                found[pn] = data
pals = {nm: load_pal(pd) for nm, pd in found.items()}
print("palettes:", len(pals))

_, d = T.find("cnsticon.shp")
raw = bytes(d[56:56 + 3072])
v = bytes(b & 0x7F for b in raw)
W, H = 64, 48

def smooth(data, pal):
    err = 0
    for y in range(H):
        for x in range(W - 1):
            a = pal[data[y * W + x]]; b = pal[data[y * W + x + 1]]
            err += abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])
    return err

res = sorted(((smooth(v, p), nm) for nm, p in pals.items()))
for s, nm in res[:8]:
    print(f"  {nm:16s} {s}")

n = 6
out = Image.new("RGBA", (n * 132, 100), (30, 30, 30, 255))
for i, (s, nm) in enumerate(res[:n]):
    f = ShpFrame(); f.x = 0; f.y = 0; f.w = W; f.h = H
    f.pixels = bytearray(v)
    im = shp_frame_to_rgba(f, pals[nm], remap=False).resize((128, 96), Image.NEAREST)
    out.paste(im, (i * 132 + 2, 2), im)
    print(i, nm)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg74.png"))
