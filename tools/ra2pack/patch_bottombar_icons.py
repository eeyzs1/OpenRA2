# Patch bottombar.png: replace control-group I/II icons with Stop / Deploy (RA2 Advanced Command Bar).
from PIL import Image, ImageDraw
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
path = os.path.join(ROOT, "assets", "gui", "bottombar.png")
bb = Image.open(path).convert("RGBA")
# Icon centers from blue-cluster scan (source 1366x33)
icons = [
    (103, 16),  # Stop
    (155, 16),  # Deploy
]
r = 9  # clear radius of old I/II glyphs

def clear_circle(img, cx, cy, rad):
    px = img.load()
    w, h = img.size
    for y in range(max(0, cy - rad), min(h, cy + rad + 1)):
        for x in range(max(0, cx - rad), min(w, cx + rad + 1)):
            if (x - cx) * (x - cx) + (y - cy) * (y - cy) <= rad * rad:
                # keep bar black bg
                px[x, y] = (0, 0, 0, 255)

def draw_stop(img, cx, cy):
    d = ImageDraw.Draw(img)
    # blue glow ring + stop: octagon-ish circle with horizontal bar
    d.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], outline=(40, 120, 220, 255), width=2)
    d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], outline=(90, 180, 255, 255), width=1)
    d.rectangle([cx - 5, cy - 2, cx + 5, cy + 2], fill=(200, 230, 255, 255))

def draw_deploy(img, cx, cy):
    d = ImageDraw.Draw(img)
    # brackets + down arrow
    col = (90, 180, 255, 255)
    hi = (200, 230, 255, 255)
    d.line([cx - 7, cy - 6, cx - 7, cy + 6], fill=col, width=2)
    d.line([cx + 7, cy - 6, cx + 7, cy + 6], fill=col, width=2)
    d.line([cx - 7, cy - 6, cx - 4, cy - 6], fill=col, width=2)
    d.line([cx + 7, cy - 6, cx + 4, cy - 6], fill=col, width=2)
    d.line([cx - 7, cy + 6, cx - 4, cy + 6], fill=col, width=2)
    d.line([cx + 7, cy + 6, cx + 4, cy + 6], fill=col, width=2)
    d.polygon([(cx, cy + 5), (cx - 5, cy - 2), (cx + 5, cy - 2)], fill=hi)

clear_circle(bb, icons[0][0], icons[0][1], r)
clear_circle(bb, icons[1][0], icons[1][1], r)
draw_stop(bb, *icons[0])
draw_deploy(bb, *icons[1])
bb.save(path)
print("patched", path)
