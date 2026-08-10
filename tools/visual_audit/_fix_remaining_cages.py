#!/usr/bin/env python3
"""Fix remaining tall cages from full-card scan."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OV = ROOT / "assets/meta/bld_cage_overrides.json"
META = json.loads((ROOT / "assets/meta/bld_cage.json").read_text(encoding="utf-8"))["buildings"]
TILE_H = 32
TARGET = 8

# From scan_all_cage_cards outliers
STEMS = [
    "conyard",
    "bioreactor",
    "techpowerplant",
    "radar",
    "battlelab",
    "navalyard",
    "servicedepot",
    "orepurifier",
    "industrialplant",
    "cloningvat",
    "robotcontrol",
    "pillbox",
    "sentrygun",
    "prismtower",
    "teslacoil",
    "patriotmissile",
    "gatlingcannon",
    "battlebunker",
    "tankbunker",
    "psychictower",
    "gapgenerator",
    "spysat",
    "psychicsensor",
    "wall",
    "barracks_sov",
    "civwatertower",
    "sandbags",
    "wall_sov",
]


def main() -> None:
    ov = json.loads(OV.read_text(encoding="utf-8"))
    b = ov.setdefault("buildings", {})
    for stem in STEMS:
        r = META[stem]
        cur = dict(b.get(stem, {}))
        fw = int(cur.get("footW", r["footW"]))
        fh = int(cur.get("footH", r["footH"]))
        depth = (fw + fh) * (TILE_H / 2)
        body = float(r.get("bodyElev") or r["visElev"])
        off_y = float(cur.get("offY", 0) or 0)
        elev = TARGET - depth + body + 7.6 + off_y
        # Allow below previous min 24 for tiny 1x1 if needed; keep floor 12.
        elev = max(12.0, min(elev, body - 2))
        cur["elev"] = round(elev, 1)
        b[stem] = cur
        print(f"{stem:20} foot={fw}x{fh} body={body:6.1f} depth={depth:5.0f} elev={cur['elev']:6.1f}")
    OV.write_text(json.dumps(ov, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("wrote", OV)


if __name__ == "__main__":
    main()
