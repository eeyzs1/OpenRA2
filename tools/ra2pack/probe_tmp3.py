# 实证：clear01 尾部字节 / rough01 多图布局假设渲染 / SHP 类(tib,tree)试渲染
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, Shp, shp_frame_to_rgba
from PIL import Image

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)

# 1) clear01 尾部字节
_, d = T.find("clear01.tem")
print("clear01[930:960]:", " ".join(f"{b:02x}" for b in d[930:960]))
print("clear01[1836:1872]:", " ".join(f"{b:02x}" for b in d[1836:1872]))

def render_diamond(d, off, W=60, H=30):
    """钻石行宽解包；返回 (img, consumed)"""
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
    return img, p - off

# 2) rough01 假设 A：nimg=28, index 16B each, pix_base=468, stride 900
_, rd = T.find("rough01.tem")
for label, base, stride, n in [("A_idx468_s900", 468, 900, 28), ("B_o1980_s1852", 1980, 1852, 27)]:
    imgs = []
    ok = True
    for i in range(n):
        off = base + i * stride
        if off + 900 > len(rd):
            ok = False; break
        im, _ = render_diamond(rd, off)
        imgs.append(im)
    if not ok:
        print(label, "out of range"); continue
    cols = 7
    rows = (len(imgs) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 64, rows * 34), (40, 40, 48, 255))
    for i, im in enumerate(imgs):
        sheet.paste(im, ((i % cols) * 64 + 2, (i // cols) * 34 + 2), im)
    sheet.save(os.path.join(OUT, f"tmp3_rough_{label}.png"))
    print(label, "saved", len(imgs))

# water01 假设：nimg=4, pix_base=84 stride 900; 以及 stride 1852
_, wd = T.find("water01.tem")
for label, base, stride, n in [("A_b84_s900", 84, 900, 4), ("B_b84_s1852", 84, 1852, 4)]:
    imgs = []
    for i in range(n):
        off = base + i * stride
        if off + 900 > len(wd):
            break
        im, _ = render_diamond(wd, off)
        imgs.append(im)
    if not imgs:
        print(label, "none"); continue
    sheet = Image.new("RGBA", (len(imgs) * 64, 34), (40, 40, 48, 255))
    for i, im in enumerate(imgs):
        sheet.paste(im, (i * 64 + 2, 2), im)
    sheet.save(os.path.join(OUT, f"tmp3_water_{label}.png"))
    print(label, "saved", len(imgs))

# 3) SHP 类叠加物：tib01/gem01/tree01/trock01 帧拼图
for stem in ["tib01", "gem01", "tree01", "tree05", "tree13", "trock01", "trock03", "srock01"]:
    _, sd = T.find(stem + ".tem")
    if not sd:
        print(stem, "MISS"); continue
    shp = Shp(sd)
    frames = []
    for i in range(min(shp.nframes, 24)):
        fr = shp.frame_pixels(i)
        if fr.w == 0 or fr.h == 0:
            continue
        im = shp_frame_to_rgba(fr, PAL, remap=False)
        frames.append((im, fr.x, fr.y))
    if not frames:
        print(stem, "no frames"); continue
    # 按 SHP 大画布合成每帧
    big = Image.new("RGBA", (shp.w, shp.h), (40, 40, 48, 255))
    cols = min(6, len(frames))
    cw = max(f[0].width for f in frames) + 8
    chh = max(f[0].height for f in frames) + 8
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * cw, rows * chh), (40, 40, 48, 255))
    for i, (im, fx, fy) in enumerate(frames):
        sheet.paste(im, ((i % cols) * cw + 4, (i // cols) * chh + 4), im)
    sheet.save(os.path.join(OUT, f"tmp3_{stem}.png"))
    print(stem, f"shp {shp.w}x{shp.h} n={shp.nframes} -> {len(frames)} frames")
