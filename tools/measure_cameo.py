#!/usr/bin/env python3
"""测量原作侧边栏 0..230 结构分界（全宽暗缝扫描）"""
from PIL import Image

img = Image.open("tools/ref_yr_ingame.png").convert("RGB")

print("--- 垂直扫描 x=1210..1355 均值，y=0..235 ---")
for y in range(0, 235):
    s = 0
    for x in range(1210, 1355, 3):
        r, g, b = img.getpixel((x, y))
        s += r + g + b
    a = s / (145 // 3)
    if y % 2 == 0 or a < 55:
        bar = "#" * int(a / 14)
        dark = "DARK" if a < 55 else ""
        print(f"{y:3d} {a:6.1f} {bar} {dark}")
