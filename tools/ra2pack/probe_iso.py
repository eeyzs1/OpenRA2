# 枚举 ra2.mix 内嵌 isotemp.mix / temperat.mix 全部已知名
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db

T = MixTree()
db = name_db()

for key in sorted(T.by_name):
    if "isotemp" in key or "temperat" in key or "generic" in key:
        mf = T.by_name[key]
        names = sorted(n for eid, n in
                       ((eid, db.get(eid)) for eid in mf.index) if n)
        print(f"=== {key}: {len(mf.index)} entries, {len(names)} named ===")
        for n in names:
            print(" ", n)
