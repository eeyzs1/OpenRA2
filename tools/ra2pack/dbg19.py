import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()

# 1) dump section names of art.ini
_, art = t.find("art.ini")
txt = art.decode("latin-1")
secs = re.findall(r"^\[(.+?)\]", txt, re.M)
print("total sections:", len(secs))
print("first 40:", secs[:40])
up = {s.upper() for s in secs}
for want in ["E1", "APOC", "MGTK", "TESLA", "ATESLA", "ORCA", "JUMPJET", "GASPYSAT", "NAINDP",
             "GISequence", "MTNK", "HARV", "GACNST", "E2", "AMCV", "DTRUCK", "MIG", "ZEP"]:
    print(f"  [{want}] in art:", want in up)

# 2) list all real filenames from every mix's embedded local mix database
print("\n== LMD filename search ==")
allnames = set()
for mf in t.mixes:
    for fn in mf.names:
        allnames.add(fn)
print("total LMD names:", len(allnames))
for pat in ("apoc", "mgtk", "amcv", "dtruck", "mig", "tesla", "pris", "spysat", "weat",
            "indp", "e1", "e2", "cons", "rock", "ggi", "jump", "seal", "trst", "gi."):
    hits = sorted(n for n in allnames if pat in n)
    print(f"  {pat:8s}: {hits[:16]}")
