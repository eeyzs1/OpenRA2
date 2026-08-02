# 抽查拼图：把关键 PNG 按类别排到一张图上便于目视检查
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

CELL = 132
GRID_BG = (32, 32, 36, 255)
LABEL_H = 14

def sheet(items, name, cols=8):
    rows = (len(items) + cols - 1) // cols
    W = cols * CELL
    H = rows * (CELL + LABEL_H)
    img = Image.new("RGBA", (W, H), (24, 24, 28, 255))
    dr = ImageDraw.Draw(img)
    for i, (label, fn) in enumerate(items):
        cx = (i % cols) * CELL
        cy = (i // cols) * (CELL + LABEL_H)
        dr.rectangle([cx, cy, cx + CELL - 2, cy + CELL - 2], fill=GRID_BG)
        p = os.path.join(SPR, fn)
        if not os.path.exists(p):
            dr.text((cx + 4, cy + 4), "MISSING", fill=(255, 60, 60, 255))
        else:
            im = Image.open(p).convert("RGBA")
            # 画 alpha 棋盘底
            for yy in range(0, CELL - 2, 8):
                for xx in range(0, CELL - 2, 8):
                    if (xx // 8 + yy // 8) % 2 == 0:
                        dr.rectangle([cx + xx, cy + yy, cx + min(xx + 8, CELL - 2), cy + min(yy + 8, CELL - 2)], fill=(48, 48, 54, 255))
            s = min((CELL - 10) / im.width, (CELL - 10) / im.height, 4.0)
            if s != 1.0:
                im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.NEAREST)
            img.paste(im, (cx + (CELL - 2 - im.width) // 2, cy + (CELL - 2 - im.height) // 2), im)
        dr.text((cx + 3, cy + CELL - 1), label[:20], fill=(220, 220, 220, 255))
    img.save(os.path.join(OUT, name))
    print("wrote", name, img.size)

# 载具 VXL 8方向（灰熊/天启/基洛夫/夜鹰/驱逐舰/矿车）
veh = []
for eng in ["grizzly", "apocalypse", "kirov", "nighthawk", "destroyer", "harvester"]:
    for e in range(8):
        veh.append((f"{eng} d{e}", f"unit_{eng}_d{e}_f0.png"))
sheet(veh, "check_vehicles.png", cols=8)

# 步兵 SHP 8方向x2帧（GI/警犬/磁暴兵）
inf = []
for eng in ["gi", "attackdog", "teslatrooper"]:
    for e in range(8):
        for f in range(2):
            inf.append((f"{eng} d{e}f{f}", f"unit_{eng}_d{e}_f{f}.png"))
sheet(inf, "check_infantry.png", cols=8)

# 建筑
bld = [(b, f"bld_{b}.png") for b in
       ["conyard", "powerplant", "teslacoil", "nukesilo", "radar", "grandcannon",
        "warfactory", "barracks", "orerefinery", "navalyard", "battlelab", "prismtower"]]
sheet(bld, "check_buildings.png", cols=6)

# 图标
ico = [(n, f"icon_{n}.png") for n in
       ["unit_grizzly", "unit_apocalypse", "unit_kirov", "unit_nighthawk", "unit_gi", "unit_tanya",
        "bld_conyard", "bld_teslacoil", "bld_nukesilo", "bld_radar", "bld_grandcannon", "bld_powerplant"]]
sheet(ico, "check_icons.png", cols=6)
