# 最终审计：所有生成 PNG 的 RGBA / 尺寸 / alpha 覆盖率 / 空图检测
import os, glob
from PIL import Image

SPR = r"e:\AI_Generated_Projects\OpenRA2\assets\sprites"
issues = []
checked = {"unit": 0, "bld": 0, "icon": 0}

for p in sorted(glob.glob(os.path.join(SPR, "*.png"))):
    fn = os.path.basename(p)
    kind = fn.split("_")[0]
    if kind not in checked:
        continue
    im = Image.open(p)
    if im.mode != "RGBA":
        issues.append(f"{fn}: mode={im.mode}")
        im = im.convert("RGBA")
    a = im.getchannel("A")
    hist = a.histogram()
    opaque = sum(hist[16:])
    total = im.width * im.height
    cov = opaque / total
    if cov < 0.005:
        issues.append(f"{fn}: nearly empty (coverage={cov:.4f})")
    checked[kind] += 1

print("checked:", checked)
print("issues:", len(issues))
for i in issues:
    print(" ", i)

# 阵营 remap 红像素抽查（纯红 (r,0,0) 像素存在性）
def has_pure_red(path):
    im = Image.open(path).convert("RGBA")
    px = im.load()
    n = 0
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a2 = px[x, y]
            if a2 == 255 and g == 0 and b == 0 and 150 <= r <= 255:
                n += 1
    return n

for fn in ["unit_grizzly_d0_f0.png", "unit_gi_d0_f0.png", "bld_conyard.png", "bld_teslacoil.png"]:
    print(fn, "pure_red_px:", has_pure_red(os.path.join(SPR, fn)))
