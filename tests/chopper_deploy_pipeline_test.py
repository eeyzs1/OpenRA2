"""SHP offsets, paired shadows, palette-index masks and failed-source preservation."""
from pathlib import Path
from PIL import Image
import sys,tempfile,struct
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ra2pack'))
from gen_chopper_deploy import export_chopper

def source_shp():
    payloads=[bytes([16,31]),bytes([2,0]),bytes([1,1]),bytes([1,0])]
    frames=[];offset=8+24*4
    for index,payload in enumerate(payloads):
        x,y=(1,1) if index<2 else (0,2)
        frames.append(struct.pack('<HHHHIIII',x,y,2,1,0,0,0,offset));offset+=len(payload)
    return struct.pack('<HHHH',0,4,3,4)+b''.join(frames)+b''.join(payloads)
class Sources:
    def __init__(self):
        palette=bytearray(768);palette[16*3:16*3+3]=bytes([0,63,0]);palette[31*3:31*3+3]=bytes([0,8,0]);palette[6:9]=bytes([3,4,5])
        self.files={'schpdepl.shp':source_shp(),'unittem.pal':bytes(palette),'rulesmd.ini':b'[AudioVisual]\r\r\nDeployDir=2 ; east\r\r\n'}
    def find(self,name):return 'synthetic.mix',self.files.get(name)
with tempfile.TemporaryDirectory(prefix='openra2-chopper-extract-') as directory:
    root=Path(directory);sources=Sources();metadata=export_chopper(sources,root)
    assert metadata['frames']==2 and metadata['shadow_offset']==2 and metadata['engine_facing']==1
    image=Image.open(root/'sprites/unit_siegechopper_transform_f0.png').convert('RGBA');mask=Image.open(root/'sprites/unit_siegechopper_transform_f0_remap.png').convert('RGBA')
    assert image.size==(6,4) and metadata['origin']==[3,2]
    assert image.getpixel((2,1))==(0,252,0,255) and mask.getpixel((2,1))==(252,252,252,255)
    assert image.getpixel((3,1))==(0,32,0,255) and mask.getpixel((3,1))==(32,32,32,255)
    assert image.getpixel((0,3))==(0,0,0,120) and mask.getpixel((0,3))[3]==0,'paired shadow became body/remap'
    assert Image.open(root/'sprites/unit_siegechopper_transform_f1.png').getpixel((2,1))==(12,16,20,255)
    before={str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*') if p.is_file()};export_chopper(sources,root)
    assert before=={str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*') if p.is_file()},'repeated extraction changed output'
    for name,bad in [('schpdepl.shp',None),('schpdepl.shp',b'bad'),('schpdepl.shp',source_shp()[:-1]),('unittem.pal',b'bad'),('rulesmd.ini',b'[AudioVisual]\nDeployDir=12\n')]:
        original=sources.files[name];sources.files[name]=bad
        try:export_chopper(sources,root)
        except (ValueError,IndexError,struct.error):pass
        else:raise AssertionError('invalid deployment source accepted')
        sources.files[name]=original
        assert before=={str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*') if p.is_file()},'source failure damaged published animation'
print('CHOPPER PIPELINE PASS: original canvas, paired shadows, index masks, world-facing metadata, deterministic export, failed-source preservation')
