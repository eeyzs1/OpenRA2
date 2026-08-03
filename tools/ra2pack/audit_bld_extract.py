# -*- coding: utf-8 -*-
"""Audit: compare MIX SHP vs assets PNG; dump art.ini Foundation/Bib/Image."""
from ra2lib import MixTree, Shp
from PIL import Image
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")
OUT = os.path.join(ROOT, "tools", "ra2pack", "_audit_out")
os.makedirs(OUT, exist_ok=True)

T = MixTree()

def find(n):
    return T.find(n)

def load_pal():
    for n in ("unittem.pal", "isotem.pal"):
        _, raw = find(n)
        if raw and len(raw) >= 768:
            mx = max(raw[:768])
            pal = []
            for i in range(256):
                r, g, b = raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]
                if mx <= 63:
                    r, g, b = r * 4, g * 4, b * 4
                pal.append((r, g, b, 0 if i == 0 else 255))
            return pal
    return [(i, i, i, 0 if i == 0 else 255) for i in range(256)]

def parse_ini(raw):
    secs, cur = {}, None
    for line in raw.decode("latin-1", "replace").splitlines():
        line = line.strip()
        if not line or line.startswith(";") or line.startswith("//"):
            continue
        if line.startswith("[") and line.endswith("]"):
            cur = line[1:-1]
            secs.setdefault(cur, {})
            continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1)
            secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs

_, art_raw = find("art.ini")
art = parse_ini(art_raw) if art_raw else {}
pal = load_pal()

checks = [
    ("powerplant", "GAPOWR", "ggpowr.shp"),
    ("teslareactor", "NAPOWR", "ngpowr.shp"),
    ("orerefinery", "GAREFN", "ggrefn.shp"),
    ("warfactory", "GAWEAP", "ggweap.shp"),
    ("barracks", "GAPILE", "ggpile.shp"),
    ("conyard", "GACNST", "ggcnst.shp"),
    ("civhouse", "CAHSE01", "cthse01.shp"),
    ("prismtower", "ATESLA", "ggpris.shp"),
]

print("=== art.ini ===")
for eng, rid, prefer in checks:
    a = art.get(rid, {})
    print("%-14s Image=%s Foundation=%s Height=%s BibShape=%s NewTheater=%s Buildup=%s" % (
        eng, a.get("Image"), a.get("Foundation"), a.get("Height"),
        a.get("BibShape"), a.get("NewTheater"), a.get("Buildup")))

print("\n=== SHP vs PNG ===")
for eng, rid, prefer in checks:
    path, raw = find(prefer)
    png_path = os.path.join(SPR, "bld_%s.png" % eng)
    im = Image.open(png_path) if os.path.exists(png_path) else None
    if not raw:
        print("%s MISS %s" % (eng, prefer))
        continue
    shp = Shp(raw)
    exp = (round(shp.w * 64 / 60), round(shp.h * 64 / 60))
    print("%s: MIX %s %dx%d f=%d | PNG %s | expect~%s | %s" % (
        eng, prefer, shp.w, shp.h, shp.nframes,
        im.size if im else None, exp, path))
    # Dump all non-empty frames alpha counts
    alphas = []
    for i in range(shp.nframes):
        fr = shp.frame_pixels(i)
        if not fr:
            alphas.append(0)
            continue
        a = sum(1 for v in fr.pixels if v != 0)
        alphas.append(a)
    print("  frame alphas:", alphas[:12], ("..." if len(alphas) > 12 else ""))
    # Composite ALL frames that have meaningful alpha (not tiny sparks)
    best = max(range(len(alphas)), key=lambda i: alphas[i]) if alphas else 0
    a0 = alphas[best] if alphas else 0
    big = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
    used = []
    for i, a in enumerate(alphas):
        if a <= 0:
            continue
        # keep main + overlays up to 50% of best (details), skip tiny FX
        if i == best or (0.02 * a0 < a < 0.55 * a0) or (i < 3 and a > 0.1 * a0):
            fr = shp.frame_pixels(i)
            px = Image.new("RGBA", (fr.w, fr.h))
            px.putdata([pal[v] if v < 256 else (0, 0, 0, 0) for v in fr.pixels])
            big.paste(px, (fr.x, fr.y), px)
            used.append(i)
    big.save(os.path.join(OUT, "mix_%s_full.png" % eng))
    # Also frame0-only and best-only
    for label, idx in (("f0", 0), ("best", best)):
        fr = shp.frame_pixels(idx)
        if not fr:
            continue
        one = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
        px = Image.new("RGBA", (fr.w, fr.h))
        px.putdata([pal[v] if v < 256 else (0, 0, 0, 0) for v in fr.pixels])
        one.paste(px, (fr.x, fr.y), px)
        one.save(os.path.join(OUT, "mix_%s_%s.png" % (eng, label)))
    if im:
        im.save(os.path.join(OUT, "png_%s.png" % eng))
    print("  composited frames:", used)

print("wrote", OUT)
