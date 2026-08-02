# shore/rough/water TMP 结构探测：int32 偏移索引解读 + 蒙太奇
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

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

def offsets(d):
    """索引区 = 从 20 开始的 int32 数组，取落在合法像素区的值"""
    n = struct.unpack_from("<i", d, 16)[0]
    res = []
    for i in range(max(1, min(n, 256))):
        v = struct.unpack_from("<i", d, 20 + i * 4)[0]
        if 0 < v and v + 900 <= len(d):
            res.append(v)
    return res

names = [f"shore{i:02d}.tem" for i in range(1, 43)]
names += ["water01.tem", "water02.tem", "rough01.tem"]
found = {}
for name in names:
    _, d = T.find(name)
    if not d:
        continue
    xb, yb, cx, cy, n = struct.unpack_from("<5i", d, 0)
    offs = offsets(d)
    found[name] = (d, xb, yb, offs)
    print(f"{name}: {xb}x{yb}blk nhdr={n} size={len(d)} offs({len(offs)})={offs[:10]}")

# 蒙太奇：每个文件按索引偏移渲染全部帧
def montage(name, d, offs, tag=""):
    fr = [render(d, o) for o in offs]
    if not fr:
        return
    cols = min(10, len(fr))
    rows = (len(fr) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 64, rows * 34), (40, 40, 48, 255))
    for i, im in enumerate(fr):
        sheet.paste(im, ((i % cols) * 64 + 2, (i // cols) * 34 + 2), im)
    out = os.path.join(OUT, f"ps_{tag}{name.replace('.tem','')}.png")
    sheet.save(out)

for name, (d, xb, yb, offs) in found.items():
    montage(name, d, offs)
print("montages saved")
