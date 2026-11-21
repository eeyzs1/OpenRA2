"""Compare exported swimming pixels with original SHP sequences and offsets."""
import configparser
from pathlib import Path
import sys

from PIL import Image

root=Path(__file__).resolve().parents[1]
if not (root/'assets/maps/native/all01t/scene.ini').exists():
    print('NATIVE MOTION ASSETS SKIP: export the original scene');sys.exit(77)
sys.path.insert(0,str(root/'tools/ra2pack'))
from map_format import read_ini
from ra2lib import MixTree,Shp,load_pal,shp_frame_to_rgba
from infantry_sequence import parse_sequence,sequence_frame,sequence_fits

tree=MixTree()
_,raw=tree.find('artmd.ini')
if not raw:_,raw=tree.find('art.ini')
art=read_ini(raw)
_,palette_raw=tree.find('unittem.pal');palette=load_pal(palette_raw)
metadata=configparser.ConfigParser();metadata.read(root/'assets/sprites/anims.ini',encoding='utf-8')
for unit,image,sequence in [('tanya','tany','tanyasequence'),('navyseal','seal','sealsequence')]:
    _,data=tree.find(image+'.shp');shp=Shp(data);info=metadata[unit]
    ready=shp.frame_pixels(0);reference=shp_frame_to_rgba(ready,palette,remap=True).getbbox()
    canvas=Image.open(root/'assets/sprites'/f'unit_{unit}_d0_f0.png').size
    scale=min(canvas[0]/(reference[2]-reference[0]),(canvas[1]-4)/(reference[3]-reference[1]),2)
    size=(round(shp.w*scale),round(shp.h*scale))
    assert (int(info['waterox']),int(info['wateroy']))==(size[0]//2,size[1]//2)
    for field,key in [('swim','swim'),('tread','tread'),('wetfire','wetattack')]:
        part=parse_sequence(art[sequence][key]);assert sequence_fits(part,shp.nframes) and int(info[field])==part[1]==6
        variants=set()
        for direction in range(8):
            for phase in range(part[1]):
                index=sequence_frame(part,(direction+6)%8,phase);frame=shp.frame_pixels(index)
                expected=Image.new('RGBA',(shp.w,shp.h),(0,0,0,0));expected.alpha_composite(shp_frame_to_rgba(frame,palette,remap=True),(frame.x,frame.y))
                expected=expected.resize(size,Image.Resampling.NEAREST)
                with Image.open(root/'assets/sprites'/f'unit_{unit}_{field}_d{direction}_f{phase}.png') as actual:
                    assert actual.size==size and actual.tobytes()==expected.tobytes(),(unit,key,direction,phase,index,'original pixels/offsets changed')
                    variants.add(actual.tobytes())
        assert len(variants)>20,(unit,key,'frozen or duplicate directions')
    print('NATIVE MOTION ASSETS PASS',unit,'144 source-exact water frames, shared SHP transform and anchor')
