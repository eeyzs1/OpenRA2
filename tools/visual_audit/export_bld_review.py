"""Export each building sprite asset for human review (NOT in-game screenshots).

Reads assets/sprites/bld_<name>.png directly, writes:
  tools/visual_audit/bld_review/singles/<NN>_<name>.png   — one labeled card per building
  tools/visual_audit/bld_review/sheet_01.png ...          — contact sheets
  tools/visual_audit/bld_review/index.txt                 — checklist + MIX id + size

Usage (repo root):
  python tools/visual_audit/export_bld_review.py
"""
from __future__ import annotations

import os
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SPR = ROOT / "assets" / "sprites"
OUT = Path(__file__).resolve().parent / "bld_review"
SINGLES = OUT / "singles"

# eng name → RA2 SHP Image id(s) used by gen_assets.py
MIX_IDS: dict[str, list[str]] = {
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
    "teslacoil": ["NATSLA", "nttslamk"],
    "flakcannon": ["NAFLAK"],
    "grandcannon": ["GTGCAN", "gagcan", "gtgcantur", "gtgcanbarl"],
    "patriotmissile": ["NASAM"],
    "wall": ["GAWALL"],
    "orepurifier": ["GAOREP"],
    "industrialplant": ["NAINDP"],
    "techpowerplant": ["CAPOWR"],
    "nukesilo": ["NAMISL", "namislmk"],
    "weatherdevice": ["GAWEAT"],
    "ironcurtain": ["NAIRON"],
    "chronosphere": ["GACSPH", "gtcsphmk"],
    "oilderrick": ["CAOILD"],
    "hospital": ["CAHOSP"],
    "machineshop": ["CAMACH"],
    "cloningvat": ["NACLON"],
    "servicedepot": ["GADEPT", "gadeptmk"],
    "gapgenerator": ["GAGAP"],
    "spysat": ["GASPST", "gaspstmk"],
    "psychicsensor": ["NAPSIS"],
    "techairport": ["CAAIRP"],
    "secretlab": ["CASLAB"],
    "civhouse": ["CAHSE01"],
    "techoutpost": ["CAOUTP"],
    "battlebunker": ["NABNKR", "nabnkrmk"],
    "tankbunker": ["NATBNK", "ngtbnkmk"],
    "bioreactor": ["YAPOWR"],
    "gatlingcannon": ["YAGGUN"],
    "grinder": ["YAGRND"],
    "geneticmutator": ["YAGNTC"],
    "psychicdominator": ["YAPPET"],
    "psychictower": ["YAPSYT"],
    "robotcontrol": ["GAROBO"],
}

# Prefer this order for review (common first)
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


def checkerboard(w: int, h: int, cell: int = 8) -> Image.Image:
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    c0, c1 = (48, 50, 56, 255), (36, 38, 42, 255)
    for y in range(h):
        for x in range(w):
            px[x, y] = c0 if ((x // cell) + (y // cell)) % 2 == 0 else c1
    return im


def font(size: int):
    for name in ("arial.ttf", "segoeui.ttf", "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def is_missing_placeholder(im: Image.Image) -> bool:
    """SPRITE-MISSING magenta placeholder heuristic."""
    a = list(im.convert("RGBA").getdata())
    n = len(a)
    if n == 0:
        return True
    mag = sum(1 for r, g, b, al in a if al > 40 and r > 200 and g < 80 and b > 180)
    return mag / n > 0.15


def opaque_bounds(im: Image.Image) -> tuple[int, int, int, int, int]:
    rgba = im.convert("RGBA")
    w, h = rgba.size
    px = rgba.load()
    xs, ys = [], []
    for y in range(h):
        for x in range(w):
            if px[x, y][3] > 60:
                xs.append(x)
                ys.append(y)
    if not xs:
        return 0, 0, w - 1, h - 1, 0
    return min(xs), min(ys), max(xs), max(ys), len(xs)


def make_card(name: str, src: Path, idx: int, total: int) -> tuple[Image.Image, str]:
    spr = Image.open(src).convert("RGBA")
    visL, visT, visR, visB, opac = opaque_bounds(spr)
    mix = ",".join(MIX_IDS.get(name, ["?"]))
    miss = is_missing_placeholder(spr)
    flag = "MISSING-PLACEHOLDER" if miss else ("EMPTY" if opac < 50 else "OK?")

    # Card: sprite on checkerboard + caption strip
    pad = 16
    label_h = 54
    cw = max(spr.width + pad * 2, 280)
    ch = spr.height + pad * 2 + label_h
    card = Image.new("RGBA", (cw, ch), (24, 26, 30, 255))
    bg = checkerboard(spr.width + pad * 2, spr.height + pad * 2)
    card.paste(bg, (0, 0))
    ox = (cw - spr.width) // 2
    oy = pad
    card.paste(spr, (ox, oy), spr)

    draw = ImageDraw.Draw(card)
    f1, f2 = font(15), font(12)
    title = f"{idx:02d}/{total:02d}  bld_{name}.png"
    sub = f"MIX:{mix}  canvas={spr.width}x{spr.height}  vis={visR - visL + 1}x{visB - visT + 1}  opac={opac}  [{flag}]"
    ty = spr.height + pad * 2 + 6
    draw.rectangle([0, spr.height + pad * 2, cw, ch], fill=(18, 20, 24, 255))
    draw.text((10, ty), title, fill=(240, 240, 245, 255), font=f1)
    col = (255, 90, 90, 255) if flag != "OK?" else (160, 200, 160, 255)
    draw.text((10, ty + 22), sub, fill=col, font=f2)

    line = f"{idx:02d}\tbld_{name}.png\t{mix}\t{spr.width}x{spr.height}\tvis={visR - visL + 1}x{visB - visT + 1}\topac={opac}\t{flag}\n"
    return card.convert("RGB"), line


def contact_sheets(cards: list[Image.Image], cols: int = 4) -> list[Image.Image]:
    if not cards:
        return []
    cw = max(c.width for c in cards)
    ch = max(c.height for c in cards)
    sheets = []
    for start in range(0, len(cards), cols * 3):  # 3 rows per sheet
        batch = cards[start : start + cols * 3]
        rows = (len(batch) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cw + 8, rows * ch + 8), (12, 14, 18))
        for i, c in enumerate(batch):
            r, col = divmod(i, cols)
            cell = Image.new("RGB", (cw, ch), (12, 14, 18))
            cell.paste(c, ((cw - c.width) // 2, (ch - c.height) // 2))
            sheet.paste(cell, (4 + col * cw, 4 + r * ch))
        sheets.append(sheet)
    return sheets


def main() -> None:
    if OUT.exists():
        for p in SINGLES.glob("*.png") if SINGLES.exists() else []:
            p.unlink()
        for p in OUT.glob("sheet_*.png"):
            p.unlink()
    SINGLES.mkdir(parents=True, exist_ok=True)

    # Discover finished sprites (no mk/scaffold/anim)
    found = sorted(
        p.stem[4:]
        for p in SPR.glob("bld_*.png")
        if "_mk_f" not in p.name and "_scaffold" not in p.name and "_a_f" not in p.name
    )
    names = [n for n in ORDER if n in found]
    names += [n for n in found if n not in names]

    cards: list[Image.Image] = []
    index_lines = [
        "# Building asset review checklist\n",
        "# Mark each line: OK / WRONG / WRONG_REASON\n",
        "# Columns: idx  file  MIX  canvas  visAABB  opaquePx  autoFlag\n\n",
    ]
    missing_files = []
    for i, name in enumerate(names, 1):
        src = SPR / f"bld_{name}.png"
        if not src.exists():
            missing_files.append(name)
            continue
        card, line = make_card(name, src, i, len(names))
        cards.append(card)
        index_lines.append(line)
        card.save(SINGLES / f"{i:02d}_{name}.png")

    for j, sheet in enumerate(contact_sheets(cards), 1):
        sheet.save(OUT / f"sheet_{j:02d}.png")

    (OUT / "index.txt").write_text("".join(index_lines), encoding="utf-8")
    print(f"Exported {len(cards)} building assets -> {OUT}")
    print(f"  singles/: {len(list(SINGLES.glob('*.png')))} cards")
    print(f"  sheets: {len(list(OUT.glob('sheet_*.png')))}")
    print(f"  index:   {OUT / 'index.txt'}")
    if missing_files:
        print("MISSING files:", ", ".join(missing_files))
    flags = [ln for ln in index_lines if "MISSING-PLACEHOLDER" in ln or "\tEMPTY\t" in ln]
    if flags:
        print("Auto-suspect:")
        for ln in flags:
            print(" ", ln.strip())


if __name__ == "__main__":
    main()
