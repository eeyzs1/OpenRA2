#!/usr/bin/env python3
"""Thicken official + fusion campaign missions into scripted (non-skirmish) layouts."""
from __future__ import annotations
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAMP = ROOT / "assets" / "campaigns"
MAPS = ROOT / "maps"

def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    body = text.replace("\r\n", "\n").replace("\r", "\n")
    if not body.endswith("\n"):
        body += "\n"
    path.write_bytes(body.replace("\n", "\r\n").encode("utf-8"))

def map_base(theme: str, size: int = 96) -> list[str]:
    lines = [f"# Campaign map: {theme}", f"size {size} {size}", "fill clear"]
    if theme == "estuary":
        lines += [
            f"rect water {size//3} 0 {size//4} {size}",
            f"rect rough 6 {size//2} {size//4} {size//3}",
            f"rect rough {size*2//3} 8 {size//4} {size//3}",
        ]
    elif theme == "city":
        lines += [
            f"rect rough 16 16 {size-32} {size-32}",
            "bld -1 CivHouse 30 30",
            "bld -1 CivHouse 40 32",
            "bld -1 CivHouse 50 36",
            "bld -1 CivHouse 36 44",
            "bld -1 CivHouse 48 48",
        ]
    elif theme == "island":
        lines += [
            f"rect water 0 0 {size} {size}",
            f"rect rough 20 20 {size-40} {size-40}",
            f"blob water {size//2} {size//2} 4",
        ]
    elif theme == "forest":
        lines += [
            f"rect rough 0 0 {size} {size}",
            f"deco tree1 4 4 {size-8} {size-8} 20",
            f"deco rock1 {size//3} {size//3} 12 10 4",
        ]
    elif theme == "desert":
        lines += [
            f"rect rough 0 0 {size} {size}",
            f"blob ore 18 {size-24} 4",
            f"blob gems {size-28} {size-24} 3",
            f"deco rock1 10 10 {size-20} {size-20} 8",
        ]
    else:  # plains
        lines += [
            f"rect rough 12 12 {size-24} {size-24}",
            f"blob ore 16 {size-28} 4",
            f"blob gems {size-28} 20 3",
            f"deco tree1 4 4 20 20 6",
        ]
    lines += [
        f"blob ore 14 {size-22} 3",
        f"blob gems {size-22} 16 2",
    ]
    return lines

def spawn_player_inf(lines: list[str], hero: str | None = "Tanya") -> None:
    if hero:
        lines.append(f"unit 0 {hero} 12 82")
    lines += [
        "unit 0 GI 10 84",
        "unit 0 GI 14 84",
        "unit 0 Engineer 12 86",
    ]

def spawn_player_base(lines: list[str], faction: str = "Allies") -> None:
    lines += [
        "unit 0 MCV 12 80",
        "unit 0 GI 10 84",
        "unit 0 GI 14 84",
        "unit 0 Engineer 12 86",
        "unit 0 Harvester 16 82" if faction != "Yuri" else "unit 0 SlaveMiner 16 82",
    ]

def spawn_enemy_base(lines: list[str], target: str = "ConYard", extra_bld: str | None = None) -> None:
    lines += [
        "bld 1 ConYard 78 12",
        "bld 1 TeslaReactor 72 12" if target != "PowerPlant" else "bld 1 PowerPlant 72 12",
        "bld 1 Barracks 78 20",
        "bld 1 WarFactory 84 18",
        "unit 1 Conscript 76 24 guard",
        "unit 1 Conscript 80 22 guard",
        "unit 1 Rhino 82 26 guard",
    ]
    if extra_bld:
        lines.append(f"bld 1 {extra_bld} 70 28")
    if target != "ConYard":
        lines.append(f"bld 1 {target} 74 30")

def mission_ini(
    *,
    name: str,
    name_en: str,
    brief: str,
    brief_en: str,
    faction: str,
    ai: str,
    map_file: str,
    line_id: str,
    line_index: int,
    track: int,
    country: str,
    money: int,
    objs: list[tuple[str, str, bool]],  # text, textEn, gateWin
    hero_lose: str | None,
    destroy_target: str,
    destroy_obj_idx: int,
    phases: bool = True,
    timer: int | None = None,
    capture_bld: str | None = None,
    capture_obj_idx: int | None = None,
    protect_bld: str | None = None,
    allowed_b: str = "",
    allowed_u: str = "",
    brief_art: str = "",
    no_start: bool = True,
    map_size: int = 96,
    extra_trigs: list[str] | None = None,
    waves: list[tuple[int, str]] | None = None,
) -> str:
    lines = [
        f"; Scripted campaign mission: {name_en}",
        "[General]",
        f"Name={name}",
        f"NameEn={name_en}",
        f"Brief={brief}",
        f"BriefEn={brief_en}",
    ]
    if brief_art:
        lines.append(f"BriefArt={brief_art}")
    lines += [
        f"Faction={faction}",
        f"AI={ai}",
        f"MapSize={map_size}",
        "MapType=0",
        f"Money={money}",
        "Objective=2",
        "ObjectiveTick=0",
        f"MapFile={map_file}",
        f"NoStartForce={'yes' if no_start else 'no'}",
        f"Track={track}",
        f"LineId={line_id}",
        f"LineIndex={line_index}",
        f"Country={country}",
        "WinOnAllPrimary=yes",
        "Phase=0",
    ]
    if timer:
        lines.append("TimerVisible=yes")
    if allowed_b:
        lines.append(f"AllowedBuildings={allowed_b}")
    if allowed_u:
        lines.append(f"AllowedUnits={allowed_u}")
    lines.append("")
    for i, (t, te, gw) in enumerate(objs, 1):
        lines += [
            f"[Objective.{i}]",
            f"Text={t}",
            f"TextEn={te}",
            "Primary=yes",
            f"GateWin={'yes' if gw else 'no'}",
            "",
        ]
    n = 1
    def trig(block: str) -> None:
        nonlocal n
        lines.append(f"[Trig.{n}]")
        lines.append(block.strip())
        lines.append("")
        n += 1

    trig(f"Cond=Always\nAct=Objective\nMsg={objs[0][0]}\nMsgEn={objs[0][1]}")
    trig(f"Cond=Always\nAct=Eva\nMsg={brief}\nMsgEn={brief_en}")
    if phases:
        trig("Cond=Always\nAct=SetPhase\nA0=1")
    if timer:
        trig(f"Cond=Always\nAct=TimerStart\nA0={timer}\nA1=1\nMsg=倒计时开始\nMsgEn=Timer started")
    trig("Cond=PlayerAllDead\nC0=0\nAct=Lose\nMsg=我军覆灭。\nMsgEn=Our forces have been destroyed.")
    if hero_lose:
        trig(
            f"Cond=UnitLost\nC0=0\nCType={hero_lose}\nAct=Lose\n"
            f"Msg={hero_lose}阵亡，任务失败。\nMsgEn={hero_lose} has fallen. Mission failed."
        )
    if protect_bld:
        trig(
            f"Cond=PlayerBldLost\nC0=0\nBType={protect_bld}\nAct=Lose\n"
            f"Msg={protect_bld}被毁，任务失败。\nMsgEn={protect_bld} destroyed. Mission failed."
        )
    if capture_bld is not None and capture_obj_idx is not None:
        trig(
            f"Cond=BldCaptured\nC0=0\nBType={capture_bld}\nC2=1\nAct=CompleteObj\nA0={capture_obj_idx}\n"
            f"Msg=目标已占领\nMsgEn=Objective captured"
        )
        if phases:
            trig(
                f"Cond=BldCaptured\nC0=0\nBType={capture_bld}\nC2=1\nAct=SetPhase\nA0=2"
            )
    trig(
        f"Cond=PlayerBldLost\nC0=1\nBType={destroy_target}\nAct=CompleteObj\nA0={destroy_obj_idx}\n"
        f"Msg=主要目标已摧毁\nMsgEn=Primary target destroyed"
    )
    if timer:
        trig(
            f"Cond=PlayerBldLost\nC0=1\nBType={destroy_target}\nAct=TimerAbort\n"
            f"Msg=威胁解除\nMsgEn=Threat neutralized"
        )
    # Secondary cleanup: enemy ConYard if not already the target
    if destroy_target != "ConYard":
        # find if any gateWin obj still needs ConYard? optional secondary
        pass
    # light reinforce flavor
    trig(
        "Cond=Time\nC0=2400\nAct=Reinforce\nA0=1\nA1=1\nA2=70\nA3=30\n"
        "Units=Conscript,Conscript,Rhino\nOnce=yes\nMsg=敌军增援！\nMsgEn=Enemy reinforcements!"
    )
    if extra_trigs:
        for et in extra_trigs:
            trig(et)
    if waves:
        for i, (at, units) in enumerate(waves, 1):
            lines += [f"[Wave.{i}]", f"At={at}", f"Units={units}", ""]
    return "\n".join(lines) + "\n"

ALLIED_B = "ConYard,PowerPlant,Barracks,OreRefinery,Pillbox,Wall,WarFactory,NavalYard,Radar,AirForceCmd,PrismTower,PatriotMissile,BattleLab,OrePurifier,ServiceDepot"
ALLIED_U = "MCV,Harvester,ChronoMiner,GI,Engineer,AttackDog,Grizzly,Destroyer,AmphTransport,Tanya,NavySEAL,Spy,GuardianGI,IFV,PrismTank,Rocketeer,Intruder,Nighthawk,Aegis,AircraftCarrier,Sniper,Chrono"
SOV_B = "ConYard,TeslaReactor,Barracks,OreRefinery,SentryGun,Wall,WarFactory,NavalYard,Radar,BattleLab,TeslaCoil,FlakCannon,IronCurtain,NukeSilo,ServiceDepot"
SOV_U = "MCV,Harvester,WarMiner,Conscript,Engineer,AttackDog,FlakTrooper,TeslaTrooper,Rhino,FlakTrack,V3Launcher,Apocalypse,Kirov,Dreadnought,Typhoon,AmphTransport,Boris,Desolator"
YR_B = "ConYard,BioReactor,Barracks,OreRefinery,Wall,WarFactory,NavalYard,PsychicSensor,BattleLab,PsychicTower,GatlingCannon,CloningVat,PsychicDominator,Grinder"
YR_U = "MCV,SlaveMiner,Initiate,Engineer,Brute,Virus,Yuri,LasherTank,GatlingTank,Magnetron,MasterMind,FloatingDisc,Boomer,AmphTransport"

# Showcase already hand-authored — skip oa01, oa05, oa08
OFFICIAL = [
    # oa
    dict(key="oa02", line="oa", idx=1, name="雄鹰破晓", name_en="Eagle Dawn",
         brief="科罗拉多泉：占领空军学院教堂并肃清苏军。谭雅存活。",
         brief_en="Colorado Springs: Capture the Academy chapel and clear Soviets. Tanya survives.",
         faction="Allies", ai="Soviet", country="America", money=8500,
         theme="plains", hero="Tanya", capture="CivHouse", destroy="ConYard",
         objs=[("占领空军学院要点", "Capture the Academy site", True),
               ("摧毁苏军指挥部", "Destroy Soviet HQ", True),
               ("谭雅存活", "Tanya survives", False)],
         allowed_b="Pillbox,Wall", allowed_u="Tanya,GI,Engineer,Spy,AttackDog,GuardianGI,NavySEAL",
         art="assets/sprites/bld_psychicbeacon.png"),
    dict(key="oa03", line="oa", idx=2, name="向领袖致敬", name_en="Hail to the Chief",
         brief="华盛顿：限时摧毁心灵信标，解放总统。",
         brief_en="Washington: Destroy the Psychic Beacon in time and free the President.",
         faction="Allies", ai="Soviet", country="America", money=10000, timer=7200,
         theme="city", hero=None, destroy="PsychicBeacon",
         objs=[("限时摧毁心灵信标", "Destroy the Beacon in time", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U, art="assets/sprites/bld_psychicbeacon.png"),
    dict(key="oa04", line="oa", idx=3, name="最后机会", name_en="Last Chance",
         brief="芝加哥：在心灵放大器启动前摧毁它。",
         brief_en="Chicago: Destroy the Psychic Amplifier before it comes online.",
         faction="Allies", ai="Soviet", country="America", money=10500, timer=7800,
         theme="city", hero=None, destroy="PsychicAmplifier",
         objs=[("限时摧毁心灵放大器", "Destroy the Amplifier in time", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U, art="assets/sprites/bld_psychicamplifier.png"),
    dict(key="oa06", line="oa", idx=5, name="自由", name_en="Liberty",
         brief="华盛顿：接管五角大楼盟军并歼灭城区苏军指挥部。",
         brief_en="Washington: Reinforce the Pentagon allies and destroy Soviet HQ.",
         faction="Allies", ai="Soviet", country="America", money=12000,
         theme="city", hero=None, destroy="ConYard", base=True,
         objs=[("摧毁苏军指挥部", "Destroy Soviet HQ", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="oa07", line="oa", idx=6, name="深海", name_en="Deep Sea",
         brief="珍珠港：歼灭夏威夷周边苏军海军与指挥部。",
         brief_en="Pearl Harbor: Destroy Soviet naval forces and HQ around Hawaii.",
         faction="Allies", ai="Soviet", country="America", money=13000,
         theme="island", hero=None, destroy="NavalYard", base=True,
         objs=[("摧毁苏军船坞", "Destroy the Soviet Naval Yard", True),
               ("摧毁苏军指挥部", "Destroy Soviet HQ", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="oa09", line="oa", idx=8, name="太阳神庙", name_en="Sun Temple",
         brief="图卢姆：阻止苏军复制棱镜科技——占领或摧毁相关设施。",
         brief_en="Tulum: Stop Soviet Prism reverse-engineering — capture or destroy the lab.",
         faction="Allies", ai="Soviet", country="America", money=12000,
         theme="forest", hero=None, capture="BattleLab", destroy="ConYard", base=True,
         objs=[("占领苏军实验室", "Capture the Soviet lab", True),
               ("摧毁苏军指挥部", "Destroy Soviet HQ", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="oa10", line="oa", idx=9, name="幻影", name_en="Mirage",
         brief="黑森林：保护爱因斯坦实验室，歼灭苏军。",
         brief_en="Black Forest: Protect Einstein's lab and destroy the Soviets.",
         faction="Allies", ai="Soviet", country="Germany", money=13000,
         theme="forest", hero=None, destroy="ConYard", protect="BattleLab", base=True,
         objs=[("摧毁苏军指挥部", "Destroy Soviet HQ", True),
               ("爱因斯坦实验室必须存活", "Einstein's lab must survive", False)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="oa11", line="oa", idx=10, name="辐射尘", name_en="Fallout",
         brief="佛罗里达：建造超时空仪并摧毁古巴方向核弹井。",
         brief_en="Florida: Build toward Chronosphere tech and destroy Cuban Nuke Silos.",
         faction="Allies", ai="Soviet", country="America", money=14000,
         theme="island", hero=None, destroy="NukeSilo", base=True,
         objs=[("摧毁核弹井", "Destroy Nuke Silos", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="oa12", line="oa", idx=11, name="超时空风暴", name_en="Chrono Storm",
         brief="莫斯科：清理落点，摧毁克里姆林宫黑卫队指挥部，结束战争。",
         brief_en="Moscow: Clear LZs, destroy Kremlin Black Guard HQ, end the war.",
         faction="Allies", ai="Soviet", country="America", money=15000,
         theme="city", hero=None, destroy="ConYard", base=True,
         objs=[("摧毁克里姆林宫指挥部", "Destroy the Kremlin HQ", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    # os soviet
    dict(key="os01", line="os", idx=0, name="红色黎明", name_en="Red Dawn",
         brief="华盛顿：摧毁五角大楼。",
         brief_en="Washington: Destroy the Pentagon.",
         faction="Soviet", ai="Allies", country="Russia", money=8000,
         theme="city", hero=None, destroy="ConYard",
         objs=[("摧毁五角大楼指挥部", "Destroy the Pentagon HQ", True)],
         allowed_b="SentryGun,Wall", allowed_u="Conscript,Engineer,AttackDog,FlakTrooper,TeslaTrooper,Rhino"),
    dict(key="os02", line="os", idx=1, name="敌对海岸", name_en="Hostile Shore",
         brief="佛罗里达：建立立足点并摧毁盟军指挥部。",
         brief_en="Florida: Establish a beachhead and destroy Allied HQ.",
         faction="Soviet", ai="Allies", country="Russia", money=10000,
         theme="island", hero=None, destroy="ConYard", base=True,
         objs=[("摧毁盟军指挥部", "Destroy Allied HQ", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="os03", line="os", idx=2, name="大苹果", name_en="Big Apple",
         brief="纽约：占领美国 Battle Lab。",
         brief_en="New York: Capture the American Battle Lab.",
         faction="Soviet", ai="Allies", country="Russia", money=11000,
         theme="city", hero=None, capture="BattleLab", destroy="ConYard", base=True,
         objs=[("占领盟军实验室", "Capture Allied Battle Lab", True),
               ("摧毁盟军指挥部", "Destroy Allied HQ", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="os04", line="os", idx=3, name="祖国前线", name_en="Home Front",
         brief="符拉迪沃斯托克：保卫本土并摧毁入侵盟军指挥部。",
         brief_en="Vladivostok: Defend the homeland and destroy the invading Allied HQ.",
         faction="Soviet", ai="Allies", country="Russia", money=12000,
         theme="plains", hero=None, destroy="ConYard", base=True,
         objs=[("摧毁盟军指挥部", "Destroy Allied HQ", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="os05", line="os", idx=4, name="光之城", name_en="City of Lights",
         brief="巴黎：保卫铁塔充能节点，摧毁盟军指挥部。",
         brief_en="Paris: Protect the Eiffel charge node and destroy Allied HQ.",
         faction="Soviet", ai="Allies", country="Russia", money=12000,
         theme="city", hero=None, destroy="ConYard", protect="TeslaCoil", base=True,
         objs=[("摧毁盟军指挥部", "Destroy Allied HQ", True),
               ("特斯拉充能节点必须存活", "Tesla charge node must survive", False)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="os06", line="os", idx=5, name="潜艇分割", name_en="Sub-Divide",
         brief="珍珠港：摧毁盟军海军船坞。",
         brief_en="Pearl Harbor: Destroy the Allied Naval Yard.",
         faction="Soviet", ai="Allies", country="Russia", money=12500,
         theme="island", hero=None, destroy="NavalYard", base=True,
         objs=[("摧毁盟军船坞", "Destroy Allied Naval Yard", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="os07", line="os", idx=6, name="超时空防御", name_en="Chrono Defense",
         brief="乌拉尔：不惜一切保卫 Battle Lab，歼灭盟军突袭部队指挥部。",
         brief_en="Urals: Protect the Battle Lab at all costs; destroy Allied raid HQ.",
         faction="Soviet", ai="Allies", country="Russia", money=13000,
         theme="forest", hero=None, destroy="ConYard", protect="BattleLab", base=True,
         objs=[("摧毁盟军指挥部", "Destroy Allied HQ", True),
               ("Battle Lab 必须存活", "Battle Lab must survive", False)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="os08", line="os", idx=7, name="亵渎", name_en="Desecration",
         brief="华盛顿：占领白宫。",
         brief_en="Washington: Capture the White House.",
         faction="Soviet", ai="Allies", country="Russia", money=13000,
         theme="city", hero=None, capture="CivHouse", destroy="ConYard", base=True,
         objs=[("占领白宫", "Capture the White House", True),
               ("摧毁盟军残部指挥部", "Destroy remaining Allied HQ", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="os09", line="os", idx=8, name="狐与猎犬", name_en="The Fox and the Hound",
         brief="圣安东尼奥：追踪并摧毁尤里相关指挥节点。",
         brief_en="San Antonio: Track and destroy Yuri's command node.",
         faction="Soviet", ai="Yuri", country="Russia", money=13500,
         theme="desert", hero=None, destroy="PsychicBeacon", base=True,
         objs=[("摧毁心灵信标", "Destroy the Psychic Beacon", True)],
         allowed_b=SOV_B, allowed_u=SOV_U, art="assets/sprites/bld_psychicbeacon.png"),
    dict(key="os10", line="os", idx=9, name="风雨同盟", name_en="Weathered Alliance",
         brief="维尔京群岛：摧毁盟军天气控制器。",
         brief_en="Virgin Islands: Destroy the Allied Weather Control Device.",
         faction="Soviet", ai="Allies", country="Russia", money=14000,
         theme="island", hero=None, destroy="WeatherDevice", base=True,
         objs=[("摧毁天气控制器", "Destroy the Weather Device", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="os11", line="os", idx=10, name="红色革命", name_en="Red Revolution",
         brief="莫斯科：摧毁尤里盘踞的克里姆林宫总部。",
         brief_en="Moscow: Destroy Yuri's Kremlin headquarters.",
         faction="Soviet", ai="Yuri", country="Russia", money=14500,
         theme="city", hero="Boris", destroy="ConYard", base=True,
         objs=[("摧毁尤里总部", "Destroy Yuri's HQ", True),
               ("鲍里斯存活", "Boris survives", False)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="os12", line="os", idx=11, name="极地风暴", name_en="Polar Storm",
         brief="阿拉斯加：摧毁盟军超时空仪。",
         brief_en="Alaska: Destroy the Allied Chronosphere.",
         faction="Soviet", ai="Allies", country="Russia", money=15000,
         theme="plains", hero=None, destroy="ChronoSphere", base=True,
         objs=[("摧毁超时空仪", "Destroy the Chronosphere", True)],
         allowed_b=SOV_B, allowed_u=SOV_U, art="assets/sprites/bld_timemachine.png"),
]

# YR lines
YR = [
    dict(key="ya01", line="ya", idx=0, name="时光倒流", name_en="Time Lapse",
         brief="旧金山：占领电厂启动时间机器，保卫装置，摧毁恶魔岛心灵统治仪。",
         brief_en="San Francisco: Capture power to start the Time Machine, protect it, destroy Alcatraz Dominator.",
         faction="Allies", ai="Yuri", country="America", money=12000,
         theme="city", hero=None, capture="PowerPlant", destroy="PsychicDominator", protect="TimeMachine", base=True,
         objs=[("占领电厂启动时间机器", "Capture power plants for Time Machine", True),
               ("摧毁心灵统治仪", "Destroy the Psychic Dominator", True),
               ("时间机器必须存活", "Time Machine must survive", False)],
         allowed_b=ALLIED_B + ",TimeMachine", allowed_u=ALLIED_U, art="assets/sprites/bld_timemachine.png"),
    dict(key="ya02", line="ya", idx=1, name="好莱坞虚荣", name_en="Hollywood and Vain",
         brief="洛杉矶：摧毁城内所有粉碎机与尤里指挥部。",
         brief_en="Los Angeles: Destroy Grinders and Yuri's HQ.",
         faction="Allies", ai="Yuri", country="America", money=12500,
         theme="city", hero=None, destroy="Grinder", base=True,
         objs=[("摧毁粉碎机", "Destroy Grinders", True),
               ("摧毁尤里指挥部", "Destroy Yuri HQ", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="ya03", line="ya", idx=2, name="电力游戏", name_en="Power Play",
         brief="西雅图：摧毁尤里核弹井并解放园区。",
         brief_en="Seattle: Destroy Yuri Nuke Silos and liberate the campus.",
         faction="Allies", ai="Yuri", country="America", money=13000,
         theme="city", hero=None, destroy="NukeSilo", base=True,
         objs=[("摧毁核弹井", "Destroy Nuke Silos", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="ya04", line="ya", idx=3, name="古墓奇兵", name_en="Tomb Raided",
         brief="埃及：摧毁金字塔周围生物反应堆并拔除尤里基地。",
         brief_en="Egypt: Destroy Bio Reactors around the pyramids and Yuri's base.",
         faction="Allies", ai="Yuri", country="America", money=13000,
         theme="desert", hero=None, destroy="BioReactor", base=True,
         objs=[("摧毁生物反应堆", "Destroy Bio Reactors", True),
               ("摧毁尤里指挥部", "Destroy Yuri HQ", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="ya05", line="ya", idx=4, name="南方克隆", name_en="Clones Down Under",
         brief="悉尼：找到并摧毁克隆缸。",
         brief_en="Sydney: Find and destroy Cloning Vats.",
         faction="Allies", ai="Yuri", country="America", money=13500,
         theme="city", hero=None, destroy="CloningVats", base=True,
         objs=[("摧毁克隆缸", "Destroy Cloning Vats", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="ya06", line="ya", idx=5, name="条约诡计", name_en="Trick or Treaty",
         brief="伦敦：保卫议会，摧毁尤里基地。",
         brief_en="London: Protect Parliament and destroy Yuri's base.",
         faction="Allies", ai="Yuri", country="England", money=14000,
         theme="city", hero=None, destroy="ConYard", protect="CivHouse", base=True,
         objs=[("摧毁尤里指挥部", "Destroy Yuri HQ", True),
               ("议会必须存活", "Parliament must survive", False)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U),
    dict(key="ya07", line="ya", idx=6, name="脑死亡", name_en="Brain Dead",
         brief="南极：摧毁心灵统治仪并歼灭尤里。",
         brief_en="Antarctica: Destroy the Psychic Dominator and eliminate Yuri.",
         faction="Allies", ai="Yuri", country="America", money=15000,
         theme="plains", hero=None, destroy="PsychicDominator", base=True,
         objs=[("摧毁心灵统治仪", "Destroy the Psychic Dominator", True)],
         allowed_b=ALLIED_B, allowed_u=ALLIED_U, art="assets/sprites/bld_psychicamplifier.png"),
    dict(key="ys01", line="ys", idx=0, name="时间偏移", name_en="Time Shift",
         brief="旧金山：夺取时间机器，摧毁心灵统治仪。",
         brief_en="San Francisco: Seize the Time Machine and destroy the Dominator.",
         faction="Soviet", ai="Yuri", country="Russia", money=12000,
         theme="city", hero=None, capture="TimeMachine", destroy="PsychicDominator", base=True,
         objs=[("占领时间机器", "Capture the Time Machine", True),
               ("摧毁心灵统治仪", "Destroy the Dominator", True)],
         allowed_b=SOV_B + ",TimeMachine", allowed_u=SOV_U, art="assets/sprites/bld_timemachine.png"),
    dict(key="ys02", line="ys", idx=1, name="似曾相识", name_en="Deja Vu",
         brief="黑森林：摧毁爱因斯坦实验室与超时空仪。",
         brief_en="Black Forest: Destroy Einstein's lab and Chronosphere.",
         faction="Soviet", ai="Allies", country="Russia", money=12500,
         theme="forest", hero=None, destroy="ChronoSphere", base=True,
         objs=[("摧毁超时空仪", "Destroy the Chronosphere", True),
               ("摧毁实验室", "Destroy the Battle Lab", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="ys03", line="ys", idx=2, name="洗脑", name_en="Brain Wash",
         brief="伦敦：摧毁心灵信标与统治仪。",
         brief_en="London: Destroy the Psychic Beacon and Dominator.",
         faction="Soviet", ai="Yuri", country="Russia", money=13000,
         theme="city", hero=None, destroy="PsychicBeacon", base=True,
         objs=[("摧毁心灵信标", "Destroy the Beacon", True),
               ("摧毁心灵统治仪", "Destroy the Dominator", True)],
         allowed_b=SOV_B, allowed_u=SOV_U, art="assets/sprites/bld_psychicbeacon.png"),
    dict(key="ys04", line="ys", idx=3, name="逃亡的罗曼诺夫", name_en="Romanov on the Run",
         brief="摩洛哥：保护罗曼诺夫并摧毁把守机场的尤里基地。",
         brief_en="Morocco: Protect Romanov and destroy Yuri's airfield base.",
         faction="Soviet", ai="Yuri", country="Russia", money=13000,
         theme="desert", hero="Boris", destroy="ConYard", base=True,
         objs=[("摧毁尤里机场基地", "Destroy Yuri airfield base", True),
               ("鲍里斯（护送）存活", "Escort (Boris) survives", False)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="ys05", line="ys", idx=4, name="逃逸速度", name_en="Escape Velocity",
         brief="太平洋岛屿：摧毁尤里潜艇船坞。",
         brief_en="Pacific isle: Destroy Yuri's submarine pens.",
         faction="Soviet", ai="Yuri", country="Russia", money=13500,
         theme="island", hero=None, destroy="NavalYard", base=True,
         objs=[("摧毁潜艇船坞", "Destroy submarine pens", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="ys06", line="ys", idx=5, name="奔向月球", name_en="To the Moon",
         brief="月球：摧毁月球指挥中心。",
         brief_en="The Moon: Destroy the lunar command center.",
         faction="Soviet", ai="Yuri", country="Russia", money=14000,
         theme="desert", hero=None, destroy="ConYard", base=True,
         objs=[("摧毁月球指挥中心", "Destroy lunar command", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
    dict(key="ys07", line="ys", idx=6, name="头脑游戏", name_en="Head Games",
         brief="特兰西瓦尼亚：摧毁尤里要塞。",
         brief_en="Transylvania: Destroy Yuri's fortress.",
         faction="Soviet", ai="Yuri", country="Russia", money=15000,
         theme="forest", hero=None, destroy="ConYard", base=True,
         objs=[("摧毁尤里要塞", "Destroy Yuri's fortress", True)],
         allowed_b=SOV_B, allowed_u=SOV_U),
]

FUSION = [
    # (idx, line, name, name_en, brief, brief_en, faction, ai, country, theme, destroy, hero, money)
    (1, "fc", 0, "边境冲突", "Border Skirmish", "击退越境苏军并摧毁其指挥部。", "Repel Soviet invaders and destroy their HQ.", "China", "Soviet", "China", "plains", "ConYard", None, 9000),
    (2, "fc", 1, "防线加固", "Fortify the Line", "建立防线，摧毁苏军前哨指挥部。", "Fortify and destroy the Soviet outpost HQ.", "China", "Soviet", "China", "plains", "ConYard", None, 9500),
    (3, "fc", 2, "矿区争夺", "Ore Contest", "夺取矿区并摧毁敌军精炼指挥部。", "Secure ore fields and destroy enemy HQ.", "China", "Soviet", "China", "desert", "ConYard", None, 10000),
    (4, "fc", 3, "夜袭", "Night Raid", "夜袭摧毁苏军雷达与指挥部。", "Night raid: destroy Soviet Radar and HQ.", "China", "Soviet", "China", "forest", "Radar", None, 10000),
    (5, "fc", 4, "港口封锁", "Harbor Blockade", "封锁港口，摧毁苏军船坞。", "Blockade the harbor and destroy the Naval Yard.", "China", "Soviet", "China", "island", "NavalYard", None, 10500),
    (6, "fc", 5, "科技夺取", "Tech Seize", "占领敌方实验室并摧毁指挥部。", "Capture the enemy lab and destroy HQ.", "China", "Soviet", "China", "city", "ConYard", None, 11000),
    (7, "fc", 6, "决战高原", "Plateau Showdown", "高原决战：摧毁苏军主基地。", "Plateau showdown: destroy the main Soviet base.", "China", "Soviet", "China", "plains", "ConYard", None, 12000),
    (8, "fc", 7, "龙之反击", "Dragon Counter", "全面反击，拔除苏军最后指挥部。", "Full counterattack — destroy the last Soviet HQ.", "China", "Soviet", "China", "plains", "ConYard", None, 13000),
    (9, "fa", 0, "盟军集结", "Allied Muster", "集结盟军，摧毁苏军入侵指挥部。", "Muster Allies and destroy the Soviet invasion HQ.", "Allies", "Soviet", "America", "plains", "ConYard", "Tanya", 9000),
    (10, "fa", 1, "海岸防御", "Coast Guard", "保卫海岸，摧毁苏军登陆指挥部。", "Defend the coast; destroy Soviet landing HQ.", "Allies", "Soviet", "America", "island", "ConYard", None, 9500),
    (11, "fa", 2, "城市解放", "City Liberate", "摧毁心灵信标解放城市。", "Destroy the Psychic Beacon to free the city.", "Allies", "Soviet", "America", "city", "PsychicBeacon", None, 10000),
    (12, "fa", 3, "实验室护送", "Lab Escort", "保护实验室并摧毁敌军。", "Protect the lab and destroy the enemy HQ.", "Allies", "Soviet", "Germany", "forest", "ConYard", None, 10500),
    (13, "fa", 4, "海战演习", "Naval Drill", "摧毁敌军船坞。", "Destroy the enemy Naval Yard.", "Allies", "Soviet", "America", "island", "NavalYard", None, 11000),
    (14, "fa", 5, "棱镜计划", "Prism Protocol", "摧毁苏军仿制棱镜设施。", "Destroy Soviet Prism reverse-engineering.", "Allies", "Soviet", "France", "plains", "BattleLab", None, 11500),
    (15, "fa", 6, "超时空前夜", "Chrono Eve", "摧毁核弹井，为超时空铺路。", "Destroy Nuke Silos before Chrono strike.", "Allies", "Soviet", "America", "island", "NukeSilo", None, 12000),
    (16, "fa", 7, "莫斯科突袭", "Moscow Strike", "突袭莫斯科指挥部。", "Strike the Moscow HQ.", "Allies", "Soviet", "America", "city", "ConYard", "Tanya", 14000),
    (17, "fs", 0, "红色推进", "Red Advance", "推进并摧毁盟军前哨。", "Advance and destroy the Allied outpost.", "Soviet", "Allies", "Russia", "plains", "ConYard", None, 9000),
    (18, "fs", 1, "抢滩", "Beachhead", "抢滩建立基地，摧毁盟军。", "Secure a beachhead and destroy Allies.", "Soviet", "Allies", "Russia", "island", "ConYard", None, 9500),
    (19, "fs", 2, "占领实验室", "Seize Lab", "占领盟军 Battle Lab。", "Capture the Allied Battle Lab.", "Soviet", "Allies", "Russia", "city", "ConYard", None, 10000),
    (20, "fs", 3, "铁幕试炼", "Iron Trial", "摧毁盟军指挥部。", "Destroy the Allied HQ.", "Soviet", "Allies", "Russia", "plains", "ConYard", None, 10500),
    (21, "fs", 4, "海狼", "Sea Wolf", "摧毁盟军海军。", "Destroy Allied naval forces.", "Soviet", "Allies", "Russia", "island", "NavalYard", None, 11000),
    (22, "fs", 5, "白宫行动", "White House Op", "占领白宫要点。", "Capture the White House site.", "Soviet", "Allies", "Russia", "city", "ConYard", "Boris", 12000),
    (23, "fs", 6, "内战火花", "Civil Spark", "摧毁叛军（尤里）节点。", "Destroy the rebel (Yuri) node.", "Soviet", "Yuri", "Russia", "city", "PsychicBeacon", None, 12500),
    (24, "fs", 7, "极地终局", "Polar End", "摧毁超时空仪。", "Destroy the Chronosphere.", "Soviet", "Allies", "Russia", "plains", "Chronosphere", None, 14000),
    (25, "fy", 0, "心灵觉醒", "Psi Awaken", "建立尤里力量，摧毁盟军前哨。", "Raise Yuri forces; destroy Allied outpost.", "Yuri", "Allies", "Yuri", "plains", "ConYard", None, 9000),
    (26, "fy", 1, "信标部署", "Beacon Deploy", "保卫心灵信标并摧毁敌军。", "Defend Beacon plans; destroy enemy HQ.", "Yuri", "Allies", "Yuri", "city", "ConYard", None, 9500),
    (27, "fy", 2, "粉碎机", "The Grinder", "摧毁敌军并扩张粉碎机经济。", "Expand and destroy enemy HQ.", "Yuri", "Soviet", "Yuri", "city", "ConYard", None, 10000),
    (28, "fy", 3, "病毒扩散", "Virus Spread", "渗透摧毁敌军实验室。", "Infiltrate and destroy enemy lab.", "Yuri", "Allies", "Yuri", "forest", "BattleLab", None, 10500),
    (29, "fy", 4, "飞碟阴影", "Disc Shadow", "摧毁盟军防空指挥部。", "Destroy Allied AA command.", "Yuri", "Allies", "Yuri", "plains", "AirForceCmd", None, 11000),
    (30, "fy", 5, "克隆计划", "Clone Plan", "保护克隆缸，摧毁敌军。", "Protect cloning ops; destroy enemy HQ.", "Yuri", "Soviet", "Yuri", "city", "ConYard", None, 12000),
    (31, "fy", 6, "统治仪", "Dominator", "限时保护统治仪充能相关目标并摧毁敌军。", "Protect Dominator ops; destroy enemy HQ.", "Yuri", "Allies", "Yuri", "desert", "ConYard", None, 13000),
    (32, "fy", 7, "全球心控", "Global Psi", "终局：摧毁最后抵抗指挥部。", "Finale: destroy the last resistance HQ.", "Yuri", "Allies", "Yuri", "city", "ConYard", "Yuri", 15000),
]

def emit_official(m: dict) -> None:
    key = m["key"]
    map_rel = f"maps/official/{key}.txt"
    lines = map_base(m.get("theme", "plains"))
    if m.get("base"):
        spawn_player_base(lines, m["faction"])
    else:
        spawn_player_inf(lines, m.get("hero"))
    # protect building for player
    if m.get("protect"):
        lines.append(f"bld 0 {m['protect']} 20 70")
    # capture target owned by enemy or neutral
    if m.get("capture"):
        owner = -1 if m["capture"] == "CivHouse" else 1
        lines.append(f"bld {owner} {m['capture']} 48 48")
        if m["capture"] == "PowerPlant":
            lines.append("bld -1 PowerPlant 44 52")
            lines.append("bld -1 PowerPlant 52 52")
        if m["capture"] == "TimeMachine":
            lines.append("bld 1 TimeMachine 48 40")
    extra = None
    if m.get("destroy") and m["destroy"] not in ("ConYard", "NavalYard"):
        extra = None
    spawn_enemy_base(lines, target=m.get("destroy", "ConYard"),
                     extra_bld=m["destroy"] if m.get("destroy") not in ("ConYard",) else None)
    # Time machine device for ya01
    if key == "ya01":
        lines.append("bld 0 TimeMachine 24 72")
        lines.append("bld -1 PowerPlant 28 76")
        lines.append("bld -1 PowerPlant 32 76")
        lines.append("bld 1 PsychicDominator 70 40")
    if key == "ys02":
        lines.append("bld 1 BattleLab 70 34")
        lines.append("bld 1 Chronosphere 66 38")
    if key == "ys03":
        lines.append("bld 1 PsychicDominator 66 36")
    if key == "oa07" or key == "os06":
        lines.append("bld 1 NavalYard 80 70")
        lines.append("unit 1 Dreadnought 84 74 guard")
    write(MAPS / "official" / f"{key}.txt", "\n".join(lines) + "\n")

    objs = m["objs"]
    destroy_idx = 0
    for i, o in enumerate(objs):
        if o[2] and ("摧毁" in o[0] or "Destroy" in o[1] or "destroy" in o[1].lower()):
            destroy_idx = i
            break
    capture_idx = None
    for i, o in enumerate(objs):
        if o[2] and ("占领" in o[0] or "Capture" in o[1] or "capture" in o[1].lower()):
            capture_idx = i
            break
    # If capture is first gate and destroy second
    extra = []
    if m.get("destroy") == "NavalYard" and any("指挥部" in o[0] or "HQ" in o[1] for o in objs):
        # second complete on ConYard
        for i, o in enumerate(objs):
            if "指挥部" in o[0] or "HQ" in o[1]:
                extra.append(
                    f"Cond=PlayerBldLost\nC0=1\nBType=ConYard\nAct=CompleteObj\nA0={i}\n"
                    f"Msg=指挥部已摧毁\nMsgEn=HQ destroyed"
                )
                break
    if key == "ys02":
        extra.append(
            "Cond=PlayerBldLost\nC0=1\nBType=BattleLab\nAct=CompleteObj\nA0=1\n"
            "Msg=实验室已摧毁\nMsgEn=Lab destroyed"
        )
    if key == "ys03":
        extra.append(
            "Cond=PlayerBldLost\nC0=1\nBType=PsychicDominator\nAct=CompleteObj\nA0=1\n"
            "Msg=统治仪已摧毁\nMsgEn=Dominator destroyed"
        )
    if key == "ya02":
        extra.append(
            "Cond=PlayerBldLost\nC0=1\nBType=ConYard\nAct=CompleteObj\nA0=1\n"
            "Msg=尤里指挥部已摧毁\nMsgEn=Yuri HQ destroyed"
        )
    if key == "ya04":
        extra.append(
            "Cond=PlayerBldLost\nC0=1\nBType=ConYard\nAct=CompleteObj\nA0=1\n"
            "Msg=尤里基地已摧毁\nMsgEn=Yuri base destroyed"
        )

    ini = mission_ini(
        name=m["name"], name_en=m["name_en"], brief=m["brief"], brief_en=m["brief_en"],
        faction=m["faction"], ai=m["ai"], map_file=map_rel, line_id=m["line"], line_index=m["idx"],
        track=1, country=m["country"], money=m["money"], objs=objs,
        hero_lose=m.get("hero"), destroy_target=m.get("destroy", "ConYard"),
        destroy_obj_idx=destroy_idx, timer=m.get("timer"),
        capture_bld=m.get("capture"), capture_obj_idx=capture_idx,
        protect_bld=m.get("protect"),
        allowed_b=m.get("allowed_b", ""), allowed_u=m.get("allowed_u", ""),
        brief_art=m.get("art", ""), extra_trigs=extra or None,
    )
    write(CAMP / "official" / f"{key}.ini", ini)

def emit_fusion(row) -> None:
    idx, line, li, name, name_en, brief, brief_en, faction, ai, country, theme, destroy, hero, money = row
    key = f"mission{idx:02d}"
    map_rel = f"maps/fusion/{key}.txt"
    lines = map_base(theme, 80 if idx <= 8 else 96)
    spawn_player_base(lines, faction)
    if hero:
        lines.append(f"unit 0 {hero} 14 78")
    spawn_enemy_base(lines, target=destroy, extra_bld=destroy if destroy != "ConYard" else None)
    if destroy == "PsychicBeacon":
        lines.append("bld 1 PsychicBeacon 48 48")
    write(MAPS / "fusion" / f"{key}.txt", "\n".join(lines) + "\n")

    objs = [(brief[:40], brief_en[:60], True)]
    if hero:
        objs.append((f"{hero}必须存活", f"{hero} must survive", False))
    ab = ALLIED_B if faction == "Allies" else (SOV_B if faction == "Soviet" else (YR_B if faction == "Yuri" else ALLIED_B))
    au = ALLIED_U if faction == "Allies" else (SOV_U if faction == "Soviet" else (YR_U if faction == "Yuri" else ALLIED_U + ",Type99"))
    if faction == "China":
        ab = "ConYard,PowerPlant,Barracks,OreRefinery,Pillbox,Wall,WarFactory,Radar,BattleLab,ServiceDepot"
        au = "MCV,Harvester,GI,Engineer,AttackDog,Type99,IFV,Grizzly"
    art = ""
    if destroy in ("PsychicBeacon",):
        art = "assets/sprites/bld_psychicbeacon.png"
    if destroy in ("Chronosphere",):
        art = "assets/sprites/bld_timemachine.png"
    waves = [
        (2700, "Conscript,Conscript,Rhino"),
        (6300, "Conscript,FlakTrooper,Rhino,Rhino"),
        (10800, "TeslaTrooper,Rhino,V3Launcher"),
    ] if ai == "Soviet" else [
        (2700, "GI,GI,Grizzly"),
        (6300, "GI,GuardianGI,Grizzly,IFV"),
        (10800, "PrismTank,Grizzly,Rocketeer"),
    ]
    if ai == "Yuri":
        waves = [
            (2700, "Initiate,Initiate,LasherTank"),
            (6300, "Initiate,Brute,GatlingTank"),
            (10800, "Yuri,LasherTank,Magnetron"),
        ]
    ini = mission_ini(
        name=name, name_en=name_en, brief=brief, brief_en=brief_en,
        faction=faction, ai=ai, map_file=map_rel, line_id=line, line_index=li,
        track=0, country=country, money=money, objs=objs, hero_lose=hero,
        destroy_target=destroy, destroy_obj_idx=0, brief_art=art,
        allowed_b=ab, allowed_u=au, map_size=80 if idx <= 8 else 96,
        waves=waves,
    )
    write(CAMP / f"{key}.ini", ini)

def main() -> None:
    for m in OFFICIAL:
        emit_official(m)
    for m in YR:
        emit_official(m)
    for row in FUSION:
        emit_fusion(row)
    # Fix oa07 secondary obj: need ConYard complete — handled via extra
    print(f"Wrote {len(OFFICIAL)} official + {len(YR)} YR + {len(FUSION)} fusion missions")

if __name__ == "__main__":
    main()
