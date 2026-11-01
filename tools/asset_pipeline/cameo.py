"""Finished cameo masters are independent of simulation sprites and house masks."""
from pathlib import Path
import os
import tempfile
from PIL import Image, ImageOps

SIZE = (108, 84)


def build_cameo(manifest: dict, sprite_dir: Path) -> Image.Image:
    if "icon_source" in manifest:
        if not isinstance(manifest["icon_source"], str) or not manifest["icon_source"].strip():
            raise ValueError("icon_source must name a nonempty cameo master path")
        source = Path(manifest["icon_source"])
        if not source.is_absolute():
            source = Path(manifest["_manifest_dir"]) / source
        with Image.open(source) as image:
            image = ImageOps.exif_transpose(image).convert("RGBA")
            if image.width < SIZE[0] or image.height < SIZE[1]:
                raise ValueError(f"cameo master must be at least 108x84: {source}")
            if abs(image.width / image.height - SIZE[0] / SIZE[1]) > 0.02:
                raise ValueError(f"cameo master must have a 9:7 aspect ratio: {source}")
            if image.getchannel("A").getextrema() != (255, 255):
                raise ValueError(f"cameo master must include an opaque scene background: {source}")
            # Resampling and palette normalization only; no runtime text/house color baked in.
            image = image.convert("RGB").resize(SIZE, Image.Resampling.LANCZOS)
            return image.quantize(colors=256, method=Image.Quantize.MEDIANCUT,
                                  dither=Image.Dither.FLOYDSTEINBERG).convert("RGBA")
    # Preserve the legacy sprite fallback for manifests without a dedicated master.
    with Image.open(sprite_dir / f"unit_{manifest['id']}_d2_f0.png") as source:
        sprite = source.convert("RGBA")
    image = Image.new("RGBA", SIZE, (96, 132, 168, 255))
    scale = min(90 / sprite.width, 70 / sprite.height)
    sprite = sprite.resize((max(1, int(sprite.width * scale)), max(1, int(sprite.height * scale))), Image.Resampling.NEAREST)
    image.alpha_composite(sprite, ((108 - sprite.width) // 2, (84 - sprite.height) // 2))
    return image


def publish_cameo(manifest: dict, sprite_dir: Path) -> Path:
    image = build_cameo(manifest, sprite_dir)  # Validate before replacing the published icon.
    target = sprite_dir / f"icon_unit_{manifest['id']}.png"
    target.parent.mkdir(parents=True, exist_ok=True)
    staging = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, suffix=".png", delete=False) as file:
            staging = Path(file.name)
            image.save(file, format="PNG")
        os.replace(staging, target)
    finally:
        if staging and staging.exists():
            staging.unlink()
    return target


def check_cameo(manifest: dict, sprite_dir: Path) -> list[str]:
    path = sprite_dir / f"icon_unit_{manifest['id']}.png"
    if not path.is_file():
        return [f"missing {path.name}"]
    try:
        with Image.open(path) as source:
            image = source.convert("RGBA")
        if image.size != SIZE or image.getchannel("A").getextrema() != (255, 255):
            return [f"{path.name} must be an opaque 108x84 cameo"]
        if "icon_source" in manifest and image.tobytes() != build_cameo(manifest, sprite_dir).tobytes():
            return [f"{path.name} differs from its declared cameo master; republish the icon"]
    except (OSError, ValueError, KeyError) as error:
        return [f"{path.name}: {error}"]
    return []
