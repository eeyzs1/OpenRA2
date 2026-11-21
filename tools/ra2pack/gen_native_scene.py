"""Export authored RA2 actors, definitions and original building SHP artwork.

The scene is separate from mission execution: no substitute waves or objectives
are generated. The original object keys/tags and map-local rule overrides stay
available to the native event runtime.
"""
import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import colorsys
import sys

from PIL import Image
from gen_native_maps import ROOT, core_file
from map_format import read_ini, parse_map
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from gen_native_scenery import export_scenery
from gen_native_movement import export_native_movement
from gen_native_gates import export_gate_frames,original_frames
from gen_native_events import export_native_events
from gen_native_rockets import export_native_rockets
from gen_native_targets import export_native_targets
from gen_native_weapons import export_native_weapons
from gen_native_production import export_native_production
from gen_native_ai import export_native_ai
from gen_native_crates import export_native_crates
from gen_native_presentation import export_text

UNIT_BASE = {"E1":"GI", "E2":"Conscript", "ADOG":"AttackDog", "DOG":"AttackDog",
             "TANY":"Tanya", "DRED":"Dreadnought", "V3":"V3Launcher", "MTNK":"Grizzly", "CMIN":"ChronoMiner", "ENGINEER":"Engineer", "SAPC":"AmphTransport"}
BLD_BASE = {"GACNST":"ConYard", "NACNST":"ConYard", "GAPOWR":"PowerPlant", "NAPOWR":"TeslaReactor",
            "GAREFN":"OreRefinery", "GAPILE":"Barracks", "NAHAND":"Barracks", "NAWEAP":"WarFactory", "NARADR":"Radar",
            "GAPILL":"Pillbox", "NALASR":"SentryGun", "NASAM":"PatriotMissile", "GAGATE_A":"Wall"}
ARMOR = ["none","flak","plate","light","medium","heavy","wood","steel","concrete","special_1","special_2"]


def truth(value, default=False):
    return str(value).lower() in ("yes","true","1") if value is not None else default


def export_scene(tree, filename="all01t.map", output=None):
    source, raw = core_file(tree, filename)
    native = parse_map(raw); scenario = native.sections
    if filename.lower() != "all01t.map":
        raise ValueError("native actor behavior mapping currently covers all01t.map only")
    _, rules_raw=core_file(tree,"rules.ini");_, art_raw=core_file(tree,"art.ini")
    rules,art=read_ini(rules_raw),read_ini(art_raw)
    target=Path(output) if output else ROOT/"assets/maps/native"/Path(filename).stem.lower()
    (target/"actors").mkdir(parents=True,exist_ok=True)
    ini=configparser.ConfigParser(interpolation=None);ini.optionxform=str
    types=sorted({v.split(',')[1].upper() for s in ("structures","units","infantry") for v in scenario[s].values()})
    # This authored reinforcement profile is needed by the original base
    # handover. Other roster entries remain in the graph, with explicit runtime
    # capability diagnostics until their physical/art profiles are available.
    for extra in ('CMIN','DMISL','V3ROCKET','ENGINEER','GAPILE','SAPC'):
        if extra not in types:types.append(extra)
    building_types={v.split(',')[1].upper() for v in scenario['structures'].values()}
    building_types.add('GAPILE')
    report={"source":source,"source_sha256":hashlib.sha256(raw).hexdigest(),"types":{},"actors":[]}
    houses=list(scenario['houses'].values());house_indices={name:i for i,name in enumerate(houses)}
    ini['Scene']={"Version":"1","Map":Path(filename).stem.lower(),"SourceSHA256":report['source_sha256'],
                  "Types":str(len(types)),"Houses":str(len(houses)),"Actors":str(sum(len(scenario[s]) for s in ("structures","units","infantry")))}
    combat={**rules.get('combatdamage',{}),**scenario.get('combatdamage',{})}
    ini['Scene']['DestroyableBridges']=str(int(truth(scenario.get('specialflags',{}).get('destroyablebridges',combat.get('destroyablebridges','yes')))))
    for i,name in enumerate(houses):
        house=scenario[name.lower()];country=scenario[house['country'].lower()]
        parent=country.get('parentcountry','Neutral')
        allies=[house_indices[v] for v in house.get('allies','').split(',') if v in house_indices]
        faction='Soviet' if country.get('side')=='Nod' else 'Allies'
        country_key={"Americans":"America","Germans":"Germany","Russians":"Russia","Neutral":"None"}.get(parent,"America")
        hsv=[int(v) for v in rules['colors'][country.get('color','Grey').lower()].split(',')]
        rgb=[round(v*255) for v in colorsys.hsv_to_rgb(*(v/255 for v in hsv))]
        ini[f'House.{i}']={"Name":name,"Faction":faction,"Country":country_key,
                          "Money":str(int(house.get('credits','0'))*100),"Allies":str(sum(1<<a for a in set(allies+[i]))),
                          "Color":str(2 if country.get('color')=='DarkBlue' else 1 if country.get('color')=='DarkRed' else 0),
                          "RGB":str(rgb[0]<<16|rgb[1]<<8|rgb[2]),
                          "PlayerControl":str(int(truth(house.get('playercontrol')))),"OriginalColor":country.get('color','')}

    def get(name):
        try:return core_file(tree,name)
        except FileNotFoundError:return None,None

    def shp_source(image,definition):
        letters={'URBAN':'U','SNOW':'A','TEMPERATE':'T'}
        candidates=[]
        if truth(definition.get('newtheater')) and len(image)>1:
            candidates += [image[0]+letters[native.theater]+image[2:],image[0]+'G'+image[2:]]
        candidates += [image]
        for name in dict.fromkeys(candidates):
            path,data=get(name.lower()+'.shp')
            if data is not None:return name.lower()+'.shp',path,data
        raise FileNotFoundError(f"original SHP missing: {image}, {candidates}")

    for index,id in enumerate(types):
        definition={**rules.get(id.lower(),{}),**scenario.get(id.lower(),{})}
        image=definition.get('image',id).lower();visual=art.get(image,{})
        building=id in building_types
        armor=definition.get('armor','none').lower();hp=int(definition.get('strength','1'))
        if armor not in ARMOR:raise ValueError((id,'unknown armor',armor))
        fields={"Id":id,"Name":definition.get('name',id),"Building":str(int(building)),
                "Base":BLD_BASE.get(id,'CivHouse') if building else UNIT_BASE.get(id,'NativeVehicle'),
                "Hp":str(hp),"Sight":definition.get('sight','0'),"Cost":definition.get('cost','0'),
                "Armor":str(ARMOR.index(armor)),"Selectable":str(int(truth(definition.get('selectable'),True))),
                "Significant":str(int(not truth(definition.get('insignificant')))),"Power":definition.get('power','0'),
                "Garrison":definition.get('maxnumberoccupants','0') if truth(definition.get('canbeoccupied')) else '0',
                "Capturable":str(int(truth(definition.get('capturable'),False))),"OriginalSpeed":definition.get('speed','0'),
                "OriginalLocomotor":definition.get('locomotor',''),
                "VeteranFaster":str(int('FASTER' in {a.strip().upper() for a in definition.get('veteranabilities','').split(',')})),
                "EliteFaster":str(int('FASTER' in {a.strip().upper() for a in (definition.get('veteranabilities','')+','+definition.get('eliteabilities','')).split(',')})),
                "OriginalVeteranSpeed":{**rules.get('general',{}),**scenario.get('general',{})}.get('veteranspeed','1'),
                "Art":id.lower(),"Primary":definition.get('primary',''),"Secondary":definition.get('secondary',''),
                "NativeRocket":str(int(truth(definition.get('missilespawn')))),
                "CanPassiveAcquire":str(int(truth(definition.get('canpassiveaquire'),True))),
                "Immune":str(int(truth(definition.get('immune')))),
                "BridgeRepairHut":str(int(truth(definition.get('bridgerepairhut')))),
                "TooBigToFitUnderBridge":str(int(truth(definition.get('toobigtofitunderbridge'))))}
        gate=building and truth(definition.get('gate'))
        fields['Gate']=str(int(gate))
        if gate:
            fields.update(GateStages=visual.get('gatestages','9'),
                          GateOpenFrames=str(original_frames(definition.get('deploytime','0'))),
                          GateCloseFrames=str(original_frames(definition.get('gateclosedelay','0'))))
        if building:
            foundation=visual.get('foundation','1x1')
            match=re.fullmatch(r'(\d+)x(\d+)',foundation)
            if not match:raise ValueError((id,'nonrectangular foundation',foundation))
            w,h=map(int,match.groups());fields.update(Width=str(max(1,w)),Height=str(max(1,h)),Blocks=str(int(w>0 and h>0 and not truth(definition.get('ispassable')) and not truth(definition.get('invisibleingame')))))
            name,path,data=shp_source(image,visual);shp=Shp(data)
            prefix=visual.get('palette','unit');suffix={'URBAN':'urb','SNOW':'sno','TEMPERATE':'tem'}[native.theater]
            palname=prefix+suffix+'.pal';palpath,paldata=get(palname)
            if not paldata:raise FileNotFoundError(palname)
            palette=load_pal(paldata)
            remap=truth(visual.get('remapable'),not id.startswith('CA'))
            fields['Remappable']=str(int(remap))
            layers=[];idle_layers=[]
            for key in ('idleanim','activeanim','activeanimtwo','activeanimthree'):
                if not visual.get(key):continue
                state_layers=[]
                for damaged in range(2):
                    anim=visual.get(key+'damaged',visual[key]) if damaged else visual[key]
                    anim_definition=art.get(anim.lower(),{})
                    anim_image=anim_definition.get('image',anim)
                    anim_name,anim_path,anim_data=shp_source(anim_image,anim_definition)
                    state_layers.append((Shp(anim_data),int(anim_definition.get('loopstart','0'))))
                    layers.append({'state':damaged,'key':key,'shp':anim_name,'source':anim_path,
                                   'sha256':hashlib.sha256(anim_data).hexdigest(),'art':anim_definition})
                # This export supplies the authored idle pose. Loop timing is
                # retained in the report for the separate animation runtime.
                idle_layers.append(state_layers)
            if gate:
                fields['ArtFrames']=str(export_gate_frames(shp,palette,target,id.lower(),remap))
            for damaged in ([] if gate else range(2)):
                frame=damaged if shp.nframes>=4 else 0
                fr=shp.frame_pixels(frame);body=Image.new('RGBA',(shp.w,shp.h))
                shadow_index=shp.nframes//2+frame
                if shadow_index<shp.nframes:
                    shadow=shp.frame_pixels(shadow_index)
                    if shadow.w and shadow.h:body.alpha_composite(shp_frame_to_rgba(shadow,palette,remap=False),(shadow.x,shadow.y))
                if fr.w and fr.h:
                    body.alpha_composite(shp_frame_to_rgba(fr,palette,remap=False),(fr.x,fr.y))
                mask=Image.new('L',(shp.w,shp.h))
                if remap:
                    for py in range(fr.h):
                        for px in range(fr.w):
                            v=fr.pixels[py*fr.w+px]
                            if 16<=v<=31:mask.putpixel((fr.x+px,fr.y+py),max(palette[v]))
                for pair in idle_layers:
                    component,start=pair[damaged];part=component.frame_pixels(start)
                    dx=(shp.w-component.w)//2;dy=(shp.h-component.h)//2
                    if part.w and part.h:
                        body.alpha_composite(shp_frame_to_rgba(part,palette,remap=False),(dx+part.x,dy+part.y))
                        if remap:
                            for py in range(part.h):
                                for px in range(part.w):
                                    v=part.pixels[py*part.w+px];xx=dx+part.x+px;yy=dy+part.y+py
                                    if v and 0<=xx<shp.w and 0<=yy<shp.h:mask.putpixel((xx,yy),max(palette[v]) if 16<=v<=31 else 0)
                # Keep the source canvas and frame offsets, including base margins.
                body=body.resize((round(shp.w*16/15),round(shp.h*16/15)),Image.Resampling.NEAREST)
                body.save(target/'actors'/f'{id.lower()}_{damaged}.png')
                if remap:mask.resize(body.size,Image.Resampling.NEAREST).save(target/'actors'/f'{id.lower()}_{damaged}_remap.png')
            fields['OriginX']=str(round(shp.w*8/15));fields['OriginY']=str(round(shp.h*8/15))
            report['types'][id]={"shp":name,"source":path,"sha256":hashlib.sha256(data).hexdigest(),"palette":palname,
                                 "palette_source":palpath,"palette_sha256":hashlib.sha256(paldata).hexdigest(),"frames":shp.nframes,"foundation":[w,h],"raw_rules":definition,"raw_art":visual,'idle_layers':layers}
        elif fields['Base']=='NativeVehicle' or id in ('CMIN','DRED','V3','SAPC'):
            fields['NativeBody']='1'
            # Reuse the existing authored-voxel rasterizer with core RA2 files.
            raster=sys.modules.get('gen_assets')
            if raster is None and hasattr(sys.modules['__main__'],'render_voxel_unit'):raster=sys.modules['__main__']
            if raster is None:
                import gen_assets as raster
            old_get=raster.get
            old_palette=raster.PAL_U
            raster.get=lambda name:get(name)[1]
            try:
                palette=load_pal(core_file(tree,'unittem.pal')[1]);raster.PAL_U=palette
                directions,layout=raster.render_voxel_unit(image,(72,72),eng='native_vehicle')
                # Isolate the remap indices without erasing their original
                # brightness: the voxel rasterizer shades from the palette.
                raster.PAL_U=[color if 16<=i<=31 else (0,0,0) for i,color in enumerate(palette)]
                masks,_=raster.render_voxel_unit(image,(72,72),eng='native_vehicle')
                if id in ('DRED','V3'):
                    ready_per=raster._project_stem_dirs(image);empty_per=raster._project_stem_dirs(image+'wo')
                    if not ready_per or not empty_per:raise FileNotFoundError(f'original {id}/{id}WO voxel missing')
                    _,layout=raster._rasterize_per(raster._merge_per(ready_per,empty_per),(72,72),False,anchor_per=ready_per)
                    def render_variant(per,pal):
                        return [raster.render_pts(points[0],pal,layout['scale'],*layout['orgs'][i],layout['w'],layout['ch'],supersample=2)
                                for i,points in enumerate(per)]
                    remap_palette=raster.PAL_U
                    directions=render_variant(ready_per,palette);masks=render_variant(ready_per,remap_palette)
                    empty=render_variant(empty_per,palette);empty_masks=render_variant(empty_per,remap_palette)
            finally:raster.get=old_get;raster.PAL_U=old_palette
            if not directions:raise FileNotFoundError(f'original voxel missing: {image}')
            for direction,body in enumerate(directions):
                body.save(target/'actors'/f'{id.lower()}_d{direction}.png')
                marker=masks[direction];mask=Image.new('L',body.size)
                for py in range(body.height):
                    for px in range(body.width):
                        r,g,b,a=marker.getpixel((px,py))
                        if a and r and g==0 and b==0:mask.putpixel((px,py),r)
                mask.save(target/'actors'/f'{id.lower()}_d{direction}_remap.png')
            fields['Remappable']='1'
            fields['OriginX']=str(directions[0].width//2);fields['OriginY']=str(round(directions[0].height*.72))
            if id in ('DMISL','V3ROCKET'):
                from native_voxel_art import missile_poses
                fields.update(missile_poses(get,id,image,palette,target/'actors',raster.ENGINE_TILE_W/float(raster.RA2_TILE_W)))
            if id in ('DRED','V3'):
                for direction,body in enumerate(empty):
                    body.save(target/'actors'/f'{id.lower()}_d{direction+8}.png');marker=empty_masks[direction];mask=Image.new('L',body.size)
                    for py in range(body.height):
                        for px in range(body.width):
                            r,g,b,a=marker.getpixel((px,py))
                            if a and r and g==0 and b==0:mask.putpixel((px,py),r)
                    mask.save(target/'actors'/f'{id.lower()}_d{direction+8}_remap.png')
            fields['Width']=fields['Height']='1';fields['Blocks']='1'
            report['types'][id]={"image":image,"raw_rules":definition,
                                 "voxel_sources":{name:{"source":get(name)[0],"sha256":hashlib.sha256(get(name)[1]).hexdigest()}
                                                  for name in [image+'.vxl',image+'.hva']+([image+'wo.vxl',image+'wo.hva'] if id in ('DRED','V3') else []) if get(name)[1]}}
        else:
            fields['Width']=fields['Height']='1';fields['Blocks']='1'
            report['types'][id]={"image":image,"raw_rules":definition}
        if id=='SAPC':
            fields.update(TransportCapacity=definition['passengers'],PassengerSize=definition['size'],PassengerLimit=definition['sizelimit'])
        ini[f'Type.{index}']=fields
    i=0
    for section in ('structures','units','infantry'):
        for key,line in scenario[section].items():
            values=line.split(',');id=values[1].upper();infantry=section=='infantry'
            actor={"Key":section+':'+key,"Type":str(types.index(id)),"House":str(house_indices[values[0]]),
                   "X":values[3],"Y":values[4],"Health":values[2],"Facing":values[7 if infantry else 5],
                   "Tag":values[8 if infantry else 6 if section=='structures' else 7],
                   "Mission":'' if section=='structures' else values[6 if infantry else 6],"Original":line}
            ini[f'Actor.{i}']=actor;report['actors'].append(actor);i+=1
    authored_count=i
    scenery,objects,scenery_report=export_scenery(tree,native,rules,art,target)
    for fields,armor in scenery:
        fields['Armor']=str(ARMOR.index(armor));ini[f'Type.{len(types)}']=fields;types.append(fields['Id'])
    for actor in objects:
        actor['Type']=str(types.index(actor.pop('Id')))
        ini[f'Actor.{i}']=actor;report['actors'].append(actor);i+=1
    ini['Scene'].update(Version='16',Types=str(len(types)),Actors=str(i),AuthoredActors=str(authored_count),SceneryActors=str(len(objects)))
    export_native_production(ini,scenario,rules,art,target,get,tree)
    export_native_targets(ini,rules,scenario)
    export_native_events(ini,scenario,rules)
    export_native_ai(ini,scenario,rules)
    export_native_crates(ini,scenario,rules)
    export_native_rockets(ini,types,rules,scenario,art)
    export_native_weapons(ini,rules,scenario)
    export_native_movement(ini,scenario,rules)
    export_text(tree,ini,target)
    report['scenery']=scenery_report
    with (target/'scene.ini').open('w',encoding='utf-8',newline='\n') as stream:ini.write(stream)
    (target/'scene-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(f'NATIVE SCENE {filename}: {len(types)} types, {len(houses)} houses, {authored_count} placed actors + {len(objects)} scenery objects')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('map',nargs='?',default='all01t.map');args=parser.parse_args()
    export_scene(MixTree(),args.map)
