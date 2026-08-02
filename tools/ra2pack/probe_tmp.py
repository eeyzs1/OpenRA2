# TMP 解析验证：读 clear01a.tem / tree01.tem / tib01.tem 渲染为 PNG
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal
from PIL import Image

T = MixTree()
_, pald = T.find("isotem.pal")
if not pald:
    _, pald = T.find("isotem.pal".replace("isotem", "temperat"))
print("pal:", len(pald) if pald else None)
PAL = load_pal(pald)

def parse_tmp(data):
    xb, yb, cx, cy, nimg = struct.unpack_from("<5i", data, 0)
    print(f"  hdr: {xb}x{yb} blocks, img {cx}x{cy}, nimg={nimg}, filesize={len(data)}")
    imgs = []
    for i in range(nimg):
        x, y, extra_ofs, z_ofs = struct.unpack_from("<4i", data, 20 + i * 16)
        imgs.append((x, y, extra_ofs, z_ofs))
    pix_base = 20 + 16 * nimg
    return xb, yb, cx, cy, nimg, imgs, pix_base

def render_tmp(name, out):
    mix, data = T.find(name)
    if not data:
        print(name, "MISS"); return
    print(f"{name} from {mix}")
    xb, yb, cx, cy, nimg, imgs, pix_base = parse_tmp(data)
    # 单帧渲染（每帧 cx*cy 原始调色板索引，0=透明）
    frames = []
    for i in range(min(nimg, 24)):
        off = pix_base + i * cx * cy
        if off + cx * cy > len(data):
            break
        img = Image.new("RGBA", (cx, cy), (0, 0, 0, 0))
        px = img.load()
        d = data[off:off + cx * cy]
        for yy in range(cy):
            for xx in range(cx):
                v = d[yy * cx + xx]
                if v:
                    r, g, b = PAL[v]
                    px[xx, yy] = (r, g, b, 255)
        frames.append(img)
    # 横向拼接输出
    W = sum(f.width for f in frames) + 4 * len(frames)
    H = max(f.height for f in frames)
    sheet = Image.new("RGBA", (W, H), (30, 30, 36, 255))
    x = 0
    for f in frames:
        sheet.paste(f, (x, 0), f)
        x += f.width + 4
    sheet.save(out)
    print(f"  -> {out} ({len(frames)} frames)")

os.makedirs("out", exist_ok=True)
render_tmp("clear01a.tem", "out/tmp_clear01a.png")
render_tmp("clear01.tem", "out/tmp_clear01.png")
render_tmp("rough01.tem", "out/tmp_rough01.png")
render_tmp("water01.tem", "out/tmp_water01.png")
render_tmp("tib01.tem", "out/tmp_tib01.png")
render_tmp("gem01.tem", "out/tmp_gem01.png")
render_tmp("tree01.tem", "out/tmp_tree01.png")
render_tmp("tree05.tem", "out/tmp_tree05.png")
