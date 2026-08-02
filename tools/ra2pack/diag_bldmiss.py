# 查 LTNK cameo 与 NAREFN 图标文件
import sys, re, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree
T = MixTree()
_, _a = T.find("art.ini"); AT = _a.decode("latin-1", "replace")
for sec in ["LTNK", "GAREFN"]:
    m = re.search(r"\[" + sec + r"\](.*?)(?=\n\[)", AT, re.S)
    if m:
        cam = re.search(r"Cameo=(\S+)", m.group(1))
        print(f"{sec:8s} art Cameo={cam.group(1) if cam else '(none)'}")
    else:
        print(f"{sec:8s} NOT in art")
for n in ["nreficon.shp", "refnicon.shp", "narefnicon.shp", "reficon.shp", "ltnkicon.shp"]:
    print(f"{n:16s}", "Y" if T.find(n)[1] else "-")
