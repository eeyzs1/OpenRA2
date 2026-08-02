# 正确 base 下全部 shore 帧联系表（分两张）+ rough01 28帧验证
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

def base_of(d):
    idx0 = struct.unpack_from("<i", d, 20)[0]
    return idx0 - 1800

entries = []
for i in range(1, 41):
    name = f"shore{i:02d}.tem"
    _, d = T.find(name)
    if not d:
        continue
    b = base_of(d)
    nfr = (len(d) - b - 900) // 1852 + 1
    fr = [render(d, b + k * 1852) for k in range(nfr)]
    entries.append((name.replace(".tem", ""), fr))

def sheet_for(sub, path):
    maxc = max(len(fr) for _, fr in sub)
    sh = Image.new("RGBA", (maxc * 64 + 90, len(sub) * 36), (30, 30, 38, 255))
    dr = ImageDraw.Draw(sh)
    for r, (name, fr) in enumerate(sub):
        dr.text((2, r * 36 + 10), name, fill=(255, 255, 100, 255))
        for c, im in enumerate(fr):
            sh.paste(im, (90 + c * 64 + 2, r * 36 + 2), im)
            dr.text((90 + c * 64 + 2, r * 36 + 2), str(c), fill=(255, 60, 60, 255))
    sh = sh.resize((sh.width * 2, sh.height * 2), Image.NEAREST)
    sh.save(path)
    print("saved", path)

sheet_for(entries[:20], os.path.join(OUT, "ps5_shore_a.png"))
sheet_for(entries[20:], os.path.join(OUT, "ps5_shore_b.png"))

# rough01 全 28 帧（base=180）
_, drf = T.find("rough01.tem")
b = base_of(drf)
nfr = (len(drf) - b - 900) // 1852 + 1
fr = [render(drf, b + k * 1852) for k in range(nfr)]
cols = 7
rows = (len(fr) + cols - 1) // cols
sh = Image.new("RGBA", (cols * 64 + 90, rows * 36), (30, 30, 38, 255))
drw = ImageDraw.Draw(sh)
for i, im in enumerate(fr):
    x, y = 90 + (i % cols) * 64 + 2, (i // cols) * 36 + 2
    sh.paste(im, (x, y), im)
    drw.text((x, y), str(i), fill=(255, 60, 60, 255))
drw.text((2, 10), f"rough01 base={b} n={nfr}", fill=(255, 255, 100, 255))
sh = sh.resize((sh.width * 2, sh.height * 2), Image.NEAREST)
sh.save(os.path.join(OUT, "ps5_rough01.png"))
print("rough01 frames:", nfr)
