# 找出尺寸异常小的素材（疑似占位/程序回退 PNG）
import os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")

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

BLDS = ["conyard","powerplant","teslareactor","nuclearreactor","barracks","warfactory",
    "orerefinery","radar","battlelab","airforcecmd","navalyard","pillbox","sentrygun",
    "prismtower","teslacoil","flakcannon","grandcannon","patriotmissile","wall",
    "orepurifier","industrialplant","nukesilo","weatherdevice","ironcurtain",
    "chronosphere","oilderrick","hospital","machineshop","cloningvat","servicedepot",
    "gapgenerator","spysat","psychicsensor","battlebunker","tankbunker","techairport",
    "secretlab","civhouse","bioreactor","gatlingcannon","grinder","geneticmutator",
    "psychicdominator","psychictower","techpowerplant","techoutpost"]

print("== 单位 d0_f0 文件尺寸（按字节升序，过小=疑似占位） ==")
rows = []
for u in UNITS:
    p = os.path.join(SPR, f"unit_{u}_d0_f0.png")
    if not os.path.exists(p):
        p = os.path.join(SPR, f"unit_{u}_d0.png")
    if os.path.exists(p):
        sz = os.path.getsize(p)
        with Image.open(p) as im:
            w, h = im.size
        rows.append((sz, u, w, h))
rows.sort()
threshold = 1500
for sz, u, w, h in rows:
    flag = " <<< 疑似占位" if sz < threshold else ""
    kind = "步兵" if u in INFANTRY else "载具"
    print(f"  {sz:6d} B  {w:3d}x{h:<3d} {kind} {u}{flag}")

print("\n== 建筑文件尺寸 ==")
rows = []
for b in BLDS:
    p = os.path.join(SPR, f"bld_{b}.png")
    if os.path.exists(p):
        sz = os.path.getsize(p)
        with Image.open(p) as im:
            w, h = im.size
        rows.append((sz, b, w, h))
rows.sort()
for sz, b, w, h in rows:
    flag = " <<< 疑似占位" if sz < 1500 else ""
    print(f"  {sz:6d} B  {w:3d}x{h:<3d} {b}{flag}")
