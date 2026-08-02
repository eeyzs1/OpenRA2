import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()

cands = [
    # apocalypse variants
    "apoc", "apocs", "mam", "mamm", "mammoth", "mmch", "mmchtur", "apoctur", "apocbarl",
    "mtank", "hvytank", "apocv",
    # mirage variants
    "mgtk", "mgta", "mirage", "mrag", "mgtank", "mrj", "mrjtur",
    # demo truck variants
    "dtruck", "demotr", "demo", "dtrk", "truckd", "demotruk", "trukd",
    # mig variants
    "mig", "mig25", "mig29", "migv", "migjet",
    # buildings
    "tesla", "ntesla", "ntsla", "natesla", "nacoil", "tescol", "tesco", "ntcoil",
    "gtgcan", "gagcan", "grandcan", "gcannon", "gcan",
    "gaweat", "gaweath", "gaweth", "weather", "gawea",
    "gaspysat", "gapsat", "gaspy", "spysat", "gaspys",
    "naindp", "ngindp", "naind", "indplant",
    "capowr", "cathpow", "capowr2", "capowr03",
    "garadr", "naradr", "saradr", "uaradr", "taradr", "ga radar".replace(" ", ""),
    # other
    "ggi", "dron", "sqd", "dlph", "dlfin",
]
print("== extended probes ==")
for c in cands:
    found = []
    for ext in (".vxl", ".hva", ".shp"):
        m, d = t.find(c + ext)
        if d:
            found.append((c + ext, m, len(d)))
    if found:
        for f in found:
            print("  ", f)
    else:
        print("%-12s NONE" % c)
