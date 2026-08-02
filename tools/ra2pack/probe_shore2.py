# water 全部文件结构 + base 对比 + shore 带标签联系表
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image, ImageDraw

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

def render(d, off, W=60, H=30):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load(); p = off
    for y in range(H):
        L = 4 * min(y, H - 1 - y) + 2
        x0 = (W - L) // 2
        for i in range(L):
            if p >= len(d):
                return img
            v = d[p]; p += 1
            if v:
                r, g, b = PAL[v]
                px[x0 + i, y] = (r, g, b, 255)
    return img

def valid_offsets(d):
    """int32 索引数组：连续等差(1852)的合法像素偏移，去掉 1852/952 尾标"""
    n = struct.unpack_from("<i", d, 16)[0]
    raw = []
    for i in range(max(1, min(n, 512))):
        v = struct.unpack_from("<i", d, 20 + i * 4)[0]
        raw.append(v)
    # 有效 = 值 > 1000 且 +900 不越界，且与首值成等差
    good = [v for v in raw if 1000 < v and v + 900 <= len(d)]
    # 等差过滤：base + k*1852
    if good:
        b = good[0]
        good = [v for v in good if (v - b) % 1852 == 0]
    return good

print("== water files ==")
for i in range(1, 15):
    name = f"water{i:02d}.tem"
    _, d = T.find(name)
    if not d:
        print(f"{name}: MISS"); continue
    xb, yb, cx, cy, n = struct.unpack_from("<5i", d, 0)
    offs = valid_offsets(d)
    print(f"{name}: {xb}x{yb}blk nhdr={n} size={len(d)} offs={offs}")

# water01/02 旧 base=84 4帧 vs 正确偏移 3帧 对比
rows = []
labels = []
_, dw1 = T.find("water01.tem")
_, dw2 = T.find("water02.tem")
for lab, d in [("w1 old84", dw1), ("w1 new", dw1), ("w2 old84", dw2), ("w2 new", dw2)]:
    if "old" in lab:
        fr = [render(d, 84 + k * 1852) for k in range(4)]
    else:
        fr = [render(d, o) for o in valid_offsets(d)]
    rows.append(fr); labels.append(lab)
cols = 4
sheet = Image.new("RGBA", (cols * 64 + 90, len(rows) * 36), (30, 30, 38, 255))
dr = ImageDraw.Draw(sheet)
for r, fr in enumerate(rows):
    dr.text((2, r * 36 + 10), labels[r], fill=(255, 255, 100, 255))
    for c, im in enumerate(fr):
        sheet.paste(im, (90 + c * 64 + 2, r * 36 + 2), im)
sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
sheet.save(os.path.join(OUT, "ps2_water_cmp.png"))
print("saved ps2_water_cmp.png")

# shore 全部帧带标签联系表（每行一个文件，帧标注序号）
entries = []
for i in list(range(1, 41)):
    name = f"shore{i:02d}.tem"
    _, d = T.find(name)
    if not d:
        continue
    offs = valid_offsets(d)
    fr = [render(d, o) for o in offs]
    entries.append((name, fr))
maxc = max(len(fr) for _, fr in entries)
sheet = Image.new("RGBA", (maxc * 64 + 80, len(entries) * 36), (30, 30, 38, 255))
dr = ImageDraw.Draw(sheet)
for r, (name, fr) in enumerate(entries):
    dr.text((2, r * 36 + 10), name.replace(".tem", ""), fill=(255, 255, 100, 255))
    for c, im in enumerate(fr):
        sheet.paste(im, (80 + c * 64 + 2, r * 36 + 2), im)
        dr.text((80 + c * 64 + 2, r * 36 + 2), str(c), fill=(255, 80, 80, 255))
sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
sheet.save(os.path.join(OUT, "ps2_shore_all.png"))
print("saved ps2_shore_all.png rows:", len(entries))
