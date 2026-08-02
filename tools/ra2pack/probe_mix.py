import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
print("=== mixes ===")
for mf in t.mixes:
    print(f"{mf.name:60s} files={mf.count:6d} lmd_names={len(mf.names)}")
