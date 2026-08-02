#!/usr/bin/env python3
"""侧边栏区域切片放大（目测确认分界）"""
from PIL import Image

def crop(src, x, y, w, h, dst, scale=4):
    img = Image.open(src)
    W, H = img.size
    w = min(w, W - x); h = min(h, H - y)
    c = img.crop((x, y, x + w, y + h))
    c = c.resize((w * scale, h * scale), Image.NEAREST)
    c.save(dst)
    print(f"saved {dst} ({w}x{h} x{scale})")

S = "tools/ref_yr_ingame.png"
crop(S, 1195, 0,   171, 20,  "tools/sb_1_money.png", 6)
crop(S, 1195, 17,  171, 143, "tools/sb_2_radar.png", 3)
crop(S, 1195, 155, 171, 40,  "tools/sb_3_dome.png", 5)
crop(S, 1195, 192, 171, 36,  "tools/sb_4_tabs.png", 5)
crop(S, 1195, 227, 171, 103, "tools/sb_5_grid12.png", 3)
crop(S, 1195, 475, 171, 110, "tools/sb_6_empty.png", 3)
crop(S, 1195, 680, 171, 88,  "tools/sb_7_bottom.png", 4)
crop(S, 1336, 227, 30, 500,  "tools/sb_8_power.png", 3)
crop(S, 0,    735, 1366, 33, "tools/sb_9_bottombar.png", 2)
