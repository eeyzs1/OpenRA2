#!/usr/bin/env python3
"""参照图关键区域放大（任务硬编码）"""
from PIL import Image

def crop(src, x, y, w, h, dst, scale=6):
    img = Image.open(src)
    W, H = img.size
    w = min(w, W - x); h = min(h, H - y)
    c = img.crop((x, y, x + w, y + h))
    c = c.resize((w * scale, h * scale), Image.NEAREST)
    c.save(dst)
    print(f"saved {dst} ({w}x{h} x{scale})")

# ref_yr_ingame.png 1366x768，侧边栏 x=1195..1366
crop("tools/ref_yr_ingame.png", 1195, 0, 171, 60, "tools/z2_moneyband.png", 6)    # 资金+蓝带
crop("tools/ref_yr_ingame.png", 1195, 190, 171, 90, "tools/z2_pilltabs.png", 6)   # 维修出售+页签
crop("tools/ref_yr_ingame.png", 1195, 700, 171, 68, "tools/z2_bottomcap.png", 6)  # 底部蓝盖
crop("tools/ref_yr_ingame.png", 0, 735, 500, 33, "tools/z2_bottombar.png", 4)     # 底栏左侧图标
crop("tools/ref_yr_ingame.png", 1300, 0, 66, 400, "tools/z2_bulbs.png", 4)        # 右缘装饰
crop("tools/ref_yr_ingame.png", 1195, 40, 171, 170, "tools/z2_radar.png", 4)      # 雷达框
