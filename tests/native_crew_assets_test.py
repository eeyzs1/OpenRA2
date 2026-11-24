"""Check crew metadata against each locally selected original MIX, without merging editions."""
import configparser
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ra2pack'))
from ra2lib import MixTree
from source_rules import select_source
from map_format import read_ini

tree = MixTree()
yes = lambda value: str(value).lower() in ('yes', 'true', '1')
provenance = json.loads((ROOT / 'tests/fixtures/native-crew-v60/provenance.json').read_text(encoding='utf-8'))
assert provenance['noHeaderRewriting']
for fixture in provenance['files']:
    assert hashlib.sha256((ROOT / fixture['path']).read_bytes()).hexdigest() == fixture['sha256']
for edition in ('ra2', 'yr'):
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    assert ini.read(ROOT / f'assets/rules/profiles/{edition}.ini', encoding='utf-8-sig')
    _, raw, identity = select_source(tree, edition)
    original = read_ini(raw)
    assert ini['Scene']['Version'] == ('68' if edition == 'ra2' else '43') and ini['Scene']['TemporalVersion'] == '1'
    assert ini['Scene']['CrewVersion'] == ini['NativeCrew']['Version'] == '2'
    assert int(ini['NativeCrew']['OccupyMode']) == int(edition == 'yr')
    combat = original.get('combatdamage', {})
    for field, key, default in [('OpenRange', 'opentoppedrangebonus', '0'),
                                ('WarpDistance', 'opentoppedwarpdistance', '0'),
                                ('OpenDamage', 'opentoppeddamagemultiplier', '1'),
                                ('OccupyDamage', 'occupydamagemultiplier', '1'),
                                ('OccupyROF', 'occupyrofmultiplier', '1'),
                                ('OccupyRange', 'occupyweaponrange', '5')]:
        assert float(ini['NativeCrew'][field]) == float(combat.get(key, default)), (edition, field)
    count = 0
    for section in ini.sections():
        if not section.startswith('Type.') or ini[section].get('Kind') != '0':
            continue
        row = ini[section]
        actor = original[row['Id'].lower()]
        occupier = yes(actor.get('occupier', 'no'))
        assert int(row['CrewOpenTopped']) == int(yes(actor.get('opentopped', 'no')))
        assert int(row['CrewOccupier']) == int(occupier)
        transport = int(actor.get('opentransportweapon', '-1'))
        assert int(row['CrewTransportWeapon']) == transport, (edition, row['Id'])
        numbered = yes(actor.get('gunner', 'no')) or yes(actor.get('isgattling', 'no')) or transport >= 2
        key = f'weapon{transport+1}' if numbered else ('primary', 'secondary')[transport] if transport >= 0 else ''
        normal = actor.get(key, '')
        for elite, field in [(False, 'CrewTransportReference'), (True, 'CrewEliteTransportReference')]:
            reference = int(row[field])
            weapon = actor.get('elite'+key, normal) if elite else normal
            if transport < 0 or weapon.lower() in ('', 'none', '<none>'):
                assert reference == -1, (edition, row['Id'], field)
            else:
                saved = ini[f'NativeWeapon.{reference}'];source = original[weapon.lower()]
                assert saved['Id'].lower() == weapon.lower(), (edition, row['Id'], field)
                assert int(saved['Damage']) == int(source.get('damage', '0'))
                assert int(saved['ROF']) == int(source.get('rof', '1'))
        for elite, field in [(False, 'CrewOccupyWeapon'), (True, 'CrewEliteOccupyWeapon')]:
            reference = int(row[field])
            if not occupier:
                assert reference == -1
                continue
            if edition == 'yr':
                weapon = actor.get('eliteoccupyweapon' if elite else 'occupyweapon', actor.get('primary', ''))
            else:
                weapon = actor.get('eliteprimary', actor.get('primary', '')) if elite else actor.get('primary', '')
            saved = ini[f'NativeWeapon.{reference}']
            assert saved['Id'].lower() == weapon.lower(), (edition, row['Id'], field)
            source = original[weapon.lower()]
            assert int(saved['Damage']) == int(source.get('damage', '0'))
            assert int(saved['ROF']) == int(source.get('rof', '1'))
        count += 1
    print(f'NATIVE_CREW_ASSETS PASS {edition} source={identity} actors={count}')
