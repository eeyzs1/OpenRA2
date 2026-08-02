import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
total_named = 0
for mf in t.mixes:
    n_entries = len(mf.index)
    n_named = len(mf.names)
    total_named += n_named
    print("%-40s entries=%-6d lmd_names=%-6d" % (mf.name, n_entries, n_named))
print("total lmd names:", total_named)

# dump all vxl/shp/hva names recovered from LMDs
exts = (".vxl", ".hva")
vxls = sorted({n for mf in t.mixes for n in mf.names if n.endswith(exts)})
print("\nVXL/HVA files present (%d):" % len(vxls))
for n in vxls:
    print("  ", n)
