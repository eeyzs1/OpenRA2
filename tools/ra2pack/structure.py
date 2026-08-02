import sys, os, zlib
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixFile, GAME_DIR

def ts_pad(name: str) -> bytes:
    name = name.upper()
    l = len(name)
    a = l >> 2
    if l & 3:
        name += chr(l - (a << 2))
        name += name[a << 2] * (3 - (l & 3))
    return name.encode("latin-1")

def name_id(name: str) -> int:
    return zlib.crc32(ts_pad(name)) & 0xFFFFFFFF

# ---- global id -> name db from XCC ra2 mix description + extras
DB = {}
desc = os.path.join(os.environ["TEMP"], "xcc_ra2mixdesc.txt")
if os.path.exists(desc):
    for line in open(desc, "r", encoding="latin-1"):
        n = line.strip().lower()
        if n and " " not in n:
            DB.setdefault(name_id(n), n)
for extra in ["local mix database.dat", "ra2.csf", "local.mix", "conquer.mix", "cache.mix",
              "generic.mix", "temperat.mix", "snow.mix", "urban.mix", "urbann.mix", "lunar.mix",
              "desert.mix", "cameo.mix", "isotem.mix", "isosnow.mix", "isourb.mix", "isourbn.mix",
              "isodes.mix", "isolun.mix", "isogen.mix", "neutral.mix", "audio.mix", "sounds.mix",
              "speech.mix", "taunts.mix", "sidec01.mix", "sidec02.mix", "rules.ini", "art.ini",
              "unittem.pal", "cameo.pal", "keyboard.ini"]:
    DB.setdefault(name_id(extra), extra)

def looks_mix(data: bytes) -> bool:
    if len(data) < 16:
        return False
    try:
        mf = MixFile(data, "probe")
        return mf.count > 0
    except Exception:
        return False

seen = {}
def expand(name: str, data: bytes, depth: int, out_lines):
    pad = "  " * depth
    try:
        mf = MixFile(data, name)
    except Exception as ex:
        out_lines.append(f"{pad}{name}: PARSE FAIL {ex}")
        return
    hits = []
    nested = []
    for eid, e in mf.index.items():
        nm = DB.get(eid)
        if nm:
            hits.append(nm)
            if nm.endswith(".mix"):
                nested.append((nm, e))
    out_lines.append(f"{pad}{name}: files={mf.count} body={mf.body_size} named={len(hits)}")
    # recurse nested mixes
    for nm, e in sorted(nested):
        sub = data[mf.body_base + e.offset: mf.body_base + e.offset + e.size]
        expand(nm, sub, depth + 1, out_lines)
    # unnamed large entries -> maybe nested mix without known name
    for eid, e in mf.index.items():
        if eid in DB or e.size < 2000:
            continue
        sub = data[mf.body_base + e.offset: mf.body_base + e.offset + e.size]
        if looks_mix(sub):
            expand(f"?{eid:08x}", sub, depth + 1, out_lines)
    interesting = [h for h in sorted(hits) if not h.endswith(".mix")][:12]
    if interesting:
        out_lines.append(f"{pad}  e.g.: {', '.join(interesting)}")

lines = []
for top in ["RA2.MIX", "LANGUAGE.MIX"]:
    expand(top.lower(), open(os.path.join(GAME_DIR, top), "rb").read(), 0, lines)
print("\n".join(lines))
