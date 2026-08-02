import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl, Hva, vxl_project, render_pts, load_pal
from PIL import Image, ImageDraw

t = MixTree()
_, _p = t.find("unittem.pal"); PAL_U = load_pal(_p)
OUT = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)

base = sys.argv[1] if len(sys.argv) > 1 else "gtnk"
_, vd = t.find(base + ".vxl")
_, hd = t.find(base + ".hva")
v = Vxl(vd); h = Hva(hd) if hd else None
print("sections:", [(s.name, s.size, len(s.voxels)) for s in v.sections])
print("hva:", h.valid if h else None, h.nframes if h else 0, h.sec_names if h else [])

# 大画布单方向渲染，看细节
cs = 240
pts, zmin = vxl_project(v, h, 90)
xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
print("proj bbox x:", min(xs), max(xs), " y:", min(ys), max(ys), " npts:", len(pts))
bw = max(xs) - min(xs) + 1.3; bh = max(ys) - min(ys) + 1.3
scale = min((cs - 10) / bw, (cs - 10) / bh)
orgx = cs / 2 - (min(xs) + max(xs)) / 2 * scale
orgy = cs / 2 - (min(ys) + max(ys)) / 2 * scale
img = render_pts(pts, PAL_U, scale, orgx, orgy, cs, cs, supersample=3)
img.save(os.path.join(OUT, f"dbg44_{base}_big.png"))

# 颜色统计： remap 红 vs 普通
from collections import Counter
cnt = Counter()
for (sx, sy, d, c, sh) in pts:
    cnt["remap" if 16 <= c <= 31 else "normal"] += 1
print("voxel colors:", dict(cnt))
