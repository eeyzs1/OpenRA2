# 精确分析岸线瓦片水方向：teal 水 vs 黄沙 分类 + ASCII 分布图 + 四边占比
from PIL import Image

def classify(r, g, b, a):
    if a < 128: return 0  # 透明
    # 水: teal/蓝绿 (G>=R 且 B 不低, 整体偏冷); 沙: 黄 (R>=G>B 偏暖)
    if g >= r - 6 and b > 60 and (g + b) / 2 > r + 8: return 1  # 水
    return 2  # 陆/沙

for i in range(12):
    im = Image.open(f"assets/sprites/tile_shore_{i}.png").convert("RGBA")
    w, h = im.size
    px = im.load()
    # ASCII 图 (32x16 半分辨率)
    print(f"--- shore_{i} ---")
    edges = {"+x": [0, 0], "+y": [0, 0], "-x": [0, 0], "-y": [0, 0]}  # [water, total]
    for yy in range(0, h, 2):
        row = ""
        for xx in range(0, w, 2):
            c = classify(*px[xx, yy])
            row += " " if c == 0 else ("~" if c == 1 else "#")
        print(row)
    # 四边带水占比
    for y in range(h):
        for x in range(w):
            c = classify(*px[x, y])
            if c == 0: continue
            u = (x - 32) / 32.0 + (y - 16) / 16.0
            v = (y - 16) / 16.0 - (x - 32) / 32.0
            m = {"+x": u, "+y": v, "-x": -u, "-y": -v}
            e = max(m, key=m.get)
            if m[e] > 0.35:
                edges[e][1] += 1
                if c == 1: edges[e][0] += 1
    fr = {e: (edges[e][0] / edges[e][1] if edges[e][1] else 0) for e in edges}
    wat = "".join(e for e in ("+x", "+y", "-x", "-y") if fr[e] > 0.5)
    print(f"  边带水占比: +x={fr['+x']:.2f} +y={fr['+y']:.2f} -x={fr['-x']:.2f} -y={fr['-y']:.2f}  => 水侧: {wat or '(无)'}")
