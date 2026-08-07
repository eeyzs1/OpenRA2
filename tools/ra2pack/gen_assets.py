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
# Prefer YR (rulesmd/artmd) when RA2MD.MIX is present; fall back to RA2.
_, _r = T.find("rulesmd.ini")
if not _r:
    _, _r = T.find("rules.ini")
_, _a = T.find("artmd.ini")
if not _a:
    _, _a = T.find("art.ini")
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

def has(n):
    return T.find_theater(n)[1] is not None
def get(n):
    return T.find_theater(n)[1]

# engine 单位/建筑 -> RA2/YR rules id 候选
# YR 建筑要求本机 game/ 有 RA2MD.MIX；无对应 SHP 时跳过（禁止近亲冒充）。
# Image 强制表：rules/art 写错或 Image 指向近亲时，用真实 MIX stem。
FORCE_IMAGE = {
    "guardiangi": "ggi",
    "initiate": "init",
    "brute": "brute",
    "virus": "virus",
    "boris": "boris",
    "robottank": "robo",
    "battlefortress": "bfrt",  # artmd 误标 SREF
    "gatlingtank": "ytnk",
    "magnetron": "tele",
    "mastermind": "mind",
    "chaosdrone": "caos",
    "mig": "bpln",
    "siegechopper": "schp",
    "floatingdisc": "disk",
    "boomer": "bsub",
    "slaveminer": "smin",
    # 官方 APOC.Image=MTNK（无独立 apoc.vxl）
    "apocalypse": "mtnk",
    # YR/渗透步兵：rules 无 Image 时强制 MIX stem
    "chronoivan": "civan",
    "slave": "slav",
    "yuriprime": "yurix",
}
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
    # ---- YR 真实素材（localmd/conqmd）----
    "guardiangi": ["GGI"],
    "initiate": ["INIT"],
    "brute": ["BRUTE"],
    "virus": ["VIRUS"],
    "boris": ["BORIS"],
    "robottank": ["ROBO"],
    "battlefortress": ["BFRT"],
    "gatlingtank": ["YTNK"],
    "magnetron": ["TELE"],
    "mastermind": ["MIND"],
    "chaosdrone": ["CAOS"],
    "mig": ["BPLN"],
    "siegechopper": ["SCHP"],
    "floatingdisc": ["DISK"],
    "boomer": ["BSUB"],
    "lashertank": ["LTNK"],
    "slaveminer": ["SMIN"],
    "chronoivan": ["CIVAN"],
    "slave": ["SLAV"],
    "yuriprime": ["YURIX", "YURIPR"],
    # ---- 融合阵营：无官方 MIX 时 skip（禁止近亲冒充）----
    "pla": ["PLA"],
    "type99": ["TYPE99"],
}
INFANTRY = {"gi", "conscript", "engineer", "spy", "flaktrooper",
            "teslatrooper", "sniper", "tanya", "desolator", "chrono", "crazyivan",
            "terrorist", "navyseal", "yuri", "chronocommando", "psicommando",
            "rocketeer", "guardiangi", "pla", "initiate", "brute", "virus", "boris",
            "chronoivan", "slave", "yuriprime"}
MINERS = {"harvester", "chronominer", "warminer", "slaveminer"}
# 卸货动画 VXL（HORV/CMON）：与车体 Image 不同
MINER_UNLOAD_VXL = {"harvester": "horv", "chronominer": "cmon", "warminer": "horv"}
# 引擎 MoveType（src/game/data.cpp）：空军/海军不烘地面投影，锚点=内容中心
AIR = {"intruder", "blackeagle", "kirov", "nighthawk", "hornet", "rocketeer",
       "mig", "siegechopper", "floatingdisc"}
# 气垫/悬浮地面单位：无履带触地点，用内容中心锚点（避免遥控坦克左偏裁切）
HOVER = {"robottank"}
NAVAL = {"destroyer", "typhoon", "aegis", "seascorpion", "dreadnought",
         "aircraftcarrier", "dolphin", "squid", "boomer"}
BLDS = {
    "conyard": ["GACNST"], "powerplant": ["GAPOWR"], "teslareactor": ["NAPOWR"],
    "nuclearreactor": ["NANRCT"], "barracks": ["GAPILE"], "warfactory": ["GAWEAP"],
    "orerefinery": ["GAREFN"], "radar": ["GARADR", "NARADR"], "battlelab": ["GATECH"],
    "airforcecmd": ["GAAIRC"], "navalyard": ["GAYARD"], "pillbox": ["GAPILL"],
    "sentrygun": ["NALASR"], "prismtower": ["GAPRIS"], "teslacoil": ["NATSLA"],
    "flakcannon": ["NAFLAK"], "grandcannon": ["GTGCAN"], "patriotmissile": ["NASAM"],
    "wall": ["GAWALL"], "orepurifier": ["GAOREP"], "industrialplant": ["NAINDP"],
    "techpowerplant": ["CAPOWR"], "nukesilo": ["NAMISL"],
    "weatherdevice": ["GAWEAT"],  # rules Image=GAWETH
    "ironcurtain": ["NAIRON"], "chronosphere": ["GACSPH"],
    "oilderrick": ["CAOILD"], "hospital": ["CAHOSP", "CATHOSP"], "machineshop": ["CAMACH"],
    "cloningvat": ["NACLON"], "servicedepot": ["GADEPT"], "gapgenerator": ["GAGAP"],
    "spysat": ["GASPST", "GASPYSAT"], "psychicsensor": ["NAPSIS"], "techairport": ["CAAIRP"],
    "secretlab": ["CASLAB", "CALAB"], "civhouse": ["CTHSE01", "CAHSE01"], "techoutpost": ["CAOUTP"],
    # ---- YR/尤里建筑：仅用真实 Image id；无 SHP 时跳过（禁止近亲冒充）----
    "battlebunker": ["NABNKR"],
    "tankbunker": ["NATBNK"],
    "bioreactor": ["YAPOWR"],
    "gatlingcannon": ["YAGGUN"],
    "grinder": ["YAGRND"],
    "geneticmutator": ["YAGNTC"],
    "psychicdominator": ["YAPPET"],
    "psychictower": ["YAPSYT"],
    "robotcontrol": ["GAROBO"],
    # ---- 阵营变体 / 民用（审核+地图用；引擎可按需映射）----
    "barracks_sov": ["NAHAND"],
    "barracks_yuri": ["YABRCK"],
    "warfactory_sov": ["NAWEAP"],
    "warfactory_yuri": ["YAWEAP"],
    "navalyard_sov": ["NAYARD"],
    "navalyard_yuri": ["YAYARD"],
    "wall_sov": ["NAWALL"],
    "civhouse2": ["CTHSE02", "CAHSE02"],
    "civhouse3": ["CTHSE03", "CAHSE03"],
    "civhouse4": ["CAHSE04"],
    "civhouse5": ["CAHSE05"],
    "civhouse6": ["CAHSE06"],
    "civhouse7": ["CAHSE07"],
    "civwash": ["CAWASH01", "CTWASH01"],
    "civbarn": ["CABARN02"],
    "civwatertower": ["CAWT01"],
    "civchurch": ["CACHIG01", "CACHURCH"],
    # ---- 阵营对称 / 科技补充（审核用；引擎可按需映射）----
    "conyard_sov": ["NACNST"],
    "battlelab_sov": ["NATECH"],
    "orerefinery_sov": ["NAREFN"],
    "servicedepot_sov": ["NADEPT"],
    "helipad_sov": ["NAHPAD"],
    "airforcecmd_usa": ["AMRADR"],
    "sandbags": ["GASAND"],
    "psychicbeacon": ["NAPSYA"],
    "psychicbeacon2": ["NAPSYB"],
    "techarmory": ["CAARMR"],
}
# 主 SHP 只是基坑/埋地状态，完整建筑在 mk 建造动画（帧位置自动选最大不透明帧）
# 弹出式炮塔 mk 末帧常是高仰角：哨戒/巨炮成品走底座 SHP + VXL，勿用 mk 峰值当 idle
BLD_FROM_MK = {"flakcannon": "naflakmk",
               "servicedepot": "gadeptmk",
               "battlebunker": "nabnkrmk",
               "tankbunker": "ngtbnkmk",
               "teslacoil": "nttslamk",
               # chronosphere：主图用静态；就绪态另存 SuperAnim（勿用 mk 峰值冒充实闲）
               "spysat": "gaspstmk"}
# 墙体：f0 是孤立门柱，f5 是连续墙段
BLD_FRAME0 = {"wall": 5, "wall_sov": 5, "sandbags": 5}
# 成品强制用指定 mk 帧
BLD_FORCE_FRAME = {}
# 建筑炮塔 VXL：name, yaw(0..7), ox, oy[, barrel_stem]
# 巨炮：底座 gagcan + gtgcantur + gtgcanbarl（rules TurretAnim）；勿按底座宽度压扁炮管
BLD_TURRET_VXL = {
    "sentrygun": ("laser", 0, 0, -6),
    # yaw=7：炮口右上，接近原作 idle / cameo 朝向
    "grandcannon": ("gtgcantur", 7, 3, 0, "gtgcanbarl"),
    # 前哨：rules TurretAnimX/Y≈-30,14；落座用柱顶 + 主体底（忽略 VXL 长颈）
    "techoutpost": ("outp", 1, 0, 2),
}
# 巨炮等：VXL 缩放与落座（oy 用 seat_frac 时忽略 tuple 的 oy）
BLD_TURRET_FIT = {
    "grandcannon": {"scale": 1.15, "seat_frac": 0.18, "expand": True},
    # OUTP 投影带长颈：anchor_bulk_frac 坐在碟身底；柱顶须近白检测
    "techoutpost": {
        "scale": 1.65,
        "expand": True,
        "auto_seat_pillars": True,
        "anchor_bulk_frac": 0.72,
    },
}
# 光棱：ActiveAnim=GAPRIS_B 是待机带电头（常在）；SpecialAnim=GAPRIS_A 才是开火
# 不要 skip ActiveAnim，否则只剩无头基座
BLD_SKIP_OVERLAY = set()
# ActiveAnim 指定帧（保留钩子）
# ActiveAnim=CAOUTP_F 是塔顶黄旗（非雷达碟）；取挥舞较实的帧
BLD_OVERLAY_FRAME = {
    "techoutpost": {"CAOUTP_F": 12, "CTOUTP_F": 12},
}
# SpecialAnim* 多数是开火/烟雾；少数是建筑本体缺块（须叠）
BLD_SPECIAL_IS_BODY = {"techoutpost"}
# 超级武器建筑：idle=未就绪；ready=SuperAnim* 叠层（原作充电完成才显示）
# 核弹井本包 namisl 仅垫层，闭合空井用 NAMISL_E 顶上
BLD_SW_IDLE_EXTRA = {
    "nukesilo": ["NAMISL_E"],
    # 超时空主 SHP 仅左半底座；GACSPH_E 是未充能也该有的环架/右半结构
    "chronosphere": ["GACSPH_E"],
}
BLD_SW_READY_LAYERS = {
    "nukesilo": ["NAMISL_E", "NAMISL_F", "NAMISL_G", "NAMISL_H"],
    "ironcurtain": ["NAIRON_A", "NAIRON_F", "NAIRON_G", "NAIRON_H"],
    "chronosphere": ["GACSPH_E", "GACSPH_F", "GACSPH_G", "GACSPH_H"],
    "weatherdevice": ["GAWETH_E", "GAWETH_F", "GAWETH_G", "GAWETH_H"],
}
# 建造/出售倒放关键帧数（mk 均采 + 完整帧；原作 MAKE 常 50+ 帧，过稀会像程序动画）
MK_KEYS = 20

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
    # 加载 HVA：多节模型（夜鹰/攻城直升机）靠 HVA 把旋翼抬到机身；idle=frame 0。
    v = Vxl(vd)
    hd = get(img + ".hva")
    h = Hva(hd) if hd else None
    if h is not None and not h.valid:
        h = None
    floating = eng in AIR or eng in NAVAL or eng in HOVER
    per = []
    for e in range(8):
        pts, zmin = vxl_project(v, h, _phi_for_screen_alpha(45 * e), hva_frame=0)
        if not pts:
            return None, None
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        per.append((pts, zmin, min(xs), max(xs), min(ys), max(ys)))
    return _rasterize_per(per, canvas, floating)


def _rasterize_per(per, canvas, floating, anchor_per=None):
    """per: [(pts,zmin,mnx,mxx,mny,mxy)*8]。anchor_per 若给则用其点做地面锚点（炮塔装配时用车体）。"""
    w, ch = canvas
    maxbw = 0.0
    maxbh = 0.0
    for pts, zmin, mnx, mxx, mny, mxy in per:
        maxbw = max(maxbw, mxx - mnx)
        maxbh = max(maxbh, mxy - mny)
    bw = maxbw + 1.3
    bh = maxbh + 1.3
    scale = ENGINE_TILE_W / float(RA2_TILE_W)
    margin = 4  # 略留边，避免飞碟/遥控坦克贴边裁切
    need_w = int(bw * scale) + 2 * margin
    if floating:
        need_h = int(bh * scale) + 2 * margin
    else:
        need_h = max(int(bh * scale / 0.72) + margin, int(bh * scale) + 2 * margin)
    w = max(w, need_w)
    ch = max(ch, need_h)
    anchor_y = ch / 2 + 4 if floating else ch * 0.72
    orgs = []
    for e in range(8):
        pts, zmin, mnx, mxx, mny, mxy = per[e]
        # 地面锚点优先用车体点，避免炮塔把触地点抬飞
        apts = pts
        if anchor_per is not None:
            apts = anchor_per[e][0]
            amnx = min(p[0] for p in apts)
            amxx = max(p[0] for p in apts)
            amny = min(p[1] for p in apts)
            amxy = max(p[1] for p in apts)
        else:
            amnx, amxx, amny, amxy = mnx, mxx, mny, mxy
        if floating:
            gx = (mnx + mxx) / 2
            gy = (mny + mxy) / 2
        else:
            ycut = amxy - 1.2
            low = [p for p in apts if p[1] >= ycut]
            if low:
                gx = sum(p[0] for p in low) / len(low)
                gy = amxy
            else:
                gx = (amnx + amxx) / 2
                gy = amxy
        orgx = w / 2 - gx * scale
        orgy = anchor_y - gy * scale + 0.5 * scale
        orgs.append((orgx, orgy))
    # 地面锚点相对包围盒偏心时，内容会顶穿画布边；扩边并平移原点
    pad_l = pad_r = pad_t = pad_b = 0.0
    for e in range(8):
        _, _, mnx, mxx, mny, mxy = per[e]
        orgx, orgy = orgs[e]
        left = orgx + mnx * scale
        right = orgx + mxx * scale
        top = orgy + mny * scale
        bottom = orgy + mxy * scale
        pad_l = max(pad_l, margin - left)
        pad_r = max(pad_r, right - (w - margin))
        pad_t = max(pad_t, margin - top)
        pad_b = max(pad_b, bottom - (ch - margin))
    pad_l = int(max(0, math.ceil(pad_l)))
    pad_r = int(max(0, math.ceil(pad_r)))
    pad_t = int(max(0, math.ceil(pad_t)))
    pad_b = int(max(0, math.ceil(pad_b)))
    if pad_l or pad_r or pad_t or pad_b:
        w += pad_l + pad_r
        ch += pad_t + pad_b
        orgs = [(ox + pad_l, oy + pad_t) for ox, oy in orgs]
    out = []
    for e in range(8):
        pts = per[e][0]
        orgx, orgy = orgs[e]
        out.append(render_pts(pts, PAL_U, scale, orgx, orgy, w, ch, supersample=2))
    layout = {"scale": scale, "w": w, "ch": ch, "floating": floating, "orgs": orgs}
    return out, layout


def _project_stem_dirs(stem: str):
    """返回 per=[(pts,zmin,mnx,mxx,mny,mxy)*8] 或 None。"""
    vd = get(stem + ".vxl")
    if not vd:
        return None
    v = Vxl(vd)
    hd = get(stem + ".hva")
    h = Hva(hd) if hd else None
    if h is not None and not h.valid:
        h = None
    per = []
    for e in range(8):
        pts, zmin = vxl_project(v, h, _phi_for_screen_alpha(45 * e), hva_frame=0)
        if not pts:
            return None
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        per.append((pts, zmin, min(xs), max(xs), min(ys), max(ys)))
    return per


def _merge_per(a, b):
    """合并两套 8 向投影的点集（同方向）。"""
    out = []
    for e in range(8):
        pts = list(a[e][0]) + list(b[e][0])
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        out.append((pts, min(a[e][1], b[e][1]), min(xs), max(xs), min(ys), max(ys)))
    return out


def _barl_stem(tur: str):
    """tur stem → barl stem；部分炮塔 VXL 已含完整炮管，叠 barl 会出双管假象。"""
    if tur in ("htktur",):  # 高射炮车：htktur 已是完整上扬高射炮
        return None
    if tur.endswith("tur"):
        return tur[:-3] + "barl"
    return tur + "barl"


def render_unit_with_turret(hull_stem, tur_stem, canvas, eng=""):
    """车体+炮塔(+炮管) 共用合并包围盒画布，避免飞碟/遥控坦克裁切。
    返回 (hull_imgs, tur_imgs, layout) 或 (None, None, None)。"""
    hull_per = _project_stem_dirs(hull_stem)
    if not hull_per:
        return None, None, None
    floating = eng in AIR or eng in NAVAL or eng in HOVER

    tur_per = None
    if tur_stem and has(tur_stem + ".vxl"):
        # 炮塔与炮管分开投影：各用自己的 HVA（避免合并后同名/异名抢矩阵）
        t_only = _project_stem_dirs(tur_stem)
        barl_name = _barl_stem(tur_stem)
        b_only = _project_stem_dirs(barl_name) if barl_name and has(barl_name + ".vxl") else None
        if t_only and b_only:
            tur_per = _merge_per(t_only, b_only)
        else:
            tur_per = t_only

    if tur_per:
        combined = _merge_per(hull_per, tur_per)
        # 先算合并 layout（画布尺寸+原点）
        _, layout = _rasterize_per(combined, canvas, floating, anchor_per=hull_per)
        # 用同一 layout 分别栅格化车体/炮塔
        scale = layout["scale"]
        w, ch = layout["w"], layout["ch"]
        orgs = layout["orgs"]
        hull_imgs, tur_imgs = [], []
        for e in range(8):
            orgx, orgy = orgs[e]
            hull_imgs.append(render_pts(hull_per[e][0], PAL_U, scale, orgx, orgy, w, ch, supersample=2))
            tur_imgs.append(render_pts(tur_per[e][0], PAL_U, scale, orgx, orgy, w, ch, supersample=2))
        return hull_imgs, tur_imgs, layout

    hull_imgs, layout = _rasterize_per(hull_per, canvas, floating)
    return hull_imgs, None, layout


def render_turret(tur, layout):
    """tur+barl 与车体同一画布/原点叠绘（layout 来自 render_voxel_unit / assembly）。"""
    if not layout:
        return None
    # 分投影：各自 HVA，避免 HTK tur(MDUMMY01) 与 barl(DUMMY01) 抢错矩阵
    t_per = _project_stem_dirs(tur)
    if not t_per:
        return None
    barl_name = _barl_stem(tur)
    b_per = _project_stem_dirs(barl_name) if barl_name and has(barl_name + ".vxl") else None
    per = _merge_per(t_per, b_per) if b_per else t_per
    scale = layout["scale"]
    w, ch = layout["w"], layout["ch"]
    orgs = layout["orgs"]
    out = []
    for e in range(8):
        orgx, orgy = orgs[e]
        out.append(render_pts(per[e][0], PAL_U, scale, orgx, orgy, w, ch, supersample=2))
    return out


def render_turret_centered(tur, canvas):
    """无车体 layout 时回退：内容居中（近亲表兜底）。"""
    t_per = _project_stem_dirs(tur)
    if not t_per:
        return None
    barl_name = _barl_stem(tur)
    b_per = _project_stem_dirs(barl_name) if barl_name and has(barl_name + ".vxl") else None
    per = _merge_per(t_per, b_per) if b_per else t_per
    w, ch = canvas
    maxbw = 0.0
    maxbh = 0.0
    for pts, zmin, mnx, mxx, mny, mxy in per:
        maxbw = max(maxbw, mxx - mnx)
        maxbh = max(maxbh, mxy - mny)
    scale = ENGINE_TILE_W / float(RA2_TILE_W)
    need = int(max(maxbw, maxbh) * scale) + 6
    w = max(w, need); ch = max(ch, need)
    out = []
    for e in range(8):
        pts, zmin, mnx, mxx, mny, mxy = per[e]
        gx = (mnx + mxx) / 2; gy = (mny + mxy) / 2
        orgx = w / 2 - gx * scale
        orgy = ch / 2 - gy * scale
        out.append(render_pts(pts, PAL_U, scale, orgx, orgy, w, ch, supersample=2))
    return out

# ------------------------------------------------------------- SHP 工具
def shp_frame_img(shp, i, pal, remap=True, civ_neutral=False, tech_neutral=False):
    fr = shp.frame_pixels(i)
    if fr.w == 0 or fr.h == 0:
        return None
    return shp_frame_to_rgba(fr, pal, remap=remap, civ_neutral=civ_neutral,
                             tech_neutral=tech_neutral), (fr.x, fr.y, fr.w, fr.h)

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
    "chronoivan": "CIvanSequence", "slave": "SlaveSequence", "yuriprime": "YuriXSequence",
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
DRONE_SHP = {"terrordrone"}  # CAOS 有真实 caos.vxl；仅 DRON 走 SHP
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
    "grizzly": "gtnktur", "rhino": "htnktur",
    "apocalypse": "mtnktur", "prismtank": "sreftur", "teslatank": "ttnktur",
    "ifv": "fvtur", "flaktrack": "htktur", "miragetank": "rtnktur",
    "robottank": "robotur", "lashertank": "ltnktur", "gatlingtank": "ytnktur",
    "magnetron": "teletur", "floatingdisc": "disktur", "slaveminer": "smintur",
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

# ------------------------------------------------------------- 建筑
def bib_shp_candidates(bibshape: str):
    """art.ini BibShape（如 GAREFNBB）→ 温带优先的 SHP 候选名。"""
    b = bibshape.lower().strip()
    if not b:
        return []
    if b.endswith(".shp"):
        b = b[:-4]
    out = []
    if len(b) >= 3:
        for c in "gtuas":
            out.append(b[0] + c + b[2:])
    out.append(b)
    seen = set()
    return [x for x in out if not (x in seen or seen.add(x))]


# art.ini 常缺 BibShape；原作按 Image+"BB"。本包多数电厂 bib 不在 MIX，resolve 会跳过。
BLD_DEFAULT_BIB = {
    "conyard": "GACNSTBB",
    "powerplant": "GAPOWRBB",
    "teslareactor": "NAPOWRBB",
    "nuclearreactor": "NANRCTBB",
    "bioreactor": "YAPOWRBB",
    "techpowerplant": "CAPOWRBB",
    "barracks": "GAPILEBB",
    "warfactory": "GAWEAPBB",
    "orerefinery": "GAREFNBB",
    "radar": "GARADRBB",
    "battlelab": "GATECHBB",
    "airforcecmd": "GAAIRCBB",
    "navalyard": "GAYARDBB",
    "servicedepot": "GADEPTBB",
}


def resolve_bib_shape(eng: str, asec: dict, rid: str, img: str):
    """Return BibShape string if any candidate SHP has real bytes; else None."""
    bib = asec.get("BibShape") or A.get(rid, {}).get("BibShape")
    if not bib:
        bib = BLD_DEFAULT_BIB.get(eng) or ((img.upper() + "BB") if img else None)
    if not bib:
        return None
    for stem in bib_shp_candidates(bib):
        data = get(stem + ".shp")
        if data:
            return bib
    return None

def shp_frame0_opaque(stem_shp: str) -> int:
    """frame0 不透明像素数；缺失或空帧返回 0。"""
    if not has(stem_shp):
        return 0
    try:
        shp = Shp(get(stem_shp))
        fr = shp.frame_pixels(0)
        if not fr:
            return 0
        return sum(1 for v in fr.pixels if v != 0)
    except Exception:
        return 0

def pick_bld_shp(tries):
    """按候选顺序选第一个「非空」SHP（跳过 generic 空壳 ngmisl 等）。"""
    for x in tries:
        if shp_frame0_opaque(x) > 32:
            return x
    return next((x for x in tries if has(x)), None)

def paste_shp_frame(big, stem, frame=0, remap=True, civ_neutral=False, tech_neutral=False):
    """把 stem.shp 的 frame 按原偏移叠到 big（必要时扩画布）。成功返回 big。"""
    if not has(stem + ".shp"):
        return big
    ash = Shp(get(stem + ".shp"))
    if frame < 0 or frame >= ash.nframes:
        return big
    fr = ash.frame_pixels(frame)
    if not fr or fr.w <= 0:
        return big
    r = shp_frame_img(ash, frame, PAL_U, remap=remap, civ_neutral=civ_neutral,
                      tech_neutral=tech_neutral)
    if not r:
        return big
    fi, (x, y, w, h) = r
    if x + fi.width > big.width or y + fi.height > big.height or x < 0 or y < 0:
        nw = max(big.width, x + fi.width, ash.w)
        nh = max(big.height, y + fi.height, ash.h)
        bigger = Image.new("RGBA", (nw, nh), (0, 0, 0, 0))
        bigger.paste(big, (0, 0), big)
        big = bigger
    big.paste(fi, (max(0, x), max(0, y)), fi)
    return big


def lower_grandcannon_barrel(img, deg=-52, pivot_y_frac=0.70, right_pad=110, blend=8):
    """gtgcanmk 峰值炮管仰角约 64°（建造结束姿），原作待机约 15–20°（略上扬）。
    只旋转枢轴以上炮管/炮塔顶，底座原样保留；deg≈-52 → 炮口仰角约 18°。"""
    import math
    bb = img.getbbox()
    if not bb:
        return img
    px = (bb[0] + bb[2]) / 2.0
    py = bb[1] + (bb[3] - bb[1]) * pivot_y_frac
    rad = math.radians(deg)
    cosr, sinr = math.cos(rad), math.sin(rad)
    W = img.width + right_pad
    H = img.height
    res = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sp = img.load()
    dp = res.load()

    def sample(sx, sy):
        x0 = int(math.floor(sx))
        y0 = int(math.floor(sy))
        x1, y1 = x0 + 1, y0 + 1
        if x0 < 0 or y0 < 0 or x1 >= img.width or y1 >= img.height:
            return (0, 0, 0, 0)
        fx, fy = sx - x0, sy - y0
        p00, p10 = sp[x0, y0], sp[x1, y0]
        p01, p11 = sp[x0, y1], sp[x1, y1]
        if p00[3] + p10[3] + p01[3] + p11[3] < 8:
            return (0, 0, 0, 0)

        def lerp(a, b, t):
            return tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(4))

        return lerp(lerp(p00, p10, fx), lerp(p01, p11, fx), fy)

    # 底座（腿+平台+炮塔下半）原样
    for y in range(max(0, int(py) - blend), img.height):
        for x in range(img.width):
            p = sp[x, y]
            if p[3] > 8:
                dp[x, y] = p

    # 枢轴以上：逆变换采样放倒炮管
    for y in range(0, int(py) + blend + 2):
        for x in range(W):
            rx = x - px
            ry = y - py
            sx = px + rx * cosr - ry * sinr
            sy = py + rx * sinr + ry * cosr
            if sy >= py + 1:
                continue
            p = sample(sx, sy)
            if p[3] <= 16:
                continue
            if y >= int(py) - blend and dp[x, y][3] > 8:
                t = (int(py) + blend - y) / max(1.0, 2.0 * blend)
                t = max(0.0, min(1.0, t))
                dst = dp[x, y]
                dp[x, y] = tuple(int(p[i] * t + dst[i] * (1 - t)) for i in range(4))
            else:
                dp[x, y] = p
    return res


def find_bright_pillar_seat(im, ox_hint=0, oy_hint=0):
    """前哨 remap 白柱顶中心。只认近白（避免浅灰屋顶/米色壳体误检偏高）。"""
    bb = im.getbbox()
    if not bb:
        return None
    px = im.load()
    x0, y0, x1, y1 = bb
    # 左上：排除右下黄黑警戒垫与橙色吊臂
    x1 = x0 + max(8, int((x1 - x0) * 0.55))
    y1 = y0 + max(8, int((y1 - y0) * 0.62))
    bright = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b, a = px[x, y]
            if a < 220:
                continue
            mx, mn = max(r, g, b), min(r, g, b)
            # tech_neutral 柱顶接近纯白；排除 180..210 的浅灰壳体
            if mn >= 225 and (mx - mn) <= 30:
                bright.append((x, y))
    if len(bright) < 24:
        return None
    top_y = min(y for _, y in bright)
    band = [p for p in bright if p[1] <= top_y + 5]
    if len(band) < 6:
        band = bright
    cx = sum(x for x, _ in band) / len(band) + ox_hint
    cy = top_y + oy_hint
    return cx, cy


def render_building(img, canvas=None, frame0=0, single=True, bib=None, active_anim=None,
                    overlay_anims=None, remap=True, turret_vxl=None, turret_yaw=0,
                    turret_ox=0, turret_oy=0, civ_neutral=False, overlay_frames=None,
                    post_process=None, turret_barrel=None, turret_fit=None,
                    tech_neutral=False):
    """保留 SHP 原画布与帧偏移（地基对齐关键），再按 64/60 放大到引擎瓦片。
    禁止 crop+居中：会剪掉地基留白，建成后地面缺角、比例错位。
    bib：art.ini BibShape；active_anim / overlay_anims：Idle/Active/Special 等补全缺块。
    Remapable=no 的科技建筑必须 remap=False，否则 16..31 被画成亮红。
    civ_neutral：民房 16..31 收成砖灰；tech_neutral：中立科技→浅灰白。
    overlay_frames：{anim名大写: frameIdx}；炮塔 VXL 在 SHP 原像素坐标叠（scale=1）。
    post_process：scale 前对原像素画布的可选处理。
    turret_barrel：分体炮管 VXL stem；turret_fit：{scale, seat_frac, expand}。"""
    sd = get(img + ".shp")
    if not sd:
        return None
    shp = Shp(sd)
    n = shp.nframes
    if frame0 < 0:
        # mk 建造动画：完整建筑帧位置不一，取最大不透明像素帧
        best, besta = 0, -1
        for i in range(n):
            fr = shp.frame_pixels(i)
            a = sum(1 for v in fr.pixels if v != 0)
            if a > besta:
                best, besta = i, a
        frame0 = best
    idxs = [frame0]
    f0 = shp_frame_img(shp, frame0, PAL_U, remap=remap, civ_neutral=civ_neutral,
                       tech_neutral=tech_neutral)
    if not f0:
        return None
    a0 = f0[1][2] * f0[1][3]
    if not single:
        for i in range(frame0 + 1, n):
            frp = shp.frame_pixels(i)
            if not frp:
                continue
            # 跳过损坏/占位帧：unittem 204..239 是品红废色，叠上去会花屏
            bad = sum(1 for v in frp.pixels if 204 <= v <= 239)
            if bad > 24:
                continue
            fi = shp_frame_img(shp, i, PAL_U, remap=remap, civ_neutral=civ_neutral,
                              tech_neutral=tech_neutral)
            if not fi:
                continue
            area = fi[1][2] * fi[1][3]
            if area < a0 * 0.45:
                idxs.append(i)
                if len(idxs) >= 4:
                    break
    big = Image.new("RGBA", (shp.w, shp.h), (0, 0, 0, 0))
    if bib:
        for stem in bib_shp_candidates(bib):
            data = get(stem + ".shp")
            if not data:
                continue
            big = paste_shp_frame(big, stem, 0, remap=False, civ_neutral=False)
            break
    for i in idxs:
        r = shp_frame_img(shp, i, PAL_U, remap=remap, civ_neutral=civ_neutral,
                          tech_neutral=tech_neutral)
        if r:
            fi, (x, y, w, h) = r
            if x + fi.width > big.width or y + fi.height > big.height or x < 0 or y < 0:
                nw = max(big.width, x + fi.width)
                nh = max(big.height, y + fi.height)
                bigger = Image.new("RGBA", (nw, nh), (0, 0, 0, 0))
                bigger.paste(big, (0, 0), big)
                big = bigger
            big.paste(fi, (max(0, x), max(0, y)), fi)
    # 兼容旧参数 + 多层 Idle/Active/Special
    anims = []
    if active_anim:
        anims.append(active_anim)
    if overlay_anims:
        anims.extend(overlay_anims)
    seen = set()
    oframes = overlay_frames or {}
    for anim in anims:
        if not anim:
            continue
        key = anim.lower()
        if key in seen:
            continue
        seen.add(key)
        fr_i = oframes.get(anim.upper(), oframes.get(anim, 0))
        for stem in bib_shp_candidates(anim):
            if not has(stem + ".shp"):
                continue
            # 跳过画布远大于当前 big 的层（本包 ggcsph_h 误成 432x304，会撑破合成）
            try:
                ash = Shp(get(stem + ".shp"))
                if ash.w > big.width * 1.6 or ash.h > big.height * 1.6:
                    continue
            except Exception:
                pass
            fr_use = oframes.get(stem.upper(), fr_i)
            big = paste_shp_frame(big, stem, fr_use, remap=remap, civ_neutral=civ_neutral,
                                 tech_neutral=tech_neutral)
            break
    # 建筑炮塔 VXL：在 SHP 原像素坐标叠（勿乘 BLD_SCALE，后面统一放大）
    if turret_vxl and has(turret_vxl + ".vxl"):
        try:
            fit = turret_fit or {}
            scale = float(fit.get("scale", 1.0))
            seat_frac = fit.get("seat_frac", None)
            expand = bool(fit.get("expand", False))
            phi = _phi_for_screen_alpha(45 * (turret_yaw % 8))
            vtur = Vxl(get(turret_vxl + ".vxl"))
            # 静态烘培：不用 HVA（与单位车体一致，避免节变换把炮管拧飞）
            pts_tur, _z = vxl_project(vtur, None, phi)
            pts = list(pts_tur) if pts_tur else []
            if turret_barrel and has(turret_barrel + ".vxl"):
                pts_bar, _ = vxl_project(Vxl(get(turret_barrel + ".vxl")), None, phi)
                if pts_bar:
                    pts.extend(pts_bar)
            if pts:
                xs = [p[0] for p in pts]
                ys = [p[1] for p in pts]
                span_x = max(xs) - min(xs)
                span_y = max(ys) - min(ys)
                if span_x < 500 and span_y < 500:
                    bb = big.getbbox()
                    if bb:
                        # 默认：短炮塔可按底座略缩；巨炮 expand=True 时禁止压扁
                        if not expand:
                            base_w = max(8, bb[2] - bb[0])
                            if span_x > 1 and span_x * scale > base_w * 0.95:
                                scale = (base_w * 0.85) / span_x
                        seat = None
                        if fit.get("auto_seat_pillars"):
                            seat = find_bright_pillar_seat(big, turret_ox, turret_oy)
                        if seat:
                            cx, cy = seat
                        else:
                            cx = (bb[0] + bb[2]) / 2 + turret_ox
                            if seat_frac is not None:
                                cy = bb[1] + (bb[3] - bb[1]) * float(seat_frac) + turret_oy
                            else:
                                cy = bb[1] + (bb[3] - bb[1]) * 0.32 + turret_oy
                    else:
                        cx = big.width / 2 + turret_ox
                        cy = big.height / 2 + turret_oy
                    # 锚点用炮塔本体（不含超长炮管），避免炮管把中心拽偏
                    if pts_tur:
                        txs = [p[0] for p in pts_tur]
                        tys = [p[1] for p in pts_tur]
                        gx = (min(txs) + max(txs)) / 2
                        bulk = fit.get("anchor_bulk_frac")
                        if bulk is not None:
                            # 0..1 from turret top→bottom in screen Y；忽略底部细颈
                            lo, hi = min(tys), max(tys)
                            gy = lo + (hi - lo) * float(bulk)
                        elif fit.get("anchor_bottom"):
                            gy = max(tys)  # 炮塔底落在 seat（白柱顶）
                        else:
                            gy = min(tys) + (max(tys) - min(tys)) * 0.65
                    else:
                        gx = (min(xs) + max(xs)) / 2
                        gy = (min(ys) + max(ys)) / 2
                    orgx = cx - gx * scale
                    orgy = cy - gy * scale
                    # 画布不够装长炮管时向右/上扩
                    need_w = int(max(big.width, orgx + max(xs) * scale + 8, -orgx + 8))
                    need_h = int(max(big.height, orgy + max(ys) * scale + 8, -orgy + 8))
                    # also account for min extents going negative relative to org
                    left = orgx + min(xs) * scale
                    top = orgy + min(ys) * scale
                    pad_l = max(0, int(math.ceil(-left + 4)))
                    pad_t = max(0, int(math.ceil(-top + 4)))
                    pad_r = max(0, int(math.ceil(orgx + max(xs) * scale + 4 - big.width)))
                    pad_b = max(0, int(math.ceil(orgy + max(ys) * scale + 4 - big.height)))
                    if pad_l or pad_t or pad_r or pad_b:
                        bigger = Image.new("RGBA", (big.width + pad_l + pad_r,
                                                    big.height + pad_t + pad_b), (0, 0, 0, 0))
                        bigger.paste(big, (pad_l, pad_t), big)
                        big = bigger
                        orgx += pad_l
                        orgy += pad_t
                    tur = render_pts(pts, PAL_U, scale, orgx, orgy, big.width, big.height,
                                     supersample=2, tech_neutral=tech_neutral)
                    if tur.getbbox():
                        big = big.copy()
                        big.alpha_composite(tur)
        except Exception as ex:
            print("turret vxl fail", turret_vxl, ex)
    if big.getbbox() is None:
        return None
    if post_process:
        big = post_process(big)
        if big is None or big.getbbox() is None:
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

def main():
    for eng, cands in UNITS.items():
        if ONLY and ("unit_" + eng) not in ONLY and eng not in ONLY:
            continue
        rid = next((c for c in cands if c in R), None)
        if not rid:
            report["units_skip"].append((eng, "no rules id")); continue
        img = FORCE_IMAGE.get(eng, R[rid].get("Image", rid).lower())
        ok = False
        layout = None
        tdirs = None
        if has(img + ".vxl"):
            # 采矿车加大画布，避免货舱被裁切；地面车辆随内容尺寸，勿强行 128（1:1 体素会显得过小）
            default = (140, 140) if eng in MINERS else (72, 72)
            canvas = ph_size("unit", eng, default)
            # 强制采矿车升到至少 120；其它地面载具至少 64（旧超大占位会偏空）
            if eng in MINERS:
                canvas = (max(canvas[0], 140), max(canvas[1], 140))
            elif eng not in AIR and eng not in NAVAL and eng not in INFANTRY:
                canvas = (max(min(canvas[0], 96), 64), max(min(canvas[1], 96), 64))
            # 有炮塔时用车体+炮塔合并包围盒，避免飞碟/遥控坦克裁切
            tur = img + "tur"
            if not has(tur + ".vxl") and eng in TURRETS:
                tur = TURRETS[eng]
            if has(tur + ".vxl"):
                dirs, tdirs, layout = render_unit_with_turret(img, tur, canvas, eng)
            else:
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
            # 尤里首脑 SHP 立姿约 28x54，需更高画布以免 LANCZOS 压扁
            if eng == "yuriprime":
                default_inf = (40, 64)
            elif eng in INFANTRY or eng == "attackdog":
                default_inf = (24, 30)
            else:
                default_inf = (60, 60)
            canvas = ph_size("unit", eng, default_inf)
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
        # 炮塔：优先用装配路径已算好的 tdirs；否则 layout 叠绘 / 居中兜底
        if ok:
            if tdirs:
                for e in range(8):
                    save(tdirs[e], f"turret_{eng}_d{e}.png")
            else:
                tur = img + "tur"
                if not has(tur + ".vxl") and eng in TURRETS:
                    tur = TURRETS[eng]
                if has(tur + ".vxl"):
                    tdirs2 = render_turret(tur, layout) if layout else None
                    if not tdirs2:
                        tdirs2 = render_turret_centered(tur, (48, 48))
                    if tdirs2:
                        for e in range(8):
                            save(tdirs2[e], f"turret_{eng}_d{e}.png")
        # 禁止：车体失败时用近亲炮塔兜底
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
            report["icons_skip"].append(("unit_" + eng, "no cameo (synth disabled)"))

    for eng, cands in BLDS.items():
        if ONLY and ("bld_" + eng) not in ONLY and eng not in ONLY:
            continue
        rid = next((c for c in cands if c in R), None) or cands[0]
        img = R.get(rid, {}).get("Image", rid).lower()
        # art 段：优先 Image 名，再 rid
        asec = A.get(img.upper(), {}) or A.get(rid, {})
        # 温带地图优先：generic(G) 是本安装里真正的温带静态建筑；T 多为 mk；A 是雪地勿抢先
        civ = img.startswith("c")
        order = "gtuas"  # G 温带通用 → T → U → A雪 → S
        tv = [img[0] + c + img[2:] + ".shp" for c in order] if len(img) >= 3 else []
        tries = (tv + [img + ".shp", img + "_a.shp"]) if civ \
            else ([img[0] + "g" + img[2:] + ".shp", img[0] + "t" + img[2:] + ".shp",
                   img + ".shp", img + "_a.shp"] + tv)
        tries += [rid.lower() + ".shp", rid.lower() + "_a.shp"]
        seen = set(); tries = [x for x in tries if not (x in seen or seen.add(x))]
        found = pick_bld_shp(tries)
        # 强制帧 / mk 成品
        force = BLD_FORCE_FRAME.get(eng)
        mk_stem = BLD_FROM_MK.get(eng)
        use_mk = bool(mk_stem) and has(mk_stem + ".shp") and not force
        if force:
            fstem, fidx = force
            if has(fstem + ".shp"):
                found = fstem + ".shp"
                frame0 = fidx
                use_mk = True  # 单帧，不叠小覆盖
            else:
                frame0 = BLD_FRAME0.get(eng, 0)
        elif use_mk:
            found = mk_stem + ".shp"
            frame0 = -1
        else:
            frame0 = BLD_FRAME0.get(eng, 0)
        bib = resolve_bib_shape(eng, asec, rid, img)
        # Idle/Active* = 常态部件（雷达碟、船厂吊臂、光棱头）。Special* 默认是开火/烟雾，勿叠。
        overlay_keys = [
            "IdleAnim", "ActiveAnim", "ActiveAnimTwo", "ActiveAnimThree",
        ]
        if eng in BLD_SPECIAL_IS_BODY:
            overlay_keys += ["SpecialAnim", "SpecialAnimTwo", "SpecialAnimThree"]
        overlays = []
        if eng not in BLD_SKIP_OVERLAY:
            for k in overlay_keys:
                v = asec.get(k) or A.get(rid, {}).get(k)
                if v:
                    overlays.append(v)
        # Remapable 缺省 yes；科技/民用常 no —— 误 remap 会把屋顶/标识打成亮红
        remap_s = (asec.get("Remapable") or A.get(rid, {}).get("Remapable") or "yes").lower()
        do_remap = remap_s not in ("no", "false", "0")
        # 前哨：静态预览用浅灰白（贴近未占领）；运行时 sprites 对中立已灰 remap，
        # 但白像素无法再换阵营色——占领变色靠 skip 白、保留红占位？此处优先修「残头」观感。
        tech_neutral = eng == "techoutpost"
        if tech_neutral:
            do_remap = False
        # 民房：强制 civ_neutral（16..31→砖灰），避免 unittem 阵营色阶花屏
        civ_neutral = eng.startswith("civhouse") or eng in ("civwash", "civbarn", "civwatertower", "civchurch")
        if civ_neutral:
            do_remap = False
            tech_neutral = False
        tur_info = BLD_TURRET_VXL.get(eng)
        if tur_info:
            tur_name, tur_yaw = tur_info[0], tur_info[1]
            tur_ox = tur_info[2] if len(tur_info) > 2 else 0
            tur_oy = tur_info[3] if len(tur_info) > 3 else 0
            tur_barrel = tur_info[4] if len(tur_info) > 4 else None
        else:
            tur_name, tur_yaw, tur_ox, tur_oy, tur_barrel = None, 0, 0, 0, None
        tur_fit = BLD_TURRET_FIT.get(eng)
        oframes = BLD_OVERLAY_FRAME.get(eng, {})
        # 超级武器：主图=未就绪；另存 bld_*_ready.png = SuperAnim 就绪态
        sw_idle_extra = list(BLD_SW_IDLE_EXTRA.get(eng, []))
        sw_ready = list(BLD_SW_READY_LAYERS.get(eng, []))
        if eng == "nukesilo":
            # 本包 namisl 仅垫层；闭合空井 = NAMISL_E；就绪有弹 = E+F+G+H
            idle_layers = sw_idle_extra
            empty = render_building(found[:-4], frame0=0, single=True, bib=None,
                                    overlay_anims=idle_layers, remap=True) if found else None
            if not empty and has("namisl_e.shp"):
                empty = render_building("namisl_e", frame0=0, single=True, remap=True)
            if empty:
                save(empty, "bld_nukesilo.png")
                report["blds_ok"].append((eng, rid, "idle_closed:" + ",".join(idle_layers or ["namisl_e"])))
            if found and sw_ready:
                ready = render_building(found[:-4], frame0=0, single=True, bib=None,
                                        overlay_anims=sw_ready, remap=True)
                if ready:
                    save(ready, "bld_nukesilo_ready.png")
                    report["blds_ok"].append(("nukesilo_ready", "NAMISL", "super:" + ",".join(sw_ready[:4])))
        elif found:
            idle_overlays = list(overlays) + sw_idle_extra
            b = render_building(found[:-4], frame0=frame0, single=use_mk or bool(force), bib=bib,
                                active_anim=None, overlay_anims=idle_overlays, remap=do_remap,
                                turret_vxl=tur_name, turret_yaw=tur_yaw,
                                turret_ox=tur_ox, turret_oy=tur_oy,
                                civ_neutral=civ_neutral, overlay_frames=oframes,
                                turret_barrel=tur_barrel, turret_fit=tur_fit,
                                tech_neutral=tech_neutral)
            if b:
                tag = found
                if bib: tag += f"+bib:{bib}"
                if idle_overlays: tag += "+anims:" + ",".join(idle_overlays[:4])
                if tur_name: tag += f"+vxl:{tur_name}"
                if tur_barrel: tag += f"+barl:{tur_barrel}"
                if oframes: tag += "+oframe"
                if civ_neutral: tag += "+civ"
                elif tech_neutral: tag += "+tech"
                elif not do_remap: tag += "+noremap"
                save(b, f"bld_{eng}.png")
                report["blds_ok"].append((eng, rid, tag))
            else:
                report["blds_skip"].append((eng, f"{found} render fail"))
            if sw_ready:
                ready = render_building(found[:-4], frame0=0, single=True, bib=bib,
                                        overlay_anims=list(overlays) + sw_ready, remap=do_remap,
                                        civ_neutral=civ_neutral)
                if ready:
                    save(ready, f"bld_{eng}_ready.png")
                    report["blds_ok"].append((eng + "_ready", rid, "super:" + ",".join(sw_ready[:4])))
        else:
            report["blds_skip"].append((eng, f"{rid}/{img} shp missing"))
        # ---- 建造动画关键帧：优先温带 mk（g* → t*mk） ----
        mk_auto = mk_stem if (use_mk and mk_stem) else None
        if not mk_auto and force:
            mk_auto = force[0] if has(force[0] + ".shp") else None
        if eng == "nukesilo" and has("namislmk.shp"):
            mk_auto = "namislmk"
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
            # 过滤：不透明度必须大致递增；在峰值处截断（RA2 MK 后半常有火花/回落帧）
            areas = []
            for idx in keys:
                frp = mshp.frame_pixels(idx)
                areas.append(sum(1 for v in frp.pixels if v != 0))
            mono = [keys[0]]
            mono_a = [areas[0]]
            last_a = areas[0]
            for idx, a in zip(keys[1:], areas[1:]):
                if a + 8 >= last_a:  # 允许轻微抖动
                    mono.append(idx)
                    mono_a.append(a)
                    last_a = max(last_a, a)
                # 显著回落：停止（后面多半是火花/损坏循环）
                elif a + max(32, last_a // 8) < last_a:
                    break
            # 截到峰值（含）：避免“先成型再塌一截”
            peak_i = max(range(len(mono_a)), key=lambda i: mono_a[i])
            mono = mono[: peak_i + 1]
            mono_a = mono_a[: peak_i + 1]
            # best 是全局最大不透明帧，应落在峰值处；若采样网格漏掉则补上
            if best not in mono:
                ba = sum(1 for v in mshp.frame_pixels(best).pixels if v != 0)
                if not mono_a or ba >= mono_a[-1]:
                    mono.append(best)
            keys = mono
            # 删掉旧 mk 帧，避免帧数减少时残留高序号 PNG
            import glob as _glob
            for old in _glob.glob(os.path.join(SPR, f"bld_{eng}_mk_f*.png")):
                try:
                    os.remove(old)
                except OSError:
                    pass
            got = 0
            saved_paths = []
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
                out_name = f"bld_{eng}_mk_f{p}.png"
                save(scale_bld_canvas(big), out_name)
                saved_paths.append(os.path.join(SPR, out_name))
                got += 1
            # 最终以落盘 PNG 不透明度为准截到峰值（SHP 索引面积与 RGBA 不完全一致）
            if got > 1:
                opac = []
                for path in saved_paths:
                    im = Image.open(path).convert("RGBA")
                    px = im.load()
                    n = sum(1 for y in range(im.size[1]) for x in range(im.size[0]) if px[x, y][3] > 60)
                    opac.append(n)
                peak_i = max(range(len(opac)), key=lambda i: opac[i])
                # 丢掉峰值之后的回落帧并重编号
                keep = saved_paths[: peak_i + 1]
                for path in saved_paths[peak_i + 1 :]:
                    try:
                        os.remove(path)
                    except OSError:
                        pass
                for i, path in enumerate(keep):
                    dest = os.path.join(SPR, f"bld_{eng}_mk_f{i}.png")
                    if path != dest:
                        try:
                            if os.path.exists(dest):
                                os.remove(dest)
                            os.replace(path, dest)
                        except OSError:
                            pass
                got = len(keep)
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
            report["icons_skip"].append(("bld_" + eng, "no cameo (synth disabled)"))

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


if __name__ == "__main__":
    main()
