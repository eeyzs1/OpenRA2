"""Exercise the real VXL exporter with independent compass/color probes and native pose origins."""
import argparse,csv,json,math,struct,subprocess,sys,tempfile
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools/asset_pipeline'))
from formats import write_vxl,write_hva_static
from ra2lib import facing_world_phi
parser=argparse.ArgumentParser();parser.add_argument('--exe',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
vectors=[(1,-1),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1),(0,-1)]
palette=bytearray((root/'assets/palettes/unittem.pal').read_bytes())
for index,color in [(40,(0,0,63)),(41,(0,63,0)),(42,(40,40,40))]:palette[index*3:index*3+3]=bytes(color)
pal=[tuple(v*4 for v in palette[i*3:i*3+3]) for i in range(256)]
def section(colors):
    return {'name':'probe','size':(40,4,4),'mins':(-20,-2,0),'maxs':(20,2,4),
        'voxels':[(x,y,z,colors[0] if x<8 else colors[1] if x>=32 else 42,0) for x in range(40) for y in range(4) for z in range(4)]}
def run(output,mod=None):
    output.mkdir(parents=True,exist_ok=True)
    command=[str(args.exe.resolve()),'--dump-vxl','--all-facings','--dump-dir',str(output.resolve())]
    if mod:command+=['--mod',str(mod)]
    process=subprocess.run(command,cwd=root,capture_output=True,timeout=90)
    (output/'console.log').write_bytes(process.stdout+process.stderr)
    assert process.returncode==0,f'VXL exporter failed {process.returncode}: {process.stderr[-1000:]}'
records=[]
with tempfile.TemporaryDirectory(prefix='openra2-voxel-probe-') as folder:
    mod=Path(folder);voxels=mod/'assets/voxels';voxels.mkdir(parents=True);(mod/'assets/palettes').mkdir()
    (mod/'mod.ini').write_text('[Mod]\nName=Independent compass and palette probe\nLoadOrder=30000\nEnabled=yes\n')
    (mod/'assets/palettes/unittem.pal').write_bytes(palette)
    # Identity VPL isolates palette semantics from native lighting lookup tables.
    (voxels/'voxels.vpl').write_bytes(struct.pack('<IIII',0,0,1,0)+palette+bytes(range(256)))
    for stem,colors in [('htnk',(40,41)),('htnktur',(16,31)),('htnkbarl',(42,42))]:
        geometry=section(colors) if stem!='htnkbarl' else {'name':'probe','size':(1,1,1),'mins':(-.5,-.5,0),'maxs':(.5,.5,1),'voxels':[(0,0,0,42,0)]}
        (voxels/(stem+'.vxl')).write_bytes(write_vxl([geometry],pal))
        (voxels/(stem+'.hva')).write_bytes(write_hva_static(1,1,['probe']))
    output=args.output/'synthetic';run(output,mod)
    for direction,(dx,dy) in enumerate(vectors):
        expected=(dx-dy,(dx+dy)/2);length=math.hypot(*expected);axis=(expected[0]/length,expected[1]/length)
        phi=math.radians(facing_world_phi(direction));projected=(math.cos(phi)-math.sin(phi),(math.cos(phi)+math.sin(phi))/2)
        assert abs(projected[0]*expected[1]-projected[1]*expected[0])<1e-6,'offline exporter facing differs from movement compass'
        body=Image.open(output/f'native_rhino_body_d{direction}.png').convert('RGBA')
        groups=[]
        for channel in (2,1):
            points=[(x,y) for y in range(body.height) for x in range(body.width) if (lambda c:c[3]>200 and c[channel]>80 and c[3-channel]<8)(body.getpixel((x,y)))]
            assert len(points)>10,'synthetic colored endpoint missing'
            groups.append((sum(x for x,y in points)/len(points),sum(y for x,y in points)/len(points)))
        facing=(groups[1][0]-groups[0][0],groups[1][1]-groups[0][1]);length=math.hypot(*facing)
        error=abs(facing[0]*axis[1]-facing[1]*axis[0])/length;forward=facing[0]*axis[0]+facing[1]*axis[1]
        turret=Image.open(output/f'native_rhino_turret_d{direction}.png').convert('RGBA')
        red=[(x*axis[0]+y*axis[1],c[0]) for y in range(turret.height) for x in range(turret.width) if (c:=turret.getpixel((x,y)))[3]>200 and c[0]>0 and c[1]<2 and c[2]<2]
        red.sort();assert len(red)>20,'native house shade endpoints missing'
        count=len(red)//3;rear=sum(c for _,c in red[:count])/count;front=sum(c for _,c in red[-count:])/count
        ok=error<0.10 and forward>0 and rear>140 and front<40 and rear>front*4
        records.append({'direction':direction,'angular_sine_error':error,'forward':forward,'bright_index16':rear,'dark_index31':front,'pass':ok})
        print(f'VOXEL_PROJECTION {"PASS" if ok else "FAIL"} direction={direction} error={error:.4f} index16={rear:.1f} index31={front:.1f}')
native=args.output/'native';run(native)
anchors=list(csv.DictReader((native/'native_siege_anchors.csv').open()))
for row in anchors:
    direction=int(row['direction']);frame=int(row['frame']);pose=row['pose'];x,y=int(row['x']),int(row['y'])
    file=native/(f'native_siege_flight_d{direction}_f{frame}.png' if pose=='flight' else f'native_siege_ground_d{direction}.png')
    image=Image.open(file).convert('RGBA');assert 0<=x<image.width and 0<=y<image.height,'world origin outside model canvas'
    if pose=='flight' and frame==1:
        first=next(r for r in anchors if r['pose']=='flight' and int(r['direction'])==direction and r['frame']=='0')
        assert (x,y)==(int(first['x']),int(first['y'])),'flight HVA changed physical origin'
    if direction==1 and frame==0:
        # Original SCHPDEPL SHP, sha256 aaf691e3...2250a; full canvas 154x118.
        # First/last body foot coordinates relative to canvas center, scaled by sqrt(2).
        reference=(11.2*math.sqrt(2),7*math.sqrt(2)) if pose=='flight' else (25.5*math.sqrt(2),13*math.sqrt(2))
        points=[(px,py) for py in range(image.height) for px in range(image.width) if image.getpixel((px,py))[3]>200]
        bottom=max(py for px,py in points);feet=[px for px,py in points if py>=bottom-1]
        foot=(sum(feet)/len(feet)-x,bottom-y);distance=math.hypot(foot[0]-reference[0],foot[1]-reference[1])
        ok=distance<4;records.append({'pose':pose,'foot':foot,'native_shp_foot':reference,'distance':distance,'pass':ok})
        print(f'VOXEL_ORIGIN {"PASS" if ok else "FAIL"} pose={pose} foot={foot} reference={reference} distance={distance:.3f}')
(args.output/'results.json').write_text(json.dumps(records,indent=2))
assert all(row['pass'] for row in records),'VXL facing, palette or native animation origin mismatch'
