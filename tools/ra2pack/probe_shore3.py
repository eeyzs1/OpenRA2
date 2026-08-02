# 精确定位像素起点：hexdump + 多 base 放大对比
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image, ImageDraw

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

_, dw = T.find("water01.tem")
_, ds = T.find("shore01.tem")
_, drf = T.find("rough01.tem")

def hx(d, a, b):
    return " ".join(f"{x:02x}" for x in d[a:b])

print("water01[16:100]:", hx(dw, 16, 100))
print("water01[532:564]:", hx(dw, 532, 564))
print("water01[1872:1936]:", hx(dw, 1872, 1936))
print("shore01[16:100]:", hx(ds, 16, 100))
print("rough01[16:100]:", hx(drf, 16, 100))
print("rough01[1900:2000]:", hx(drf, 1900, 2000))

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

# 对比：water01 bases [36, 84, 116, 1884]；shore01 bases [84, 1936, 1884, 3736]；rough01 [1980, 2068, 128, 468]
cases = [
    ("w01", dw, [36, 84, 116, 1884]),
    ("s01", ds, [84, 1936, 1884, 3736]),
    ("r01", drf, [1980, 2068, 128, 468]),
]
rows = sum(len(c[2]) for c in cases)
sheet = Image.new("RGBA", (4 * 64 + 90, rows * 40), (30, 30, 38, 255))
dr = ImageDraw.Draw(sheet)
r = 0
for tag, d, bases in cases:
    for b in bases:
        dr.text((2, r * 40 + 12), f"{tag} b{b}", fill=(255, 255, 100, 255))
        for k in range(1):
            im = render(d, b)
            sheet.paste(im, (90 + 2, r * 40 + 4), im)
        r += 1
sheet = sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST)
sheet.save(os.path.join(OUT, "ps3_bases.png"))
print("saved ps3_bases.png")
