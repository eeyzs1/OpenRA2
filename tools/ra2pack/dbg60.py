# cnsticon.shp 两帧分别渲染 + 原始字节检查
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image

T = MixTree()
_, d = T.find("cnsticon.shp")
shp = Shp(d)
print("frames:", shp.nframes, "canvas:", shp.w, "x", shp.h)
_, pc = T.find("cameo.pal"); PAL_C = load_pal(pc)

out = Image.new("RGBA", (shp.nframes * 70, 60), (30, 30, 30, 255))
for i in range(shp.nframes):
    fr = shp.frame_pixels(i)
    nz = sum(1 for v in fr.pixels if v)
    print(f"frame {i}: off=({fr.x},{fr.y}) size={fr.w}x{fr.h} nonzero_px={nz}")
    im = shp_frame_to_rgba(fr, PAL_C, remap=False)
    im = im.resize((64, 48), Image.NEAREST)
    out.paste(im, (i * 70 + 3, 3), im)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg60_cnst2.png"))
print("wrote dbg60_cnst2.png")
