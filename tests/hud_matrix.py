import argparse
import subprocess
import sys
from pathlib import Path
from PIL import Image

parser=argparse.ArgumentParser()
parser.add_argument("--exe",type=Path,required=True)
parser.add_argument("--output",type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(root/"tools/asset_pipeline"))
from cameo import build_cameo, check_cameo
from publish_unit import load_manifest
master=load_manifest(root/"tools/asset_pipeline/templates/type99/manifest.yaml")
errors=check_cameo(master,root/"assets/sprites")
if errors:
    raise AssertionError("Cameo publication mismatch: "+"; ".join(errors))
expected=2166136261
for value in build_cameo(master,root/"assets/sprites").tobytes():
    expected=((expected^value)*16777619)&0xffffffff
output=args.output.resolve()
run=subprocess.run([str(args.exe.resolve()),"--hud-regression",str(output)],cwd=root,capture_output=True,timeout=100)
text=(run.stdout+run.stderr).decode("utf-8",errors="replace").replace("\r\n","\n")
(output/"hud.log").write_text(text,encoding="utf-8")
if run.returncode:
    reports="\n".join(line for line in text.splitlines() if line.startswith("HUD "))
    raise AssertionError(f"HUD regression failed ({run.returncode}): {reports}\n{text[-1500:]}")
if f"HUD_CAMEO type99={expected:08x} remap_stable=1" not in text:
    raise AssertionError("GPU cameo differs from the published master, or was house-remapped")
stems=[f"f{f}_l{l}_{tab}" for f in range(4) for l in range(2) for tab in ["vehicles","buildings"]]
stems += [f"state{s}_l{l}" for s in range(8) for l in range(2)]
stems += [f"bottom_l{l}" for l in range(2)]
for stem in stems:
    for width,height in [(1280,720),(1920,1080)]:
        with Image.open(output/f"{stem}_{width}x{height}.png") as frame:
            if frame.size!=(width,height) or frame.getcolors(maxcolors=128) is not None:
                raise AssertionError(f"Blank or incorrectly sized HUD frame: {stem} {frame.size}")
for line in text.splitlines():
    if line.startswith("HUD "):
        print(line)
print(f"HUD MATRIX PASS: {len(stems)*2} actual framebuffer captures")
