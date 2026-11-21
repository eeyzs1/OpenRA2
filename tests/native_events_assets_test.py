"""Compare every exported native trigger record with the locally installed map."""
import configparser
from decimal import Decimal
import hashlib
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ra2pack'))
from gen_native_maps import ROOT,core_file
from map_format import read_ini
from ra2lib import MixTree

folder=ROOT/'assets/maps/native/all01t'
if not (folder/'scene.ini').exists():
    print('NATIVE EVENTS ASSETS SKIP: export the original scene');sys.exit(77)
tree=MixTree();raw=core_file(tree,'all01t.map')[1];original=read_ini(raw);rules=read_ini(core_file(tree,'rules.ini')[1])
p=configparser.ConfigParser(interpolation=None);p.optionxform=str;p.read(folder/'scene.ini',encoding='utf-8')
assert p['Scene']['Version']=='16' and p['Scene']['SourceSHA256']==hashlib.sha256(raw).hexdigest()
for name in ('triggers','events','actions','tags','celltags','waypoints','scripttypes','taskforces','teamtypes'):
    assert dict(p['NativeEvent.'+name])==original.get(name,{}),(name,'source records changed')
for table in ('scripttypes','taskforces','teamtypes'):
    for identifier in original[table].values():
        assert dict(p['NativeEvent.record.'+identifier.lower()])==original[identifier.lower()],identifier
assert (len(p['NativeEvent.triggers']),len(p['NativeEvent.tags']),len(p['NativeEvent.celltags']))==(130,128,180)
assert (len(p['NativeEvent.teamtypes']),len(p['NativeEvent.taskforces']),len(p['NativeEvent.scripttypes']))==(49,29,38)
countries=[]
for table in (rules['countries'],original['countries']):
    for key,name in sorted(table.items(),key=lambda item:int(item[0])):
        if name.lower() not in [v.lower() for v in countries]:countries.append(name)
for house,name in enumerate(original['houses'].values()):
    country=original[name.lower()]['country']
    index=next(i for i,v in enumerate(countries) if v.lower()==country.lower())
    assert int(p['NativeEvent.houseNumbers'][str(index)])==house
    assert int(p['NativeEvent.houses'][country])==house
assert p['NativeEvents']['FogOfWar']=='0' and p['NativeEvents']['RevealRadius']=='9'
assert Decimal(p['NativeEvents']['StrayDistance'])==Decimal(original.get('general',{}).get('straydistance',rules['general'].get('straydistance','2')))
assert p['NativeEvent.houseNumbers']['13']=='0'
assert any(row.get('Id')=='CMIN' and row.get('Base')=='ChronoMiner' and row.get('NativeBody')=='1'
           for name in p.sections() if name.startswith('Type.') for row in [p[name]])
print('NATIVE EVENTS ASSETS PASS: 130 triggers, 128 tags, 180 cell tags; every action, event, team, script and task force matches the original')
