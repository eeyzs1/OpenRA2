import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
print("mixes loaded:", len(t.mixes))
targets = ["rules.ini", "art.ini", "unittem.pal", "unitsno.pal", "uniturb.pal", "cameo.pal",
           "ggi.shp", "mtnk.vxl", "mtnk.hva", "gacnst.shp", "grizzlyicon.shp", "mtnkicon.shp",
           "ggiicon.shp", "gacnsticon.shp", "conyardicon.shp"]
for fn in targets:
    name, data = t.find(fn)
    print(f"{fn:20s} -> {name} ({len(data) if data else '-'})")
