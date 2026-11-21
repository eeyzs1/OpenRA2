"""Import locally installed RA2 maps without inventing terrain or mission logic.

Outputs are generated assets under assets/maps/native/<map stem>/. The original
INI and every scenario section remain available for the separate mission port.
Only terrain is enabled in the engine at this stage; no campaign is registered.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct

from PIL import Image
from map_format import parse_map, read_ini
from ra2lib import MixTree, load_pal, Shp
from tmp_format import decode_tmp_full_frame

ROOT = Path(__file__).resolve().parents[2]
THEATERS = {"TEMPERATE": ("temperat.ini", "tem", "isotem.pal"),
            "SNOW": ("snow.ini", "sno", "isosno.pal"),
            "URBAN": ("urban.ini", "urb", "isourb.pal")}


def core_file(tree, name):
    copies = [(p, b) for p, b in tree.find_all(name)
              if p.lower().startswith("ra2.mix/") or p.lower() in ("maps01.mix", "maps02.mix")
              or (name.lower().endswith(".map") and p.lower()=="multi.mix")]
    if not copies:
        raise FileNotFoundError(f"RA2 source missing: {name}")
    copies.sort(key=lambda pair: pair[0].lower())
    return copies[0]


def tile_catalog(ini, extension):
    result = []
    for section in sorted(s for s in ini if re.fullmatch(r"tileset\d+", s)):
        definition = ini[section]
        for i in range(int(definition.get("tilesinset", "0"))):
            result.append(f"{definition['filename']}{i + 1:02}.{extension}".lower())
    return result


def import_map(tree, filename, output_root=ROOT / "assets/maps/native"):
    if not re.fullmatch(r"[a-zA-Z0-9_-]+\.map", filename):
        raise ValueError("map name must be a plain .map filename")
    source, data = core_file(tree, filename)
    native = parse_map(data)
    if native.theater not in THEATERS:
        raise ValueError(f"unsupported RA2 theater: {native.theater}")
    ini_name, extension, palette_name = THEATERS[native.theater]
    ini_source, ini_data = core_file(tree, ini_name)
    catalog = tile_catalog(read_ini(ini_data), extension)
    pal_source, pal_data = core_file(tree, palette_name)
    palette = load_pal(pal_data)
    rules_source, rules_data = core_file(tree, "rules.ini")
    rules = read_ini(rules_data)
    overlays = [v.upper() for v in rules["overlaytypes"].values()]
    side = native.width + native.height
    image = Image.new("RGBA", (side * 60, side * 30))
    owners = Image.new("RGBA", image.size)
    frame_cache, source_files = {}, {}
    cells, count_land, count_ramp = [], Counter(), Counter()

    def frame_for(tile):
        # 0xffff is the original implicit clear tile, not a missing asset.
        tile_id = 0 if tile.tile == 65535 else tile.tile
        key = tile_id, tile.subtile
        if key not in frame_cache:
            if tile_id >= len(catalog):
                raise ValueError(f"unknown {native.theater} tile {tile_id}")
            name = catalog[tile_id]
            path, tmp = core_file(tree, name)
            source_files[name] = {"source": path, "sha256": hashlib.sha256(tmp).hexdigest()}
            frame_cache[key] = decode_tmp_full_frame(tmp, tile.subtile, palette)
        return frame_cache[key]

    for tile in sorted(native.tiles, key=lambda t: (t.x + t.y, t.x)):
        frame = frame_for(tile)
        x = (tile.x - tile.y) * 30 - 30 + (side - 1) * 30 + frame.offset[0]
        y = (tile.x + tile.y - tile.height) * 15 + frame.offset[1]
        image.alpha_composite(frame.image, (x, y))
        owner=tile.y*side+tile.x+1
        owner_image=Image.new("RGBA",frame.image.size,(owner&255,(owner>>8)&255,(owner>>16)&255,255))
        owners.paste(owner_image,(x,y),frame.image.getchannel("A"))
        overlay_index = tile.x + 512 * tile.y
        overlay, overlay_data = native.overlays[overlay_index], native.overlay_data[overlay_index]
        # TMP terrain codes differ from the runtime LandType enum! EA's
        # Defines.h: water=9, ground=13, rough=14, paved=11, dirtroad=12,
        # impassable=15, building=7. Preserve the raw code alongside our mask.
        terrain = 2 if frame.terrain_type == 9 else (1 if frame.terrain_type in (10, 14) else 0)
        blocked = frame.terrain_type in (7, 15)
        # Preserve walls in the simulation without masquerading as rock art.
        if overlay != 255:
            if overlay >= len(overlays):
                raise ValueError(f"unknown original overlay {overlay}")
            definition = rules.get(overlays[overlay].lower(), {})
            blocked |= definition.get("wall", "no").lower() in ("yes", "true")
            if definition.get("tiberium", "no").lower() in ("yes", "true"):
                terrain = 4 if definition.get("name", "").lower().startswith("gem") else 3
        cells.append((tile.x, tile.y, tile.tile, tile.auxiliary, tile.subtile, tile.height,
                      tile.auxiliary2, overlay, overlay_data, frame.terrain_type, frame.ramp, terrain, int(blocked)))
        count_land[frame.terrain_type] += 1
        count_ramp[frame.ramp] += 1

    # Scale once, by the same exact 16/15 ratio as engine coordinates/heights.
    image = image.resize((side * 64, side * 32), Image.Resampling.NEAREST)
    owners = owners.resize(image.size, Image.Resampling.NEAREST)
    stem = Path(filename).stem.lower()
    destination = Path(output_root) / stem
    destination.mkdir(parents=True, exist_ok=True)
    image.save(destination / "terrain.png")
    owners.save(destination / "terrain-owners.png")
    source_hash = hashlib.sha256(data).digest()
    header = struct.pack("<8s7H", b"RA2NMAP1", side, native.width, native.height, *native.local)
    header += source_hash
    for value in (stem, native.theater):
        raw = value.encode("ascii"); header += struct.pack("<H", len(raw)) + raw
    header += struct.pack("<I", len(cells))
    payload = header + b"".join(struct.pack("<HHHH9B", *cell) for cell in cells)
    (destination / "terrain.nmap").write_bytes(payload)
    (destination / "scenario.map").write_bytes(data)
    # All scenario sections and exact strings survive import, including mission-
    # local rules, houses, trigger linkage and the full scripted task forces.
    scenario = {s: v for s, v in native.sections.items()
                if s not in ("isomappack5", "overlaypack", "overlaydatapack", "previewpack")}
    (destination / "scenario.json").write_text(json.dumps(scenario, ensure_ascii=False, indent=2), encoding="utf-8")
    report = {
        "format": 1, "map": filename.lower(), "source": source, "source_sha256": source_hash.hex(),
        "status": "terrain-import; original scenario preserved; mission runtime not enabled",
        "theater": native.theater, "size": [native.width, native.height], "local_size": native.local,
        "canvas_side": side, "tiles": len(cells), "tmp_terrain_types": dict(count_land), "ramps": dict(count_ramp),
        "tile_frames": len(frame_cache), "source_tiles": source_files,
        "tile_ini": {"source": ini_source, "sha256": hashlib.sha256(ini_data).hexdigest()},
        "palette": {"source": pal_source, "sha256": hashlib.sha256(pal_data).hexdigest()},
        "rules": {"source": rules_source, "sha256": hashlib.sha256(rules_data).hexdigest()},
        "terrain_sha256": hashlib.sha256((destination / "terrain.png").read_bytes()).hexdigest(),
        "owners_sha256": hashlib.sha256((destination / "terrain-owners.png").read_bytes()).hexdigest(),
        "nmap_sha256": hashlib.sha256(payload).hexdigest(),
        "section_counts": {s: len(v) for s, v in scenario.items()},
    }
    (destination / "import-report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"NATIVE MAP {stem}: {native.width}x{native.height}, {len(cells)} tiles, {len(frame_cache)} exact TMP frames")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("maps", nargs="*", default=["all01t.map"])
    parser.add_argument("--output", type=Path, default=ROOT / "assets/maps/native")
    args = parser.parse_args()
    tree = MixTree()
    for filename in args.maps:
        import_map(tree, filename, args.output)
