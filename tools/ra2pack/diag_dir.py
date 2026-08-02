# 放大海豚/乌贼 f8-f15 确认方向序
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image, ImageDraw

T = MixTree()
_, _p = T.find("unittem.pal"); PAL = load_pal(_p)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

def dump(name, frames, tag):
    d = T.find(name + ".shp")[1]
    shp = Shp(d)
    cell = 150
    m = Image.new("RGB", (8 * cell, cell + 16), (30, 30, 34))
    dr = ImageDraw.Draw(m)
    for k, i in enumerate(frames):
        fr = shp.frame_pixels(i)
        if fr.w and fr.h:
            img = shp_frame_to_rgba(fr, PAL, remap=True)
            s = min((cell - 10) / img.width, (cell - 10) / img.height)
            img = img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.NEAREST)
            bg = Image.new("RGBA", (cell, cell), (52, 66, 48, 255))
            bg.paste(img, ((cell - img.width) // 2, (cell - img.height) // 2), img)
            m.paste(bg, (k * cell, 0))
        dr.text((k * cell + 4, cell + 2), f"f{i}", fill=(240, 240, 240))
    m.save(os.path.join(OUT, f"dir_{tag}.png"))
    print(f"-> out/dir_{tag}.png")

dump("dlph", list(range(8, 16)), "dlph_8_15")
dump("dlph", list(range(16, 24)), "dlph_16_23")
dump("sqd", list(range(8, 16)), "sqd_8_15")
