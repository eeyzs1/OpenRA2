#!/usr/bin/env python3
"""从原作截图提取侧边栏/底栏贴图到 assets/gui/，清理动态区域（资金/雷达/槽位/电力填充）"""
from PIL import Image
import os

SRC = "tools/ref_yr_ingame.png"
OUT = "assets/gui"
os.makedirs(OUT, exist_ok=True)

img = Image.open(SRC).convert("RGB")

# ---- 结构常量（1366x768 实测，见 tools/sbprobe.py） ----
SB_X, SB_W = 1195, 171
GRID_Y0, PITCH, ROWS = 227, 50, 10
SLOT_XS = (1221, 1283)   # 槽外框 x
SLOT_W, SLOT_H = 60, 48
MONEY_DIG = (1250, 4, 50, 12)     # 资金数字区 x,y,w,h
RADAR_IN = (1209, 37, 147, 118)   # 雷达内腔
POW_X, POW_Y0, POW_Y1 = 1203, 228, 726  # 电力填充腔（不含框）

sb = img.crop((SB_X, 0, SB_X + SB_W, 768)).copy()
px = sb.load()

# 1) 清理资金数字 → 槽内深色
dark = px[1300 - SB_X, 8]
for y in range(MONEY_DIG[1], MONEY_DIG[1] + MONEY_DIG[3]):
    for x in range(MONEY_DIG[0] - SB_X, MONEY_DIG[0] - SB_X + MONEY_DIG[2]):
        px[x, y] = dark

# 2) 清理雷达内腔 → 近黑
for y in range(RADAR_IN[1], RADAR_IN[1] + RADAR_IN[3]):
    for x in range(RADAR_IN[0] - SB_X, RADAR_IN[0] - SB_X + RADAR_IN[2]):
        px[x, y] = (2, 3, 5)

# 3) cameo 槽：用第 8 行（y=627 空槽）覆盖全部 10 行
for col_x in SLOT_XS:
    tile = img.crop((col_x, 627, col_x + SLOT_W, 627 + SLOT_H))
    for r in range(ROWS):
        sb.paste(tile, (col_x - SB_X, GRID_Y0 + r * PITCH))

# 4) 电力填充腔：用未点亮段（y230..350 暗区）平铺覆盖
px = sb.load()
unlit = sb.crop((POW_X - SB_X, 230, POW_X - SB_X + 10, 350))
for y in range(POW_Y0, POW_Y1 + 1):
    src_y = (y - POW_Y0) % 120
    for x in range(POW_X - SB_X, POW_X - SB_X + 10):
        px[x, y] = unlit.getpixel((x - (POW_X - SB_X), src_y))

sb.save(f"{OUT}/sidebar_allied.png")
print(f"saved {OUT}/sidebar_allied.png {sb.size}")

# 5) 底栏：全宽 1366x33，清理右侧超武计时文字
bb = img.crop((0, 735, 1366, 768)).copy()
bp = bb.load()
for y in range(4, 28):
    for x in range(1100, 1315):
        r, g, b = bp[x, y]
        if r + g + b > 200:
            bp[x, y] = (14, 15, 20)
bb.save(f"{OUT}/bottombar.png")
print(f"saved {OUT}/bottombar.png {bb.size}")
