# TMP 头与索引精确解剖
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

T = MixTree()
for name in ["clear01.tem", "clear01a.tem", "sandy01.tem", "rough01.tem", "water01.tem", "shore01.tem"]:
    mix, d = T.find(name)
    if not d:
        print(name, "MISS"); continue
    xb, yb, cx, cy, n = struct.unpack_from("<5i", d, 0)
    print(f"\n{name} from {mix}: xb={xb} yb={yb} cx={cx} cy={cy} nimg={n} size={len(d)}")
    # 索引条目（x, y, extra_ofs, z_ofs）
    for i in range(min(n, 8)):
        x, y, eo, zo = struct.unpack_from("<4i", d, 20 + i * 16)
        print(f"  idx[{i}]: x={x} y={y} extra_ofs={eo} z_ofs={zo}")
    if n > 8:
        x, y, eo, zo = struct.unpack_from("<4i", d, 20 + (n - 1) * 16)
        print(f"  idx[{n-1}]: x={x} y={y} extra_ofs={eo} z_ofs={zo}")
    pix_base = 20 + 16 * n
    print(f"  pix_base={pix_base} data_len={len(d) - pix_base} per_img={(len(d) - pix_base) / max(n,1):.1f}")
