import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, rules = t.find("rules.ini")
txt = rules.decode("latin-1")

# dump raw sections
for sid in ["APOC", "SMCV", "AMCV", "MGTK", "DTRUCK", "GASPYSAT"]:
    m = re.search(r"^\[" + sid + r"\]\s*$", txt, re.M)
    if not m:
        print(f"[{sid}] not found"); continue
    start = m.start()
    nxt = txt.find("\n[", start + 1)
    body = txt[start:nxt if nxt > 0 else len(txt)]
    keep = [l for l in body.splitlines() if re.match(r"\s*(Image|UIName|Name|Cameo|Voxel)\s*=", l) or l.startswith("[")]
    print("\n".join(keep))
    print("-" * 40)

def has_f(name): return t.find(name)[1] is not None
print("gaspst.shp:", has_f("gaspst.shp"), " gaspstmk.shp:", has_f("gaspstmk.shp"))
print("rtnk.vxl:", has_f("rtnk.vxl"), " rtnk.hva:", has_f("rtnk.hva"))
print("trucka.vxl:", has_f("trucka.vxl"), " trucka.hva:", has_f("trucka.hva"))
print("mcv.vxl:", has_f("mcv.vxl"), " mcv.hva:", has_f("mcv.hva"))
