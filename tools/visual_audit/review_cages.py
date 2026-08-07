"""Review every cage card by pixel: yellow cage vs non-checker sprite."""
from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent
SINGLES = ROOT / "bld_review" / "cage" / "singles"
INDEX = ROOT / "bld_review" / "cage" / "index.txt"
LABEL_H = 58


def review_card(path: Path) -> tuple[str, str]:
    im = np.array(Image.open(path).convert("RGBA"))
    body = im[: im.shape[0] - LABEL_H]
    r, g, b, a = [body[:, :, i] for i in range(4)]
    yellow = (a > 200) & (r > 200) & (g > 180) & (b < 120)
    checker = (
        (np.abs(r.astype(int) - 36) < 22)
        & (np.abs(g.astype(int) - 40) < 22)
        & (np.abs(b.astype(int) - 46) < 26)
    ) | (
        (np.abs(r.astype(int) - 48) < 22)
        & (np.abs(g.astype(int) - 50) < 22)
        & (np.abs(b.astype(int) - 56) < 26)
    )
    sprite = (a > 80) & ~yellow & ~checker
    if not yellow.any() or not sprite.any():
        return "WRONG", "no cage or sprite detected"
    cy0, cy1 = int(np.where(yellow)[0].min()), int(np.where(yellow)[0].max())
    cx0, cx1 = int(np.where(yellow)[1].min()), int(np.where(yellow)[1].max())
    sy0, sy1 = int(np.where(sprite)[0].min()), int(np.where(sprite)[0].max())
    sx0, sx1 = int(np.where(sprite)[1].min()), int(np.where(sprite)[1].max())
    # positive = cage extends beyond sprite; negative = sprite sticks out of cage
    top_pad = sy0 - cy0  # >0 cage taller above; <0 peak sticks out
    bot_pad = cy1 - sy1  # >0 cage below sprite; <0 tip below cage
    left_pad = sx0 - cx0
    right_pad = cx1 - sx1
    issues = []
    if top_pad < -10:
        issues.append(f"peak_out={-top_pad}px")
    if top_pad > 35:
        issues.append(f"too_tall_top=+{top_pad}px")
    if bot_pad < -12:
        issues.append(f"base_below_cage={-bot_pad}px")
    if bot_pad > 20:
        issues.append(f"cage_below_base=+{bot_pad}px")
    if left_pad < -14:
        issues.append(f"left_out={-left_pad}px")
    if right_pad < -14:
        issues.append(f"right_out={-right_pad}px")
    # horizontal looseness (OK as borderline)
    loose = []
    if left_pad > 40:
        loose.append(f"loose_L={left_pad}")
    if right_pad > 40:
        loose.append(f"loose_R={right_pad}")
    if top_pad > 18:
        loose.append(f"headroom={top_pad}")

    if issues:
        return "WRONG", "; ".join(issues)
    if loose:
        return "BORDERLINE", "; ".join(loose)
    return "OK", f"fit top={top_pad:+d} bot={bot_pad:+d} L={left_pad:+d} R={right_pad:+d}"


def main() -> None:
    cards = sorted(SINGLES.glob("*_cage.png"))
    # Keep metric columns from export index if present
    meta: dict[str, str] = {}
    if INDEX.exists():
        for line in INDEX.read_text(encoding="utf-8").splitlines():
            if not line or line.startswith("#") or "\t" not in line:
                continue
            parts = line.split("\t")
            if len(parts) >= 2:
                meta[parts[1]] = "\t".join(parts[:-1])  # drop old flag

    lines = [
        "# Building selection-cage review — pixel pass (yellow vs sprite)\n",
        "# Formula: halfW=max(foot,art); halfD=min(foot/2,0.15*visElev)[8..28]; elev=visElev-2*halfD\n",
        "# Verdict from card pixels: OK | BORDERLINE | WRONG\n\n",
    ]
    counts = {"OK": 0, "BORDERLINE": 0, "WRONG": 0}
    wrongs = []
    for card in cards:
        # 01_conyard_cage.png -> bld_conyard.png
        stem = card.stem  # 01_conyard_cage
        parts = stem.split("_", 1)
        idx = parts[0]
        name = parts[1].removesuffix("_cage")
        fname = f"bld_{name}.png"
        verdict, note = review_card(card)
        counts[verdict] = counts.get(verdict, 0) + 1
        base = meta.get(fname)
        if base:
            lines.append(f"{base}\t{verdict}\t{note}\n")
        else:
            lines.append(f"{idx}\t{fname}\t{verdict}\t{note}\n")
        if verdict == "WRONG":
            wrongs.append(f"  {idx} {fname}: {note}")

    lines.append(
        f"\n# Summary OK={counts['OK']} BORDERLINE={counts['BORDERLINE']} WRONG={counts['WRONG']}\n"
    )
    INDEX.write_text("".join(lines), encoding="utf-8")
    print(f"OK={counts['OK']} BORDERLINE={counts['BORDERLINE']} WRONG={counts['WRONG']}")
    for w in wrongs:
        print(w)


if __name__ == "__main__":
    main()
