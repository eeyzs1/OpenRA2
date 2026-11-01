"""Synthetic SHP/PAL fixtures verify chrono extraction without redistributing game data."""
from pathlib import Path
import sys, tempfile, struct, json
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools/ra2pack"))
from gen_chrono_fx import export_chrono
from PIL import Image

def shp():
    count=2;offset=8+24*count
    header=struct.pack("<HHHH",0,4,3,count)
    frames=b"".join(struct.pack("<HHHHIIII",1,1,2,1,0,0,0,offset+i*2) for i in range(count))
    return header+frames+bytes([0,1,16,3])
class Sources:
    def __init__(self):
        self.files={name:shp() for name in ["warpin.shp","warpout.shp","warpaway.shp"]}
        self.files["anim.pal"]=bytes(value for index in range(256) for value in (index%64,(index+1)%64,(index+2)%64))
    def find(self,name):return "synthetic.mix",self.files.get(name)
with tempfile.TemporaryDirectory(prefix="openra2-chrono-extract-") as directory:
    assets=Path(directory);sources=Sources();manifest=export_chrono(sources,assets)
    assert [sequence["frames"] for sequence in manifest["sequences"]]==[2,2,2]
    image=Image.open(assets/"sprites/fx_warpin_0.png").convert("RGBA")
    assert image.size==(4,3) and image.getpixel((1,1))==(0,0,0,0)
    assert image.getpixel((2,1))==(4,8,12,255),"anim palette index 1 must not become a unit shadow"
    assert Image.open(assets/"sprites/fx_warpin_1.png").getpixel((1,1))==(64,68,72,255),"animation index 16 must not become a house remap"
    assert "Frames=2" in (assets/"chrono_fx.ini").read_text()
    before={str(file.relative_to(assets)):file.read_bytes() for file in assets.rglob("*") if file.is_file()}
    export_chrono(sources,assets)
    assert before=={str(file.relative_to(assets)):file.read_bytes() for file in assets.rglob("*") if file.is_file()},"repeat extraction must be deterministic"
    for value in [None,b"bad shp",shp()[:-1]]:
        sources.files["warpaway.shp"]=value
        try:export_chrono(sources,assets)
        except (ValueError,struct.error,IndexError):pass
        else:raise AssertionError("missing/corrupt/truncated source accepted")
        assert before=={str(file.relative_to(assets)):file.read_bytes() for file in assets.rglob("*") if file.is_file()},"source failure altered published frames"
print("CHRONO PIPELINE PASS: palette indices, canvas offsets, frame count, repeat output, failed-source preservation")
