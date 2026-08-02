import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, rules = t.find("rules.ini")
_, art = t.find("art.ini")

def parse_ini(txt):
    secs = {}; cur = None
    for line in txt.splitlines():
        s = line.strip()
        if not s or s.startswith(";"): continue
        m = re.match(r"\[(.+)\]", s)
        if m: cur = m.group(1); secs.setdefault(cur, {}); continue
        if "=" in s and cur is not None:
            k, v = s.split("=", 1)
            v = v.split(";")[0].strip()
            secs[cur][k.strip()] = v
    return secs
R = parse_ini(rules.decode("latin-1"))
A = parse_ini(art.decode("latin-1"))

def has_f(name): return t.find(name)[1] is not None

# probe tricky building names
print("== tricky probes ==")
for n in ["gaspysat.shp", "gtspysat.shp", "gaspysat .shp", "gaweth.shp", "gawethmk.shp",
          "gaweat.shp", "nadept.shp", "nahpad.shp", "nawast.shp",
          "gtnkicon.shp", "apocicon.shp", "amcvicon.shp", "dtrkicon.shp", "migicon.shp"]:
    print(f"  {n:22s} {'YES' if has_f(n) else 'no'}")

# rules GASPYSAT / GAWEAT raw
for sid in ["GASPYSAT", "GAWEAT", "GTGCAN", "AMCV", "APOC", "MGTK", "DTRUCK", "MIG"]:
    sec = R.get(sid, {})
    print(f"  rules[{sid}]: Image={sec.get('Image','<none>')!r} Name={sec.get('Name','?')!r}")
    asec = A.get(sid, {})
    print(f"  art[{sid}]:   Image={asec.get('Image','<none>')!r} Cameo={asec.get('Cameo','<none>')!r} NewTheater={asec.get('NewTheater','<none>')!r} Voxel={asec.get('Voxel','<none>')!r}")
