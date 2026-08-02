import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl, Hva, vxl_project, render_pts, vxl_pal, load_pal
from PIL import Image, ImageDraw

t = MixTree()
OUT = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)

_, upd = t.find("unittem.pal")
up = load_pal(upd)

def load_pair(base):
    _, vd = t.find(base + ".vxl")
    _, hd = t.find(base + ".hva")
    if not vd:
        return None, None
    return Vxl(vd), (Hva(hd) if hd else None)

def render_at(base, phi, pal, cs=120):
    v, h = load_pair(base)
    if v is None:
        return None
    pts, zmin = vxl_project(v, h, phi)
    if not pts:
        return None
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    bw = max(xs) - min(xs) + 1.3; bh = max(ys) - min(ys) + 1.3
    scale = min((cs - 10) / bw, (cs - 10) / bh)
    orgx = cs / 2 - (min(xs) + max(xs)) / 2 * scale
    orgy = cs / 2 - (min(ys) + max(ys)) / 2 * scale
    return render_pts(pts, pal, scale, orgx, orgy, cs, cs)

names = ["mtnk", "gtnk", "htnk", "1tnk", "harv", "mcv", "v3ln", "ttnk", "suv"]
cols = len(names)
# 两行：上=内嵌调色板，下=unittem.pal
sheet = Image.new("RGBA", (130 * cols, 150 * 2), (40, 40, 48, 255))
dr = ImageDraw.Draw(sheet)
for i, nm in enumerate(names):
    _, vd = t.find(nm + ".vxl")
    if not vd:
        dr.text((i * 130 + 5, 5), nm + " NA", fill=(255, 80, 80, 255))
        continue
    ep = vxl_pal(vd)
    img1 = render_at(nm, -45, ep)
    img2 = render_at(nm, -45, up)
    if img1:
        sheet.paste(img1, (i * 130 + 5, 5), img1)
    if img2:
        sheet.paste(img2, (i * 130 + 5, 155 - 5 - 0), img2)
    dr.text((i * 130 + 5, 132), nm, fill=(255, 255, 0, 255))
    dr.text((i * 130 + 5, 282), nm, fill=(0, 255, 255, 255))
sheet.save(os.path.join(OUT, "palcmp.png"))
print("saved palcmp.png")
