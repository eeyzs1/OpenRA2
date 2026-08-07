"""Build stem→(w,h) from art.ini Foundations for every extracted bld_*.png.

Writes assets/meta/bld_stem_feet.json used by fit_bld_cages.py / export.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ra2pack"))
from ra2lib import MixTree  # noqa: E402

OUT = ROOT / "assets" / "meta" / "bld_stem_feet.json"
GEN = (ROOT / "tools" / "ra2pack" / "gen_assets.py").read_text(encoding="utf-8", errors="replace")

# eng -> rules id list from BLDS = { ... }
BLDS = {}
m = re.search(r"^BLDS\s*=\s*\{(.*?)^\}", GEN, re.S | re.M)
if m:
    for eng, cands in re.findall(r'"(\w+)"\s*:\s*\[([^\]]+)\]', m.group(1)):
        ids = re.findall(r'"([A-Z0-9_]+)"', cands)
        BLDS[eng] = ids


def parse_foundations(text: str) -> dict[str, tuple[int, int]]:
    out: dict[str, tuple[int, int]] = {}
    cur = None
    for line in text.splitlines():
        sm = re.match(r"\[([^\]]+)\]", line)
        if sm:
            cur = sm.group(1).upper()
            continue
        if cur and line.lower().startswith("foundation="):
            v = line.split("=", 1)[1].strip().lower()
            mm = re.match(r"(\d+)\s*x\s*(\d+)", v)
            if mm:
                out[cur] = (int(mm.group(1)), int(mm.group(2)))
    return out


def main() -> None:
    T = MixTree(str(ROOT / "tools" / "ra2pack" / "game"))
    feet: dict[str, tuple[int, int]] = {}
    for ini in ("artmd.ini", "art.ini"):
        _, data = T.find(ini)
        if not data:
            continue
        for k, v in parse_foundations(data.decode("latin-1", "replace")).items():
            feet.setdefault(k, v)

    stem_feet: dict[str, list[int]] = {}
    for eng, cands in BLDS.items():
        for rid in cands:
            if rid in feet:
                stem_feet[eng] = [feet[rid][0], feet[rid][1]]
                break
        # Image= override in rules
        for rid in cands:
            # already handled via section name == Image usually
            pass

    # Explicit extras if BLDS key uses Image name different from rid
    extras = {
        "barracks_sov": "NAHAND",
        "barracks_yuri": "YABRCK",
        "battlelab_sov": "NATECH",
        "civbarn": "CABARN02",
        "civchurch": "CACHIG01",
        "civhouse": "CAHSE01",
        "civhouse2": "CAHSE02",
        "civhouse3": "CAHSE03",
        "civhouse4": "CAHSE04",
        "civhouse5": "CAHSE05",
        "civhouse6": "CAHSE06",
        "civhouse7": "CAHSE07",
        "civwash": "CAWASH01",
        "civwatertower": "CAWT01",
        "psychicbeacon": "NAPSYA",
        "psychicbeacon2": "NAPSYB",
        "techoutpost": "CAOUTP",
        "warfactory_sov": "NAWEAP",
        "warfactory_yuri": "YAWEAP",
        "navalyard_sov": "NAYARD",
        "orerefinery_sov": "NAREFN",
        "servicedepot_sov": "NADEPT",
        "conyard_sov": "NACNST",
        "wall_sov": "NAWALL",
        "sandbags": "GASAND",
    }
    for stem, rid in extras.items():
        if rid in feet:
            stem_feet[stem] = [feet[rid][0], feet[rid][1]]

    OUT.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "note": "art.ini/artmd Foundation per extract stem — source of truth for cage feet",
        "buildings": stem_feet,
    }
    OUT.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(stem_feet)} stems -> {OUT}")
    for k in sorted(
        ["barracks_yuri", "civchurch", "civhouse5", "civhouse6", "techoutpost", "barracks_sov"]
    ):
        print(f"  {k}: {stem_feet.get(k)}")


if __name__ == "__main__":
    main()
