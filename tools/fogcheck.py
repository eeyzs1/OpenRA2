#!/usr/bin/env python3
"""检查迷雾区是否存在真实棋盘格：比较 FOG_SEEN 区相邻像素差分规律"""
from PIL import Image

img = Image.open("pt_03_deployed.png").convert("RGB")
# FOG_SEEN 区取样（z_fog_edge 对应源区 pt_03_deployed (500,180)-(660,300)）
# 打印一块 16x8 区域的 G 通道值
x0, y0 = 560, 220
print("G channel 16x8 @ (560,220) [SEEN区]:")
for y in range(y0, y0 + 8):
    row = []
    for x in range(x0, x0 + 16):
        r, g, b = img.getpixel((x, y))
        row.append(f"{g:3d}")
    print(" ".join(row))
# 可见区对照
x0, y0 = 560, 500
print("\nG channel 16x8 @ (560,500) [VISIBLE区]:")
for y in range(y0, y0 + 8):
    row = []
    for x in range(x0, x0 + 16):
        r, g, b = img.getpixel((x, y))
        row.append(f"{g:3d}")
    print(" ".join(row))
