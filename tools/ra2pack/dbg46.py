import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl, Hva, vxl_project, render_pts, load_pal
from PIL import Image

t = MixTree()
_, _p = t.find("unittem.pal"); PAL_U = load_pal(_p)
_, vd = t.find("gtnk.vxl"); _, hd = t.find("gtnk.hva")
v = Vxl(vd); h = Hva(hd)

w, ch = 60, 60
per = []
gminx = 1e9; gmaxx = -1e9; gminy = 1e9; gmaxy = -1e9
for e in range(8):
    pts, zmin = vxl_project(v, h, 90 + 45 * e)
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    print(f"dir{e}: n={len(pts)} x[{min(xs):.1f},{max(xs):.1f}] y[{min(ys):.1f},{max(ys):.1f}]")
    gminx = min(gminx, min(xs)); gmaxx = max(gmaxx, max(xs))
    gminy = min(gminy, min(ys)); gmaxy = max(gmaxy, max(ys))
    per.append((pts, zmin, min(xs), max(xs), min(ys), max(ys)))
bw = gmaxx - gminx + 1.3; bh = gmaxy - gminy + 1.3
scale = min((w - 6) / bw, (ch - 6) / bh)
print(f"global bw={bw:.1f} bh={bh:.1f} scale={scale:.3f}")
