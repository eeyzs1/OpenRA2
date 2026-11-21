"""Independently compare local original AI definitions and recruitment bits."""
import configparser
import hashlib
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/ra2pack'))
from gen_native_maps import core_file, ROOT
from map_format import read_ini
from ra2lib import MixTree

scene = ROOT / 'assets/maps/native/all01t/scene.ini'
if not scene.exists():
    print('NATIVE AI ASSETS SKIP: export the original scene')
    sys.exit(77)
tree = MixTree()
raw = core_file(tree, 'all01t.map')[1]
source = read_ini(raw)
rules = read_ini(core_file(tree, 'rules.ini')[1])
p = configparser.ConfigParser(interpolation=None)
p.read(scene, encoding='utf-8')
assert p['Scene']['version'] == '16' and p['NativeAI']['version'] == '1'
assert p['Scene']['sourcesha256'] == hashlib.sha256(raw).hexdigest()
assert p['NativeAI']['ignoreglobalaitriggers'] == '1' and source['basic']['ignoreglobalaitriggers'] == 'yes'
assert dict(p['NativeAI.Triggers']) == source['aitriggertypes'] and len(p['NativeAI.Triggers']) == 6
assert dict(p['NativeAI.Enabled']) == source['aitriggertypesenable']
general = {**rules['general'], **source['general']}
for key in ('teamdelays', 'totalaiteamcap', 'minimumaidefensiveteams', 'maximumaidefensiveteams',
            'fillearliestteamprobability', 'aitriggersuccessweightdelta', 'aitriggerfailureweightdelta', 'aitriggertrackrecordcoefficient'):
    assert p['NativeAI'][key] == general[key], key
for i, house in enumerate(source['houses'].values()):
    assert p[f'NativeAI.House.{i}']['country'] == source[house.lower()]['country']
for key, text in source['aitriggertypes'].items():
    fields = text.split(',')
    assert fields[4] == '1' and fields[5] == 'NAHAND'
    block = bytes.fromhex(fields[6])
    assert int.from_bytes(block[:4], 'little') == 1 and int.from_bytes(block[4:8], 'little') == 3
    team = source[fields[1].lower()]
    force = source[team['taskforce'].lower()]
    difficulty = [int(x) for x in fields[-3:]].index(1)
    assert int(force['0'].split(',')[0]) == (2, 5, 10)[difficulty]
    assert int(team['max']) == (2, 3, 4)[difficulty]
for section in p.sections():
    if not section.startswith('Actor.') or not p[section]['key'].startswith(('infantry:', 'units:')):
        continue
    actor = p[section]
    group, key = actor['key'].split(':')
    original = source[group][key].split(',')
    row = p['NativeAI.Recruit.' + section[6:]]
    assert row['object'] == actor['key'] and row['recruitable'] == original[-2] and row['autocreaterecruitable'] == original[-1]
    assert row['group'] == original[10 if group == 'infantry' else 9]
actions = source['actions']['08f8484c'].split(',')
assert [int(actions[1+i*8]) for i in range(int(actions[0]))] == [54, 74, 3, 54, 12, 54]
assert any(p[s].get('tag', '').lower() == '08f8706c' and p[s].get('original', '').split(',')[1:2] == ['CABHUT'] for s in p.sections() if s.startswith('Actor.'))
print('NATIVE AI ASSETS PASS: 6 exact local triggers, original gates/weights/team budgets, 2/5/10 rosters and independent original recruit flags')
