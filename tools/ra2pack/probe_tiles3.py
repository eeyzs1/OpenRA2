# TMP 地面瓦片集全面勘察：头解析 + 帧数 + 蒙太奇渲染
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

def hdr(name):
    _, d = T.find(name)
    if not d: return None
    xb, yb, cx, cy, n = struct.unpack_from("<5i", d, 0)
    return d, xb, yb, cx, cy, n

# 钻石行宽解包（60x30 单帧 900 字节）
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

def opaque(img):
    return sum(1 for y in range(img.height) for x in range(img.width) if img.load()[x, y][3])

def probe_set(stem, maxn=64, stride_cands=(900, 1800, 1852, 2700)):
    """尝试 nimg 头声明 + pix_base=20+16n；帧步长自动探测"""
    r = hdr(stem)
    if not r:
        print(f"{stem}: MISS"); return []
    d, xb, yb, cx, cy, n = r
    pix_base = 20 + 16 * n
    print(f"{stem}: {xb}x{yb}blk {cx}x{cy} nimg={n} size={len(d)} pixbase={pix_base}")
    frames = []
    if n > 0 and n <= maxn:
        stride = (len(d) - pix_base) // n
        for i in range(n):
            off = pix_base + i * stride
            if off + 900 > len(d): break
            im = render_frame(d, off)
            frames.append((i, im, opaque(im)))
    return frames

SETS = ["clear01.tem", "clear01a.tem", "rough01.tem", "rough02.tem", "rough03.tem",
        "rough04.tem", "rough05.tem", "rough06.tem", "rough07.tem", "rough08.tem",
        "sandy01.tem", "water01.tem", "water02.tem", "water03.tem", "water09.tem",
        "shore01.tem", "shore02.tem", "shore03.tem", "shore09.tem", "shore13.tem",
        "clat01.tem", "dirt01.tem", "dirt02.tem", "grss01.tem"]
all_frames = {}
for s in SETS:
    fr = probe_set(s)
    if fr:
        all_frames[s] = fr
        print(f"   frames={len(fr)} opaque0={fr[0][2]} last={fr[-1][2]}")

# 蒙太奇：每个集合一行（前 16 帧）
for stem, fr in all_frames.items():
    cols = min(16, len(fr))
    rows = (len(fr) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 64, rows * 34), (40, 40, 48, 255))
    for i, (_, im, op) in enumerate(fr):
        if i >= cols * rows: break
        sheet.paste(im, ((i % cols) * 64 + 2, (i // cols) * 34 + 2), im)
    out = os.path.join(OUT, f"tmp2_{stem.replace('.tem','')}.png")
    sheet.save(out)
    print("  ->", out)
