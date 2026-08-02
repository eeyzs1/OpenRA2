# xor80/sub80 + 多调色板评分；并渲染 4 个 comp=0 图标对比
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, shp_frame_to_rgba, ShpFrame
from PIL import Image

T = MixTree()
_, d = T.find("cnsticon.shp")
raw = bytes(d[56:56 + 3072])
W, H = 64, 48

pals = {}
for pn in ["cameo.pal", "unittem.pal", "anim.pal", "shell.pal", "shell2.pal"]:
    _, pd = T.find(pn)
    if pd:
        pals[pn] = load_pal(pd)
_, pd = T.find("uibkgd.pal")
if pd:
    pals["uibkgd"] = load_pal(pd)

def smooth(data, pal):
    err = 0
    for y in range(H):
        for x in range(W - 1):
            a = pal[data[y * W + x]]; b = pal[data[y * W + x + 1]]
            err += abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])
    return err

x = bytes(b ^ 0x80 for b in raw)
res = sorted(((smooth(x, p), nm) for nm, p in pals.items()))
print("xor80 palette smoothness ranking:")
for s, nm in res:
    print(f"  {nm:14s} {s}")

# 渲染 4 个 comp=0 图标：xor80 + cameo.pal / + 最优 palette
best_nm, best_pal = res[0][1], pals[res[0][1]]
files = ["cnsticon.shp", "empicon.shp", "lpsticon.shp", "npsiicon.shp"]
out = Image.new("RGBA", (len(files) * 2 * 132, 100), (30, 30, 30, 255))
for i, fn in enumerate(files):
    _, dd = T.find(fn)
    nf = struct.unpack_from("<H", dd, 6)[0]
    off = struct.unpack_from("<I", dd, 8 + 20)[0]
    r = bytes(dd[off:off + 3072])
    v = bytes(b ^ 0x80 for b in r)
    for j, (pn, pal) in enumerate([("cameo", pals["cameo.pal"]), (best_nm, best_pal)]):
        f = ShpFrame(); f.x = 0; f.y = 0; f.w = 64; f.h = 48
        f.pixels = bytearray(v)
        im = shp_frame_to_rgba(f, pal, remap=False).resize((128, 96), Image.NEAREST)
        out.paste(im, ((i * 2 + j) * 132 + 2, 2), im)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg72.png"))
print("wrote dbg72.png  (even=cameo.pal, odd=", best_nm, ")")
