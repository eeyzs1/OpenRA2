# clear01 顶部黑刺排查：不同像素起点对比 + 源文件首行内容
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(__file__), "out")

_, d = T.find("clear01.tem")
print("bytes[28:48]:", " ".join(f"{b:02x}" for b in d[28:48]))

def render(off, W=60, H=30):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load(); p = off
    for y in range(H):
        L = 4 * min(y, H - 1 - y) + 2
        x0 = (W - L) // 2
        for i in range(L):
            v = d[p]; p += 1
            if v:
                r, g, b = PAL[v]
                px[x0 + i, y] = (r, g, b, 255)
    return img

sheet = Image.new("RGBA", (5 * 64, 34), (40, 40, 48, 255))
for k, off in enumerate([28, 32, 36, 40, 44]):
    im = render(off)
    sheet.paste(im, (k * 64 + 2, 2), im)
sheet = sheet.resize((5 * 128, 68), Image.NEAREST)
sheet.save(os.path.join(OUT, "tmp5_clear_offsets.png"))
print("saved tmp5_clear_offsets.png (offsets 28,32,36,40,44)")
