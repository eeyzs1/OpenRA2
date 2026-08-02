import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl, Hva, _apply_mat

t = MixTree()
_, vd = t.find("shad.vxl"); _, hd = t.find("shad.hva")
v = Vxl(vd); h = Hva(hd)

for i, sec in enumerate(v.sections):
    hi = h.sec_names.index(sec.name)
    m = h.mats[1][hi] if h.nframes > 1 else h.mats[0][hi]
    tf = sec.transform
    sx, sy, sz = sec.size
    cx, cy, cz = sx / 2, sy / 2, sz / 2
    print(f"== {sec.name} size={sec.size}")
    print("  tf:", [f"{x:.2f}" for x in tf])
    print("  m :", [f"{x:.2f}" for x in m])
    p_tf = _apply_mat(tf, cx, cy, cz)
    p_m = _apply_mat(m, cx, cy, cz)
    p_mtf = _apply_mat(m, *_apply_mat(tf, cx, cy, cz))
    p_tfm = _apply_mat(tf, *_apply_mat(m, cx, cy, cz))
    print("  center tf-only:", [f"{x:.1f}" for x in p_tf])
    print("  center m-only :", [f"{x:.1f}" for x in p_m])
    print("  center m*tf   :", [f"{x:.1f}" for x in p_mtf])
    print("  center tf*m   :", [f"{x:.1f}" for x in p_tfm])
