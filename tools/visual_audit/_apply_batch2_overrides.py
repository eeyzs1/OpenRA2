#!/usr/bin/env python3
"""Apply modest elev reductions for review batch 2; shrink teslareactor foot."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OV = ROOT / "assets/meta/bld_cage_overrides.json"
META = ROOT / "assets/meta/bld_cage.json"

# Card numbers → stem (from latest index)
BATCH = {
    2: "powerplant",
    3: "teslareactor",
    30: "nukesilo",
    31: "weatherdevice",
    32: "ironcurtain",
    33: "chronosphere",
    34: "geneticmutator",
    35: "psychicdominator",
    39: "oilderrick",
    40: "hospital",
    41: "machineshop",
    42: "techairport",
    43: "secretlab",
    44: "techoutpost",
    45: "civhouse",
    46: "grinder",
    50: "barracks_yuri",
    51: "battlelab_sov",
    52: "civbarn",
    53: "civchurch",
    54: "civhouse2",
    55: "civhouse3",
    56: "civhouse4",
    57: "civhouse5",
    58: "civhouse6",
    59: "civhouse7",
    60: "civwash",
}

# Modest height cut (~18%), keep prior offX/foot overrides.
FACTOR = 0.82
# Extra absolute cut for very tall cages so "一点" is still visible.
EXTRA = 8.0


def main() -> None:
    ov = json.loads(OV.read_text(encoding="utf-8"))
    meta = json.loads(META.read_text(encoding="utf-8"))["buildings"]
    buildings = ov.setdefault("buildings", {})

    for num, stem in BATCH.items():
        row = meta[stem]
        cur = dict(buildings.get(stem, {}))
        base_elev = float(cur.get("elev", row["elev"]))
        new_elev = max(24.0, round(base_elev * FACTOR - EXTRA, 1))
        cur["elev"] = new_elev
        if stem == "teslareactor":
            # Shrink overall cage footprint (collision remains BldDef 3x2).
            cur["footW"] = 2
            cur["footH"] = 2
            # With smaller foot, elev can sit a bit closer to body without rear flare.
            body = float(row.get("bodyElev", row["visElev"]))
            cur["elev"] = max(24.0, round(min(new_elev, body * 0.72), 1))
        buildings[stem] = cur
        print(f"{num:02d} {stem:20} elev {base_elev:6.1f} -> {cur['elev']:6.1f}"
              + (f" foot={cur['footW']}x{cur['footH']}" if "footW" in cur else ""))

    OV.write_text(json.dumps(ov, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("wrote", OV)


if __name__ == "__main__":
    main()
