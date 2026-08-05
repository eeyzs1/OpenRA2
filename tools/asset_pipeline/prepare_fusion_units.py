# Build PLA / Type99 pipeline fixtures from extracted conscript / rhino sprites.
# Stylize via make_fusion_units (same remap rules), write facing_frame PNGs for publish_unit.
#
#   python tools/asset_pipeline/prepare_fusion_units.py
#   python tools/asset_pipeline/publish_unit.py --manifest tools/asset_pipeline/templates/pla/manifest.yaml
#   python tools/asset_pipeline/publish_unit.py --manifest tools/asset_pipeline/templates/type99/manifest.yaml
from __future__ import annotations

import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SPR = ROOT / "assets" / "sprites"
TMPL = Path(__file__).resolve().parent / "templates"
sys.path.insert(0, str(ROOT / "tools" / "ra2pack"))
from make_fusion_units import (  # noqa: E402
    stylize_pla,
    stylize_type99_body,
    stylize_type99_turret,
)


def _clear_dir(d: Path) -> None:
    if d.is_dir():
        for p in d.rglob("*"):
            if p.is_file():
                p.unlink()
    d.mkdir(parents=True, exist_ok=True)


def _write(im: Image.Image, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path)


def prepare_pla() -> int:
    out = TMPL / "pla" / "fixtures"
    _clear_dir(out)
    n = 0
    # stand: unit_conscript_d{D}_f{F}.png
    for src in sorted(SPR.glob("unit_conscript_d*_f*.png")):
        m = re.match(r"unit_conscript_d(\d+)_f(\d+)\.png$", src.name)
        if not m:
            continue
        d, f = int(m.group(1)), int(m.group(2))
        _write(stylize_pla(Image.open(src)), out / "stand" / f"{d}_{f:02d}.png")
        n += 1
    for src in sorted(SPR.glob("unit_conscript_walk_d*_f*.png")):
        m = re.match(r"unit_conscript_walk_d(\d+)_f(\d+)\.png$", src.name)
        if not m:
            continue
        d, f = int(m.group(1)), int(m.group(2))
        _write(stylize_pla(Image.open(src)), out / "walk" / f"{d}_{f:02d}.png")
        n += 1
    for src in sorted(SPR.glob("unit_conscript_fire_d*_f*.png")):
        m = re.match(r"unit_conscript_fire_d(\d+)_f(\d+)\.png$", src.name)
        if not m:
            continue
        d, f = int(m.group(1)), int(m.group(2))
        _write(stylize_pla(Image.open(src)), out / "fire" / f"{d}_{f:02d}.png")
        n += 1
    # die: facing-independent — store under die/0_XX only
    for src in sorted(SPR.glob("unit_conscript_die_f*.png")):
        m = re.match(r"unit_conscript_die_f(\d+)\.png$", src.name)
        if not m:
            continue
        f = int(m.group(1))
        _write(stylize_pla(Image.open(src)), out / "die" / f"0_{f:02d}.png")
        n += 1
    for src in sorted(SPR.glob("unit_conscript_dep_d*.png")):
        m = re.match(r"unit_conscript_dep_d(\d+)\.png$", src.name)
        if not m:
            continue
        d = int(m.group(1))
        _write(stylize_pla(Image.open(src)), out / "dep" / f"{d}_00.png")
        n += 1
    print(f"PLA fixtures: {n} → {out}")
    return n


def prepare_type99() -> int:
    out = TMPL / "type99" / "fixtures"
    _clear_dir(out)
    n = 0
    for src in sorted(SPR.glob("unit_rhino_d*_f0.png")):
        m = re.match(r"unit_rhino_d(\d+)_f0\.png$", src.name)
        if not m:
            continue
        d = int(m.group(1))
        _write(stylize_type99_body(Image.open(src)), out / "stand" / f"{d}_00.png")
        n += 1
    for src in sorted(SPR.glob("turret_rhino_d*.png")):
        m = re.match(r"turret_rhino_d(\d+)\.png$", src.name)
        if not m:
            continue
        d = int(m.group(1))
        _write(stylize_type99_turret(Image.open(src)), out / "turret" / f"{d}_00.png")
        n += 1
    print(f"Type99 fixtures: {n} → {out}")
    return n


def main() -> None:
    if not (SPR / "unit_conscript_d2_f0.png").is_file():
        raise SystemExit("missing conscript sprites — run tools/ra2pack/gen_assets.py first")
    if not (SPR / "unit_rhino_d2_f0.png").is_file():
        raise SystemExit("missing rhino sprites — run tools/ra2pack/gen_assets.py first")
    prepare_pla()
    prepare_type99()
    print("prepare_fusion_units done")


if __name__ == "__main__":
    main()
