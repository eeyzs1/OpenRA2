#!/usr/bin/env python3
"""批量裁剪放大：crop.py（任务硬编码，避免参数传递问题）"""
from PIL import Image

def crop(src, x, y, w, h, dst, scale=3):
    img = Image.open(src)
    W, H = img.size
    w = min(w, W - x); h = min(h, H - y)
    c = img.crop((x, y, x + w, y + h))
    c = c.resize((w * scale, h * scale), Image.NEAREST)
    c.save(dst)
    print(f"saved {dst} ({w}x{h} x{scale})")

# YR 参考图 1366x768：侧边栏 x≈1195..1366，底栏 y≈735..768
crop("tools/ref_yr_ingame.png", 1195, 0, 171, 400, "tools/z_ref_sb_top.png", 3)
crop("tools/ref_yr_ingame.png", 1195, 380, 171, 355, "tools/z_ref_sb_grid.png", 3)
crop("tools/ref_yr_ingame.png", 0, 700, 1366, 68, "tools/z_ref_bottombar.png", 2)
# 我们的截图：建筑接地检查 + 侧边栏现状
crop("pt_04_placed.png", 300, 150, 250, 180, "tools/z_our_bld.png", 3)
crop("pt_04_placed.png", 1180, 0, 260, 810, "tools/z_our_sb.png", 1)
# 迷雾边界 6 倍放大（pt_03 有 可见/已探索/未探索 三区交界）
crop("pt_03_deployed.png", 500, 180, 160, 120, "tools/z_fog_edge.png", 6)
# ConYard 建筑接地检查（pt_03 1440x810，建筑在 ~560,390）
crop("pt_03_deployed.png", 420, 260, 320, 220, "tools/z_conyard.png", 3)
