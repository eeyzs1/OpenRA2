import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image

OUT = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)

t = MixTree()
_, pal_unit = t.find("unittem.pal")
pal_u = load_pal(pal_unit)
_, pal_cameo = t.find("cameo.pal")
pal_c = load_pal(pal_cameo)

def composite(shp: Shp, i: int, pal):
    """render frame i onto the full SHP canvas at its (x,y)"""
    img = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
    fr = shp.frame_pixels(i)
    fi = shp_frame_to_rgba(fr, pal)
    img.paste(fi, (fr.x, fr.y))
    return img

# ---- 1) GI infantry: standing facings 0..7 and walk frames of facings 0..7 (steps 0..5)
_, gi_data = t.find("gi.shp")
gi = Shp(gi_data)
print("gi.shp canvas", gi.w, gi.h, "frames", gi.nframes)
S = 3  # upscale for visibility
row1 = Image.new("RGBA", (gi.w * 8, gi.h), (40, 40, 48, 255))
for f in range(8):
    row1.paste(composite(gi, f, pal_u), (f * gi.w, 0))
row2 = Image.new("RGBA", (gi.w * 8, gi.h), (40, 40, 48, 255))
for f in range(8):
    row2.paste(composite(gi, 8 + f * 6, pal_u), (f * gi.w, 0))  # walk step 0 per facing
row3 = Image.new("RGBA", (gi.w * 8, gi.h), (40, 40, 48, 255))
for f in range(8):
    row3.paste(composite(gi, 8 + f * 6 + 3, pal_u), (f * gi.w, 0))  # walk step 3 per facing
sheet = Image.new("RGBA", (gi.w * 8, gi.h * 3), (40, 40, 48, 255))
sheet.paste(row1, (0, 0)); sheet.paste(row2, (0, gi.h)); sheet.paste(row3, (0, gi.h * 2))
sheet = sheet.resize((sheet.width * S, sheet.height * S), Image.NEAREST)
sheet.save(os.path.join(OUT, "sample_gi_facings.png"))
print("wrote sample_gi_facings.png  (rows: stand f0-7, walk step0 f0-7, walk step3 f0-7)")

# ---- 2) GACNST building (A suffix = all theaters): all frames
src, ga_data = t.find("gacnst.shp")
print("gacnst.shp from", src, ":", len(ga_data) if ga_data else None)
ga = Shp(ga_data)
print("gacnst canvas", ga.w, ga.h, "frames", ga.nframes)
bw = ga.w * ga.nframes
sheet2 = Image.new("RGBA", (bw, ga.h), (40, 40, 48, 255))
for f in range(ga.nframes):
    sheet2.paste(composite(ga, f, pal_u), (f * ga.w, 0))
sheet2 = sheet2.resize((min(sheet2.width * 2, 1600), min(sheet2.height * 2, 900)), Image.NEAREST)
sheet2.save(os.path.join(OUT, "sample_gacnst_frames.png"))
print("wrote sample_gacnst_frames.png")

# ---- 3) cameo mtnkicon.shp frame0 with cameo.pal
_, ic_data = t.find("mtnkicon.shp")
ic = Shp(ic_data)
print("mtnkicon canvas", ic.w, ic.h, "frames", ic.nframes)
im = composite(ic, 0, pal_c)
im = im.resize((ic.w * 4, ic.h * 4), Image.NEAREST)
im.save(os.path.join(OUT, "sample_mtnkicon.png"))
print("wrote sample_mtnkicon.png")
