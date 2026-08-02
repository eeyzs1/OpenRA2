# 位旋转穷举 + 多种调色板，按相邻色平滑度评分
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, shp_frame_to_rgba, ShpFrame
from PIL import Image

T = MixTree()
_, d = T.find("cnsticon.shp")
raw = bytes(d[56:56 + 3072])
W, H = 64, 48

def rol(b, k):
    return ((b << k) | (b >> (8 - k))) & 0xFF

pals = {}
for pn in ["cameo.pal", "unittem.pal"]:
    _, pd = T.find(pn)
    pals[pn] = load_pal(pd)
_, pd = T.find("uibkgd.pal")
if pd:
    pals["uibkgd(sidec01)"] = load_pal(pd)
# uibkgd 在 sidec01/sidec02 都有，find 拿第一个

def smooth(data, pal):
    err = 0
    for y in range(H):
        for x in range(W - 1):
            a = pal[data[y * W + x]]; b = pal[data[y * W + x + 1]]
            err += abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])
    return err

results = []
for k in range(8):
    v = bytes(rol(b, k) for b in raw)
    for pn, pal in pals.items():
        results.append((smooth(v, pal), k, pn, v, pal))
results.sort(key=lambda t: t[0])
for s, k, pn, _, _ in results[:8]:
    print(f"rol{k} {pn:20s} smooth={s}")

n = min(6, len(results))
out = Image.new("RGBA", (n * 132, 100), (30, 30, 30, 255))
for i, (s, k, pn, v, pal) in enumerate(results[:n]):
    f = ShpFrame(); f.x = 0; f.y = 0; f.w = W; f.h = H
    f.pixels = bytearray(v)
    im = shp_frame_to_rgba(f, pal, remap=False).resize((128, 96), Image.NEAREST)
    out.paste(im, (i * 132 + 2, 2), im)
    print(i, f"rol{k}", pn)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg70.png"))
