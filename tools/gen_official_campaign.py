# -*- coding: utf-8 -*-
"""Generate OpenRA2 official + fusion campaign shell fields and hand maps."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAMP = ROOT / "assets" / "campaigns"
MAPS = ROOT / "maps" / "official"
MAPS.mkdir(parents=True, exist_ok=True)
(CAMP / "official").mkdir(parents=True, exist_ok=True)

EARLY_ALLIED_BLD = "ConYard,PowerPlant,Barracks,OreRefinery,Pillbox,Wall,WarFactory,NavalYard"
EARLY_ALLIED_UNIT = "MCV,Harvester,ChronoMiner,GI,Engineer,AttackDog,Grizzly,Destroyer,AmphTransport,Tanya,NavySEAL,Spy"
MID_ALLIED_BLD = EARLY_ALLIED_BLD + ",Radar,AirForceCmd,PrismTower,PatriotMissile,BattleLab,OrePurifier,ServiceDepot"
MID_ALLIED_UNIT = EARLY_ALLIED_UNIT + ",GuardianGI,IFV,PrismTank,Rocketeer,Intruder,Nighthawk,Aegis,AircraftCarrier,Sniper,Chrono"
LATE_ALLIED_BLD = MID_ALLIED_BLD + ",ChronoSphere,GapGenerator,SpySat,RobotControl"
LATE_ALLIED_UNIT = MID_ALLIED_UNIT + ",MirageTank,BattleFortress,RobotTank,Dolphin,ChronoCommando"

EARLY_SOV_BLD = "ConYard,TeslaReactor,Barracks,OreRefinery,SentryGun,Wall,WarFactory,NavalYard"
EARLY_SOV_UNIT = "MCV,Harvester,WarMiner,Conscript,Engineer,AttackDog,Rhino,FlakTrooper,Typhoon,AmphTransport,SeaScorpion"
MID_SOV_BLD = EARLY_SOV_BLD + ",Radar,TeslaCoil,FlakCannon,BattleLab,BattleBunker,TankBunker,IndustrialPlant"
MID_SOV_UNIT = EARLY_SOV_UNIT + ",TeslaTrooper,FlakTrack,V3Launcher,TerrorDrone,Desolator,CrazyIvan,Dreadnought,Squid,Yuri,MiG"
LATE_SOV_BLD = MID_SOV_BLD + ",NukeSilo,IronCurtain,NuclearReactor,PsychicSensor"
LATE_SOV_UNIT = MID_SOV_UNIT + ",Apocalypse,TeslaTank,Kirov,Boris,SiegeChopper,DemoTruck,Terrorist"

def map_txt(name, size, extras):
    lines = [
        f"# Official campaign map: {name}",
        f"size {size} {size}",
        "fill clear",
        f"rect rough {size//4} {size//4} {size//3} {size//5}",
        f"blob ore {size//5} {size//2} 4",
        f"blob gems {size*2//3} {size//3} 3",
        f"blob water {size//2} {size*3//4} 4",
        f"deco tree1 8 8 {size//4} {size//4} 10",
        f"deco rock1 {size//2} 10 12 10 4",
    ]
    lines.extend(extras)
    path = MAPS / f"{name}.txt"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return f"maps/official/{name}.txt"

def write_mission(fname, general, objectives, trigs, waves=None):
    out = ["; Official OpenRA2 mission", "[General]"]
    for k, v in general.items():
        if v is None or v == "":
            continue
        if isinstance(v, bool):
            if v:
                out.append(f"{k}=yes")
        else:
            out.append(f"{k}={v}")
    for i, o in enumerate(objectives, 1):
        out.append(f"\n[Objective.{i}]")
        out.append(f"Text={o[0]}")
        out.append(f"TextEn={o[1]}")
        out.append(f"Primary={'yes' if o[2] else 'no'}")
    if waves:
        for i, (at, units) in enumerate(waves, 1):
            out.append(f"\n[Wave.{i}]")
            out.append(f"At={at}")
            out.append(f"Units={units}")
    for i, t in enumerate(trigs, 1):
        out.append(f"\n[Trig.{i}]")
        for k, v in t.items():
            out.append(f"{k}={v}")
    path = CAMP / "official" / fname
    path.write_text("\n".join(out) + "\n", encoding="utf-8")
    return f"official/{fname}"

# -------- Allied 12 --------
OA = [
    ("oa01", "孤独的守护者", "Lone Guardian",
     "纽约：摧毁苏军无畏舰舰队，与布拉德利堡会合，捣毁苏军补给基地。谭雅必须存活。",
     "New York: Destroy the Dreadnought fleet, link with Fort Bradley, and raze the Soviet supply base. Tanya must survive.",
     "America", 0, True,
     [("摧毁无畏舰与补给基地", "Destroy Dreadnoughts and the supply base", True),
      ("谭雅必须存活", "Tanya must survive", True)],
     "hero"),
    ("oa02", "雄鹰破晓", "Eagle Dawn",
     "科罗拉多泉：夺回空军学院，歼灭苏军。谭雅必须存活。",
     "Colorado Springs: Retake the Air Force Academy and destroy the Soviets. Tanya must survive.",
     "America", 1, True, [("占领并肃清空军学院", "Secure the Air Force Academy", True), ("谭雅存活", "Keep Tanya alive", True)], "hero"),
    ("oa03", "向首领致敬", "Hail to the Chief",
     "华盛顿特区：摧毁心灵信标以解放区域。",
     "Washington D.C.: Destroy the Psychic Beacon to liberate the area.",
     "America", 2, False, [("摧毁心灵信标", "Destroy the Psychic Beacon", True)], "beacon"),
    ("oa04", "最后机会", "Last Chance",
     "芝加哥：抢滩建立基地，在心灵放大器上线前摧毁它。",
     "Chicago: Establish a beachhead and destroy the Psychic Amplifier before it comes online.",
     "America", 3, False, [("摧毁心灵放大器", "Destroy the Psychic Amplifier", True)], "amp"),
    ("oa05", "暗夜", "Dark Night",
     "德波边境：间谍渗透苏军 Battle Lab，摧毁两座核弹井。谭雅存活。",
     "German-Polish border: Infiltrate the Battle Lab and destroy both Nuke Silos. Tanya must survive.",
     "Germany", 4, True, [("摧毁两座核弹井", "Destroy both Nuke Silos", True), ("谭雅存活", "Keep Tanya alive", True)], "nuke2"),
    ("oa06", "自由", "Liberty",
     "华盛顿特区：增援五角大楼盟军，歼灭华盛顿苏军。",
     "Washington D.C.: Reinforce the Pentagon Allies and eliminate Soviet forces.",
     "America", 5, False, [("歼灭华盛顿苏军", "Eliminate Soviets in Washington", True)], "base"),
    ("oa07", "深海", "Deep Sea",
     "珍珠港：歼灭夏威夷周边苏军舰队与岸防。",
     "Pearl Harbor: Destroy Soviet naval forces and coastal defenses around Hawaii.",
     "America", 6, False, [("歼灭苏军海军", "Destroy the Soviet navy", True)], "naval"),
    ("oa08", "自由之门", "Free Gateway",
     "圣路易斯：限时摧毁心灵信标，肃清城内苏军。",
     "St. Louis: Destroy the Psychic Beacon under time pressure and clear the city.",
     "America", 7, False, [("限时摧毁心灵信标", "Destroy the Beacon in time", True)], "beacon_timed"),
    ("oa09", "太阳神庙", "Sun Temple",
     "图卢姆：阻止苏军复制棱镜科技，占领或摧毁相关设施。",
     "Tulum: Stop Soviet attempts to copy Prism tech — capture or destroy the facilities.",
     "America", 8, False, [("摧毁/占领棱镜研究设施", "Destroy or capture Prism research", True)], "base"),
    ("oa10", "幻影", "Mirage",
     "黑森林：保护爱因斯坦实验室，歼灭苏军。",
     "Black Forest: Protect Einstein's lab and eliminate the Soviets.",
     "Germany", 9, False, [("保护爱因斯坦实验室", "Protect Einstein's laboratory", True), ("歼灭苏军", "Eliminate Soviets", True)], "protect_lab"),
    ("oa11", "辐射尘", "Fallout",
     "佛罗里达群岛：建造超时空传送仪，摧毁古巴方向核弹井。",
     "Florida Keys: Build a Chronosphere and neutralize Cuban Nuke Silos.",
     "America", 10, False, [("建造超时空传送仪", "Build a Chronosphere", True), ("摧毁核弹井", "Destroy Nuke Silos", True)], "chrono"),
    ("oa12", "超时空风暴", "Chrono Storm",
     "莫斯科：清理落点，消灭克里姆林宫精英防守，结束战争。",
     "Moscow: Clear drop zones and destroy Kremlin elite defenses to end the war.",
     "America", 11, False, [("摧毁克里姆林宫防御", "Destroy Kremlin defenses", True)], "finale"),
]

OS = [
    ("os01", "红色黎明", "Red Dawn",
     "华盛顿特区：摧毁五角大楼，开启对美闪击。",
     "Washington D.C.: Destroy the Pentagon to open the invasion.",
     "Russia", 0, False, [("摧毁五角大楼", "Destroy the Pentagon", True)], "pentagon"),
    ("os02", "敌对海岸", "Hostile Shore",
     "佛罗里达海岸：建立立足点并歼灭盟军。",
     "Florida coast: Establish a foothold and eliminate the Allies.",
     "Cuba", 1, False, [("建立基地并歼灭盟军", "Build up and eliminate Allies", True)], "base"),
    ("os03", "大苹果", "Big Apple",
     "纽约：占领美国 Battle Lab，推进科技线。",
     "New York: Capture the American Battle Lab.",
     "Russia", 2, False, [("占领 Battle Lab", "Capture the Battle Lab", True)], "capture_lab"),
    ("os04", "后方战线", "Home Front",
     "符拉迪沃斯托克：保卫本土，歼灭来犯盟军。",
     "Vladivostok: Defend the homeland and destroy Allied invaders.",
     "Russia", 3, False, [("歼灭来犯盟军", "Destroy Allied invaders", True)], "base"),
    ("os05", "光之城", "City of Lights",
     "巴黎：保卫巴黎铁塔（特斯拉充能地标）。",
     "Paris: Defend the Eiffel Tower Tesla landmark.",
     "Russia", 4, False, [("保卫巴黎铁塔", "Defend the Eiffel Tower", True)], "protect_tower"),
    ("os06", "细分", "Sub-Divide",
     "珍珠港：摧毁盟军海军。",
     "Pearl Harbor: Destroy the Allied navy.",
     "Russia", 5, False, [("摧毁盟军舰队", "Destroy the Allied fleet", True)], "naval"),
    ("os07", "超时空防御", "Chrono Defense",
     "乌拉尔：不惜一切保卫 Battle Lab，防超时空突袭。",
     "Urals: Defend the Battle Lab against Chrono assaults at all costs.",
     "Russia", 6, False, [("保卫 Battle Lab", "Defend the Battle Lab", True)], "protect_lab"),
    ("os08", "亵渎", "Desecration",
     "华盛顿特区：占领白宫。",
     "Washington D.C.: Capture the White House.",
     "Russia", 7, False, [("占领白宫", "Capture the White House", True)], "capture_wh"),
    ("os09", "狐与猎犬", "The Fox and the Hound",
     "圣安东尼奥：追踪并摧毁尤里心灵设施。",
     "San Antonio: Hunt down Yuri's psychic facilities.",
     "Russia", 8, False, [("摧毁尤里心灵设施", "Destroy Yuri psychic facilities", True)], "beacon"),
    ("os10", "风雨同盟", "Weathered Alliance",
     "维尔京群岛：摧毁盟军天气控制器。",
     "Virgin Islands: Destroy the Allied Weather Control Device.",
     "Russia", 9, False, [("摧毁天气控制器", "Destroy the Weather Device", True)], "weather"),
    ("os11", "红色革命", "Red Revolution",
     "莫斯科：摧毁尤里盘踞的克里姆林宫总部。",
     "Moscow: Destroy Yuri's Kremlin headquarters.",
     "Russia", 10, False, [("摧毁尤里总部", "Destroy Yuri's HQ", True)], "finale"),
    ("os12", "极地风暴", "Polar Storm",
     "阿拉斯加：摧毁超时空传送仪，阻止盟军终局打击。",
     "Alaska: Destroy the Chronosphere and stop the Allied endgame.",
     "Russia", 11, False, [("摧毁超时空传送仪", "Destroy the Chronosphere", True)], "chrono_kill"),
]

YA = [
    ("ya01", "时间流逝", "Time Lapse", "YR盟军01：保护时间机器并击退尤里。", "YR Allied 01: Protect the Time Machine and repel Yuri.", "America", 0, False, [("保护时间机器", "Protect the Time Machine", True)], "time_machine"),
    ("ya02", "无形威胁", "Invisible Threat", "YR盟军02：肃清尤里前哨。", "YR Allied 02: Clear Yuri's outpost.", "America", 1, False, [("歼灭尤里部队", "Eliminate Yuri forces", True)], "base"),
    ("ya03", "大脑洗劫", "Brain Wash", "YR盟军03：摧毁心灵信标。", "YR Allied 03: Destroy the Psychic Beacon.", "America", 2, False, [("摧毁心灵信标", "Destroy the Psychic Beacon", True)], "beacon"),
    ("ya04", "万劫不复", "Hooked On Chrono", "YR盟军04：运用超时空打击尤里基地。", "YR Allied 04: Chrono-strike Yuri bases.", "America", 3, False, [("摧毁尤里基地", "Destroy Yuri bases", True)], "base"),
    ("ya05", "权力痴迷", "Power Play", "YR盟军05：夺取并守卫关键电厂。", "YR Allied 05: Seize and hold key power.", "America", 4, False, [("控制关键电厂", "Control key power plants", True)], "base"),
    ("ya06", "头颅狩猎", "Head Games", "YR盟军06：摧毁心灵控制仪原型。", "YR Allied 06: Destroy Dominator prototypes.", "America", 5, False, [("摧毁心灵控制仪", "Destroy Psychic Dominators", True)], "dominator"),
    ("ya07", "脑死亡", "Brain Dead", "YR盟军07：突袭尤里月球基地，结束危机。", "YR Allied 07: Assault Yuri's lunar base.", "America", 6, False, [("摧毁月球基地核心", "Destroy the lunar base core", True)], "finale"),
]

YS = [
    ("ys01", "倒转乾坤", "Time Shift", "YR苏军01：保护时间机器。", "YR Soviet 01: Protect the Time Machine.", "Russia", 0, False, [("保护时间机器", "Protect the Time Machine", True)], "time_machine"),
    ("ys02", "意外访客", "Unexpected Guests", "YR苏军02：击退尤里入侵。", "YR Soviet 02: Repel Yuri's invasion.", "Russia", 1, False, [("歼灭尤里入侵军", "Eliminate Yuri invaders", True)], "base"),
    ("ys03", "罗曼诺夫的王座", "Romanov's Return", "YR苏军03：营救并护送罗曼诺夫。", "YR Soviet 03: Rescue and escort Romanov.", "Russia", 2, False, [("护送罗曼诺夫", "Escort Romanov", True)], "base"),
    ("ys04", "深海", "Escape Velocity", "YR苏军04：夺取太空发射设施。", "YR Soviet 04: Seize space-launch facilities.", "Russia", 3, False, [("夺取发射设施", "Capture launch facilities", True)], "base"),
    ("ys05", "月球", "To The Moon", "YR苏军05：月球作战，摧毁尤里设施。", "YR Soviet 05: Lunar ops — destroy Yuri facilities.", "Russia", 4, False, [("摧毁月球设施", "Destroy lunar facilities", True)], "finale"),
    ("ys06", "重拳", "Deja Vu", "YR苏军06：摧毁心灵放大器网络。", "YR Soviet 06: Destroy Amplifier network.", "Russia", 5, False, [("摧毁心灵放大器", "Destroy Psychic Amplifiers", True)], "amp"),
    ("ys07", "心灵终结", "Huey Strike", "YR苏军07：摧毁终极心灵控制仪。", "YR Soviet 07: Destroy the final Dominator.", "Russia", 6, False, [("摧毁心灵控制仪", "Destroy the Psychic Dominator", True)], "dominator"),
]

def layout(kind, size, faction_enemy="Soviet"):
    e = 1
    mid = size // 2
    lines = []
    if kind == "hero":
        lines += [
            f"unit 0 Tanya 10 {size-12}",
            f"unit 0 GI 8 {size-10}",
            f"unit 0 GI 12 {size-10}",
            f"unit 0 Engineer 9 {size-8}",
            f"unit 0 GI 11 {size-8}",
            f"bld 0 Pillbox 14 {size-14}",
            f"bld 1 ConYard {size-18} 8",
            f"bld 1 TeslaReactor {size-24} 8",
            f"bld 1 Barracks {size-18} 16",
            f"bld 1 WarFactory {size-12} 14",
            f"bld 1 NavalYard {size-10} {size-28}",
            f"unit 1 Dreadnought {size-8} {size-24} guard",
            f"unit 1 Dreadnought {size-14} {size-22} guard",
            f"unit 1 Conscript {size-20} 20 guard",
            f"unit 1 Rhino {size-16} 22 guard",
        ]
    elif kind == "beacon":
        lines += [
            f"unit 0 MCV 12 {size-14}",
            f"unit 0 GI 10 {size-12}",
            f"unit 0 Engineer 14 {size-12}",
            f"bld 1 PsychicBeacon {mid} {mid}",
            f"bld 1 ConYard {size-16} 10",
            f"bld 1 TeslaReactor {size-22} 10",
            f"bld 1 Barracks {size-16} 18",
            f"unit 1 Conscript {mid-4} {mid} guard",
            f"unit 1 Conscript {mid+4} {mid} guard",
            f"unit 1 Rhino {mid} {mid+6} guard",
            f"bld -1 CivHouse {mid-8} {mid-6}",
            f"bld -1 CivHouse {mid+6} {mid-4}",
        ]
    elif kind == "beacon_timed":
        lines += layout("beacon", size)
    elif kind == "amp":
        lines += [
            f"unit 0 MCV 10 {size-12}",
            f"unit 0 GI 8 {size-10}",
            f"unit 0 AmphTransport 6 {size-16}",
            f"bld 1 PsychicAmplifier {size-20} 12",
            f"bld 1 ConYard {size-14} 20",
            f"bld 1 TeslaReactor {size-20} 20",
            f"bld 1 Barracks {size-14} 28",
            f"unit 1 Rhino {size-18} 30 guard",
            f"unit 1 Conscript {size-22} 26 guard",
        ]
    elif kind == "nuke2":
        lines += [
            f"unit 0 Tanya 8 {size-10}",
            f"unit 0 Spy 10 {size-10}",
            f"unit 0 Engineer 12 {size-10}",
            f"unit 0 GI 9 {size-12}",
            f"bld 1 NukeSilo {size-12} 8",
            f"bld 1 NukeSilo {size-20} 8",
            f"bld 1 BattleLab {size-16} 16",
            f"bld 1 ConYard {size-24} 14",
            f"bld 1 TeslaReactor {size-28} 14",
            f"bld 1 Barracks {size-24} 22",
            f"unit 1 Conscript {size-18} 20 guard",
            f"unit 1 TeslaTrooper {size-14} 18 guard",
        ]
    elif kind == "naval":
        lines += [
            f"unit 0 MCV 14 {size-14}",
            f"unit 0 Destroyer 8 {size-20}",
            f"unit 0 Destroyer 12 {size-22}",
            f"bld 0 NavalYard 10 {size-18}",
            f"bld 1 ConYard {size-16} 10",
            f"bld 1 NavalYard {size-12} 20",
            f"bld 1 TeslaReactor {size-22} 10",
            f"unit 1 Dreadnought {size-8} 24 guard",
            f"unit 1 Typhoon {size-10} 28 guard",
            f"unit 1 SeaScorpion {size-14} 26 guard",
        ]
    elif kind == "protect_lab":
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"bld 0 BattleLab 16 {size-16}",
            f"bld 0 PowerPlant 20 {size-16}",
            f"bld 0 Barracks 12 {size-20}",
            f"unit 0 GI 14 {size-18}",
            f"unit 0 GI 18 {size-18}",
            f"bld 1 ConYard {size-16} 10",
            f"bld 1 TeslaReactor {size-22} 10",
            f"bld 1 WarFactory {size-16} 18",
            f"unit 1 Rhino {size-14} 22 guard",
            f"unit 1 Apocalypse {size-20} 20 guard",
        ]
    elif kind == "protect_tower":
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"bld 0 TeslaCoil {mid} {mid}",  # stand-in for Eiffel
            f"bld 0 TeslaReactor {mid-4} {mid+4}",
            f"bld 1 ConYard {size-16} 10",
            f"bld 1 PowerPlant {size-22} 10",
            f"bld 1 WarFactory {size-16} 18",
            f"unit 1 Grizzly {size-14} 22 guard",
            f"unit 1 PrismTank {size-20} 20 guard",
        ]
    elif kind == "capture_lab":
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"unit 0 Engineer 10 {size-10}",
            f"unit 0 Conscript 14 {size-10}",
            f"bld 1 BattleLab {mid} {mid}",
            f"bld 1 PowerPlant {mid+4} {mid}",
            f"bld 1 ConYard {size-16} 10",
            f"unit 1 GI {mid-3} {mid+3} guard",
            f"unit 1 Grizzly {mid+3} {mid+3} guard",
        ]
    elif kind == "capture_wh":
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"unit 0 Engineer 10 {size-10}",
            f"bld 1 TechOutpost {mid} {mid}",  # White House stand-in
            f"bld 1 PowerPlant {mid+5} {mid}",
            f"bld 1 ConYard {size-16} 10",
            f"unit 1 GI {mid-2} {mid+2} guard",
            f"unit 1 PrismTank {mid+4} {mid+4} guard",
        ]
    elif kind == "pentagon":
        lines += [
            f"unit 0 MCV 10 {size-12}",
            f"unit 0 Conscript 8 {size-10}",
            f"unit 0 Rhino 12 {size-10}",
            f"bld 1 TechOutpost {mid} {mid}",  # Pentagon stand-in
            f"bld 1 PowerPlant {mid+5} {mid}",
            f"bld 1 Barracks {mid-5} {mid}",
            f"unit 1 GI {mid} {mid+4} guard",
            f"unit 1 Grizzly {mid+4} {mid+4} guard",
        ]
    elif kind == "chrono":
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"unit 0 GI 10 {size-10}",
            f"bld 1 NukeSilo {size-14} 10",
            f"bld 1 NukeSilo {size-22} 10",
            f"bld 1 ConYard {size-18} 18",
            f"bld 1 TeslaReactor {size-24} 18",
            f"unit 1 Conscript {size-16} 24 guard",
        ]
    elif kind == "chrono_kill":
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"bld 1 ChronoSphere {mid} {mid}",
            f"bld 1 PowerPlant {mid+4} {mid}",
            f"bld 1 ConYard {size-16} 10",
            f"unit 1 PrismTank {mid-4} {mid+4} guard",
            f"unit 1 GI {mid+2} {mid+4} guard",
        ]
    elif kind == "weather":
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"bld 1 WeatherDevice {mid} {mid}",
            f"bld 1 PowerPlant {mid+4} {mid}",
            f"bld 1 ConYard {size-16} 10",
            f"unit 1 PrismTank {mid} {mid+5} guard",
        ]
    elif kind == "dominator":
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"bld 1 PsychicDominator {mid} {mid}",
            f"bld 1 BioReactor {mid+5} {mid}",
            f"bld 1 ConYard {size-16} 10",
            f"unit 1 Initiate {mid-3} {mid} guard",
            f"unit 1 LasherTank {mid} {mid+5} guard",
        ]
    elif kind == "time_machine":
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"bld 0 TimeMachine {16} {size-18}",
            f"bld 0 PowerPlant {20} {size-18}",
            f"unit 0 GI 14 {size-14}",
            f"bld 1 ConYard {size-16} 10",
            f"bld 1 BioReactor {size-22} 10",
            f"unit 1 Initiate {size-14} 18 guard",
            f"unit 1 GatlingTank {size-18} 20 guard",
        ]
    elif kind == "finale":
        lines += [
            f"unit 0 MCV 10 {size-12}",
            f"unit 0 GI 8 {size-10}",
            f"bld 1 ConYard {size-16} 8",
            f"bld 1 BattleLab {size-22} 8",
            f"bld 1 WarFactory {size-16} 16",
            f"bld 1 Barracks {size-22} 16",
            f"unit 1 Apocalypse {size-14} 22 guard",
            f"unit 1 Rhino {size-20} 24 guard",
            f"unit 1 Conscript {size-18} 20 guard",
        ]
    else:  # base
        lines += [
            f"unit 0 MCV 12 {size-12}",
            f"unit 0 GI 10 {size-10}",
            f"unit 0 Engineer 14 {size-10}",
            f"bld 1 ConYard {size-16} 10",
            f"bld 1 TeslaReactor {size-22} 10" if faction_enemy != "Allies" else f"bld 1 PowerPlant {size-22} 10",
            f"bld 1 Barracks {size-16} 18",
            f"bld 1 WarFactory {size-12} 16",
            f"unit 1 Conscript {size-18} 22 guard" if faction_enemy != "Allies" else f"unit 1 GI {size-18} 22 guard",
            f"unit 1 Rhino {size-14} 24 guard" if faction_enemy != "Allies" else f"unit 1 Grizzly {size-14} 24 guard",
        ]
    return lines

def tech_for(line, idx):
    if line.startswith("oa") or line.startswith("ya"):
        if idx < 4: return EARLY_ALLIED_BLD, EARLY_ALLIED_UNIT
        if idx < 8: return MID_ALLIED_BLD, MID_ALLIED_UNIT
        return LATE_ALLIED_BLD, LATE_ALLIED_UNIT
    else:
        if idx < 4: return EARLY_SOV_BLD, EARLY_SOV_UNIT
        if idx < 8: return MID_SOV_BLD, MID_SOV_UNIT
        return LATE_SOV_BLD, LATE_SOV_UNIT

def make_series(entries, line_id, enemy_faction):
    files = []
    for id_, name, name_en, brief, brief_en, country, idx, no_start, objs, kind in entries:
        size = 96 if idx < 8 else 128
        # YR lunar finales use generated large maps
        if id_ in ("ya07", "ys05"):
            size = 96
        map_file = map_txt(id_, size, layout(kind, size, enemy_faction))
        ab, au = tech_for(id_, idx)
        if kind == "hero" or kind == "nuke2":
            # infiltration: no base tech
            ab = "Pillbox,Wall"
            au = "Tanya,GI,Engineer,Spy,AttackDog,GuardianGI,NavySEAL"
        gen = {
            "Name": name,
            "NameEn": name_en,
            "Brief": brief,
            "BriefEn": brief_en,
            "Faction": "Allies" if line_id in ("oa", "ya") else "Soviet",
            "AI": enemy_faction if kind not in ("dominator", "time_machine", "beacon") or line_id.startswith("y") else enemy_faction,
            "MapSize": size,
            "MapType": 1 if kind == "naval" else 0,
            "Money": 8000 + idx * 500,
            "Objective": 2,
            "ObjectiveTick": 0,
            "MapFile": map_file,
            "NoStartForce": True,
            "Track": 1,
            "LineId": line_id,
            "LineIndex": idx,
            "Country": country,
            "AllowedBuildings": ab,
            "AllowedUnits": au,
        }
        if line_id.startswith("y"):
            gen["AI"] = "Yuri"
        # rebuild map enemy faction for Yuri lines
        if line_id.startswith("y"):
            map_file = map_txt(id_, size, layout(kind, size, "Yuri"))
            gen["MapFile"] = map_file

        trigs = [
            {"Cond": "Always", "Act": "Objective", "Msg": objs[0][0], "MsgEn": objs[0][1]},
            {"Cond": "Always", "Act": "Eva", "Msg": brief[:40], "MsgEn": brief_en[:60]},
        ]
        # Lose: all player dead OR hero lost
        if kind in ("hero", "nuke2"):
            trigs.append({"Cond": "UnitLost", "C0": "0", "CType": "Tanya", "Act": "Lose",
                          "Msg": "谭雅阵亡，任务失败。", "MsgEn": "Tanya has fallen. Mission failed."})
        trigs.append({"Cond": "PlayerAllDead", "C0": "0", "Act": "Lose",
                      "Msg": "我军覆灭。", "MsgEn": "Our forces have been destroyed."})

        # Win conditions by kind
        if kind in ("beacon", "beacon_timed"):
            if kind == "beacon_timed":
                trigs.append({"Cond": "Time", "C0": "9000", "Act": "Lose",
                              "Msg": "信标完成充能，城市沦陷。", "MsgEn": "The Beacon charged. The city is lost."})
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "PsychicBeacon", "Act": "Win",
                          "Msg": "心灵信标已摧毁！", "MsgEn": "Psychic Beacon destroyed!"})
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "PsychicBeacon", "Act": "CompleteObj", "A0": "0",
                          "Msg": "信标已摧毁", "MsgEn": "Beacon destroyed"})
        elif kind == "amp":
            trigs.append({"Cond": "Time", "C0": "12000", "Act": "Lose",
                          "Msg": "放大器上线，任务失败。", "MsgEn": "Amplifier online. Mission failed."})
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "PsychicAmplifier", "Act": "Win",
                          "Msg": "放大器已摧毁！", "MsgEn": "Amplifier destroyed!"})
        elif kind == "nuke2":
            # Win when both silos gone — approximate with PlayerAllDead enemy or first silo + script chain
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "NukeSilo", "Act": "CompleteObj", "A0": "0",
                          "Msg": "核弹井正在被清除…", "MsgEn": "Nuke silos are falling…"})
            trigs.append({"Cond": "PlayerAllDead", "C0": "1", "Act": "Win",
                          "Msg": "任务完成！", "MsgEn": "Mission accomplished!"})
        elif kind in ("protect_lab",):
            btype = "BattleLab"
            trigs.append({"Cond": "PlayerBldLost", "C0": "0", "BType": btype, "Act": "Lose",
                          "Msg": "关键实验室被毁。", "MsgEn": "The laboratory was destroyed."})
            trigs.append({"Cond": "PlayerAllDead", "C0": "1", "Act": "Win",
                          "Msg": "实验室安全，敌军溃败！", "MsgEn": "Lab secure, enemy defeated!"})
        elif kind == "protect_tower":
            trigs.append({"Cond": "PlayerBldLost", "C0": "0", "BType": "TeslaCoil", "Act": "Lose",
                          "Msg": "铁塔失守。", "MsgEn": "The tower has fallen."})
            trigs.append({"Cond": "PlayerAllDead", "C0": "1", "Act": "Win",
                          "Msg": "巴黎守住了！", "MsgEn": "Paris holds!"})
        elif kind == "time_machine":
            trigs.append({"Cond": "PlayerBldLost", "C0": "0", "BType": "TimeMachine", "Act": "Lose",
                          "Msg": "时间机器被毁。", "MsgEn": "Time Machine destroyed."})
            trigs.append({"Cond": "PlayerAllDead", "C0": "1", "Act": "Win",
                          "Msg": "时间机器安全！", "MsgEn": "Time Machine secured!"})
        elif kind == "pentagon":
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "TechOutpost", "Act": "Win",
                          "Msg": "五角大楼已摧毁！", "MsgEn": "Pentagon destroyed!"})
        elif kind == "capture_lab":
            # Win if enemy dead (lab captured or destroyed via combat) — also Win on BattleLab lost by enemy
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "BattleLab", "Act": "Win",
                          "Msg": "Battle Lab 已夺取/摧毁！", "MsgEn": "Battle Lab secured!"})
        elif kind == "capture_wh":
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "TechOutpost", "Act": "Win",
                          "Msg": "白宫已夺取！", "MsgEn": "White House secured!"})
        elif kind == "weather":
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "WeatherDevice", "Act": "Win",
                          "Msg": "天气控制器已摧毁！", "MsgEn": "Weather Device destroyed!"})
        elif kind == "chrono_kill":
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "ChronoSphere", "Act": "Win",
                          "Msg": "超时空仪已摧毁！", "MsgEn": "Chronosphere destroyed!"})
        elif kind == "dominator":
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "PsychicDominator", "Act": "Win",
                          "Msg": "心灵控制仪已摧毁！", "MsgEn": "Psychic Dominator destroyed!"})
        elif kind == "chrono":
            trigs.append({"Cond": "PlayerBldLost", "C0": "1", "BType": "NukeSilo", "Act": "CompleteObj", "A0": "1",
                          "Msg": "核威胁削弱中…", "MsgEn": "Nuclear threat weakening…"})
            trigs.append({"Cond": "PlayerAllDead", "C0": "1", "Act": "Win",
                          "Msg": "佛罗里达已安全！", "MsgEn": "Florida is secure!"})
        else:
            trigs.append({"Cond": "PlayerAllDead", "C0": "1", "Act": "Win",
                          "Msg": "任务完成！", "MsgEn": "Mission accomplished!"})

        waves = None
        if kind in ("base", "finale", "naval"):
            if line_id.startswith("o") and line_id.startswith("oa") or line_id == "oa" or line_id.startswith("oa"):
                waves = [(3600, "Conscript,Rhino"), (7200, "Conscript,FlakTrooper,Rhino,Rhino")]
            elif line_id.startswith("os"):
                waves = [(3600, "GI,Grizzly"), (7200, "GI,GuardianGI,Grizzly,PrismTank")]
            else:
                waves = [(3600, "Initiate,LasherTank"), (7200, "Initiate,GatlingTank,LasherTank")]

        files.append(write_mission(f"{id_}.ini", gen, objs, trigs, waves))
    return files

def patch_fusion():
    lids = ["fc"] * 8 + ["fa"] * 8 + ["fs"] * 8 + ["fy"] * 8
    for i in range(1, 33):
        path = CAMP / f"mission{i:02d}.ini"
        text = path.read_text(encoding="utf-8")
        if "LineId=" in text:
            continue
        # insert after Track or ObjectiveTick
        insert = f"Track=0\nLineId={lids[i-1]}\nLineIndex={(i-1)%8}\n"
        if "Track=" in text:
            # already may have no Track
            pass
        lines = text.splitlines()
        out = []
        inserted = False
        for ln in lines:
            out.append(ln)
            if not inserted and ln.startswith("ObjectiveTick="):
                out.append(f"LineId={lids[i-1]}")
                out.append(f"LineIndex={(i-1)%8}")
                if not any(x.startswith("Track=") for x in lines):
                    out.append("Track=0")
                inserted = True
        if not inserted:
            out.append(f"LineId={lids[i-1]}")
            out.append(f"LineIndex={(i-1)%8}")
            out.append("Track=0")
        # ensure at least one Objective section for fusion
        body = "\n".join(out)
        if "[Objective." not in body:
            # pull brief
            brief = ""
            for ln in out:
                if ln.startswith("Brief=") and not ln.startswith("BriefEn"):
                    brief = ln[6:]
            body += f"\n\n[Objective.1]\nText={brief or '完成任务目标'}\nTextEn=Complete mission objectives\nPrimary=yes\n"
        path.write_text(body + "\n", encoding="utf-8")

def main():
    files = []
    files += make_series(OA, "oa", "Soviet")
    files += make_series(OS, "os", "Allies")
    files += make_series(YA, "ya", "Yuri")
    files += make_series(YS, "ys", "Yuri")
    patch_fusion()

    # campaign.ini
    lines = [
        "; OpenRA2 campaign list",
        "; First 32 = Fusion (China/Allies/Soviet/Yuri). Then Official RA2+YR.",
        "[Missions]",
    ]
    for i in range(1, 33):
        lines.append(f"Mission=mission{i:02d}.ini")
    for f in files:
        lines.append(f"Mission={f}")
    (CAMP / "campaign.ini").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # Remove old prototype files from being required (kept on disk but not listed)
    print(f"Generated {len(files)} official missions + patched fusion + campaign.ini")

if __name__ == "__main__":
    main()
