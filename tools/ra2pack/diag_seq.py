# 渲染 SHP 帧序列 f0..f71，识别人形/海军单位的方向帧结构
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image, ImageDraw

T = MixTree()
_, _p = T.find("unittem.pal"); PAL = load_pal(_p)
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

def dump_range(name, start, count):
    d = T.find(name + ".shp")[1]
    if not d:
        print(f"{name}.shp 不存在"); return
    shp = Shp(d)
    cell = 90
    cols = 8
    rows = (count + cols - 1) // cols
    m = Image.new("RGB", (cols * cell, rows * (cell + 13)), (30, 30, 34))
    dr = ImageDraw.Draw(m)
    for k in range(count):
        i = start + k
        if i >= shp.nframes: break
        fr = shp.frame_pixels(i)
        x0, y0 = (k % cols) * cell, (k // cols) * (cell + 13)
        if fr.w and fr.h:
            img = shp_frame_to_rgba(fr, PAL, remap=True)
            s = min((cell - 6) / img.width, (cell - 6) / img.height, 3.0)
            img = img.resize((max(1, int(img.width * s)), max(1, int(img.height * s))), Image.NEAREST)
            bg = Image.new("RGBA", (cell, cell), (52, 66, 48, 255))
            bg.paste(img, ((cell - img.width) // 2, (cell - img.height) // 2), img)
            m.paste(bg, (x0, y0))
            dr.text((x0 + 2, y0 + cell), f"f{i} {fr.w}x{fr.h}", fill=(230, 230, 230))
        else:
            dr.text((x0 + 2, y0 + cell), f"f{i} 空", fill=(200, 80, 80))
    fn = os.path.join(OUT, f"seq_{name}_{start}.png")
    m.save(fn)
    print(f"  -> {fn}")

dump_range("gi", 0, 64)    # GI 结构
dump_range("cons", 0, 64)  # 动员兵结构
dump_range("dlph", 0, 32)  # 海豚
dump_range("sqd", 0, 32)   # 乌贼
