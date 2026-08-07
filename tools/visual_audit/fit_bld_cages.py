"""Fit per-building selection cage elevation (Foundation @ geo SE).

Bottom = full tile Foundation (fw×fh) at geo ox / opaque south oy.
Elevation ≈ body roof (no half-depth crush). offX/offY = 0.
Engine adds global nudge (-2,+1) only.

Writes:
  assets/meta/bld_cage.json
  src/gfx/bld_cage_data.inc

Usage:
  python tools/visual_audit/fit_bld_cages.py
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
OVERRIDE = ROOT / "assets" / "meta" / "bld_cage_overrides.json"
REVIEW_JSON = Path(__file__).resolve().parent / "bld_review" / "cage" / "measured.json"
INC = ROOT / "src" / "gfx" / "bld_cage_data.inc"
ALPHA = 60
TILE_W = 64
TILE_H = 32


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
    """Skip thin antennae / masts at the top; stop at first 'body-wide' row."""
    if oy <= visT:
        return visT
    vis_elev = oy - visT
    max_n = max(counts[visT : oy + 1]) if oy >= visT else 1
    # Thin spikes (1–4px) are common on Yuri / radar masts — look deeper than 10%.
    thresh = max(5, min(14, int(max_n * 0.06)))
    max_tip = max(8, min(56, int(vis_elev * 0.35)))
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
    stem_path = ROOT / "assets" / "meta" / "bld_stem_feet.json"
    if stem_path.exists():
        stem_feet = json.loads(stem_path.read_text(encoding="utf-8")).get("buildings", {})
        if name in stem_feet:
            w, h = stem_feet[name]
            return int(w), int(h)
    if name in feet:
        return feet[name]
    for suf in ("_sov", "_usa", "_yuri"):
        if name.endswith(suf):
            base = name[: -len(suf)]
            if base in feet:
                return feet[base]
    return (2, 2)


def load_overrides() -> dict:
    if not OVERRIDE.exists():
        return {}
    data = json.loads(OVERRIDE.read_text(encoding="utf-8"))
    return data.get("buildings", data)


def fit_one(path: Path, foot: tuple[int, int]) -> dict:
    from PIL import Image

    im = Image.open(path).convert("RGBA")
    (visL, visT, visR, visB), counts = opaque_bounds(im)
    if visR < visL:
        return {"error": "empty", "stem": path.stem[4:]}
    tip_ox, tip_oy = ground_anchor(im, visB)
    vis_w = visR - visL + 1
    vis_elev = max(8, tip_oy - visT)

    fw, fh = max(1, foot[0]), max(1, foot[1])
    geo_ox = im.width / 2.0 + (fw - fh) * (TILE_W / 2.0)

    # Full Foundation parallelogram from SE tip.
    e_arm_x = float(fh * (TILE_W / 2))
    e_arm_y = float(-fh * (TILE_H / 2))
    w_arm_x = float(-fw * (TILE_W / 2))
    w_arm_y = float(-fw * (TILE_H / 2))
    half_w = 0.5 * (abs(e_arm_x) + abs(w_arm_x))
    depth = abs(e_arm_y + w_arm_y)

    roof_y = measure_body_roof(counts, visT, tip_oy)
    body_elev = max(8, tip_oy - roof_y)
    # Soft roof only — do not subtract foundation depth.
    elev = max(8.0, float(body_elev))

    art_cx = 0.5 * (visL + visR)
    tip_d = float(tip_ox - geo_ox)
    art_d = float(art_cx - geo_ox)
    # Only when SE tip and geo disagree a lot — otherwise keep Foundation@geo (warfactory tipΔ≈0).
    if abs(tip_d) >= 16.0:
        off_x = art_d
    else:
        off_x = 0.0
    if abs(off_x) < 6.0:
        off_x = 0.0
    off_y = 0.0

    return {
        "file": path.name,
        "stem": path.stem[4:],
        "footW": fw,
        "footH": fh,
        "visL": visL,
        "visT": visT,
        "visR": visR,
        "visB": visB,
        "ox": tip_ox,
        "geoOx": round(geo_ox, 2),
        "oy": tip_oy,
        "artCx": round(art_cx, 2),
        "tipOx": tip_ox,
        "tipMinusGeo": round(tip_ox - geo_ox, 2),
        "visW": vis_w,
        "visElev": vis_elev,
        "roofY": roof_y,
        "bodyElev": body_elev,
        "halfW": round(half_w, 2),
        "halfD": round(0.5 * depth, 2),
        "eArmX": round(e_arm_x, 2),
        "eArmY": round(e_arm_y, 2),
        "wArmX": round(w_arm_x, 2),
        "wArmY": round(w_arm_y, 2),
        "elev": round(elev, 2),
        "offX": round(off_x, 2),
        "offY": round(off_y, 2),
        "bsX": round(geo_ox + off_x, 2),
        "bsY": round(tip_oy + off_y, 2),
    }


def apply_override(row: dict, ov: dict) -> dict:
    for k in ("eArmX", "eArmY", "wArmX", "wArmY", "elev", "offX", "offY", "halfW", "halfD"):
        if k in ov:
            row[k] = float(ov[k])
    if "footW" in ov and "footH" in ov:
        fw, fh = max(1, int(ov["footW"])), max(1, int(ov["footH"]))
        row["footW"], row["footH"] = fw, fh
        row["eArmX"] = float(fh * (TILE_W / 2))
        row["eArmY"] = float(-fh * (TILE_H / 2))
        row["wArmX"] = float(-fw * (TILE_W / 2))
        row["wArmY"] = float(-fw * (TILE_H / 2))
        row["halfW"] = round(0.5 * (abs(row["eArmX"]) + abs(row["wArmX"])), 2)
        row["halfD"] = round(0.5 * abs(row["eArmY"] + row["wArmY"]), 2)
        # geo SE x shifts with non-square foot
        cw = float(row.get("visR", 0) - row.get("visL", 0) + 1)
        # prefer canvas width from ox/geo relationship: ox tip is independent
        # reconstruct canvas W from stored geoOx formula inverse is messy — use tip+geo span
        # Store was: geo_ox = width/2 + (fw-fh)*TILE_W/2. Keep tip oy; recompute geo from
        # approximate width via vis bounds padded (sprite full width unknown). Use oy as tip_oy
        # and estimate width from file via halfW*2 heuristic — better: keep geoOx if foot
        # change is applied pre-fit. Here adjust geoOx by delta of (fw-fh) term.
        old_fw, old_fh = int(row.get("_fitFootW", fw)), int(row.get("_fitFootH", fh))
        # If fit already used same foot, geoOx ok. When override changes foot after fit:
        tip_oy = float(row["oy"])
        # Re-derive canvas width from geoOx of fit: geo = W/2 + (old_fw-old_fh)*TW/2
        # We don't store W; approximate W ≈ visR+pad. Use ox/geo: actually store width in fit.
        if "canvasW" in row:
            w = float(row["canvasW"])
            row["geoOx"] = round(w / 2.0 + (fw - fh) * (TILE_W / 2.0), 2)
        else:
            # fallback: shift geo by change in (fw-fh) term
            d_old = (old_fw - old_fh) * (TILE_W / 2.0)
            d_new = (fw - fh) * (TILE_W / 2.0)
            row["geoOx"] = round(float(row["geoOx"]) - d_old + d_new, 2)
    geo = float(row.get("geoOx", row["ox"]))
    row["bsX"] = round(geo + row["offX"], 2)
    row["bsY"] = round(row["oy"] + row["offY"], 2)
    row["override"] = True
    return row


def list_idle():
    out = []
    for p in sorted(SPR.glob("bld_*.png")):
        if any(s in p.name for s in ("_mk_f", "_scaffold", "_a_f", "_ready")):
            continue
        out.append(p)
    return out


def main() -> None:
    feet = load_footprints()
    overrides = load_overrides()
    rows = []
    for p in list_idle():
        stem = p.stem[4:]
        foot = resolve_foot(stem, feet)
        ov = overrides.get(stem, {})
        # Cage-only foot override applied before fit so geo/arms match.
        if "footW" in ov and "footH" in ov:
            foot = (max(1, int(ov["footW"])), max(1, int(ov["footH"])))
        m = fit_one(p, foot)
        if "error" not in m:
            from PIL import Image

            im = Image.open(p)
            m["canvasW"] = im.width
            m["_fitFootW"], m["_fitFootH"] = foot[0], foot[1]
        rows.append(m)
        if "error" in m:
            print(f"{stem:22} ERROR")
            continue
        if stem in overrides:
            m = apply_override(m, overrides[stem])
            rows[-1] = m
        tag = " OV" if m.get("override") else ""
        tip_d = m.get("tipMinusGeo", 0)
        print(
            f"{stem:22} foot={m['footW']}x{m['footH']} elev={m['elev']:6.1f} "
            f"off=({m['offX']:+.0f},{m['offY']:+.0f}) tipΔgeo={tip_d:+.0f}{tag}"
        )

    good = [r for r in rows if "error" not in r]
    payload = {
        "version": 12,
        "note": (
            "Foundation@geo: bottom parallelogram at geo SE; "
            "optional cage-only footW/H override (collision stays BldDef); "
            "elev≈body roof; engine nudge (-2,+1)+edge."
        ),
        "buildings": {r["stem"]: r for r in good},
    }
    text = json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    REVIEW_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(text, encoding="utf-8")
    REVIEW_JSON.write_text(text, encoding="utf-8")

    lines = [
        "// Auto-generated by tools/visual_audit/fit_bld_cages.py — DO NOT EDIT\n",
        "struct BldCageMeas {\n",
        "    const char* stem;\n",
        "    float elev, offX, offY;\n",
        "    int footW, footH; // 0,0 = use BldDef w×h\n",
        "};\n",
        "static const BldCageMeas kBldCageMeas[] = {\n",
    ]
    for r in sorted(good, key=lambda x: x["stem"]):
        # Only emit non-art feet when override changed them vs stem_feet / when explicitly set
        ov = overrides.get(r["stem"], {})
        if "footW" in ov and "footH" in ov:
            fw, fh = int(r["footW"]), int(r["footH"])
        else:
            fw, fh = 0, 0  # engine falls back to BldDef
        lines.append(
            f'    {{"{r["stem"]}", {r["elev"]:.2f}f, '
            f'{r["offX"]:.2f}f, {r["offY"]:.2f}f, {fw}, {fh}}},\n'
        )
    lines.append("};\n")
    lines.append(f"static const int kBldCageMeasN = {len(good)};\n")
    INC.write_text("".join(lines), encoding="utf-8")

    print(f"\nWrote {len(good)} -> {OUT_JSON}")
    print(f"         {INC}")
    if overrides:
        print(f"Overrides applied: {len(overrides)} from {OVERRIDE.name}")


if __name__ == "__main__":
    main()
