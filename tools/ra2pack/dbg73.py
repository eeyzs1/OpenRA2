# &0x7F 假设 + 平滑度对比
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, shp_frame_to_rgba, ShpFrame
from PIL import Image

T = MixTree()
_, pc = T.find("cameo.pal"); PAL_C = load_pal(pc)
W, H = 64, 48

def smooth(data, pal):
    err = 0
    for y in range(H):
        for x in range(W - 1):
            a = pal[data[y * W + x]]; b = pal[data[y * W + x + 1]]
            err += abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])
    return err

files = ["cnsticon.shp", "empicon.shp", "lpsticon.shp", "npsiicon.shp"]
variants = [("xor80", lambda b: b ^ 0x80), ("mask7f", lambda b: b & 0x7F)]
out = Image.new("RGBA", (len(files) * len(variants) * 132, 100), (30, 30, 30, 255))
col = 0
for i, fn in enumerate(files):
    _, dd = T.find(fn)
    off = struct.unpack_from("<I", dd, 8 + 20)[0]
    r = bytes(dd[off:off + 3072])
    for j, (vn, tf) in enumerate(variants):
        v = bytes(tf(b) for b in r)
        print(f"{fn:16s} {vn:7s} smooth={smooth(v, PAL_C)}")
        f = ShpFrame(); f.x = 0; f.y = 0; f.w = W; f.h = H
        f.pixels = bytearray(v)
        im = shp_frame_to_rgba(f, PAL_C, remap=False).resize((128, 96), Image.NEAREST)
        out.paste(im, (col * 132 + 2, 2), im)
        col += 1
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg73.png"))
print("wrote dbg73.png")
