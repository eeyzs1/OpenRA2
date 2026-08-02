# 提取 rules.ini 中各单位的 Sequence= 与关键行为参数
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

T = MixTree()
mix, d = T.find("rules.ini")
if not d:
    print("rules.ini MISS"); raise SystemExit
txt = d.decode("latin-1", errors="replace")
outp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "rules.ini")
open(outp, "w", encoding="latin-1").write(txt)
print("rules.ini saved,", len(txt), "chars ->", outp)

UNITS = ["E1","E2","ENGINEER","ADOG","SPY","FLAKT","SHK","SNIPE","TANY","DESO","CLEG","IVAN",
         "TERROR","GHOST","YURI","CCOMAND","PTROOP","JUMPJET","DRON","DLPH","SQD",
         "AMCV","SMCV","HARV","CMIN","MTNK","HTNK","HTK","FV","SREF","TTNK","MGTK",
         "V3","APOC","DTRUCK","TNKD","ORCA","BEAG","ZEP","SHAD","HORNET","DEST","SUB",
         "AEGIS","HYD","DRED","CARRIER","SAPC","LTNK"]
secs = {}
cur = None
for line in txt.splitlines():
    s = line.strip()
    if s.startswith("[") and s.endswith("]"):
        cur = s[1:-1]
    elif cur in UNITS and "=" in s:
        k = s.split("=", 1)[0].strip()
        if k in ("Sequence", "DeployingAnim", "Primary", "Secondary", "Speed", "Locomotor",
                 "MovementZone", "DeploysInto", "UndeploysInto", "DeployFire", "CrateGoodie",
                 "Harvester", "DockUnload", "DeployTime", "DeploySound", "Turret", "FireAngle"):
            secs.setdefault(cur, []).append(s)
for u in UNITS:
    if u in secs:
        print(f"\n[{u}]")
        for l in secs[u]:
            print(" ", l)
