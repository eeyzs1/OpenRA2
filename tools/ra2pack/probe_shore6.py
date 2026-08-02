# 候选 shore 帧 6x 放大分类
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

CANDS = [
    "shore01.1", "shore01.2", "shore01.3", "shore03.3",
    "shore04.1", "shore05.2", "shore05.3", "shore05.4",
    "shore13.4", "shore13.5", "shore14.2", "shore14.4",
    "shore15.0", "shore15.1", "shore17.0", "shore20.0",
    "shore23.0", "shore24.0", "shore28.0", "shore33.3",
    "shore36.1", "shore37.0", "shore21.2", "shore29.4",
    "shore06.3", "shore07.2", "shore09.1", "shore30.3",
]
cols = 7
rows = (len(CANDS) + cols - 1) // cols
CW, CH = 64, 40
sh = Image.new("RGBA", (cols * CW + 10, rows * CH + 10), (30, 30, 38, 255))
dr = ImageDraw.Draw(sh)
for i, tag in enumerate(CANDS):
    name, fi = tag.split(".")
    _, d = T.find(name + ".tem")
    idx0 = struct.unpack_from("<i", d, 20)[0]
    b = idx0 - 1800
    im = render(d, b + int(fi) * 1852)
    x, y = 10 + (i % cols) * CW, 10 + (i // cols) * CH
    sh.paste(im, (x + 2, y + 2), im)
    dr.text((x + 2, y + 30), tag.replace("shore", "s"), fill=(255, 255, 100, 255))
sh = sh.resize((sh.width * 4, sh.height * 4), Image.NEAREST)
sh.save(os.path.join(OUT, "ps6_cand.png"))
print("saved ps6_cand.png", sh.size)
