# RA2 真实素材批量生成器：MIX -> PNG (assets/sprites/)
# 用法: python gen_assets.py [--only unit_grizzly,bld_conyard,...] [--sheet]
import sys, os, re, math, time
sys.path.insert(0, os.path.dirname(__file__))
from ra2lib import MixTree, Vxl, Hva, Shp, load_pal, vxl_project, render_pts, shp_frame_to_rgba, _phi_for_screen_alpha
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPR = os.path.join(ROOT, "assets", "sprites")
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

ONLY = None
SHEET = False
for a in sys.argv[1:]:
    if a.startswith("--only="):
        ONLY = set(a[7:].split(","))
    elif a == "--sheet":
        SHEET = True

t0 = time.time()
T = MixTree()
_, _r = T.find("rules.ini"); _, _a = T.find("art.ini")
RT = _r.decode("latin-1", "replace"); AT = _a.decode("latin-1", "replace")

def parse_ini(txt):
    secs = {}; cur = None
    for line in txt.splitlines():
        line = line.strip()
        if not line or line.startswith(";"):
            continue
        m = re.match(r"\[(.+?)\]", line)
        if m:
            cur = m.group(1); secs.setdefault(cur, {}); continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1)
            secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs
R = parse_ini(RT); A = parse_ini(AT)

_, _p = T.find("unittem.pal"); PAL_U = load_pal(_p)
_, _p = T.find("cameo.pal"); PAL_C = load_pal(_p)

def has(n): return T.find(n)[1] is not None
def get(n): return T.find(n)[1]

# engine 单位/建筑 -> RA2 rules id 候选
# 注：本 MIX 为 RA2 原版（无尤复）。尤复/中国特有单位用最相似的原版素材近亲渲染，
# 保证全单位视觉质量统一（真实 VXL/SHP 光影），替代粗糙程序占位。
UNITS = {
    "gi": ["E1"], "conscript": ["E2"], "engineer": ["ENGINEER"],
    "attackdog": ["ADOG", "DOG"], "spy": ["SPY"], "flaktrooper": ["FLAKT"],
    "teslatrooper": ["SHK"], "sniper": ["SNIPE"], "tanya": ["TANY"],
    "desolator": ["DESO"], "chrono": ["CLEG"], "crazyivan": ["IVAN"],
    "terrorist": ["TERROR"], "navyseal": ["GHOST"], "yuri": ["YURI"],
    "chronocommando": ["CCOMAND"], "psicommando": ["PTROOP"], "rocketeer": ["JUMPJET"],
    "mcv": ["AMCV", "SMCV"], "harvester": ["HARV"], "chronominer": ["CMIN"],
    "warminer": ["HARV"], "grizzly": ["MTNK"], "rhino": ["HTNK"], "flaktrack": ["HTK"],
    "ifv": ["FV"], "prismtank": ["SREF"], "teslatank": ["TTNK"], "miragetank": ["MGTK"],
    "v3launcher": ["V3"], "apocalypse": ["APOC"], "terrordrone": ["DRON"],
    "demotruck": ["DTRUCK"], "tankdestroyer": ["TNKD"],
    "intruder": ["ORCA"], "blackeagle": ["BEAG"], "kirov": ["ZEP"],
    "nighthawk": ["SHAD"], "hornet": ["HORNET"],
    "destroyer": ["DEST"], "typhoon": ["SUB"], "aegis": ["AEGIS"],
    "seascorpion": ["HYD"], "dreadnought": ["DRED"], "aircraftcarrier": ["CARRIER"],
    "amphtransport": ["SAPC"], "dolphin": ["DLPH"], "squid": ["SQD"],
    # ---- 近亲替代（MIX 无此单位）----
    "pla": ["E2"],               # 解放军 -> 动员兵（苏联系步兵）
    "guardiangi": ["E1"],        # 重装大兵 -> 大兵
    "initiate": ["E2"],          # 尤里新兵 -> 动员兵
    "brute": ["DESO"],           # 狂兽人 -> 辐射工兵（大块头）
    "virus": ["SNIPE"],          # 病毒狙击手 -> 狙击手
    "boris": ["GHOST"],          # 鲍里斯 -> 海豹部队
    "type99": ["HTNK"],          # 99式 -> 犀牛
    "robottank": ["MTNK"],       # 遥控坦克 -> 灰熊
    "battlefortress": ["APOC"],  # 战斗要塞 -> 天启（本 MIX Image=mtnk 大型战车）
    "gatlingtank": ["HTK"],      # 盖特坦克 -> 防空履带车
    "magnetron": ["TTNK"],       # 磁电坦克 -> 磁能坦克
    "mastermind": ["APOC"],      # 主脑坦克 -> 天启（大型车体）
    "chaosdrone": ["DRON"],      # 混乱无人机 -> 恐怖机器人
    "mig": ["BEAG"],             # 米格 -> 黑鹰
    "siegechopper": ["SHAD"],    # 攻城直升机 -> 夜鹰
    "boomer": ["SUB"],           # 雷鸣潜艇 -> 台风
    "floatingdisc": ["ZEP"],     # 镭射幽浮 -> 基洛夫（同为大型慢速空军，原版无飞碟素材）
    "lashertank": ["LTNK"],      # 狂风坦克（本 MIX 有真实 ltnk.vxl）
}
INFANTRY = {"gi", "conscript", "engineer", "spy", "flaktrooper",
            "teslatrooper", "sniper", "tanya", "desolator", "chrono", "crazyivan",
            "terrorist", "navyseal", "yuri", "chronocommando", "psicommando",
            "rocketeer", "guardiangi", "pla", "initiate", "brute", "virus", "boris"}
MINERS = {"harvester", "chronominer", "warminer"}
# 卸货动画 VXL（HORV/CMON）：与车体 Image 不同
MINER_UNLOAD_VXL = {"harvester": "horv", "chronominer": "cmon", "warminer": "horv"}
# 引擎 MoveType（src/game/data.cpp）：空军/海军不烘地面投影，锚点=内容中心
AIR = {"intruder", "blackeagle", "kirov", "nighthawk", "hornet", "rocketeer",
       "mig", "siegechopper", "floatingdisc"}
NAVAL = {"destroyer", "typhoon", "aegis", "seascorpion", "dreadnought",
         "aircraftcarrier", "dolphin", "squid", "boomer"}
BLDS = {
    "conyard": ["GACNST"], "powerplant": ["GAPOWR"], "teslareactor": ["NAPOWR"],
    "nuclearreactor": ["NANRCT"], "barracks": ["GAPILE"], "warfactory": ["GAWEAP"],
    "orerefinery": ["GAREFN"], "radar": ["GARADR", "NARADR"], "battlelab": ["GATECH"],
    "airforcecmd": ["GAAIRC"], "navalyard": ["GAYARD"], "pillbox": ["GAPILL"],
    "sentrygun": ["NALASR"], "prismtower": ["ATESLA"], "teslacoil": ["TESLA"],
    "flakcannon": ["NAFLAK"], "grandcannon": ["GTGCAN"], "patriotmissile": ["NASAM"],
    "wall": ["GAWALL"], "orepurifier": ["GAOREP"], "industrialplant": ["NAINDP", "NAREFN"],
    "techpowerplant": ["CAPOWR", "GAPOWR"], "nukesilo": ["NAMISL"],
    "weatherdevice": ["GAWEAT"], "ironcurtain": ["NAIRON"], "chronosphere": ["GACSPH"],
    "oilderrick": ["CAOILD"], "hospital": ["CAHOSP", "CATHOSP"], "machineshop": ["CAOUTP", "CAMACH"],
    "cloningvat": ["NACLON"], "servicedepot": ["GADEPT"], "gapgenerator": ["GAGAP"],
    "spysat": ["GASPYSAT"], "psychicsensor": ["NAPSIS"], "techairport": ["CAAIRP"],
    "secretlab": ["CASLAB", "CALAB"], "civhouse": ["CAHSE01"], "techoutpost": ["CAOUTP"],
    # ---- YR/尤里建筑：本 MIX 无 SHP，用最相似原版建筑近亲渲染 ----
    "battlebunker": ["NABNKR", "GAPILL"],       # 战斗碉堡 -> 机枪碉堡
    "tankbunker": ["NATBNK", "GADEPT"],         # 坦克碉堡 -> 维修厂平台
    "bioreactor": ["YAPOWR", "NAPOWR"],         # 生化反应炉 -> 磁能反应炉
    "gatlingcannon": ["YAGGUN", "NAFLAK"],      # 盖特机炮 -> 防空炮
    "grinder": ["YAGRND", "NAWEAP"],            # 研磨机 -> 苏军战车工厂
    "geneticmutator": ["YAGNTC", "NACLON"],     # 基因突变器 -> 复制中心（生物科技）
    "psychicdominator": ["YAPSYC", "NAIRON"],   # 心灵控制器 -> 铁幕装置（大型装置）
    "psychictower": ["YAPSYT", "NAPSIS"],       # 心灵控制塔 -> 心灵探测器
}
# 主 SHP 只是基坑/埋地状态，完整建筑在 mk 建造动画（帧位置自动选最大不透明帧）
BLD_FROM_MK = {"grandcannon": "gagcanmk", "nukesilo": "namislmk", "sentrygun": "nalasrmk",
               "flakcannon": "naflakmk", "gatlingcannon": "naflakmk",
               "servicedepot": "gadeptmk", "tankbunker": "gadeptmk",
               "battlebunker": "gapillmk"}
# 墙体：f0 是孤立门柱，f5 是连续墙段
BLD_FRAME0 = {"wall": 5}
# 建造动画关键帧数（mk 均采 + 完整帧）
MK_KEYS = 6

_size_cache = {}
def ph_size(kind, name, default):
    """占位 PNG 尺寸（引擎期望画布）；无占位图用 default"""
    key = (kind, name)
    if key not in _size_cache:
        pat = {"unit": f"unit_{name}_d0_f0.png", "bld": f"bld_{name}.png",
               "turret": f"turret_{name}_d0.png",
               "icon_unit": f"icon_unit_{name}.png", "icon_bld": f"icon_bld_{name}.png"}[kind]
        p = os.path.join(SPR, pat)
        if os.path.exists(p):
            with Image.open(p) as im:
                _size_cache[key] = im.size
        else:
            _size_cache[key] = default
    return _size_cache[key]

# ------------------------------------------------------------- VXL 渲染
# RA2 源瓦片 60×30 → 引擎 64×32；VXL/建筑共用此比例
RA2_TILE_W, ENGINE_TILE_W = 60, 64
BLD_SCALE = ENGINE_TILE_W / RA2_TILE_W

def render_voxel_unit(img, canvas, eng=""):
    """8 方向渲染，返回 ([PIL]*8, layout) 或 (None, None)。
    layout = {scale, w, ch, floating, orgs:[(orgx,orgy)*8]} 供炮塔同坐标系叠绘。
    固定 px_per_voxel = 64/60；地面单位南触点 y=0.72h。"""
    vd = get(img + ".vxl")
    if not vd:
        return None, None
    # 静态站姿烘焙：不用 HVA（与炮塔/炮管共用 section 坐标系对齐）。
    # HVA 大平移在单独烘炮塔时会被丢弃，若车体保留会导致错位。
    v = Vxl(vd); h = None
    w, ch = canvas
    floating = eng in AIR or eng in NAVAL
    per = []
    maxbw = 0.0; maxbh = 0.0
    for e in range(8):
        pts, zmin = vxl_project(v, h, _phi_for_screen_alpha(45 * e))
        if not pts:
            return None, None
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        maxbw = max(maxbw, max(xs) - min(xs)); maxbh = max(maxbh, max(ys) - min(ys))
        per.append((pts, zmin, min(xs), max(xs), min(ys), max(ys)))
    bw = maxbw + 1.3; bh = maxbh + 1.3
    scale = ENGINE_TILE_W / float(RA2_TILE_W)
    margin = 2
    need_w = int(bw * scale) + 2 * margin
    if floating:
        need_h = int(bh * scale) + 2 * margin
    else:
        need_h = max(int(bh * scale / 0.72) + margin, int(bh * scale) + 2 * margin)
    w = max(w, need_w)
    ch = max(ch, need_h)
    anchor_y = ch / 2 + 4 if floating else ch * 0.72
    out = []
    orgs = []
    for e in range(8):
        pts, zmin, mnx, mxx, mny, mxy = per[e]
        if floating:
            gx = (mnx + mxx) / 2; gy = (mny + mxy) / 2
        else:
            ycut = mxy - 1.2
            low = [p for p in pts if p[1] >= ycut]
            if low:
                gx = sum(p[0] for p in low) / len(low)
                gy = mxy
            else:
                gx = (mnx + mxx) / 2; gy = mxy
        orgx = w / 2 - gx * scale
        orgy = anchor_y - gy * scale + 0.5 * scale
        orgs.append((orgx, orgy))
        out.append(render_pts(pts, PAL_U, scale, orgx, orgy, w, ch, supersample=2))
    layout = {"scale": scale, "w": w, "ch": ch, "floating": floating, "orgs": orgs}
    return out, layout

# ------------------------------------------------------------- SHP 工具
def shp_frame_img(shp, i, pal, remap=True):
    fr = shp.frame_pixels(i)
    if fr.w == 0 or fr.h == 0:
        return None
    return shp_frame_to_rgba(fr, pal, remap=remap), (fr.x, fr.y, fr.w, fr.h)

def composite_frames(shp, idxs, pal, remap=True):
    """把若干帧按各自 (x,y) 画到 SHP 大画布，返回自动裁剪后的 (img, bbox)"""
    big = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
    for i in idxs:
        r = shp_frame_img(shp, i, pal, remap)
        if r:
            fi, (x, y, w, h) = r
            big.paste(fi, (x, y), fi)
    bbox = big.getbbox()
    if not bbox:
        return None
    return big.crop(bbox)

def place_bottom_center(content, canvas, bottom_margin):
    """内容等比缩放到画布内（仅缩小或<=1.6x放大），底边对齐 h-bottom_margin，水平居中。
    仅用于单位/图标；建筑必须保留 SHP 原画布偏移，见 render_building。"""
    w, ch = canvas
    cw, chh = content.size
    if cw == 0 or chh == 0:
        return Image.new("RGBA", canvas, (0, 0, 0, 0))
    availh = ch - bottom_margin - 1
    s = min(w / cw, availh / chh)
    s = min(s, 2.0)
    if abs(s - 1.0) > 0.02:
        content = content.resize((max(1, round(cw * s)), max(1, round(chh * s))), Image.LANCZOS)
    img = Image.new("RGBA", canvas, (0, 0, 0, 0))
    x = (w - content.width) // 2
    y = ch - bottom_margin - content.height
    img.paste(content, (x, y), content)
    return img

# BLD_SCALE / ENGINE_TILE_W 定义见上方 VXL 节

def scale_bld_canvas(img):
    """NEAREST 放大到引擎瓦片比例，保持锐利像素边（RA2 原作观感）"""
    if abs(BLD_SCALE - 1.0) < 0.001:
        return img
    nw = max(1, round(img.width * BLD_SCALE))
    nh = max(1, round(img.height * BLD_SCALE))
    return img.resize((nw, nh), Image.NEAREST)

# ------------------------------------------------------------- 步兵/SHP 单位
# art.ini 序列驱动（权威帧映射）：Ready=站立 Walk=行走 FireUp=开火 Die1=死亡
# 布局规律：方向块打包 —— 方向 s 的相位 p 帧号 = start + s*frames + p（die 无方向：start+p）
# 方向映射：SHP f0=南 逆时针(S,SW,W,NW,N,NE,E,SE)；引擎 dir e=东起顺时针 => s=(e+6)%8
SEQ_MAP = {
    "gi": "GISequence", "guardiangi": "GISequence",
    "conscript": "ConSequence", "pla": "ConSequence", "initiate": "ConSequence",
    "teslatrooper": "ConSequence", "sniper": "ConSequence", "virus": "ConSequence",
    "engineer": "EngineerSequence", "attackdog": "DogSequence", "spy": "SpySequence",
    "flaktrooper": "FlakSequence", "tanya": "TanyaSequence",
    "desolator": "DesoSequence", "brute": "DesoSequence",
    "chrono": "ClegSequence", "crazyivan": "IvanSequence", "terrorist": "TerroristSequence",
    "navyseal": "SealSequence", "boris": "SealSequence",
    "yuri": "YuriSequence", "chronocommando": "ComandoSequence",
    "psicommando": "PsiTroopSequence", "rocketeer": "RocketeerSequence",
}
ANIM_META = {"units": {}, "blds": {}}  # 写出到 assets/sprites/anims.ini
# --only 增量运行：先读入既有 anims.ini，末尾覆盖写回时保留未触及条目（全量运行时自然整体刷新）
_anims_path = os.path.join(SPR, "anims.ini")
if ONLY and os.path.exists(_anims_path):
    _sec = None
    for ln in open(_anims_path, encoding="utf-8"):
        ln = ln.strip()
        if ln.startswith("[") and ln.endswith("]"):
            _sec = ln[1:-1]
            (ANIM_META["blds"] if _sec.startswith("bld_") else ANIM_META["units"]).setdefault(
                _sec[4:] if _sec.startswith("bld_") else _sec, {})
        elif "=" in ln and _sec:
            k, v = ln.split("=", 1)
            d = ANIM_META["blds"] if _sec.startswith("bld_") else ANIM_META["units"]
            d[_sec[4:] if _sec.startswith("bld_") else _sec][k.strip()] = v.strip()

def seq_part(seq, key):
    """解析序列段某键 -> (start, frames, rate)；缺失/无效返回 None"""
    v = A.get(seq, {}).get(key)
    if not v:
        return None
    ps = v.split(",")
    if len(ps) < 3:
        return None
    try:
        st, fr, ra = int(ps[0]), int(ps[1]), int(ps[2])
    except ValueError:
        return None
    if fr <= 0 or st < 0:
        return None
    return (st, fr, ra)

def render_seq_unit(img, eng, canvas):
    """art.ini 序列全套提取：stand(legacy d{e}f0)/walk/fire/die/dep。
    返回 True/False；帧 PNG 直接落盘，元数据记入 ANIM_META。"""
    seq = SEQ_MAP.get(eng)
    if not seq or seq not in A:
        return False
    sd = get(img + ".shp")
    if not sd:
        return False
    shp = Shp(sd)
    n = shp.nframes
    ready = seq_part(seq, "Ready")
    walk = seq_part(seq, "Walk")
    fire = seq_part(seq, "FireUp")
    die = seq_part(seq, "Die1")
    dep = seq_part(seq, "Deployed") or seq_part(seq, "Prone")
    if not ready:
        return False
    def grab(idxs):
        c = composite_frames(shp, idxs, PAL_U)
        return place_bottom_center(c, canvas, 3) if c else None
    meta = {}
    # 站立（legacy 命名 unit_eng_d{e}_f0.png）
    for e in range(8):
        s = (e + 6) % 8
        im = grab([ready[0] + s * ready[1]])
        if not im:
            return False
        save(im, f"unit_{eng}_d{e}_f0.png")
    # 行走（多相位）
    if walk and walk[0] + 8 * walk[1] <= n:
        meta["walk"] = walk[1]
        meta["walkrate"] = max(1, walk[2])
        for e in range(8):
            s = (e + 6) % 8
            for p in range(walk[1]):
                im = grab([walk[0] + s * walk[1] + p])
                if not im:
                    return False
                save(im, f"unit_{eng}_walk_d{e}_f{p}.png")
    # 开火（多相位）
    if fire and fire[0] + 8 * fire[1] <= n:
        meta["fire"] = fire[1]
        meta["firerate"] = max(1, fire[2])
        for e in range(8):
            s = (e + 6) % 8
            for p in range(fire[1]):
                im = grab([fire[0] + s * fire[1] + p])
                if not im:
                    return False
                save(im, f"unit_{eng}_fire_d{e}_f{p}.png")
    # 死亡（无方向，顺序帧）
    if die and die[0] + die[1] <= n:
        meta["die"] = die[1]
        for p in range(die[1]):
            im = grab([die[0] + p])
            if not im:
                return False
            save(im, f"unit_{eng}_die_f{p}.png")
    # 部署站姿（deso Deployed / 其他 Prone 匍匐）
    if dep and dep[0] + 8 * dep[1] <= n:
        meta["dep"] = 1
        for e in range(8):
            s = (e + 6) % 8
            im = grab([dep[0] + s * dep[1]])
            if not im:
                return False
            save(im, f"unit_{eng}_dep_d{e}.png")
    ANIM_META["units"][eng] = meta
    return True

# 特殊 SHP 结构（经本 MIX 逐帧实测）：
#  - 恐怖机器人 dron（176帧=22相位×8方向交错：帧=相位*8+方向）：相位0站立 相位1..6 行走
#  - 海豚 dlph（48帧全圆）：f0=N f12=E f24=S f36=W（顺时针）→ f=(12+6e)%48
#  - 乌贼 sqd（296帧=8方向×37帧动画）：各方向块内取平静帧(+9)
DRONE_SHP = {"terrordrone", "chaosdrone"}
NAVAL_ROUND48 = {"dolphin"}
BLOCK_SHP = {"squid": 37}

def render_drone_unit(img, eng, canvas):
    """恐怖机器人：相位交错帧。stand=相位0，walk=相位1..6（腿部循环）"""
    sd = get(img + ".shp")
    if not sd:
        return False
    shp = Shp(sd)
    n = shp.nframes  # 176 = 22 相位 × 8 方向
    nph = n // 8
    wp = min(6, nph - 1)
    for e in range(8):
        s = (e + 6) % 8
        f0 = composite_frames(shp, [s], PAL_U)
        if not f0:
            return False
        save(place_bottom_center(f0, canvas, 3), f"unit_{eng}_d{e}_f0.png")
        for p in range(wp):
            f1 = composite_frames(shp, [(1 + p) * 8 + s], PAL_U)
            if not f1:
                return False
            save(place_bottom_center(f1, canvas, 3), f"unit_{eng}_walk_d{e}_f{p}.png")
    ANIM_META["units"][eng] = {"walk": wp, "walkrate": 2}
    return True

def render_shp_unit(img, eng, canvas):
    sd = get(img + ".shp")
    if not sd:
        return None
    shp = Shp(sd)
    n = shp.nframes
    frames = {}
    # 方向映射（步兵/机器人实测）：SHP f0=南 逆时针(S,SW,W,NW,N,NE,E,SE)
    # 引擎 dir e = 东起顺时针 => 帧 s=(e+6)%8
    if eng in NAVAL_ROUND48:
        for e in range(8):
            f0 = composite_frames(shp, [(12 + 6 * e) % 48], PAL_U)
            if not f0:
                return None
            frames[(e, 0)] = place_bottom_center(f0, canvas, 3)
    elif eng in BLOCK_SHP:
        bs = BLOCK_SHP[eng]
        for e in range(8):
            s = (e + 6) % 8
            i0 = s * bs
            f0 = composite_frames(shp, [i0 + (9 if eng == "squid" else 0)], PAL_U)
            if not f0:
                return None
            frames[(e, 0)] = place_bottom_center(f0, canvas, 3)
    else:
        for e in range(8):
            s = (e + 6) % 8
            if s >= n:
                s = s % n
            f0 = composite_frames(shp, [s], PAL_U)
            if not f0:
                return None
            frames[(e, 0)] = place_bottom_center(f0, canvas, 3)
    return frames

# ------------------------------------------------------------- 炮塔（tur.vxl 8 方向）
# 近亲替代单位的炮塔跟随近亲（type99 用犀牛炮塔等）
# 炮塔：与车体 Image stem 配对（*tur.vxl）；画布约为车体 55%，避免旧超大占位把炮塔撑满画布
TURRETS = {
    "grizzly": "gtnktur", "rhino": "htnktur", "type99": "htnktur",
    "apocalypse": "mtnktur", "prismtank": "sreftur", "teslatank": "ttnktur",
    "ifv": "fvtur", "flaktrack": "htktur", "miragetank": "rtnktur",
    "robottank": "gtnktur", "lashertank": "ltnktur", "gatlingtank": "htktur",
    "magnetron": "ttnktur", "mastermind": "mtnktur",
}

def _merge_vxl(*vxls):
    """合并多个 VXL（同坐标系：炮塔+炮管），保留各自 section。"""
    out = Vxl.__new__(Vxl)
    out.sections = []
    for v in vxls:
        if v is None:
            continue
        out.sections.extend(v.sections)
    return out

def render_turret(tur, layout):
    """tur+barl 与车体同一画布/原点叠绘（layout 来自 render_voxel_unit）。"""
    if not layout:
        return None
    vd = get(tur + ".vxl")
    if not vd:
        return None
    vtur = Vxl(vd)
    barl_name = tur[:-3] + "barl" if tur.endswith("tur") else tur + "barl"
    vbarl = Vxl(get(barl_name + ".vxl")) if has(barl_name + ".vxl") else None
    v = _merge_vxl(vtur, vbarl)
    scale = layout["scale"]
    w, ch = layout["w"], layout["ch"]
    orgs = layout["orgs"]
    out = []
    for e in range(8):
        pts, zmin = vxl_project(v, None, _phi_for_screen_alpha(45 * e))
        if not pts:
            return None
        orgx, orgy = orgs[e]
        out.append(render_pts(pts, PAL_U, scale, orgx, orgy, w, ch, supersample=2))
    return out

def render_turret_centered(tur, canvas):
    """无车体 layout 时回退：内容居中（近亲表兜底）。"""
    vd = get(tur + ".vxl")
    if not vd:
        return None
    vtur = Vxl(vd)
    barl_name = tur[:-3] + "barl" if tur.endswith("tur") else tur + "barl"
    vbarl = Vxl(get(barl_name + ".vxl")) if has(barl_name + ".vxl") else None
    v = _merge_vxl(vtur, vbarl)
    w, ch = canvas
    per = []
    maxbw = 0.0; maxbh = 0.0
    for e in range(8):
        pts, zmin = vxl_project(v, None, _phi_for_screen_alpha(45 * e))
        if not pts:
            return None
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        maxbw = max(maxbw, max(xs) - min(xs)); maxbh = max(maxbh, max(ys) - min(ys))
        per.append((pts, min(xs), max(xs), min(ys), max(ys)))
    scale = ENGINE_TILE_W / float(RA2_TILE_W)
    need = int(max(maxbw, maxbh) * scale) + 6
    w = max(w, need); ch = max(ch, need)
    out = []
    for e in range(8):
        pts, mnx, mxx, mny, mxy = per[e]
        gx = (mnx + mxx) / 2; gy = (mny + mxy) / 2
        orgx = w / 2 - gx * scale
        orgy = ch / 2 - gy * scale
        out.append(render_pts(pts, PAL_U, scale, orgx, orgy, w, ch, supersample=2))
    return out

# ------------------------------------------------------------- 建筑
def bib_shp_candidates(bibshape: str):
    """art.ini BibShape（如 GAREFNBB）→ 温带优先的 SHP 候选名。"""
    b = bibshape.lower().strip()
    if not b:
        return []
    out = []
    if len(b) >= 3:
        for c in "gtuas":
            out.append(b[0] + c + b[2:])
    out.append(b)
    seen = set()
    return [x for x in out if not (x in seen or seen.add(x))]

def render_building(img, canvas=None, frame0=0, single=True, bib=None, active_anim=None, remap=True):
    """保留 SHP 原画布与帧偏移（地基对齐关键），再按 64/60 放大到引擎瓦片。
    禁止 crop+居中：会剪掉地基留白，建成后地面缺角、比例错位。
    bib：art.ini BibShape；active_anim：ActiveAnim（油田泵机等补全缺块）。
    Remapable=no 的科技建筑必须 remap=False，否则 16..31 被画成亮红。"""
    sd = get(img + ".shp")
    if not sd:
        return None
    shp = Shp(sd)
    n = shp.nframes
    if frame0 < 0:
        # mk 建造动画：完整建筑帧位置不一（弹出式防御炮在中段，核弹井在后段），
        # 取最大不透明像素帧（完整建筑像素最多，火花特效帧像素少）
        best, besta = 0, -1
        for i in range(n):
            fr = shp.frame_pixels(i)
            a = sum(1 for v in fr.pixels if v != 0)
            if a > besta:
                best, besta = i, a
        frame0 = best
    # 基帧 + 后续小覆盖帧（旗帜/天线等）；最多叠 3 层以免特效帧污染
    idxs = [frame0]
    f0 = shp_frame_img(shp, frame0, PAL_U, remap=remap)
    if not f0:
        return None
    a0 = f0[1][2] * f0[1][3]
    if not single:
        for i in range(frame0 + 1, n):
            fi = shp_frame_img(shp, i, PAL_U, remap=remap)
            if not fi:
                continue
            area = fi[1][2] * fi[1][3]
            if area < a0 * 0.45:
                idxs.append(i)
                if len(idxs) >= 4:
                    break
    big = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
    # BibShape：南侧地基垫，必须在主体之下，否则精炼厂/战车厂看起来“缺半截”
    if bib:
        for stem in bib_shp_candidates(bib):
            if not has(stem + ".shp"):
                continue
            bshp = Shp(get(stem + ".shp"))
            fr0 = bshp.frame_pixels(0)
            if fr0 and fr0.w > 0:
                r = shp_frame_img(bshp, 0, PAL_U, remap=False)
                if r:
                    fi, (x, y, w, h) = r
                    if x + fi.width > big.width or y + fi.height > big.height or x < 0 or y < 0:
                        nw = max(big.width, x + fi.width, bshp.w)
                        nh = max(big.height, y + fi.height, bshp.h)
                        bigger = Image.new("RGBA", (nw, nh), (0, 0, 0, 0))
                        bigger.paste(big, (0, 0), big)
                        big = bigger
                    big.paste(fi, (max(0, x), max(0, y)), fi)
            break
    for i in idxs:
        r = shp_frame_img(shp, i, PAL_U, remap=remap)
        if r:
            fi, (x, y, w, h) = r
            big.paste(fi, (x, y), fi)
    # ActiveAnim 静态取帧0（油田泵机/动画层补全主体缺块）
    if active_anim:
        for stem in bib_shp_candidates(active_anim):  # 同 theater 字母替换
            if not has(stem + ".shp"):
                continue
            ash = Shp(get(stem + ".shp"))
            fr = ash.frame_pixels(0)
            if not fr or fr.w <= 0:
                break
            r = shp_frame_img(ash, 0, PAL_U, remap=remap)
            if r:
                fi, (x, y, w, h) = r
                if x + fi.width > big.width or y + fi.height > big.height or x < 0 or y < 0:
                    nw = max(big.width, x + fi.width, ash.w)
                    nh = max(big.height, y + fi.height, ash.h)
                    bigger = Image.new("RGBA", (nw, nh), (0, 0, 0, 0))
                    bigger.paste(big, (0, 0), big)
                    big = bigger
                big.paste(fi, (max(0, x), max(0, y)), fi)
            break
    if big.getbbox() is None:
        return None
    return scale_bld_canvas(big)

# ------------------------------------------------------------- 图标
def render_icon(cameo, canvas):
    sd = get(cameo + ".shp")
    if not sd:
        return None
    shp = Shp(sd)
    # 中文包 4 个损坏 cameo（cnsticon/empicon/lpsticon/npsiicon）：
    # 帧头 comp==0 但帧非空，像素流为椒盐损坏数据（xor0x80 可辨形但不可用）-> 跳过走合成
    x0, y0, w0, h0, comp0, off0 = shp.frames[0]
    if comp0 == 0 and w0 * h0 > 0:
        return None
    # cameo.pal 的 16..31 不是阵营色（多为蓝灰抖动底纹），图标不做 remap
    content = composite_frames(shp, [0], PAL_C, remap=False)
    if not content:
        return None
    return content.resize(canvas, Image.LANCZOS)

def synth_icon_from_bld(shp_stem, canvas):
    """损坏 cameo 的回退：建筑本体 SHP 帧0（+小覆盖帧）合成图标，天空底色"""
    shp = Shp(get(shp_stem + ".shp"))
    idxs = [0]
    f0 = shp_frame_img(shp, 0, PAL_U)
    if not f0:
        return None
    a0 = f0[1][2] * f0[1][3]
    for i in range(1, shp.nframes):
        fi = shp_frame_img(shp, i, PAL_U)
        if fi and fi[1][2] * fi[1][3] < a0 * 0.4:
            idxs.append(i)
            break
    content = composite_frames(shp, idxs, PAL_U, remap=False)
    if not content:
        return None
    w, ch = canvas
    s = min((w - 8) / content.width, (ch - 8) / content.height)
    ic = content.resize((max(1, round(content.width * s)), max(1, round(content.height * s))), Image.LANCZOS)
    cv = Image.new("RGBA", canvas, (96, 132, 168, 255))
    cv.paste(ic, ((w - ic.width) // 2, (ch - ic.height) // 2), ic)
    return cv

# ------------------------------------------------------------- 主流程
report = {"units_ok": [], "units_skip": [], "blds_ok": [], "blds_skip": [],
          "icons_ok": [], "icons_skip": []}

def save(img, name):
    img.save(os.path.join(SPR, name), "PNG")

for eng, cands in UNITS.items():
    if ONLY and ("unit_" + eng) not in ONLY and eng not in ONLY:
        continue
    rid = next((c for c in cands if c in R), None)
    if not rid:
        report["units_skip"].append((eng, "no rules id")); continue
    img = R[rid].get("Image", rid).lower()
    ok = False
    layout = None
    if has(img + ".vxl"):
        # 采矿车加大画布，避免货舱被裁切；地面车辆随内容尺寸，勿强行 128（1:1 体素会显得过小）
        default = (140, 140) if eng in MINERS else (72, 72)
        canvas = ph_size("unit", eng, default)
        # 强制采矿车升到至少 120；其它地面载具至少 64（旧超大占位会偏空）
        if eng in MINERS:
            canvas = (max(canvas[0], 140), max(canvas[1], 140))
        elif eng not in AIR and eng not in NAVAL and eng not in INFANTRY:
            canvas = (max(min(canvas[0], 96), 64), max(min(canvas[1], 96), 64))
        dirs, layout = render_voxel_unit(img, canvas, eng)
        if dirs:
            for e in range(8):
                save(dirs[e], f"unit_{eng}_d{e}_f0.png")
            if eng in MINERS:
                for e in range(8):
                    # 满载：货舱区域略提亮偏黄（VXL 无独立满载帧，用色调区分空/满）
                    full = dirs[e].copy()
                    px = full.load()
                    w, h = full.size
                    for y in range(h):
                        for x in range(w):
                            r, g, b, a = px[x, y]
                            if a < 128: continue
                            # 中后部货舱带：避开驾驶室 remap 红
                            if r > 150 and g < 90 and b < 90: continue
                            if x < w * 0.35:
                                nr = min(255, int(r * 1.12 + 18))
                                ng = min(255, int(g * 1.08 + 10))
                                nb = min(255, int(b * 0.92))
                                px[x, y] = (nr, ng, nb, a)
                    save(full, f"unit_{eng}_d{e}_f1.png")
            if eng in MINER_UNLOAD_VXL:
                uvxl = MINER_UNLOAD_VXL[eng]
                if has(uvxl + ".vxl"):
                    udirs, _ = render_voxel_unit(uvxl, canvas, eng)
                    if udirs:
                        for e in range(8):
                            save(udirs[e], f"unit_{eng}_unload_d{e}_f0.png")
            ok = True
    elif has(img + ".shp"):
        canvas = ph_size("unit", eng, (24, 30) if eng in INFANTRY or eng == "attackdog" else (60, 60))
        # 优先：art.ini 序列全套（stand/walk/fire/die/dep）；其次：机器人相位交错；兜底：方向帧
        if render_seq_unit(img, eng, canvas):
            ok = True
        elif eng in DRONE_SHP and render_drone_unit(img, eng, canvas):
            ok = True
        else:
            fr = render_shp_unit(img, eng, canvas)
            if fr:
                for (e, f), im in fr.items():
                    save(im, f"unit_{eng}_d{e}_f{f}.png")
                ok = True
    if ok:
        report["units_ok"].append((eng, rid, img))
    else:
        report["units_skip"].append((eng, f"{rid}/{img} file missing"))
    # 炮塔：与车体同一坐标系/画布叠绘（layout）；失败则居中兜底
    if ok:
        tur = img + "tur"
        if not has(tur + ".vxl") and eng in TURRETS:
            tur = TURRETS[eng]
        if has(tur + ".vxl"):
            tdirs = render_turret(tur, layout) if layout else None
            if not tdirs:
                tdirs = render_turret_centered(tur, (48, 48))
            if tdirs:
                for e in range(8):
                    save(tdirs[e], f"turret_{eng}_d{e}.png")
    elif eng in TURRETS:
        tdirs = render_turret_centered(TURRETS[eng], (48, 48))
        if tdirs:
            for e in range(8):
                save(tdirs[e], f"turret_{eng}_d{e}.png")
    # 图标
    asec = A.get(R[rid].get("Image", rid).upper(), {}) or A.get(rid, {})
    cameo = asec.get("Cameo", "").lower()
    for cand in ([cameo + ".shp"] if cameo else []) + [img + "icon.shp", rid.lower() + "icon.shp"]:
        if has(cand):
            ic = render_icon(cand[:-4], (108, 84))
            if ic:
                save(ic, f"icon_unit_{eng}.png")
                report["icons_ok"].append(("unit_" + eng, cand)); break
    else:
        # cameo 缺失或损坏（中文包 comp==0）-> 用刚生成的本体 d2 帧合成图标
        body = os.path.join(SPR, f"unit_{eng}_d2_f0.png")
        if ok and os.path.exists(body):
            with Image.open(body) as bm:
                im = bm.convert("RGBA").copy()
            w, ch = 108, 84
            s = min((w - 16) / im.width, (ch - 16) / im.height)
            im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
            cv = Image.new("RGBA", (w, ch), (96, 132, 168, 255))
            cv.paste(im, ((w - im.width) // 2, (ch - im.height) // 2), im)
            save(cv, f"icon_unit_{eng}.png")
            report["icons_ok"].append(("unit_" + eng, "synth:body"))
        else:
            report["icons_skip"].append(("unit_" + eng, "no cameo"))

for eng, cands in BLDS.items():
    if ONLY and ("bld_" + eng) not in ONLY and eng not in ONLY:
        continue
    rid = next((c for c in cands if c in R), None) or cands[0]
    img = R.get(rid, {}).get("Image", rid).lower()
    asec = A.get(img.upper(), {}) or A.get(rid, {})
    # 温带地图优先：generic(G) 是本安装里真正的温带静态建筑；T 多为 mk；A 是雪地勿抢先
    # NewTheater：第2字符 = T温 / A雪 / U城 / G通用
    civ = img.startswith("c")
    order = "gtuas"  # G 温带通用 → T → U → A雪 → S
    tv = [img[0] + c + img[2:] + ".shp" for c in order] if len(img) >= 3 else []
    tries = (tv + [img + ".shp", img + "_a.shp"]) if civ \
        else ([img[0] + "g" + img[2:] + ".shp", img[0] + "t" + img[2:] + ".shp",
               img + ".shp", img + "_a.shp"] + tv)
    tries += [rid.lower() + ".shp", rid.lower() + "_a.shp"]
    # 去重保序
    seen = set(); tries = [x for x in tries if not (x in seen or seen.add(x))]
    found = next((x for x in tries if has(x)), None)
    # 完整建筑在 mk：优先温带 mk（gt*mk / nt*mk），再回退
    mk_stem = BLD_FROM_MK.get(eng)
    use_mk = bool(mk_stem) and has(mk_stem + ".shp")
    if use_mk:
        found = mk_stem + ".shp"
    frame0 = -1 if use_mk else BLD_FRAME0.get(eng, 0)
    bib = asec.get("BibShape") or A.get(rid, {}).get("BibShape")
    active = asec.get("ActiveAnim") or A.get(rid, {}).get("ActiveAnim")
    # Remapable 缺省 yes；科技/民用常 no —— 误 remap 会把屋顶/标识打成亮红
    remap_s = (asec.get("Remapable") or A.get(rid, {}).get("Remapable") or "yes").lower()
    do_remap = remap_s not in ("no", "false", "0")
    if found:
        b = render_building(found[:-4], frame0=frame0, single=use_mk, bib=bib,
                            active_anim=active, remap=do_remap)
        if b:
            tag = found
            if bib: tag += f"+bib:{bib}"
            if active: tag += f"+anim:{active}"
            if not do_remap: tag += "+noremap"
            save(b, f"bld_{eng}.png")
            report["blds_ok"].append((eng, rid, tag))
        else:
            report["blds_skip"].append((eng, f"{found} render fail"))
    else:
        report["blds_skip"].append((eng, f"{rid}/{img} shp missing"))
    # ---- 建造动画关键帧：优先温带 mk（g* → t*mk） ----
    mk_auto = mk_stem if use_mk else None
    if not mk_auto and len(img) >= 3:
        for letter in "gtua":
            cand = img[0] + letter + img[2:] + "mk"
            if has(cand + ".shp"):
                mk_auto = cand
                break
    if not mk_auto:
        for cand in [img + "mk", rid.lower() + "mk"]:
            if has(cand + ".shp"):
                mk_auto = cand
                break
    if mk_auto:
        msd = get(mk_auto + ".shp")
        mshp = Shp(msd)
        mn = mshp.nframes
        # 完整帧 = 最大不透明帧（与静态一致）；关键帧均采 [0, best] 区间
        best, besta = 0, -1
        for i in range(mn):
            frp = mshp.frame_pixels(i)
            a = sum(1 for v in frp.pixels if v != 0)
            if a > besta:
                best, besta = i, a
        keys = []
        for k in range(MK_KEYS - 1):
            idx = round(best * k / (MK_KEYS - 1))
            if idx not in keys:
                keys.append(idx)
        if best not in keys:
            keys.append(best)
        keys.sort()
        got = 0
        for p, idx in enumerate(keys):
            # 整幅 mk 画布 + 原帧偏移（与成品同坐标系），禁止 crop/居中
            big = Image.new("RGBA", (mshp.w, mshp.h), (0, 0, 0, 0))
            r = shp_frame_img(mshp, idx, PAL_U)
            if not r:
                continue
            fi, (fx, fy, fw, fh) = r
            big.paste(fi, (fx, fy), fi)
            if big.getbbox() is None:
                continue
            save(scale_bld_canvas(big), f"bld_{eng}_mk_f{p}.png")
            got += 1
        if got > 1:
            ANIM_META["blds"][eng] = {"mk": got}
    cameo = asec.get("Cameo", "").lower()
    # 建筑图标常去掉两位剧场前缀（GACNST -> cnsticon.shp）
    ic = None
    for cand in ([cameo + ".shp"] if cameo else []) + [img + "icon.shp", img[2:] + "icon.shp", rid.lower() + "icon.shp"]:
        if has(cand):
            ic = render_icon(cand[:-4], (108, 84))
            if ic:
                save(ic, f"icon_bld_{eng}.png")
                report["icons_ok"].append(("bld_" + eng, cand)); break
    if not ic:
        # cameo 缺失或损坏（中文包 comp==0）-> 用建筑本体合成
        ic2 = synth_icon_from_bld(found[:-4], (108, 84)) if found else None
        if ic2:
            save(ic2, f"icon_bld_{eng}.png")
            report["icons_ok"].append(("bld_" + eng, "synth:" + (found or "")))
        else:
            report["icons_skip"].append(("bld_" + eng, "no cameo"))

print(f"== done in {time.time()-t0:.1f}s ==")
print("units_ok:", len(report["units_ok"]), report["units_ok"])
print("units_skip:", len(report["units_skip"]), report["units_skip"])
print("blds_ok:", len(report["blds_ok"]), report["blds_ok"])
print("blds_skip:", len(report["blds_skip"]), report["blds_skip"])
print("icons_ok:", len(report["icons_ok"]))
print("icons_skip:", len(report["icons_skip"]), report["icons_skip"])

# ---- 动画元数据（引擎 sprites.cpp 启动读取：walk/fire/die 帧数与速率、建筑 mk 帧数） ----
with open(os.path.join(SPR, "anims.ini"), "w", encoding="utf-8") as f:
    f.write("; OpenRA2 animation metadata (auto-generated by gen_assets.py)\n")
    f.write("; unit: walk/fire/die=帧数 walkrate/firerate=帧间隔(tick) dep=有部署站姿\n")
    f.write("; bld:  mk=建造动画关键帧数\n")
    for eng, meta in ANIM_META["units"].items():
        f.write(f"[{eng}]\n")
        for k, v in meta.items():
            f.write(f"{k}={v}\n")
    for eng, meta in ANIM_META["blds"].items():
        f.write(f"[bld_{eng}]\n")
        for k, v in meta.items():
            f.write(f"{k}={v}\n")
print("anims.ini:", len(ANIM_META["units"]), "units,", len(ANIM_META["blds"]), "blds")
