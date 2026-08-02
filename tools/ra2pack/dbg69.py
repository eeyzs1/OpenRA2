# 测试转置/灰度/调色板变体
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, shp_frame_to_rgba, ShpFrame
from PIL import Image

T = MixTree()
_, d = T.find("cnsticon.shp")
raw = d[56:56 + 3072]
W, H = 64, 48

def adj_eq(data, w, h, horizontal=True):
    eq = 0; tot = 0
    if horizontal:
        for y in range(h):
            for x in range(w - 1):
                tot += 1
                if data[y * w + x] == data[y * w + x + 1]:
                    eq += 1
    else:
        for y in range(h - 1):
            for x in range(w):
                tot += 1
                if data[y * w + x] == data[(y + 1) * w + x]:
                    eq += 1
    return eq / tot

print("raw horiz adj:", adj_eq(raw, W, H, True))
print("raw vert  adj:", adj_eq(raw, W, H, False))

# 转置: data[y*W+x] -> out[x*H+y] (列主序存储 -> 48x64)
trans = bytearray(3072)
for y in range(H):
    for x in range(W):
        trans[x * H + y] = raw[y * W + x]
print("transposed-as-48x64 horiz adj:", adj_eq(bytes(trans), H, W, True))

_, pc = T.find("cameo.pal"); PAL_C = load_pal(pc)

variants = []
# 1) 灰度
gray = [(i, i, i) for i in range(256)]
variants.append(("gray", gray, raw, W, H))
# 2) 转置 + cameo
variants.append(("transp_cameo", PAL_C, bytes(trans), H, W))
# 3) 转置 + 灰度
variants.append(("transp_gray", gray, bytes(trans), H, W))

out = Image.new("RGBA", (len(variants) * 132, 100), (30, 30, 30, 255))
for i, (nm, pal, dat, w, h) in enumerate(variants):
    f = ShpFrame(); f.x = 0; f.y = 0; f.w = w; f.h = h
    f.pixels = bytearray(dat)
    im = shp_frame_to_rgba(f, pal, remap=False).resize((128, 96), Image.NEAREST)
    out.paste(im, (i * 132 + 2, 2), im)
    print(i, nm)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg69.png"))
print("wrote dbg69.png")
