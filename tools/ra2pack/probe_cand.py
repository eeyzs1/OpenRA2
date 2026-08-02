# 用候选 base/stride 渲染 shore/water/rough 蒙太奇，目检哪组偏移正确
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

def render_frame(d, off, W=60, H=30):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load(); p = off
    for y in range(H):
        L = 4 * min(y, H - 1 - y) + 2
        x0 = (W - L) // 2
        for i in range(L):
            if p >= len(d): return img
            v = d[p]; p += 1
            if v:
                r, g, b = PAL[v]
                px[x0 + i, y] = (r, g, b, 255)
    return img

def montage(name, base, stride, maxn=48):
    _, d = T.find(name)
    if not d: return
    frames = []
    for i in range(maxn):
        off = base + i * stride
        if off + 900 > len(d): break
        frames.append(render_frame(d, off))
    cols = min(12, max(1, len(frames)))
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 64, rows * 34), (40, 40, 48, 255))
    for i, im in enumerate(frames):
        sheet.paste(im, ((i % cols) * 64 + 2, (i // cols) * 34 + 2), im)
    out = os.path.join(OUT, f"cand_{name.replace('.tem','')}_b{base}_s{stride}.png")
    sheet.save(out)
    print(f"{name} base={base} stride={stride} frames={len(frames)} -> {os.path.basename(out)}")

# water01: 候选 base 84(旧经验) vs 1884(索引读数)
montage("water01.tem", 84, 1852)
montage("water01.tem", 1884, 1852)
# shore01: 同尺寸类
montage("shore01.tem", 84, 1852)
montage("shore01.tem", 1884, 1852)
# rough01: 旧经验 1980
montage("rough01.tem", 1980, 1852)
montage("rough01.tem", 128, 1852)
# clear01 单帧 vs 多帧尝试
montage("clear01.tem", 36, 1852)
montage("clear01.tem", 36, 936)
# shore13 (3x2blk)
montage("shore13.tem", 84, 1852)
# clat01 (悬崖?)
montage("clat01.tem", 36, 1852)
