# Publish a custom unit from manifest + rendered/fixture frames into OpenRA2 assets/.
# Example:
#   python tools/asset_pipeline/publish_unit.py --manifest tools/asset_pipeline/templates/infantry_unit/manifest.yaml
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from formats import (  # noqa: E402
    load_unittem_pal,
    pack_shp_ts,
    rgba_to_indexed,
    save_indexed_png,
    vox_to_section,
    write_hva_static,
    write_vxl,
)

SPR = ROOT / "assets" / "sprites"
VOXELS = ROOT / "assets" / "voxels"


def load_manifest(path: Path) -> dict:
    text = Path(path).read_text(encoding="utf-8")
    try:
        import yaml
        data = yaml.safe_load(text)
    except ImportError:
        data = _parse_simple_yaml(text)
    if not isinstance(data, dict) or "id" not in data:
        raise SystemExit("manifest must be a mapping with id=")
    return data


def _parse_simple_yaml(text: str) -> dict:
    """Minimal YAML subset for pipeline manifests (no nested lists of maps)."""
    root: dict = {}
    stack = [root]
    indents = [-1]
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        line = raw.strip()
        while len(indents) > 1 and indent <= indents[-1]:
            stack.pop()
            indents.pop()
        cur = stack[-1]
        if ":" in line and not line.startswith("-"):
            k, _, v = line.partition(":")
            k, v = k.strip(), v.strip()
            if v == "":
                child = {}
                cur[k] = child
                stack.append(child)
                indents.append(indent)
            elif v.startswith("[") and v.endswith("]"):
                inner = v[1:-1].strip()
                if not inner:
                    cur[k] = []
                else:
                    parts = [p.strip() for p in inner.split(",")]
                    cur[k] = [int(p) if p.isdigit() else p.strip("\"'") for p in parts]
            elif v.isdigit():
                cur[k] = int(v)
            elif v in ("true", "false"):
                cur[k] = v == "true"
            else:
                cur[k] = v.strip("\"'")
        elif line.startswith("- "):
            # not needed for our manifests
            pass
    return root


def find_frames(srcdir: Path, anim: str, facings: int, frames: int) -> list:
    """Load facing_frame PNGs; raise if any missing."""
    out = []
    base = srcdir / anim
    for d in range(facings):
        for f in range(frames):
            # accept facing_00.png or facing_0.png
            cands = [
                base / f"{d}_{f:02d}.png",
                base / f"{d}_{f}.png",
            ]
            path = next((p for p in cands if p.is_file()), None)
            if not path:
                raise FileNotFoundError(f"missing frame {anim} facing={d} frame={f} under {base}")
            out.append(Image.open(path).convert("RGBA"))
    return out


def paste_on_canvas(im: Image.Image, cw: int, ch: int) -> Image.Image:
    """Center-bottom paste (infantry feet on bottom)."""
    canvas = Image.new("RGBA", (cw, ch), (0, 0, 0, 0))
    im = im.convert("RGBA")
    # Scale down if larger
    if im.width > cw or im.height > ch:
        s = min(cw / im.width, ch / im.height)
        im = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.NEAREST)
    x = (cw - im.width) // 2
    y = ch - im.height
    if y < 0:
        y = 0
    canvas.paste(im, (x, y), im)
    return canvas


def publish_infantry(m: dict, pal) -> None:
    uid = m["id"]
    cw, ch = m.get("canvas", [24, 30])
    facings = int(m.get("facings", 8))
    src = Path(m["source_dir"])
    if not src.is_absolute():
        src = (Path(m.get("_manifest_dir", ROOT)) / src).resolve()

    sequences = m.get("sequences", {})
    # Default: stand 1 frame / facing
    if not sequences:
        sequences = {"stand": {"frames": 1}}

    indexed_all = []  # for optional SHP pack in facing-major stand order
    anim_meta = {"walk": 0, "walkrate": 4, "fire": 0, "firerate": 4, "die": 0, "dep": 0}

    for anim, cfg in sequences.items():
        nframes = int(cfg.get("frames", 1))
        # die / prone-style: often facing-independent (fixtures under facing 0 only)
        anim_facings = int(cfg.get("facings", 1 if anim.startswith("die") else facings))
        imgs = find_frames(src, anim, anim_facings, nframes)
        idx = 0
        for d in range(anim_facings):
            for f in range(nframes):
                rgba = paste_on_canvas(imgs[idx], cw, ch)
                indexed = rgba_to_indexed(rgba, pal)
                indexed_all.append(indexed)
                # Engine naming
                if anim in ("stand", "ready"):
                    out = SPR / f"unit_{uid}_d{d}_f{f}.png"
                    save_indexed_png(indexed, out)
                elif anim == "walk":
                    out = SPR / f"unit_{uid}_walk_d{d}_f{f}.png"
                    save_indexed_png(indexed, out)
                    anim_meta["walk"] = max(anim_meta["walk"], nframes)
                elif anim in ("fire", "fireup"):
                    out = SPR / f"unit_{uid}_fire_d{d}_f{f}.png"
                    save_indexed_png(indexed, out)
                    anim_meta["fire"] = max(anim_meta["fire"], nframes)
                elif anim.startswith("die"):
                    out = SPR / f"unit_{uid}_die_f{f}.png"
                    if d == 0:  # die often non-facing in engine
                        save_indexed_png(indexed, out)
                    anim_meta["die"] = max(anim_meta["die"], nframes)
                elif anim in ("dep", "deploy"):
                    out = SPR / f"unit_{uid}_dep_d{d}.png"
                    if f == 0:
                        save_indexed_png(indexed, out)
                    anim_meta["dep"] = 1
                else:
                    # generic dump
                    out = SPR / f"unit_{uid}_{anim}_d{d}_f{f}.png"
                    save_indexed_png(indexed, out)
                idx += 1

    # Ensure f0 stand exists for all dirs
    for d in range(facings):
        p = SPR / f"unit_{uid}_d{d}_f0.png"
        if not p.is_file():
            raise FileNotFoundError(f"publish incomplete: {p}")

    # Icon from d2 stand
    icon_src = SPR / f"unit_{uid}_d2_f0.png"
    icon = Image.open(icon_src).convert("RGBA")
    ic = Image.new("RGBA", (108, 84), (96, 132, 168, 255))
    s = min(90 / icon.width, 70 / icon.height)
    icon = icon.resize((max(1, int(icon.width * s)), max(1, int(icon.height * s))), Image.NEAREST)
    ic.paste(icon, ((108 - icon.width) // 2, (84 - icon.height) // 2), icon)
    ic.save(SPR / f"icon_unit_{uid}.png")

    # Pack SHP master (stand frames only if present)
    out_dir = Path(m.get("out_dir", ROOT / "tools" / "asset_pipeline" / "out" / uid))
    if not out_dir.is_absolute():
        out_dir = (ROOT / out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    stand_indexed = []
    for d in range(facings):
        for f in range(int(sequences.get("stand", sequences.get("ready", {"frames": 1})).get("frames", 1))):
            p = SPR / f"unit_{uid}_d{d}_f{f}.png"
            rgba = Image.open(p).convert("RGBA")
            stand_indexed.append(rgba_to_indexed(rgba, pal))
    shp = pack_shp_ts(stand_indexed, cw, ch)
    (out_dir / f"{uid}.shp").write_bytes(shp)

    # anims.ini snippet
    snippet = out_dir / "anims_snippet.ini"
    lines = [f"[{uid}]", f"walk={anim_meta['walk']}", f"walkrate={anim_meta['walkrate']}",
             f"fire={anim_meta['fire']}", f"firerate={anim_meta['firerate']}",
             f"die={anim_meta['die']}"]
    if anim_meta["dep"]:
        lines.append("dep=1")
    snippet.write_text("\n".join(lines) + "\n", encoding="utf-8")

    # Merge into assets/sprites/anims.ini if section missing/update
    anims = SPR / "anims.ini"
    if anims.is_file():
        txt = anims.read_text(encoding="utf-8")
        block = "\n".join(lines) + "\n"
        if f"[{uid}]" in txt:
            txt = re.sub(rf"\[{re.escape(uid)}\][^\[]*", block, txt, count=1)
        else:
            txt = txt.rstrip() + "\n" + block
        anims.write_text(txt, encoding="utf-8")
    print(f"published infantry {uid} → {SPR} ; master SHP {out_dir / (uid + '.shp')}")


def publish_vehicle(m: dict, pal) -> None:
    uid = m["id"]
    stem = m.get("vxl_stem", uid)
    src = Path(m["source_dir"])
    if not src.is_absolute():
        src = (Path(m.get("_manifest_dir", ROOT)) / src).resolve()
    vox = src / m.get("vox", f"{stem}.vox")
    if not vox.is_file():
        raise FileNotFoundError(f"missing VOX {vox}")
    sections = [vox_to_section(vox, name="body", pal_unittem=pal)]
    tur = src / m.get("vox_turret", f"{stem}tur.vox")
    if tur.is_file():
        sections.append(vox_to_section(tur, name="tur", pal_unittem=pal))
    barl = src / m.get("vox_barrel", f"{stem}barl.vox")
    if barl.is_file():
        sections.append(vox_to_section(barl, name="barl", pal_unittem=pal))
    VOXELS.mkdir(parents=True, exist_ok=True)
    (VOXELS / f"{stem}.vxl").write_bytes(write_vxl(sections, pal))
    (VOXELS / f"{stem}.hva").write_bytes(write_hva_static(len(sections), 1))
    out_dir = Path(m.get("out_dir", ROOT / "tools" / "asset_pipeline" / "out" / uid))
    if not out_dir.is_absolute():
        out_dir = (ROOT / out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / f"{stem}.vxl").write_bytes((VOXELS / f"{stem}.vxl").read_bytes())
    print(f"published vehicle {uid} VXL → {VOXELS / (stem + '.vxl')}")


def publish_vehicle_png(m: dict, pal) -> None:
    """8-facing body (+ optional turret) PNGs — for tanks without MagicaVoxel yet (e.g. Type99)."""
    uid = m["id"]
    cw, ch = m.get("canvas", [96, 96])
    facings = int(m.get("facings", 8))
    src = Path(m["source_dir"])
    if not src.is_absolute():
        src = (Path(m.get("_manifest_dir", ROOT)) / src).resolve()
    sequences = m.get("sequences", {"stand": {"frames": 1}})
    for anim, cfg in sequences.items():
        nframes = int(cfg.get("frames", 1))
        imgs = find_frames(src, anim, facings, nframes)
        idx = 0
        for d in range(facings):
            for f in range(nframes):
                rgba = paste_on_canvas(imgs[idx], cw, ch)
                indexed = rgba_to_indexed(rgba, pal)
                if anim in ("stand", "ready", "body"):
                    save_indexed_png(indexed, SPR / f"unit_{uid}_d{d}_f{f}.png")
                else:
                    save_indexed_png(indexed, SPR / f"unit_{uid}_{anim}_d{d}_f{f}.png")
                idx += 1
    # Optional turret fixtures
    tur_dir = src / "turret"
    if tur_dir.is_dir():
        for d in range(facings):
            cands = [tur_dir / f"{d}_00.png", tur_dir / f"{d}_0.png", tur_dir / f"{d}.png"]
            path = next((p for p in cands if p.is_file()), None)
            if not path:
                raise FileNotFoundError(f"missing turret facing {d} under {tur_dir}")
            rgba = paste_on_canvas(Image.open(path).convert("RGBA"), cw, ch)
            save_indexed_png(rgba_to_indexed(rgba, pal), SPR / f"turret_{uid}_d{d}.png")
    for d in range(facings):
        if not (SPR / f"unit_{uid}_d{d}_f0.png").is_file():
            raise FileNotFoundError(f"publish incomplete: unit_{uid}_d{d}_f0.png")
    icon_src = SPR / f"unit_{uid}_d2_f0.png"
    icon = Image.open(icon_src).convert("RGBA")
    ic = Image.new("RGBA", (108, 84), (96, 132, 168, 255))
    s = min(90 / icon.width, 70 / icon.height)
    icon = icon.resize((max(1, int(icon.width * s)), max(1, int(icon.height * s))), Image.NEAREST)
    ic.paste(icon, ((108 - icon.width) // 2, (84 - icon.height) // 2), icon)
    ic.save(SPR / f"icon_unit_{uid}.png")
    out_dir = Path(m.get("out_dir", ROOT / "tools" / "asset_pipeline" / "out" / uid))
    if not out_dir.is_absolute():
        out_dir = (ROOT / out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    stand_indexed = []
    for d in range(facings):
        rgba = Image.open(SPR / f"unit_{uid}_d{d}_f0.png").convert("RGBA")
        stand_indexed.append(rgba_to_indexed(rgba, pal))
    (out_dir / f"{uid}.shp").write_bytes(pack_shp_ts(stand_indexed, cw, ch))
    print(f"published vehicle_png {uid} → {SPR} ; master SHP {out_dir / (uid + '.shp')}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    args = ap.parse_args()
    man_path = Path(args.manifest).resolve()
    m = load_manifest(man_path)
    m["_manifest_dir"] = str(man_path.parent)
    pal = load_unittem_pal()
    kind = m.get("kind", "infantry")
    if kind == "infantry":
        publish_infantry(m, pal)
    elif kind == "vehicle":
        publish_vehicle(m, pal)
    elif kind == "vehicle_png":
        publish_vehicle_png(m, pal)
    else:
        raise SystemExit(f"unknown kind={kind}")


if __name__ == "__main__":
    main()
