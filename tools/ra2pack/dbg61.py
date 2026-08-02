# 检查 cnsticon.shp 帧头压缩字段 + 扫描全部 cameo 找同类未压缩帧
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Shp

T = MixTree()
_, d = T.find("cnsticon.shp")
print("=== cnsticon.shp 原始头 ===")
print("shp header:", struct.unpack_from("<HHHH", d, 0))  # zero, cx, cy, nframes
n = struct.unpack_from("<H", d, 6)[0]
for i in range(n):
    x, y, cx, cy, comp, off = struct.unpack_from("<HHHHII", d, 8 + i * 24)
    # 24 字节帧头: x,y,cx,cy (u16x4) compression(u32) offset(u32) + 8 pad?
    print(f"frame {i}: x={x} y={y} cx={cx} cy={cy} comp={comp} off={off}")
    raw = struct.unpack_from("<8H", d, 8 + i * 24)
    print("   raw16:", raw)

# 对照一个正常 cameo
_, d2 = T.find("mtnkicon.shp")
if d2 is None:
    _, d2 = T.find("cnsticon.shp")
_, d3 = T.find("ticon.shp")
for nm in ["apocicon.shp", "zepicon.shp", "gtgcicon.shp", "gpowicon.shp"]:
    _, dd = T.find(nm)
    if dd:
        nn = struct.unpack_from("<H", dd, 6)[0]
        x, y, cx, cy, comp, off = struct.unpack_from("<HHHHII", dd, 8)
        print(f"{nm}: frames={nn} f0 comp={comp} cx={cx} cy={cy} size={len(dd)}")
