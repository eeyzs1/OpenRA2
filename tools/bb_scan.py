#!/usr/bin/env python3
"""放大原作底栏左侧图标区，确认各图标精确 x 位置"""
from PIL import Image

img = Image.open("assets/gui/bottombar.png").convert("RGB")
print(f"bottombar {img.size}")
# 左侧 0..280 放大 4x
c = img.crop((0, 0, 280, 33)).resize((280 * 4, 33 * 4), Image.NEAREST)
c.save("tools/bb_left_zoom.png")
# 扫描亮图标列（y=6..26 均值亮度），定位图标中心
px = img.load()
runs = []
in_run = False
for x in range(0, 400):
    s = sum(sum(px[x, y]) for y in range(6, 27)) / 21
    if s > 90 and not in_run:
        in_run = x
    elif s <= 90 and in_run is not False:
        runs.append((in_run, x)); in_run = False
print("bright runs:", runs)
