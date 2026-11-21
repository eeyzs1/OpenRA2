"""Check normal movement metadata against original local rules and map overrides."""
import configparser
from decimal import Decimal
import json
import struct
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/ra2pack'))
from ra2lib import MixTree
from gen_native_maps import core_file
from map_format import read_ini
from gen_native_movement import original_float

path=ROOT/'assets/maps/native/all01t/scene.ini'
if not path.exists():
    print('NATIVE MOVEMENT ASSETS SKIP: export original scene');sys.exit(77)
tree=MixTree()
rules=read_ini(core_file(tree,'rules.ini')[1]);scenario=read_ini(core_file(tree,'all01t.map')[1])
ini=configparser.ConfigParser(interpolation=None);ini.optionxform=str;ini.read(path,encoding='utf-8')
assert ini['Scene']['Version']=='16' and ini['NativeMovement']['Version']=='1'
assert (int(ini['Scene']['Types']),int(ini['Scene']['Actors']))==(109,738)
reference=json.loads((ROOT/'tests/fixtures/native-movement-reference.json').read_text(encoding='utf-8'))
def oracle(text,single=False):
    key=str(Decimal(text.rstrip('%')).normalize())+('%' if text.endswith('%') else '')
    row=reference['scalar'][key]
    return struct.unpack('<f' if single else '<d',bytes.fromhex(row['stored_float_bits'] if single else row['double_bits'])[::-1])[0]
for value in reference['scalar']:
    assert original_float(value)==oracle(value),(value,'actual original INI function disagrees')
def truth(text):return text.lower() in ('yes','true','1')
count=0
for section in ini:
    row=ini[section]
    if not section.startswith('Type.') or 'MovementLocomotor' not in row:continue
    count+=1
    original={**rules.get(row['Id'].lower(),{}),**scenario.get(row['Id'].lower(),{})}
    assert int(row['OriginalSpeed'])==int(original.get('speed','0'))
    assert row['OriginalLocomotor']==original['locomotor']
    assert int(row['OriginalAccelerates'])==truth(original.get('accelerates','yes'))
    assert int(row['OriginalPassive'])==truth(original.get('passive','no'))
    for field,key,default in [('OriginalAccelerationFactor','accelerationfactor','.03'),
                              ('OriginalDeaccelerationFactor','deaccelerationfactor','.002'),
                              ('OriginalSlowdownDistance','slowdowndistance','500')]:
        expected=oracle(original[key]) if key in original else float(default)
        assert float(row[field])==expected,(row['Id'],field)
    general={**rules['general'],**scenario.get('general',{})}
    assert float(row['MovementVeteranSpeed'])==oracle(general['veteranspeed'])
    expected=original.get('speedtype','Track' if truth(original.get('crusher','no')) else 'Wheel')
    assert row['OriginalSpeedType'].lower()==expected.lower()
assert count==12
general={**rules['general'],**scenario.get('general',{})}
for key in ['TrackedUphill','TrackedDownhill','WheeledUphill','WheeledDownhill','HoverAcceleration','HoverBrake','HoverBoost']:
    assert float(ini['NativeMovement'][key])==oracle(general[key.lower()]),key
visual={**rules['audiovisual'],**scenario.get('audiovisual',{})}
assert float(ini['NativeMovement']['ConditionYellow'])==oracle(visual['conditionyellow'])
lands=('Clear','Road','Water','Rock','Wall','Tiberium','Beach','Rough','Ice','Railroad','Tunnel','Weeds')
speeds=('Foot','Track','Wheel','Float','Hover','Amphibious','Creep','FloatBeach')
for i,name in enumerate(lands):
    row=ini[f'NativeMovement.Land.{i}'];assert row['Name']==name
    original={**rules.get(name.lower(),{}),**scenario.get(name.lower(),{})}
    for speed in speeds:assert float(row[speed])==oracle(original.get(speed.lower(),'0%'),True),(name,speed)
for i,name in enumerate(scenario['houses'].values()):
    country=scenario[name.lower()]['country'].lower()
    original={**rules.get(country,{}),**scenario.get(country,{})}
    assert float(ini[f'NativeMovement.House.{i}']['SpeedUnitsMult'])==oracle(original.get('speedunitsmult','1'),True)
print('NATIVE MOVEMENT ASSETS PASS:12 source mobile profiles,12 land tables,eight country multipliers,109 definitions and738 actors retained')
