import sys, os, math
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl, Hva, vxl_project, render_pts, vxl_pal
from PIL import Image, ImageDraw

t = MixTree()
OUT = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)

def load_pair(base):
    _, vd = t.find(base + ".vxl")
    _, hd = t.find(base + ".hva")
    if not vd:
        return None, None, None
    v = Vxl(vd)
    h = Hva(hd) if hd else None
    return v, h, vxl_pal(vd)

def render_at(base, phi, cs=120):
    v, h, pal = load_pair(base)
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

# 1) mtnk orientation test: phi = 0, 90, 180, 270
sheet = Image.new("RGBA", (130 * 4, 150), (40, 40, 48, 255))
dr = ImageDraw.Draw(sheet)
for i, phi in enumerate([0, 90, 180, 270]):
    img = render_at("mtnk", phi)
    if img:
        sheet.paste(img, (i * 130 + 5, 5), img)
    dr.text((i * 130 + 5, 132), "phi=%d" % phi, fill=(255, 255, 0, 255))
sheet.save(os.path.join(OUT, "orient_mtnk.png"))

# 2) unknown tanks for identification (phi=-45 -> screen east-ish)
names = ["1tnk", "2tnk", "3tnk", "4tnk", "gtnk", "ltnk", "rtnk", "ftnk", "stnk", "utnk",
         "mrj", "mmchbarl", "trucka", "truckb", "truk", "truck2", "ptruck", "sub", "subt",
         "ftnk", "fortress", "mlrs", "tnkd"]
cols = 8
rows = (len(names) + cols - 1) // cols
sheet = Image.new("RGBA", (130 * cols, 150 * rows), (40, 40, 48, 255))
dr = ImageDraw.Draw(sheet)
for i, nm in enumerate(names):
    img = render_at(nm, -45)
    x, y = (i % cols) * 130 + 5, (i // cols) * 150 + 5
    if img:
        sheet.paste(img, (x, y), img)
    dr.text((x, y + 127), nm, fill=(255, 255, 0, 255))
sheet.save(os.path.join(OUT, "identify.png"))
print("saved", os.path.join(OUT, "orient_mtnk.png"), "and identify.png")
