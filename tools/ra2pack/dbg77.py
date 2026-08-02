# 修复后验证：conyard/psychicsensor/wall 等合成图标 + 全图标总览
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

CELL = 116
LABEL_H = 14

def sheet(items, name, cols=8):
    rows = (len(items) + cols - 1) // cols
    img = Image.new("RGBA", (cols * CELL, rows * (CELL + LABEL_H)), (24, 24, 28, 255))
    dr = ImageDraw.Draw(img)
    for i, (label, fn) in enumerate(items):
        cx = (i % cols) * CELL
        cy = (i // cols) * (CELL + LABEL_H)
        dr.rectangle([cx, cy, cx + CELL - 2, cy + CELL - 2], fill=(40, 40, 46, 255))
        p = os.path.join(SPR, fn)
        if not os.path.exists(p):
            dr.text((cx + 4, cy + 4), "MISSING", fill=(255, 60, 60, 255))
        else:
            im = Image.open(p).convert("RGBA")
            s = min((CELL - 8) / im.width, (CELL - 8) / im.height, 4.0)
            if s != 1.0:
                im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.NEAREST)
            img.paste(im, (cx + (CELL - 2 - im.width) // 2, cy + (CELL - 2 - im.height) // 2), im)
        dr.text((cx + 3, cy + CELL - 1), label[:18], fill=(220, 220, 220, 255))
    img.save(os.path.join(OUT, name))
    print("wrote", name, img.size)

# 合成/修复的图标
fixed = [(n, f"icon_{n}.png") for n in
         ["bld_conyard", "bld_psychicsensor", "bld_wall", "bld_civhouse",
          "bld_hospital", "bld_machineshop", "bld_oilderrick", "bld_techairport"]]
sheet(fixed, "check_icons_fixed.png", cols=4)

# 全建筑图标总览
import glob
allbld = sorted(os.path.basename(p) for p in glob.glob(os.path.join(SPR, "icon_bld_*.png")))
sheet([(f[9:-4], f) for f in allbld], "check_icons_allbld.png", cols=8)
allunit = sorted(os.path.basename(p) for p in glob.glob(os.path.join(SPR, "icon_unit_*.png")))
sheet([(f[10:-4], f) for f in allunit], "check_icons_allunit.png", cols=8)
print("bld icons:", len(allbld), "unit icons:", len(allunit))
