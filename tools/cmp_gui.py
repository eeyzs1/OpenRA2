#!/usr/bin/env python3
"""游戏截图侧边栏 vs 原作侧边栏 并排对比（等高缩放）"""
from PIL import Image

ref = Image.open("tools/ref_yr_ingame.png").convert("RGB")   # 1366x768, 侧栏 x1195..1366
game = Image.open("pt_03_deployed.png").convert("RGB")       # 1440x810, 侧栏 184
W, H = game.size
sbX = W - 184

ref_sb = ref.crop((1195, 0, 1366, 768))      # 171x768
game_sb = game.crop((sbX, 0, W, H))          # 184x810

# 缩放到同一高度 810
ref_sb = ref_sb.resize((int(171 * 810 / 768), 810), Image.LANCZOS)  # ~180x810

canvas = Image.new("RGB", (ref_sb.width + game_sb.width + 8, 810), (40, 40, 40))
canvas.paste(ref_sb, (0, 0))
canvas.paste(game_sb, (ref_sb.width + 8, 0))
canvas.save("tools/cmp_sidebar.png")
print(f"saved tools/cmp_sidebar.png {canvas.size} (左=原作 右=游戏)")

# 底栏对比
ref_bb = ref.crop((0, 735, 1366, 768)).resize((1440, 35), Image.LANCZOS)
game_bb = game.crop((0, H - 35, W, H))
cv2 = Image.new("RGB", (1440, 70 + 8), (40, 40, 40))
cv2.paste(ref_bb, (0, 0))
cv2.paste(game_bb, (0, 35 + 8))
cv2.save("tools/cmp_bottombar.png")
print("saved tools/cmp_bottombar.png (上=原作 下=游戏)")
