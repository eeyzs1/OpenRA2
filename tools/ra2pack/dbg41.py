import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, rules = t.find("rules.ini")
_, art = t.find("art.ini")
rt = rules.decode("latin-1", "replace")
at = art.decode("latin-1", "replace")

for kw in ["GARADR", "RadarDome", "RADAR", "ROBO", "RobotTank", "CAPOWR"]:
    hits = [ln.strip() for ln in rt.splitlines() if kw.lower() in ln.lower()][:5]
    print("rules:", kw, "->", hits)
print()
# art.ini [E1] section dump
m = re.search(r"^\[E1\]\n((?:[^\[]|\n(?!\[))*?)(?=^\[)", at, re.M | re.S)
print("art [E1]:\n", m.group(0)[:600] if m else "NOT FOUND")
# rules [E1] Image-ish
m = re.search(r"^\[E1\]\n((?:[^\[]|\n(?!\[))*?)(?=^\[)", rt, re.M | re.S)
print("rules [E1] head:\n", m.group(0)[:400] if m else "NOT FOUND")
