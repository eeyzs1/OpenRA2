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
W = H = 800
ox, oy = W / 2.0, 430.0
owner = np.full((H, W), -1, dtype=int)
zb = np.full((H, W), 1e30)

for qi, (v, c) in enumerate(quads):
    sx = ox + v[:, 0]
    sy = oy + (-v[:, 1] * SIN_E - v[:, 2] * COS_E)
    sd = v[:, 1] * COS_E - v[:, 2] * SIN_E
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
                owner[py, px] = qi

# 面 ID → 颜色：盒=亮度档，面=色相
face_cols = [(255, 80, 80), (255, 0, 0), (80, 255, 80), (0, 160, 0), (80, 80, 255), (0, 0, 160)]
box_gain = [1.0, 0.85, 0.7, 0.55]
img = np.zeros((H, W, 3), dtype=np.uint8)
for qi in range(24):
    m = owner == qi
    col = np.array(face_cols[qi % 6]) * box_gain[qi // 6]
    img[m] = np.array(col, dtype=np.uint8)

names = ["slab", "podium", "body", "cornice"]
faces = ["top", "bot", "+x", "-x", "+y", "-y"]
fig, ax = plt.subplots(figsize=(10, 10))
ax.imshow(img)
import matplotlib.patches as mp
handles = [mp.Patch(color=np.array(face_cols[i]) / 255, label=f) for i, f in enumerate(faces)]
ax.legend(handles=handles, loc="upper right")
ax.set_title("owner map: hue=face(top/bot/+x/-x/+y/-y), brightness=box(slab>podium>body>cornice)")
plt.tight_layout()
plt.savefig("ownermap.png", dpi=100)
print("saved ownermap.png")
