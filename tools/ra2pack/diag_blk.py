# 验证 sqd.shp 结构（296帧 = 8方向 x 37帧?）：渲染每方向块首帧 + dron/adog 结构
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image, ImageDraw

T = MixTree()
_, _p = T.find("unittem.pal"); PAL = load_pal(_p)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

def dump(name, frames, tag, cols=8):
    d = T.find(name + ".shp")[1]
    if not d:
        print(name, "不存在"); return
    shp = Shp(d)
    cell = 150
    rows = (len(frames) + cols - 1) // cols
    m = Image.new("RGB", (cols * cell, rows * (cell + 16)), (30, 30, 34))
    dr = ImageDraw.Draw(m)
    for k, i in enumerate(frames):
        if i >= shp.nframes: break
        fr = shp.frame_pixels(i)
        x0, y0 = (k % cols) * cell, (k // cols) * (cell + 16)
        if fr.w and fr.h:
            img = shp_frame_to_rgba(fr, PAL, remap=True)
            s = min((cell - 10) / img.width, (cell - 10) / img.height)
            img = img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.NEAREST)
            bg = Image.new("RGBA", (cell, cell), (52, 66, 48, 255))
            bg.paste(img, ((cell - img.width) // 2, (cell - img.height) // 2), img)
            m.paste(bg, (x0, y0))
        dr.text((x0 + 4, y0 + cell + 2), f"f{i}", fill=(240, 240, 240))
    m.save(os.path.join(OUT, f"blk_{tag}.png"))
    print(f"-> out/blk_{tag}.png  ({shp.nframes} frames)")

# sqd: 8 方向块首帧（37帧/方向假设）
dump("sqd", [i * 37 for i in range(8)], "sqd_blocks")
# dron 恐怖机器人结构
dump("dron", list(range(0, 40)), "dron_0_40")
# adog 狗结构
dump("adog", list(range(0, 40)), "adog_0_40")
