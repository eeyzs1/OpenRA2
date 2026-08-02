#!/usr/bin/env python3
"""精确测量原作侧边栏各区域坐标（为贴图提取服务）"""
from PIL import Image

img = Image.open("tools/ref_yr_ingame.png").convert("RGB")
W, H = img.size
print(f"img {W}x{H}")

def rowavg(y, x0=1200, x1=1355):
    s = 0; n = 0
    for x in range(x0, x1, 2):
        r, g, b = img.getpixel((x, y)); s += r+g+b; n += 1
    return s / n

def colavg(x, y0, y1):
    s = 0; n = 0
    for y in range(y0, y1, 2):
        r, g, b = img.getpixel((x, y)); s += r+g+b; n += 1
    return s / n

print("--- 行亮度 y=0..240（找 money/radar/dome/tabs 分界） ---")
prev = 0
for y in range(0, 240):
    a = rowavg(y)
    if y % 4 == 0 or abs(a - prev) > 40:
        print(f"{y:3d} {a:6.1f} {'#' * int(a/16)}")
    prev = a

print("--- 行亮度 y=240..768（找 cameo 网格行与底盖） ---")
prev = 0
for y in range(240, 768):
    a = rowavg(y)
    if y % 8 == 0 or abs(a - prev) > 40:
        print(f"{y:3d} {a:6.1f} {'#' * int(a/16)}")
    prev = a

print("--- 列亮度 y=420..700（cameo 区，找列分界与电力条位置） ---")
for x in range(1195, 1366):
    a = colavg(x, 420, 700)
    if x % 2 == 0:
        print(f"{x:4d} {a:6.1f} {'#' * int(a/16)}")
