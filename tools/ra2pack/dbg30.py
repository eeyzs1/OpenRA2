import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import name_db, MixTree

db = name_db()
turs = sorted(n for n in db.values() if n.endswith("tur.vxl"))
print("all *tur.vxl:", turs)
print()
t = MixTree()
cands = ["1tnktur","2tnktur","3tnktur","4tnktur","gtnktur","mtnktur","htnktur",
         "ttnktur","sreftur","fvtur","htktur","mgtktur","robotur","apoctur",
         "tnkdtur","desttur","subtur"]
for c in cands:
    mix, d = t.find(c + ".vxl")
    mix2, d2 = t.find(c + ".hva")
    print("%-12s vxl=%-3s hva=%-3s %s" % (c, "YES" if d else "-", "YES" if d2 else "-", mix or ""))

# also: which vxl/hva names exist in db containing tank ids
print()
for pat in ("mtnk", "htnk", "apoc", "mgtk", "sref", "tnkd", "robo", "htk", "fv"):
    hits = sorted(n for n in db.values() if n.startswith(pat) and (n.endswith(".vxl") or n.endswith(".hva")))
    print("%-6s" % pat, hits)
