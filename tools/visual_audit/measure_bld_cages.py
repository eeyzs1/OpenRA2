"""Measure building selection-cage HEIGHT from each idle PNG.

Bottom face is NOT measured from art — it is the tile footprint diamond
(the occluded underside of the building), sized in the engine/export as:
  halfW = (fw+fh)*TILE_W/4
  halfD = halfW/2
South tip = sprite ox/oy (= bldScreenPos).

This script only measures elev so the top-south of the cuboid sits near
the building body roof (thin antenna tips ignored).

Writes:
  assets/meta/bld_cage.json
  tools/visual_audit/bld_review/cage/measured.json
  src/gfx/bld_cage_data.inc

Usage:
  python tools/visual_audit/measure_bld_cages.py
  python tools/visual_audit/export_bld_cage_review.py
"""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPR = ROOT / "assets" / "sprites"
DATA_CPP = ROOT / "src" / "game" / "data.cpp"
ASSETS_H = ROOT / "src" / "gfx" / "assets.h"
OUT_JSON = ROOT / "assets" / "meta" / "bld_cage.json"
REVIEW_JSON = Path(__file__).resolve().parent / "bld_review" / "cage" / "measured.json"
INC = ROOT / "src" / "gfx" / "bld_cage_data.inc"
ALPHA = 60
TILE_W = 64


def opaque_bounds(im):
    px = im.load()
    w, h = im.size
    counts = [0] * h
    visL, visT, visR, visB = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            if px[x, y][3] > ALPHA:
                counts[y] += 1
                visL = min(visL, x)
                visR = max(visR, x)
                visT = min(visT, y)
                visB = max(visB, y)
    return (visL, visT, visR, visB), counts


def ground_anchor(im, visB: int):
    px = im.load()
    w, _ = im.size
    tip_sum = tip_n = 0
    for x in range(w):
        if px[x, visB][3] > ALPHA:
            tip_sum += x
            tip_n += 1
    return (tip_sum // tip_n if tip_n else w // 2), visB


def measure_body_roof(counts, visT: int, oy: int) -> int:
    """Skip only wispy antenna tips (capped), keep tower shafts."""
    if oy <= visT:
        return visT
    vis_elev = oy - visT
    max_n = max(counts[visT : oy + 1]) if oy >= visT else 1
    thresh = max(4, min(10, int(max_n * 0.05)))
    max_tip = max(6, min(16, int(vis_elev * 0.10)))
    roof = visT
    for y in range(visT, min(oy, visT + max_tip) + 1):
        if counts[y] >= thresh:
            return y
        roof = y
    return min(roof + 1, oy)


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
        if name.endswith(suf):
            base = name[: -len(suf)]
            if base in feet:
                return feet[base]
    return (2, 2)


def measure_one(path: Path, foot: tuple[int, int]) -> dict:
    from PIL import Image

    im = Image.open(path).convert("RGBA")
    (visL, visT, visR, visB), counts = opaque_bounds(im)
    if visR < visL:
        return {"error": "empty", "stem": path.stem[4:]}
    ox, oy = ground_anchor(im, visB)
    vis_w = visR - visL + 1
    vis_elev = max(8, oy - visT)
    roof_y = measure_body_roof(counts, visT, oy)
    body_elev = max(8, oy - roof_y)

    fw, fh = foot
    half_w = (fw + fh) * (TILE_W / 4.0)
    half_d = half_w * 0.5
    # Top-south ≈ body roof; bottom face is the footprint on the ground
    elev = max(8.0, float(body_elev) + 1.0)

    return {
        "file": path.name,
        "stem": path.stem[4:],
        "footW": fw,
        "footH": fh,
        "visL": visL,
        "visT": visT,
        "visR": visR,
        "visB": visB,
        "ox": ox,
        "oy": oy,
        "artCx": round(0.5 * (visL + visR), 2),
        "visW": vis_w,
        "visElev": vis_elev,
        "roofY": roof_y,
        "bodyElev": body_elev,
        "tipPx": roof_y - visT,
        "halfW": round(half_w, 2),
        "halfD": round(half_d, 2),
        "elev": round(elev, 2),
        "bsX": float(ox),
        "bsY": oy,
    }


def list_idle():
    out = []
    for p in sorted(SPR.glob("bld_*.png")):
        if any(s in p.name for s in ("_mk_f", "_scaffold", "_a_f", "_ready")):
            continue
        out.append(p)
    return out


def main() -> None:
    feet = load_footprints()
    rows = []
    for p in list_idle():
        stem = p.stem[4:]
        foot = resolve_foot(stem, feet)
        m = measure_one(p, foot)
        rows.append(m)
        if "error" in m:
            print(f"{stem:22} ERROR")
            continue
        print(
            f"{stem:22} foot={m['footW']}x{m['footH']} "
            f"halfW={m['halfW']:5.1f} halfD={m['halfD']:5.1f} "
            f"elev={m['elev']:6.1f} body={m['bodyElev']:3} tip={m['tipPx']:2}"
        )

    good = [r for r in rows if "error" not in r]
    payload = {
        "version": 3,
        "note": (
            "Bottom face = tile footprint diamond (occluded underside), dashed in renderer. "
            "elev measured per sprite so top-south ≈ body roof. South tip = ox/oy."
        ),
        "buildings": {r["stem"]: r for r in good},
    }
    text = json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    REVIEW_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(text, encoding="utf-8")
    REVIEW_JSON.write_text(text, encoding="utf-8")

    lines = [
        "// Auto-generated by tools/visual_audit/measure_bld_cages.py — DO NOT EDIT\n",
        "// elev only; halfW/halfD come from tile footprint at draw time\n",
        "struct BldCageMeas { const char* stem; float elev; };\n",
        "static const BldCageMeas kBldCageMeas[] = {\n",
    ]
    for r in sorted(good, key=lambda x: x["stem"]):
        lines.append(f'    {{"{r["stem"]}", {r["elev"]:.2f}f}},\n')
    lines.append("};\n")
    lines.append(f"static const int kBldCageMeasN = {len(good)};\n")
    INC.write_text("".join(lines), encoding="utf-8")

    print(f"\nWrote {len(good)} -> {OUT_JSON}")
    print(f"         {INC}")


if __name__ == "__main__":
    main()
