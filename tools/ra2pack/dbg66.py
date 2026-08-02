# 列出 0xCCCCCCCC 文件名 + 检查 4 个 comp=0 文件数据特征
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, name_db

T = MixTree()
cameo = T.by_name.get("language.mix/cameo.mix")
db = name_db()
id2name = {}
for hh, nn in db.items():
    id2name.setdefault(hh, nn)

cc = []
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
        if comp == 0xCCCCCCCC:
            cc.append((nm, w, h, nf, len(data), data[:16].hex()))
    except Exception:
        pass

print("0xCCCCCCCC files:", len(cc))
for nm, w, h, nf, sz, hx in sorted(cc):
    print(f"  {nm:24s} {w}x{h} frames={nf} size={sz} head={hx}")

# 4 个 comp=0 文件头 64 字节对比
print("\ncomp=0 heads:")
for fn in ["cnsticon.shp", "empicon.shp", "lpsticon.shp", "npsiicon.shp"]:
    _, d = T.find(fn)
    print(f"  {fn}: {d[56:72].hex()}")
