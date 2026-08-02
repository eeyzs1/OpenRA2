#!/usr/bin/env python3
from PIL import Image
img = Image.open("tools/ref_yr_ingame.png")
c = img.crop((1195, 227, 1290, 727))  # 电力条+左槽列
c = c.resize((95 * 2, 500), Image.NEAREST)
c.save("tools/sb_power2.png")
print("saved tools/sb_power2.png")

# 采样电力条列颜色分布
img2 = Image.open("tools/ref_yr_ingame.png").convert("RGB")
for y in range(227, 728, 25):
    px = [img2.getpixel((x, y)) for x in (1203, 1206, 1209, 1212)]
    print(y, px)
