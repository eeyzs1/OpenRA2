# 最后一批变换 + 回退方案验证（建筑 sprite 合成图标）
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, shp_frame_to_rgba, ShpFrame, Shp
from PIL import Image

T = MixTree()
_, d = T.find("cnsticon.shp")
raw = bytes(d[56:56 + 3072])
W, H = 64, 48
_, pc = T.find("cameo.pal"); PAL_C = load_pal(pc)

variants = [
    ("shr2", bytes(b >> 2 for b in raw)),
    ("mask3f", bytes(b & 0x3F for b in raw)),
    ("xor80", bytes(b ^ 0x80 for b in raw)),
    ("sub80", bytes((b - 0x80) & 0xFF for b in raw)),
]
out = Image.new("RGBA", (len(variants) * 132, 100), (30, 30, 30, 255))
for i, (nm, v) in enumerate(variants):
    f = ShpFrame(); f.x = 0; f.y = 0; f.w = W; f.h = H
    f.pixels = bytearray(v)
    im = shp_frame_to_rgba(f, PAL_C, remap=False).resize((128, 96), Image.NEAREST)
    out.paste(im, (i * 132 + 2, 2), im)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg71a.png"))

# 回退方案：gacnst.shp 帧0 -> unittem.pal -> 缩放到 60x48 -> 108x84
_, gd = T.find("gacnst.shp")
shp = Shp(gd)
fr = shp.frame_pixels(0)
_, pu = T.find("unittem.pal"); PAL_U = load_pal(pu)
im = shp_frame_to_rgba(fr, PAL_U, remap=False)
print("gacnst f0 size:", im.size)
big = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
big.paste(im, (fr.x, fr.y), im)
bbox = big.getbbox()
content = big.crop(bbox)
print("content bbox:", content.size)
# 放入 60x48（cameo 比例），再放大到 108x84
s = min(60 / content.width, 48 / content.height)
ic = content.resize((round(content.width * s), round(content.height * s)), Image.LANCZOS)
cv = Image.new("RGBA", (60, 48), (90, 130, 170, 255))  # 天空底
cv.paste(ic, ((60 - ic.width) // 2, (48 - ic.height) // 2), ic)
final = cv.resize((108, 84), Image.LANCZOS)
final.save(os.path.join(os.path.dirname(__file__), "out", "dbg71b_fallback.png"))
print("wrote dbg71a.png / dbg71b_fallback.png")
