# 全部 shore 帧量化分类：水占比 + 质心方向
import sys, os, struct, math
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)

def render_idx(d, off, W=60, H=30):
    px = []
    p = off
    for y in range(H):
        L = 4 * min(y, H - 1 - y) + 2
        x0 = (W - L) // 2
        row = [(0, 0)] * 60
        for i in range(L):
            v = d[p]; p += 1
            if v:
                row[x0 + i] = (1, v)
        px.append(row)
    return px

def analyze(d, off):
    px = render_idx(d, off)
    n = 0; nw = 0; sx = 0.0; sy = 0.0
    for y in range(30):
        for x in range(60):
            o, v = px[y][x]
            if not o:
                continue
            n += 1
            r, g, b = PAL[v]
            if b > r:  # 水 = 蓝主导
                nw += 1; sx += x; sy += y
    if n == 0:
        return None
    fw = nw / n
    if nw:
        cx, cy = sx / nw - 30, (sy / nw - 15) * 2  # y 放大2倍等比
        ang = math.degrees(math.atan2(cy, cx))  # 0=E 90=S 180=W -90=N
        dist = math.hypot(cx, cy)
    else:
        ang, dist = 0, 0
    return fw, ang, dist

def dir8(ang):
    # E=0, SE=45, S=90, SW=135, W=180/-180, NW=-135, N=-90, NE=-45
    a = (ang + 180 + 22.5) % 360 - 180
    names = [(180, "W"), (135, "SW"), (90, "S"), (45, "SE"), (0, "E"), (-45, "NE"), (-90, "N"), (-135, "NW")]
    best = min(names, key=lambda t: abs(t[0] - a))
    return best[1]

print(f"{'tile':10s} {'water%':>6s} {'dir':>4s} {'dist':>5s}  class")
for i in range(1, 41):
    name = f"shore{i:02d}.tem"
    _, d = T.find(name)
    if not d:
        continue
    idx0 = struct.unpack_from("<i", d, 20)[0]
    b = idx0 - 1800
    nfr = (len(d) - b - 900) // 1852 + 1
    for k in range(nfr):
        r = analyze(d, b + k * 1852)
        if not r:
            continue
        fw, ang, dist = r
        if fw < 0.08:
            cls = "LAND"
        elif fw > 0.92:
            cls = "WATER"
        elif 0.32 <= fw <= 0.68:
            cls = f"STRAIGHT water-{dir8(ang)}"
        elif fw < 0.32:
            cls = f"INNER-CORNER water-notch-{dir8(ang)}"
        else:
            cls = f"OUTER-CORNER land-point-{dir8((ang + 180) % 360 - 180)}"
        print(f"shore{i:02d}.{k}   {fw * 100:6.1f} {dir8(ang):>4s} {dist:5.1f}  {cls}")
