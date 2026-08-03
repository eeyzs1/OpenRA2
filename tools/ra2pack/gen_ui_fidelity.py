# Extract RA2 UI fidelity assets: full mouse.shp, gclock2, money digits, oil/hospital ActiveAnim.
import os, sys
from ra2lib import MixTree, Shp, shp_frame_to_rgba
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GUI = os.path.join(ROOT, "assets", "gui")
CUR = os.path.join(GUI, "cursors")
CLK = os.path.join(GUI, "gclock2")
MONEY = os.path.join(GUI, "money_digits")
SPR = os.path.join(ROOT, "assets", "sprites")
for d in (CUR, CLK, MONEY, SPR):
    os.makedirs(d, exist_ok=True)

def load_pal(T, names):
    for n in names:
        _, raw = T.find(n)
        if raw and len(raw) >= 768:
            pal, mx = [], max(raw[:768])
            for i in range(256):
                r, g, b = raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]
                if mx <= 63:
                    r, g, b = r * 4, g * 4, b * 4
                pal.append((r, g, b))
            print("palette", n)
            return pal
    return [(i, i, i) for i in range(256)]

def save_shp_frames(shp, pal, out_dir, prefix, remap=False, shadow=True):
    n = 0
    for i in range(shp.nframes):
        fr = shp.frame_pixels(i)
        if fr is None:
            continue
        img = shp_frame_to_rgba(fr, pal, canvas=(shp.w, shp.h), remap=remap)
        if isinstance(img, Image.Image):
            path = os.path.join(out_dir, "%s_%02d.png" % (prefix, i))
            img.save(path)
            n += 1
        else:
            # ra2lib may return list/tuple of pixels — fallback manual
            im = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
            px = im.load()
            for yy in range(fr.h):
                for xx in range(fr.w):
                    v = fr.pixels[yy * fr.w + xx]
                    if v == 0:
                        continue
                    if shadow and v == 1:
                        px[fr.x + xx, fr.y + yy] = (0, 0, 0, 120)
                        continue
                    r, g, b = pal[v]
                    px[fr.x + xx, fr.y + yy] = (r, g, b, 255)
            im.save(os.path.join(out_dir, "%s_%02d.png" % (prefix, i)))
            n += 1
    return n

T = MixTree()

# ---- mouse.shp full ----
_, raw = T.find("mouse.shp")
if not raw:
    print("FAIL mouse.shp"); sys.exit(1)
pal = load_pal(T, ("mouse.pal", "unittem.pal", "isotem.pal"))
shp = Shp(raw)
print("mouse.shp frames=%d %dx%d" % (shp.nframes, shp.w, shp.h))
# clear old truncated set
for f in os.listdir(CUR):
    if f.startswith("cursor_") and f.endswith(".png"):
        os.remove(os.path.join(CUR, f))
n = 0
for i in range(shp.nframes):
    img = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
    fr = shp.frame_pixels(i)
    if fr is not None and fr.w > 0 and fr.h > 0:
        out = img.load()
        for yy in range(fr.h):
            for xx in range(fr.w):
                v = fr.pixels[yy * fr.w + xx]
                if v == 0:
                    continue
                if v == 1:
                    out[fr.x + xx, fr.y + yy] = (0, 0, 0, 120)
                    continue
                r, g, b = pal[v]
                out[fr.x + xx, fr.y + yy] = (r, g, b, 255)
    img.save(os.path.join(CUR, "cursor_%03d.png" % i))
    n += 1
print("cursors saved", n)

# also keep 2-digit names for first 100 for compatibility during transition
for i in range(min(100, shp.nframes)):
    src = os.path.join(CUR, "cursor_%03d.png" % i)
    dst = os.path.join(CUR, "cursor_%02d.png" % i)
    if os.path.exists(src):
        Image.open(src).save(dst)

# ---- gclock2.shp ----
_, raw = T.find("gclock2.shp")
if not raw:
    print("WARN gclock2.shp missing")
else:
    cpal = load_pal(T, ("sidebar.pal", "unittem.pal", "cameo.pal"))
    cshp = Shp(raw)
    print("gclock2 frames=%d %dx%d" % (cshp.nframes, cshp.w, cshp.h))
    for f in os.listdir(CLK):
        if f.endswith(".png"):
            os.remove(os.path.join(CLK, f))
    n = 0
    for i in range(cshp.nframes):
        img = Image.new("RGBA", (cshp.w, cshp.h), (0, 0, 0, 0))
        fr = cshp.frame_pixels(i)
        if fr is not None and fr.w > 0 and fr.h > 0:
            out = img.load()
            for yy in range(fr.h):
                for xx in range(fr.w):
                    v = fr.pixels[yy * fr.w + xx]
                    if v == 0:
                        continue
                    if v == 1:
                        out[fr.x + xx, fr.y + yy] = (0, 0, 0, 100)
                        continue
                    r, g, b = cpal[v]
                    # clock overlay: keep semi-transparent dark wash
                    a = 180 if v > 1 else 0
                    out[fr.x + xx, fr.y + yy] = (r, g, b, a)
        img.save(os.path.join(CLK, "gclock2_%02d.png" % i))
        n += 1
    print("gclock2 saved", n)

# ---- money.shp digits ----
_, raw = T.find("money.shp")
if not raw:
    print("WARN money.shp missing — will synthesize cyan digits from sidebar probe later")
else:
    mpal = load_pal(T, ("sidebar.pal", "unittem.pal"))
    mshp = Shp(raw)
    print("money.shp frames=%d %dx%d" % (mshp.nframes, mshp.w, mshp.h))
    for f in os.listdir(MONEY):
        if f.endswith(".png"):
            os.remove(os.path.join(MONEY, f))
    for i in range(mshp.nframes):
        img = Image.new("RGBA", (mshp.w, mshp.h), (0, 0, 0, 0))
        fr = mshp.frame_pixels(i)
        if fr is not None and fr.w > 0 and fr.h > 0:
            out = img.load()
            for yy in range(fr.h):
                for xx in range(fr.w):
                    v = fr.pixels[yy * fr.w + xx]
                    if v == 0:
                        continue
                    r, g, b = mpal[v]
                    out[fr.x + xx, fr.y + yy] = (r, g, b, 255)
        img.save(os.path.join(MONEY, "digit_%02d.png" % i))
    print("money digits saved", mshp.nframes)

# ---- ActiveAnim: CAOILD_A / CAHOSP_A ----
upal = load_pal(T, ("unittem.pal", "unitsno.pal"))
for stem, outpref in (("caoild_a", "bld_oilderrick_a"), ("cahosp_a", "bld_hospital_a"),
                      ("ctoild_a", "bld_oilderrick_ta"), ("cthosp_a", "bld_hospital_ta")):
    _, raw = T.find(stem + ".shp")
    if not raw:
        print("WARN missing", stem)
        continue
    ashp = Shp(raw)
    print("%s frames=%d %dx%d" % (stem, ashp.nframes, ashp.w, ashp.h))
    for i in range(ashp.nframes):
        img = Image.new("RGBA", (ashp.w, ashp.h), (0, 0, 0, 0))
        fr = ashp.frame_pixels(i)
        if fr is not None and fr.w > 0 and fr.h > 0:
            out = img.load()
            for yy in range(fr.h):
                for xx in range(fr.w):
                    v = fr.pixels[yy * fr.w + xx]
                    if v == 0:
                        continue
                    if v == 1:
                        out[fr.x + xx, fr.y + yy] = (0, 0, 0, 120)
                        continue
                    # Remapable=no: keep palette colors 16-31 as-is
                    r, g, b = upal[v]
                    out[fr.x + xx, fr.y + yy] = (r, g, b, 255)
        img.save(os.path.join(SPR, "%s_f%d.png" % (outpref, i)))
    print("saved", outpref, ashp.nframes)

print("DONE")
