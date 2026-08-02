import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, hd = t.find("gtnk.hva")
print("len:", len(hd), "first 64 bytes:", hd[:64])
