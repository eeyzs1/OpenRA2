# 确认：xor80+cameo 是正确解码；中值滤波去椒盐；检查引擎用到的受影响图标
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, shp_frame_to_rgba, ShpFrame
from PIL import Image, ImageFilter

T = MixTree()
_, pc = T.find("cameo.pal"); PAL_C = load_pal(pc)
W, H = 64, 48

def decode(fn):
    _, dd = T.find(fn)
    off = struct.unpack_from("<I", dd, 8 + 20)[0]
    r = bytes(dd[off:off + 3072])
    v = bytes(b ^ 0x80 for b in r)
    f = ShpFrame(); f.x = 0; f.y = 0; f.w = W; f.h = H
    f.pixels = bytearray(v)
    return shp_frame_to_rgba(f, PAL_C, remap=False)

files = ["cnsticon.shp", "empicon.shp", "lpsticon.shp", "npsiicon.shp"]
out = Image.new("RGBA", (len(files) * 2 * 132, 100), (30, 30, 30, 255))
for i, fn in enumerate(files):
    im = decode(fn)
    med = im.filter(ImageFilter.MedianFilter(3))
    out.paste(im.resize((128, 96), Image.NEAREST), (i * 264 + 2, 2), im.resize((128, 96), Image.NEAREST))
    out.paste(med.resize((128, 96), Image.NEAREST), (i * 264 + 134, 2), med.resize((128, 96), Image.NEAREST))
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg75.png"))
print("wrote dbg75.png (per icon: left=raw xor80, right=median3)")

# 引擎现有受影响图标检查
SPR = r"e:\AI_Generated_Projects\OpenRA2\assets\sprites"
for fn in ["icon_bld_conyard.png", "icon_bld_psychicsensor.png", "icon_bld_oilderrick.png"]:
    p = os.path.join(SPR, fn)
    print(fn, "exists:", os.path.exists(p))
