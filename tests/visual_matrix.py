"""Validate actual framebuffer captures; inspection of art remains a separate gate."""
import argparse
import re
import subprocess
from pathlib import Path
from PIL import Image, ImageChops

parser=argparse.ArgumentParser()
parser.add_argument("--exe",type=Path,required=True)
parser.add_argument("--output",type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
output=args.output.resolve()
run=subprocess.run([str(args.exe.resolve()),"--visual-regression",str(output)],cwd=root,capture_output=True,timeout=90)
text=(run.stdout+run.stderr).decode("utf-8",errors="replace").replace("\r\n","\n")
if run.returncode:
    raise AssertionError(f"Visual regression failed ({run.returncode}): {text[-5000:]}")
for width,height in [(1280,720),(1920,1080)]:
    for prefix in ["","crowd_500_","feedback_rejected_"]:
        image=Image.open(output/f"{prefix}{width}x{height}.png")
        if image.size!=(width,height):
            raise AssertionError(f"Capture dimensions differ from actual framebuffer: {image.size}")
        if image.getcolors(maxcolors=128) is not None:
            raise AssertionError("Capture is blank or missing scene content")
        if prefix=="feedback_rejected_":
            # Actual marker location in logical canvas: tile center (22.5,22.5), camera (-564,80).
            scale=min(width/1440,height/810)
            cx=int((width-1440*scale)/2+564*scale)
            cy=int((height-810*scale)/2+640*scale)
            pixels=list(image.convert("RGB").crop((cx-10,cy-10,cx+11,cy+11)).getdata())
            if sum(r>200 and g<130 and b<130 for r,g,b in pixels)<5:
                raise AssertionError("Rejected command marker missing at actual destination")
        image.close()
    if f"VISUAL PASS size={width}x{height} actual={width}x{height} pick=1" not in text:
        raise AssertionError("Physical window-to-canvas picking failed")
    if f"VISUAL_MINIMAP size={width}x{height} zoom=1.4 centered=1" not in text:
        raise AssertionError("Zoomed radar navigation failed")
bench=re.findall(r"VISUAL_BENCH units=(200|500) size=(1280x720|1920x1080) render_p95_ms=([0-9.]+) simulation_unchanged=1",text)
for faction in range(4):
    if f"VISUAL_COMBAT faction={faction} pass=1 fire=1 impact=1 death=1 walk=1 isolated=1 expired=1" not in text:
        raise AssertionError(f"Combat presentation failed for faction {faction}")
    for width,height in [(1280,720),(1920,1080)]:
        for stage in ["fire","impact","impact_without","death"]:
            with Image.open(output/f"combat_f{faction}_{stage}_{width}x{height}.png") as capture:
                if capture.size!=(width,height):
                    raise AssertionError("Combat framebuffer dimensions differ")
        # Same world/tick with and without app-owned cues must differ at the impact.
        with Image.open(output/f"combat_f{faction}_impact_{width}x{height}.png") as a, Image.open(output/f"combat_f{faction}_impact_without_{width}x{height}.png") as b:
            scale=min(width/1440,height/810)
            cx=int((width-1440*scale)/2+788*scale)
            cy=int((height-810*scale)/2+558*scale)
            diff=ImageChops.difference(a.convert("RGB"),b.convert("RGB")).crop((cx-14,cy-14,cx+15,cy+15))
            if sum(any(pixel) for pixel in diff.getdata())<5:
                raise AssertionError(f"Impact cue missing at target for faction {faction}, {width}x{height}")
for stage in ["attack_rejected","attack_partial"]:
    for width,height in [(1280,720),(1920,1080)]:
        with Image.open(output/f"{stage}_{width}x{height}.png") as a,Image.open(output/f"{stage}_without_{width}x{height}.png") as b:
            if a.size!=(width,height) or b.size!=a.size:
                raise AssertionError("Attack feedback capture size differs")
            scale=min(width/1440,height/810)
            cx=int((width-1440*scale)/2+660*scale)
            cy=int((height-810*scale)/2+632*scale) # target altitude, not its ground cell
            diff=ImageChops.difference(a.convert("RGB"),b.convert("RGB")).crop((cx-14,cy-14,cx+15,cy+15))
            if sum(any(pixel) for pixel in diff.getdata())<10:
                raise AssertionError("Attack feedback missing at the aircraft's actual height")
for stage in ["engineer_rejected","engineer_partial"]:
    for width,height in [(1280,720),(1920,1080)]:
        with Image.open(output/f"{stage}_{width}x{height}.png") as a,Image.open(output/f"{stage}_without_{width}x{height}.png") as b:
            if a.size!=(width,height) or b.size!=a.size:
                raise AssertionError("Engineer feedback capture size differs")
            scale=min(width/1440,height/810)
            cx=int((width-1440*scale)/2+660*scale)
            cy=int((height-810*scale)/2+672*scale)
            diff=ImageChops.difference(a.convert("RGB"),b.convert("RGB")).crop((cx-14,cy-14,cx+15,cy+15))
            if sum(any(pixel) for pixel in diff.getdata())<10:
                raise AssertionError("Engineer feedback missing at building center")
if "VISUAL_ENGINEER rejected_partial=1 simulation_unchanged=1" not in text:
    raise AssertionError("Engineer result presentation regression")
for stage in ["entry_rejected","entry_partial"]:
    for width,height in [(1280,720),(1920,1080)]:
        with Image.open(output/f"{stage}_{width}x{height}.png") as a,Image.open(output/f"{stage}_without_{width}x{height}.png") as b:
            if a.size!=(width,height) or b.size!=a.size:
                raise AssertionError("Entry feedback capture size differs")
            scale=min(width/1440,height/810)
            cx=int((width-1440*scale)/2+532*scale)
            cy=int((height-810*scale)/2+720*scale)
            diff=ImageChops.difference(a.convert("RGB"),b.convert("RGB")).crop((cx-14,cy-14,cx+15,cy+15))
            if sum(any(pixel) for pixel in diff.getdata())<10:
                raise AssertionError("Entry feedback missing at transport position")
if "VISUAL_ENTRY rejected_partial=1 simulation_unchanged=1" not in text:
    raise AssertionError("Entry result presentation regression")
for stage in ["release_blocked","release_partial"]:
    for width,height in [(1280,720),(1920,1080)]:
        with Image.open(output/f"{stage}_{width}x{height}.png") as a,Image.open(output/f"{stage}_without_{width}x{height}.png") as b:
            if a.size!=(width,height) or b.size!=a.size:
                raise AssertionError("Release feedback capture size differs")
            scale=min(width/1440,height/810)
            cx=int((width-1440*scale)/2+532*scale)
            cy=int((height-810*scale)/2+720*scale)
            diff=ImageChops.difference(a.convert("RGB"),b.convert("RGB")).crop((cx-14,cy-14,cx+15,cy+15))
            if sum(any(pixel) for pixel in diff.getdata())<10:
                raise AssertionError("Release feedback missing at container position")
if "VISUAL_RELEASE blocked_partial=1 simulation_unchanged=1" not in text:
    raise AssertionError("Release result presentation regression")
for stage in ["service_rejected","service_partial","service_money","service_power"]:
    for width,height in [(1280,720),(1920,1080)]:
        with Image.open(output/f"{stage}_{width}x{height}.png") as a,Image.open(output/f"{stage}_without_{width}x{height}.png") as b:
            if a.size!=(width,height) or b.size!=a.size:
                raise AssertionError("Service feedback capture size differs")
            scale=min(width/1440,height/810)
            cx=int((width-1440*scale)/2+532*scale)
            cy=int((height-810*scale)/2+500*scale)
            diff=ImageChops.difference(a.convert("RGB"),b.convert("RGB")).crop((cx-14,cy-14,cx+15,cy+15))
            if sum(any(pixel) for pixel in diff.getdata())<10:
                raise AssertionError("Service feedback missing at facility position")
if "VISUAL_SERVICE rejected_partial_paused=1 simulation_unchanged=1" not in text:
    raise AssertionError("Service result presentation regression")
if len(bench)!=4 or "VISUAL simulation_unchanged=1" not in text or "VISUAL_REMAP pass=1" not in text or "VISUAL_COMMAND rejected=1 simulation_unchanged=1" not in text:
    raise AssertionError("Missing render benchmark, simulation isolation or indexed-remap evidence")
if "VISUAL_CUE_RULES pass=1 invulnerable=1 actual_loss=1 fog=1 metadata=1 snapshot=1 bounded=1" not in text:
    raise AssertionError("Combat cue protection, visibility or lifecycle regression")
if "VISUAL_PNG_REMAP pass=1" not in text:
    raise AssertionError("PNG body/turret house-mask regression")
if "VISUAL_ATTACK rejected_partial=1 simulation_unchanged=1" not in text:
    raise AssertionError("Attack result presentation regression")
for line in text.splitlines():
    if line.startswith("VISUAL"):
        print(line)
print("VISUAL MATRIX PASS: exact framebuffer sizes, picking, zoomed radar, indexed team colors, 200/500 units")
