import sys, os, re, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl

t = MixTree()
_, rules = t.find("rules.ini")
_, art = t.find("art.ini")
rtxt = rules.decode("latin-1"); atxt = art.decode("latin-1")

def dump(txt, sid, label):
    m = re.search(r"^\[" + sid + r"\][^\n]*$", txt, re.M)
    if not m:
        print(f"{label}[{sid}] NOT found"); return
    start = m.start()
    nxt = txt.find("\n[", start + 1)
    body = txt[start:nxt if nxt > 0 else len(txt)]
    keep = [l.strip() for l in body.splitlines() if re.match(r"\s*(Image|Name)\s*=", l) or l.strip().startswith("[")]
    print(f"{label}[{sid}]:", " | ".join(keep))

for sid in ["MTNK", "HTNK", "APOC", "TTNK", "SREF"]:
    dump(rtxt, sid, "rules")
for sid in ["3TNK", "4TNK", "HTNK"]:
    dump(atxt, sid, "art")

def has_f(n): return t.find(n)[1] is not None
for n in ["3tnk.vxl", "3tnk.hva"]:
    print(n, has_f(n))

print("\n== voxel internals ==")
for base in ["mtnk", "4tnk", "3tnk", "htnk", "mcv", "smcv"]:
    src, data = t.find(base + ".vxl")
    if not data:
        print(f"  {base}: missing"); continue
    v = Vxl(data)
    secs = [(s.name, s.size, len(s.voxels)) for s in v.sections]
    print(f"  {base}: {secs}")
