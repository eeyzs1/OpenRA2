import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
for fn in ("gi.shp", "gacnst.shp", "mtnkicon.shp"):
    where, d = t.find(fn)
    print(fn, "from", where, "size", len(d))
    print("  head64:", d[:64].hex(" "))
