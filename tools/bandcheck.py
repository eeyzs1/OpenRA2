#!/usr/bin/env python3
"""分析烘焙地形纹理的透明行分布规律"""
from PIL import Image

img = Image.open("dbg_terrain_cpu_64x64.png").convert("RGBA")
W, H = img.size
print(f"size={W}x{H}")
px = img.load()

rows = []
for y in range(H):
    n = sum(1 for x in range(W) if px[x, y][3] > 0)
    rows.append(n)

# 找到完全透明/部分透明的行区间
runs = []
cur = None
for y, n in enumerate(rows):
    state = "full" if n > W * 0.9 else ("zero" if n == 0 else "part")
    if cur is None or cur[2] != state:
        if cur: runs.append(cur)
        cur = [y, y, state, n]
    else:
        cur[1] = y
if cur: runs.append(cur)

for r in runs[:40]:
    print(f"rows {r[0]:4d}..{r[1]:4d} ({r[1]-r[0]+1:4d} rows) {r[2]} firstN={r[3]}")

# 列方向统计（看垂直条纹）
cols = []
for x in range(W):
    n = sum(1 for y in range(H) if px[x, y][3] > 0)
    cols.append(n)
zero_cols = sum(1 for n in cols if n == 0)
print(f"zero-alpha cols: {zero_cols}/{W}")
