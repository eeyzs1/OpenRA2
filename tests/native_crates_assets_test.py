"""Compare authored crate actions and rule values directly with the local MIXs."""
import configparser
import hashlib
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ra2pack'))
from gen_native_maps import ROOT, core_file
from map_format import read_ini
from ra2lib import MixTree

path = ROOT / 'assets/maps/native/all01t/scene.ini'
if not path.exists():
    print('NATIVE CRATES ASSETS SKIP: export the original scene')
    sys.exit(77)
tree = MixTree()
_, raw = core_file(tree, 'all01t.map')
_, rule_raw = core_file(tree, 'rules.ini')
original, rules = read_ini(raw), read_ini(rule_raw)
scene = configparser.ConfigParser(interpolation=None)
scene.optionxform = str
scene.read(path, encoding='utf-8')
assert scene['Scene']['Version'] == '16'
assert scene['Scene']['SourceSHA256'] == hashlib.sha256(raw).hexdigest()
assert scene['NativeEvents']['Version'] == '4' and scene['NativeCrates']['Version'] == '1'
crate = {**rules['craterules'], **original.get('craterules', {})}
powerups = {**rules['powerups'], **original.get('powerups', {})}
assert scene['NativeCrates']['Radius'] == crate['crateradius']
assert scene['NativeCrates']['SoloMoney'] == crate['solocratemoney']
assert {k.lower(): v for k, v in scene['NativeCrates.Powerups'].items()} == powerups
assert int(scene['Scene']['Types']) == 109 and int(scene['Scene']['Actors']) == 738
used = []
for key, value in original['actions'].items():
    assert scene['NativeEvent.actions'][key] == value
    row = value.split(',')
    for i in range(int(row[0])):
        if row[1 + i * 8] == '108':
            used.append(int(row[3 + i * 8]))
assert len(used) == 9 and set(used) == {0, 2, 9, 10, 11, 13}
print('NATIVE CRATES ASSETS PASS: 19 exact powerup records, 9 authored actions, all 6 used kinds; original 738 actors unchanged')
