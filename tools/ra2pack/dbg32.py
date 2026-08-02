import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db

t = MixTree()
db = name_db()
found = {}  # name -> mix
for mf in t.mixes:
    for eid in mf.index:
        n = db.get(eid)
        if n:
            found.setdefault(n, mf.name)

shps = sorted(n for n in found if n.endswith(".shp"))
vxls = sorted(n for n in found if n.endswith(".vxl") or n.endswith(".hva"))
pals = sorted(n for n in found if n.endswith(".pal"))
print("SHP present (%d):" % len(shps))
for n in shps:
    print("  ", n, found[n])
print("\nVXL/HVA present (%d):" % len(vxls))
for n in vxls:
    print("  ", n, found[n])
print("\nPAL present (%d):" % len(pals))
for n in pals:
    print("  ", n, found[n])
