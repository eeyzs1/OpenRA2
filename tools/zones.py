#!/usr/bin/env python3
"""原作侧边栏分区放大测量"""
from PIL import Image

ref = Image.open("tools/ref_yr_ingame.png")  # 1366x768, 侧边栏 1195..1366
zones = [
    ("zz_ref_money.png", 1195, 0, 171, 45, 5),     # 资金条
    ("zz_ref_radar.png", 1195, 40, 171, 170, 3),   # 雷达框
    ("zz_ref_tabs.png", 1195, 200, 171, 60, 5),    # 徽标带+页签
    ("zz_ref_cameo.png", 1195, 250, 171, 120, 4),  # cameo 前两行
    ("zz_ref_bottom.png", 1195, 620, 171, 148, 4), # 底部
    ("zz_ref_power.png", 1195, 250, 30, 400, 3),   # 左缘电力条
    ("zz_our_money.png", 1256, 0, 184, 70, 5),     # 我方资金条
    ("zz_our_tabs.png", 1256, 210, 184, 90, 5),    # 我方页签区
]
our = Image.open("shot_p0.png")  # 1440x810, 侧边栏 1256..1440
for name, x, y, w, h, sc in zones:
    src = our if "our" in name else ref
    c = src.crop((x, y, x + w, y + h))
    c = c.resize((w * sc, h * sc), Image.NEAREST)
    c.save("tools/" + name)
    print("saved", name)
