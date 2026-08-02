import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_hash

t = MixTree()
_, _a = t.find("art.ini")
_, _r = t.find("rules.ini")
RT = _r.decode("latin-1", "replace"); AT = _a.decode("latin-1", "replace")

# 1) art.ini [GARADR] 整节
m = re.search(r"\[GARADR\](.*?)(?=\n\[)", AT, re.S)
print("[GARADR] art:", m.group(1).strip()[:400] if m else None)
m = re.search(r"\[GTGCAN\](.*?)(?=\n\[)", AT, re.S)
print("[GTGCAN] art:", m.group(1).strip()[:400] if m else None)

# 2) rules.ini 中含 Radar/GARADR 的行
for pat in ["GARADR", "RadarDome", "NARADR"]:
    for line in RT.splitlines():
        if pat.lower() in line.lower() and not line.strip().startswith(";"):
            print("rules>", line.strip()[:120])

# 3) 直接按哈希探测候选文件
cands = ["garadr.shp", "garadr_a.shp", "garadrmk.shp", "gagcan.shp", "gagcan_a.shp",
         "gagcanmk.shp", "gcanicon.shp", "radricon.shp", "gtgcantur.vxl", "gtgcanbarl.vxl",
         "oilicon.shp", "hospicon.shp", "machicon.shp", "outpicon.shp", "airpicon.shp",
         "slabicon.shp", "cahseicon.shp", "capowricon.shp", "oildicon.shp"]
for c in cands:
    src, d = t.find(c)
    print(f"  {c}: {'Y ' + src if d else 'N'}")
