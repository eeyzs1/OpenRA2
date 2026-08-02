# 验证像素起点：base=36(旧) / 52 / 56(跳过0xCD)，clear01 + water09 对比
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(__file__), "out")

def render(d, off, W=60, H=30, skip_ends=False):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load(); p = off
    for y in range(H):
        L = 4 * min(y, H - 1 - y) + 2
        if skip_ends and (y == 0 or y == H - 1):
            continue  # 首尾行不存储
        x0 = (W - L) // 2
        for i in range(L):
            v = d[p]; p += 1
            if v:
                r, g, b = PAL[v]
                px[x0 + i, y] = (r, g, b, 255)
    return img

cases = [("b36", 36, False), ("b52", 52, False), ("b56skip", 56, True)]
names = ["clear01.tem", "water09.tem", "sandy01.tem"]
sheet = Image.new("RGBA", (len(cases) * 64, len(names) * 36), (40, 40, 48, 255))
for r, name in enumerate(names):
    _, d = T.find(name)
    for c, (lab, off, sk) in enumerate(cases):
        im = render(d, off, skip_ends=sk)
        sheet.paste(im, (c * 64 + 2, r * 36 + 2), im)
sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
sheet.save(os.path.join(OUT, "tmp7_bases.png"))
print("rows:", names, "cols:", [c[0] for c in cases])
