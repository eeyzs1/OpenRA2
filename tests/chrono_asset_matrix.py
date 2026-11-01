"""Reject invalid metadata, missing frames and corrupt chrono PNGs at normal startup."""
import argparse,subprocess,tempfile
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument("--exe",type=Path,required=True);args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
for name,count in [("zero","0"),("large","257"),("invalid","3junk"),("missing","11"),("corrupt","10")]:
    with tempfile.TemporaryDirectory(prefix="openra2-chrono-content-") as directory:
        mod=Path(directory);(mod/"assets/sprites").mkdir(parents=True)
        (mod/"mod.ini").write_text("[Mod]\nName=Chrono Validation\nLoadOrder=30000\nEnabled=yes\n")
        (mod/"assets/chrono_fx.ini").write_text(f"[Arrival]\nFrames={count}\n[Departure]\nFrames=21\n[Erase]\nFrames=23\n")
        if name=="corrupt":(mod/"assets/sprites/fx_warpin_0.png").write_bytes(b"not a PNG")
        run=subprocess.run([str(args.exe.resolve()),"--presentation-replay","0","--mod",str(mod)],cwd=root,capture_output=True,timeout=60)
        output=(run.stdout+run.stderr).decode("utf-8",errors="replace")
        assert run.returncode!=0 and "SPRITE-MISSING chrono" in output and "FATAL: required assets missing" in output,(name,output[-1500:])
print("CHRONO ASSET MATRIX PASS: bounded integer metadata, missing frames, corrupt PNGs reject startup")
