import csv
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

quads = []
with open("quads.csv") as f:
    r = csv.DictReader(f)
    for row in r:
        v = np.array([[float(row[f"x{i}"]), float(row[f"y{i}"]), float(row[f"z{i}"])] for i in range(4)])
        c = (int(row["r"]), int(row["g"]), int(row["b"]))
        quads.append((v, c))

SIN_E, COS_E = 0.5, 0.8660254
LX, LY, LZ = -0.42, -0.50, 0.759
AMB, DIFF = 0.38, 0.95
W = H = 800
ox, oy = W / 2.0, 430.0
img = np.zeros((H, W, 3), dtype=np.uint8)
owner = np.full((H, W), -1, dtype=int)
zb = np.full((H, W), 1e30)

for qi, (v, c) in enumerate(quads):
    sx = ox + v[:, 0]
    sy = oy + (-v[:, 1] * SIN_E - v[:, 2] * COS_E)
    sd = v[:, 1] * COS_E - v[:, 2] * SIN_E
    e1 = v[1] - v[0]
    e2 = v[3] - v[0]
    n = np.cross(e1, e2)
    l = np.linalg.norm(n)
    if l < 1e-6:
        continue
    n /= l
    cc = v.mean(axis=0)
    if np.dot(v[0] - cc, n) < 0:
        n = -n
    k = AMB + DIFF * max(0.0, n @ np.array([LX, LY, LZ]))
    k = min(k, 1.28)
    col = np.minimum(255, (np.array(c) * k)).astype(int)
    for tri in [(0, 1, 2), (0, 2, 3)]:
        i0, i1, i2 = tri
        ax_, ay_ = sx[i0], sy[i0]
        bx_, by_ = sx[i1], sy[i1]
        cx_, cy_ = sx[i2], sy[i2]
        area = (bx_ - ax_) * (cy_ - ay_) - (cx_ - ax_) * (by_ - ay_)
        if abs(area) < 1e-6:
            continue
        minX = max(0, int(np.floor(min(ax_, bx_, cx_))))
        maxX = min(W - 1, int(np.ceil(max(ax_, bx_, cx_))))
        minY = max(0, int(np.floor(min(ay_, by_, cy_))))
        maxY = min(H - 1, int(np.ceil(max(ay_, by_, cy_))))
        for py in range(minY, maxY + 1):
            for px in range(minX, maxX + 1):
                fx, fy = px + 0.5, py + 0.5
                wa = ((bx_ - fx) * (cy_ - fy) - (cx_ - fx) * (by_ - fy)) / area
                wb = ((cx_ - fx) * (ay_ - fy) - (ax_ - fx) * (cy_ - fy)) / area
                wc = 1.0 - wa - wb
                if wa < -0.001 or wb < -0.001 or wc < -0.001:
                    continue
                depth = wa * sd[i0] + wb * sd[i1] + wc * sd[i2]  # 修正后
                if depth >= zb[py, px]:
                    continue
                zb[py, px] = depth
                img[py, px] = col
                owner[py, px] = qi

# 面归属统计
names = ["slab", "podium", "body", "cornice"]
faces = ["top", "bot", "+x", "-x", "+y", "-y"]
print(f"{'quad':>10} {'color':>15} {'pixels':>7}  z-range")
for qi, (v, c) in enumerate(quads):
    box = names[qi // 6]
    face = faces[qi % 6]
    cnt = int((owner == qi).sum())
    z0, z1 = v[:, 2].min(), v[:, 2].max()
    print(f"{box:>6} {face:>4} {str(c):>15} {cnt:>7}  z={z0:.1f}..{z1:.1f}")

plt.figure(figsize=(10, 10))
plt.imshow(img)
plt.title("re-render 800px")
plt.savefig("rerender_big.png", dpi=100)
print("saved rerender_big.png")
