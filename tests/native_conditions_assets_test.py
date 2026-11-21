"""Independent original registries, locals and transport data for ALL01 conditions."""
import configparser
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ra2pack'))
from gen_native_maps import ROOT,core_file
from map_format import read_ini
from ra2lib import MixTree

path=ROOT/'assets/maps/native/all01t/scene.ini'
if not path.exists():
    print('NATIVE CONDITIONS ASSETS SKIP: export original scene');sys.exit(77)
tree=MixTree();rules=read_ini(core_file(tree,'rules.ini')[1]);scenario=read_ini(core_file(tree,'all01t.map')[1])
p=configparser.ConfigParser(interpolation=None);p.optionxform=str;p.read(path,encoding='utf-8')
assert p['Scene']['Version']=='16' and p['NativeEvents']['Version']=='4'
assert int(p['Scene']['Types'])==109 and int(p['Scene']['Actors'])==738
assert dict(p['NativeEvent.variableNames'])==scenario['variablenames']=={'0':'Transport 1,0','1':'Transport 2,1'}
for original,exported in [('vehicletypes','vehicleTypes'),('infantrytypes','infantryTypes'),('aircrafttypes','aircraftTypes')]:
    expected=[]
    for table in (rules.get(original,{}),scenario.get(original,{})):
        for _,name in sorted(table.items(),key=lambda item:int(item[0])):
            if name.upper() not in expected:expected.append(name.upper())
    assert list(p['NativeEvent.'+exported].values())==expected,(original,'original enumeration differs')
assert p['NativeEvent.vehicleTypes']['33']=='CMIN'
assert p['NativeEvent.infantryTypes']['3']=='ENGINEER'
assert scenario['events']['0b415b3c']=='1,21,0,3'
assert scenario['events']['0a97c4bc']=='1,20,0,33'
assert scenario['events']['0860ea2c']=='1,36,0,0' and scenario['events']['0860e73c']=='1,36,0,1'
boat=next(p[s] for s in p.sections() if s.startswith('Type.') and p[s].get('Id')=='SAPC')
assert boat['Base']=='AmphTransport' and boat['NativeBody']=='1'
for field,key in [('TransportCapacity','passengers'),('PassengerSize','size'),('PassengerLimit','sizelimit'),('Hp','strength'),('Cost','cost')]:
    assert boat[field]==scenario.get('sapc',{}).get(key,rules['sapc'][key]),field
for script,local in [('0ac7832c','0'),('0ac7ab7c','1')]:
    assert p['NativeEvent.record.'+script]['1']=='8,2'
    assert p['NativeEvent.record.'+script]['2']=='39,'+local
print('NATIVE CONDITIONS ASSETS PASS: original registries, build selectors 33/3, both local defaults, SAPC and unload/local source scripts')
