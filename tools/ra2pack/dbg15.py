import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
src, rules = t.find("rules.ini")
print("rules.ini from", src, len(rules))
src2, art = t.find("art.ini")
print("art.ini from", src2, len(art))

rules_txt = rules.decode("latin-1")
art_txt = art.decode("latin-1")

def parse_ini(txt):
    secs = {}
    cur = None
    for line in txt.splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        m = re.match(r"\[(.+)\]", line)
        if m:
            cur = m.group(1)
            secs[cur] = {}
            continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1)
            secs[cur][k.strip()] = v.strip()
    return secs

R = parse_ini(rules_txt)
A = parse_ini(art_txt)

for lst in ("InfantryTypes", "VehicleTypes", "AircraftTypes", "BuildingTypes"):
    ids = [v for k, v in sorted(R[lst].items(), key=lambda kv: int(kv[0]) if kv[0].isdigit() else 999)]
    print(f"== {lst} ({len(ids)}):")
    for i in ids:
        img = A.get(i, {}).get("Image", i)
        voxel = A.get(i, {}).get("Voxel", "")
        cameo = A.get(i, {}).get("Cameo", "")
        print(f"  {i:12s} img={img:12s} voxel={voxel:5s} cameo={cameo}")
