"""Audit four weapon slots directly against installed original and map-local rules."""
import configparser
from decimal import Decimal
import hashlib
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ra2pack'))
from gen_native_maps import core_file, ROOT
from ra2lib import MixTree
from map_format import read_ini

path=ROOT/'assets/maps/native/all01t/scene.ini'
if not path.exists():print('NATIVE WEAPONS ASSETS SKIP: export original scene');sys.exit(77)
p=configparser.ConfigParser(interpolation=None);p.optionxform=str;p.read(path,encoding='utf-8')
tree=MixTree();raw=core_file(tree,'all01t.map')[1];s=read_ini(raw);r=read_ini(core_file(tree,'rules.ini')[1])
merged=lambda name:{**r.get(name.lower(),{}),**s.get(name.lower(),{})}
yes=lambda value:str(value).lower() in ('yes','true','1')
assert p['Scene']['Version']=='16' and p['Scene']['SourceSHA256']==hashlib.sha256(raw).hexdigest()
g=merged('general');profiles=slots=0
for section in p.sections():
    if not section.startswith('Type.') or p[section].get('Kind','0')!='0':continue
    row=p[section];d=merged(row['Id']);profiles+=1
    assert row['NativeGuns']=='1'
    primary=merged(d.get('primary',''))
    assert int(row['GunMode'])==(2 if yes(primary.get('spawner')) else 1 if yes(d.get('canbeoccupied')) else 0)
    assert int(row['GunSelector'])==(1 if yes(d.get('deployfire')) else 2 if yes(d.get('c4')) else 3 if row['Id'] in ('ADOG','DOG') else 0)
    for field,key in [('GunVeteranCombat','veterancombat'),('GunVeteranROF','veteranrof'),('GunVeteranArmor','veteranarmor')]:
        assert Decimal(row[field])==Decimal(g.get(key,'1'))
    for field,key in [('GunVeteranAbilities','veteranabilities'),('GunEliteAbilities','eliteabilities')]:
        flags=set(d.get(key,'').upper().split(','));assert int(row[field])==sum(bit for name,bit in [('FIREPOWER',1),('ROF',2),('STRONGER',4)] if name in flags)
    for i,key in enumerate(('primary','secondary','eliteprimary','elitesecondary')):
        name=d.get(key,'');prefix=f'Gun{i}';present=name.lower() not in ('','none','<none>');assert int(row[prefix+'Present'])==int(present)
        if not present:continue
        slots+=1;w=merged(name);projectile=merged(w['projectile']);warhead=merged(w['warhead'])
        assert row[prefix+'Id']==name and row[prefix+'Projectile']==w['projectile'] and row[prefix+'Report']==w.get('report','')
        for field,key,default in [('Damage','damage','0'),('ROF','rof','1'),('Range','range','0'),('Burst','burst','1'),('Speed','speed','40')]:
            assert Decimal(row[prefix+field])==Decimal(w.get(key,default)),(row['Id'],prefix,field)
        assert Decimal(row[prefix+'Spread'])==Decimal(warhead.get('cellspread','0'))
        assert Decimal(row[prefix+'PercentAtMax'])==Decimal(warhead.get('percentatmax','1'))
        assert row[prefix+'Verses']==warhead.get('verses',','.join(['100%']*11))
        assert int(row[prefix+'AA'])==int(yes(projectile.get('aa','no'))) and int(row[prefix+'AG'])==int(yes(projectile.get('ag','yes')))
        style=3 if yes(w.get('iselectricbolt')) else 0 if yes(projectile.get('inviso')) else 2 if int(projectile.get('rot','0'))>0 else 1
        assert int(row[prefix+'Style'])==style
        delay=int(d.get('burstdelay0','-1'))
        assert (int(row[prefix+'DelayMin']),int(row[prefix+'DelayMax']))==((delay,delay) if delay>=0 else (3,5))
assert profiles==74
print(f'NATIVE WEAPONS ASSETS PASS: {profiles} source profiles, {slots} weapon slots; map overrides, projectile/warhead data and rank flags match')
