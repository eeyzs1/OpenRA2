#!/usr/bin/env python3
"""放大游戏截图中的建筑区域，检查接地/阴影/锚点"""
from PIL import Image

img = Image.open("pt_03_deployed.png").convert("RGB")
W, H = img.size
print(f"game shot {W}x{H}")
# 建筑在画面中心附近 — 裁中心 500x350 放大 2x
cx, cy = W // 2, H // 2 - 40
c = img.crop((cx - 250, cy - 175, cx + 250, cy + 175))
c = c.resize((1000, 700), Image.NEAREST)
c.save("tools/zoom_bld.png")
print("saved tools/zoom_bld.png")
