import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Hva, Vxl

t = MixTree()
for base in ["gtnk", "mtnk", "htnk", "ttnk", "sref", "fv", "harv", "v3", "mcv"]:
    _, vd = t.find(base + ".vxl")
    _, hd = t.find(base + ".hva")
    v = Vxl(vd) if vd else None
    h = Hva(hd) if hd else None
    vsecs = [(s.name, s.size) for s in v.sections] if v else None
    print(f"{base:6s} vxl_secs={vsecs}")
    if h and h.valid:
        print(f"        hva: nframes={h.nframes} nsec={h.nsec} names={h.sec_names}")
    else:
        print(f"        hva: invalid/missing")
