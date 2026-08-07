"""Quick probe: lower grand cannon barrel angles on mk peak (scaled PNG)."""
from PIL import Image
import math, os

SRC = r"E:\AI_Generated_Projects\OpenRA2\assets\sprites\bld_grandcannon_mk_f10.png"
OUT = r"E:\AI_Generated_Projects\OpenRA2\tools\visual_audit\bld_review\gcan_probe"


def lower(img, deg=-58, pivot_y_frac=0.70, right_pad=110, blend=8):
    bb = img.getbbox()
    px = (bb[0] + bb[2]) / 2.0
    py = bb[1] + (bb[3] - bb[1]) * pivot_y_frac
    rad = math.radians(deg)
    cosr, sinr = math.cos(rad), math.sin(rad)
    W = img.width + right_pad
    H = img.height
    res = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sp = img.load()
    dp = res.load()

    def sample(sx, sy):
        x0 = int(math.floor(sx)); y0 = int(math.floor(sy))
        x1, y1 = x0 + 1, y0 + 1
        if x0 < 0 or y0 < 0 or x1 >= img.width or y1 >= img.height:
            return (0, 0, 0, 0)
        fx, fy = sx - x0, sy - y0
        p00, p10 = sp[x0, y0], sp[x1, y0]
        p01, p11 = sp[x0, y1], sp[x1, y1]
        if p00[3] + p10[3] + p01[3] + p11[3] < 8:
            return (0, 0, 0, 0)
        def lerp(a, b, t):
            return tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(4))
        return lerp(lerp(p00, p10, fx), lerp(p01, p11, fx), fy)

    # base intact
    for y in range(int(py) - blend, img.height):
        for x in range(img.width):
            p = sp[x, y]
            if p[3] > 8:
                dp[x, y] = p

    # upper: only sample source above py (barrel/turret roof)
    for y in range(0, int(py) + blend + 2):
        for x in range(W):
            rx = x - px
            ry = y - py
            sx = px + rx * cosr - ry * sinr
            sy = py + rx * sinr + ry * cosr
            if sy >= py + 1:
                continue
            p = sample(sx, sy)
            if p[3] <= 16:
                continue
            # near pivot blend with existing base
            if y >= int(py) - blend and dp[x, y][3] > 8:
                t = (int(py) + blend - y) / max(1, 2 * blend)  # more rotated near top
                t = max(0.0, min(1.0, t))
                dst = dp[x, y]
                dp[x, y] = tuple(int(p[i] * t + dst[i] * (1 - t)) for i in range(4))
            else:
                dp[x, y] = p
    return res, (px, py)


def elev(im, pivot):
    px, py = pivot
    pts = []
    p = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = p[x, y]
            if a < 40:
                continue
            if y < py - 5 and r > 100:
                pts.append((x, y))
    if not pts:
        return None
    tip = max(pts, key=lambda t: (t[0], -t[1]))  # rightmost
    tip2 = min(pts, key=lambda t: (t[1], -t[0]))  # highest
    for name, t in [("right", tip), ("high", tip2)]:
        dx = t[0] - px
        dy = py - t[1]
        ang = math.degrees(math.atan2(dy, max(dx, 0.01)))
        print(f"  {name} tip={t} elev={ang:.1f}")
    return tip


im = Image.open(SRC).convert("RGBA")
for deg in [-45, -52, -58, -62, -68, -72]:
    out, piv = lower(im, deg=deg)
    path = os.path.join(OUT, f"aim{abs(deg)}.png")
    out.save(path)
    print(f"deg={deg} pivot=({piv[0]:.0f},{piv[1]:.0f})")
    elev(out, piv)
