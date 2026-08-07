"""Export building idle sprites with selection-cage overlay for human review.

Cage from assets/meta/bld_cage.json (fit_bld_cages.py):
  Foundation fw×fh at geo SE + fitted elev; engine nudge (-2,+1).

Usage:
  python tools/visual_audit/fit_bld_cages.py
  python tools/visual_audit/export_bld_cage_review.py
"""
from __future__ import annotations

import json
import math
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SPR = ROOT / "assets" / "sprites"
DATA_CPP = ROOT / "src" / "game" / "data.cpp"
ASSETS_H = ROOT / "src" / "gfx" / "assets.h"
MEASURED = ROOT / "assets" / "meta" / "bld_cage.json"
OUT = Path(__file__).resolve().parent / "bld_review" / "cage"
SINGLES = OUT / "singles"

TILE_W = 64
TILE_H = 32
EDGE = (255, 240, 60, 245)

ORDER = [
    "conyard", "powerplant", "teslareactor", "bioreactor", "nuclearreactor", "techpowerplant",
    "barracks", "warfactory", "orerefinery", "radar", "battlelab", "airforcecmd", "navalyard",
    "servicedepot", "orepurifier", "industrialplant", "cloningvat", "robotcontrol",
    "pillbox", "sentrygun", "prismtower", "teslacoil", "flakcannon", "grandcannon",
    "patriotmissile", "gatlingcannon", "battlebunker", "tankbunker", "psychictower",
    "nukesilo", "weatherdevice", "ironcurtain", "chronosphere",
    "geneticmutator", "psychicdominator", "gapgenerator", "spysat", "psychicsensor",
    "oilderrick", "hospital", "machineshop", "techairport", "secretlab", "techoutpost",
    "civhouse", "grinder", "wall",
]


def font(size: int):
    for name in (
        "arial.ttf",
        "segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    ):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def load_measured() -> dict:
    if not MEASURED.exists():
        return {}
    data = json.loads(MEASURED.read_text(encoding="utf-8"))
    return data.get("buildings", {})


def load_asset_name_map() -> dict[str, str]:
    text = ASSETS_H.read_text(encoding="utf-8", errors="replace")
    return {
        enum: stem
        for enum, stem in re.findall(
            r"case\s+BldType::(\w+):\s*return\s+\"([a-z0-9_]+)\";", text
        )
    }


def load_footprints() -> dict[str, tuple[int, int]]:
    names = load_asset_name_map()
    text = DATA_CPP.read_text(encoding="utf-8", errors="replace")
    feet: dict[str, tuple[int, int]] = {}
    for enum, w, h in re.findall(
        r"\{BldType::(\w+)\s*,[^,]*,\s*\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*(\d+)\s*,\s*(\d+)\s*,",
        text,
    ):
        stem = names.get(enum)
        if stem:
            feet[stem] = (int(w), int(h))
    return feet


def checker(w: int, h: int, cell: int = 8) -> Image.Image:
    im = Image.new("RGBA", (w, h), (36, 38, 42, 255))
    px = im.load()
    c0, c1 = (48, 50, 56, 255), (36, 38, 42, 255)
    for y in range(h):
        for x in range(w):
            px[x, y] = c0 if ((x // cell) + (y // cell)) % 2 == 0 else c1
    return im


def dash_line(draw: ImageDraw.ImageDraw, a, b, fill, thick=2):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    length = math.hypot(dx, dy)
    if length < 1:
        return
    if length < 14:
        draw.line([a, b], fill=fill, width=thick)
        return
    dx /= length
    dy /= length
    dash, gap = 5.0, 3.0
    t = 0.0
    while t < length:
        t1, t2 = t, min(length, t + dash)
        draw.line(
            [(ax + dx * t1, ay + dy * t1), (ax + dx * t2, ay + dy * t2)],
            fill=fill,
            width=thick,
        )
        t += dash + gap


def foot_corners(bs, e_arm_x, e_arm_y, w_arm_x, w_arm_y):
    be = (bs[0] + e_arm_x, bs[1] + e_arm_y)
    bw = (bs[0] + w_arm_x, bs[1] + w_arm_y)
    bn = (bs[0] + e_arm_x + w_arm_x, bs[1] + e_arm_y + w_arm_y)
    return bn, be, bw


def draw_iso_cuboid(draw, bs, e_arm_x, e_arm_y, w_arm_x, w_arm_y, elev: float, edge):
    elev = max(8.0, min(260.0, elev))
    bn, be, bw = foot_corners(bs, e_arm_x, e_arm_y, w_arm_x, w_arm_y)
    tn = (bn[0], bn[1] - elev)
    te = (be[0], be[1] - elev)
    ts = (bs[0], bs[1] - elev)
    tw = (bw[0], bw[1] - elev)
    for a, b in ((bn, be), (be, bs), (bs, bw), (bw, bn)):
        dash_line(draw, a, b, edge, thick=2)
    for a, b in ((tn, te), (te, ts), (ts, tw), (tw, tn)):
        dash_line(draw, a, b, edge, thick=2)
    for a, b in ((bn, tn), (be, te), (bs, ts), (bw, tw)):
        draw.line([a, b], fill=edge, width=2)


def metrics_from_measured(row: dict, foot: tuple[int, int], canvas_w: int) -> dict:
    fw, fh = max(1, foot[0]), max(1, foot[1])
    oy = float(row["oy"])
    geo_ox = float(row.get("geoOx", canvas_w / 2.0 + (fw - fh) * (TILE_W / 2.0)))
    # Match engine: nudge (-2,+1) + 4px along E→S/W→S + 3px along Ts→S.
    off_x = float(row.get("offX", 0.0)) - 2.0
    off_y = float(row.get("offY", 0.0)) + 1.0
    # Engine always draws Foundation arms from BldDef.
    e_arm_x = float(fh * (TILE_W / 2.0))
    e_arm_y = float(-fh * (TILE_H / 2.0))
    w_arm_x = float(-fw * (TILE_W / 2.0))
    w_arm_y = float(-fw * (TILE_H / 2.0))
    edge_nudge = 4.0

    def add_along(dx: float, dy: float):
        nonlocal off_x, off_y
        length = math.hypot(dx, dy)
        if length > 1e-3:
            off_x += edge_nudge * dx / length
            off_y += edge_nudge * dy / length

    add_along(-e_arm_x, -e_arm_y)  # E → S
    add_along(-w_arm_x, -w_arm_y)  # W → S
    off_y += 3.0  # Ts → S
    half_w = 0.5 * (abs(e_arm_x) + abs(w_arm_x))
    bs = (geo_ox + off_x, oy + off_y)
    depth = abs(e_arm_y + w_arm_y)
    vis_elev = float(row.get("visElev", 0))
    elev = min(float(row["elev"]), max(8.0, vis_elev - 4.0))
    elev = max(8.0, min(280.0, elev))
    return {
        "visL": row.get("visL", 0),
        "visT": row.get("visT", 0),
        "visR": row.get("visR", 0),
        "visB": row.get("visB", row["oy"]),
        "oy": oy,
        "geo_ox": geo_ox,
        "off_x": off_x,
        "off_y": off_y,
        "half_w": half_w,
        "vis_w": row.get("visW", 0),
        "vis_elev": vis_elev,
        "fw": fw,
        "fh": fh,
        "e_arm_x": e_arm_x,
        "e_arm_y": e_arm_y,
        "w_arm_x": w_arm_x,
        "w_arm_y": w_arm_y,
        "depth": depth,
        "cage_elev": elev,
        "bs": bs,
    }


def render_cage_card(
    name: str, src: Path, foot: tuple[int, int], idx: int, total: int, measured: dict
) -> tuple[Image.Image, str]:
    spr = Image.open(src).convert("RGBA")
    row = measured.get(name)
    if not row:
        raise SystemExit(
            f"Missing measured cage for bld_{name}.png — run fit_bld_cages.py first"
        )
    m = metrics_from_measured(row, foot, spr.width)
    bs = m["bs"]
    fw, fh = m["fw"], m["fh"]

    pad = 20
    label_h = 58
    left = int(min(0, bs[0] + min(0, m["e_arm_x"], m["w_arm_x"], m["e_arm_x"] + m["w_arm_x"]) - 4))
    top = int(
        min(
            0,
            bs[1]
            + min(0, m["e_arm_y"], m["w_arm_y"], m["e_arm_y"] + m["w_arm_y"])
            - m["cage_elev"]
            - 4,
        )
    )
    right = int(
        max(
            spr.width,
            bs[0] + max(0, m["e_arm_x"], m["w_arm_x"], m["e_arm_x"] + m["w_arm_x"]) + 4,
            m["visR"] + 4,
        )
    )
    bottom = int(max(spr.height, bs[1] + 4, m["visB"] + 4))
    cw = right - left + pad * 2
    ch = bottom - top + pad * 2 + label_h
    off = (pad - left, pad - top)

    card = Image.new("RGBA", (cw, ch), (24, 26, 30, 255))
    bg = checker(cw, ch - label_h)
    card.paste(bg, (0, 0))
    layer = Image.new("RGBA", (cw, ch - label_h), (0, 0, 0, 0))
    layer.alpha_composite(spr, off)
    overlay = Image.new("RGBA", (cw, ch - label_h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay, "RGBA")
    cage_bs = (bs[0] + off[0], bs[1] + off[1])
    draw_iso_cuboid(
        draw,
        cage_bs,
        m["e_arm_x"],
        m["e_arm_y"],
        m["w_arm_x"],
        m["w_arm_y"],
        m["cage_elev"],
        EDGE,
    )
    r = 3
    draw.ellipse(
        [cage_bs[0] - r, cage_bs[1] - r, cage_bs[0] + r, cage_bs[1] + r],
        outline=(255, 0, 255, 220),
    )
    layer = Image.alpha_composite(layer, overlay)
    card.paste(layer, (0, 0), layer)

    d = ImageDraw.Draw(card)
    d.rectangle([0, ch - label_h, cw, ch], fill=(18, 20, 24, 255))
    f1, f2 = font(14), font(11)
    title = f"{idx:02d}/{total:02d}  bld_{name}.png  +cage (Foundation@geo)"
    sub = (
        f"foot={fw}x{fh}  visW={m['vis_w']} visElev={m['vis_elev']}  "
        f"halfW={m['half_w']:.0f} elev={m['cage_elev']:.0f}  "
        f"off=({m['off_x']:+.0f},{m['off_y']:+.0f})"
    )
    d.text((10, ch - label_h + 8), title, fill=(255, 220, 80, 255), font=f1)
    d.text((10, ch - label_h + 30), sub, fill=(180, 180, 186, 255), font=f2)

    line = (
        f"{idx:02d}\tbld_{name}.png\tfoot={fw}x{fh}\t"
        f"visW={m['vis_w']}\tvisElev={m['vis_elev']}\t"
        f"halfW={m['half_w']:.1f}\telev={m['cage_elev']:.1f}\t"
        f"off=({m['off_x']:+.0f},{m['off_y']:+.0f})\tOK?\n"
    )
    return card.convert("RGB"), line


def contact_sheets(cards: list[Image.Image], cols: int = 4) -> list[Image.Image]:
    if not cards:
        return []
    cw = max(c.width for c in cards)
    ch = max(c.height for c in cards)
    sheets = []
    per = cols * 3
    for start in range(0, len(cards), per):
        batch = cards[start : start + per]
        rows = (len(batch) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cw + 8, rows * ch + 8), (12, 14, 18))
        for i, c in enumerate(batch):
            r, col = divmod(i, cols)
            cell = Image.new("RGB", (cw, ch), (12, 14, 18))
            cell.paste(c, ((cw - c.width) // 2, (ch - c.height) // 2))
            sheet.paste(cell, (4 + col * cw, 4 + r * ch))
        sheets.append(sheet)
    return sheets


def resolve_foot(name: str, feet: dict[str, tuple[int, int]]) -> tuple[int, int]:
    stem_path = ROOT / "assets" / "meta" / "bld_stem_feet.json"
    if stem_path.exists():
        stem_feet = json.loads(stem_path.read_text(encoding="utf-8")).get("buildings", {})
        if name in stem_feet:
            w, h = stem_feet[name]
            return int(w), int(h)
    base = name
    for suf in ("_sov", "_allied", "_yuri"):
        if base.endswith(suf):
            base = base[: -len(suf)]
    m = re.match(r"^(civhouse)\d*$", base)
    if m:
        base = m.group(1)
    foot = feet.get(base) or feet.get(name)
    if foot:
        return foot
    aliases = {
        "airforcecmd_usa": "airforcecmd",
        "sandbags": "wall",
        "psychicbeacon": "psychicsensor",
    }
    alt = aliases.get(name) or aliases.get(base)
    return feet.get(alt, (2, 2)) if alt else (2, 2)


def main() -> None:
    measured = load_measured()
    if not measured:
        print("No cage data. Run: python tools/visual_audit/gen_bld_cage_from_art.py", file=sys.stderr)
        sys.exit(1)
    feet = load_footprints()
    print(f"Cage data: {len(measured)}  footprints: {len(feet)}")

    if OUT.exists():
        for p in SINGLES.glob("*.png") if SINGLES.exists() else []:
            p.unlink()
        for p in OUT.glob("sheet_*.png"):
            p.unlink()
    SINGLES.mkdir(parents=True, exist_ok=True)

    found = sorted(
        p.stem[4:]
        for p in SPR.glob("bld_*.png")
        if "_mk_f" not in p.name
        and "_scaffold" not in p.name
        and "_a_f" not in p.name
        and "_ready" not in p.name
    )
    names = [n for n in ORDER if n in found]
    names += [n for n in found if n not in names]

    cards: list[Image.Image] = []
    index = [
        "# Building cages: Foundation@geo + fitted elev; nudge (-2,+1)\n",
        "# Source: assets/meta/bld_cage.json via fit_bld_cages.py\n",
        "# Mark: OK / WRONG / WRONG_REASON\n\n",
    ]
    for i, name in enumerate(names, 1):
        src = SPR / f"bld_{name}.png"
        row = measured.get(name, {})
        if row.get("footW") and row.get("footH"):
            foot = (int(row["footW"]), int(row["footH"]))
        else:
            foot = resolve_foot(name, feet)
        card, line = render_cage_card(name, src, foot, i, len(names), measured)
        cards.append(card)
        index.append(line)
        card.save(SINGLES / f"{i:02d}_{name}_cage.png")

    for j, sheet in enumerate(contact_sheets(cards), 1):
        sheet.save(OUT / f"sheet_{j:02d}.png")

    (OUT / "index.txt").write_text("".join(index), encoding="utf-8")
    print(f"Exported {len(cards)} measured cage cards -> {OUT}")


if __name__ == "__main__":
    main()
