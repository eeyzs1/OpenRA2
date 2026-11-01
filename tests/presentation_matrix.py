"""Independent process replay with real presentation-only content overrides."""
import argparse
import hashlib
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--exe", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
exe = args.exe.resolve()

def replay(audio, mod=None):
    command = [str(exe), "--presentation-replay", str(audio)]
    if mod:
        command += ["--mod", str(mod)]
    run = subprocess.run(command, cwd=root, capture_output=True, timeout=60)
    output = (run.stdout + run.stderr).decode("utf-8", errors="replace").replace("\r\n", "\n")
    if run.returncode:
        raise AssertionError(f"Replay exited {run.returncode}: {output[-3000:]}")
    traces = re.findall(r"^TRACE (\d+) ([0-9a-f]{8})$", output, re.M)
    header = re.search(r"PRESENTATION walk=(\d+) fire=(\d+) mk=(\d+) sprite_hash=([0-9a-f]+) audio=(\d+)", output)
    if len(traces) != 600 or not header:
        raise AssertionError(f"Incomplete replay: {output[-3000:]}")
    deployed = re.search(r"PRESENTATION_DEP depfire=(\d+)",output)
    cameo = re.search(r"PRESENTATION_CAMEO type99=([0-9a-f]{8})",output)
    if not cameo:
        raise AssertionError("Cameo did not load from the real sprite bank")
    if not deployed:
        raise AssertionError("Deployed animation metadata missing")
    if "PRESENTATION_ENTRY board=1 garrison=1" not in output:
        raise AssertionError("Boarding/garrison replay did not complete its entry actions")
    if "PRESENTATION_RELEASE unload=1 evacuate=1" not in output:
        raise AssertionError("Unload/evacuation replay did not complete its release actions")
    if "PRESENTATION_SERVICE completed=1" not in output:
        raise AssertionError("Service replay did not complete actual repair")
    magnetic=re.search(r"PRESENTATION_MAGNETRON damage=(\d+) beam=1",output)
    if not magnetic or int(magnetic.group(1))<=0:
        raise AssertionError("Magnetron replay did not damage a building with its secondary beam")
    warp=re.search(r"PRESENTATION_WARP frames=(\d+) hash=([0-9a-f]+) emitted=1",output)
    if not warp: raise AssertionError("Real chrono jump or sprite missing from replay")
    siege=re.search(r"PRESENTATION_SIEGE hash=([0-9a-f]{8}) damage=(\d+) isolated=1",output)
    if not siege or int(siege.group(2))<=0:raise AssertionError("Deployed siege model or actual isolated artillery damage missing")
    voxel=re.search(r"PRESENTATION_VOXEL body_frames=(\d+) turret_frames=(\d+) rotor=([0-9a-f]{8}) barrel=([0-9a-f]{8}) fired=1 isolated=1",output)
    if not voxel:raise AssertionError("Actual voxel animation replay or isolated fire missing")
    transform=re.search(r"PRESENTATION_TRANSFORM frames=(\d+) hash=([0-9a-f]{8}) seen=1 isolated=1",output)
    if not transform:raise AssertionError("Actual chopper unfolding/folding replay missing")
    apoc = re.findall(r"PRESENTATION_APOC mode=(\d) shots=(\d+) projectiles=(\d+) direct=(\d+) edge=(\d+) cadence=1 physical=1 isolated=1", output)
    if len(apoc)!=3 or [tuple(map(int,row[:3])) for row in apoc] != [(0,2,2),(1,4,4),(2,2,2)]:
        raise AssertionError("Real ordinary/elite/anti-air Apocalypse volleys missing")
    if any(not 0<int(row[4])<int(row[3]) for row in apoc) or apoc[0][3:]!=('50','36'):
        raise AssertionError("Actual Apocalypse splash falloff missing")
    apoc_art=re.search(r"PRESENTATION_APOC_ART frames=(\d+) hash=([0-9a-f]{8})",output)
    if not apoc_art:raise AssertionError("Apocalypse turret pixels/frame count missing")
    print(f"APOC audio={audio} override={mod is not None}: {apoc}; art={apoc_art.groups()}")
    trace_hash=hashlib.sha256('\n'.join(f'{tick} {checksum}' for tick,checksum in traces).encode('ascii')).hexdigest()
    print(f"TRACE_SHA256 audio={audio} override={mod is not None}: {trace_hash}")
    return traces, header.groups()+(cameo.group(1),deployed.group(1),warp.group(1),warp.group(2),siege.group(1))+voxel.groups()+transform.groups()+apoc_art.groups()

baseline, base_art = replay(0)
audio, audio_art = replay(1)
assert baseline == audio, "Sound playback changed a simulation tick"
assert base_art[5] == audio_art[5], "Sound playback selected a different cameo"
with tempfile.TemporaryDirectory(prefix="openra2-presentation-") as temp:
    mod = Path(temp).resolve()
    sprites = mod / "assets" / "sprites"
    sprites.mkdir(parents=True)
    (mod / "mod.ini").write_text("[Mod]\nName=Presentation Regression\nLoadOrder=30000\nEnabled=yes\n", encoding="utf-8")
    (sprites / "anims.ini").write_text("[gi]\nwalk=31\nfire=2\nfirerate=1\ndepfire=31\ndie=1\n[bld_barracks]\nmk=2\n", encoding="utf-8")
    shutil.copyfile(root / "assets/sprites/unit_conscript_d0_f0.png", sprites / "unit_gi_d0_f0.png")
    shutil.copyfile(root / "assets/sprites/icon_unit_rhino.png", sprites / "icon_unit_type99.png")
    # A declared sequence must have real frames; repeat native art for this cadence override.
    for direction in range(8):
        for phase in range(31):
            shutil.copyfile(root / f"assets/sprites/unit_gi_walk_d{direction}_f{phase % 6}.png", sprites / f"unit_gi_walk_d{direction}_f{phase}.png")
            shutil.copyfile(root / f"assets/sprites/unit_gi_depfire_d{direction}_f{phase % 6}.png", sprites / f"unit_gi_depfire_d{direction}_f{phase}.png")
    (mod / "assets/chrono_fx.ini").write_text("[Arrival]\nFrames=3\n[Departure]\nFrames=21\n[Erase]\nFrames=23\n",encoding="utf-8")
    for frame in range(3):
        shutil.copyfile(root / f"assets/sprites/fx_warpout_{frame+5}.png",sprites / f"fx_warpin_{frame}.png")
    voxels=mod/"assets"/"voxels";voxels.mkdir()
    transform=bytearray((root/"assets/voxels/schdtur.hva").read_bytes())
    frames,sections=struct.unpack_from("<II",transform,16)
    assert frames==1 and sections==1, "Unexpected source turret transform layout"
    translation=24+sections*16+3*4
    struct.pack_into("<f",transform,translation,struct.unpack_from("<f",transform,translation)[0]+3.0)
    (voxels/"schdtur.hva").write_bytes(transform)
    # Expand the real Apocalypse turret from one native HVA frame to two,
    # moving the second transform so the sampled pixels must actually differ.
    raw=(root/'assets/voxels/mtnktur.hva').read_bytes()
    frames,sections=struct.unpack_from('<II',raw,16)
    assert (frames,sections)==(1,1), 'Unexpected Apocalypse turret HVA layout'
    start=24+sections*16;header=bytearray(raw[:start]);struct.pack_into('<I',header,16,2)
    changed=bytearray(raw[start:]);struct.pack_into('<f',changed,3*4,struct.unpack_from('<f',changed,3*4)[0]+12.0)
    (voxels/'mtnktur.hva').write_bytes(header+raw[start:]+changed)
    transform_meta=(root/'assets/chopper_deploy.ini').read_text().replace('Frames=11','Frames=3')
    assert 'Frames=3' in transform_meta
    (mod/'assets/chopper_deploy.ini').write_text(transform_meta)
    for phase,source in enumerate([0,5,10]):
        for suffix in ('','_remap'):
            shutil.copyfile(root/f'assets/sprites/unit_siegechopper_transform_f{source}{suffix}.png',sprites/f'unit_siegechopper_transform_f{phase}{suffix}.png')
    for stem in ("shad","schp","ytnktur"):
        raw=(root/f"assets/voxels/{stem}.hva").read_bytes()
        frames,sections=struct.unpack_from("<II",raw,16)
        assert frames==2 and sections==3, "Unexpected native multi-section animation"
        start=24+sections*16;stride=sections*48
        header=bytearray(raw[:start]);struct.pack_into("<I",header,16,3)
        changed=bytearray(raw[start:start+stride])
        struct.pack_into("<f",changed,3*4,struct.unpack_from("<f",changed,3*4)[0]+40.0)
        (voxels/f"{stem}.hva").write_bytes(header+raw[start:start+stride]+changed+raw[start+stride:start+2*stride])
    replacement, changed_art = replay(1, mod)
    assert changed_art[:3] == ("31", "2", "2"), f"Metadata override did not load: {changed_art}"
    assert changed_art[3] != base_art[3], "The real sprite override did not load"
    assert changed_art[5] != base_art[5], "The real cameo override did not load"
    assert changed_art[6] == "31" and base_art[6] == "6", "Deployed animation override did not load"
    assert base_art[7]=="10" and changed_art[7]=="3" and base_art[8]!=changed_art[8], "Chrono sequence replacement did not load"
    assert base_art[9]!=changed_art[9], "Real siege turret transform replacement did not load"
    assert base_art[10:12]==("2","2") and changed_art[10:12]==("3","3"), "HVA body/turret frame-count override did not load"
    assert base_art[12]!=changed_art[12] and base_art[13]!=changed_art[13], "Animated rotor/barrel transforms did not change actual sprite pixels"
    assert base_art[14]=='11' and changed_art[14]=='3' and base_art[15]!=changed_art[15], "Actual chopper unfolding sequence replacement did not load"
    assert base_art[16]=='1' and changed_art[16]=='2' and base_art[17]!=changed_art[17], 'Actual Apocalypse turret frame count/pixels did not change'
    assert baseline == replacement, "Presentation replacement changed a simulation tick"
    (voxels/"schd.vxl").write_bytes(b"invalid deployed voxel fixture")
    invalid=subprocess.run([str(exe),"--presentation-replay","0","--mod",str(mod)],cwd=root,capture_output=True,timeout=60)
    invalid_output=(invalid.stdout+invalid.stderr).decode("utf-8",errors="replace")
    assert invalid.returncode==1 and "deployed unit" in invalid_output and "ERROR: AddressSanitizer" not in invalid_output, "Corrupt deployed hull was not rejected with a controlled exit"
    (voxels/"schd.vxl").unlink()
    valid=(voxels/"shad.hva").read_bytes()
    for malformed in (b"invalid HVA fixture", valid[:40], valid[:72]+struct.pack("<f",float("nan"))+valid[76:]):
        (voxels/"shad.hva").write_bytes(malformed)
        invalid=subprocess.run([str(exe),"--presentation-replay","0","--mod",str(mod)],cwd=root,capture_output=True,timeout=60)
        output=(invalid.stdout+invalid.stderr).decode("utf-8",errors="replace")
        assert invalid.returncode==1 and "voxel animation" in output and "ERROR: AddressSanitizer" not in output, "Malformed HVA was not rejected with a controlled exit"
    (voxels/'shad.hva').write_bytes(valid)
    valid_meta=(mod/'assets/chopper_deploy.ini').read_text()
    for malformed in (valid_meta.replace('Frames=3','Frames=0'),valid_meta.replace('Facing=1','Facing=8'),valid_meta.replace('OriginX=109','OriginX=999')):
        (mod/'assets/chopper_deploy.ini').write_text(malformed)
        invalid=subprocess.run([str(exe),'--presentation-replay','0','--mod',str(mod)],cwd=root,capture_output=True,timeout=60)
        output=(invalid.stdout+invalid.stderr).decode('utf-8',errors='replace')
        assert invalid.returncode==1 and 'chopper-transform-metadata' in output and 'ERROR: AddressSanitizer' not in output,'Invalid chopper sequence metadata accepted or crashed'
    (mod/'assets/chopper_deploy.ini').write_text(valid_meta)
    (sprites/'unit_siegechopper_transform_f1_remap.png').unlink()
    invalid=subprocess.run([str(exe),'--presentation-replay','0','--mod',str(mod)],cwd=root,capture_output=True,timeout=60)
    output=(invalid.stdout+invalid.stderr).decode('utf-8',errors='replace')
    assert invalid.returncode==1 and 'chopper-transform' in output and 'ERROR: AddressSanitizer' not in output,'A mod sequence without its own house mask silently inherited the base mask'

print("PRESENTATION MATRIX PASS: 600 ticks identical with sound off/on and sprite/cameo/deployment/Apocalypse SHP/HVA frame-count replacement; actual ordinary/elite/AA volleys and splash; invalid metadata/HVA/house masks rejected")
print(f"Base metadata/hash: {base_art}; replacement: {changed_art}")
