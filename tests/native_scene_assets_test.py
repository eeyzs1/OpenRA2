"""Audit native actors against installed RA2 map/rules/art, including local overrides."""
import configparser
import hashlib
import json
from pathlib import Path
import sys
from decimal import Decimal
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ra2pack'))
from gen_native_maps import core_file, ROOT
from gen_native_scene import ARMOR
from map_format import read_ini
from ra2lib import MixTree, Shp, Vxl, load_pal
from PIL import Image

folder=ROOT/'assets/maps/native/all01t'
if not (folder/'scene.ini').exists():
    print('NATIVE SCENE ASSETS SKIP: export original scene first');sys.exit(77)
tree=MixTree();source=read_ini(core_file(tree,'all01t.map')[1]);rules=read_ini(core_file(tree,'rules.ini')[1]);art=read_ini(core_file(tree,'art.ini')[1])
ini=configparser.ConfigParser(interpolation=None);ini.optionxform=str;ini.read(folder/'scene.ini',encoding='utf-8')
report=json.loads((folder/'scene-report.json').read_text(encoding='utf-8'))
assert ini['Scene']['SourceSHA256']==hashlib.sha256(core_file(tree,'all01t.map')[1]).hexdigest()
assert (int(ini['Scene']['Types']),int(ini['Scene']['Actors']),int(ini['Scene']['Houses']))==(109,738,8)
assert (int(ini['Scene']['AuthoredActors']),int(ini['Scene']['SceneryActors']))==(249,489)
house_names=list(source['houses'].values())
for i,name in enumerate(house_names):
    row=ini[f'House.{i}'];original=source[name.lower()]
    assert row['Name']==name and int(row['Money'])==100*int(original['credits'])
    allies={house_names.index(n) for n in original['allies'].split(',')}|{i}
    assert int(row['Allies'])==sum(1<<n for n in allies)

for i in range(74):
    row=ini[f'Type.{i}'];id=row['Id'];original={**rules.get(id.lower(),{}),**source.get(id.lower(),{})}
    assert int(row['Hp'])==int(original.get('strength','1'))
    assert int(row['Armor'])==ARMOR.index(original.get('armor','none').lower())
    assert int(row['Sight'])==int(original.get('sight','0'))
    assert row['OriginalSpeed']==original.get('speed','0') and row['OriginalLocomotor']==original.get('locomotor','')
    veteran={a.strip().upper() for a in original.get('veteranabilities','').split(',')}
    elite=veteran|{a.strip().upper() for a in original.get('eliteabilities','').split(',')}
    assert int(row['VeteranFaster'])==int('FASTER' in veteran) and int(row['EliteFaster'])==int('FASTER' in elite)
    assert row['OriginalVeteranSpeed']=={**rules['general'],**source.get('general',{})}.get('veteranspeed','1')
    for field,key in [('Immune','immune'),('BridgeRepairHut','bridgerepairhut'),('TooBigToFitUnderBridge','toobigtofitunderbridge'),('Gate','gate')]:
        assert int(row[field])==int(original.get(key,'no').lower() in ('yes','true','1')),(id,field,'original rule mismatch')
    if row['Building']=='1':
        record=report['types'][id];data=core_file(tree,record['shp'])[1]
        assert hashlib.sha256(data).hexdigest()==record['sha256']
        assert hashlib.sha256(core_file(tree,record['palette'])[1]).hexdigest()==record['palette_sha256']
        for layer in record.get('idle_layers',[]):
            assert hashlib.sha256(core_file(tree,layer['shp'])[1]).hexdigest()==layer['sha256']
        original_art=art[original.get('image',id).lower()]
        w,h=map(int,original_art.get('foundation','1x1').split('x'))
        assert (int(row['Width']),int(row['Height']))==(max(1,w),max(1,h))
        shp=Shp(data);palette=load_pal(core_file(tree,record['palette'])[1])
        if row['Gate']=='1':
            assert int(row['GateStages'])==int(original_art.get('gatestages','9'))
            assert int(row['GateOpenFrames'])==int(Decimal(original.get('deploytime','0'))*900)
            assert int(row['GateCloseFrames'])==int(Decimal(original.get('gateclosedelay','0'))*900)
            assert int(row['ArtFrames'])==shp.nframes//2
        for damaged in range(shp.nframes//2 if row['Gate']=='1' else 2):
            image=Image.open(folder/'actors'/f'{id.lower()}_{damaged}.png')
            assert image.size==(round(shp.w*16/15),round(shp.h*16/15))
            if row['Remappable']=='1':
                mask=Image.open(folder/'actors'/f'{id.lower()}_{damaged}_remap.png')
                assert mask.size==image.size
            if not record.get('idle_layers'):
                # Compare source palette samples directly, including natural
                # red pixels and the dark end of the house-color ramp.
                frame=shp.frame_pixels(damaged if shp.nframes>=4 else 0);samples={}
                for offset,value in enumerate(frame.pixels):
                    x=frame.x+offset%frame.w;y=frame.y+offset//frame.w
                    if value>1 and 0<=x<shp.w and 0<=y<shp.h:samples.setdefault(value,(x,y))
                for value,(x,y) in samples.items():
                    point=(round((x+.5)*image.width/shp.w-.5),round((y+.5)*image.height/shp.h-.5))
                    assert image.getpixel(point)==(*palette[value],255),(id,damaged,value,'source pixel changed')
                    if row['Remappable']=='1':
                        assert mask.getpixel(point)==(max(palette[value]) if 16<=value<=31 else 0),(id,damaged,value,'remap shade changed')
    elif row.get('NativeBody','0')=='1':
        voxel=Vxl(core_file(tree,report['types'][id]['image']+'.vxl')[1])
        source_remap=any(16<=p[3]<=31 for section in voxel.sections for p in section.voxels)
        for filename,record in report['types'][id]['voxel_sources'].items():
            assert hashlib.sha256(core_file(tree,filename)[1]).hexdigest()==record['sha256']
        for direction in range(8):
            image=Image.open(folder/'actors'/f'{id.lower()}_d{direction}.png')
            assert image.getbbox()
            if row['Remappable']=='1':
                mask=Image.open(folder/'actors'/f'{id.lower()}_d{direction}_remap.png')
                assert mask.size==image.size
                if not source_remap:assert mask.getbbox() is None,(id,direction,'fixed-color vehicle was marked for house remap')

for i in range(249):
    row=ini[f'Actor.{i}'];section,key=row['Key'].split(':');values=source[section][key].split(',')
    assert row['Original']==source[section][key]
    assert row['X']==values[3] and row['Y']==values[4] and row['Health']==values[2]
    assert ini[f'Type.{row["Type"]}']['Id']==values[1]
    assert ini[f'House.{row["House"]}']['Name']==values[0]
    assert row['Facing']==values[7 if section=='infantry' else 5]
print('NATIVE SCENE ASSETS PASS: 249 original records; 68 placed definitions plus six source runtime types including SAPC; original health/armor, foundations, palettes and house relations')
