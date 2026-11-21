"""Independently compare every catalog record with the identified original MIX."""
import configparser
from decimal import Decimal
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ra2pack'))
from ra2lib import MixTree
from source_rules import select_source
from map_format import read_ini

tree = MixTree()
yes = lambda value: str(value).lower() in ('yes', 'true', '1')
for edition in ('ra2', 'yr'):
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    assert ini.read(ROOT / f'assets/rules/profiles/{edition}.ini', encoding='utf-8')
    _, raw, source_id = select_source(tree, edition)
    original = read_ini(raw)
    assert ini['Scene']['Version'] == ('46' if edition == 'ra2' else '43') and ini['Scene']['RulesId'] == source_id
    assert ini['Scene']['TemporalVersion'] == '1'
    for section in ini.sections():
        if section.startswith('Type.') and ini[section].get('Kind', '0') == '0':
            actor = ini[section]
            assert int(actor['Warpable']) == int(yes(original[actor['Id'].lower()].get('warpable', 'yes')))
    assert ini['Scene']['SourceSHA256'] == hashlib.sha256(raw).hexdigest()
    header = ini['NativeWeapons']
    assert header['Version'] == '3'
    weapons = [ini[f'NativeWeapon.{i}'] for i in range(int(header['Weapons']))]
    projectiles = [ini[f'NativeProjectile.{i}'] for i in range(int(header['Projectiles']))]
    warheads = [ini[f'NativeWarhead.{i}'] for i in range(int(header['Warheads']))]
    for rows in (weapons, projectiles, warheads):
        assert len({row['Id'].lower() for row in rows}) == len(rows)
    names = {row['Id'].lower(): i for i, row in enumerate(weapons)}
    for p in projectiles:
        source = original[p['Id'].lower()]
        for field, key, default in [('Arcing', 'arcing', 'no'), ('Inviso', 'inviso', 'no'),
                                    ('Vertical', 'vertical', 'no'), ('Inaccurate', 'inaccurate', 'no'),
                                    ('Proximity', 'proximity', 'no'), ('AA', 'aa', 'no'), ('AG', 'ag', 'yes'),
                                    ('Level', 'level', 'no'), ('FlakScatter', 'flakscatter', 'no'), ('Airburst', 'airburst', 'no')]:
            assert int(p[field]) == int(yes(source.get(key, default))), (edition, p['Id'], field)
        for field, key, default in [('ROT', 'rot', '0'), ('Cluster', 'cluster', '1'), ('ShrapnelCount', 'shrapnelcount', '0')]:
            assert int(p[field]) == int(source.get(key, default))
        for field, key in [('ShrapnelWeapon', 'shrapnelweapon'), ('AirburstWeapon', 'airburstweapon')]:
            assert int(p[field]) == names.get(source.get(key, '').lower(), -1)
        assert p['Image'] == source.get('image', 'none')
    for wh in warheads:
        source = original[wh['Id'].lower()]
        assert wh['Verses'] == source.get('verses', ','.join(['100%'] * 11))
        assert Decimal(wh['Spread']) == Decimal(source.get('cellspread', '0'))
        assert Decimal(wh['PercentAtMax']) == Decimal(source.get('percentatmax', '1'))
        assert int(wh['Temporal']) == int(yes(source.get('temporal', 'no')))
        for field, key, default in [('AffectsAllies', 'affectsallies', 'yes'), ('PsychicDamage', 'psychicdamage', 'no'), ('Radiation', 'radiation', 'no')]:
            assert int(wh[field]) == int(yes(source.get(key, default)))
        assert int(wh['InfantryDeath']) == int(source.get('infdeath', '0'))
    for weapon in weapons:
        source = original[weapon['Id'].lower()]
        assert projectiles[int(weapon['Projectile'])]['Id'].lower() == source['projectile'].lower()
        assert warheads[int(weapon['Warhead'])]['Id'].lower() == source['warhead'].lower()
        for field, key, default in [('Damage', 'damage', '0'), ('ROF', 'rof', '1'), ('Range', 'range', '0'),
                                    ('Burst', 'burst', '1'), ('Speed', 'speed', '40'), ('AmbientDamage', 'ambientdamage', '0'),
                                    ('LaserDuration', 'laserduration', '15')]:
            assert Decimal(weapon[field]) == Decimal(source.get(key, default)), (edition, weapon['Id'], field)
        for field, key in [('Laser', 'islaser'), ('HouseColor', 'ishousecolor'), ('Sonic', 'issonic'),
                            ('Electric', 'iselectricbolt'), ('RadBeam', 'isradbeam')]:
            assert int(weapon[field]) == int(yes(source.get(key, 'no')))
        color = [int(n) for n in source.get('laserinnercolor', '255,0,0').split(',')]
        assert int(weapon['LaserColor']) == color[0] << 16 | color[1] << 8 | color[2]
        assert weapon['Report'] == source.get('report', '')
        assert int(weapon['Suicide']) == int(yes(source.get('suicide', 'no')))
        assert int(weapon['RadLevel']) == int(source.get('radlevel', '0'))
    slots = 0
    for section in ini.sections():
        if not section.startswith('Type.') or ini[section].get('Kind', '0') != '0':
            continue
        row = ini[section]
        for slot in range(12):
            if row.get(f'Gun{slot}Present') == '1':
                slots += 1
                assert weapons[int(row[f'Gun{slot}Reference'])]['Id'].lower() == row[f'Gun{slot}Id'].lower()
        death = original[row['Id'].lower()].get('deathweapon', '').lower()
        if death not in ('', 'none', '<none>'):
            assert death in names
    assert {'cometfragment', 'supercometfragment'} <= names.keys()
    print(f'NATIVE_WEAPON_CATALOG PASS edition={source_id} weapons={len(weapons)} projectiles={len(projectiles)} warheads={len(warheads)} slots={slots}')
