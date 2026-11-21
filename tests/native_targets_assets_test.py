"""Independent audit of source target roles and the five rating coefficients."""
import configparser
from decimal import Decimal
import hashlib
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ra2pack'))
from gen_native_maps import core_file, ROOT
from map_format import read_ini
from ra2lib import MixTree

file=ROOT/'assets/maps/native/all01t/scene.ini'
if not file.exists():
    print('NATIVE TARGET ASSETS SKIP: export original scene');sys.exit(77)
ini=configparser.ConfigParser(interpolation=None);ini.optionxform=str;ini.read(file,encoding='utf-8')
tree=MixTree();raw=core_file(tree,'all01t.map')[1];scenario=read_ini(raw);rules=read_ini(core_file(tree,'rules.ini')[1])
assert ini['Scene']['Version']=='16' and ini['Scene']['SourceSHA256']==hashlib.sha256(raw).hexdigest()
merged=lambda name:{**rules.get(name.lower(),{}),**scenario.get(name.lower(),{})}
yes=lambda text:int(str(text).lower() in ('yes','true','1'))
general=merged('general');count=0
coefficients={'TargetMyEffectiveness':'myeffectivenesscoefficient','TargetEffectiveness':'targeteffectivenesscoefficient',
              'TargetSpecialCoefficient':'targetspecialthreatcoefficient','TargetStrength':'targetstrengthcoefficient',
              'TargetDistance':'targetdistancecoefficient'}
for section in ini.sections():
    if not section.startswith('Type.'):continue
    row=ini[section]
    if int(row.get('Kind','0'))!=0:continue
    count+=1;id=row['Id'].lower();definition=merged(id)
    assert row['TargetProfile']=='1'
    assert int(row['TargetLegal'])==yes(definition.get('legaltarget','yes'))
    assert int(row['TargetInvisible'])==yes(definition.get('invisibleingame','no'))
    assert int(row['TargetStorage'])==int(Decimal(definition.get('storage','0'))!=0)
    assert int(row['TargetFactory'])==int(definition.get('factory','none').lower() not in ('','none','<none>'))
    assert int(row['TargetUndeploys'])==int(definition.get('undeploysinto','none').lower() not in ('','none','<none>'))
    for output,key in [('TargetDetectDisguise','detectdisguise'),('TargetAIDetectDisguise','aidetectdisguise')]:
        assert int(row[output])==yes(definition.get(key,'no'))
    assert Decimal(row['TargetSpecialThreat'])==Decimal(definition.get('specialthreatvalue','0'))
    for output,key in coefficients.items():
        default=Decimal(general.get(key+'default','0'))
        value=Decimal(definition.get(key,str(default)))
        if id in scenario and value==0:value=default
        assert Decimal(row[output])==value,(id,key)
    for slot,key in enumerate(('primary','secondary')):
        name=definition.get(key,'');prefix=f'TargetWeapon{slot}'
        present=name.lower() not in ('','none','<none>');assert int(row[prefix+'Present'])==int(present)
        if not present:continue
        weapon=merged(name);projectile=merged(weapon['projectile']);warhead=merged(weapon['warhead'])
        assert Decimal(row[prefix+'Range'])==Decimal(weapon.get('range','0'))
        assert int(row[prefix+'AA'])==yes(projectile.get('aa','no'))
        assert int(row[prefix+'AG'])==yes(projectile.get('ag','yes'))
        assert row[prefix+'Verses']==warhead.get('verses',','.join(['100%']*11))
        assert int(row[prefix+'VersesDefault'])==int('verses' not in warhead)
assert count==74
for id in scenario['scripttypes'].values():
    script=scenario[id.lower()]
    for key,value in script.items():
        if key.isdigit() and value.startswith('0,'):
            assert value==ini['NativeEvent.record.'+id.lower()][key]
print(f'NATIVE TARGET ASSETS PASS: {count} source profiles; exact quarry lines, roles, coefficients and both weapon slots')
