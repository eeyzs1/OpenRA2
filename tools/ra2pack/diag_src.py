# 对比主 SHP f0 vs mk f0：nalasr/naflak/namisl/gapill/atesla/nasam
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image, ImageDraw
T = MixTree()
_, _p = T.find("unittem.pal"); PAL = load_pal(_p)
bases = ["nalasr", "naflak", "namisl", "gapill", "gagcan", "nasam"]
cell = 130
m = Image.new("RGBA", (cell * len(bases), (cell + 18) * 2), (30, 34, 30, 255))
d = ImageDraw.Draw(m)
def put(nm, col, row, label):
    sd = T.find(nm + ".shp")[1]
    if not sd:
        d.text((col * cell + 4, row * (cell + 18) + cell // 2), nm + " MISS", fill=(255, 80, 80, 255)); return
    shp = Shp(sd)
    fr = shp.frame_pixels(0)
    img = shp_frame_to_rgba(fr, PAL)
    s = min((cell - 8) / img.width, (cell - 8) / img.height)
    img = img.resize((max(1, round(img.width * s)), max(1, round(img.height * s))), Image.NEAREST)
    m.paste(img, (col * cell + (cell - img.width) // 2, row * (cell + 18) + (cell - img.height) // 2), img)
    d.text((col * cell + 4, row * (cell + 18) + cell + 2), label, fill=(255, 255, 0, 255))
for i, b in enumerate(bases):
    put(b, i, 0, b + " f0")
    put(b + "mk", i, 1, b + "mk f0")
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "diag_mkcmp.png")
m.save(out); print("saved", out)
