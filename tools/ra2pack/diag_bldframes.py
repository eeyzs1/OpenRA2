# 诊断建筑 SHP 帧结构：每帧尺寸/非空像素，找出帧0异常的根因
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image, ImageDraw

T = MixTree()
_, _p = T.find("unittem.pal"); PAL = load_pal(_p)

names = sys.argv[1:] or ["nalasr", "naflak", "gagcan", "namisl", "gawall"]
for nm in names:
    d = T.find(nm + ".shp")[1]
    if not d:
        print(nm, "MISSING"); continue
    shp = Shp(d)
    print(f"== {nm}: {shp.nframes} frames, canvas {shp.w}x{shp.h}")
    # 帧信息
    for i in range(shp.nframes):
        fr = shp.frame_pixels(i)
        solid = sum(1 for v in fr.pixels if v != 0)
        print(f"  f{i:02d}: {fr.w}x{fr.h} at({fr.x},{fr.y}) solid={solid}")
    # 拼图（前 12 帧）
    cell = 110
    cols = min(shp.nframes, 12)
    m = Image.new("RGBA", (cell * cols, cell + 18), (30, 34, 30, 255))
    dr = ImageDraw.Draw(m)
    for i in range(cols):
        fr = shp.frame_pixels(i)
        if fr.w == 0 or fr.h == 0:
            continue
        img = shp_frame_to_rgba(fr, PAL)
        s = min((cell - 10) / img.width, (cell - 10) / img.height)
        img = img.resize((max(1, round(img.width * s)), max(1, round(img.height * s))), Image.NEAREST)
        m.paste(img, (i * cell + (cell - img.width) // 2, (cell - img.height) // 2), img)
        dr.text((i * cell + 4, cell + 2), f"f{i}", fill=(255, 255, 0, 255))
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", f"diag_bld_{nm}.png")
    m.save(out)
    print("  saved", out)
