# 定位所有含 cnsticon.shp 的 mix + 用不同调色板渲染
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_hash, load_pal, shp_frame_to_rgba, ShpFrame
from PIL import Image

T = MixTree()
h = name_hash("cnsticon.shp")
print("cnsticon.shp hash:", hex(h))
found = []
for mf in T.mixes:
    e = mf.index.get(h)
    if e:
        data = mf.get("cnsticon.shp")
        found.append((mf.name, len(data), data[:40].hex()[:32]))
        print(f"  in {mf.name}: size={len(data)}")

# cameo.pal 中 176..232 的颜色
_, pc = T.find("cameo.pal")
PAL_C = load_pal(pc)
print("\ncameo.pal[176..184]:", PAL_C[176:185])
print("cameo.pal[214..224]:", PAL_C[214:225])

# 用多个调色板渲染 cnsticon frame0
_, d = T.find("cnsticon.shp")
f = ShpFrame(); f.x = 0; f.y = 0; f.w = 64; f.h = 48
f.pixels = bytearray(d[56:56 + 3072])
pals = [("cameo", PAL_C)]
for pn in ["unittem.pal", "unitsno.pal", "uniturb.pal"]:
    _, pd = T.find(pn)
    if pd:
        pals.append((pn, load_pal(pd)))
out = Image.new("RGBA", (len(pals) * 134, 100), (30, 30, 30, 255))
for i, (nm, pl) in enumerate(pals):
    im = shp_frame_to_rgba(f, pl, remap=False).resize((128, 96), Image.NEAREST)
    out.paste(im, (i * 134 + 2, 2), im)
    print("rendered with", nm)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg64_pals.png"))
