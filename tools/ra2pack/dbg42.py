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

print("== art [E1] =="); print(sec(at, "E1") or "NOT FOUND")
print("== art [GI] ==", "EXISTS" if re.search(r"^\[GI\]", at, re.M) else "no")
print("== art [GARADR_A] =="); print(sec(at, "GARADR_A") or "NOT FOUND")
print("== rules [GARADR_A] ==", "EXISTS" if re.search(r"^\[GARADR_A\]", rt, re.M) else "no")
print("== rules [GARADR_AD] ==", "EXISTS" if re.search(r"^\[GARADR_AD\]", rt, re.M) else "no")
# rules building list around 23
m = re.search(r"^\[BuildingTypes\]\n(.*?)(?=^\[)", rt, re.M | re.S)
if m:
    lines = m.group(1).splitlines()[:30]
    print("== BuildingTypes head =="); print("\n".join(lines))
