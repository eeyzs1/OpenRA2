# RA2 地形/矿/装饰素材批量提取：MIX -> assets/sprites/tile_*.png, overlay_*.png
# 用法: python gen_terrain.py [--report-only]
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, load_pal, Shp
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

T = MixTree()
_, _p = T.find("isotem.pal")
PAL = load_pal(_p)

TILE_W, TILE_H = 64, 32  # 引擎瓦片尺寸（RA2 源 60x30 放大）
report = []

# ------------------------------------------------------------- TMP 地面瓦片
def tmp_render(d, off, W=60, H=30):
    """钻石行宽解包单帧 -> RGBA"""
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load(); p = off
    for y in range(H):
        L = 4 * min(y, H - 1 - y) + 2
        x0 = (W - L) // 2
        for i in range(L):
            v = d[p]; p += 1
            if v:
                r, g, b = PAL[v]
                px[x0 + i, y] = (r, g, b, 255)
    return img

def tmp_single(name):
    """单图 TMP（clear01/sandy01 系列）：像素在 36（20 头 + 16 索引）"""
    _, d = T.find(name)
    if not d:
        return None
    xb, yb, cx, cy, n = struct.unpack_from("<5i", d, 0)
    if xb * yb != 1 or cx != 60 or cy != 30:
        print(f"  !! {name} unexpected hdr {xb}x{yb} {cx}x{cy} n={n}")
        return None
    return tmp_render(d, 36)

def tmp_block(name, idx, base, stride):
    """多图 TMP（rough01/water01 实证布局）：第 idx 帧"""
    _, d = T.find(name)
    if not d or base + idx * stride + 900 > len(d):
        return None
    return tmp_render(d, base + idx * stride)

def fill_diamond_edges(img):
    """60→64 NEAREST 后菱形边缘常留透明缝；用邻域不透明色填 1px，拼地时如一体。"""
    px = img.load()
    w, h = img.size
    misses = []
    for y in range(h):
        for x in range(w):
            if px[x, y][3] >= 128:
                continue
            # 菱形内：距中心的曼哈顿式等距覆盖
            cx, cy = (w - 1) * 0.5, (h - 1) * 0.5
            # 只填菱形轮廓附近的洞，不填画布四角
            dx = abs(x - cx) / (w * 0.5)
            dy = abs(y - cy) / (h * 0.5)
            if dx + dy > 1.15:
                continue
            acc = [0, 0, 0, 0]
            n = 0
            for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, 1), (1, -1), (-1, -1)):
                nx, ny = x + ox, y + oy
                if 0 <= nx < w and 0 <= ny < h and px[nx, ny][3] >= 128:
                    c = px[nx, ny]
                    for i in range(4):
                        acc[i] += c[i]
                    n += 1
            if n:
                misses.append((x, y, tuple(v // n for v in acc)))
    for x, y, c in misses:
        px[x, y] = c
    return img

def save_tile(img, terrain, variant):
    # NEAREST：保持 RA2 TMP 锐利像素，避免 LANCZOS 把草地糊成泥色
    img = img.resize((TILE_W, TILE_H), Image.NEAREST)
    img = fill_diamond_edges(img)
    img.save(os.path.join(SPR, f"tile_{terrain}_{variant}.png"), "PNG")
    report.append(f"tile_{terrain}_{variant}")

# clear: clear01 + clear01a..g（8 个单图文件）
clear_files = ["clear01.tem"] + [f"clear01{c}.tem" for c in "abcdefg"]
for v, fn in enumerate(clear_files):
    im = tmp_single(fn)
    if im:
        save_tile(im, "clear", v)
    else:
        print("  MISS clear", fn)

# 多图 TMP 实证布局（hexdump 验证）：int32[20] = 第二帧结构体绝对偏移，
# 首帧像素 base = idx0 - 1800，帧步长 1852（900 像素 + 952 Z/结构填充）
def tmp_base(name):
    _, d = T.find(name)
    if not d:
        return None, 0, 0
    idx0 = struct.unpack_from("<i", d, 20)[0]
    base = idx0 - 1800
    nfr = (len(d) - base - 900) // 1852 + 1
    return d, base, nfr

def montage(frames, path, cols=8):
    """frames: [(label, img)] -> 带序号蒙太奇（放大2x）"""
    from PIL import ImageDraw
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 64, rows * 34), (40, 40, 48, 255))
    dr = ImageDraw.Draw(sheet)
    for i, (lab, im) in enumerate(frames):
        x, y = (i % cols) * 64 + 2, (i // cols) * 34 + 2
        sheet.paste(im, (x, y), im)
        dr.text((x, y), lab, fill=(255, 80, 80, 255))
    sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
    sheet.save(path)

# rough: rough01.tem 真·泥草地面；只用前段平坦帧（后段常是坡/坎，成片会像「蛋格小山」）
d_r, base_r, nfr_r = tmp_base("rough01.tem")
ROUGH_PICK = [0, 1, 2, 3, 4, 5, 6, 7]
rough_frames = []
for v, fi in enumerate(ROUGH_PICK):
    im = tmp_render(d_r, base_r + fi * 1852)
    save_tile(im, "rough", v)
    rough_frames.append((f"r{fi}", im))
montage(rough_frames, os.path.join(OUT, "gen_rough.png"), 8)
# 全 28 帧参考
refs = [(str(i), tmp_render(d_r, base_r + i * 1852)) for i in range(nfr_r)]
montage(refs, os.path.join(OUT, "gen_rough01set.png"), 7)
print(f"rough variants: {len(ROUGH_PICK)} (from rough01.tem {nfr_r} frames, base={base_r})")

# water: water01 + water02 各 4 帧（base=84 stride=1852，hexdump+目检验证正确）
wv = 0
water_frames = []
for fn in ["water01.tem", "water02.tem"]:
    for i in range(4):
        im = tmp_block(fn, i, 84, 1852)
        if im:
            save_tile(im, "water", wv)
            water_frames.append((f"w{wv}", im)); wv += 1
montage(water_frames, os.path.join(OUT, "gen_water.png"), 8)
print(f"water variants: {wv}")

def tmp_frame(fn, fi):
    """通用帧提取：单图(36 头) 或 多图(tmp_base 布局) TMP 的第 fi 帧"""
    _, d = T.find(fn)
    if not d:
        return None
    xb, yb, cx, cy, n = struct.unpack_from("<5i", d, 0)
    if xb * yb == 1 and cx == 60 and cy == 30:
        return tmp_render(d, 36) if fi == 0 else None
    idx0 = struct.unpack_from("<i", d, 20)[0]
    base = idx0 - 1800
    off = base + fi * 1852
    if off + 900 > len(d):
        return None
    return tmp_render(d, off)

# shore: 岸线过渡瓦片，按"邻水方向 mask"组织（probe_shore_scan.py 全量扫描 214 帧自动分类+目检确认）
# mask bit: 1=+x邻水 2=+y邻水 4=-x邻水 8=-y邻水；每种 mask 2 个变体
# 文件名 tile_shore_m{mask}_{v}.png，供 bakeTerrain 按实际邻水关系选取
SHORE_MASK_PICKS = {
    0b0001: [("shore01.tem", 3), ("shore02.tem", 3)],   # 直岸 水在 +x
    0b0010: [("shore41.tem", 17), ("shore07.tem", 1)],  # 直岸 水在 +y
    0b0100: [("shore41.tem", 13), ("shore15.tem", 0)],  # 直岸 水在 -x
    0b1000: [("shore31.tem", 0), ("shore25.tem", 2)],   # 直岸 水在 -y
    0b0011: [("shore41.tem", 7), ("shore13.tem", 1)],   # 底角 水在 +x+y
    0b0110: [("shore22.tem", 2), ("shore41.tem", 14)],  # 左角 水在 +y-x
    0b1100: [("shore38.tem", 0), ("shore25.tem", 0)],   # 顶角 水在 -x-y
    0b1001: [("shore05.tem", 5), ("shore30.tem", 3)],   # 右角 水在 +x-y
    0b0101: [("shore35.tem", 1), ("shore06.tem", 5)],   # 海峡 水在 +x-x
    0b1010: [("shore13.tem", 5), ("shore11.tem", 1)],   # 海峡 水在 +y-y
    0b0111: [("shore15.tem", 3), ("shore12.tem", 1)],   # 半岛 陆角 -y
    0b1011: [("shore07.tem", 2), ("shore02.tem", 2)],   # 半岛 陆角 -x
    0b1101: [("shore05.tem", 4), ("shore36.tem", 1)],   # 半岛 陆角 +y
    0b1110: [("shore22.tem", 1), ("shore22.tem", 0)],   # 半岛 陆角 +x
    0b1111: [("shore41.tem", 20), ("shore14.tem", 2)],  # 岛 四面环水
}
shore_frames = []
for mask, picks in SHORE_MASK_PICKS.items():
    for v, (fn, fi) in enumerate(picks):
        im = tmp_frame(fn, fi)
        if not im:
            print(f"  MISS shore m{mask} {fn}#{fi}")
            continue
        img = im.resize((TILE_W, TILE_H), Image.LANCZOS)
        img.save(os.path.join(SPR, f"tile_shore_m{mask}_{v}.png"), "PNG")
        report.append(f"tile_shore_m{mask}_{v}")
        shore_frames.append((f"m{mask}v{v}", im))
montage(shore_frames, os.path.join(OUT, "gen_shore.png"), 6)
print(f"shore variants: {len(shore_frames)} (15 masks x 2)")

# ------------------------------------------------------------- cliff / ramp（高度差侧立面）
# dir: 0=+x低 1=+y低 2=-x低 3=-y低；从 temperate cliff/ramp TMP 取代表性帧
CLIFF_PICKS = {
    0: [("cliff01.tem", 0), ("cliff08.tem", 0)],
    1: [("cliff12.tem", 0), ("cliff15.tem", 0)],
    2: [("cliff22.tem", 0), ("cliff25.tem", 0)],
    3: [("cliff31.tem", 0), ("cliff36.tem", 0)],
}
cliff_frames = []
for d, picks in CLIFF_PICKS.items():
    for v, (fn, fi) in enumerate(picks):
        im = tmp_frame(fn, fi)
        if not im:
            # 退化：用 rough 帧裁成侧立条
            im = tmp_frame("rough01.tem", min(4 + d, 20))
        if not im:
            print(f"  MISS cliff n{d} {fn}#{fi}")
            continue
        img = im.resize((TILE_W, max(TILE_H, TILE_H + 16)), Image.NEAREST)
        img = fill_diamond_edges(img) if img.size == (TILE_W, TILE_H) else img
        outp = os.path.join(SPR, f"tile_cliff_n{d}_{v}.png")
        img.save(outp, "PNG")
        report.append(f"tile_cliff_n{d}_{v}")
        cliff_frames.append((f"c{d}v{v}", im))
montage(cliff_frames, os.path.join(OUT, "gen_cliff.png"), 4)
print(f"cliff variants: {len(cliff_frames)}")

RAMP_PICKS = [("ramp01.tem", 0), ("ramp02.tem", 0)]
ramp_frames = []
for v, (fn, fi) in enumerate(RAMP_PICKS):
    im = tmp_frame(fn, fi)
    if not im:
        im = tmp_frame("shore01.tem", 0)
    if not im:
        print(f"  MISS ramp {fn}")
        continue
    img = im.resize((TILE_W, TILE_H), Image.NEAREST)
    img = fill_diamond_edges(img)
    img.save(os.path.join(SPR, f"tile_ramp_{v}.png"), "PNG")
    report.append(f"tile_ramp_{v}")
    ramp_frames.append((f"ramp{v}", im))
montage(ramp_frames, os.path.join(OUT, "gen_ramp.png"), 4)
print(f"ramp variants: {len(ramp_frames)}")

# ------------------------------------------------------------- SHP 矿脉/彩矿
def shp_frame(shp, i):
    fr = shp.frame_pixels(i)
    if fr.w == 0 or fr.h == 0:
        return None
    img = Image.new("RGBA", (fr.w, fr.h), (0, 0, 0, 0))
    out = img.load()
    for y in range(fr.h):
        for x in range(fr.w):
            v = fr.pixels[y * fr.w + x]
            if v:
                r, g, b = PAL[v]
                out[x, y] = (r, g, b, 255)
    return img, (fr.x, fr.y)

def last_frame_content(stem):
    """SHP 中不透明像素最多的帧（矿脉最满），返回裁剪内容"""
    _, sd = T.find(stem + ".tem")
    if not sd:
        return None
    shp = Shp(sd)
    best_img, best_a = None, -1
    for i in range(shp.nframes):
        r = shp_frame(shp, i)
        if not r:
            continue
        fim, _ = r
        a = sum(1 for p in fim.getdata() if p[3] > 60)
        if a > best_a:
            best_img, best_a = fim, a
    if not best_img:
        return None
    bb = best_img.getbbox()
    return best_img.crop(bb) if bb else None

def save_crystal(content, terrain, variant):
    """晶体放入 64x32 画布：限缩放至钻石带内，底边对齐 y=26"""
    if content is None:
        return
    cw, chh = content.size
    s = min(50.0 / cw, 22.0 / chh, 1.9)
    im = content.resize((max(1, round(cw * s)), max(1, round(chh * s))), Image.LANCZOS)
    cv = Image.new("RGBA", (TILE_W, TILE_H), (0, 0, 0, 0))
    cv.paste(im, ((TILE_W - im.width) // 2, 26 - im.height), im)
    cv.save(os.path.join(SPR, f"tile_{terrain}_{variant}.png"), "PNG")
    report.append(f"tile_{terrain}_{variant}")

for v in range(8):
    save_crystal(last_frame_content(f"tib{v + 1:02d}"), "ore", v)
for v in range(8):
    save_crystal(last_frame_content(f"gem{v + 1:02d}"), "gems", v)

# ------------------------------------------------------------- 树木/岩石（主帧 + 阴影帧合成）
def compose_overlay(stem):
    _, sd = T.find(stem + ".tem")
    if not sd:
        return None
    shp = Shp(sd)
    big = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
    r1 = shp_frame(shp, 1)
    if r1:  # 阴影帧：非零索引 -> 暗色半透明
        fim, (fx, fy) = r1
        sh = Image.new("RGBA", fim.size, (0, 0, 0, 0))
        sp = sh.load(); ip = fim.load()
        for y in range(fim.height):
            for x in range(fim.width):
                if ip[x, y][3]:
                    sp[x, y] = (0, 0, 0, 96)
        big.paste(sh, (fx, fy), sh)
    r0 = shp_frame(shp, 0)
    if not r0:
        return None
    fim, (fx, fy) = r0
    big.paste(fim, (fx, fy), fim)
    bb = big.getbbox()
    return big.crop(bb) if bb else None

OVERLAYS = {
    "tree1": "tree08",  # 圆形阔叶（大）
    "tree2": "tree03",  # 中型阔叶
    "tree3": "tree26",  # 针叶松
    "rock1": "trock02",
    "rock2": "trock04",
}
for eng, stem in OVERLAYS.items():
    im = compose_overlay(stem)
    if im:
        im.save(os.path.join(SPR, f"overlay_{eng}.png"), "PNG")
        report.append(f"overlay_{eng}({stem} {im.width}x{im.height})")
    else:
        print("  MISS overlay", eng, stem)

print(f"== {len(report)} assets ==")
for r in report:
    print(" ", r)
