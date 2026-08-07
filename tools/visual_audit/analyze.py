"""Analyze visual-audit screenshots: mk vs finished diffs + mk asset opacity."""
from __future__ import annotations

import re
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent / "out"
SPR = ROOT / "assets" / "sprites"


def mad(u: np.ndarray, v: np.ndarray) -> float:
    return float(np.abs(u.astype(int) - v.astype(int)).mean())


def center_crop(im: Image.Image, half_w: int = 100, half_h: int = 100) -> np.ndarray:
    a = np.array(im.convert("RGB"))
    h, w, _ = a.shape
    x0, x1 = w // 2 - half_w - 50, w // 2 + half_w - 50  # bias left of sidebar
    y0, y1 = h // 2 - half_h, h // 2 + half_h
    return a[y0:y1, x0:x1]


def opac_count(path: Path) -> int:
    im = np.array(Image.open(path).convert("RGBA"))
    return int((im[:, :, 3] > 60).sum())


def mk_frames(tag: str) -> list[Path]:
    def key(p: Path) -> int:
        m = re.search(r"_f(\d+)\.png$", p.name)
        return int(m.group(1)) if m else -1

    return sorted(SPR.glob(f"bld_{tag}_mk_f*.png"), key=key)


def main() -> None:
    print("=== mk vs finished (center crop MAD; higher = more different) ===")
    for tag in ["powerplant", "teslacoil", "warfactory", "barracks", "pillbox"]:
        mid = Image.open(OUT / f"01_{tag}_mk_mid.png")
        late = Image.open(OUT / f"02_{tag}_mk_late.png")
        bare_p = OUT / f"03_{tag}_bare.png"
        fin_p = bare_p if bare_p.exists() else OUT / f"03_{tag}_cage.png"
        fin = Image.open(fin_p)
        sell = Image.open(OUT / f"04_{tag}_sell_mid.png")
        ca, cb, cc, cd = map(center_crop, (mid, fin, late, sell))
        print(
            f"{tag:12s} mid-fin={mad(ca, cb):5.1f} late-fin={mad(cc, cb):5.1f} "
            f"sell-mid={mad(cd, ca):5.1f} mid-late={mad(ca, cc):5.1f}"
        )

    print("\n=== mk asset opaque-pixel count (numeric frame order; should rise) ===")
    for tag in ["powerplant", "teslacoil", "warfactory", "barracks", "pillbox"]:
        mks = mk_frames(tag)
        fin = SPR / f"bld_{tag}.png"
        if not mks or not fin.exists():
            print(f"{tag}: missing assets")
            continue
        counts = [opac_count(p) for p in mks]
        regs = [i for i in range(1, len(counts)) if counts[i] + 50 < counts[i - 1]]
        print(
            f"{tag:12s} frames={len(mks):2d} f0={counts[0]} mid={counts[len(counts)//2]} "
            f"last={counts[-1]} fin={opac_count(fin)} regs={regs}"
        )


if __name__ == "__main__":
    main()
