# Hard QA gates for asset_pipeline manifests / published sprites.
#   python tools/asset_pipeline/qa_check.py --manifest tools/asset_pipeline/templates/infantry_unit/manifest.yaml
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from formats import REMAP_HI, REMAP_LO, load_unittem_pal, nearest_index, rgba_to_indexed  # noqa: E402
from publish_unit import load_manifest  # noqa: E402

SPR = ROOT / "assets" / "sprites"


class QAFailure(Exception):
    pass


def file_md5(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()


def check_infantry(m: dict) -> list:
    errs = []
    uid = m["id"]
    cw, ch = m.get("canvas", [24, 30])
    facings = int(m.get("facings", 8))
    pal = load_unittem_pal()
    refs = m.get("qa", {}).get("not_identical_to", [])
    for d in range(facings):
        p = SPR / f"unit_{uid}_d{d}_f0.png"
        if not p.is_file():
            errs.append(f"missing {p.name}")
            continue
        im = Image.open(p).convert("RGBA")
        if im.size != (cw, ch):
            errs.append(f"{p.name} size {im.size} != canvas {(cw, ch)}")
        # Ground row must have some opaque pixels
        bottom = [im.getpixel((x, ch - 1))[3] for x in range(cw)]
        if max(bottom) < 40:
            # allow feet a few rows up
            ok = False
            for y in range(max(0, ch - 4), ch):
                if max(im.getpixel((x, y))[3] for x in range(cw)) >= 40:
                    ok = True
                    break
            if not ok:
                errs.append(f"{p.name} no opaque pixels near bottom (anchor)")
        indexed = rgba_to_indexed(im, pal)
        px = list(indexed.getdata())
        solid = [v for v in px if v != 0]
        if not solid:
            errs.append(f"{p.name} empty after quantize")
            continue
        remap_count = sum(1 for v in solid if REMAP_LO <= v <= REMAP_HI)
        if remap_count < 3:
            errs.append(f"{p.name} remap indices 16-31 unused ({remap_count} px) — need house-color red")
        # Palette fidelity: re-quantize should be stable
        bad = 0
        rgba = im.load()
        for y in range(ch):
            for x in range(cw):
                r, g, b, a = rgba[x, y]
                if a < 16:
                    continue
                # skip exact remap reds
                if r >= 200 and g < 90 and b < 90:
                    continue
                idx = nearest_index((r, g, b), pal, forbid={0, 1})
                pr, pg, pb = pal[idx]
                if (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2 > 80 * 80:
                    bad += 1
        if bad > (cw * ch) * 0.15:
            errs.append(f"{p.name} too many off-palette colors ({bad} px)")
        for ref in refs:
            rp = SPR / f"unit_{ref}_d{d}_f0.png"
            if rp.is_file() and file_md5(p) == file_md5(rp):
                errs.append(f"{p.name} byte-identical to {rp.name} (fake new unit)")
    icon = SPR / f"icon_unit_{uid}.png"
    if not icon.is_file():
        errs.append(f"missing {icon.name}")
    return errs


def check_vehicle(m: dict) -> list:
    errs = []
    stem = m.get("vxl_stem", m["id"])
    vxl = ROOT / "assets" / "voxels" / f"{stem}.vxl"
    if not vxl.is_file():
        errs.append(f"missing {vxl}")
        return errs
    data = vxl.read_bytes()
    if data[:15] != b"Voxel Animation":
        errs.append(f"{vxl.name} bad magic")
    if len(data) < 200:
        errs.append(f"{vxl.name} suspiciously small")
    return errs


def check_vehicle_png(m: dict) -> list:
    """Body + optional turret PNGs (no VXL)."""
    errs = []
    uid = m["id"]
    cw, ch = m.get("canvas", [96, 96])
    facings = int(m.get("facings", 8))
    refs = m.get("qa", {}).get("not_identical_to", [])
    want_turret = bool(m.get("qa", {}).get("require_turret", True))
    for d in range(facings):
        p = SPR / f"unit_{uid}_d{d}_f0.png"
        if not p.is_file():
            errs.append(f"missing {p.name}")
            continue
        im = Image.open(p).convert("RGBA")
        if im.size != (cw, ch):
            errs.append(f"{p.name} size {im.size} != canvas {(cw, ch)}")
        for ref in refs:
            rp = SPR / f"unit_{ref}_d{d}_f0.png"
            if rp.is_file() and file_md5(p) == file_md5(rp):
                errs.append(f"{p.name} byte-identical to {rp.name} (fake new unit)")
        if want_turret:
            tp = SPR / f"turret_{uid}_d{d}.png"
            if not tp.is_file():
                errs.append(f"missing {tp.name}")
            else:
                for ref in refs:
                    rp = SPR / f"turret_{ref}_d{d}.png"
                    if rp.is_file() and file_md5(tp) == file_md5(rp):
                        errs.append(f"{tp.name} byte-identical to {rp.name}")
    icon = SPR / f"icon_unit_{uid}.png"
    if not icon.is_file():
        errs.append(f"missing {icon.name}")
    return errs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    args = ap.parse_args()
    m = load_manifest(Path(args.manifest))
    kind = m.get("kind", "infantry")
    if kind == "infantry":
        errs = check_infantry(m)
    elif kind == "vehicle":
        errs = check_vehicle(m)
    elif kind == "vehicle_png":
        errs = check_vehicle_png(m)
    else:
        errs = [f"unknown kind {kind}"]
    if errs:
        print("QA FAIL:")
        for e in errs:
            print(" -", e)
        sys.exit(1)
    print("QA PASS:", m["id"])


if __name__ == "__main__":
    main()
