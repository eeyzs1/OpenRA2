"""Export the first two original skirmish maps, starts and neutral scenery.

The ordinary rules package and AI stay independent of authored mission scenes.
Every coordinate and scenery asset comes from the identified local RA2 archive.
"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path

from gen_native_maps import ROOT, core_file, import_map
from gen_native_scene import ARMOR
from gen_native_scenery import export_scenery
from map_format import parse_map, read_ini
from ra2lib import MixTree

MAPS = {'mp08t2.map': 2, 'mp15s4.map': 4}


def export_skirmish_map(tree, filename, output_root=ROOT / 'assets/maps/native'):
    if filename not in MAPS:
        raise ValueError('the first original skirmish cohort contains only mp08t2 and mp15s4')
    source, raw = core_file(tree, filename)
    native = parse_map(raw)
    if any(native.sections.get(k) for k in ('structures', 'units', 'infantry')):
        raise ValueError('placed armies/buildings need a separately defined skirmish import')
    report = import_map(tree, filename, output_root)
    rules_source, rules_raw = core_file(tree, 'rules.ini')
    art_source, art_raw = core_file(tree, 'art.ini')
    target = Path(output_root) / Path(filename).stem
    (target / 'actors').mkdir(exist_ok=True)
    definitions, objects, scenery_sources = export_scenery(tree, native, read_ini(rules_raw), read_ini(art_raw), target)
    starts = []
    for i in range(MAPS[filename]):
        packed = int(native.sections['waypoints'][str(i)])
        starts.append((packed % 1000, packed // 1000))
    cells = {(t.x, t.y) for t in native.tiles}
    if any(p not in cells for p in starts):
        raise ValueError('original starting point is outside the imported cells')
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini['SkirmishMap'] = dict(Version='1', Id=target.name, SourceSHA256=report['source_sha256'],
        TerrainSHA256=report['nmap_sha256'], Players=str(len(starts)), Types=str(len(definitions)), Actors=str(len(objects)))
    overrides = []
    for identity in ('GAYARD', 'NAYARD'):
        row = native.sections.get(identity.lower(), {})
        if set(row)-{'name', 'techlevel'}:
            raise ValueError(('unsupported map-local rule', identity, row))
        if 'techlevel' in row:
            overrides.append(dict(Id=identity, TechLevel=row['techlevel']))
    ini['SkirmishMap']['Overrides'] = str(len(overrides))
    for i, row in enumerate(overrides):
        ini[f'Override.{i}'] = row
    for i, (x, y) in enumerate(starts):
        ini[f'Start.{i}'] = dict(X=str(x), Y=str(y))
    for i, (fields, armor) in enumerate(definitions):
        row = dict(fields, Armor=str(ARMOR.index(armor)), AirObstacleHeight='0', Underwater='0',
            NavalTargeting='0', LandTargeting='0', Trainable='0', VehicleRepairable='0', ImmuneToPsionicWeapons='1',
            ImmuneToRadiation='1', ImmuneToPoison='1')
        ini[f'Type.{i}'] = row
    indexes = {row['Id']: i for i, (row, _) in enumerate(definitions)}
    for i, obj in enumerate(objects):
        ini[f'Actor.{i}'] = {k: obj[k] for k in ('Key', 'X', 'Y', 'Health', 'Facing', 'Tag', 'Mission')}
        ini[f'Actor.{i}']['Type'] = str(indexes[obj['Id']])
    with (target / 'skirmish.ini').open('w', encoding='utf-8', newline='\n') as f:
        ini.write(f)
    manifest = dict(source=source, source_sha256=hashlib.sha256(raw).hexdigest(), starts=starts,
        actors=len(objects), types=len(definitions), scenery=scenery_sources, overrides=overrides,
        rules=dict(source=rules_source, sha256=hashlib.sha256(rules_raw).hexdigest()),
        art=dict(source=art_source, sha256=hashlib.sha256(art_raw).hexdigest()),
        status='original terrain, starts and neutral scenery for ordinary source-rule skirmishes',
        limits='ambient sound triggers and original global AI trigger scheduling remain outside this map cohort')
    (target / 'skirmish-report.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'ORIGINAL SKIRMISH {target.name}: {len(starts)} starts, {len(objects)} neutral objects', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('maps', nargs='*', default=list(MAPS))
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/maps/native')
    args = parser.parse_args()
    tree = MixTree()
    for filename in args.maps:
        export_skirmish_map(tree, filename, args.output)
