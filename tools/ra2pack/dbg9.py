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

def hash_std(name):   # zlib crc32 (init FFFFFFFF, xorout FFFFFFFF)
    return zlib.crc32(ts_pad(name)) & 0xFFFFFFFF

# dump entry ids of language.mix and ra2.mix
for mf_name in ("LANGUAGE.MIX", "RA2.MIX"):
    d = open(os.path.join(GAME_DIR, mf_name), "rb").read()
    mf = MixFile(d, mf_name)
    print(f"== {mf_name}: {mf.count} files, body {mf.body_size}")
    for eid, e in sorted(mf.index.items()):
        print(f"  id={eid:08x} off={e.offset:10d} size={e.size:10d}")

cands = ["local mix database.dat", "ra2.csf", "ra2md.csf", "subtitle.txt", "local.mix",
         "conquer.mix", "cache.mix", "generic.mix", "temperat.mix", "snow.mix", "urban.mix",
         "urbann.mix", "desert.mix", "lunar.mix", "cameo.mix", "isotem.mix", "isosnow.mix",
         "isourb.mix", "isourbn.mix", "isodes.mix", "isolun.mix", "isogen.mix", "neutral.mix",
         "audio.mix", "sounds.mix", "speech.mix", "taunts.mix", "sidec01.mix", "sidec02.mix",
         "sidec01md.mix", "sidec02md.mix", "genericmd.mix", "temperatmd.mix", "snowmd.mix",
         "urbanmd.mix", "urbannmd.mix", "desertmd.mix", "lunarmd.mix", "cameomd.mix",
         "rules.ini", "art.ini", "rulesmd.ini", "artmd.ini", "unittem.pal", "cameo.pal",
         "ra2.mix", "ra2md.mix", "language.mix", "maps01.mix", "multi.mix", "theme.mix",
         "thememd.mix", "wdt.mix", "keyboard.ini", "binkw32.dll", "subtitle.mix"]
print("== candidate hashes (zlib crc32) ==")
for c in cands:
    print(f"  {hash_std(c):08x}  {c}")
