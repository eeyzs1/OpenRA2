import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from PIL import Image

SPR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "assets", "sprites")

for name in ["unit_grizzly_d0_f0.png", "unit_grizzly_d2_f0.png", "unit_gi_d0_f0.png", "bld_conyard.png", "icon_unit_grizzly.png"]:
    p = os.path.join(SPR, name)
    im = Image.open(p)
    bbox = im.getbbox()
    n = 0
    if bbox:
        n = sum(1 for px in im.getdata() if px[3] > 8)
    print(f"{name}: size={im.size} bbox={bbox} solidpx={n}")
