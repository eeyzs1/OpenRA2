"""Compare the complete native missile profile to the installed original files."""
import configparser
from decimal import Decimal
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/ra2pack'))
from gen_native_maps import ROOT,core_file
from map_format import read_ini
from ra2lib import MixTree
from PIL import Image

folder=ROOT/'assets/maps/native/all01t'
if not (folder/'scene.ini').exists():print('NATIVE ROCKET ASSETS SKIP: export the original scene');sys.exit(77)
tree=MixTree();rules=read_ini(core_file(tree,'rules.ini')[1]);scenario=read_ini(core_file(tree,'all01t.map')[1]);art=read_ini(core_file(tree,'art.ini')[1])
merged=lambda key:{**rules.get(key.lower(),{}),**scenario.get(key.lower(),{})}
p=configparser.ConfigParser(interpolation=None);p.read(folder/'scene.ini',encoding='utf-8')
assert p['Scene']['Version']=='16' and int(p['Scene']['Actors'])==738
assert p['NativeRockets']['Version']=='2' and int(p['NativeRockets']['Count'])==2
r=p['NativeRocket.0'];dred=merged('dred');air=merged('dmisl');launcher=merged(dred['primary']);general=merged('general')
assert p['Type.'+r['launcher']]['id']=='DRED' and p['Type.'+r['missile']]['id']=='DMISL'
for field,key in [('capacity','spawnsnumber'),('regenerate','spawnregenrate'),('reload','spawnreloadrate')]:assert int(r[field])==int(dred[key])
assert int(r['launchdelay'])==int(art['dmisl']['spawndelay']) and r['flh']==art['dred']['primaryfireflh']
for field,key in [('rof','rof'),('range','range'),('minimumrange','minimumrange')]:assert Decimal(r[field])==Decimal(launcher[key])
for field,key in [('pause','pauseframes'),('tilt','tiltframes'),('altitude','altitude'),('damage','damage'),('elitedamage','elitedamage'),
                  ('bodylength','bodylength'),('pitchinitial','pitchinitial'),('pitchfinal','pitchfinal'),('turnrate','turnrate'),('acceleration','acceleration')]:
    assert Decimal(r[field])==Decimal(general['dmisl'+key]),field
assert Decimal(r['maxspeed'])==Decimal(air['speed']) and Decimal(r['yawrate'])==Decimal(air['rot'])
for name,id in [('Normal','dmislwh'),('Elite','dmislewh')]:
    row=p['NativeRocket.0.'+name];source=merged(id)
    for field in ('cellspread','percentatmax'):assert Decimal(row[field])==Decimal(source[field])
    assert row['verses']==source['verses']
assert p['NativeRocket.0.Normal']['verses'].split(',')[9]=='2%'
missile=p['Type.'+r['missile']];assert int(rules['dmisl']['strength'])==50
assert int(missile['hp'])==int(air['strength'])==75 and int(missile['armor'])==10
assert missile['nativerocket']=='1' and missile['remappable']=='0' and int(missile['artframes'])==136
poses=[Image.open(folder/'actors'/f'dmisl_d{frame}.png') for frame in range(136)]
assert len({image.size for image in poses})==1
for image in poses:
    bounds=image.getbbox();assert bounds and bounds[0]>0 and bounds[1]>0 and bounds[2]<image.width and bounds[3]<image.height
assert int(missile['originx'])==poses[0].width//2 and int(missile['originy'])==poses[0].height//2+4
assert Image.open(folder/'actors/dmisl_d0.png').tobytes()!=Image.open(folder/'actors/dmisl_d64.png').tobytes()
for direction in range(8):
    ready=Image.open(folder/'actors'/f'dred_d{direction}.png');empty=Image.open(folder/'actors'/f'dred_d{direction+8}.png')
    assert ready.getbbox() and empty.getbbox() and ready.size==empty.size and ready.tobytes()!=empty.tobytes()
v=p['NativeRocket.1'];source=merged('v3');air=merged('v3rocket');gun=merged(source['primary'])
assert p['Type.'+v['launcher']]['id']=='V3' and p['Type.'+v['missile']]['id']=='V3ROCKET'
for field,key in [('capacity','spawnsnumber'),('regenerate','spawnregenrate'),('reload','spawnreloadrate')]:assert int(v[field])==int(source[key])
for field,key in [('rof','rof'),('range','range'),('minimumrange','minimumrange')]:assert Decimal(v[field])==Decimal(gun[key])
for field,key in [('pause','pauseframes'),('tilt','tiltframes'),('altitude','altitude'),('damage','damage'),('elitedamage','elitedamage'),
                  ('bodylength','bodylength'),('pitchinitial','pitchinitial'),('pitchfinal','pitchfinal'),('turnrate','turnrate'),('acceleration','acceleration')]:
    assert Decimal(v[field])==Decimal(general['v3rocket'+key]),field
assert v['flh']==art['v3']['primaryfireflh']=='-160,0,75' and int(v['launchdelay'])==int(art['v3rocket']['spawndelay'])
assert Decimal(v['maxspeed'])==Decimal(air['speed']) and Decimal(v['yawrate'])==Decimal(air['rot'])
for rocket,prefix,launcher in [(r,'dmisl','dred'),(v,'v3rocket','v3')]:
    for field,key in [('lazycurve',prefix+'lazycurve')]:assert int(rocket[field])==int(general[key].lower()=='yes')
    assert int(rocket['nospawnalt'])==int(merged(launcher)['nospawnalt'].lower()=='yes')
for label,id in [('Normal','V3WH'),('Elite','V3EWH')]:
    row=p['NativeRocket.1.'+label];source=merged(id);assert row['id']==id
    for field in ('cellspread','percentatmax'):assert Decimal(row[field])==Decimal(source[field])
    assert row['verses']==source['verses']
assert p['NativeRocket.1.Normal']['verses'].split(',')[9]=='50000%'
missile=p['Type.'+v['missile']]
assert int(missile['hp'])==int(air['strength'])==50 and int(missile['armor'])==10
assert missile['nativerocket']=='1' and missile['remappable']=='0' and int(missile['artframes'])==136
poses=[Image.open(folder/'actors'/f'v3rocket_d{frame}.png') for frame in range(136)]
assert len({image.size for image in poses})==1
for image in poses:
    bounds=image.getbbox();assert bounds and bounds[0]>0 and bounds[1]>0 and bounds[2]<image.width and bounds[3]<image.height
assert int(missile['originx'])==poses[0].width//2 and int(missile['originy'])==poses[0].height//2+4
assert poses[0].tobytes()!=poses[64].tobytes()
for direction in range(8):
    ready=Image.open(folder/'actors'/f'v3_d{direction}.png');empty=Image.open(folder/'actors'/f'v3_d{direction+8}.png')
    assert ready.getbbox() and empty.getbbox() and ready.size==empty.size and ready.tobytes()!=empty.tobytes()
sam=next(p[n] for n in p.sections() if n.startswith('Type.') and p[n]['id']=='NASAM')
source=merged(merged('NASAM')['primary']);wh=merged(source['warhead'])
for field,key in [('weapondamage','damage'),('weaponrange','range'),('weaponrof','rof'),('weaponspeed','speed')]:assert Decimal(sam[field])==Decimal(source[key])
assert sam['weaponverses']==wh['verses'] and sam['weaponprojectile']==source['projectile']
print('NATIVE ROCKET ASSETS PASS: original DRED/DMISL and V3/V3ROCKET slots, phase/flight rules, exact map warheads, 272 missile poses, empty bodies and Patriot weapon')
