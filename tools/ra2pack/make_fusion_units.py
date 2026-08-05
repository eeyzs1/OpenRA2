# 融合单位美术：在已提取的 RA2 像素风格上做可辨认改款（非官方 MIX）
# PLA ← 动员兵；99式 ← 犀牛。保持 remap 红(≈255,0,0) 与等距比例。
# 用法: python make_fusion_units.py
# Prefer: tools/asset_pipeline (prepare → publish → QA). See docs/asset-pipeline.md.
import os
import subprocess
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SPR = ROOT / "assets" / "sprites"

# RA2 house remap 占位红（引擎 runtime remap）
def is_remap(r, g, b, a):
    return a > 40 and r >= 200 and g < 80 and b < 80

def is_skin(r, g, b, a):
    return a > 40 and r > 160 and g > 110 and b > 80 and abs(r - g) < 70

def is_khaki(r, g, b, a):
    # 动员兵裤：偏黄褐
    return a > 40 and not is_remap(r, g, b, a) and r > 100 and g > 70 and b < 90 and r >= g >= b

def is_helmet_grey(r, g, b, a):
    return a > 40 and not is_remap(r, g, b, a) and abs(r - g) < 25 and abs(g - b) < 30 and 70 < r < 180 and b >= g - 10

def clamp(x):
    return max(0, min(255, int(x)))

def pla_pixel(r, g, b, a, y, h):
    if a < 8:
        return (r, g, b, a)
    if is_remap(r, g, b, a):
        return (r, g, b, a)  # 保留阵营红
    # 上半头盔区：钢盔 → 橄榄绿软帽/作训帽
    if y < h * 0.38 and is_helmet_grey(r, g, b, a):
        return (clamp(g * 0.55 + 28), clamp(g * 0.72 + 36), clamp(b * 0.35 + 18), a)
    # 卡其裤 → 草绿作训裤
    if is_khaki(r, g, b, a):
        return (clamp(r * 0.35 + 28), clamp(g * 0.55 + 48), clamp(b * 0.30 + 22), a)
    # 靴/枪深色略压暗
    if r + g + b < 140 and a > 40:
        return (clamp(r * 0.85), clamp(g * 0.9), clamp(b * 0.85), a)
    # 中间灰布 → 略偏绿灰
    if abs(r - g) < 20 and abs(g - b) < 20 and 60 < r < 160:
        return (clamp(r * 0.75), clamp(g * 0.88 + 8), clamp(b * 0.70), a)
    return (r, g, b, a)

def stylize_pla(im: Image.Image) -> Image.Image:
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    op = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            op[x, y] = pla_pixel(r, g, b, a, y, h)
    # 帽檐：在头顶不透明区最上沿加 1px 深绿沿，破动员兵钢盔轮廓
    for x in range(w):
        for y in range(h):
            if op[x, y][3] > 40:
                if y > 0 and op[x, y - 1][3] < 20:
                    op[x, y] = (42, 72, 38, 255)
                    if x + 1 < w and op[x + 1, y][3] > 40:
                        op[x + 1, y] = (48, 78, 42, op[x + 1, y][3])
                break
    return out

def type99_hull_pixel(r, g, b, a):
    if a < 8:
        return (r, g, b, a)
    if is_remap(r, g, b, a):
        return (r, g, b, a)
    # 犀牛灰绿 → 略深的墨绿履带装甲（仍偏 RA2 工业色）
    lum = (r + g + b) / 3
    if lum > 40:
        return (clamp(r * 0.72 + 18), clamp(g * 0.78 + 28), clamp(b * 0.62 + 12), a)
    return (r, g, b, a)

def stylize_type99_body(im: Image.Image) -> Image.Image:
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    op = out.load()
    for y in range(h):
        for x in range(w):
            op[x, y] = type99_hull_pixel(*px[x, y])
    # 炮塔顶加小「储物箱」块（用内容包围盒上缘中段）
    xs = [x for y in range(h) for x in range(w) if op[x, y][3] > 40]
    ys = [y for y in range(h) for x in range(w) if op[x, y][3] > 40]
    if xs and ys:
        cx = (min(xs) + max(xs)) // 2
        ty = min(ys) + 2
        for dy in range(4):
            for dx in range(-5, 6):
                x, y = cx + dx, ty + dy
                if 0 <= x < w and 0 <= y < h and op[x, y][3] > 20:
                    # 浅灰块 + 深缝，像附加装甲
                    if abs(dx) == 5 or dy == 0 or dy == 3:
                        op[x, y] = (70, 78, 68, 255)
                    else:
                        op[x, y] = (118, 128, 110, 255)
    return out

def stylize_type99_turret(im: Image.Image) -> Image.Image:
    im = im.convert("RGBA")
    px = im.load()
    w, h = im.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    op = out.load()
    for y in range(h):
        for x in range(w):
            op[x, y] = type99_hull_pixel(*px[x, y])
    # 炮管略加粗：向不透明炮管像素邻域扩张 1px 深绿
    grow = []
    for y in range(h):
        for x in range(w):
            if op[x, y][3] < 40:
                continue
            # 细长高亮/深色条当炮管候选
            r, g, b, a = op[x, y]
            if r + g + b < 220 and y < h * 0.55:
                for dx, dy in ((-1, 0), (1, 0), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and op[nx, ny][3] < 20:
                        grow.append((nx, ny, (55, 62, 48, 230)))
    for x, y, c in grow:
        op[x, y] = c
    return out

def convert_tree(src_glob: str, dst_prefix: str, fn):
    n = 0
    for src in sorted(SPR.glob(src_glob)):
        name = src.name
        # unit_conscript_xxx → unit_pla_xxx
        if not name.startswith(dst_prefix.split("_")[0]):
            pass
        dst_name = name.replace("conscript", "pla").replace("rhino", "type99")
        if "pla" not in dst_name and "type99" not in dst_name:
            continue
        dst = SPR / dst_name
        im = Image.open(src)
        fn(im).save(dst)
        n += 1
    return n

def main():
    prep = ROOT / "tools" / "asset_pipeline" / "prepare_fusion_units.py"
    pub = ROOT / "tools" / "asset_pipeline" / "publish_unit.py"
    qa = ROOT / "tools" / "asset_pipeline" / "qa_check.py"
    pla_m = ROOT / "tools" / "asset_pipeline" / "templates" / "pla" / "manifest.yaml"
    t99_m = ROOT / "tools" / "asset_pipeline" / "templates" / "type99" / "manifest.yaml"
    if prep.is_file() and pla_m.is_file() and t99_m.is_file():
        for cmd in (
            [sys.executable, str(prep)],
            [sys.executable, str(pub), "--manifest", str(pla_m)],
            [sys.executable, str(pub), "--manifest", str(t99_m)],
            [sys.executable, str(qa), "--manifest", str(pla_m)],
            [sys.executable, str(qa), "--manifest", str(t99_m)],
        ):
            print("+", " ".join(cmd))
            r = subprocess.run(cmd, cwd=str(ROOT))
            if r.returncode != 0:
                raise SystemExit(r.returncode)
        (SPR / "FUSION_ART.txt").write_text(
            "PLA / Type99 via tools/asset_pipeline (prepare_fusion_units → publish_unit).\n"
            "Derived from conscript / rhino; not official Westwood MIX.\n",
            encoding="utf-8",
        )
        print("done (pipeline)")
        return

    # Legacy direct write
    n = 0
    for src in sorted(SPR.glob("unit_conscript_*.png")):
        stylize_pla(Image.open(src)).save(SPR / src.name.replace("conscript", "pla"))
        n += 1
    print(f"PLA frames: {n}")
    for src in sorted(SPR.glob("unit_rhino_d*_f0.png")):
        stylize_type99_body(Image.open(src)).save(SPR / src.name.replace("rhino", "type99"))
    for src in sorted(SPR.glob("turret_rhino_d*.png")):
        stylize_type99_turret(Image.open(src)).save(SPR / src.name.replace("rhino", "type99"))
    print("done (legacy)")

if __name__ == "__main__":
    main()
