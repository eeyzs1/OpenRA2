"""Audit all native scenery placements and original palette pixels independently."""
import configparser
import hashlib
import json
from pathlib import Path
import sys
from collections import Counter

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ra2pack'))
from gen_native_maps import ROOT, core_file
from map_format import parse_map, read_ini
from ra2lib import MixTree, Shp, load_pal
from PIL import Image

folder = ROOT / 'assets/maps/native/all01t'
if not (folder / 'scene.ini').exists():
    print('NATIVE SCENERY ASSETS SKIP: export the original native scene first')
    sys.exit(77)
ini = configparser.ConfigParser(interpolation=None); ini.optionxform = str
ini.read(folder / 'scene.ini', encoding='utf-8')
assert ini['Scene']['Version'] == '16'
tree = MixTree(); native = parse_map(core_file(tree, 'all01t.map')[1])
rules = read_ini(core_file(tree, 'rules.ini')[1]); names = list(rules['overlaytypes'].values())
report = json.loads((folder / 'scene-report.json').read_text(encoding='utf-8'))['scenery']
source_heights={(tile.x,tile.y):tile.height for tile in native.tiles}
rows = [ini[s] for s in ini.sections() if s.startswith('Actor.') and int(ini[f'Type.{ini[s]["Type"]}'].get('Kind', '0'))]
assert len(rows) == 489
assert Counter(int(ini[f'Type.{row["Type"]}']['Kind']) for row in rows) == {1: 344, 2: 136, 3: 9}
terrain = {row['Key'].split(':')[1]: row for row in rows if row['Key'].startswith('terrain:')}
assert set(terrain) == set(native.sections['terrain'])
for key, name in native.sections['terrain'].items():
    row = terrain[key]; definition = ini[f'Type.{row["Type"]}']
    assert (definition['Id'], int(row['X']), int(row['Y']), row['Original']) == (name, int(key) % 1000, int(key) // 1000, name)
    assert int(definition['Hp']) == 200 and row['House'] == '-1'
for row in rows:
    definition = ini[f'Type.{row["Type"]}']
    assert definition['Selectable'] == '0' and definition['Garrison'] == '0' and definition['Remappable'] == '0'
    if row['Key'].startswith('overlay:'):
        index = int(row['Key'].split(':')[1]); overlay, frame = native.overlays[index], native.overlay_data[index]
        assert (row['Original'], row['OriginalFrame'], definition['Id']) == (f'{overlay},{frame}', str(frame), names[overlay])
        if int(definition['Kind'])==3:
            assert int(row['OriginalHeight'])==source_heights[int(row['X']),int(row['Y'])]
        assert (int(row['X']), int(row['Y'])) == (index % 512, index // 512)
        assert int(definition['Overlay']) == overlay
        if definition['Wall'] == '1': assert int(definition['Hp']) == 100 and int(definition['DamageLevels']) == 2
        if definition['Kind'] == '3':
            combat={**rules.get('combatdamage',{}),**native.sections.get('combatdamage',{})}
            assert frame==9 and int(definition['Hp'])==int(combat['bridgestrength'])
            enabled=native.sections.get('specialflags',{}).get('destroyablebridges',combat.get('destroyablebridges','yes'))
            assert int(ini['Scene']['DestroyableBridges'])==int(enabled.lower() in ('yes','true','1'))
assert len(report) == 35
for name, source in report.items():
    raw = core_file(tree, source['shp'])[1]; pal_raw = core_file(tree, source['palette'])[1]
    assert hashlib.sha256(raw).hexdigest() == source['sha256']
    assert hashlib.sha256(pal_raw).hexdigest() == source['palette_sha256']
    shp, palette = Shp(raw), load_pal(pal_raw)
    assert source['palette'] == ('isourb.pal' if source['shp'].endswith('.urb') and not name.startswith('TIBTRE') else 'uniturb.pal'), name
    for frame in range(shp.nframes // 2):
        image = Image.open(folder / 'actors' / f'{name.lower()}_{frame}.png')
        assert image.size == (round(shp.w * 16 / 15), round(shp.h * 16 / 15))
        pixels = shp.frame_pixels(frame); seen = set()
        for offset, value in enumerate(pixels.pixels):
            x, y = pixels.x + offset % pixels.w, pixels.y + offset // pixels.w
            if value <= 1 or value in seen or not (0 <= x < shp.w and 0 <= y < shp.h): continue
            seen.add(value)
            point = round((x + .5) * image.width / shp.w - .5), round((y + .5) * image.height / shp.h - .5)
            assert image.getpixel(point) == (*palette[value], 255), (name, frame, value)
print('NATIVE SCENERY ASSETS PASS: 344 exact terrain records, 145 overlays, 35 source types; original frames/palettes, tree and bridge strength')
