# comp=0 数据变换穷举：xor-delta / add-delta / 反相 / 半字节交换 / 位反转 / LCW
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, shp_frame_to_rgba, ShpFrame
from PIL import Image

T = MixTree()
_, pc = T.find("cameo.pal"); PAL_C = load_pal(pc)
_, d = T.find("cnsticon.shp")
raw = bytes(d[56:56 + 3072])
W, H = 64, 48

def adj_corr(data):
    """水平相邻像素相等的比例"""
    eq = 0; tot = 0
    for y in range(H):
        for x in range(W - 1):
            tot += 1
            if data[y * W + x] == data[y * W + x + 1]:
                eq += 1
    return eq / tot

def xor_delta(data):
    out = bytearray(len(data))
    for y in range(H):
        prev = 0
        for x in range(W):
            i = y * W + x
            prev = out[i] = data[i] ^ prev
    return bytes(out)

def add_delta(data):
    out = bytearray(len(data))
    for y in range(H):
        prev = 0
        for x in range(W):
            i = y * W + x
            prev = out[i] = (data[i] + prev) & 0xFF
    return bytes(out)

def sub_delta(data):
    out = bytearray(len(data))
    for y in range(H):
        prev = 0
        for x in range(W):
            i = y * W + x
            prev = out[i] = (data[i] - prev) & 0xFF
    return bytes(out)

def bitrev(b):
    return int(f"{b:08b}"[::-1], 2)

variants = [
    ("raw", raw),
    ("xor_delta", xor_delta(raw)),
    ("add_delta", add_delta(raw)),
    ("sub_delta", sub_delta(raw)),
    ("invert", bytes(b ^ 0xFF for b in raw)),
    ("nibswap", bytes(((b & 0xF) << 4) | (b >> 4) for b in raw)),
    ("bitrev", bytes(bitrev(b) for b in raw)),
]
for nm, v in variants:
    print(f"{nm:10s} adj_eq={adj_corr(v):.3f}")

out = Image.new("RGBA", (len(variants) * 132, 100), (30, 30, 30, 255))
for i, (nm, v) in enumerate(variants):
    f = ShpFrame(); f.x = 0; f.y = 0; f.w = W; f.h = H
    f.pixels = bytearray(v)
    im = shp_frame_to_rgba(f, PAL_C, remap=False).resize((128, 96), Image.NEAREST)
    out.paste(im, (i * 132 + 2, 2), im)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg67_var.png"))
print("wrote dbg67_var.png")

# 对照：正常 cameo 的 adj_eq
_, dz = T.find("zepicon.shp")
off = struct.unpack_from("<I", dz, 8 + 20)[0]
zraw = bytes(dz[off:off + 60 * 48])
eq = sum(1 for y in range(48) for x in range(59) if zraw[y * 60 + x] == zraw[y * 60 + x + 1])
print("zepicon adj_eq:", eq / (48 * 59))
