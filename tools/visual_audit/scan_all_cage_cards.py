#!/usr/bin/env python3
"""Scan all cage review cards for top-gap outliers."""
from __future__ import annotations

from pathlib import Path

from PIL import Image

CARDS = Path(__file__).resolve().parents[2] / "review_out" / "bld_cages" / "cards"


def is_yellow(r, g, b, a) -> bool:
    return a > 200 and r > 200 and g > 180 and b < 120


def is_bld(r, g, b, a) -> bool:
    if a < 40:
        return False
    if is_yellow(r, g, b, a):
        return False
    if abs(r - g) < 8 and abs(g - b) < 8 and 30 <= r <= 70:
        return False
    return True


def measure(path: Path):
    im = Image.open(path).convert("RGBA")
    px = im.load()
    w, h = im.size
    h_use = h - 48
    bld_top = cage_top = None
    for y in range(h_use):
        for x in range(w):
            r, g, b, a = px[x, y]
            if is_bld(r, g, b, a):
                bld_top = y if bld_top is None else min(bld_top, y)
            if is_yellow(r, g, b, a):
                cage_top = y if cage_top is None else min(cage_top, y)
    if bld_top is None or cage_top is None:
        return None
    gap = bld_top - cage_top
    if gap > 20:
        note = "STILL_TALL"
    elif gap > 12:
        note = "A_BIT_TALL"
    elif gap >= 2:
        note = "GOOD"
    elif gap >= -8:
        note = "TIGHT_OK"
    else:
        note = "CLIPPED"
    return gap, note, bld_top, cage_top


def main() -> None:
    rows = []
    for path in sorted(CARDS.glob("*_cage.png")):
        m = measure(path)
        if not m:
            print(f"FAIL {path.name}")
            continue
        gap, note, bt, ct = m
        rows.append((path.name, gap, note, bt, ct))
    bad = [r for r in rows if r[2] not in ("GOOD", "TIGHT_OK")]
    print(f"total={len(rows)} good={len(rows)-len(bad)} need_fix={len(bad)}")
    print("--- outliers ---")
    for name, gap, note, bt, ct in bad:
        print(f"{name:40} gap={gap:4}  {note}")
    print("--- all (compact) ---")
    for name, gap, note, bt, ct in rows:
        mark = " " if note in ("GOOD", "TIGHT_OK") else "*"
        print(f"{mark}{name:40} {gap:4} {note}")


if __name__ == "__main__":
    main()
