import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
print("== mix tree ==")
for mf in t.mixes:
    print(f"  {mf.name:50s} count={mf.count:5d} body={mf.body_size}")

_, rules = t.find("rules.ini")
txt = rules.decode("latin-1")

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
R = parse_ini(txt)

def idlist(name):
    out = {}
    sec = R.get(name, {})
    for k, v in sec.items():
        out[v] = True
    return list(out.keys())

veh = idlist("VehicleTypes"); inf = idlist("InfantryTypes"); air = idlist("AircraftTypes"); bld = idlist("BuildingTypes")
print(f"\nVehicleTypes={len(veh)} InfantryTypes={len(inf)} AircraftTypes={len(air)} BuildingTypes={len(bld)}")

def has_f(name): return t.find(name)[1] is not None

print("\n== vehicles (rules Image= -> vxl?) ==")
for vid in veh:
    img = R.get(vid, {}).get("Image", vid).lower()
    v = has_f(img + ".vxl"); h = has_f(img + ".hva")
    if not v:
        print(f"  {vid:10s} img={img:12s} vxl={v} hva={h}")
print("\n== aircraft ==")
for vid in air:
    img = R.get(vid, {}).get("Image", vid).lower()
    v = has_f(img + ".vxl"); h = has_f(img + ".hva"); s = has_f(img + ".shp")
    if not v and not s:
        print(f"  {vid:10s} img={img:12s} vxl={v} hva={h} shp={s}")
print("\n== infantry missing ==")
for vid in inf:
    img = R.get(vid, {}).get("Image", vid).lower()
    s = has_f(img + ".shp")
    if not s:
        print(f"  {vid:10s} img={img:12s} shp={s}")
print("\n== buildings missing ==")
for vid in bld:
    img = R.get(vid, {}).get("Image", vid).lower()
    # NewTheater naming: 2nd char replaced by theater; generic fallback 'A'
    names = [img + ".shp"]
    if len(img) >= 2:
        names.append(img[0] + "a" + img[2:] + ".shp")
    s = any(has_f(n) for n in names)
    if not s:
        print(f"  {vid:10s} img={img:12s} shp=False")
