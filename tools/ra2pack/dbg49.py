import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image, ImageDraw

t = MixTree()
_, _p = t.find("unittem.pal"); PAL_U = load_pal(_p)
OUT = os.path.join(os.path.dirname(__file__), "out")

_, sd = t.find("gi.shp")
shp = Shp(sd)
S = 4
strip = Image.new("RGBA", (shp.w * S * 8 + 8 * 40, shp.h * S + 20), (40, 40, 48, 255))
dr = ImageDraw.Draw(strip)
for f in range(8):
    fr = shp.frame_pixels(f)
    fi = shp_frame_to_rgba(fr, PAL_U)
    big = fi.resize((fr.w * S, fr.h * S), Image.NEAREST)
    strip.paste(big, (f * (shp.w * S + 40) + 5, 5), big)
    dr.text((f * (shp.w * S + 40) + 5, shp.h * S + 8), f"f{f}", fill=(255, 255, 0, 255))
strip.save(os.path.join(OUT, "dbg49_gi_facings.png"))
print("saved")
