import csv
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon

quads = []
with open("quads.csv") as f:
    r = csv.DictReader(f)
    for row in r:
        v = np.array([[float(row[f"x{i}"]), float(row[f"y{i}"]), float(row[f"z{i}"])] for i in range(4)])
        c = (int(row["r"]), int(row["g"]), int(row["b"]))
        quads.append((v, c))

# ---- 三视图 ----
fig, axes = plt.subplots(1, 4, figsize=(28, 8))
views = [("top XY", 0, 1), ("front XZ", 0, 2), ("side YZ", 1, 2)]
for ax, (name, a, b) in zip(axes[:3], views):
    for v, c in quads:
        ax.add_patch(Polygon(v[:, [a, b]], closed=True, facecolor=np.array(c) / 255,
                             edgecolor="k", linewidth=0.4, alpha=0.85))
    ax.autoscale_view()
    ax.set_aspect("equal")
    ax.set_title(name)
    ax.grid(True, alpha=0.3)

# ---- numpy 复刻渲染（与 m3Render 相同投影/光照/z-buffer，SS=1 简化）----
SIN_E, COS_E = 0.5, 0.8660254
LX, LY, LZ = -0.42, -0.50, 0.759
AMB, DIFF = 0.38, 0.95
W = H = 400
ox, oy = W / 2.0, 220.0
img = np.zeros((H, W, 3), dtype=np.uint8)
zb = np.full((H, W), 1e30)

for v, c in quads:
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
                w1 = ((bx_ - fx) * (cy_ - fy) - (cx_ - fx) * (by_ - fy)) / area
                w2 = ((cx_ - fx) * (ay_ - fy) - (ax_ - fx) * (cy_ - fy)) / area
                w0 = 1.0 - w1 - w2
                if w0 < -0.001 or w1 < -0.001 or w2 < -0.001:
                    continue
                depth = w0 * sd[i0] + w1 * sd[i1] + w2 * sd[i2]
                if depth >= zb[py, px]:
                    continue
                zb[py, px] = depth
                img[py, px] = col

axes[3].imshow(img)
axes[3].set_title("re-render (numpy)")
plt.tight_layout()
plt.savefig("quads_check.png", dpi=110)
print("saved quads_check.png")
