#!/usr/bin/env python3
"""Aggressive elev for batch2 using bodyElev - foundation depth + target gap."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OV = ROOT / "assets/meta/bld_cage_overrides.json"
META = json.loads((ROOT / "assets/meta/bld_cage.json").read_text(encoding="utf-8"))["buildings"]
TILE_H = 32
TARGET = 8

BATCH = [
    "powerplant",
    "teslareactor",
    "nukesilo",
    "weatherdevice",
    "ironcurtain",
    "chronosphere",
    "geneticmutator",
    "psychicdominator",
    "oilderrick",
    "hospital",
    "machineshop",
    "techairport",
    "secretlab",
    "techoutpost",
    "civhouse",
    "grinder",
    "barracks_yuri",
    "battlelab_sov",
    "civbarn",
    "civchurch",
    "civhouse2",
    "civhouse3",
    "civhouse4",
    "civhouse5",
    "civhouse6",
    "civhouse7",
    "civwash",
]


def main() -> None:
    ov = json.loads(OV.read_text(encoding="utf-8"))
    b = ov.setdefault("buildings", {})
    for stem in BATCH:
        r = META[stem]
        cur = dict(b.get(stem, {}))
        fw = int(cur.get("footW", r["footW"]))
        fh = int(cur.get("footH", r["footH"]))
        if stem == "teslareactor":
            fw = fh = 2
            cur["footW"] = 2
            cur["footH"] = 2
        depth = (fw + fh) * (TILE_H / 2)
        body = float(r.get("bodyElev") or r["visElev"])
        off_y = float(cur.get("offY", 0) or 0)
        elev = TARGET - depth + body + 7.6 + off_y
        elev = max(24.0, min(elev, body - 4))
        cur["elev"] = round(elev, 1)
        # Keep prior horizontal offsets already in overrides.
        b[stem] = cur
        print(f"{stem:20} foot={fw}x{fh} body={body:6.1f} depth={depth:5.0f} elev={cur['elev']:6.1f}")
    OV.write_text(json.dumps(ov, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("wrote", OV)


if __name__ == "__main__":
    main()
