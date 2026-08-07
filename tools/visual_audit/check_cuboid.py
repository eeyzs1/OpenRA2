"""Score selection-cage edges against an expected isometric cuboid."""
from __future__ import annotations

import os
import numpy as np
from PIL import Image, ImageDraw

BASE = os.path.join(os.path.dirname(__file__), "out")


def edge_hit(line: np.ndarray, a, b, tol: float = 2.5) -> float:
    x0, y0 = a
    x1, y1 = b
    n = max(int(np.hypot(x1 - x0, y1 - y0)), 1)
    hit = 0
    itol = int(tol)
    for i in range(n + 1):
        t = i / n
        x = x0 + (x1 - x0) * t
        y = y0 + (y1 - y0) * t
        x0i, y0i = int(round(x)), int(round(y))
        ok = False
        for dy in range(-itol, itol + 1):
            for dx in range(-itol, itol + 1):
                yy, xx = y0i + dy, x0i + dx
                if 0 <= yy < line.shape[0] and 0 <= xx < line.shape[1] and line[yy, xx]:
                    ok = True
                    break
            if ok:
                break
        if ok:
            hit += 1
    return hit / (n + 1)


def analyze(name: str, half_w: float, elev: float) -> None:
    im = np.array(Image.open(os.path.join(BASE, name)).convert("RGBA"))
    r, g, b, a = im[:, :, 0], im[:, :, 1], im[:, :, 2], im[:, :, 3]
    line = (a > 200) & (r >= 240) & (g >= 220) & (g <= 250) & (b >= 40) & (b <= 90)
    line = line.copy()
    line[:, 1100:] = False
    ys, xs = np.where(line)
    if len(xs) == 0:
        print(name, "NO YELLOW")
        return
    cx = int(np.median(xs))
    band = line[:, cx - 8 : cx + 9]
    bys, bxs = np.where(band)
    sy = int(bys.max())
    sx = cx - 8 + int(bxs[bys.argmax()])

    half_d = half_w * 0.5
    bn = (sx, sy - 2 * half_d)
    be = (sx + half_w, sy - half_d)
    bs = (sx, sy)
    bw = (sx - half_w, sy - half_d)
    tn = (bn[0], bn[1] - elev)
    te = (be[0], be[1] - elev)
    ts = (bs[0], bs[1] - elev)
    tw = (bw[0], bw[1] - elev)

    edges = [
        ("bot_NE", bn, be),
        ("bot_ES", be, bs),
        ("bot_SW", bs, bw),
        ("bot_WN", bw, bn),
        ("top_NE", tn, te),
        ("top_ES", te, ts),
        ("top_SW", ts, tw),
        ("top_WN", tw, tn),
        ("V_N", bn, tn),
        ("V_E", be, te),
        ("V_S", bs, ts),
        ("V_W", bw, tw),
    ]
    scores = {k: edge_hit(line, a, b) for k, a, b in edges}
    bot = np.mean([scores[k] for k in scores if k.startswith("bot")])
    top = np.mean([scores[k] for k in scores if k.startswith("top")])
    vert = np.mean([scores[k] for k in scores if k.startswith("V")])
    mean = float(np.mean(list(scores.values())))
    print(f"{name} S=({sx},{sy}) halfW={half_w:.1f} elev={elev:.1f}")
    for k, v in scores.items():
        print(f"  {k}: {v:.0%}")
    print(f"  mean={mean:.0%} bottom={bot:.0%} top={top:.0%} verts={vert:.0%}")

    out = Image.fromarray(im.copy())
    dr = ImageDraw.Draw(out)
    for a, b in [
        (bn, be),
        (be, bs),
        (bs, bw),
        (bw, bn),
        (tn, te),
        (te, ts),
        (ts, tw),
        (tw, tn),
        (bn, tn),
        (be, te),
        (bs, ts),
        (bw, tw),
    ]:
        dr.line([a, b], fill=(255, 0, 255), width=2)
    x0 = int(min(bw[0], tw[0]) - 20)
    x1 = int(max(be[0], te[0]) + 20)
    y0 = int(min(tn[1], tw[1], te[1]) - 20)
    y1 = int(sy + 20)
    out.crop((x0, y0, x1, y1)).save(os.path.join(BASE, "fit_" + name))


def main() -> None:
    # elev = visElev - halfD when visElev > 2.2*halfD; else keep visElev (short blds)
    analyze("03_pillbox_cage.png", max(32.0, 51 / 2), 30.0)
    analyze("03_teslacoil_cage.png", max(32.0, 41 / 2), max(90.0 - 16.0, 8.0))
    analyze("03_powerplant_cage.png", max(64.0, 89 / 2), max(97.0 - 32.0, 8.0))
    analyze("03_barracks_cage.png", max(64.0, 139 / 2), max(133.0 - 34.75, 8.0))
    analyze("03_warfactory_cage.png", max(96.0, 221 / 2), max(164.0 - 55.25, 8.0))


if __name__ == "__main__":
    main()
