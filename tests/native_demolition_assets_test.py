"""Audit script demolition against the original rules and authored action list."""
import configparser
from decimal import Decimal
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ra2pack'))
from gen_native_maps import ROOT, core_file
from map_format import read_ini
from ra2lib import MixTree

scene = ROOT / 'assets/maps/native/all01t/scene.ini'
if not scene.exists():
    print('NATIVE DEMOLITION ASSETS SKIP: export the original scene')
    sys.exit(77)
tree = MixTree()
rules = read_ini(core_file(tree, 'rules.ini')[1])
scenario = read_ini(core_file(tree, 'all01t.map')[1])
merged = lambda key: {**rules.get(key.lower(), {}), **scenario.get(key.lower(), {})}
p = configparser.ConfigParser(interpolation=None)
p.read(scene, encoding='utf-8')
assert p['Scene']['Version'] == '16' and p['NativeEvents']['Version'] == '4'
profile = p['NativeEvent.Demolition']
combat, general = merged('combatdamage'), merged('general')
warhead = merged(combat['c4warhead'])
assert profile['warhead'] == combat['c4warhead'] == 'Super'
assert int(profile['bridgestrength']) == int(combat['bridgestrength']) == 1500
assert int(profile['flightlevel']) == int(general['flightlevel']) == 1500
for key, default in [('cellspread', '0'), ('percentatmax', '1')]:
    assert Decimal(profile[key]) == Decimal(warhead.get(key, default))
    assert int(profile[key + 'default']) == int(key not in warhead)
assert profile['verses'] == warhead['verses']
assert [Decimal(v.rstrip('%')) for v in profile['verses'].split(',')] == [100] * 11
assert profile['animlist'] == warhead['animlist']
for key in ['wall', 'wood', 'tiberium']:
    assert int(profile[key]) == int(warhead.get(key, 'no').lower() in ('yes', 'true', '1'))
assert profile['wall'] == profile['wood'] == '0' and profile['tiberium'] == '1'
trigger = scenario['triggers']['080777bc']
assert trigger.split(',')[2] == 'Break Bridges'
assert scenario['events']['080777bc'] == '1,13,0,0'
raw = scenario['actions']['080777bc']
assert p['NativeEvent.actions']['080777bc'] == raw
parts = raw.split(',')
assert int(parts[0]) == 10 and len(parts) == 81
actions = [parts[1+i*8:9+i*8] for i in range(10)]
assert actions == [['63', '0', '80', '0', '0', '0', '0', 'CC']] * 5 + [['63', '0', '79', '0', '0', '0', '0', 'CB']] * 5
for number, position in [('80', 59062), ('79', 52082)]:
    assert int(p['NativeEvent.waypoints'][number]) == int(scenario['waypoints'][number]) == position
assert p['Scene']['destroyablebridges'] == '0'
print('NATIVE DEMOLITION ASSETS PASS: C4 source profile, zero-time trigger, ten numeric-waypoint actions and protected ordinary bridges match original files')
