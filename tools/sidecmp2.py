#!/usr/bin/env python3
"""并排对比：我们的侧边栏 vs 原作侧边栏（同高度拼接）"""
from PIL import Image

# 我们的截图 1440x810，侧边栏 x=1256..1440（184 宽）
our = Image.open("pt_04_placed.png").crop((1256, 0, 1440, 810))
# 原作 1366x768，侧边栏 x=1195..1366（171 宽）
ref = Image.open("tools/ref_yr_ingame.png").crop((1195, 0, 1366, 768))

# 等比缩放到同高 810：ref 需放大 810/768
sc = 810 / 768
ref = ref.resize((int(171 * sc), 810), Image.LANCZOS)

# 并排拼接（左原作 右我们的），加 4px 分隔黑条
gap = 4
W = ref.width + gap + our.width
canvas = Image.new("RGB", (W, 810), (16, 16, 16))
canvas.paste(ref, (0, 0))
canvas.paste(our, (ref.width + gap, 0))
canvas.save("tools/cmp_sidebar.png")
print(f"saved tools/cmp_sidebar.png ({W}x810)  左=原作 右=当前")

# 顶部区域放大对比（资金带+雷达+药丸+页签，前 300px 高）
top_ref = ref.crop((0, 0, ref.width, 300)).resize((ref.width * 2, 600), Image.NEAREST)
top_our = our.crop((0, 0, our.width, 300)).resize((our.width * 2, 600), Image.NEAREST)
W2 = top_ref.width + gap + top_our.width
c2 = Image.new("RGB", (W2, 600), (16, 16, 16))
c2.paste(top_ref, (0, 0))
c2.paste(top_our, (top_ref.width + gap, 0))
c2.save("tools/cmp_top.png")
print(f"saved tools/cmp_top.png ({W2}x600)")

# cameo 网格区域对比（280..560）
g_ref = ref.crop((0, 280, ref.width, 560)).resize((ref.width * 2, 560), Image.NEAREST)
g_our = our.crop((0, 280, our.width, 560)).resize((our.width * 2, 560), Image.NEAREST)
W3 = g_ref.width + gap + g_our.width
c3 = Image.new("RGB", (W3, 560), (16, 16, 16))
c3.paste(g_ref, (0, 0))
c3.paste(g_our, (g_ref.width + gap, 0))
c3.save("tools/cmp_grid.png")
print(f"saved tools/cmp_grid.png ({W3}x560)")
