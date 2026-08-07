"""Compare isolated bare vs selection-cage screenshots.

Expects --visual-audit output:
  tools/visual_audit/out/03_<tag>_bare.png
  tools/visual_audit/out/03_<tag>_cage.png

Writes pair_<tag>.png = bare | cage | magenta-diff (center crop).
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image

OUT = Path(__file__).resolve().parent / "out"
# Screen-space crop centers from visualAudit (camZoom=1.6, bldScreenPos logs)
CROPS = {
    "powerplant": (628, 456, 160),
    "teslacoil": (628, 456, 140),
    "warfactory": (628, 507, 220),
    "barracks": (628, 456, 180),
    "pillbox": (628, 456, 100),
}


def main() -> None:
    print("=== bare | cage | diff (magenta = cage overlay pixels) ===")
    for tag, (cx, cy, half) in CROPS.items():
        bare_p = OUT / f"03_{tag}_bare.png"
        cage_p = OUT / f"03_{tag}_cage.png"
        if not bare_p.exists() or not cage_p.exists():
            print(f"{tag}: missing pair")
            continue
        bare = np.asarray(Image.open(bare_p).convert("RGB"))
        cage = np.asarray(Image.open(cage_p).convert("RGB"))
        h, w, _ = bare.shape
        x0, x1 = max(0, cx - half), min(w, cx + half)
        y0, y1 = max(0, cy - half - 40), min(h, cy + half // 2)
        bw, bh = x1 - x0, y1 - y0
        b = bare[y0:y1, x0:x1]
        c = cage[y0:y1, x0:x1]
        diff = np.abs(c.astype(int) - b.astype(int)).sum(axis=2)
        ov = b.copy()
        ov[diff > 40] = (255, 0, 255)
        trip = Image.new("RGB", (bw * 3 + 8, bh), (20, 20, 24))
        trip.paste(Image.fromarray(b), (0, 0))
        trip.paste(Image.fromarray(c), (bw + 4, 0))
        trip.paste(Image.fromarray(ov), (bw * 2 + 8, 0))
        out = OUT / f"pair_{tag}.png"
        trip.save(out)
        changed = int((diff > 40).sum())
        print(f"{tag:12s} crop={bw}x{bh} overlay_px={changed} -> {out.name}")


if __name__ == "__main__":
    main()
