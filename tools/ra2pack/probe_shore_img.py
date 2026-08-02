# 生成岸线瓦片放大对照图（3x4 网格，每块放大4倍 + 索引标注）
from PIL import Image, ImageDraw

S = 4  # 放大倍数
TW, TH = 64 * S, 32 * S
cols, rows = 3, 4
grid = Image.new("RGBA", (cols * (TW + 24) + 24, rows * (TH + 40) + 24), (24, 24, 32, 255))
d = ImageDraw.Draw(grid)
for i in range(12):
    im = Image.open(f"assets/sprites/tile_shore_{i}.png").convert("RGBA").resize((TW, TH), Image.NEAREST)
    cx, cy = i % cols, i // cols
    ox, oy = 24 + cx * (TW + 24), 24 + cy * (TH + 40)
    # 棋盘格底显示透明区
    for yy in range(0, TH, 8):
        for xx in range(0, TW, 8):
            if (xx // 8 + yy // 8) % 2 == 0:
                d.rectangle([ox + xx, oy + yy, ox + xx + 7, oy + yy + 7], fill=(48, 48, 60, 255))
    grid.paste(im, (ox, oy), im)
    d.text((ox + 4, oy + TH + 6), f"shore_{i}", fill=(255, 255, 100, 255))
    # 画菱边方向标记: +x 右下边(红), +y 左下边(绿), -x 左上边(蓝), -y 右上边(黄)
    cxp, cyp = ox + TW // 2, oy + TH // 2
    d.line([cxp, cyp, ox + TW - 8, cyp + TH // 4], fill=(255, 60, 60, 255), width=2)   # +x
    d.line([cxp, cyp, ox + 8, cyp + TH // 4], fill=(60, 255, 60, 255), width=2)        # +y
    d.line([cxp, cyp, ox + 8, cyp - TH // 4], fill=(60, 60, 255, 255), width=2)        # -x
    d.line([cxp, cyp, ox + TW - 8, cyp - TH // 4], fill=(255, 255, 60, 255), width=2)  # -y
grid.save("tools/ra2pack/shore_inspect.png")
print("saved tools/ra2pack/shore_inspect.png")
