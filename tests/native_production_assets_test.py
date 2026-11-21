"""Compare native production gates, timing, identity and cameos to original files."""
import configparser
from decimal import Decimal
import hashlib
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ra2pack'))
from gen_native_maps import core_file,ROOT
from map_format import read_ini
from ra2lib import MixTree,Shp,load_pal,shp_frame_to_rgba
from PIL import Image

folder=ROOT/'assets/maps/native/all01t'
if not (folder/'scene.ini').exists():print('NATIVE PRODUCTION ASSETS SKIP: export source scene');sys.exit(77)
tree=MixTree();m=read_ini(core_file(tree,'all01t.map')[1]);r=read_ini(core_file(tree,'rules.ini')[1]);a=read_ini(core_file(tree,'art.ini')[1])
merged=lambda key:{**r.get(key.lower(),{}),**m.get(key.lower(),{})}
p=configparser.ConfigParser(interpolation=None);p.read(folder/'scene.ini',encoding='utf-8')
assert p['Scene']['version']=='16' and p['NativeProduction']['version']=='1'
assert Decimal(p['NativeProduction']['buildspeed'])==Decimal(merged('general')['buildspeed'])==Decimal('.7')
assert p['NativeProduction.House.0']['techlevel']==m['player house']['techlevel']=='3'
assert p['NativeProduction.House.0']['country']=='Americans'
types={p[s]['id']:p[s] for s in p.sections() if s.startswith('Type.')}
profiles={p['Type.'+s.split('.')[-1]]['id']:p[s] for s in p.sections() if s.startswith('NativeProduction.Type.')}
assert len(types)==109 and len(profiles)==74 and int(p['Scene']['actors'])==738
for id,row in profiles.items():
    d=merged(id)
    assert int(row['infantryexit'])==(1 if d.get('gdibarracks','no').lower()=='yes' else 2 if d.get('nodbarracks','no').lower()=='yes' else 0)
    for field,default in [('techlevel','-1'),('owner',''),('prerequisite',''),('requiredhouses',''),('forbiddenhouses',''),('buildlimit','2147483647'),('factory','none'),('freeunit','')]:
        assert row[field]==d.get(field,default),(id,field)
    frames=int(Decimal(d.get('cost','0'))*Decimal('.7')*Decimal('.9')*Decimal(d.get('buildtimemultiplier','1')))
    assert int(row['basebuildframes'])==frames and int(row['stepticks'])==2*max(1,min(255,frames//54))
    if 'cameosource' in row:
        data=next(data for path,data in tree.find_all(row['cameo'].lower()+'.shp') if path==row['cameosource'])
        assert hashlib.sha256(data).hexdigest()==row['cameosha256']
        shp=Shp(data);f=shp.frame_pixels(0);expected=Image.new('RGBA',(shp.w,shp.h))
        if f.w and f.h:expected.alpha_composite(shp_frame_to_rgba(f,load_pal(core_file(tree,'cameo.pal')[1]),remap=False),(f.x,f.y))
        assert Image.open(folder/'actors'/(id.lower()+'_icon.png')).convert('RGBA').tobytes()==expected.tobytes(),id
for id,cost,hp,ticks in [('ENGINEER',500,75,540),('E1',200,125,216),('GAPILE',500,500,540)]:
    assert (int(types[id]['cost']),int(types[id]['hp']),int(profiles[id]['stepticks'])*54)==(cost,hp,ticks)
assert (types['GAPILE']['building'],types['GAPILE']['base'],types['ENGINEER']['base'])==('1','Barracks','Engineer')
assert profiles['ENGINEER']['prerequisite'].upper()=='BARRACKS' and profiles['GAPILE']['prerequisite']=='POWER,GACNST'
assert profiles['GAREFN']['freeunit']=='CMIN'
assert profiles['GAGATE_A']['category']=='4' and profiles['GAGATE_A']['techlevel']=='-1'
# Independently close the original player's available construction/production
# tree. This verifies that the new definitions cover every actually reachable
# type in this mission, rather than selecting only convenient demonstration types.
country='AMERICANS';level=3;general=merged('general')
def names(value):return {v.strip().upper() for v in value.split(',') if v.strip()}
def permitted(d):
    return 0<=int(d.get('techlevel','-1'))<=level and int(d.get('buildlimit','2147483647'))!=0 and (not d.get('requiredhouses') or country in names(d['requiredhouses'])) and country not in names(d.get('forbiddenhouses',''))
owned={v.split(',')[1].upper() for v in m['structures'].values() if v.split(',')[6].lower()=='0965bddc'}
def prerequisites(d):
    for name in names(d.get('prerequisite','')):
        if name in owned:continue
        group=names(general.get('prerequisite'+name.lower(),''))
        if not group.intersection(owned):return False
    return True
reachable=set()
building_ids={v.upper() for table in (r['buildingtypes'],m.get('buildingtypes',{})) for v in table.values()}
changed=True
while changed:
    changed=False
    for id in building_ids:
        d=merged(id)
        if permitted(d) and prerequisites(d) and country in names(d.get('owner','')) and id not in reachable:
            reachable.add(id);owned.add(id);changed=True
for table,category in [('infantrytypes','INFANTRYTYPE'),('vehicletypes','UNITTYPE'),('aircrafttypes','AIRCRAFTTYPE')]:
    for id in {v.upper() for data in (r[table],m.get(table,{})) for v in data.values()}:
        d=merged(id)
        if permitted(d) and prerequisites(d) and any(merged(b).get('factory','').upper()==category and names(merged(b).get('owner','')).intersection(names(d.get('owner',''))) for b in owned):reachable.add(id)
assert reachable=={'GAPOWR','GAREFN','GAPILE','GAPILL','E1','ENGINEER','ADOG'},reachable
assert reachable.issubset(types)
print('NATIVE PRODUCTION ASSETS PASS: 74 exact source profiles, original cameos; native barracks/engineer gates, cost, health and 54-step timing')
