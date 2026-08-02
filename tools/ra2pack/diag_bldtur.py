# 查 art.ini 防御建筑的炮塔动画配置
import sys, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree
T = MixTree()
_, _a = T.find("art.ini"); AT = _a.decode("latin-1", "replace")
_, _r = T.find("rules.ini"); RT = _r.decode("latin-1", "replace")
for sec in ["NALASR", "GTGCAN", "NAFLAK", "GAPILL", "TESLA", "ATESLA", "NASAM", "GTGCAN"]:
    m = re.search(r"\[" + sec + r"\](.*?)(?=\n\[)", AT, re.S)
    if not m:
        print(f"== {sec}: NOT in art"); continue
    body = m.group(1)
    keys = {}
    for k in ["Cameo", "Turret", "TurretAnim", "TurretAnimX", "TurretAnimY", "TurretRecoil", "Voxel", "Shadow", "NewTheater", "Remapable", "Foundation", "Height", "AnimLow", "AnimHigh"]:
        mm = re.search(r"^" + k + r"=(.*)$", body, re.M)
        if mm:
            keys[k] = mm.group(1).strip()
    print(f"== {sec}: {keys}")
for sec in ["NALASR", "GTGCAN", "NAFLAK"]:
    m = re.search(r"\[" + sec + r"\](.*?)(?=\n\[)", RT, re.S)
    if m:
        img = re.search(r"Image=(\S+)", m.group(1))
        tur = re.search(r"Turret=(\S+)", m.group(1))
        print(f"rules {sec}: Image={img.group(1) if img else '(default)'} Turret={tur.group(1) if tur else '(none)'}")
# 候选炮塔 SHP 文件
for n in ["nalasrtur.shp", "gtgcantur.shp", "gagcantur.shp", "naflaktur.shp", "lasrtur.shp", "gcantur.shp"]:
    print(f"{n:18s}", "Y" if T.find(n)[1] else "-")
