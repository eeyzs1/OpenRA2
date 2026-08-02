#!/usr/bin/env python3
"""我方截图与RA2原作参照的侧边栏并排对比"""
from PIL import Image

# 我方: shot_p0.png 1024x576, 侧边栏约 x=895..1024
our = Image.open("shot_p0.png")
OW, OH = our.size
print(f"our size={OW}x{OH}")
# 侧边栏宽度按 184/1440 比例估算
sb_w = round(OW * 184 / 1440)
our_sb = our.crop((OW - sb_w, 0, OW, OH))

# 原作: tools/ref_yr_ingame.png 1366x768, 侧边栏 x=1195..1366 (171px)
ref = Image.open("tools/ref_yr_ingame.png")
RW, RH = ref.size
print(f"ref size={RW}x{RH}")
ref_sb = ref.crop((1195, 0, RW, RH))

# 缩放到同一宽度便于对比
TW = 342
our_s = our_sb.resize((TW, round(our_sb.height * TW / our_sb.width)), Image.LANCZOS)
ref_s = ref_sb.resize((TW, round(ref_sb.height * TW / ref_sb.width)), Image.LANCZOS)

H = max(our_s.height, ref_s.height)
canvas = Image.new("RGB", (TW * 2 + 8, H + 24), (24, 24, 28))
canvas.paste(ref_s, (0, 24))
canvas.paste(our_s, (TW + 8, 24))
canvas.save("tools/cmp_sidebar.png")
print(f"saved tools/cmp_sidebar.png ref_h={ref_s.height} our_h={our_s.height}")

# 底部状态栏对比
our_bb = our.crop((0, OH - 40, 500, OH))
ref_bb = ref.crop((0, RH - 33, 500, RH))
w2 = 1000
our_bb = our_bb.resize((w2, round(our_bb.height * w2 / our_bb.width)), Image.LANCZOS)
ref_bb = ref_bb.resize((w2, round(ref_bb.height * w2 / ref_bb.width)), Image.LANCZOS)
c2 = Image.new("RGB", (w2, our_bb.height + ref_bb.height + 8), (24, 24, 28))
c2.paste(ref_bb, (0, 0))
c2.paste(our_bb, (0, ref_bb.height + 8))
c2.save("tools/cmp_bottombar.png")
print("saved tools/cmp_bottombar.png")
