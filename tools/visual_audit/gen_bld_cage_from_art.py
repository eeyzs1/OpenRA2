"""Generate building cage from art.ini Height + engine Foundation (BldDef w×h).

RA2-style: no per-building pixel overrides.
  halfW = (fw+fh)*TILE_W/4
  halfD = halfW/2
  elev  = Height * TILE_H   (Height from art.ini, cells)

Writes:
  assets/meta/bld_cage.json
  src/gfx/bld_cage_data.inc

Usage:
  python tools/visual_audit/gen_bld_cage_from_art.py
  python tools/visual_audit/export_bld_cage_review.py
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "ra2pack"))

from ra2lib import MixTree  # noqa: E402

DATA_CPP = ROOT / "src" / "game" / "data.cpp"
ASSETS_H = ROOT / "src" / "gfx" / "assets.h"
SPR = ROOT / "assets" / "sprites"
OUT_JSON = ROOT / "assets" / "meta" / "bld_cage.json"
INC = ROOT / "src" / "gfx" / "bld_cage_data.inc"
REVIEW_JSON = Path(__file__).resolve().parent / "bld_review" / "cage" / "measured.json"

TILE_W = 64
TILE_H = 32

# stem -> art.ini section candidates (from gen_assets.BLDS)
BLD_ART = {
    "conyard": ["GACNST"],
    "powerplant": ["GAPOWR"],
    "teslareactor": ["NAPOWR"],
    "nuclearreactor": ["NANRCT"],
    "barracks": ["GAPILE"],
    "warfactory": ["GAWEAP"],
    "orerefinery": ["GAREFN"],
    "radar": ["GARADR", "NARADR"],
    "battlelab": ["GATECH"],
    "airforcecmd": ["GAAIRC"],
    "navalyard": ["GAYARD"],
    "pillbox": ["GAPILL"],
    "sentrygun": ["NALASR"],
    "prismtower": ["GAPRIS"],
    "teslacoil": ["NATSLA"],
    "flakcannon": ["NAFLAK"],
    "grandcannon": ["GTGCAN"],
    "patriotmissile": ["NASAM"],
    "wall": ["GAWALL"],
    "orepurifier": ["GAOREP"],
    "industrialplant": ["NAINDP"],
    "techpowerplant": ["CAPOWR"],
    "nukesilo": ["NAMISL"],
    "weatherdevice": ["GAWEAT", "GAWETH"],
    "ironcurtain": ["NAIRON"],
    "chronosphere": ["GACSPH"],
    "oilderrick": ["CAOILD"],
    "hospital": ["CAHOSP", "CATHOSP"],
    "machineshop": ["CAMACH"],
    "cloningvat": ["NACLON"],
    "servicedepot": ["GADEPT"],
    "gapgenerator": ["GAGAP"],
    "spysat": ["GASPST", "GASPYSAT"],
    "psychicsensor": ["NAPSIS"],
    "techairport": ["CAAIRP"],
    "secretlab": ["CASLAB", "CALAB"],
    "civhouse": ["CTHSE01", "CAHSE01"],
    "techoutpost": ["CAOUTP"],
    "battlebunker": ["NABNKR"],
    "tankbunker": ["NATBNK"],
    "bioreactor": ["YAPOWR"],
    "gatlingcannon": ["YAGGUN"],
    "grinder": ["YAGRND"],
    "geneticmutator": ["YAGNTC"],
    "psychicdominator": ["YAPPET", "NAPPET"],
    "psychictower": ["YAPSYT", "NAPSYT"],
    "robotcontrol": ["GAROBO", "NAROBO"],
    "techpowerplant": ["CAPOWR", "GAPOWR"],
    "tankbunker": ["NATBNK", "YATBNK"],
    "battlebunker": ["NABNKR", "YABNKR"],
    "wall": ["GAWALL", "NAWALL"],
    "wall_sov": ["NAWALL"],
    "sandbags": ["GASAND", "NASAND", "GABAG"],
    "gatlingcannon": ["YAGGUN", "NAGGUN"],
    "grinder": ["YAGRND", "NAGRND"],
    "geneticmutator": ["YAGNTC", "NAGNTC"],
    "bioreactor": ["YAPOWR", "NAPOWR"],
    "grandcannon": ["GTGCAN", "GAGCAN"],
    "chronosphere": ["GACSPH"],
    "ironcurtain": ["NAIRON"],
    "flakcannon": ["NAFLAK"],
    "prismtower": ["GAPRIS", "ATESLA"],
    "teslacoil": ["NATSLA", "NATESL"],
    "patriotmissile": ["NASAM"],
    "gapgenerator": ["GAGAP"],
    "cloningvat": ["NACLON"],
    "industrialplant": ["NAINDP"],
    "orepurifier": ["GAOREP"],
    "nukesilo": ["NAMISL"],
    "weatherdevice": ["GAWEAT", "GAWETH"],
    "machineshop": ["CAMACH"],
    "oilderrick": ["CAOILD"],
    "hospital": ["CAHOSP", "CATHOSP"],
    "civbarn": ["CABARN02", "CABARN"],
    "civwash": ["CAWASH01", "CTWASH01"],
    "civhouse": ["CTHSE01", "CAHSE01"],
    "civhouse2": ["CTHSE02", "CAHSE02"],
    "civhouse3": ["CTHSE03", "CAHSE03"],
    "civhouse4": ["CAHSE04"],
    "chronosphere": ["GACSPH", "GACHRO", "CHRONO"],
    "civbarn": ["CABARN02", "CABARN"],
    "civwash": ["CAWASH01", "CTWASH01"],
    "tankbunker": ["NATBNK", "YATBNK", "GATBNK"],
    "sandbags": ["GASAND", "NASAND", "GABAG"],
    "wall": ["GAWALL", "NAWALL"],
    "wall_sov": ["NAWALL"],
}


def parse_ini(raw: bytes) -> dict:
    secs, cur = {}, None
    for line in raw.decode("latin-1", "replace").splitlines():
        line = line.strip()
        if not line or line.startswith(";") or line.startswith("//"):
            continue
        if line.startswith("[") and line.endswith("]"):
            cur = line[1:-1]
            secs.setdefault(cur, {})
            continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1)
            secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs


def load_art() -> dict:
    T = MixTree()
    merged: dict = {}
    for name in ("art.ini", "artmd.ini"):
        _, raw = T.find(name)
        if not raw:
            continue
        for sec, kv in parse_ini(raw).items():
            merged.setdefault(sec, {}).update(kv)
    if not merged:
        raise SystemExit("art.ini / artmd.ini not found in MIX")
    return merged


def load_footprints() -> dict[str, tuple[int, int]]:
    names = {
        enum: stem
        for enum, stem in re.findall(
            r"case\s+BldType::(\w+):\s*return\s+\"([a-z0-9_]+)\";",
            ASSETS_H.read_text(encoding="utf-8", errors="replace"),
        )
    }
    feet: dict[str, tuple[int, int]] = {}
    text = DATA_CPP.read_text(encoding="utf-8", errors="replace")
    for enum, w, h in re.findall(
        r"\{BldType::(\w+)\s*,[^,]*,\s*\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*(\d+)\s*,\s*(\d+)\s*,",
        text,
    ):
        stem = names.get(enum)
        if stem:
            feet[stem] = (int(w), int(h))
    return feet


def resolve_foot(name: str, feet: dict[str, tuple[int, int]]) -> tuple[int, int]:
    if name in feet:
        return feet[name]
    for suf in ("_sov", "_usa", "_yuri"):
        if name.endswith(suf) and name[: -len(suf)] in feet:
            return feet[name[: -len(suf)]]
    return (2, 2)


def art_height(art: dict, stem: str) -> tuple[int, str]:
    for sec in BLD_ART.get(stem, []):
        a = art.get(sec, {})
        if "Height" in a:
            try:
                return max(1, int(a["Height"])), sec
            except ValueError:
                pass
        # follow Image= redirect once
        img = a.get("Image")
        if img:
            b = art.get(img.upper(), {})
            if "Height" in b:
                try:
                    return max(1, int(b["Height"])), img.upper()
                except ValueError:
                    pass
    # faction variants: try base stem
    for suf in ("_sov", "_usa", "_yuri"):
        if stem.endswith(suf):
            return art_height(art, stem[: -len(suf)])
    return 1, ""


def sprite_anchor(path: Path):
    from PIL import Image

    im = Image.open(path).convert("RGBA")
    px = im.load()
    w, h = im.size
    alpha = 60
    visL, visT, visR, visB = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            if px[x, y][3] > alpha:
                visL = min(visL, x)
                visR = max(visR, x)
                visT = min(visT, y)
                visB = max(visB, y)
    if visR < 0:
        return w // 2, h - 1, 0, 0
    tip_sum = tip_n = 0
    for x in range(w):
        if px[x, visB][3] > alpha:
            tip_sum += x
            tip_n += 1
    ox = tip_sum // tip_n if tip_n else w // 2
    return ox, visB, visR - visL + 1, max(8, visB - visT)


def list_idle():
    out = []
    for p in sorted(SPR.glob("bld_*.png")):
        if any(s in p.name for s in ("_mk_f", "_scaffold", "_a_f", "_ready")):
            continue
        out.append(p)
    return out


def main() -> None:
    art = load_art()
    feet = load_footprints()
    rows = []
    for p in list_idle():
        stem = p.stem[4:]
        fw, fh = resolve_foot(stem, feet)
        h_cells, art_sec = art_height(art, stem)
        half_w = (fw + fh) * (TILE_W / 4.0)
        half_d = half_w * 0.5
        elev = float(h_cells * TILE_H)
        ox, oy, vis_w, vis_elev = sprite_anchor(p)
        row = {
            "file": p.name,
            "stem": stem,
            "footW": fw,
            "footH": fh,
            "artHeight": h_cells,
            "artSec": art_sec,
            "halfW": round(half_w, 2),
            "halfD": round(half_d, 2),
            "elev": round(elev, 2),
            "offX": 0.0,
            "offY": 0.0,
            "ox": ox,
            "oy": oy,
            "visW": vis_w,
            "visElev": vis_elev,
            "visL": 0,
            "visT": max(0, oy - vis_elev),
            "visR": ox + vis_w // 2,
            "visB": oy,
            "artCx": float(ox),
            "bsX": float(ox),
            "bsY": float(oy),
        }
        rows.append(row)
        print(
            f"{stem:22} foot={fw}x{fh} Height={h_cells} "
            f"halfW={half_w:.0f} halfD={half_d:.0f} elev={elev:.0f} "
            f"art={art_sec or '?'} visElev={vis_elev}"
        )

    payload = {
        "version": 5,
        "note": (
            "RA2-style cage: Foundation from BldDef (w×h), Height from art.ini. "
            "halfW=(w+h)*TILE_W/4, halfD=halfW/2, elev=Height*TILE_H. No per-bld overrides."
        ),
        "buildings": {r["stem"]: r for r in rows},
    }
    text = json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    REVIEW_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(text, encoding="utf-8")
    REVIEW_JSON.write_text(text, encoding="utf-8")

    lines = [
        "// Auto-generated by tools/visual_audit/gen_bld_cage_from_art.py — DO NOT EDIT\n",
        "// Foundation = BldDef w×h at draw time; only art.ini Height stored here.\n",
        "struct BldCageMeas { const char* stem; float elev; int artHeight; };\n",
        "static const BldCageMeas kBldCageMeas[] = {\n",
    ]
    for r in sorted(rows, key=lambda x: x["stem"]):
        lines.append(
            f'    {{"{r["stem"]}", {r["elev"]:.2f}f, {r["artHeight"]}}},\n'
        )
    lines.append("};\n")
    lines.append(f"static const int kBldCageMeasN = {len(rows)};\n")
    INC.write_text("".join(lines), encoding="utf-8")

    print(f"\nWrote {len(rows)} -> {OUT_JSON}")
    print(f"         {INC}")
    missing = [r for r in rows if not r["artSec"]]
    if missing:
        print(f"Height defaulted to 1 for {len(missing)} stems (no art.ini Height)")


if __name__ == "__main__":
    main()
