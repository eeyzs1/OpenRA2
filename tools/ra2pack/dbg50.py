import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image, ImageDraw

t = MixTree()
_, _p = t.find("unittem.pal"); PAL_U = load_pal(_p)
OUT = os.path.join(os.path.dirname(__file__), "out")

_, sd = t.find("gi.shp")
shp = Shp(sd)
S = 10
cell = shp.w * S + 60
strip = Image.new("RGBA", (cell * 4, (shp.h * S + 40) * 2), (40, 40, 48, 255))
dr = ImageDraw.Draw(strip)
for f in range(8):
    fr = shp.frame_pixels(f)
    fi = shp_frame_to_rgba(fr, PAL_U)
    big = fi.resize((fr.w * S, fr.h * S), Image.NEAREST)
    x = (f % 4) * cell + 10
    y = (f // 4) * (shp.h * S + 40) + 10
    strip.paste(big, (x, y), big)
    dr.text((x, y + shp.h * S + 5), f"frame {f}", fill=(255, 255, 0, 255))
strip.save(os.path.join(OUT, "dbg50_gi_big.png"))
print("saved")
