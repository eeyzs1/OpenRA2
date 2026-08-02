# 全量盘点 isotemp.mix / temperat.mix 中所有已知 .tem/.shp 文件（名称+大小）
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db

T = MixTree()
db = name_db()

for mixkey in ["ra2.mix/isotemp.mix", "ra2.mix/temperat.mix"]:
    mf = T.by_name.get(mixkey)
    if not mf:
        print(mixkey, "NOT FOUND"); continue
    hits = []
    for eid, e in mf.index.items():
        n = db.get(eid)
        if n and (n.endswith(".tem") or n.endswith(".shp")):
            hits.append((n, e.size))
    hits.sort()
    print(f"\n=== {mixkey}: {len(hits)} named tem/shp of {len(mf.index)} entries ===")
    for n, s in hits:
        print(f"  {n:24s} {s}")
