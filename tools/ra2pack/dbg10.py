import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db

t = MixTree()
# what do the ini files call the GI / grizzly?
_, rules = t.find("rules.ini")
_, art = t.find("art.ini")
rt = rules.decode("latin-1")
at = art.decode("latin-1")

def section(txt, name):
    out = []
    cur = None
    for line in txt.splitlines():
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            cur = line[1:-1]
        elif cur == name and "=" in line:
            out.append(line)
        elif cur == name and line.startswith(";"):
            out.append(line)
    return out

for sec in ["GI", "MTNK", "GACNST", "E1"]:
    print(f"--- rules [{sec}] ---")
    for l in section(rt, sec)[:8]:
        print("  ", l)
for sec in ["GI", "GGI", "MTNK", "GACNST"]:
    print(f"--- art [{sec}] ---")
    for l in section(at, sec)[:12]:
        print("  ", l)

# find infantry shp names in conquer.mix
db = name_db()
cq = t.by_name.get("ra2.mix/conquer.mix")
names = sorted(n for eid, n in ((e, db.get(e)) for e in cq.index) if n)
inf = [n for n in names if n.endswith(".shp")]
print("conquer.shp count:", len(inf))
print([n for n in inf if "ggi" in n or n.startswith("gi")])
print("sample:", inf[:40])
