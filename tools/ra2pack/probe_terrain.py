# 探测 isotem.mix / temperat.mix 内容：TMP 地形瓦片与树 SHP
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db

T = MixTree()
db = name_db()

# 列出 isotem.mix 里所有已知名文件
iso = T.by_name.get("isotem.mix")
print("=== isotem.mix ===")
if iso:
    named = 0
    for eid, e in sorted(iso.index.items(), key=lambda kv: kv[1].offset):
        n = db.get(eid)
        if n:
            named += 1
    print(f"entries={len(iso.index)} named={named}")
    # 分类统计
    from collections import Counter
    ext = Counter()
    for eid in iso.index:
        n = db.get(eid)
        if n:
            ext[n.rsplit('.', 1)[-1]] += 1
    print("by ext:", dict(ext))

# 找常见地形瓦片名
print("\n=== probe common tile names ===")
for probe in ["clear01.tem", "clear01a.tem", "grass1.tem", "cliff01.tem",
              "water.tem", "shore01.tem", "rough01.tem", "ore01.tem",
              "gems01.tem", "dirt01.tem", "pave01.tem"]:
    mix, data = T.find(probe)
    print(f"{probe:16s} -> {mix if data else 'MISS'} {len(data) if data else 0}")

# 树 SHP
print("\n=== probe tree shp names ===")
for probe in ["tree01.shp", "trees01.shp", "ltree01.shp", "treetem01.shp",
              "tibtree01.shp", "oak01.shp", "tree01.tem"]:
    mix, data = T.find(probe)
    print(f"{probe:16s} -> {mix if data else 'MISS'} {len(data) if data else 0}")

# temperat.mix 里找树/地形
print("\n=== temperat.mix tree/terrain candidates ===")
tmp = T.by_name.get("temperat.mix")
if tmp:
    hits = []
    for eid in tmp.index:
        n = db.get(eid)
        if n and ("tree" in n or n.endswith(".tem") or "ore" in n or "gem" in n):
            hits.append(n)
    print(f"total {len(hits)}")
    for n in sorted(hits)[:80]:
        print(" ", n)
