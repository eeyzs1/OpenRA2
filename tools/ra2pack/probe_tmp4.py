# 树木/岩石全量 montage：frame0 主图 + frame1 阴影合成预览，供挑选
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, Shp
from PIL import Image

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)
OUT = os.path.join(os.path.dirname(__file__), "out")

def frame_rgba(shp, i):
    fr = shp.frame_pixels(i)
    if fr.w == 0 or fr.h == 0:
        return None
    img = Image.new("RGBA", (fr.w, fr.h), (0, 0, 0, 0))
    out = img.load()
    for y in range(fr.h):
        for x in range(fr.w):
            v = fr.pixels[y * fr.w + x]
            if v:
                r, g, b = PAL[v]
                out[x, y] = (r, g, b, 255)
    return img, (fr.x, fr.y)

def compose(shp):
    """frame0 主图 + frame1 阴影(暗色半透明)，按 SHP 画布坐标合成，返回裁剪后 RGBA"""
    big = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
    r1 = frame_rgba(shp, 1)
    if r1:  # 阴影帧：任何非零索引 -> 暗色半透明
        fim, (fx, fy) = r1
        sh = Image.new("RGBA", fim.size, (0, 0, 0, 0))
        sp = sh.load(); ip = fim.load()
        for y in range(fim.height):
            for x in range(fim.width):
                if ip[x, y][3]:
                    sp[x, y] = (0, 0, 0, 96)
        big.paste(sh, (fx, fy), sh)
    r0 = frame_rgba(shp, 0)
    if not r0:
        return None
    fim, (fx, fy) = r0
    big.paste(fim, (fx, fy), fim)
    bb = big.getbbox()
    return big.crop(bb) if bb else None

def montage(stems, out_name, cols=7):
    cells = []
    for s in stems:
        _, sd = T.find(s + ".tem")
        if not sd:
            cells.append((s, None)); continue
        shp = Shp(sd)
        cells.append((s, compose(shp)))
    cw = 96; chh = 130
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * cw, rows * chh), (34, 34, 40, 255))
    from PIL import ImageDraw
    dr = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(cells):
        x0 = (i % cols) * cw; y0 = (i // cols) * chh
        if im:
            s = min((cw - 8) / im.width, (chh - 18) / im.height, 1.0)
            im2 = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.LANCZOS) if s < 1.0 else im
            sheet.paste(im2, (x0 + (cw - im2.width) // 2, y0 + chh - 14 - im2.height), im2)
        dr.text((x0 + 4, y0 + chh - 12), name, fill=(255, 220, 120, 255))
    sheet.save(os.path.join(OUT, out_name))
    print(out_name, len(cells))

montage([f"tree{ i:02d}" for i in range(1, 29)], "tmp4_trees.png")
montage([f"trock0{i}" for i in range(1, 6)] + [f"srock0{i}" for i in range(1, 6)], "tmp4_rocks.png", cols=5)
