# 最终对比：conyard/psychicsensor 图标的 (a) xor80+median vs (b) 建筑 sprite 合成
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, shp_frame_to_rgba, ShpFrame, Shp
from PIL import Image, ImageFilter

T = MixTree()
_, pc = T.find("cameo.pal"); PAL_C = load_pal(pc)
_, pu = T.find("unittem.pal"); PAL_U = load_pal(pu)

def cameo_decode(fn):
    _, dd = T.find(fn)
    off = struct.unpack_from("<I", dd, 8 + 20)[0]
    r = bytes(dd[off:off + 3072])
    v = bytes(b ^ 0x80 for b in r)
    f = ShpFrame(); f.x = 0; f.y = 0; f.w = 64; f.h = 48
    f.pixels = bytearray(v)
    im = shp_frame_to_rgba(f, PAL_C, remap=False)
    return im.filter(ImageFilter.MedianFilter(3)).resize((108, 84), Image.LANCZOS)

def synth_from_bld(shpname):
    _, sd = T.find(shpname)
    shp = Shp(sd)
    n = shp.nframes
    idxs = [0]
    f0 = shp.frame_pixels(0)
    a0 = f0.w * f0.h
    for i in range(1, n):
        fi = shp.frame_pixels(i)
        if fi.w * fi.h < a0 * 0.4:
            idxs.append(i)
            break
    big = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
    for i in idxs:
        fr = shp.frame_pixels(i)
        if fr.w == 0:
            continue
        fi = shp_frame_to_rgba(fr, PAL_U, remap=False)
        big.paste(fi, (fr.x, fr.y), fi)
    content = big.crop(big.getbbox())
    s = min(100 / content.width, 76 / content.height)
    ic = content.resize((round(content.width * s), round(content.height * s)), Image.LANCZOS)
    cv = Image.new("RGBA", (108, 84), (96, 132, 168, 255))
    cv.paste(ic, ((108 - ic.width) // 2, (84 - ic.height) // 2), ic)
    return cv

pairs = [("cnsticon.shp", "gacnst.shp"), ("npsiicon.shp", "napsis.shp")]
out = Image.new("RGBA", (4 * 112, 88), (30, 30, 30, 255))
for i, (ic, bs) in enumerate(pairs):
    a = cameo_decode(ic)
    b = synth_from_bld(bs)
    out.paste(a, (i * 224 + 2, 2), a)
    out.paste(b, (i * 224 + 114, 2), b)
out = out.resize((out.width * 2, out.height * 2), Image.NEAREST)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg76.png"))
print("wrote dbg76.png  [conyard: cameo|synth] [psychicsensor: cameo|synth]")
