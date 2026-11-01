"""Observe game-rendered poses and reject incomplete/corrupt declared sequences."""
import argparse
import re
import subprocess
import tempfile
from pathlib import Path
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument("--exe",type=Path,required=True)
parser.add_argument("--output",type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
exe=args.exe.resolve()
output=args.output.resolve()
run=subprocess.run([str(exe),"--animation-regression",str(output)],cwd=root,capture_output=True,timeout=90)
text=(run.stdout+run.stderr).decode("utf-8",errors="replace")
if run.returncode:
    raise AssertionError(text[-6000:])
for unit in ["gi","conscript","pla","initiate","attackdog"]:
    if not re.search(rf"ANIMATION PASS unit={unit} walk=6 fire=6 death=15 variants=\d+,\d+,\d+ continuous=1 isolated=1",text):
        raise AssertionError(f"Missing actual animation coverage for {unit}")
if "ANIMATION_TIMING blocked=1 saved=1 continued=1" not in text:
    raise AssertionError("Motion clock advances while blocked or is lost in save restoration")
for unit,count in [("gi",6),("guardiangi",6),("desolator",7)]:
    if not re.search(rf"DEPLOYED_ANIMATION PASS unit={unit} phases={count} pose=1 max_diff=\d+ isolated=1 replay=1 immobile=1 separate=1 passed=1 restored=1",text):
        raise AssertionError(f"Deployed firing, immobility or undeploy pose failed for {unit}")
for name,size in [("animation_deploy_idle.png",(512,240)),("animation_deploy_fire.png",(512,240)),("animation_undeploy.png",(64,240))]:
    with Image.open(output/name) as sheet:
        if sheet.size!=size:
            raise AssertionError("Deployed contact sheet dimensions differ")
for stage,count in [("walk",6),("fire",6),("death",15)]:
    with Image.open(output/f"animation_{stage}.png") as sheet:
        if sheet.size!=(1024,400):
            raise AssertionError("Animation sheet dimensions differ")
        for row in range(5):
            frames=[sheet.crop((phase*64,row*80+14,phase*64+64,row*80+78)).tobytes() for phase in range(count)]
            if len(set(frames))<min(count,5):
                raise AssertionError(f"Frozen rendered sequence: {stage}, row {row}")
for case,key,count in [("missing","walk",7),("oversized","walk",257),("corrupt","walk",1),("missing_deployed","depfire",7)]:
    with tempfile.TemporaryDirectory(prefix="openra2-animation-") as directory:
        mod=Path(directory);sprites=mod/"assets/sprites";sprites.mkdir(parents=True)
        (mod/"mod.ini").write_text("[Mod]\nName=Incomplete Animation\nLoadOrder=30000\nEnabled=yes\n",encoding="utf-8")
        (sprites/"anims.ini").write_text(f"[gi]\n{key}={count}\n",encoding="utf-8")
        if case=="corrupt":
            (sprites/"unit_gi_walk_d0_f0.png").write_bytes(b"not a PNG")
        invalid=subprocess.run([str(exe),"--presentation-replay","0","--mod",str(mod)],cwd=root,capture_output=True,timeout=30)
        errors=(invalid.stdout+invalid.stderr).decode("utf-8",errors="replace")
        if invalid.returncode==0 or "SPRITE-MISSING animation" not in errors or "FATAL: required assets missing" not in errors:
            raise AssertionError(f"Invalid declared animation accepted: {case}, {errors[-2000:]}")
for line in text.splitlines():
    if line.startswith(("ANIMATION","DEPLOYED_ANIMATION")):
        print(line)
print("ANIMATION MATRIX PASS: rendered poses, continuous motion clock, save replay, missing/corrupt/oversized rejection")
