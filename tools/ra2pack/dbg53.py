import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, _a = t.find("art.ini")
_, _r = t.find("rules.ini")

def parse_ini(txt):
    secs = {}; cur = None
    for line in txt.splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        m = re.match(r"\[(.+?)\]", line)
        if m:
            cur = m.group(1); secs.setdefault(cur, {}); continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1)
            secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs
A = parse_ini(_a.decode("latin-1", "replace"))
R = parse_ini(_r.decode("latin-1", "replace"))

def has(n): return t.find(n)[1] is not None

print("== rules 存在性 ==")
for rid in ["GGI", "BFRT", "MIG", "NAINDP", "CAPOWR", "GARADR", "GTGCAN"]:
    print(f"  {rid}: rules={'Y' if rid in R else 'N'} art={'Y' if rid in A else 'N'}",
          "Image=" + R.get(rid, {}).get("Image", "-"))

print("== shp 文件探测 ==")
for n in ["garadr.shp", "garadr_a.shp", "gtgcan.shp", "gtgcan_a.shp", "capowr.shp",
          "capowr_a.shp", "naindp.shp", "caradr.shp", "garadr1.shp", "naradr.shp"]:
    print(f"  {n}: {has(n)}")

print("== 科技建筑 cameo (art.ini) ==")
for sec in ["CAOILD", "CATHOSP", "CAMACH", "CAOUTP", "CAAIRP", "CALAB", "CASLAB", "CAHSE01", "CAPOWR", "GARADR", "GTGCAN"]:
    d = A.get(sec, {})
    print(f"  {sec}: Cameo={d.get('Cameo','-')} Image={d.get('Image','-')}")
