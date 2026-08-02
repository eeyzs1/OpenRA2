import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl, Hva, vxl_project, render_pts, load_pal, _phi_for_screen_alpha
from PIL import Image, ImageDraw

t = MixTree()
_, _p = t.find("unittem.pal"); PAL_U = load_pal(_p)
OUT = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)

def render_at(base, phi, cs=160):
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

base = sys.argv[1] if len(sys.argv) > 1 else "gtnk"
# 上排: 旧公式 phi=90+45e 的 d0..d3；下排: 修正 _phi_for_screen_alpha(45d) 的 d0..d3
sheet = Image.new("RGBA", (170 * 4, 180 * 2), (40, 40, 48, 255))
dr = ImageDraw.Draw(sheet)
for i, e in enumerate(range(4)):
    img = render_at(base, 90 + 45 * e)
    if img: sheet.paste(img, (i * 170 + 5, 5), img)
    dr.text((i * 170 + 5, 167), f"old d{e} phi={90+45*e}", fill=(255, 255, 0, 255))
for i, d in enumerate(range(4)):
    phi = _phi_for_screen_alpha(45 * d)
    img = render_at(base, phi)
    if img: sheet.paste(img, (i * 170 + 5, 180 + 5), img)
    dr.text((i * 170 + 5, 180 + 167), f"new d{d} phi={phi:.1f}", fill=(0, 255, 255, 255))
sheet.save(os.path.join(OUT, f"dbg47_{base}_dirs.png"))
print("saved", os.path.join(OUT, f"dbg47_{base}_dirs.png"))
