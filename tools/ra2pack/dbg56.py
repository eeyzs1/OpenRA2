import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image, ImageDraw

t = MixTree()
_, _p = t.find("unittem.pal"); PAL_U = load_pal(_p)
OUT = os.path.join(os.path.dirname(__file__), "out")

for base in ["namisl", "gagcan"]:
    _, sd = t.find(base + ".shp")
    shp = Shp(sd)
    print(base, "frames:", shp.nframes, "canvas", shp.w, shp.h)
    cols = min(shp.nframes, 8)
    rows = (shp.nframes + cols - 1) // cols
    cw, chh = shp.w + 10, shp.h + 24
    sheet = Image.new("RGBA", (cw * cols, chh * rows), (40, 40, 48, 255))
    dr = ImageDraw.Draw(sheet)
    for f in range(shp.nframes):
        fr = shp.frame_pixels(f)
        fi = shp_frame_to_rgba(fr, PAL_U)
        x = (f % cols) * cw + 5
        y = (f // cols) * chh + 5
        sheet.paste(fi, (x + fr.x, y + fr.y), fi)
        dr.text((x, y + shp.h + 6), f"f{f} {fr.w}x{fr.h}", fill=(255, 255, 0, 255))
    sheet.save(os.path.join(OUT, f"dbg56_{base}_frames.png"))
print("saved")
