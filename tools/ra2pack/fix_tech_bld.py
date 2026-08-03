# Re-export oil/hospital: base WITH ActiveAnim frame0 baked (completes missing structure),
# plus separate ActiveAnim frame sequences for runtime cycling. Same canvas/scale as gen_assets.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(os.path.abspath(__file__)))

from ra2lib import MixTree, Shp, shp_frame_to_rgba
from PIL import Image

BLD_SCALE = 64.0 / 60.0

def scale_bld_canvas(img):
    nw = max(1, round(img.width * BLD_SCALE))
    nh = max(1, round(img.height * BLD_SCALE))
    return img.resize((nw, nh), Image.NEAREST)

T = MixTree()
_, pr = T.find("unittem.pal")
mx = max(pr[:768])
pal = []
for i in range(256):
    r, g, b = pr[i * 3], pr[i * 3 + 1], pr[i * 3 + 2]
    if mx <= 63:
        r, g, b = r * 4, g * 4, b * 4
    pal.append((r, g, b))

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")

def paste_frame(big, shp, i):
    fr = shp.frame_pixels(i)
    img = shp_frame_to_rgba(fr, pal, remap=False)
    x, y = fr.x, fr.y
    if x + img.width > big.width or y + img.height > big.height or x < 0 or y < 0:
        nw = max(big.width, x + img.width, shp.w)
        nh = max(big.height, y + img.height, shp.h)
        bigger = Image.new("RGBA", (nw, nh), (0, 0, 0, 0))
        bigger.paste(big, (0, 0), big)
        big = bigger
    big.paste(img, (max(0, x), max(0, y)), img)
    return big

def export_pair(base_stem, anim_stem, out_base, anim_pref, nmax):
    _, braw = T.find(base_stem + ".shp")
    _, araw = T.find(anim_stem + ".shp")
    bshp = Shp(braw)
    ashp = Shp(araw)
    # Base = body frame0 + ActiveAnim frame0 (补全缺块), same as gen_assets.render_building
    big = Image.new("RGBA", (bshp.w, bshp.h), (0, 0, 0, 0))
    big = paste_frame(big, bshp, 0)
    big = paste_frame(big, ashp, 0)
    scaled = scale_bld_canvas(big)
    scaled.save(os.path.join(SPR, out_base))
    print("base+anim0", out_base, scaled.size, scaled.getbbox())
    # Runtime anim frames on matching canvas size
    n = min(nmax, ashp.nframes)
    for i in range(n):
        canvas = Image.new("RGBA", (ashp.w, ashp.h), (0, 0, 0, 0))
        canvas = paste_frame(canvas, ashp, i)
        # Match base canvas if base was expanded
        if canvas.size != (bshp.w, bshp.h):
            fixed = Image.new("RGBA", (bshp.w, bshp.h), (0, 0, 0, 0))
            fixed.paste(canvas, (0, 0), canvas)
            canvas = fixed
        out = scale_bld_canvas(canvas)
        out.save(os.path.join(SPR, f"{anim_pref}_f{i}.png"))
    print("anim", anim_pref, n)

export_pair("ctoild", "caoild_a", "bld_oilderrick.png", "bld_oilderrick_a", 128)
export_pair("cthosp", "cahosp_a", "bld_hospital.png", "bld_hospital_a", 8)
print("DONE")
