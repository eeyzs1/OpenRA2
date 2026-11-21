"""Compare the complete gunner mappings and art with the selected original MIX."""
import configparser
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/ra2pack'))
from ra2lib import MixTree
from source_rules import select_source, source_asset
from map_format import read_ini

tree = MixTree()
yes = lambda value: str(value).lower() in ('yes', 'true', '1')
present = lambda value: value.lower() not in ('', 'none', '<none>')
for edition in ('ra2', 'yr'):
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    assert ini.read(ROOT / f'assets/rules/profiles/{edition}.ini', encoding='utf-8-sig')
    _, raw, source_id = select_source(tree, edition)
    original = read_ini(raw)
    rows = {ini[s]['Id'].lower(): ini[s] for s in ini.sections() if s.startswith('Type.') and ini[s].get('Kind') == '0'}
    modes = ['normal', 'repair', 'machinegun', 'flak', 'pistol', 'sniper', 'shock', 'explode', 'brainblast',
             'radcannon', 'chrono', 'terroristexplode', 'cow']
    if edition == 'yr':
        modes += ['initiate', 'virus', 'yuriprime', 'guardian']
    vehicles = {v.lower() for v in original['vehicletypes'].values()}
    aircraft = {v.lower() for v in original['aircrafttypes'].values()}
    for identity, row in rows.items():
        source = original[identity]
        assert row['NativeIFV'] == '2' and int(row['IFVPassengerMode']) == int(source.get('ifvmode', '0'))
        assert int(row['IFVGunner']) == int(yes(source.get('gunner', 'no')))
        deployable = row['Building'] == '1' and present(source.get('undeploysinto', '')) and not yes(source.get('constructionyard', 'no'))
        if edition == 'yr':
            deployable = deployable and row.get('W') == row.get('H') == '1'
        expected = deployable or identity in aircraft or identity in vehicles and not yes(source.get('nonvehicle', 'no'))
        assert int(row['VehicleRepairable']) == int(expected), (edition, identity)
    fv, source = rows['fv'], original['fv']
    assert int(fv['IFVWeapons']) == int(source['weaponcount']) == len(modes)
    assert int(fv['IFVModes']) == len(modes) and fv['IFVTurrets'] == source['turretcount'] == '4'
    delay = int(source.get('burstdelay0', '-1'))
    assert int(fv['IFVBurstDelayMin']) == (delay if delay >= 0 else 3)
    assert int(fv['IFVBurstDelayMax']) == (delay if delay >= 0 else 5)
    for mode, prefix in enumerate(modes):
        assert int(fv[f'IFVMode{mode}Weapon']) == int(source[prefix+'turretweapon'])
        assert int(fv[f'IFVMode{mode}Turret']) == int(source[prefix+'turretindex'])
        for rank, suffix in (('', 'Reference'), ('elite', 'EliteReference')):
            weapon = ini['NativeWeapon.'+fv[f'IFVWeapon{mode}{suffix}']]
            expected = source.get(rank+f'weapon{mode+1}', source[f'weapon{mode+1}'])
            assert weapon['Id'].lower() == expected.lower(), (edition, mode, rank)
    directory = ROOT / 'assets/rules/profiles' / source_id / 'ifv'
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf-8-sig'))
    image = source.get('image', 'FV').lower()
    for stem in [image] + [image+'tur'+(str(i) if i else '') for i in range(4)]:
        for extension in ('.vxl', '.hva'):
            name = stem+extension
            archive, data = source_asset(tree, edition, name)
            assert (directory / name).read_bytes() == data
            assert manifest[name] == {'source': archive, 'sha256': hashlib.sha256(data).hexdigest()}
    repair = ini['NativeWeapon.'+fv['IFVWeapon1Reference']]
    assert repair['Id'] == 'RepairBullet' and repair['Damage'] == '-50' and repair['ROF'] == '80' and repair['Range'] == '1.8'
    assert ini['NativeWarhead.'+ini['NativeWeapon.'+fv['IFVWeapon8Reference']]['Warhead']]['PsychicDamage'] == '1'
    print(f'NATIVE_IFV_ASSETS PASS edition={source_id} modes={len(modes)} ranks=2 parts=5 actors={len(rows)}')
