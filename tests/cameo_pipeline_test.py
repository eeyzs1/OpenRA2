"""Cameo source/normalization/publish contract without MIX, a GPU, or simulation data."""
import hashlib
import sys
import tempfile
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/asset_pipeline"))
from cameo import build_cameo, publish_cameo, check_cameo
from publish_unit import load_manifest, _parse_simple_yaml
import publish_unit as publisher

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    art = root / "art"
    art.mkdir()
    sprites = root / "sprites"
    sprites.mkdir()
    manifests = root / "manifests"
    manifests.mkdir()
    source = art / "cameo.png"
    image = Image.new("RGB", (216, 168))
    image.putdata([(x % 256, (y * 2) % 256, (x + y) % 256) for y in range(168) for x in range(216)])
    image.save(source)
    original = source.read_bytes()
    manifest_path = manifests / "unit.yaml"
    manifest_path.write_text("id: sample\nicon_source: ../art/cameo.png\n", encoding="utf-8")
    m = load_manifest(manifest_path)
    assert _parse_simple_yaml(manifest_path.read_text())['icon_source'] == '../art/cameo.png'
    published = publish_cameo(m, sprites)
    first = published.read_bytes()
    icon = Image.open(published).convert("RGBA")
    assert icon.size == (108, 84) and icon.getchannel("A").getextrema() == (255, 255)
    assert icon.getcolors(maxcolors=256) is not None
    assert source.read_bytes() == original and not check_cameo(m, sprites)
    assert publish_cameo(m, root / "new-sprites").is_file(), "icon-only publish must work without existing simulation sprites"
    publish_cameo(m, sprites)
    assert published.read_bytes() == first, "republication drifted"
    Image.new("RGBA", (108, 84), (96, 132, 168, 255)).save(published)
    assert check_cameo(m, sprites), "a fallback overwriting the declared master passed QA"
    publish_cameo(m, sprites)
    for bad in ["missing", "empty", "portrait", "transparent", "undersized"]:
        if bad not in ("missing", "empty"):
            size = (84, 108) if bad == "portrait" else (54, 42) if bad == "undersized" else (216, 168)
            Image.new("RGBA", size, (40, 80, 30, 0 if bad == "transparent" else 255)).save(art / f"{bad}.png")
        invalid = dict(m, icon_source="" if bad == "empty" else f"../art/{bad}.png")
        try:
            publish_cameo(invalid, sprites)
        except (OSError, ValueError):
            pass
        else:
            raise AssertionError(f"invalid {bad} master silently published")
        assert published.read_bytes() == first, "invalid input damaged the previous icon"
    # Old manifests retain their sprite-on-blue fallback; translucent source edges
    # must composite over the opaque scene rather than reduce its alpha.
    Image.new("RGBA",(24,30),(100,40,30,120)).save(sprites/"unit_translucent_d2_f0.png")
    assert build_cameo({'id':'translucent'},sprites).getchannel('A').getextrema()==(255,255)
    sprite = Image.new("RGBA", (24, 30), (180, 30, 40, 255))
    sprite.save(sprites / "unit_legacy_d2_f0.png")
    fallback = build_cameo({"id": "legacy"}, sprites)
    assert fallback.size == (108, 84) and fallback.getpixel((0, 0)) == (96, 132, 168, 255)
    assert fallback.getpixel((54, 42)) == (180, 30, 40, 255)
    # Exercise full unit publication, not just the icon-only helper. Both original
    # publisher branches previously overwrote independent cameos after body export.
    stand = manifests / "fixtures" / "stand"
    stand.mkdir(parents=True)
    for direction in range(8):
        sprite.save(stand / f"{direction}_00.png")
    previous_sprites = publisher.SPR
    publisher.SPR = sprites
    try:
        for kind in ("infantry", "vehicle_png"):
            complete = dict(m, kind=kind, canvas=[24, 30], facings=8, source_dir="fixtures",
                            sequences={"stand": {"frames": 1}}, out_dir=str(root / kind))
            (publisher.publish_infantry if kind == "infantry" else publisher.publish_vehicle_png)(
                complete, [(value, value, value) for value in range(256)])
            assert published.read_bytes() == first, f"full {kind} publication replaced the declared cameo"
    finally:
        publisher.SPR = previous_sprites

actual = load_manifest(ROOT / "tools/asset_pipeline/templates/type99/manifest.yaml")
master = ROOT / "art/cameos/type99-v1.png"
assert master.is_file() and actual['icon_source'], "Type99 must retain its project-owned source"
normalized = build_cameo(actual, ROOT / "assets/sprites")
assert len(normalized.getcolors(maxcolors=256)) > 100, "Type99 still has an empty/flat placeholder"
print("CAMEO PIPELINE PASS: source resolution, opaque 9:7 masters, 256-color normalization, repeat publication, failure preservation, fallback compatibility")
print("CAMEO_TYPE99 source_sha256=" + hashlib.sha256(master.read_bytes()).hexdigest())
