# 检查引擎全部单位在 RA2 MIX 中的素材可用性
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree

T = MixTree()
def has(n): return T.find(n)[1] is not None

# 引擎单位 -> rules 候选 id（与 gen_assets.py 一致 + 补全缺失）
UNITS = {
    "mcv": ["AMCV", "SMCV"], "harvester": ["HARV"], "chronominer": ["CMIN"], "warminer": ["HARV"],
    "gi": ["E1"], "conscript": ["E2"], "pla": [], "engineer": ["ENGINEER"],
    "attackdog": ["ADOG", "DOG"], "spy": ["SPY"], "flaktrooper": ["FLAKT"],
    "teslatrooper": ["SHK"], "sniper": ["SNIPE"], "tanya": ["TANY"],
    "desolator": ["DESO"], "chrono": ["CLEG"], "guardiangi": ["GGI"],
    "crazyivan": ["IVAN"], "terrorist": ["TERROR"], "navyseal": ["GHOST"],
    "yuri": ["YURI"], "chronocommando": ["CCOMAND"], "psicommando": ["PTROOP"],
    "initiate": ["INIT"], "brute": ["BRUTE"], "virus": ["VIRUS"], "boris": ["BORIS"],
    "rocketeer": ["JUMPJET"],
    "grizzly": ["MTNK"], "rhino": ["HTNK"], "type99": [], "flaktrack": ["HTK"],
    "ifv": ["FV"], "prismtank": ["SREF"], "teslatank": ["TTNK"], "miragetank": ["MGTK"],
    "v3launcher": ["V3"], "apocalypse": ["APOC"], "terrordrone": ["DRON"],
    "tankdestroyer": ["TNKD"], "demotruck": ["DTRUCK"], "robottank": ["ROBO"],
    "battlefortress": ["BFRT"], "lashertank": ["LTNK"], "gatlingtank": ["GTNK"],
    "magnetron": ["MAG"], "mastermind": ["MIND"], "chaosdrone": ["CADRN"],
    "intruder": ["ORCA"], "mig": ["MIG"], "blackeagle": ["BEAG"], "kirov": ["ZEP"],
    "nighthawk": ["SHAD"], "hornet": ["HORNET"], "siegechopper": ["SCHP"],
    "floatingdisc": ["DISK"],
    "destroyer": ["DEST"], "typhoon": ["SUB"], "aegis": ["AEGIS"],
    "seascorpion": ["HYD"], "dreadnought": ["DRED"], "aircraftcarrier": ["CARRIER"],
    "amphtransport": ["SAPC"], "dolphin": ["DLPH"], "squid": ["SQD"], "boomer": ["BSUB"],
}
import re
_, _r = T.find("rules.ini"); _, _a = T.find("art.ini")
RT = _r.decode("latin-1", "replace"); AT = _a.decode("latin-1", "replace")
def parse_ini(txt):
    secs = {}; cur = None
    for line in txt.splitlines():
        line = line.strip()
        if not line or line.startswith(";"): continue
        m = re.match(r"\[(.+?)\]", line)
        if m: cur = m.group(1); secs.setdefault(cur, {}); continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1)
            secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs
R = parse_ini(RT)

print("单位          rules_id   Image      vxl  shp  hva   备注")
no_asset = []
for eng, cands in UNITS.items():
    rid = next((c for c in cands if c in R), None)
    if not rid:
        print(f"{eng:14s} -          -          -    -    -     *** MIX 无此单位 ***")
        no_asset.append(eng)
        continue
    img = R[rid].get("Image", rid).lower()
    v = has(img + ".vxl"); s = has(img + ".shp"); h = has(img + ".hva")
    print(f"{eng:14s} {rid:10s} {img:10s} {'Y' if v else '-':4s} {'Y' if s else '-':4s} {'Y' if h else '-':5s}")
    if not v and not s:
        no_asset.append(eng)
print("\n无素材单位:", ", ".join(no_asset) if no_asset else "无")
