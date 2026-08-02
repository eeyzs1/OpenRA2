import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

t = MixTree()
_, art = t.find("art.ini")
txt = art.decode("latin-1")

def dump(sid):
    m = re.search(r"^\[" + sid + r"\][^\n]*$", txt, re.M)
    if not m:
        print(f"[{sid}] NOT in art.ini"); return
    start = m.start()
    nxt = txt.find("\n[", start + 1)
    print(txt[start:nxt if nxt > 0 else len(txt)].strip()[:600])
    print("-" * 50)

for sid in ["MTNK", "4TNK", "MCV", "SMCV", "HARV", "RTNK", "APOC"]:
    dump(sid)

def has_f(n): return t.find(n)[1] is not None
print("4tnk.hva:", has_f("4tnk.hva"))
