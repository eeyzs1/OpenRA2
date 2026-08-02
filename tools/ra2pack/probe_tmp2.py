# TMP 格式实证解析：打印头/索引/各区偏移，渲染钻石主体与 extra 区（树/矿含钻出部分）
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image

T = MixTree()
_, pald = T.find("isotem.pal")
PAL = load_pal(pald)

def info(name):
    mix, d = T.find(name)
    if not d:
        print(name, "MISS"); return None
    xb, yb, cx, cy, nimg = struct.unpack_from("<5i", d, 0)
    print(f"{name} [{mix}] size={len(d)} blocks={xb}x{yb} img={cx}x{cy} nimg={nimg}")
    base = 20 + 16 * nimg
    for i in range(min(nimg, 6)):
        x, y, eo, zo = struct.unpack_from("<4i", d, 20 + i * 16)
        print(f"  img{i}: cell=({x},{y}) extra_ofs={eo} z_ofs={zo} (pix_base={base})")
    return d

# 看 clear01/tib01/tree01 的索引数值，推断 extra_ofs/z_ofs 基准
for n in ["clear01.tem", "water01.tem", "tib01.tem", "tree01.tem", "rough01.tem"]:
    info(n)
