#!/usr/bin/env python3
"""Measure top gap on exported cage review cards (yellow vs opaque building)."""
from __future__ import annotations

from pathlib import Path

from PIL import Image

CARDS = Path(__file__).resolve().parents[2] / "review_out" / "bld_cages" / "cards"
TARGETS = [
    "02_powerplant_cage.png",
    "03_teslareactor_cage.png",
    "05_nuclearreactor_cage.png",
    "07_barracks_cage.png",
    "08_warfactory_cage.png",
    "30_nukesilo_cage.png",
    "31_weatherdevice_cage.png",
    "32_ironcurtain_cage.png",
    "33_chronosphere_cage.png",
    "34_geneticmutator_cage.png",
    "35_psychicdominator_cage.png",
    "39_oilderrick_cage.png",
    "40_hospital_cage.png",
    "41_machineshop_cage.png",
    "42_techairport_cage.png",
    "43_secretlab_cage.png",
    "44_techoutpost_cage.png",
    "45_civhouse_cage.png",
    "46_grinder_cage.png",
    "50_barracks_yuri_cage.png",
    "51_battlelab_sov_cage.png",
    "52_civbarn_cage.png",
    "53_civchurch_cage.png",
    "54_civhouse2_cage.png",
    "55_civhouse3_cage.png",
    "56_civhouse4_cage.png",
    "57_civhouse5_cage.png",
    "58_civhouse6_cage.png",
    "59_civhouse7_cage.png",
    "60_civwash_cage.png",
]


def is_yellow(r, g, b, a) -> bool:
    return a > 200 and r > 200 and g > 180 and b < 120


def is_bld(r, g, b, a) -> bool:
    # opaque non-checker, non-yellow, non-label
    if a < 40:
        return False
    if is_yellow(r, g, b, a):
        return False
    # checkerboard greys
    if abs(r - g) < 8 and abs(g - b) < 8 and 30 <= r <= 70:
        return False
    return True


def main() -> None:
    print(f"{'card':32} bldTop cageTop gap  note")
    for name in TARGETS:
        path = CARDS / name
        im = Image.open(path).convert("RGBA")
        px = im.load()
        w, h = im.size
        # ignore bottom caption band (~40px)
        h_use = h - 48
        bld_top = None
        cage_top = None
        for y in range(h_use):
            for x in range(w):
                r, g, b, a = px[x, y]
                if bld_top is None and is_bld(r, g, b, a):
                    bld_top = y
                if cage_top is None and is_yellow(r, g, b, a):
                    cage_top = y
            if bld_top is not None and cage_top is not None:
                # keep scanning a bit? first hits are enough for tops
                pass
            if y > 0 and bld_top is not None and cage_top is not None and y > max(bld_top, cage_top) + 5:
                break
        # full scan for true tops
        bld_top = cage_top = None
        for y in range(h_use):
            for x in range(w):
                r, g, b, a = px[x, y]
                if is_bld(r, g, b, a):
                    bld_top = y if bld_top is None else min(bld_top, y)
                if is_yellow(r, g, b, a):
                    cage_top = y if cage_top is None else min(cage_top, y)
        if bld_top is None or cage_top is None:
            print(f"{name:32} FAIL")
            continue
        gap = bld_top - cage_top  # + = cage above building
        if gap > 20:
            note = "STILL_TALL"
        elif gap > 12:
            note = "A_BIT_TALL"
        elif gap >= 2:
            note = "GOOD"
        elif gap >= -6:
            note = "TIGHT"
        else:
            note = "CLIPPED"
        print(f"{name:32} {bld_top:6} {cage_top:7} {gap:4}  {note}")


if __name__ == "__main__":
    main()
