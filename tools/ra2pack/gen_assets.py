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
# 引擎 MoveType（src/game/data.cpp）：空军/海军不烘地面投影，锚点=内容中心
AIR = {"intruder", "blackeagle", "kirov", "nighthawk", "hornet", "rocketeer",
       "mig", "siegechopper", "floatingdisc"}
NAVAL = {"destroyer", "typhoon", "aegis", "seascorpion", "dreadnought",
         "aircraftcarrier", "dolphin", "squid", "boomer"}
BLDS = {
    "conyard": ["GACNST"], "powerplant": ["GAPOWR"], "teslareactor": ["NAPOWR"],
    "nuclearreactor": ["NANRCT"], "barracks": ["GAPILE"], "warfactory": ["GAWEAP"],
    "orerefinery": ["GAREFN"], "radar": ["NARADR"], "battlelab": ["GATECH"],
    "airforcecmd": ["GAAIRC"], "navalyard": ["GAYARD"], "pillbox": ["GAPILL"],
    "sentrygun": ["NALASR"], "prismtower": ["ATESLA"], "teslacoil": ["TESLA"],
    "flakcannon": ["NAFLAK"], "grandcannon": ["GTGCAN"], "patriotmissile": ["NASAM"],
    "wall": ["GAWALL"], "orepurifier": ["GAOREP"], "industrialplant": ["NAINDP", "NAREFN"],
    "techpowerplant": ["CAPOWR", "GAPOWR"], "nukesilo": ["NAMISL"],
    "weatherdevice": ["GAWEAT"], "ironcurtain": ["NAIRON"], "chronosphere": ["GACSPH"],
    "oilderrick": ["CAOILD"], "hospital": ["CATHOSP"], "machineshop": ["CAMACH", "CAOUTP"],
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
def render_voxel_unit(img, canvas, eng=""):
    """8 方向渲染，返回 [PIL]*8 或 None。统一 scale。
    地面单位：地面接触点（最低体素层）对齐引擎投影线 y=0.72h（引擎在此烘阴影）；
    空军/海军：内容中心对齐锚点 (w/2, h/2+4)。"""
    vd = get(img + ".vxl")
    if not vd:
        return None
    hd = get(img + ".hva")
    v = Vxl(vd); h = Hva(hd) if hd else None
    w, ch = canvas
    floating = eng in AIR or eng in NAVAL
    anchor_y = ch / 2 + 4 if floating else ch * 0.72
    # 第一遍：8 方向投影。模型中心不在原点（旋转时扫出圆形轨迹），
    # 故 scale 用「单方向最大包围盒」而非并集，各方向按自身地面锚点对齐
    per = []
    maxbw = 0.0; maxbh = 0.0
    for e in range(8):
        # 引擎 dir e = 屏幕角 45e°（东起顺时针，y 向下）；经 zep 机头朝向验证
        pts, zmin = vxl_project(v, h, _phi_for_screen_alpha(45 * e))
        if not pts:
            return None
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        maxbw = max(maxbw, max(xs) - min(xs)); maxbh = max(maxbh, max(ys) - min(ys))
        per.append((pts, zmin, min(xs), max(xs), min(ys), max(ys)))
    bw = maxbw + 1.3; bh = maxbh + 1.3
    margin = 3
    scale = min((w - 2 * margin) / bw, (ch - 2 * margin) / bh)
    out = []
    for e in range(8):
        pts, zmin, mnx, mxx, mny, mxy = per[e]
        if floating:
            gx = (mnx + mxx) / 2; gy = (mny + mxy) / 2  # 空军/海军：内容中心
        else:
            # 地面接触点：最底层（sy 最大 1.2 范围内）体素的屏幕均值
            ycut = mxy - 1.2
            low = [p for p in pts if p[1] >= ycut]
            if low:
                gx = sum(p[0] for p in low) / len(low)
                gy = mxy
            else:
                gx = (mnx + mxx) / 2; gy = mxy
        orgx = w / 2 - gx * scale
        orgy = anchor_y - gy * scale + 0.5 * scale
        ss = 3 if scale >= 1.6 else 2
        out.append(render_pts(pts, PAL_U, scale, orgx, orgy, w, ch, supersample=ss))
    return out

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
    """内容等比缩放到画布内（仅缩小或<=1.6x放大），底边对齐 h-bottom_margin，水平居中"""
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
TURRETS = {
    "grizzly": "gtnktur", "rhino": "htnktur", "type99": "htnktur",
    "apocalypse": "mtnktur", "prismtank": "sreftur", "teslatank": "ttnktur",
    "ifv": "fvtur", "flaktrack": "htktur", "miragetank": "rtnktur",
    "robottank": "gtnktur", "lashertank": "ltnktur", "gatlingtank": "htktur",
    "magnetron": "ttnktur", "mastermind": "mtnktur",
}

def render_turret(tur, canvas):
    """tur.vxl 8 方向渲染（无 hva：炮塔以旋转轴心为原点，直接投影）"""
    vd = get(tur + ".vxl")
    if not vd:
        return None
    v = Vxl(vd)
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
    bw = maxbw + 1.3; bh = maxbh + 1.3
    margin = 3
    scale = min((w - 2 * margin) / bw, (ch - 2 * margin) / bh)
    out = []
    for e in range(8):
        pts, mnx, mxx, mny, mxy = per[e]
        gx = (mnx + mxx) / 2; gy = (mny + mxy) / 2
        orgx = w / 2 - gx * scale
        orgy = ch / 2 - gy * scale
        ss = 3 if scale >= 1.6 else 2
        out.append(render_pts(pts, PAL_U, scale, orgx, orgy, w, ch, supersample=ss))
    return out

# ------------------------------------------------------------- 建筑
def render_building(img, canvas, frame0=0, single=True):
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
    # 基帧 + 第一个小覆盖帧（旗帜/天线等，面积 < 基帧 40%；核弹井天线 38%）
    idxs = [frame0]
    f0 = shp_frame_img(shp, frame0, PAL_U)
    if not f0:
        return None
    a0 = f0[1][2] * f0[1][3]
    if not single:
        for i in range(frame0 + 1, n):
            fi = shp_frame_img(shp, i, PAL_U)
            if fi and fi[1][2] * fi[1][3] < a0 * 0.4:
                idxs.append(i)
                break
    content = composite_frames(shp, idxs, PAL_U)
    if not content:
        return None
    return place_bottom_center(content, canvas, 4)

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
    if has(img + ".vxl"):
        canvas = ph_size("unit", eng, (60, 60))
        dirs = render_voxel_unit(img, canvas, eng)
        if dirs:
            for e in range(8):
                save(dirs[e], f"unit_{eng}_d{e}_f0.png")
            if eng in MINERS:
                for e in range(8):
                    save(dirs[e], f"unit_{eng}_d{e}_f1.png")
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
    # 炮塔（tur.vxl 8 方向，近亲替代跟随 TURRETS 映射）
    if eng in TURRETS:
        tcanvas = ph_size("turret", eng, (48, 48))
        tdirs = render_turret(TURRETS[eng], tcanvas)
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
    # 文件探测：img.shp，_a 后缀，NewTheater 剧场字母变体（第2字符 -> a/t/s/g/n），rules id
    # 民用建筑（C 前缀）第2字符为剧场字母：A=arctic 雪地、T=温带；本游戏地图全温带，t 变体优先
    civ = img.startswith("c")
    order = "tasgn" if civ else "atsgn"
    tv = [img[0] + c + img[2:] + ".shp" for c in order] if len(img) >= 3 else []
    tries = (tv + [img + ".shp", img + "_a.shp"]) if civ \
        else ([img + ".shp", img + "_a.shp"] + tv)
    tries += [rid.lower() + ".shp", rid.lower() + "_a.shp"]
    found = next((x for x in tries if has(x)), None)
    # 完整建筑在 mk 建造动画（主 SHP 只是基坑/埋地状态），帧位置自动选
    mk_stem = BLD_FROM_MK.get(eng)
    use_mk = bool(mk_stem) and has(mk_stem + ".shp")
    if use_mk:
        found = mk_stem + ".shp"
    frame0 = -1 if use_mk else BLD_FRAME0.get(eng, 0)
    if found:
        canvas = ph_size("bld", eng, (120, 100))
        b = render_building(found[:-4], canvas, frame0=frame0, single=use_mk)
        if b:
            save(b, f"bld_{eng}.png")
            report["blds_ok"].append((eng, rid, found))
        else:
            report["blds_skip"].append((eng, f"{found} render fail"))
    else:
        report["blds_skip"].append((eng, f"{rid}/{img} shp missing"))
    # ---- 建造动画关键帧（mk 均采 MK_KEYS-1 帧 + 完整帧，与静态图同画布对齐） ----
    mk_auto = mk_stem if use_mk else None
    if not mk_auto:
        for cand in [img + "mk", rid.lower() + "mk"]:
            if has(cand + ".shp"):
                mk_auto = cand
                break
    if mk_auto:
        msd = get(mk_auto + ".shp")
        mshp = Shp(msd)
        mn = mshp.nframes
        canvas = ph_size("bld", eng, (120, 100))
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
        # 联合 bbox：各关键帧在 mk 画布原位合成后取并集，逐帧裁剪同一区域，避免播放时水平抖动
        ims = []
        ubox = None
        for idx in keys:
            big = Image.new("RGBA", (mshp.w, mshp.h), (0, 0, 0, 0))
            r = shp_frame_img(mshp, idx, PAL_U)
            if not r:
                ims.append(None)
                continue
            fi, (fx, fy, fw, fh) = r
            big.paste(fi, (fx, fy), fi)
            bb = big.getbbox()
            ims.append((big, bb))
            if bb:
                ubox = bb if ubox is None else (min(ubox[0], bb[0]), min(ubox[1], bb[1]),
                                                max(ubox[2], bb[2]), max(ubox[3], bb[3]))
        got = 0
        for p, it in enumerate(ims):
            if not it or not ubox:
                continue
            big, _ = it
            content = big.crop(ubox)
            im = place_bottom_center(content, canvas, 4)
            save(im, f"bld_{eng}_mk_f{p}.png")
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
