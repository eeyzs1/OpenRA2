import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl, Hva, vxl_project, render_pts, load_pal, _phi_for_screen_alpha
from PIL import Image, ImageDraw

t = MixTree()
_, _p = t.find("unittem.pal"); PAL_U = load_pal(_p)
OUT = os.path.join(os.path.dirname(__file__), "out")

def render_at(base, phi, cs=200):
    _, vd = t.find(base + ".vxl")
    _, hd = t.find(base + ".hva")
    v = Vxl(vd); h = Hva(hd) if hd else None
    pts, zmin = vxl_project(v, h, phi)
    if not pts:
        return None
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    bw = max(xs) - min(xs) + 1.3; bh = max(ys) - min(ys) + 1.3
    scale = min((cs - 10) / bw, (cs - 10) / bh)
    orgx = cs / 2 - (min(xs) + max(xs)) / 2 * scale
    orgy = cs / 2 - (min(ys) + max(ys)) / 2 * scale
    return render_pts(pts, PAL_U, scale, orgx, orgy, cs, cs, supersample=3)

base = sys.argv[1] if len(sys.argv) > 1 else "zep"
# 4 候选映射下的 d0（应指向屏幕东=右）: old=90, new=-45, 以及 ±x 轴两变体
cands = [("old d0 phi=90", 90), ("new d0 phi=-45", -45),
         ("new+180 phi=135", 135), ("new+90 phi=45", 45)]
sheet = Image.new("RGBA", (210 * 4, 240), (40, 40, 48, 255))
dr = ImageDraw.Draw(sheet)
for i, (label, phi) in enumerate(cands):
    img = render_at(base, phi)
    if img: sheet.paste(img, (i * 210 + 5, 5), img)
    dr.text((i * 210 + 5, 212), label, fill=(255, 255, 0, 255))
sheet.save(os.path.join(OUT, f"dbg48_{base}_d0.png"))
print("saved")
