# 放大查看可疑单位 PNG 实际像素（4x NEAREST 无插值，所见即游戏内像素）
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

SUSPECT = ["pla","guardiangi","initiate","brute","virus","boris","type99","robottank",
    "battlefortress","gatlingtank","magnetron","mastermind","chaosdrone","mig",
    "siegechopper","floatingdisc","boomer",  # 17 个无 MIX 素材
    "dolphin","aegis","apocalypse","grizzly","gi","conscript","tanya"]  # 对照/疑似 remap 错乱

cell = 120
cols = 6
rows = (len(SUSPECT) + cols - 1) // cols
m = Image.new("RGB", (cols * cell, rows * (cell + 14)), (24, 24, 28))
dr = ImageDraw.Draw(m)
for i, u in enumerate(SUSPECT):
    p = os.path.join(SPR, f"unit_{u}_d2_f0.png")
    if not os.path.exists(p):
        p = os.path.join(SPR, f"unit_{u}_d2.png")
    x0, y0 = (i % cols) * cell, (i // cols) * (cell + 14)
    if os.path.exists(p):
        im = Image.open(p).convert("RGBA")
        s = min((cell - 8) / im.width, (cell - 8) / im.height)
        s = min(s, 4.0)
        im = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.NEAREST)
        bg = Image.new("RGBA", (cell, cell), (52, 66, 48, 255))
        bg.paste(im, ((cell - im.width) // 2, (cell - im.height) // 2), im)
        m.paste(bg, (x0, y0))
    dr.text((x0 + 4, y0 + cell + 2), u, fill=(240, 240, 240))
m.save(os.path.join(OUT, "suspect_zoom.png"))
print("-> out/suspect_zoom.png")
