# 调试 icon_bld_conyard 噪声问题：列出候选 cameo 文件并各自渲染对比
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image

T = MixTree()
_, _a = T.find("art.ini")
AT = _a.decode("latin-1", "replace")

m = re.search(r"\[GACNST\](.*?)\n\[", AT, re.S)
print("=== art.ini [GACNST] ===")
print(m.group(1).strip()[:600] if m else "NOT FOUND")

_, _p = T.find("cameo.pal"); PAL_C = load_pal(_p)

cands = ["cnsticon.shp", "gacnsticon.shp", "gacnst.shp", "mcvicon.shp", "amcvicon.shp"]
out = Image.new("RGBA", (len(cands) * 70, 80), (30, 30, 30, 255))
for i, c in enumerate(cands):
    d = T.find(c)[1]
    print(c, "->", "missing" if d is None else f"{len(d)} bytes")
    if d is None:
        continue
    try:
        shp = Shp(d)
        print("   frames:", shp.nframes, "canvas:", shp.w, "x", shp.h)
        fr = shp.frame_pixels(0)
        im = shp_frame_to_rgba(fr, PAL_C, remap=False)
        im = im.resize((64, 64), Image.NEAREST)
        out.paste(im, (i * 70 + 3, 3), im)
    except Exception as ex:
        print("   ERROR:", ex)
out.save(os.path.join(os.path.dirname(__file__), "out", "dbg59_cnst.png"))

# 当前 icon_bld_conyard.png 的直接尺寸/内容统计
p = os.path.join(r"e:\AI_Generated_Projects\OpenRA2\assets\sprites", "icon_bld_conyard.png")
im = Image.open(p)
print("current icon_bld_conyard.png size:", im.size)
