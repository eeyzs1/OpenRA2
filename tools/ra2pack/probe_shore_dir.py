# 分析岸线瓦片：判断每块 tile_shore_N.png 的哪些菱边邻接水域
# 菱形 64x32：中心(32,16)。四边中点：
#   右边中点(56,16)-> +x 邻格方向(屏幕右下)   顶边中点(32,2)-> -y? 需按等距映射
# 等距: sx=(tx-ty)*32, sy=(tx+ty)*16
#   +tx -> 屏幕 (+32,+16) 即右下边; +ty -> (-32,+16) 左下边
#   -tx -> (-32,-16) 左上边;        -ty -> (+32,-16) 右上边
# 对每边取沿边带状区域统计水像素占比
from PIL import Image

def is_water(r, g, b, a):
    if a < 128: return False
    # RA2 水面: 深蓝 B 显著大于 R；岸线沙滩/草地 R>=B
    return b > r + 25 and b > g + 10

def edge_band(mask, w, h, edge):
    # 沿菱边采样带：对每像素计算其到四边的"归属"
    # 用菱形参数化: u=(x-32)/32 + (y-16)/16, v=(y-16)/16 - (x-32)/32
    # u≈+1 -> +x侧边(右下); v≈+1 -> +y侧边(左下); u≈-1 -> -x侧边(左上); v≈-1 -> -y侧边(右上)
    cnt = tot = 0
    for y in range(h):
        for x in range(w):
            if not mask[y][x][3]: continue
            u = (x - 32) / 32.0 + (y - 16) / 16.0
            v = (y - 16) / 16.0 - (x - 32) / 32.0
            m = {"+x": u, "+y": v, "-x": -u, "-y": -v}[edge]
            other = max({"+x": u, "+y": v, "-x": -u, "-y": -v}[e] for e in ("+x", "+y", "-x", "-y") if e != edge)
            if m > 0.55 and m > other + 0.15:  # 靠近该边且明显属于该边
                tot += 1
                if is_water(*mask[y][x]): cnt += 1
    return cnt / tot if tot else 0.0

for i in range(12):
    im = Image.open(f"assets/sprites/tile_shore_{i}.png").convert("RGBA")
    w, h = im.size
    px = im.load()
    mask = [[px[x, y] for x in range(w)] for y in range(h)]
    fr = {e: edge_band(mask, w, h, e) for e in ("+x", "+y", "-x", "-y")}
    # 整体水占比
    allp = [mask[y][x] for y in range(h) for x in range(w) if mask[y][x][3] >= 128]
    wf = sum(1 for p in allp if is_water(*p)) / len(allp) if allp else 0
    dirs = "".join(e for e in ("+x", "+y", "-x", "-y") if fr[e] > 0.45)
    print(f"shore_{i:2d}: water={wf:4.2f}  +x={fr['+x']:.2f} +y={fr['+y']:.2f} -x={fr['-x']:.2f} -y={fr['-y']:.2f}  -> 水侧: {dirs or '(无/全)'}")
