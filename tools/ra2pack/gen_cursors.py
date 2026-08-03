# Extract RA2 mouse.shp cursors → assets/gui/cursors/
import os
from ra2lib import MixTree, Shp
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "assets", "gui", "cursors")
os.makedirs(OUT, exist_ok=True)

def load_pal(T):
    for n in ("mouse.pal", "unittem.pal", "isotem.pal"):
        _, raw = T.find(n)
        if raw and len(raw) >= 768:
            pal = []
            mx = max(raw[:768])
            for i in range(256):
                r, g, b = raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]
                if mx <= 63:
                    r, g, b = r * 4, g * 4, b * 4
                pal.append((r, g, b))
            print("palette", n, "idx0", pal[0], "idx1", pal[1])
            return pal
    return [(i, i, i) for i in range(256)]

T = MixTree()
_, raw = T.find("mouse.shp")
if not raw:
    print("FAIL: mouse.shp missing")
    raise SystemExit(1)
pal = load_pal(T)
shp = Shp(raw)
print("mouse.shp frames=%d %dx%d" % (shp.nframes, shp.w, shp.h))
# 全量导出：AttackMove=404 等帧必须存在（Ares MouseCursors）
saved = 0
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
    path = os.path.join(OUT, "cursor_%03d.png" % i)
    img.save(path)
    if i < 100:
        img.save(os.path.join(OUT, "cursor_%02d.png" % i))
    saved += 1
print("saved", saved, "→", OUT)
