# 锁定 rough01/shore04/shore05/shore13 真实像素 base
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image, ImageDraw

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

def hx(d, a, b):
    return " ".join(f"{x:02x}" for x in d[a:b])

_, drf = T.find("rough01.tem")
_, d04 = T.find("shore04.tem")
_, d05 = T.find("shore05.tem")
_, d12 = T.find("shore12.tem")
_, d13 = T.find("shore13.tem")
print("rough01[124:200]:", hx(drf, 124, 200))
print("shore04[16:100]:", hx(d04, 16, 100))
print("shore05[16:110]:", hx(d05, 16, 110))
print("shore13[16:110]:", hx(d13, 16, 110))

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

cases = [
    ("r01 b180", drf, 180), ("r01 b2032", drf, 2032), ("r01 b48332", drf, 48332),
    ("s04 b76", d04, 76), ("s04 b1928", d04, 1928),
    ("s05 b92", d05, 92), ("s05 b1944", d05, 1944), ("s05 b9352", d05, 9352),
    ("s12 b76", d12, 76), ("s13 b92", d13, 92),
]
sheet = Image.new("RGBA", (64 + 110, len(cases) * 40), (30, 30, 38, 255))
dr = ImageDraw.Draw(sheet)
for r, (lab, d, b) in enumerate(cases):
    dr.text((2, r * 40 + 12), lab, fill=(255, 255, 100, 255))
    im = render(d, b)
    sheet.paste(im, (112, r * 40 + 4), im)
sheet = sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST)
sheet.save(os.path.join(OUT, "ps4_bases.png"))
print("saved ps4_bases.png")
