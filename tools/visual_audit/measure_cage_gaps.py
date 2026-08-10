#!/usr/bin/env python3
"""Measure selection-cage top gap vs building visT for review stems."""
from __future__ import annotations

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
META = json.loads((ROOT / "assets/meta/bld_cage.json").read_text(encoding="utf-8"))["buildings"]
TILE_W, TILE_H = 64, 32

STEMS = {
    "05": "nuclearreactor",
    "07": "barracks",
    "08": "warfactory",
    "09": "orerefinery",
    "12": "airforcecmd",
    "24": "grandcannon",
    "62": "conyard_sov",
    "63": "navalyard_sov",
    "64": "navalyard_yuri",
    "65": "orerefinery_sov",
    "66": "psychicbeacon",
    "67": "psychicbeacon2",
    "69": "servicedepot_sov",
    "71": "warfactory_sov",
    "72": "warfactory_yuri",
}


def measure(stem: str):
    row = META[stem]
    fw, fh = int(row["footW"]), int(row["footH"])
    oy = float(row["oy"])
    geo = float(row.get("geoOx", 0.0))
    off_x = float(row.get("offX", 0.0)) - 2.0
    off_y = float(row.get("offY", 0.0)) + 1.0
    e_arm_x = fh * (TILE_W / 2.0)
    e_arm_y = -fh * (TILE_H / 2.0)
    w_arm_x = -fw * (TILE_W / 2.0)
    w_arm_y = -fw * (TILE_H / 2.0)

    def add(dx, dy):
        nonlocal off_x, off_y
        length = math.hypot(dx, dy)
        if length > 1e-3:
            off_x += 4.0 * dx / length
            off_y += 4.0 * dy / length

    add(-e_arm_x, -e_arm_y)
    add(-w_arm_x, -w_arm_y)
    off_y += 3.0
    elev = min(float(row["elev"]), max(8.0, float(row["visElev"]) - 4.0))
    bs = (geo + off_x, oy + off_y)
    corners = [
        (bs[0] + e_arm_x + w_arm_x, bs[1] + e_arm_y + w_arm_y),
        (bs[0] + e_arm_x, bs[1] + e_arm_y),
        bs,
        (bs[0] + w_arm_x, bs[1] + w_arm_y),
    ]
    cage_top = min(c[1] - elev for c in corners)
    cage_bot = max(c[1] for c in corners)
    vis_t = float(row["visT"])
    vis_b = float(row["visB"])
    # body roof (skip thin antenna): approx from stored roofY if present
    roof_y = float(row.get("roofY", vis_t))
    gap_vis = vis_t - cage_top  # + = empty above full sprite tip
    gap_body = roof_y - cage_top  # + = empty above body roof
    base_over = cage_bot - vis_b
    return {
        "elev": elev,
        "offY": float(row.get("offY", 0.0)),
        "gap_vis": gap_vis,
        "gap_body": gap_body,
        "base_over": base_over,
        "visElev": float(row["visElev"]),
        "roofY": roof_y,
        "visT": vis_t,
    }


def main() -> None:
    print(f"{'#':>3} {'stem':20} elev offY gapVis gapBody base  note")
    for num, stem in STEMS.items():
        m = measure(stem)
        # Prefer body roof gap ~6..14px; antenna tips may stick out.
        if m["gap_body"] > 16:
            note = "STILL_TALL"
        elif m["gap_body"] < 0:
            note = "CLIP_BODY"
        elif m["gap_vis"] < -8:
            note = "CLIP_ANTENNA_OK?"
        else:
            note = "OK-ish"
        print(
            f"{num:>3} {stem:20} {m['elev']:5.0f} {m['offY']:4.0f} "
            f"{m['gap_vis']:6.1f} {m['gap_body']:6.1f} {m['base_over']:5.1f}  {note}"
        )


if __name__ == "__main__":
    main()
