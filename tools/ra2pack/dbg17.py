import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db

t = MixTree()
_, art = t.find("art.ini")
art_txt = art.decode("latin-1")
_, rules = t.find("rules.ini")
rules_txt = rules.decode("latin-1")

def parse_ini(txt):
    secs = {}; cur = None
    for line in txt.splitlines():
        s = line.strip()
        if not s or s.startswith(";"): continue
        m = re.match(r"\[(.+)\]", s)
        if m: cur = m.group(1); secs.setdefault(cur, {}); continue
        if "=" in s and cur is not None:
            k, v = s.split("=", 1); secs[cur][k.strip()] = v.strip()
    return secs
A = parse_ini(art_txt); R = parse_ini(rules_txt)

for sid in ["E1","E2","TERROR","GHOST","JUMPJET","APOC","MGTK","AMCV","DTRUCK","ORCA","SAPC",
            "ATESLA","TESLA","NASAM","NALASR","NAINDP","GAWEAT","GASPYSAT","GARADR","GTGCAN"]:
    a = A.get(sid)
    r = R.get(sid)
    name = (r or {}).get("Name", (r or {}).get("UIName", "?"))
    print(f"[{sid}] rules_name={name}")
    if a:
        keep = {k: v for k, v in a.items() if k.lower() in ("image", "voxel", "cameo", "altcameo", "remapable", "foundation", "newtheater")}
        print("   art:", keep if keep else dict(list(a.items())[:6]))
    else:
        print("   art: <no section>")

print("\n== name db search ==")
db = name_db()
for pat in ("tesla", "pris", "sam", "weat", "psat", "radar", "radr", "seal", "ghost", "jump", "rock", "terror", "trst", "e2.shp", "gi.shp", "apoc", "mgtk", "amcv", "dtruck", "orca", "sapc", "indp", "gcan"):
    hits = sorted(n for n in db.values() if pat in n and (n.endswith(".shp") or n.endswith(".vxl")))
    print(f"  {pat}: {hits[:14]}")
