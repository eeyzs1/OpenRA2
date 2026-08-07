"""Draw Grand Cannon selection cage (engine formula) onto review PNGs.

Mirrors src/game/game_render_world.cpp drawIsoCuboid + SpriteBank::finishBldSprite metrics.
"""
from __future__ import annotations

import math
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites", "bld_grandcannon.png")
OUT = os.path.join(ROOT, "tools", "visual_audit", "bld_review", "gcan_cage")
REV = os.path.join(ROOT, "tools", "visual_audit", "bld_review", "singles")
TILE_W = 64
BLD_W, BLD_H = 2, 2  # Grand Cannon foundation
ALPHA = 60


def opaque_bbox(im: Image.Image):
    px = im.load()
    w, h = im.size
    visL, visT, visR, visB = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            if px[x, y][3] > ALPHA:
                if x < visL:
                    visL = x
                if x > visR:
                    visR = x
                if y < visT:
                    visT = y
                if y > visB:
                    visB = y
    if visR < 0:
        return 0, 0, w - 1, h - 1
    return visL, visT, visR, visB


def ground_anchor(im: Image.Image):
    """Lowest opaque row + centroid x — same as SpriteBank building load."""
    px = im.load()
    w, h = im.size
    ground_y = h - 1
    while ground_y > 0:
        solid = any(px[x, ground_y][3] > ALPHA for x in range(w))
        if solid:
            break
        ground_y -= 1
    tip_sum = tip_n = 0
    for x in range(w):
        if px[x, ground_y][3] > ALPHA:
            tip_sum += x
            tip_n += 1
    content_ox = (tip_sum // tip_n) if tip_n else w // 2
    return content_ox, ground_y


def dash_line(draw: ImageDraw.ImageDraw, a, b, fill, thick=2):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    length = math.hypot(dx, dy)
    if length < 1:
        return
    if length < 14:
        draw.line([a, b], fill=fill, width=thick)
        return
    dx /= length
    dy /= length
    dash, gap = 5.0, 3.0
    t = 0.0
    while t < length:
        t1, t2 = t, min(length, t + dash)
        draw.line(
            [(ax + dx * t1, ay + dy * t1), (ax + dx * t2, ay + dy * t2)],
            fill=fill,
            width=thick,
        )
        t += dash + gap


def draw_iso_cuboid(draw: ImageDraw.ImageDraw, bs, half_w: float, elev: float, edge):
    half_w = max(half_w, 8.0)
    half_d = half_w * 0.5
    elev = max(8.0, min(240.0, elev))
    bn = (bs[0], bs[1] - 2.0 * half_d)
    be = (bs[0] + half_w, bs[1] - half_d)
    bw = (bs[0] - half_w, bs[1] - half_d)
    tn = (bn[0], bn[1] - elev)
    te = (be[0], be[1] - elev)
    ts = (bs[0], bs[1] - elev)
    tw = (bw[0], bw[1] - elev)

    # 侧面极淡填充会在部分 Pillow 版本上盖掉底层；审核图只画线框
    fill = None
    for quad in ((bw, bs, ts, tw), (bs, be, te, ts)):
        if fill:
            draw.polygon(list(quad), fill=fill)

    # bottom solid diamond
    for a, b in ((bn, be), (be, bs), (bs, bw), (bw, bn)):
        draw.line([a, b], fill=edge, width=2)
    # top dashed diamond
    for a, b in ((tn, te), (te, ts), (ts, tw), (tw, tn)):
        dash_line(draw, a, b, edge, thick=2)
    # pillars solid
    for a, b in ((bn, tn), (be, te), (bs, ts), (bw, tw)):
        draw.line([a, b], fill=edge, width=2)

    return {
        "bn": bn, "be": be, "bs": bs, "bw": bw,
        "tn": tn, "te": te, "ts": ts, "tw": tw,
        "half_w": half_w, "half_d": half_d, "elev": elev,
    }


def checker(w, h, a=34, b=44):
    im = Image.new("RGBA", (w, h), (a, a + 2, a + 6, 255))
    px = im.load()
    for y in range(0, h, 8):
        for x in range(0, w, 8):
            if ((x // 8) + (y // 8)) % 2 == 0:
                for dy in range(min(8, h - y)):
                    for dx in range(min(8, w - x)):
                        px[x + dx, y + dy] = (b, b + 2, b + 6, 255)
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(REV, exist_ok=True)
    src = Image.open(SPR).convert("RGBA")
    visL, visT, visR, visB = opaque_bbox(src)
    ox, oy = ground_anchor(src)
    vis_w = max(1, visR - visL + 1)
    vis_elev = max(8, oy - visT)
    foot_half_w = (BLD_W + BLD_H) * (TILE_W / 4.0)  # 64 for 2x2
    art_half_w = vis_w * 0.5
    cage_half_w = foot_half_w  # footprint only (do not expand by art AABB)
    half_d = cage_half_w * 0.5
    cage_elev = max(8.0, min(240.0, float(vis_elev) - 2.0 * half_d))
    art_cx = 0.5 * (visL + visR)
    # cage south tip = sprite ground anchor (ox, oy)
    cage_bs = (float(ox), float(oy))

    metrics = (
        f"foot={BLD_W}x{BLD_H} TILE={TILE_W}  "
        f"vis=({visL},{visT})-({visR},{visB}) visW={vis_w} visElev={vis_elev}  "
        f"ox={ox} oy={oy} artCx={art_cx:.1f}  "
        f"footHalfW={foot_half_w:.0f} artHalfW={art_half_w:.1f} "
        f"cageHalfW={cage_half_w:.1f} cageElev={cage_elev:.1f}"
    )
    print(metrics)

    # --- 1) sprite + cage (content space) ---
    pad = 24
    # expand canvas so top of cage / barrel tip aren't clipped
    left = int(min(0, cage_bs[0] - cage_half_w - 4))
    top = int(min(0, cage_bs[1] - 2 * half_d - cage_elev - 4))
    right = int(max(src.width, cage_bs[0] + cage_half_w + 4, visR + 4))
    bottom = int(max(src.height, cage_bs[1] + 4, visB + 4))
    # also pad for label
    canvas_w = right - left + pad * 2
    canvas_h = bottom - top + pad * 2 + 36
    off = (pad - left, pad + 36 - top)

    bg = checker(canvas_w, canvas_h)
    bg.alpha_composite(src, off)
    # 笼画在透明层再叠，避免侧面 fill 盖掉贴图
    overlay = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay, "RGBA")
    bs = (cage_bs[0] + off[0], cage_bs[1] + off[1])
    corners = draw_iso_cuboid(draw, bs, cage_half_w, cage_elev, (255, 240, 60, 245))
    bg = Image.alpha_composite(bg, overlay)
    draw2 = ImageDraw.Draw(bg, "RGBA")
    r = 3
    draw2.ellipse([bs[0] - r, bs[1] - r, bs[0] + r, bs[1] + r], outline=(255, 0, 255, 200))
    draw2.text((8, 6), "Grand Cannon selection cage (engine formula)", fill=(255, 220, 80, 255))
    draw2.text((8, 22), metrics[:110], fill=(180, 180, 186, 255))
    path1 = os.path.join(OUT, "gcan_idle_with_cage.png")
    bg.save(path1)
    bg.save(os.path.join(REV, "24h_grandcannon_cage.png"))

    # --- 2) bare | cage side-by-side ---
    bare = checker(src.width + 16, src.height + 40)
    bare.alpha_composite(src, (8, 32))
    d2 = ImageDraw.Draw(bare)
    d2.text((8, 8), "1) idle bare (no cage)", fill=(200, 200, 200, 255))

    caged = bg.copy()
    # scale both to same height
    H = 280
    bare.thumbnail((400, H))
    caged.thumbnail((520, H))
    strip = Image.new("RGBA", (bare.width + caged.width + 30, H + 10), (18, 20, 24, 255))
    strip.paste(bare, (10, 5), bare)
    strip.paste(caged, (20 + bare.width, 5), caged)
    path2 = os.path.join(OUT, "gcan_bare_vs_cage.png")
    strip.save(path2)
    strip.save(os.path.join(REV, "24i_grandcannon_bare_vs_cage.png"))

    # --- 3) metrics card ---
    card = Image.new("RGBA", (720, 200), (24, 26, 30, 255))
    d3 = ImageDraw.Draw(card)
    lines = [
        "Grand Cannon cage inference (matches game_render_world.cpp)",
        f"Foundation {BLD_W}x{BLD_H}  =>  footHalfW = (2+2)*(64/4) = {foot_half_w:.0f}px",
        f"Sprite opaque box visW={vis_w}  (artHalfW={art_half_w:.1f}, unused for halfW)",
        f"cageHalfW = foot only = {cage_half_w:.1f}   halfD={half_d:.1f}",
        f"visElev = oy - visT = {oy} - {visT} = {vis_elev}",
        f"cageElev = visElev - 2*halfD = {cage_elev:.1f}",
        f"Horizontal: cage south tip at ox,oy=({ox},{oy}) (footprint anchor)",
        f"Bottom diamond SOLID, top diamond DASHED, 4 pillars SOLID  — yellow #FFF03C",
    ]
    y = 10
    for line in lines:
        d3.text((12, y), line, fill=(255, 220, 80, 255) if y == 10 else (200, 200, 200, 255))
        y += 22
    path3 = os.path.join(OUT, "gcan_cage_metrics.png")
    card.save(path3)
    card.save(os.path.join(REV, "24j_grandcannon_cage_metrics.png"))

    print("wrote", path1)
    print("wrote", path2)
    print("wrote", path3)
    print("corners elev tip", corners["ts"])


if __name__ == "__main__":
    main()
