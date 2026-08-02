import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, rules = t.find("rules.ini")
_, art = t.find("art.ini")
rules_txt = rules.decode("latin-1"); art_txt = art.decode("latin-1")

def parse_ini(txt):
    secs = {}; cur = None
    for line in txt.splitlines():
        line = line.strip()
        if not line or line.startswith(";"): continue
        m = re.match(r"\[(.+)\]", line)
        if m: cur = m.group(1); secs.setdefault(cur, {}); continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1); secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs
R = parse_ini(rules_txt); A = parse_ini(art_txt)

# engine name -> candidate RA2 ids (first existing wins)
UNITS = {
    # infantry
    "gi": ["E1"], "conscript": ["E2"], "pla": [], "engineer": ["ENGINEER"],
    "attackdog": ["ADOG", "DOG"], "spy": ["SPY"], "flaktrooper": ["FLAKT"],
    "teslatrooper": ["SHK"], "sniper": ["SNIPE"], "tanya": ["TANY"],
    "desolator": ["DESO"], "chrono": ["CLEG"], "guardiangi": ["GGI"],
    "crazyivan": ["IVAN"], "terrorist": ["TERROR"], "navyseal": ["GHOST"],
    "yuri": ["YURI"], "chronocommando": ["CCOMAND"], "psicommando": ["PTROOP"],
    "initiate": [], "brute": [], "virus": [], "boris": [],
    "rocketeer": ["JUMPJET"],
    # vehicles
    "mcv": ["AMCV", "SMCV"], "harvester": ["HARV"], "chronominer": ["CMIN"], "warminer": ["HARV"],
    "grizzly": ["MTNK"], "rhino": ["HTNK"], "type99": [], "flaktrack": ["HTK"],
    "ifv": ["FV"], "prismtank": ["SREF"], "teslatank": ["TTNK"], "miragetank": ["MGTK"],
    "v3launcher": ["V3"], "apocalypse": ["APOC"], "terrordrone": ["DRON"],
    "demotruck": ["DTRUCK"], "robottank": ["ROBO"], "battlefortress": ["BFRT"],
    "tankdestroyer": ["TNKD"],
    # aircraft
    "intruder": ["ORCA"], "mig": ["MIG"], "blackeagle": ["BEAG"], "kirov": ["ZEP"],
    "nighthawk": ["SHAD"], "hornet": ["HORNET"],
    # ships
    "destroyer": ["DEST"], "typhoon": ["SUB"], "aegis": ["AEGIS"],
    "seascorpion": ["HYD"], "dreadnought": ["DRED"], "aircraftcarrier": ["CARRIER"],
    "amphtransport": ["SAPC", "LCRF"], "dolphin": ["DLPH"], "squid": ["SQD"],
    # yuri faction (YR only)
    "lashertank": [], "gatlingtank": [], "magnetron": [], "mastermind": [],
    "floatingdisc": [], "boomer": [], "siegechopper": [], "chaosdrone": [],
}
BLDS = {
    "conyard": ["GACNST"], "powerplant": ["GAPOWR"], "teslareactor": ["NAPOWR"],
    "nuclearreactor": ["NANRCT"], "barracks": ["GAPILE"], "warfactory": ["GAWEAP"],
    "orerefinery": ["GAREFN"], "radar": ["GARADR"], "battlelab": ["GATECH"],
    "airforcecmd": ["GAAIRC"], "navalyard": ["GAYARD"], "pillbox": ["GAPILL"],
    "sentrygun": ["NALASR"], "prismtower": ["ATESLA"], "teslacoil": ["TESLA"],
    "flakcannon": ["NAFLAK"], "grandcannon": ["GTGCAN"], "patriotmissile": ["NASAM"],
    "wall": ["GAWALL"], "orepurifier": ["GAOREP"], "industrialplant": ["NAINDP"],
    "nukesilo": ["NAMISL"], "weatherdevice": ["GAWEAT"], "ironcurtain": ["NAIRON"],
    "chronosphere": ["GACSPH"], "oilderrick": ["CAOILD"], "hospital": ["CATHOSP"],
    "machineshop": ["CAMACH", "CAOUTP"], "cloningvat": ["NACLON"], "servicedepot": ["GADEPT"],
    "gapgenerator": ["GAGAP"], "spysat": ["GASPYSAT"], "psychicsensor": ["NAPSIS"],
    "battlebunker": [], "tankbunker": [], "techairport": ["CAAIRP"],
    "secretlab": ["CASLAB", "CALAB"], "civhouse": ["CAHSE01"],
    "bioreactor": [], "gatlingcannon": [], "grinder": [], "geneticmutator": [],
    "psychicdominator": [], "psychictower": [], "techpowerplant": ["CAPOWR"],
    "techoutpost": ["CAOUTP"],
}

def has_f(name): return t.find(name)[1] is not None

print("== UNITS ==")
for eng, cands in UNITS.items():
    rid = next((c for c in cands if c in R), None)
    if not rid:
        print(f"  {eng:16s} -> SKIP (no RA2 id)")
        continue
    img = A.get(rid, {}).get("Image", rid)
    voxel = A.get(rid, {}).get("Voxel", "no").lower() == "yes"
    low = img.lower()
    if voxel:
        ok = has_f(low + ".vxl"); hva_ok = has_f(low + ".hva")
        print(f"  {eng:16s} -> {rid:10s} vxl={ok} hva={hva_ok}  ({img})")
    else:
        ok = has_f(low + ".shp")
        print(f"  {eng:16s} -> {rid:10s} shp={ok}  ({img})")
print("== BUILDINGS ==")
for eng, cands in BLDS.items():
    rid = next((c for c in cands if c in R), None)
    if not rid:
        print(f"  {eng:16s} -> SKIP")
        continue
    img = A.get(rid, {}).get("Image", rid)
    low = img.lower()
    ok = has_f(low + ".shp")
    mk = has_f(low[:2] + "t" + low[2:] + "mk.shp") or has_f(low + "mk.shp")
    print(f"  {eng:16s} -> {rid:10s} shp={ok} mk={mk}  ({img})")
