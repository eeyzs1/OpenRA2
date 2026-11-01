"""Ground vehicle anchors, alpha and palette-index house masks without game data."""
import sys
import tempfile
from pathlib import Path
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools/asset_pipeline"))
from publish_unit import paste_on_canvas
from formats import rgba_to_indexed, save_indexed_png, write_hva_static
from infantry_sequence import parse_sequence, sequence_frame, sequence_fits

prone=parse_sequence("86,1,6 ; facing stride differs from count")
assert [sequence_frame(prone,d) for d in range(8)] == [86,92,98,104,110,116,122,128]
assert sequence_frame(parse_sequence("20,2,5"),3,1)==36
assert sequence_frame(parse_sequence("134,15,0,S"),7,14)==148
assert sequence_fits(prone,129) and not sequence_fits(prone,128)
assert parse_sequence("0,0,0") is None and parse_sequence("8,6,-1") is None

source = Image.new("RGBA", (12,20), (0,0,0,0))
source.putpixel((6,14), (80,120,160,128))
canvas = paste_on_canvas(source,96,96,0.72)
assert canvas.getpixel((48,69)) == (80,120,160,128), "ground anchor or alpha changed"
palette = [(128,128,128)]*256
for index in range(16,32):
    palette[index] = (0,round(252-(index-16)*220/15),0)  # native bright-to-dark order, deliberately non-red
image = Image.new("RGBA",(3,1))
image.putdata([(252,0,0,255),(32,0,0,255),(128,128,128,255)])
indexed = rgba_to_indexed(image,palette)
assert indexed.getpixel((0,0)) == 16 and indexed.getpixel((1,0)) == 31
assert not 16 <= indexed.getpixel((2,0)) <= 31, "neutral paint became a house pixel"
gradient=Image.new("RGBA",(16,1));gradient.putdata([(max(palette[index]),0,0,255) for index in range(16,32)])
quantized=rgba_to_indexed(gradient,palette)
assert [quantized.getpixel((index,0)) for index in range(16)]==list(range(16,32)),"native shade order or darkest house pixels lost"
assert rgba_to_indexed(Image.new("RGBA",(1,1),(23,0,0,255)),palette).getpixel((0,0))==31,"shaded dark house paint became a transparent shadow"
from ra2lib import Hva
hva=Hva(write_hva_static(2,2,["body","rotor"]))
assert hva.valid and hva.nframes==2 and hva.nsec==2 and hva.sec_names==["body","rotor"],"HVA names/matrix layout invalid"
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/"body.png"
    save_indexed_png(indexed,path,remap_mask=True)
    with Image.open(path.with_name("body_remap.png")) as mask:
        assert mask.getpixel((0,0)) == (252,252,252,255)
        assert mask.getpixel((1,0)) == (32,32,32,255)
        assert mask.getpixel((2,0))[3] == 0
print("ASSET PIPELINE PASS: shared ground anchor, alpha, dark/bright remap indices, neutral exclusion")
