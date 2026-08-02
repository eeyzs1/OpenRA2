import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
for c in ["gtradr.shp", "gsradr.shp", "guradr.shp", "gtradrmk.shp",
          "gtgcan.shp", "gsgcan.shp", "gugcan.shp", "gagcanmk.shp",
          "gtnst.shp", "naradr_a.shp", "ngradr.shp"]:
    src, d = t.find(c)
    print(f"  {c}: {'Y ' + src if d else 'N'}")
