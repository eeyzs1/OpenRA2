import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()

# 1) probe missing voxels by direct hash
probes = ["apoc", "mmch", "mmchtur", "apoctur", "apocbarl", "mmchbarl", "mgtk", "mgtktur",
          "robo", "robotur", "dtruck", "dron", "orca", "orcab", "sapc", "lcrf", "amcv", "smcv",
          "mcv", "fortress", "bfrt", "zep", "beag", "mig", "cleg", "shad", "hornet",
          "dest", "sub", "subt", "aegis", "hyd", "dred", "carrier", "sqd", "dlph", "sonic"]
print("== direct vxl probes ==")
for p in probes:
    mv, dv = t.find(p + ".vxl")
    mh, dh = t.find(p + ".hva")
    print("%-10s vxl=%-3s hva=%-3s %s" % (p, "Y" if dv else "-", "Y" if dh else "-", mv or ""))

# 2) probe infantry shp candidates
print("\n== infantry shp probes ==")
infc = {
    "gi": ["gi", "e1"], "conscript": ["e2", "cons", "conscript", "e2cons"],
    "guardiangi": ["ggi", "ggi Deploy".replace(" ", "")],
    "terrorist": ["trst", "terror"], "navyseal": ["ghost", "seal"],
    "rocketeer": ["jumpjet", "jjet", "rock"],
    "initiate": ["init"], "brute": ["brute"], "virus": ["virus"], "boris": ["boris"],
    "pla": ["pla"], "engineer": ["engineer", "engr"], "spy": ["spy"],
    "attackdog": ["adog", "dog"], "flaktrooper": ["flakt"], "teslatrooper": ["shk"],
    "sniper": ["snipe"], "tanya": ["tany"], "desolator": ["deso"],
    "chrono": ["cleg"], "crazyivan": ["ivan"], "yuri": ["yuri"],
    "chronocommando": ["ccomand"], "psicommando": ["ptroop"],
}
for eng, cands in infc.items():
    hits = []
    for c in cands:
        m, d = t.find(c + ".shp")
        if d:
            hits.append((c, m, len(d)))
    print("%-14s %s" % (eng, hits if hits else "NONE"))

# 3) probe building shp candidates for the False ones
print("\n== building shp probes ==")
bldc = {
    "radar": ["garadr", "garadar", "naradr", "gapsat", "gaspysat"],
    "prismtower": ["atesla", "gapris", "gprism"],
    "teslacoil": ["tesla", "natesla", "nacoil"],
    "grandcannon": ["gtgcan"],
    "weatherdevice": ["gaweat"],
    "spysat": ["gaspysat", "gaspysat"],
    "industrialplant": ["naindp"],
    "techpowerplant": ["capowr", "capowr2", "cathpow"],
}
for eng, cands in bldc.items():
    hits = []
    for c in cands:
        m, d = t.find(c + ".shp")
        if d:
            hits.append((c, m, len(d)))
    print("%-16s %s" % (eng, hits if hits else "NONE"))
