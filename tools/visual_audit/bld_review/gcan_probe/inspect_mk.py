"""Inspect GTGCAN make-anim frames vs static SHP; do NOT import gen_assets."""
import os, sys
sys.path.insert(0, r"E:\AI_Generated_Projects\OpenRA2\tools\ra2pack")

from ra2lib import MixTree, Shp, load_pal, shp_frame_to_rgba
from PIL import Image

T = MixTree()
PAL = load_pal(T.find("unittem.pal")[1])
OUT = r"E:\AI_Generated_Projects\OpenRA2\tools\visual_audit\bld_review\gcan_probe\mk_frames"
os.makedirs(OUT, exist_ok=True)

for name in ("artmd.ini", "art.ini"):
    _, data = T.find(name)
    if not data:
        continue
    txt = data.decode("latin-1", "replace")
    i = txt.find("[GTGCAN]")
    if i < 0:
        continue
    end = txt.find("\n[", i + 1)
    chunk = txt[i:end if end > 0 else i + 1200]
    print("===", name, "GTGCAN ===")
    print(chunk)
    # also turret section
    for sec in ("[GTGCANTUR]", "[GTGCANBARL]", "[GTGCANMK]"):
        j = txt.find(sec)
        if j >= 0:
            e2 = txt.find("\n[", j + 1)
            print("---", sec, "---")
            print(txt[j:e2 if e2 > 0 else j + 400])
    break

for stem in ("gtgcanmk", "gtgcan", "gagcan"):
    _, data = T.find_theater(stem + ".shp")
    if not data:
        _, data = T.find(stem + ".shp")
    print(f"\n{stem}.shp:", "YES" if data else "NO")
    if not data:
        continue
    shp = Shp(data)
    print(f"  frames={shp.nframes} canvas={shp.w}x{shp.h}")
    best_i, best_a = 0, -1
    rows = []
    for i in range(shp.nframes):
        fr = shp.frame_pixels(i)
        a = sum(1 for v in fr.pixels if v != 0)
        im = shp_frame_to_rgba(fr, PAL, canvas=None, remap=True)
        bb = im.getbbox() if im else None
        top = bb[1] if bb else None
        if a > best_a:
            best_a, best_i = a, i
        rows.append((i, a, top, fr.x, fr.y, fr.w, fr.h))
    print(f"  PEAK opacity i={best_i} a={best_a} top={rows[best_i][2]}")
    print(f"  LAST         i={shp.nframes-1} a={rows[-1][1]} top={rows[-1][2]}")
    print("  i  opac  topY  xy wh")
    for i, a, top, x, y, w, h in rows:
        mark = ""
        if i == best_i:
            mark = " <<PEAK"
        if i == shp.nframes - 1:
            mark += " <<LAST"
        print(f"  {i:2d} {a:5d}  {str(top):>4}  ({x},{y}) {w}x{h}{mark}")

    for i in sorted({0, max(0, best_i - 1), best_i, shp.nframes - 2, shp.nframes - 1}):
        fr = shp.frame_pixels(i)
        im = shp_frame_to_rgba(fr, PAL, canvas=None, remap=True)
        big = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
        if im:
            big.paste(im, (fr.x, fr.y), im)
        tag = f"{stem}_f{i:02d}"
        if i == best_i:
            tag += "_PEAK"
        if i == shp.nframes - 1:
            tag += "_LAST"
        big.save(os.path.join(OUT, tag + ".png"))
        print("  saved", tag, big.getbbox())

# VXL presence
for n in ("gtgcantur.vxl", "gtgcanbarl.vxl", "gtgcantur.hva", "gtgcanbarl.hva"):
    print(n, "YES" if T.find(n)[1] else "NO")
