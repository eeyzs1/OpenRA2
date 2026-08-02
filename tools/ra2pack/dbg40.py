import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_hash

t = MixTree()
for target in ("rules.ini", "art.ini"):
    print("=====", target)
    for mf in t.mixes:
        data = mf.get(target)
        if data is not None:
            txt = data.decode("latin-1", "replace")
            nsec = len(re.findall(r"^\[", txt, re.M))
            print(f"  {mf.name:40s} size={len(data):8d} sections={nsec}")
