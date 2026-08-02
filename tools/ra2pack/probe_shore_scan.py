# 全量扫描 shore*.tem 所有帧，按四边带水占比分类，为每种水向 mask 选最佳帧
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image, ImageDraw

T = MixTree()
_, _p = T.find("isotem.pal")
PAL = load_pal(_p)
W, H = 60, 30

def tmp_render(d, off):
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

def classify(r, g, b, a):
    if a < 128: return 0
    if g >= r - 6 and b > 60 and (g + b) / 2 > r + 8: return 1  # 水
    return 2

def analyze(img):
    """返回 (cu, cv, au, av, frac): 水质心、水|u|/|v|均值(海峡朝向)、水占比"""
    px = img.load()
    cx, cy = W / 2, H / 2
    su = sv = sau = sav = 0.0
    nw = nt = 0
    for y in range(H):
        for x in range(W):
            c = classify(*px[x, y])
            if c == 0: continue
            u = (x - cx) / (W / 2) + (y - cy) / (H / 2)
            v = (y - cy) / (H / 2) - (x - cx) / (W / 2)
            nt += 1
            if c == 1:
                nw += 1; su += u; sv += v; sau += abs(u); sav += abs(v)
    if nw:
        return su / nw, sv / nw, sau / nw, sav / nw, nw / nt
    return 0, 0, 0, 0, 0

EDGES = ["+x", "+y", "-x", "-y"]
results = []  # (file, frame, img, cu, cv, au, av, frac)

names = sorted({n.lower() for n in T.names()} if hasattr(T, "names") else [])
if not names:
    # MixTree 无名单接口：直接尝试 shore01..shore60
    names = [f"shore{i:02d}.tem" for i in range(1, 61)]

for fn in names:
    if not fn.startswith("shore") or not fn.endswith(".tem"): continue
    r = T.find(fn)
    if not r or not r[1]: continue
    d = r[1]
    xb, yb, cx, cy, n = struct.unpack_from("<5i", d, 0)
    frames = []
    if xb * yb == 1 and cx == 60 and cy == 30:
        frames = [(0, 36)]
    else:
        idx0 = struct.unpack_from("<i", d, 20)[0]
        base = idx0 - 1800
        nfr = max(1, (len(d) - base - 900) // 1852 + 1)
        frames = [(i, base + i * 1852) for i in range(min(nfr, 24))]
    for fi, off in frames:
        if off + 900 > len(d): continue
        img = tmp_render(d, off)
        cu, cv, au, av, frac = analyze(img)
        results.append((fn, fi, img, cu, cv, au, av, frac))

print(f"scanned {len(results)} frames from shore files")

# 目标 mask: bit0=+x bit1=+y bit2=-x bit3=-y
targets = {
    0b0001: "直岸E(+x)", 0b0010: "直岸S(+y)", 0b0100: "直岸W(-x)", 0b1000: "直岸N(-y)",
    0b0011: "底角S(+x+y)", 0b0110: "左角W(+y-x)", 0b1100: "顶角N(-x-y)", 0b1001: "右角E(+x-y)",
    0b0101: "海峡(+x-x)", 0b1010: "海峡(+y-y)",
    0b0111: "半岛(-y陆)", 0b1011: "半岛(-x陆)", 0b1101: "半岛(+y陆)", 0b1110: "半岛(+x陆)",
    0b1111: "岛(全水)",
}
# 每 mask 期望质心/水占比（单边 0.5 幅度；岛/海峡高占比）
def tgt_of(mask):
    tu = 0.55 * ((mask & 1) - ((mask >> 2) & 1))
    tv = 0.55 * (((mask >> 1) & 1) - ((mask >> 3) & 1))
    npop = bin(mask).count("1")
    tf = {1: 0.42, 2: 0.55, 3: 0.75, 4: 0.85}[npop]
    if mask in (0b0101, 0b1010): tf = 0.55
    return tu, tv, tf

def err_of(mask, cu, cv, au, av, frac):
    tu, tv, tf = tgt_of(mask)
    e = abs(cu - tu) + abs(cv - tv) + abs(frac - tf) * 0.8
    if mask == 0b0101:   # 海峡 \：水 |u| 应大于 |v|
        e += max(0.0, av - au) * 2.0
    elif mask == 0b1010:  # 海峡 /：水 |v| 应大于 |u|
        e += max(0.0, au - av) * 2.0
    return e

# 贪心去重分配：全局 (帧,mask) 按 err 升序，帧/目标均只用一次；每 mask 取 2 变体
pairs = []
for mask in targets:
    for fn, fi, img, cu, cv, au, av, frac in results:
        if frac < 0.06 or frac > 0.97: continue
        pairs.append((err_of(mask, cu, cv, au, av, frac), mask, fn, fi, img, cu, cv, frac))
pairs.sort(key=lambda p: p[0])
used_frames = set()
picked = {m: [] for m in targets}
for err, mask, fn, fi, img, cu, cv, frac in pairs:
    if len(picked[mask]) >= 2: continue
    if (fn, fi) in used_frames: continue
    used_frames.add((fn, fi))
    picked[mask].append((err, fn, fi, img, cu, cv, frac))
for mask in targets:
    print(f"mask {mask:04b} {targets[mask]}:")
    for err, fn, fi, img, cu, cv, frac in picked[mask]:
        print(f"   err={err:.2f} {fn}#{fi}  c=({cu:+.2f},{cv:+.2f}) frac={frac:.2f}")
best = picked

# 蒙太奇：每 mask 最优 2 帧
tiles = []
for mask in targets:
    for err, fn, fi, img, cu, cv, frac in best[mask]:
        tiles.append((f"{mask:04b}", fn, fi, err, img))
cols = 4
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new("RGBA", (cols * 76, rows * 52), (30, 30, 40, 255))
dr = ImageDraw.Draw(sheet)
for i, (mk, fn, fi, err, img) in enumerate(tiles):
    x, y = (i % cols) * 76 + 2, (i // cols) * 52 + 2
    sheet.paste(img, (x, y), img)
    dr.text((x, y + 32), f"{mk} {fn[5:7]}#{fi} e{err:.1f}", fill=(255, 255, 120, 255))
sheet = sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST)
sheet.save("tools/ra2pack/shore_scan.png")
print("saved tools/ra2pack/shore_scan.png")
