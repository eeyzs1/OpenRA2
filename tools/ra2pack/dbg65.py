# 扫描 cameo.mix 全部 SHP 的 compression 分布，并收集 comp=0 文件清单
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db

T = MixTree()
cameo = T.by_name.get("language.mix/cameo.mix")
if not cameo:
    print("cameo.mix not in tree; available:", [k for k in T.by_name if "cameo" in k])
    sys.exit(1)

db = name_db()
id2name = {}
for hh, nn in db.items():
    id2name.setdefault(hh, nn)

stats = {}
comp0 = []
for eid, e in cameo.index.items():
    data = cameo.data[cameo.body_base + e.offset: cameo.body_base + e.offset + e.size]
    if len(data) < 32:
        continue
    try:
        _z, w, h, nf = struct.unpack_from("<HHHH", data, 0)
        if nf == 0 or nf > 400 or w == 0 or w > 800:
            continue
        comp = struct.unpack_from("<I", data, 8 + 8)[0]
        nm = id2name.get(eid, hex(eid))
        stats[comp] = stats.get(comp, 0) + 1
        if comp == 0:
            x, y, cx, cy = struct.unpack_from("<HHHH", data, 8)
            off = struct.unpack_from("<I", data, 8 + 20)[0]
            raw_ok = (off + cx * cy <= len(data))
            comp0.append((nm, cx, cy, nf, len(data), raw_ok))
    except Exception:
        pass

print("compression histogram:", stats)
print("\ncomp=0 files:", len(comp0))
for nm, cx, cy, nf, sz, ok in sorted(comp0)[:40]:
    print(f"  {nm:24s} {cx}x{cy} frames={nf} size={sz} raw_fits={ok}")
