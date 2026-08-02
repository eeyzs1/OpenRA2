import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, rules = t.find("rules.ini")
_, art = t.find("art.ini")

def parse_ini(txt):
    secs = {}; cur = None
    for line in txt.decode("latin-1").splitlines():
        line = line.strip()
        if not line or line.startswith(";"): continue
        m = re.match(r"\[(.+)\]", line)
        if m: cur = m.group(1); secs.setdefault(cur, {}); continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1); secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs
R = parse_ini(rules); A = parse_ini(art)

probe = ["E1", "E2", "APOC", "MGTK", "DTRUCK", "SAPC", "ORCA", "TERROR", "GHOST",
         "JUMPJET", "ROBO", "GARADR", "CAPOWR", "ATESLA", "TESLA", "GASPYSAT",
         "GAWEAT", "GTGCAN", "NALASR", "NASAM", "MIG", "BFRT", "GGI"]
for rid in probe:
    a = A.get(rid, {})
    img = a.get("Image", rid).lower()
    vox = a.get("Voxel", "no")
    cameo = a.get("Cameo", "?")
    inrules = rid in R
    def has(n): return t.find(n)[1] is not None
    files = []
    for n in (img + ".vxl", img + ".shp", rid.lower() + ".vxl", rid.lower() + ".shp",
              cameo.lower() + ".shp"):
        if has(n):
            files.append(n)
    print(f"{rid:10s} rules={inrules} Image={img:10s} Voxel={vox:3s} Cameo={cameo:12s} files={files}")

# rules sections containing keywords
for kw in ["ROBO", "GARADR", "RADAR", "CAPOWR", "POWR"]:
    hits = [s for s in R if kw in s]
    print(kw, "->", hits)
