"""Export each unit idle sprite for human review (NOT in-game screenshots).

Reads assets/sprites/unit_<name>_d0_f0.png (facing 0, idle frame), writes:
  tools/visual_audit/unit_review/singles/<NN>_<中文名>.png — one labeled card per unit
  tools/visual_audit/unit_review/sheet_01.png ...          — contact sheets
  tools/visual_audit/unit_review/index.txt                 — checklist + MIX id + size

Chinese display names come from assets/rules/rules.ini [Unit.*] Name=.

Usage (repo root):
  python tools/visual_audit/export_unit_review.py
"""
from __future__ import annotations

import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SPR = ROOT / "assets" / "sprites"
RULES = ROOT / "assets" / "rules" / "rules.ini"
OUT = Path(__file__).resolve().parent / "unit_review"
SINGLES = OUT / "singles"

# Fallback when rules.ini has no [Unit.*] Name=
ZH_FALLBACK: dict[str, str] = {
    "slaveminer": "奴隶矿车",
    "pipedemo": "管道自爆",
    "slave": "奴隶",
    "unknown": "未知单位",
}

# eng name → RA2 SHP/VXL Image id(s) used by gen_assets.py
MIX_IDS: dict[str, list[str]] = {
    "gi": ["E1"],
    "conscript": ["E2"],
    "engineer": ["ENGINEER"],
    "attackdog": ["ADOG", "DOG"],
    "spy": ["SPY"],
    "flaktrooper": ["FLAKT"],
    "teslatrooper": ["SHK"],
    "sniper": ["SNIPE"],
    "tanya": ["TANY"],
    "desolator": ["DESO"],
    "chrono": ["CLEG"],
    "crazyivan": ["IVAN"],
    "terrorist": ["TERROR"],
    "navyseal": ["GHOST"],
    "yuri": ["YURI"],
    "chronocommando": ["CCOMAND"],
    "psicommando": ["PTROOP"],
    "rocketeer": ["JUMPJET"],
    "mcv": ["AMCV", "SMCV"],
    "harvester": ["HARV"],
    "chronominer": ["CMIN"],
    "warminer": ["HARV"],
    "grizzly": ["GTNK"],
    "rhino": ["HTNK"],
    "flaktrack": ["HTK"],
    "ifv": ["FV"],
    "prismtank": ["SREF"],
    "teslatank": ["TTNK"],
    "miragetank": ["MGTK"],
    "v3launcher": ["V3"],
    "apocalypse": ["APOC/MTNK"],
    "terrordrone": ["DRON"],
    "demotruck": ["DTRUCK"],
    "tankdestroyer": ["TNKD"],
    "intruder": ["ORCA"],
    "blackeagle": ["BEAG"],
    "kirov": ["ZEP"],
    "nighthawk": ["SHAD"],
    "hornet": ["HORNET"],
    "destroyer": ["DEST"],
    "typhoon": ["SUB"],
    "aegis": ["AEGIS"],
    "seascorpion": ["HYD"],
    "dreadnought": ["DRED"],
    "aircraftcarrier": ["CARRIER"],
    "amphtransport": ["SAPC"],
    "dolphin": ["DLPH"],
    "squid": ["SQD"],
    "guardiangi": ["GGI"],
    "initiate": ["INIT"],
    "brute": ["BRUTE"],
    "virus": ["VIRUS"],
    "boris": ["BORIS"],
    "robottank": ["ROBO"],
    "battlefortress": ["BFRT"],
    "gatlingtank": ["YTNK"],
    "magnetron": ["TELE"],
    "mastermind": ["MIND"],
    "chaosdrone": ["CAOS"],
    "mig": ["BPLN"],
    "siegechopper": ["SCHP"],
    "floatingdisc": ["DISK"],
    "boomer": ["BSUB"],
    "lashertank": ["LTNK"],
    "slaveminer": ["SMIN"],
    "pla": ["PLA"],
    "type99": ["TYPE99"],
    "chronoivan": ["CIVAN"],
    "slave": ["SLAV"],
    "yuriprime": ["YURIX", "YURIPR"],
}

# Human review marks from visual pass (stem → note). Empty/OK? = leave autoFlag.
# Fixed via HVA bake + MIX infantry extract — clear prior WRONG marks for re-review.
REVIEW_MARKS: dict[str, str] = {}

# Prefer this order for review (infantry → vehicles → air → naval → YR → extras)
ORDER = [
    # Allied / Soviet / shared infantry
    "gi", "conscript", "engineer", "attackdog", "spy", "flaktrooper",
    "teslatrooper", "sniper", "tanya", "desolator", "chrono", "crazyivan",
    "terrorist", "navyseal", "yuri", "chronocommando", "psicommando", "rocketeer",
    # Vehicles / miners
    "mcv", "harvester", "chronominer", "warminer", "grizzly", "rhino", "flaktrack",
    "ifv", "prismtank", "teslatank", "miragetank", "v3launcher", "apocalypse",
    "terrordrone", "demotruck", "tankdestroyer",
    # Air
    "intruder", "blackeagle", "kirov", "nighthawk", "hornet",
    # Naval
    "destroyer", "typhoon", "aegis", "seascorpion", "dreadnought",
    "aircraftcarrier", "amphtransport", "dolphin", "squid",
    # Yuri's Revenge
    "guardiangi", "initiate", "brute", "virus", "boris",
    "robottank", "battlefortress", "gatlingtank", "magnetron", "mastermind",
    "chaosdrone", "mig", "siegechopper", "floatingdisc", "boomer",
    "lashertank", "slaveminer",
    # Fusion / extras (MIX-extracted infantry)
    "pla", "type99", "chronoivan", "slave", "yuriprime",
]

# Orphan procedural dumps — not real RA2 units; skip in review / delete sprites.
SKIP_STEMS = {"pipedemo", "unknown"}

ANIM_SUFFIXES = ("_walk", "_fire", "_die", "_dep", "_unload")
_INVALID_FN = re.compile(r'[\\/:*?"<>|]')


def load_zh_names() -> dict[str, str]:
    """Map engine stem (lowercase) → Chinese Name from rules.ini."""
    out: dict[str, str] = dict(ZH_FALLBACK)
    if not RULES.exists():
        return out
    text = RULES.read_text(encoding="utf-8")
    for sec in re.split(r"\n(?=\[)", text):
        m = re.match(r"\[Unit\.([^\]]+)\]", sec)
        if not m:
            continue
        nm = re.search(r"^Name=(.+)$", sec, re.M)
        if nm:
            out[m.group(1).lower()] = nm.group(1).strip()
    return out


def zh_of(stem: str, zh_map: dict[str, str]) -> str:
    return zh_map.get(stem.lower(), stem)


def safe_filename(zh: str) -> str:
    return _INVALID_FN.sub("_", zh).strip() or "unnamed"


def checkerboard(w: int, h: int, cell: int = 8) -> Image.Image:
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = im.load()
    c0, c1 = (48, 50, 56, 255), (36, 38, 42, 255)
    for y in range(h):
        for x in range(w):
            px[x, y] = c0 if ((x // cell) + (y // cell)) % 2 == 0 else c1
    return im


def font(size: int):
    # Prefer CJK fonts so Chinese titles render; fall back to Latin.
    for name in (
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "msyh.ttc",
        "simhei.ttf",
        "arial.ttf",
        "segoeui.ttf",
    ):
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


def is_idle_stem(stem: str) -> bool:
    return not any(stem.endswith(s) or f"{s}_" in stem for s in ANIM_SUFFIXES)


def load_unit_preview(name: str, src: Path) -> tuple[Image.Image, bool]:
    """Body + same-canvas turret overlay. Prefer d2 (¾ view) when available — closer to cameo."""
    body_path = src
    d2 = SPR / f"unit_{name}_d2_f0.png"
    if d2.is_file():
        body_path = d2
    body = Image.open(body_path).convert("RGBA")
    facing = "d2" if body_path == d2 else "d0"
    tur_path = SPR / f"turret_{name}_{facing}.png"
    if not tur_path.exists():
        tur_path = SPR / f"turret_{name}_d0.png"
    if not tur_path.exists():
        return body, False
    tur = Image.open(tur_path).convert("RGBA")
    if tur.size != body.size:
        layer = Image.new("RGBA", body.size, (0, 0, 0, 0))
        layer.paste(tur, (0, 0), tur)
        tur = layer
    return Image.alpha_composite(body, tur), True


def make_card(
    name: str, zh: str, src: Path, idx: int, total: int, review: str = "OK?"
) -> tuple[Image.Image, str]:
    spr, has_tur = load_unit_preview(name, src)
    visL, visT, visR, visB, opac = opaque_bounds(spr)
    # 裁到不透明包围盒再居中，避免大画布偏心看起来像「残缺裁切」
    m = 6
    crop = spr.crop(
        (
            max(0, visL - m),
            max(0, visT - m),
            min(spr.width, visR + 1 + m),
            min(spr.height, visB + 1 + m),
        )
    )
    mix = ",".join(MIX_IDS.get(name, ["?"]))
    miss = is_missing_placeholder(spr)
    auto = "MISSING-PLACEHOLDER" if miss else ("EMPTY" if opac < 50 else "OK?")
    flag = review if review != "OK?" else auto
    tur_tag = "+tur" if has_tur else "body-only"

    pad = 16
    label_h = 54
    cw = max(crop.width + pad * 2, 300)
    ch = crop.height + pad * 2 + label_h
    card = Image.new("RGBA", (cw, ch), (24, 26, 30, 255))
    bg = checkerboard(crop.width + pad * 2, crop.height + pad * 2)
    card.paste(bg, (0, 0))
    ox = (cw - crop.width) // 2
    oy = pad
    card.paste(crop, (ox, oy), crop)

    draw = ImageDraw.Draw(card)
    f1, f2 = font(16), font(12)
    title = f"{idx:02d}/{total:02d}  {zh}  ({name})"
    sub = (
        f"MIX:{mix}  {tur_tag}  canvas={spr.width}x{spr.height}  "
        f"vis={visR - visL + 1}x{visB - visT + 1}  opac={opac}  [{flag}]"
    )
    ty = crop.height + pad * 2 + 6
    draw.rectangle([0, crop.height + pad * 2, cw, ch], fill=(18, 20, 24, 255))
    draw.text((10, ty), title, fill=(240, 240, 245, 255), font=f1)
    col = (255, 90, 90, 255) if not flag.startswith("OK") else (160, 200, 160, 255)
    draw.text((10, ty + 22), sub, fill=col, font=f2)

    line = (
        f"{idx:02d}\t{zh}\t{name}\tunit_{name}_d0_f0.png\t{mix}\t{tur_tag}\t"
        f"{spr.width}x{spr.height}\tvis={visR - visL + 1}x{visB - visT + 1}\t"
        f"opac={opac}\t{flag}\n"
    )
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

    zh_map = load_zh_names()

    # Discover idle sprites: unit_<name>_d0_f0.png (skip walk/fire/die/dep/unload)
    found: list[str] = []
    for p in sorted(SPR.glob("unit_*_d0_f0.png")):
        stem = p.stem  # unit_<name>_d0_f0
        if not stem.startswith("unit_") or not stem.endswith("_d0_f0"):
            continue
        name = stem[len("unit_") : -len("_d0_f0")]
        if is_idle_stem(name) and name not in SKIP_STEMS:
            found.append(name)

    names = [n for n in ORDER if n in found]
    names += [n for n in found if n not in names]

    cards: list[Image.Image] = []
    index_lines = [
        "# 单位素材审核清单\n",
        "# 每行标注: OK / WRONG / WRONG_REASON\n",
        "# 列: idx  中文名  英文stem  源文件  MIX  body+tur  canvas  visAABB  opaquePx  flag\n",
        "# 预览: unit_*_d0_f0.png + 同画布 turret_*_d0.png（有炮塔则叠绘）\n",
        "# 注: 坦克「没头」多为车身/炮塔分文件；请看 +tur 预览后再判\n\n",
    ]
    missing_files = []
    for i, name in enumerate(names, 1):
        src = SPR / f"unit_{name}_d0_f0.png"
        if not src.exists():
            missing_files.append(name)
            continue
        zh = zh_of(name, zh_map)
        card, line = make_card(name, zh, src, i, len(names), REVIEW_MARKS.get(name, "OK?"))
        cards.append(card)
        index_lines.append(line)
        card.save(SINGLES / f"{i:02d}_{safe_filename(zh)}.png")

    for j, sheet in enumerate(contact_sheets(cards), 1):
        sheet.save(OUT / f"sheet_{j:02d}.png")

    (OUT / "index.txt").write_text("".join(index_lines), encoding="utf-8")
    print(f"Exported {len(cards)} unit assets -> {OUT}")
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
