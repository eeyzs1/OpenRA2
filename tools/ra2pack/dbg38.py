import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl, Hva, vxl_project, render_pts, load_pal
from PIL import Image, ImageDraw

t = MixTree()
OUT = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)

_, upd = t.find("unittem.pal")
UP = load_pal(upd)

def load_pair(base):
    _, vd = t.find(base + ".vxl")
    _, hd = t.find(base + ".hva")
    if not vd:
        return None, None
    return Vxl(vd), (Hva(hd) if hd else None)

def render_at(base, phi, cs=110):
    v, h = load_pair(base)
    if v is None:
        return None
    pts, zmin = vxl_project(v, h, phi)
    if not pts:
        return None
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    bw = max(xs) - min(xs) + 1.3; bh = max(ys) - min(ys) + 1.3
    scale = min((cs - 12) / bw, (cs - 12) / bh)
    orgx = cs / 2 - (min(xs) + max(xs)) / 2 * scale
    orgy = cs / 2 - (min(ys) + max(ys)) / 2 * scale
    return render_pts(pts, UP, scale, orgx, orgy, cs, cs)

# 8 个 phi，两模型对照（mtnk 炮管指向 = 前向）
names = ["mtnk", "gtnk"]
sheet = Image.new("RGBA", (120 * 8, 140 * len(names)), (40, 40, 48, 255))
dr = ImageDraw.Draw(sheet)
for r, nm in enumerate(names):
    for i in range(8):
        phi = i * 45
        img = render_at(nm, phi)
        x, y = i * 120 + 5, r * 140 + 5
        if img:
            sheet.paste(img, (x, y), img)
        dr.text((x, y + 115), "phi=%d" % phi, fill=(255, 255, 0, 255))
sheet.save(os.path.join(OUT, "calib8.png"))
print("saved calib8.png")
