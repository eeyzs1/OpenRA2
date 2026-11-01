"""Extract original chrono animation canvases and indexed anim.pal colors from local MIX files."""
from pathlib import Path
import argparse
import hashlib
import json
import os
from PIL import Image
from ra2lib import MixTree, Shp, load_pal

SEQUENCES = (("Arrival", "warpin"), ("Departure", "warpout"), ("Erase", "warpaway"))

def export_chrono(tree, assets: Path):
    source_pal, raw_pal = tree.find("anim.pal")
    if not raw_pal or len(raw_pal) != 768:
        raise ValueError("anim.pal must contain exactly 256 RGB entries")
    palette = load_pal(raw_pal)
    prepared = []
    manifest = {"palette": {"mix": source_pal, "sha256": hashlib.sha256(raw_pal).hexdigest()}, "sequences": []}
    for section, stem in SEQUENCES:
        source, data = tree.find(stem + ".shp")
        if not data:
            raise ValueError("missing required " + stem + ".shp")
        shp = Shp(data)
        if not (1 <= shp.nframes <= 256 and 1 <= shp.w <= 256 and 1 <= shp.h <= 256):
            raise ValueError("unsupported chrono canvas/frame count: " + stem)
        frames = []
        for index in range(shp.nframes):
            frame = shp.frame_pixels(index)
            if frame.x + frame.w > shp.w or frame.y + frame.h > shp.h or len(frame.pixels) != frame.w * frame.h:
                raise ValueError("invalid chrono frame bounds: " + stem)
            image = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
            pixels = image.load()
            for y in range(frame.h):
                for x in range(frame.w):
                    value = frame.pixels[y * frame.w + x]
                    if value:
                        pixels[frame.x + x, frame.y + y] = (*palette[value], 255)
            # No unit remap or unit shadow-index reinterpretation for anim.pal.
            frames.append(image)
        prepared.append((section, stem, frames))
        manifest["sequences"].append({"section": section, "shp": stem + ".shp", "mix": source,
            "sha256": hashlib.sha256(data).hexdigest(), "frames": shp.nframes, "width": shp.w, "height": shp.h})
    # Validate/decode all sources before touching a previously published sequence.
    sprites = assets / "sprites"
    sprites.mkdir(parents=True, exist_ok=True)
    for section, stem, frames in prepared:
        for index, image in enumerate(frames):
            target = sprites / f"fx_{stem}_{index}.png"
            temporary = target.with_suffix(".png.tmp")
            image.save(temporary, format="PNG")
            os.replace(temporary, target)
    metadata = "; Presentation only: original SHP frames; simulation lifetimes remain rule/state values.\n"
    for section, stem, frames in prepared:
        metadata += f"[{section}]\nFrames={len(frames)}\n"
    for name, content in [("chrono_fx.ini", metadata), ("chrono_fx_sources.json", json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")]:
        target = assets / name
        temporary = target.with_suffix(target.suffix + ".tmp")
        temporary.write_text(content, encoding="utf-8")
        os.replace(temporary, target)
    return manifest

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[2] / "assets")
    options = parser.parse_args()
    result = export_chrono(MixTree(), options.output)
    for sequence in result["sequences"]:
        print(sequence["shp"], sequence["frames"], sequence["width"], sequence["height"], sequence["mix"])
