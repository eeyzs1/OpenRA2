# 用项目 ra2lib 直接渲染指定 VXL/SHP，输出 8 方向拼图供目检
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree, Vxl, Hva, Shp, load_pal, vxl_project, render_pts, shp_frame_to_rgba, _phi_for_screen_alpha
from PIL import Image

T = MixTree()
_, _p = T.find("unittem.pal"); PAL = load_pal(_p)

def render_vxl(name, cell=90):
    vd = T.find(name + ".vxl")[1]
    hd = T.find(name + ".hva")[1]
    v = Vxl(vd); h = Hva(hd) if hd else None
    m = Image.new("RGBA", (cell * 8, cell + 16), (24, 24, 28, 255))
    for e in range(8):
        pts, zmin = vxl_project(v, h, _phi_for_screen_alpha(45 * e))
        if not pts:
            continue
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        bw, bh = max(xs) - min(xs) + 1.3, max(ys) - min(ys) + 1.3
        sc = min((cell - 8) / bw, (cell - 8) / bh)
        img = render_pts(pts, PAL, sc, cell / 2 - (min(xs) + max(xs)) / 2 * sc,
                         cell - 6 - max(ys) * sc, cell, cell, supersample=2)
        m.paste(img, (e * cell, 0), img)
    return m

names = sys.argv[1:] or ["mtnk"]
m = Image.new("RGBA", (720, 106 * len(names)), (24, 24, 28, 255))
from PIL import ImageDraw
d = ImageDraw.Draw(m)
for i, nm in enumerate(names):
    try:
        im = render_vxl(nm)
        m.paste(im, (0, i * 106), im)
    except Exception as ex:
        print(nm, "FAIL", ex)
    d.text((4, i * 106 + 92), nm, fill=(255, 255, 0, 255))
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "vxlcheck.png")
m.save(out)
print("saved", out)
