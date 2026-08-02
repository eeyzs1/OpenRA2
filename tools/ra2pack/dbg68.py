# 穷举所有 mix 中的 .pal 文件渲染 cnsticon
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db, load_pal, shp_frame_to_rgba, ShpFrame, name_hash
from PIL import Image

T = MixTree()
db = name_db()

# 找所有 .pal 候选名
pal_names = [n for n in set(db.values()) if n.endswith(".pal")]
print("pal names in db:", pal_names)

# 在所有 mix 中探测这些 pal
found = []
for mf in T.mixes:
    for pn in pal_names:
        e = mf.index.get(name_hash(pn))
        if e:
            data = mf.data[mf.body_base + e.offset: mf.body_base + e.offset + e.size]
            if len(data) == 768:
                found.append((mf.name + "/" + pn, data))
print("found palettes:", len(found))
for nm, _ in found:
    print("  ", nm)

# 额外：穷举 mix 中所有 768 字节 entry 当作调色板试
_, d = T.find("cnsticon.shp")
raw = d[56:56 + 3072]

def score(paldata):
    """渲染后用'相邻像素色差'评分：结构图相邻色应相近"""
    pal = load_pal(paldata)
    err = 0
    for y in range(48):
        for x in range(63):
            a = pal[raw[y * 64 + x]]; b = pal[raw[y * 64 + x + 1]]
            err += abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])
    return err

scored = [(score(pd), nm) for nm, pd in found]
scored.sort()
print("\nbest palettes by adjacency smoothness:")
for s, nm in scored[:10]:
    print(f"  {s:9d}  {nm}")

# 用 top6 渲染
n = min(6, len(scored))
out = Image.new("RGBA", (n * 132, 100), (30, 30, 30, 255))
for i, (s, nm) in enumerate(scored[:n]):
    pd = dict(found)[nm]
    pal = load_pal(pd)
    f = ShpFrame(); f.x = 0; f.y = 0; f.w = 64; f.h = 48
    f.pixels = bytearray(raw)
    im = shp_frame_to_rgba(f, pal, remap=False).resize((128, 96), Image.NEAREST)
    out.paste(im, (i * 132 + 2, 2), im)
    print(i, nm)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg68_pals.png"))
