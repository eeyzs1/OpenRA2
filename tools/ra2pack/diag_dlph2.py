# 打印 dlph.shp 全部帧尺寸，按 宽/高比 推断方向块
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ra2lib import MixTree, Shp

T = MixTree()
d = T.find("dlph.shp")[1]
shp = Shp(d)
print(f"dlph.shp {shp.nframes} 帧")
# 宽高比: >1.6 水平(E/W), <0.62 竖直(N/S), 其余斜向
line = []
for i in range(shp.nframes):
    x, y, w, h, comp, off = shp.frames[i]
    if w == 0 or h == 0:
        line.append(f"{i}:--")
        continue
    r = w / h
    c = "H" if r > 1.6 else ("V" if r < 0.62 else "D")
    line.append(f"{i}:{w}x{h}{c}")
for i in range(0, len(line), 8):
    print("  ".join(line[i:i+8]))
