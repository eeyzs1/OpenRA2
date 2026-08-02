# 诊断 dlph.shp / e1.shp 帧内容 + 调色板 remap 分布
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image, ImageDraw

T = MixTree()
_, _p = T.find("unittem.pal"); PAL = load_pal(_p)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

def dump_shp(name, nshow=16):
    d = T.find(name + ".shp")[1]
    if not d:
        print(f"{name}.shp 不存在"); return
    shp = Shp(d)
    print(f"{name}.shp: {shp.nframes} 帧, 画布 {shp.w}x{shp.h}")
    cell = 80
    cols = 8
    rows = (min(nshow, shp.nframes) + cols - 1) // cols
    m = Image.new("RGB", (cols * cell, rows * (cell + 12)), (30, 30, 34))
    dr = ImageDraw.Draw(m)
    for i in range(min(nshow, shp.nframes)):
        fr = shp.frame_pixels(i)
        x0, y0 = (i % cols) * cell, (i // cols) * (cell + 12)
        if fr.w and fr.h:
            # 统计 remap 像素占比
            remap = sum(1 for v in fr.pixels if 16 <= v <= 31)
            total = sum(1 for v in fr.pixels if v != 0)
            img = shp_frame_to_rgba(fr, PAL, remap=True)
            s = min((cell - 6) / img.width, (cell - 6) / img.height, 3.0)
            img = img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.NEAREST)
            bg = Image.new("RGBA", (cell, cell), (52, 66, 48, 255))
            bg.paste(img, ((cell - img.width) // 2, (cell - img.height) // 2), img)
            m.paste(bg, (x0, y0))
            dr.text((x0 + 2, y0 + cell), f"f{i} {fr.w}x{fr.h} r{remap*100//max(1,total)}%", fill=(230, 230, 230))
        else:
            dr.text((x0 + 2, y0 + cell), f"f{i} 空", fill=(200, 80, 80))
    m.save(os.path.join(OUT, f"shp_{name}.png"))
    print(f"  -> out/shp_{name}.png")

dump_shp("dlph", 16)
dump_shp("e1", 16)   # GI 注意 rules Image=gi? audit 显示 gi->E1 Image=gi
dump_shp("gi", 16)
dump_shp("cons", 16)
dump_shp("tany", 16)
dump_shp("sqd", 16)  # 乌贼对照（海军 SHP）
