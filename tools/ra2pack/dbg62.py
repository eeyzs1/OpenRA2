# 完整 24 字节帧头 + 验证 raw 解码
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, shp_frame_to_rgba, ShpFrame
from PIL import Image

T = MixTree()
_, d = T.find("cnsticon.shp")
print("file size:", len(d))
for i in range(2):
    base = 8 + i * 24
    x, y, cx, cy = struct.unpack_from("<HHHH", d, base)
    comp = struct.unpack_from("<I", d, base + 8)[0]
    unk = struct.unpack_from("<I", d, base + 12)[0]
    zero = struct.unpack_from("<I", d, base + 16)[0]
    off = struct.unpack_from("<I", d, base + 20)[0]
    print(f"frame {i}: x={x} y={y} cx={cx} cy={cy} comp={comp:#x} unk={unk} zero={zero} off={off}")
print("expected raw data start:", 8 + 2 * 24)

# 手动 raw 解码 frame0（假设数据紧跟帧头表）
off0 = struct.unpack_from("<I", d, 8 + 20)[0]
print("off0 =", off0)
_, pc = T.find("cameo.pal"); PAL_C = load_pal(pc)

f = ShpFrame(); f.x = 0; f.y = 0; f.w = 64; f.h = 48
f.pixels = bytearray(d[56:56 + 64 * 48])
im1 = shp_frame_to_rgba(f, PAL_C, remap=False).resize((128, 96), Image.NEAREST)

f2 = ShpFrame(); f2.x = 0; f2.y = 0; f2.w = 64; f2.h = 48
f2.pixels = bytearray(d[off0:off0 + 64 * 48])
im2 = shp_frame_to_rgba(f2, PAL_C, remap=False).resize((128, 96), Image.NEAREST)

out = Image.new("RGBA", (270, 100), (30, 30, 30, 255))
out.paste(im1, (2, 2), im1)
out.paste(im2, (136, 2), im2)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg62_raw.png"))
print("wrote dbg62_raw.png")
