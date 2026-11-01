"""Extract the original one-view SCHPDEPL body/shadow sequence in a shared world-origin canvas."""
import argparse,hashlib,json,math,os,re
from pathlib import Path
from PIL import Image
from ra2lib import MixTree,Shp,load_pal

def export_chopper(tree,assets:Path):
    source,data=tree.find('schpdepl.shp');palette_source,raw_palette=tree.find('unittem.pal');rules_source,rules=tree.find('rulesmd.ini')
    if not data or not raw_palette or len(raw_palette)!=768 or not rules:raise ValueError('missing chopper deployment source, palette or rules')
    section=re.search(r'^\[AudioVisual\][^\n]*\n(.*?)(?=^\[|\Z)',rules.decode('latin1').replace('\r',''),re.M|re.S|re.I)
    facing=re.search(r'^DeployDir\s*=\s*([0-7])\s*(?:;[^\r\n]*)?$',section.group(1),re.M|re.I) if section else None
    if not facing:raise ValueError('missing original simple-deployer direction')
    original_facing=int(facing.group(1));engine_facing=(original_facing+7)&7
    shp=Shp(data)
    if not 2<=shp.nframes<=512 or shp.nframes%2 or not 1<=shp.w<=256 or not 1<=shp.h<=256:raise ValueError('unsupported SCHPDEPL canvas/body-shadow pairing')
    count=shp.nframes//2;palette=load_pal(raw_palette);scale=math.sqrt(2)
    size=(round(shp.w*scale),round(shp.h*scale));origin=(round(shp.w/2*scale),round(shp.h/2*scale));prepared=[]
    for phase in range(count):
        image=Image.new('RGBA',(shp.w,shp.h));mask=Image.new('RGBA',image.size)
        for index,shadow in [(phase+count,True),(phase,False)]:
            frame=shp.frame_pixels(index)
            if frame.x+frame.w>shp.w or frame.y+frame.h>shp.h or len(frame.pixels)!=frame.w*frame.h:raise ValueError('invalid deployment frame bounds')
            layer=Image.new('RGBA',image.size);pixels=layer.load();colors=mask.load()
            for y in range(frame.h):
                for x in range(frame.w):
                    value=frame.pixels[y*frame.w+x]
                    if not value:continue
                    px,py=frame.x+x,frame.y+y
                    pixels[px,py]=(0,0,0,120) if shadow or value==1 else (*palette[value],255)
                    if not shadow and 16<=value<=31:
                        shade=max(palette[value]);colors[px,py]=(shade,shade,shade,255)
            image.alpha_composite(layer)
        prepared.append((image.resize(size,Image.Resampling.NEAREST),mask.resize(size,Image.Resampling.NEAREST)))
    manifest={'source':source,'sha256':hashlib.sha256(data).hexdigest(),'palette_source':palette_source,'palette_sha256':hashlib.sha256(raw_palette).hexdigest(),
        'rules_source':rules_source,'rules_sha256':hashlib.sha256(rules).hexdigest(),'original_deploy_dir':original_facing,'engine_facing':engine_facing,
        'source_canvas':[shp.w,shp.h],'frames':count,'shadow_offset':count,'scale':scale,'canvas':list(size),'origin':list(origin)}
    # Decode and validate the complete source before replacing existing published frames.
    sprites=assets/'sprites';sprites.mkdir(parents=True,exist_ok=True)
    for phase,(image,mask) in enumerate(prepared):
        for suffix,pixels in [('',image),('_remap',mask)]:
            target=sprites/f'unit_siegechopper_transform_f{phase}{suffix}.png';temporary=target.with_suffix('.png.tmp');pixels.save(temporary,format='PNG');os.replace(temporary,target)
    (assets/'chopper_deploy.ini').write_text(f'; Presentation-only original SCHPDEPL sequence; simulation timing is in rules.\n[SiegeChopper]\nFrames={count}\nWidth={size[0]}\nHeight={size[1]}\nOriginX={origin[0]}\nOriginY={origin[1]}\nFacing={engine_facing}\n',encoding='utf-8')
    (assets/'chopper_deploy_sources.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8');return manifest

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[2]/'assets');args=parser.parse_args()
    print(json.dumps(export_chopper(MixTree(),args.output),indent=2))
