import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_hash

t = MixTree()
_, art = t.find("art.ini")
txt = art.decode("latin-1")

for sid in ["MTNK", "RTNK", "MCV", "SMCV", "TRUCKA"]:
    m = re.search(r"^\[" + sid + r"\]\s*$", txt, re.M)
    if not m:
        print(f"[{sid}] not in art.ini"); continue
    start = m.start()
    nxt = txt.find("\n[", start + 1)
    print(txt[start:nxt if nxt > 0 else len(txt)].strip())
    print("-" * 40)

# probe every conceivable apoc filename variant across the whole tree
names = ["apoc.vxl", "apoc.hva", "apocs.vxl", "apocmk.shp", "apoc.shp",
         "4tnk.vxl", "htnk2.vxl", "apoc2.vxl"]
for n in names:
    h = name_hash(n)
    found = [mf.name for mf in t.mixes if h in mf.index]
    print(f"  {n:14s} {found if found else 'no'}")
