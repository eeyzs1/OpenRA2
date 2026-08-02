# 单位/建筑素材完整性审计：列出缺失方向/帧 + 生成对照 montage
import os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

UNITS = ["mcv","harvester","gi","conscript","pla","engineer","attackdog","spy",
    "flaktrooper","teslatrooper","sniper","tanya","desolator","chrono","guardiangi",
    "crazyivan","grizzly","rhino","type99","flaktrack","ifv","prismtank","teslatank",
    "miragetank","v3launcher","apocalypse","terrordrone","intruder","mig","blackeagle",
    "kirov","rocketeer","destroyer","typhoon","aegis","seascorpion","dreadnought",
    "aircraftcarrier","amphtransport","chronominer","warminer","tankdestroyer",
    "terrorist","demotruck","nighthawk","dolphin","squid","robottank","battlefortress",
    "hornet","navyseal","yuri","chronocommando","psicommando","initiate","brute",
    "virus","lashertank","gatlingtank","magnetron","mastermind","floatingdisc",
    "boomer","boris","siegechopper","chaosdrone"]
INFANTRY = {"gi","conscript","pla","engineer","attackdog","spy","flaktrooper",
    "teslatrooper","sniper","tanya","desolator","chrono","guardiangi","crazyivan",
    "terrorist","navyseal","yuri","chronocommando","psicommando","initiate","brute",
    "virus","boris","rocketeer"}
MINERS = {"harvester","chronominer","warminer"}

BLDS = ["conyard","powerplant","teslareactor","nuclearreactor","barracks","warfactory",
    "orerefinery","radar","battlelab","airforcecmd","navalyard","pillbox","sentrygun",
    "prismtower","teslacoil","flakcannon","grandcannon","patriotmissile","wall",
    "orepurifier","industrialplant","nukesilo","weatherdevice","ironcurtain",
    "chronosphere","oilderrick","hospital","machineshop","cloningvat","servicedepot",
    "gapgenerator","spysat","psychicsensor","battlebunker","tankbunker","techairport",
    "secretlab","civhouse","bioreactor","gatlingcannon","grinder","geneticmutator",
    "psychicdominator","psychictower","techpowerplant","techoutpost"]

def has(p): return os.path.exists(os.path.join(SPR, p))

print("== 单位缺失报告 ==")
missing = []
for u in UNITS:
    miss = []
    frames = 2 if (u in INFANTRY or u in MINERS) else 1
    for d in range(8):
        for f in range(frames):
            if not has(f"unit_{u}_d{d}_f{f}.png"):
                # 兼容无帧命名
                if f == 0 and has(f"unit_{u}_d{d}.png"):
                    continue
                miss.append(f"d{d}f{f}")
    if miss:
        missing.append((u, miss))
        print(f"  {u}: 缺 {len(miss)} 帧 -> {miss[:8]}{'...' if len(miss)>8 else ''}")
if not missing:
    print("  全部单位 8 方向帧齐")

print("\n== 炮塔缺失报告（应有炮塔的单位） ==")
TURRETED = ["grizzly","rhino","type99","prismtank","teslatank","miragetank","apocalypse",
    "tankdestroyer","ifv","flaktrack","v3launcher","lashertank","gatlingtank","magnetron",
    "mastermind","robottank","battlefortress","destroyer","aegis","dreadnought",
    "aircraftcarrier","intruder","mig","blackeagle","nighthawk","siegechopper"]
for u in TURRETED:
    miss = [f"d{d}" for d in range(8) if not has(f"turret_{u}_d{d}.png")]
    if miss:
        print(f"  {u}: 缺炮塔 {miss}")

print("\n== 建筑缺失报告 ==")
for b in BLDS:
    if not has(f"bld_{b}.png"):
        print(f"  {b}: 缺 bld_{b}.png")

# ---- 生成 montage：每单位 d2（朝南）f0 帧缩略 + 名字 ----
print("\n== 生成单位对照图 audit_units.png ==")
cell_w, cell_h = 96, 96
cols = 8
rows = (len(UNITS) + cols - 1) // cols
from PIL import ImageDraw
m = Image.new("RGB", (cols * cell_w, rows * cell_h), (30, 30, 34))
dr = ImageDraw.Draw(m)
for i, u in enumerate(UNITS):
    p = os.path.join(SPR, f"unit_{u}_d2_f0.png")
    if not os.path.exists(p):
        p = os.path.join(SPR, f"unit_{u}_d2.png")
    x0, y0 = (i % cols) * cell_w, (i // cols) * cell_h
    if os.path.exists(p):
        im = Image.open(p).convert("RGBA")
        s = min((cell_w - 8) / im.width, (cell_h - 20) / im.height, 2.0)
        im = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.NEAREST)
        bg = Image.new("RGBA", (cell_w, cell_h - 14), (48, 60, 44, 255))
        bg.paste(im, ((cell_w - im.width) // 2, (cell_h - 14 - im.height) // 2), im)
        m.paste(bg, (x0, y0))
    else:
        dr.rectangle([x0, y0, x0 + cell_w - 1, y0 + cell_h - 15], outline=(200, 40, 40))
        dr.text((x0 + 4, y0 + 30), "MISSING", fill=(255, 80, 80))
    dr.text((x0 + 4, y0 + cell_h - 13), u, fill=(230, 230, 230))
m.save(os.path.join(OUT, "audit_units.png"))

print("== 生成建筑对照图 audit_blds.png ==")
cols = 8
rows = (len(BLDS) + cols - 1) // cols
m = Image.new("RGB", (cols * cell_w, rows * cell_h), (30, 30, 34))
dr = ImageDraw.Draw(m)
for i, b in enumerate(BLDS):
    p = os.path.join(SPR, f"bld_{b}.png")
    x0, y0 = (i % cols) * cell_w, (i // cols) * cell_h
    if os.path.exists(p):
        im = Image.open(p).convert("RGBA")
        s = min((cell_w - 8) / im.width, (cell_h - 20) / im.height, 1.6)
        im = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.NEAREST)
        bg = Image.new("RGBA", (cell_w, cell_h - 14), (48, 60, 44, 255))
        bg.paste(im, ((cell_w - im.width) // 2, (cell_h - 14 - im.height) // 2), im)
        m.paste(bg, (x0, y0))
    else:
        dr.rectangle([x0, y0, x0 + cell_w - 1, y0 + cell_h - 15], outline=(200, 40, 40))
        dr.text((x0 + 4, y0 + 30), "MISSING", fill=(255, 80, 80))
    dr.text((x0 + 4, y0 + cell_h - 13), b, fill=(230, 230, 230))
m.save(os.path.join(OUT, "audit_blds.png"))
print("完成 -> tools/ra2pack/out/audit_units.png, audit_blds.png")
