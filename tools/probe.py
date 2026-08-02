import csv
import numpy as np

quads = []
with open("quads.csv") as f:
    r = csv.DictReader(f)
    for row in r:
        v = np.array([[float(row[f"x{i}"]), float(row[f"y{i}"]), float(row[f"z{i}"])] for i in range(4)])
        c = (int(row["r"]), int(row["g"]), int(row["b"]))
        quads.append((v, c))

SIN_E, COS_E = 0.5, 0.8660254
ox, oy = 400.0, 430.0

def quad_cover(qi, px, py):
    """返回该四边形在像素处的插值深度，未覆盖返回 None"""
    v, c = quads[qi]
    sx = ox + v[:, 0]
    sy = oy + (-v[:, 1] * SIN_E - v[:, 2] * COS_E)
    sd = v[:, 1] * COS_E - v[:, 2] * SIN_E
    best = None
    for tri in [(0, 1, 2), (0, 2, 3)]:
        i0, i1, i2 = tri
        ax_, ay_ = sx[i0], sy[i0]
        bx_, by_ = sx[i1], sy[i1]
        cx_, cy_ = sx[i2], sy[i2]
        area = (bx_ - ax_) * (cy_ - ay_) - (cx_ - ax_) * (by_ - ay_)
        if abs(area) < 1e-6:
            continue
        fx, fy = px + 0.5, py + 0.5
        w1 = ((bx_ - fx) * (cy_ - fy) - (cx_ - fx) * (by_ - fy)) / area
        w2 = ((cx_ - fx) * (ay_ - fy) - (ax_ - fx) * (cy_ - fy)) / area
        w0 = 1.0 - w1 - w2
        if w0 < -0.001 or w1 < -0.001 or w2 < -0.001:
            continue
        depth = w0 * sd[i0] + w1 * sd[i1] + w2 * sd[i2]
        if best is None or depth < best:
            best = depth
    return best

names = ["slab", "podium", "body", "cornice"]
faces = ["top", "bot", "+x", "-x", "+y", "-y"]

# 探测屋顶区域多个像素
for (px, py) in [(380, 420), (400, 425), (420, 430), (390, 435), (410, 440), (380, 445), (360, 430)]:
    print(f"--- pixel ({px},{py}) ---")
    res = []
    for qi in range(18, 24):  # cornice 六面
        d = quad_cover(qi, px, py)
        if d is not None:
            res.append((d, qi))
    res.sort()
    for d, qi in res:
        print(f"  {names[qi//6]:>7} {faces[qi%6]:>4} depth={d:.3f}")
