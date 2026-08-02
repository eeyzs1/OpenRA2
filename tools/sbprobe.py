#!/usr/bin/env python3
"""探测雷达内腔/资金数字/电力条 精确边界"""
from PIL import Image
img = Image.open("tools/ref_yr_ingame.png").convert("RGB")

def scan_row(y, x0=1195, x1=1366):
    out = []
    for x in range(x0, x1):
        r, g, b = img.getpixel((x, y))
        s = r + g + b
        out.append("." if s < 90 else ("#" if s > 420 else "+"))
    print(f"y={y:3d} " + "".join(out))

def scan_col(x, y0, y1):
    out = []
    for y in range(y0, y1):
        r, g, b = img.getpixel((x, y))
        s = r + g + b
        out.append("." if s < 90 else ("#" if s > 420 else "+"))
    print(f"x={x:4d} " + "".join(out))

print("雷达区水平扫描（.=暗 +=中 #=亮）x1195..1365")
for y in (20, 26, 30, 34, 36, 38, 42, 60, 100, 140, 150, 154, 156):
    scan_row(y)
print("雷达区垂直扫描")
for x in (1200, 1206, 1210, 1214, 1218, 1222, 1230, 1300, 1340, 1348, 1352, 1356, 1360):
    scan_col(x, 17, 160)
print("资金行扫描 y=4..16")
for y in range(4, 16):
    scan_row(y, 1195, 1366)
print("电力条列扫描 x=1195..1214, y=227..727 采样")
for x in range(1195, 1215):
    r, g, b = img.getpixel((x, 300))
    r2, g2, b2 = img.getpixel((x, 600))
    print(f"x={x} y300=({r},{g},{b}) y600=({r2},{g2},{b2})")
