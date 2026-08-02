import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db

t = MixTree()
db = name_db()
mf = t.by_name["ra2.mix/isotemp.mix"]
hits = sorted(db[eid] for eid in mf.index if eid in db)
shps = [n for n in hits if n.endswith(".shp")]
print("isotemp shp count:", len(shps))
print(shps[:120])
