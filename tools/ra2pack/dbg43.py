import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, rules = t.find("rules.ini")
_, art = t.find("art.ini")
rt = rules.decode("latin-1", "replace").replace("\r\n", "\n")
at = art.decode("latin-1", "replace").replace("\r\n", "\n")

def sec(txt, name):
    m = re.search(r"^\[" + re.escape(name) + r"\]\n(.*?)(?=^\[)", txt, re.M | re.S)
    return m.group(0) if m else None

for nm, txt in [("rules [E1]", sec(rt, "E1")), ("rules [E2]", sec(rt, "E2")),
                ("rules [MTNK]", sec(rt, "MTNK")), ("rules [GARADR]", sec(rt, "GARADR"))]:
    print("==", nm, "==")
    print((txt[:500] if txt else "NOT FOUND"), "\n")
for nm in ["GARADR", "GACNST", "MTNK", "GI"]:
    s = sec(at, nm)
    if s:
        # 只打关键行
        keys = [ln for ln in s.splitlines() if re.match(r"(Image|Voxel|Cameo|AltCameo|NewTheater|Foundation|Height|Remapable)", ln)]
        print(f"art [{nm}]:", keys)
    else:
        print(f"art [{nm}]: NOT FOUND")
