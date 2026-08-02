# cnsticon.shp 帧数据字节分析：直方图 +  hex dump + RLE 假设检验
import sys, os, struct
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree

T = MixTree()
_, d = T.find("cnsticon.shp")
data = d[56:56 + 3072]

# 直方图
hist = [0] * 256
for b in data:
    hist[b] += 1
top = sorted(range(256), key=lambda i: -hist[i])[:16]
print("top16 byte values:", [(i, hist[i]) for i in top])
print("distinct values:", sum(1 for h in hist if h))

# hex dump 前 96 字节
for r in range(6):
    row = data[r * 16:(r + 1) * 16]
    print(f"{r*16:5d}: " + " ".join(f"{b:02x}" for b in row))

# 假设是 format3 RLE：每行 u16 rowlen
print("\nRLE hypothesis: first row lens if format3:")
p = 56
for row in range(6):
    rl = struct.unpack_from("<H", d, p)[0]
    print(f" row {row}: rowlen={rl}")
    if rl < 2 or rl > 200:
        break
    p += rl

# 对照 zepicon（raw 正常）的直方图
_, d2 = T.find("zepicon.shp")
off2 = struct.unpack_from("<I", d2, 8 + 20)[0]
data2 = d2[off2:off2 + 60 * 48]
hist2 = [0] * 256
for b in data2:
    hist2[b] += 1
top2 = sorted(range(256), key=lambda i: -hist2[i])[:16]
print("\nzepicon top16:", [(i, hist2[i]) for i in top2])
print("zepicon distinct:", sum(1 for h in hist2 if h))
