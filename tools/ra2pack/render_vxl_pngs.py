# Render stand PNGs from assets/voxels/*.vxl (no MIX tree) for YR units missing sprites.
# Usage: python tools/ra2pack/render_vxl_pngs.py
from __future__ import annotations

import gc
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import Vxl, load_pal, vxl_project, render_pts, _phi_for_screen_alpha
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")
VOX = os.path.join(ROOT, "assets", "voxels")
PAL_PATH = os.path.join(ROOT, "assets", "palettes", "unittem.pal")
SCALE = 64 / 60.0
CANVAS = 72

UNITS = [
    ("slaveminer", "smin", "smintur", False),
    ("mastermind", "mind", None, False),
    ("magnetron", "tele", "teletur", False),
    ("gatlingtank", "ytnk", "ytnktur", False),
    ("floatingdisc", "disk", "disktur", True),
    ("boomer", "bsub", None, True),
    ("siegechopper", "schp", None, True),
    ("battlefortress", "bfrt", None, False),
    ("robottank", "robo", "robotur", False),
    ("mig", "bpln", None, True),
    ("chaosdrone", "caos", None, False),
]


def main() -> None:
    pal = load_pal(open(PAL_PATH, "rb").read())
    w = ch = CANVAS
    for eng, body, tur, floating in UNITS:
        bp = os.path.join(VOX, body + ".vxl")
        if not os.path.isfile(bp):
            print(f"MISS body {eng} {bp}")
            continue
        v = Vxl(open(bp, "rb").read())
        imgs = []
        for e in range(8):
            pts, _ = vxl_project(v, None, _phi_for_screen_alpha(45 * e))
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            gx = (min(xs) + max(xs)) / 2
            gy = max(ys) if not floating else (min(ys) + max(ys)) / 2
            orgx = w / 2 - gx * SCALE
            orgy = (ch * 0.72 if not floating else ch / 2) - gy * SCALE
            imgs.append(render_pts(pts, pal, SCALE, orgx, orgy, w, ch, supersample=1))
            del pts
        for e, im in enumerate(imgs):
            im.save(os.path.join(SPR, f"unit_{eng}_d{e}_f0.png"))
        if tur:
            tp = os.path.join(VOX, tur + ".vxl")
            if os.path.isfile(tp):
                vt = Vxl(open(tp, "rb").read())
                for e in range(8):
                    pts, _ = vxl_project(vt, None, _phi_for_screen_alpha(45 * e))
                    xs = [p[0] for p in pts]
                    ys = [p[1] for p in pts]
                    gx = (min(xs) + max(xs)) / 2
                    gy = (min(ys) + max(ys)) / 2
                    im = render_pts(pts, pal, SCALE, w / 2 - gx * SCALE, ch / 2 - gy * SCALE, w, ch, supersample=1)
                    im.save(os.path.join(SPR, f"turret_{eng}_d{e}.png"))
                    del pts
        icon = imgs[2].resize((54, 42), Image.NEAREST)
        ic = Image.new("RGBA", (108, 84), (96, 132, 168, 255))
        ic.paste(icon, ((108 - icon.width) // 2, (84 - icon.height) // 2), icon)
        ic.save(os.path.join(SPR, f"icon_unit_{eng}.png"))
        print(f"OK {eng}")
        del imgs, v
        gc.collect()
    print("done")


if __name__ == "__main__":
    main()
