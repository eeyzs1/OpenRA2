import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_hash

t = MixTree()

# 1) enumerate every mix that contains art.ini / rules.ini
for target in ("art.ini", "rules.ini"):
    print(f"== all mixes containing {target} ==")
    h = name_hash(target)
    for mf in t.mixes:
        e = mf.index.get(h)
        if e:
            print(f"  {mf.name:60s} size={e.size}")

# 2) check content markers in each art.ini
import re
print("\n== art.ini section presence per source ==")
for mf in t.mixes:
    data = mf.get("art.ini")
    if not data:
        continue
    txt = data.decode("latin-1")
    marks = ["[E1]", "[APOC]", "[MGTK]", "[GASPYSAT]", "[NAINDP]", "[TESLA]", "[ATESLA]", "[ORCA]", "[JUMPJET]"]
    have = [m for m in marks if m in txt]
    print(f"  {mf.name:50s} len={len(data):7d} sections={txt.count('[')} have={have}")

# 3) direct hash existence for candidate asset filenames (db independent)
cands = [
    # voxels
    "apoc.vxl", "apoc.hva", "mgtk.vxl", "mgtk.hva", "amcv.vxl", "amcv.hva",
    "smcv.vxl", "smcv.hva", "dtruck.vxl", "dtruck.hva", "orca.vxl", "orca.hva",
    "sapc.vxl", "sapc.hva", "mig.vxl", "mig.hva",
    # infantry shp
    "gi.shp", "cons.shp", "e2.shp", "trst.shp", "seal.shp", "rock.shp",
    "jumpjet.shp", "ggi.shp",
    # buildings
    "natesla.shp", "nttesla.shp", "gttsla.shp", "gapris.shp", "gaprismk.shp",
    "gagcan.shp", "gagcanmk.shp", "naindp.shp", "naindpmk.shp",
    "gaweath.shp", "gaweat.shp", "gawe_a.shp", "gaspsat.shp", "gaspysat.shp",
    "gapsat.shp", "naradr.shp", "naradrmk.shp", "gaairc.shp",
    "mislsam.vxl", "sam.vxl",
]
print("\n== direct existence ==")
for c in cands:
    src, data = t.find(c)
    print(f"  {c:18s} {'YES ' + src if data else 'no'}")
