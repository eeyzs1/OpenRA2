"""Export original terrain objects and non-resource overlays for a native scene.

Coordinates and frames come from the map. Theater/palette selection and overlay
anchors follow EA's FinalAlert2 Loading.cpp / IsoView.cpp; no lookalike assets.
The source records remain distinct from placed army/building records.
"""
import hashlib
import re

from PIL import Image
from gen_native_maps import core_file
from ra2lib import Shp, load_pal, shp_frame_to_rgba


def yes(value):
    return str(value).lower() in ('yes', 'true', '1')


def export_scenery(tree, native, rules, art, target):
    combat={**rules.get('combatdamage',{}),**native.sections.get('combatdamage',{})}
    theater = {'URBAN': ('urb', 'U'), 'TEMPERATE': ('tem', 'T'), 'SNOW': ('sno', 'A')}[native.theater]
    extension, letter = theater
    overlay_names = [v.upper() for v in rules['overlaytypes'].values()]
    placements = []
    for key, name in native.sections.get('terrain', {}).items():
        cell = int(key)
        placements.append((1, name.upper(), cell % 1000, cell // 1000, 0, 'terrain:' + key, name))
    for tile in sorted(native.tiles, key=lambda p: (p.y, p.x)):
        index = tile.x + 512 * tile.y
        overlay, frame = native.overlays[index], native.overlay_data[index]
        if overlay == 255:
            continue
        name = overlay_names[overlay]
        rule = {**rules.get(name.lower(), {}), **native.sections.get(name.lower(), {})}
        if yes(rule.get('tiberium')):
            continue  # Harvestable resources keep their quantity in Map cells.
        kind = 3 if name in ('BRIDGE1', 'BRIDGE2', 'BRIDGEB1', 'BRIDGEB2') else 2
        if kind == 2 and not (yes(rule.get('wall')) or yes(rule.get('isarock'))):
            raise ValueError(('unsupported native overlay', name))
        placements.append((kind, name, tile.x, tile.y, frame, f'overlay:{index}', f'{overlay},{frame}'))

    definitions, records, report = [], [], {}
    for kind, name in sorted({(p[0], p[1]) for p in placements}):
        rule = {**rules.get(name.lower(), {}), **native.sections.get(name.lower(), {})}
        image = rule.get('image', name).lower()
        visual = art.get(image, {})
        image = visual.get('image', image).lower()
        candidates = [image + '.' + extension]
        if yes(visual.get('newtheater')) and len(image) > 1:
            candidates.append(image[0] + letter.lower() + image[2:] + '.shp')
        candidates.append(image + '.shp')
        selected = None
        for candidate in dict.fromkeys(candidates):
            try:
                source, raw = core_file(tree, candidate)
                selected = candidate, source, raw
                break
            except FileNotFoundError:
                pass
        if selected is None:
            raise FileNotFoundError(('native scenery art', name, candidates))
        filename, source, raw = selected
        shp = Shp(raw)
        if shp.nframes < 2 or shp.nframes % 2:
            raise ValueError(('unpaired native scenery frames', name, shp.nframes))
        body_frames = shp.nframes // 2
        # FA2's theater-extension path selects iso for both terrain objects and
        # overlays. The .shp/NewTheater path uses the unit palette.
        prefix = 'iso' if filename.endswith('.' + extension) or yes(visual.get('terrainpalette')) else 'unit'
        # EA's FAData.ini ForceUnitPalettePrefix explicitly includes TIBTRE.
        if name.startswith('TIBTRE'):prefix='unit'
        palname = prefix + extension + '.pal'
        pal_source, pal_raw = core_file(tree, palname)
        palette = load_pal(pal_raw)
        for frame in range(body_frames):
            body = Image.new('RGBA', (shp.w, shp.h))
            for source_frame in (body_frames + frame, frame):
                pixels = shp.frame_pixels(source_frame)
                if pixels.w and pixels.h:
                    body.alpha_composite(shp_frame_to_rgba(pixels, palette, remap=False), (pixels.x, pixels.y))
            body.resize((round(shp.w * 16 / 15), round(shp.h * 16 / 15)), Image.Resampling.NEAREST).save(target / 'actors' / f'{name.lower()}_{frame}.png')
        foundation = visual.get('foundation', '1x1')
        match = re.fullmatch(r'(\d+)x(\d+)', foundation)
        if not match:
            raise ValueError(('native scenery foundation', name, foundation))
        width, height = map(int, match.groups())
        hp = int(rule.get('strength', rules['general']['treestrength'] if kind == 1 else combat['bridgestrength'] if kind == 3 else '1'))
        armor = rule.get('armor', 'none').lower()
        colors = [int(v) for v in rule.get('radarcolor', '92,92,92').split(',')]
        if len(colors) != 3 or any(v < 0 or v > 255 for v in colors):
            raise ValueError(('native scenery radar color', name))
        # Native building drawing anchors at the cell center (+15 source px).
        # FA2 terrain anchors at +12; overlays at 0, high-bridge frames 9..17
        # another -15. Preserve these as origins rather than cropping artwork.
        origin_y = round(shp.h * 8 / 15) + (round(3 * 16 / 15) if kind == 1 else 16)
        fields = {'Id': name, 'Name': rule.get('name', name), 'Building': '1', 'Base': 'CivHouse',
                  'Kind': str(kind), 'Hp': str(hp), 'Sight': '0', 'Cost': '0', 'Selectable': '0',
                  'Significant': '0', 'Power': '0', 'Garrison': '0', 'Capturable': '0',
                  'Width': str(width), 'Height': str(height), 'Blocks': str(int(kind != 3)),
                  'Immune': str(int(yes(rule.get('immune')) or (kind == 2 and not yes(rule.get('legaltarget', 'true'))))),
                  'Wall': str(int(yes(rule.get('wall')))), 'DamageLevels': visual.get('damagelevels', '1'),
                  'Art': name.lower(), 'ArtFrames': str(body_frames), 'Remappable': '0',
                  'OriginX': str(round(shp.w * 8 / 15) + int(kind == 3)), 'OriginY': str(origin_y),
                  'Overlay': str(overlay_names.index(name) if kind != 1 else -1),
                  'RadarRGB': str(colors[0] << 16 | colors[1] << 8 | colors[2])}
        definitions.append((fields, armor))
        report[name] = {'kind': kind, 'shp': filename, 'source': source, 'sha256': hashlib.sha256(raw).hexdigest(),
                        'palette': palname, 'palette_source': pal_source, 'palette_sha256': hashlib.sha256(pal_raw).hexdigest(),
                        'raw_rules': rule, 'raw_art': visual, 'frames': shp.nframes}
    heights={(tile.x,tile.y):tile.height for tile in native.tiles}
    for kind, name, x, y, frame, key, original in placements:
        if frame >= int(next(row['ArtFrames'] for row, _ in definitions if row['Id'] == name)):
            raise ValueError(('native overlay frame outside original SHP', name, frame))
        records.append({'Key': key, 'Id': name, 'Kind': str(kind), 'House': '-1', 'X': str(x), 'Y': str(y),
                        'Health': '256', 'Facing': '0', 'Tag': '', 'Mission': 'Sleep', 'Original': original,
                        'OriginalFrame': str(frame)})
        if kind==3:records[-1]['OriginalHeight']=str(heights[x,y])
    return definitions, records, report
