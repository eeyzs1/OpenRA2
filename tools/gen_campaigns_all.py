#!/usr/bin/env python3
"""Generate all 70 OpenRA2 campaign missions as full scripted content."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAMP, MAPS = ROOT / "assets" / "campaigns", ROOT / "maps"

def W(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    body = text.replace("\r\n", "\n").replace("\r", "\n")
    if not body.endswith("\n"): body += "\n"
    path.write_bytes(body.replace("\n", "\r\n").encode("utf-8"))

AB = "ConYard,PowerPlant,Barracks,OreRefinery,Pillbox,Wall,WarFactory,NavalYard,Radar,AirForceCmd,PrismTower,PatriotMissile,BattleLab,OrePurifier,ServiceDepot,GapGenerator,SpySat,ChronoSphere"
AU = "MCV,Harvester,ChronoMiner,GI,Engineer,AttackDog,Grizzly,Destroyer,AmphTransport,Tanya,NavySEAL,Spy,GuardianGI,IFV,PrismTank,Rocketeer,Intruder,Nighthawk,Aegis,AircraftCarrier,Sniper,Chrono,MirageTank"
ABE, AUE = "Pillbox,Wall", "Tanya,GI,Engineer,Spy,AttackDog,GuardianGI,NavySEAL,Rocketeer"
SB = "ConYard,TeslaReactor,Barracks,OreRefinery,SentryGun,Wall,WarFactory,NavalYard,Radar,BattleLab,TeslaCoil,FlakCannon,IronCurtain,NukeSilo,ServiceDepot,BattleBunker,TankBunker"
SU = "MCV,Harvester,WarMiner,Conscript,Engineer,AttackDog,FlakTrooper,TeslaTrooper,Rhino,FlakTrack,V3Launcher,Apocalypse,Kirov,Dreadnought,Typhoon,AmphTransport,Boris,Desolator,TerrorDrone,SeaScorpion"
SBE, SUE = "SentryGun,Wall", "Conscript,Engineer,AttackDog,FlakTrooper,TeslaTrooper,Rhino"
YB = "ConYard,BioReactor,Barracks,OreRefinery,Wall,WarFactory,NavalYard,PsychicSensor,BattleLab,PsychicTower,GatlingCannon,CloningVat,PsychicDominator,Grinder,GeneticMutator"
YU = "MCV,SlaveMiner,Initiate,Engineer,Brute,Virus,Yuri,LasherTank,GatlingTank,Magnetron,MasterMind,FloatingDisc,Boomer,AmphTransport,ChaosDrone"
CB = "ConYard,PowerPlant,Barracks,OreRefinery,Pillbox,Wall,WarFactory,Radar,BattleLab,ServiceDepot,FlakCannon"
CU = "MCV,Harvester,PLA,Engineer,AttackDog,Type99,IFV,Grizzly,BlackEagle"

def trig(**kw) -> str:
    keys = ["Cond","Act","C0","C1","C2","C3","C4","CType","BType","UType","A0","A1","A2","A3","A4",
            "Units","Tag","RequiresPhase","Enabled","Once","Msg","MsgEn"]
    lines = [f"{k}={kw[k]}" for k in keys if k in kw]
    for k, v in kw.items():
        if k not in keys:
            lines.append(f"{k}={v}")
    return "\n".join(lines)

def write_ini(path: Path, d: dict) -> None:
    L = [f"; Full scripted: {d['name_en']}", "[General]",
         f"Name={d['name']}", f"NameEn={d['name_en']}", f"Brief={d['brief']}", f"BriefEn={d['brief_en']}"]
    if d.get("art"): L.append(f"BriefArt={d['art']}")
    L += [f"Faction={d['faction']}", f"AI={d['ai']}", f"MapSize={d.get('size',96)}", "MapType=0",
          f"Money={d['money']}", "Objective=2", "ObjectiveTick=0", f"MapFile={d['map']}",
          f"NoStartForce={'yes' if d.get('ns',True) else 'no'}", f"Track={d['track']}",
          f"LineId={d['line']}", f"LineIndex={d['idx']}", f"Country={d['country']}",
          "WinOnAllPrimary=yes", f"Phase={d.get('phase',0)}"]
    if d.get("timer"): L.append("TimerVisible=yes")
    if d.get("ab"): L.append(f"AllowedBuildings={d['ab']}")
    if d.get("au"): L.append(f"AllowedUnits={d['au']}")
    L.append("")
    for i,(zh,en,g) in enumerate(d["objs"],1):
        L += [f"[Objective.{i}]", f"Text={zh}", f"TextEn={en}", "Primary=yes", f"GateWin={'yes' if g else 'no'}", ""]
    for i,b in enumerate(d["trigs"],1):
        L += [f"[Trig.{i}]", b.strip(), ""]
    for i,(at,u) in enumerate(d.get("waves") or [],1):
        L += [f"[Wave.{i}]", f"At={at}", f"Units={u}", ""]
    W(path, "\n".join(L)+"\n")

def base_map(title, size=96, theme="plains"):
    L = [f"# {title}", f"size {size} {size}", "fill clear"]
    if theme == "estuary":
        L += [f"rect water {size//3} 0 {size//4} {size}", f"rect rough 6 {size//2} {size//4} {size//3}",
              f"rect rough {size*2//3} 8 {size//4} {size//3}"]
    elif theme == "city":
        L += [f"rect rough 14 14 {size-28} {size-28}"]
        for x,y in [(28,28),(34,30),(40,28),(32,36),(38,38),(44,34),(30,42),(42,44),(48,40)]:
            L.append(f"bld -1 CivHouse {x} {y}")
    elif theme == "island":
        L += [f"rect water 0 0 {size} {size}", f"rect rough 14 14 {size-28} {size-28}",
              f"rect rough {size//2+8} {size//2+8} 24 24"]
    elif theme == "forest":
        L += [f"rect rough 0 0 {size} {size}", f"deco tree1 2 2 {size-4} {size-4} 24",
              f"deco rock1 {size//3} {size//3} 16 12 6"]
    elif theme == "desert":
        L += [f"rect rough 0 0 {size} {size}", f"deco rock1 8 8 {size-16} {size-16} 14"]
    elif theme == "beach":
        L += [f"rect water 0 {size//2-8} {size} {size//2+8}", f"rect rough 8 8 {size//2} {size//2-12}",
              f"rect rough {size//2} {size//2+4} {size//2-8} {size//2-12}"]
    else:
        L += [f"rect rough 10 10 {size-20} {size-20}", f"deco tree1 4 4 18 18 6"]
    L += [f"blob ore 12 {size-20} 4", f"blob ore {size-22} 16 4", f"blob gems {size//2} {size-16} 3",
          f"blob ore 36 24 3"]
    return L

def ebase(L, x, y, fac, extras=None, guards=None):
    power = {"Soviet":"TeslaReactor","Yuri":"BioReactor","China":"PowerPlant"}.get(fac,"PowerPlant")
    L += [f"bld 1 ConYard {x} {y}", f"bld 1 {power} {x-6} {y}", f"bld 1 Barracks {x} {y+8}",
          f"bld 1 WarFactory {x+6} {y+6}", f"bld 1 OreRefinery {x-8} {y+8}", f"bld 1 Radar {x+2} {y+14}"]
    if fac=="Soviet": L += [f"bld 1 TeslaCoil {x-4} {y+16}", f"bld 1 FlakCannon {x+8} {y+16}"]
    elif fac=="Allies": L += [f"bld 1 PrismTower {x-4} {y+16}", f"bld 1 PatriotMissile {x+8} {y+16}"]
    elif fac=="Yuri": L += [f"bld 1 PsychicTower {x-4} {y+16}", f"bld 1 GatlingCannon {x+8} {y+16}"]
    for i,b in enumerate(extras or []):
        L.append(f"bld 1 {b} {x-4+(i%3)*6} {y+20+(i//3)*6}")
    g = guards or {"Soviet":["Conscript","Conscript","TeslaTrooper","Rhino","FlakTrack"],
                   "Allies":["GI","GI","GuardianGI","Grizzly","IFV"],
                   "Yuri":["Initiate","Initiate","Brute","LasherTank","GatlingTank"],
                   "China":["PLA","PLA","Type99","IFV"]}.get(fac,["Conscript","Rhino"])
    for i,u in enumerate(g):
        L.append(f"unit 1 {u} {x-2+(i%4)*3} {y+10+(i//4)*3} guard")

def psquad(L, x, y, units):
    for i,u in enumerate(units):
        L.append(f"unit 0 {u} {x+(i%4)*2} {y+(i//4)*2}")

def pmcv(L, x, y, fac):
    miner = {"Yuri":"SlaveMiner","Soviet":"WarMiner"}.get(fac,"Harvester")
    inf = {"Yuri":"Initiate","Soviet":"Conscript","China":"PLA"}.get(fac,"GI")
    L += [f"unit 0 MCV {x} {y}", f"unit 0 {inf} {x-2} {y+2}", f"unit 0 {inf} {x+2} {y+2}",
          f"unit 0 Engineer {x} {y+3}", f"unit 0 {miner} {x+4} {y}"]

def std_fail(hero=None, protect=None):
    t = [trig(Cond="PlayerAllDead", C0=0, Act="Lose", Msg="我军覆灭。", MsgEn="Our forces destroyed.")]
    if hero:
        t.append(trig(Cond="UnitLost", C0=0, CType=hero, Act="Lose", Msg=f"{hero}阵亡。", MsgEn=f"{hero} fallen."))
    if protect:
        t.append(trig(Cond="PlayerBldLost", C0=0, BType=protect, Act="Lose",
                      Msg=f"{protect}被毁。", MsgEn=f"{protect} destroyed."))
    return t

def boot(msg_zh, msg_en, phase=1, timer=None):
    t = [trig(Cond="Always", Act="Objective", Msg=msg_zh, MsgEn=msg_en),
         trig(Cond="Always", Act="Eva", Msg=msg_zh, MsgEn=msg_en),
         trig(Cond="Always", Act="SetPhase", A0=phase)]
    if timer:
        t.append(trig(Cond="Always", Act="TimerStart", A0=timer, A1=1))
    return t

def emit(key, folder, d, ml):
    d["map"] = f"maps/{folder}/{key}.txt"
    W(MAPS/folder/f"{key}.txt", "\n".join(ml)+"\n")
    out = CAMP/"official"/f"{key}.ini" if folder=="official" else CAMP/f"{key}.ini"
    write_ini(out, d)

# ========== ALL MISSIONS ==========
def build():
    n = 0
    # ---- OA Allied 12 ----
    missions_oa = []

    # oa01
    ml = base_map("oa01 NY estuary", theme="estuary")
    ml += ["rect water 30 55 36 41", "deco tree1 4 4 28 40 12"]
    psquad(ml, 12, 84, ["Tanya","GI","GI","GI","Engineer","GuardianGI"])
    ml += ["bld 0 Pillbox 20 44","bld 0 Pillbox 26 48","bld -1 CivHouse 22 42","bld -1 OilDerrick 24 52",
           "bld 1 NavalYard 76 68","unit 1 Dreadnought 82 72 guard","unit 1 Dreadnought 76 74 guard",
           "unit 1 Dreadnought 84 66 guard","unit 1 SeaScorpion 80 70 guard"]
    ebase(ml, 78, 10, "Soviet")
    emit("oa01","official", dict(
        name="孤独的守护者", name_en="Lone Guardian",
        brief="纽约：摧毁无畏舰→会合布拉德利堡→捣毁补给基地。谭雅存活。",
        brief_en="NY: Sink Dreadnoughts, link Fort Bradley, raze supply base. Tanya lives.",
        faction="Allies", ai="Soviet", country="America", money=8000, line="oa", idx=0, track=1,
        ab=ABE, au=AUE, art="assets/sprites/bld_psychicbeacon.png",
        objs=[("摧毁无畏舰舰队","Destroy Dreadnought fleet",True),("与布拉德利堡会合","Link Fort Bradley",True),
              ("捣毁补给基地","Destroy supply base",True),("谭雅存活","Tanya lives",False)],
        trigs=boot("阶段1：摧毁无畏舰","Phase1: sink fleet")+std_fail("Tanya")+[
            trig(Cond="PlayerBldLost",C0=1,BType="NavalYard",Act="CompleteObj",A0=0,RequiresPhase=1,Msg="舰队瘫痪！",MsgEn="Fleet down!"),
            trig(Cond="PlayerBldLost",C0=1,BType="NavalYard",Act="SetPhase",A0=2,RequiresPhase=1),
            trig(Cond="UnitInRect",C0=0,C1=16,C2=40,C3=30,C4=52,Act="CompleteObj",A0=1,RequiresPhase=2,Msg="已会合！",MsgEn="Linked!"),
            trig(Cond="UnitInRect",C0=0,C1=16,C2=40,C3=30,C4=52,Act="SetPhase",A0=3,RequiresPhase=2),
            trig(Cond="UnitInRect",C0=0,C1=16,C2=40,C3=30,C4=52,Act="Reinforce",A0=0,A1=3,A2=22,A3=45,
                 Units="GI,GI,GuardianGI,Engineer",RequiresPhase=2,Msg="布拉德利堡增援！",MsgEn="Bradley reinforce!"),
            trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=2,RequiresPhase=3,Msg="基地摧毁！",MsgEn="Base razed!"),
            trig(Cond="Time",C0=1500,Act="Reinforce",A0=1,A1=1,A2=70,A3=20,Units="Conscript,Rhino",Once="yes"),
        ]), ml); n+=1

    def oa(key, idx, name, name_en, brief, brief_en, country, money, theme, setup, objs, trigs, ab=AB, au=AU, timer=None, art="", hero=None, protect=None):
        nonlocal n
        ml = base_map(f"{key} {name_en}", theme=theme)
        setup(ml)
        emit(key,"official", dict(
            name=name, name_en=name_en, brief=brief, brief_en=brief_en,
            faction="Allies", ai="Soviet", country=country, money=money, line="oa", idx=idx, track=1,
            ab=ab, au=au, timer=timer, art=art,
            objs=objs, trigs=boot(brief, brief_en, timer=timer)+std_fail(hero, protect)+trigs
        ), ml); n+=1

    def s_oa02(ml):
        psquad(ml,10,82,["Tanya","GI","GI","Engineer","Spy","Rocketeer","Rocketeer"])
        ml.append("bld 1 BattleLab 48 44"); ml.append("bld -1 TechAirport 52 40")
        ebase(ml,76,12,"Soviet"); ml.append("unit 1 Conscript 46 48 guard")
    oa("oa02",1,"雄鹰破晓","Eagle Dawn","占领空军学院实验室并摧毁苏军指挥部。谭雅存活。",
       "Capture Academy lab and destroy Soviet HQ. Tanya lives.","America",8500,"plains",s_oa02,
       [("占领学院实验室","Capture Academy lab",True),("摧毁苏军指挥部","Destroy Soviet HQ",True),("谭雅存活","Tanya lives",False)],
       [trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="CompleteObj",A0=0,Msg="学院夺回！",MsgEn="Academy secured!"),
        trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="SetPhase",A0=2),
        trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="Reinforce",A0=0,A1=2,A2=48,A3=48,Units="GI,Rocketeer"),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="肃清！",MsgEn="Cleared!")],
       ab=ABE, au=AUE, hero="Tanya")

    def s_oa03(ml):
        pmcv(ml,12,80,"Allies"); ml += ["unit 0 Grizzly 16 80","bld 1 PsychicBeacon 48 46"]
        ebase(ml,78,10,"Soviet"); ml += ["unit 1 Conscript 44 46 guard","unit 1 Rhino 48 52 guard"]
    oa("oa03",2,"向领袖致敬","Hail to the Chief","限时摧毁华盛顿心灵信标并拔除苏军指挥部。",
       "Destroy DC Psychic Beacon in time and Soviet HQ.","America",10000,"city",s_oa03,
       [("限时摧毁心灵信标","Destroy Beacon in time",True),("摧毁苏军指挥部","Destroy Soviet HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="CompleteObj",A0=0,Msg="信标已毁！",MsgEn="Beacon down!"),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="TimerAbort"),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="完成！",MsgEn="Done!"),
        trig(Cond="Time",C0=3600,Act="Reinforce",A0=1,A1=1,A2=48,A3=48,Units="Conscript,Rhino,Rhino",Once="yes")],
       timer=7200, art="assets/sprites/bld_psychicbeacon.png")

    def s_oa04(ml):
        pmcv(ml,14,18,"Allies"); ml += ["unit 0 AmphTransport 20 36","unit 0 Grizzly 18 16","bld 1 PsychicAmplifier 70 70"]
        ebase(ml,78,58,"Soviet",extras=[],guards=["Conscript","Rhino","Apocalypse","FlakTrack"])
    oa("oa04",3,"最后机会","Last Chance","芝加哥抢滩，限时摧毁心灵放大器。",
       "Chicago beachhead: destroy Amplifier in time.","America",10500,"beach",s_oa04,
       [("限时摧毁心灵放大器","Destroy Amplifier in time",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="PsychicAmplifier",Act="CompleteObj",A0=0,Msg="放大器摧毁！",MsgEn="Amplifier destroyed!"),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicAmplifier",Act="TimerAbort"),
        trig(Cond="Time",C0=3000,Act="Reinforce",A0=1,A1=0,A2=70,A3=70,Units="Rhino,V3Launcher",Once="yes")],
       timer=7800, art="assets/sprites/bld_psychicamplifier.png")

    def s_oa05(ml):
        psquad(ml,8,82,["Tanya","Spy","Engineer","GI","AttackDog","NavySEAL"])
        ml += ["bld 1 BattleLab 74 28","bld 1 NukeSilo 82 10","bld 1 NukeSilo 74 8","bld 1 TeslaCoil 68 32"]
        ebase(ml,70,16,"Soviet"); ml += ["unit 1 Conscript 50 40 guard","unit 1 AttackDog 60 35 guard"]
    oa("oa05",4,"暗夜","Dark Night","占领Battle Lab揭示核弹井，摧毁两座井。谭雅存活。",
       "Capture Battle Lab to reveal silos; destroy both. Tanya lives.","Germany",10000,"forest",s_oa05,
       [("占领Battle Lab","Capture Battle Lab",True),("摧毁核弹井","Destroy Nuke Silos",True),("谭雅存活","Tanya lives",False)],
       [trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="CompleteObj",A0=0,Msg="坐标上传！",MsgEn="Coords up!"),
        trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="RevealMap",A0=0,A1=78,A2=12,A3=20),
        trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="NukeSilo",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="井已清除！",MsgEn="Silos gone!")],
       ab=ABE, au=AUE, hero="Tanya", art="assets/sprites/bld_psychicamplifier.png")

    def s_oa06(ml):
        pmcv(ml,10,78,"Allies"); ml += ["bld 0 Pillbox 24 70","bld 0 Pillbox 28 72","unit 0 Grizzly 30 76"]
        ebase(ml,76,14,"Soviet",extras=["TeslaCoil"],guards=["Conscript","Rhino","Apocalypse","V3Launcher"])
    oa("oa06",5,"自由","Liberty","增援五角大楼，摧毁华盛顿苏军指挥部。",
       "Reinforce Pentagon; destroy DC Soviet HQ.","America",12000,"city",s_oa06,
       [("摧毁苏军指挥部","Destroy Soviet HQ",True),("五角大楼防线存活","Pentagon line lives",False)],
       [trig(Cond="Always",Act="Reinforce",A0=0,A1=3,A2=26,A3=72,Units="GI,GI,GuardianGI,IFV",Msg="五角大楼增援！",MsgEn="Pentagon reinforce!"),
        trig(Cond="PlayerBldLost",C0=0,BType="Pillbox",Act="Lose",Msg="防线失守。",MsgEn="Line fallen."),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="华盛顿肃清！",MsgEn="DC cleared!"),
        trig(Cond="Time",C0=2700,Act="Reinforce",A0=1,A1=1,A2=60,A3=40,Units="Rhino,Kirov",Once="yes")],
       protect="Pillbox")

    def s_oa07(ml):
        pmcv(ml,16,20,"Allies")
        ml += ["bld 0 NavalYard 22 30","unit 0 Destroyer 28 36","unit 0 Destroyer 32 34","unit 0 Aegis 26 38",
               "bld 1 NavalYard 70 70","unit 1 Dreadnought 76 74 guard","unit 1 Typhoon 68 72 guard"]
        ebase(ml,72,52,"Soviet")
    oa("oa07",6,"深海","Deep Sea","摧毁苏军船坞与指挥部，肃清珍珠港海域。",
       "Destroy Soviet Naval Yard and HQ at Pearl Harbor.","America",13000,"island",s_oa07,
       [("摧毁苏军船坞","Destroy Naval Yard",True),("摧毁苏军指挥部","Destroy Soviet HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="NavalYard",Act="CompleteObj",A0=0,Msg="船坞已毁！",MsgEn="Yard down!"),
        trig(Cond="PlayerBldLost",C0=1,BType="NavalYard",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="海域肃清！",MsgEn="Seas cleared!"),
        trig(Cond="Time",C0=2400,Act="Reinforce",A0=1,A1=1,A2=74,A3=74,Units="Dreadnought,SeaScorpion",Once="yes")])

    def s_oa08(ml):
        pmcv(ml,12,80,"Allies"); ml += ["unit 0 Grizzly 16 78","bld 1 PsychicBeacon 48 46"]
        ebase(ml,78,12,"Soviet"); ml += ["unit 1 Conscript 44 46 guard","unit 1 Rhino 48 52 guard"]
    oa("oa08",7,"自由之门","Free Gateway","限时摧毁圣路易斯心灵信标并拔除指挥部。",
       "Destroy St. Louis Beacon in time and Soviet HQ.","America",11500,"city",s_oa08,
       [("限时摧毁心灵信标","Destroy Beacon in time",True),("摧毁苏军指挥部","Destroy Soviet HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="CompleteObj",A0=0,Msg="信标已毁！",MsgEn="Beacon down!"),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="TimerAbort"),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="解放！",MsgEn="Liberated!"),
        trig(Cond="Time",C0=4500,Act="Reinforce",A0=1,A1=1,A2=48,A3=48,Units="Conscript,Rhino",Once="yes")],
       timer=9000, art="assets/sprites/bld_psychicbeacon.png")

    def s_oa09(ml):
        pmcv(ml,12,80,"Allies"); ml += ["bld 1 BattleLab 48 40","bld 1 PrismTower 44 36","bld 1 PrismTower 52 36"]
        ebase(ml,74,14,"Soviet",guards=["Conscript","Rhino","TeslaTank","FlakTrack"])
    oa("oa09",8,"太阳神庙","Sun Temple","图卢姆：占领/摧毁棱镜仿制实验室并拔除指挥部。",
       "Tulum: capture/destroy Prism lab and Soviet HQ.","America",12000,"forest",s_oa09,
       [("夺取棱镜实验室","Secure Prism lab",True),("摧毁苏军指挥部","Destroy Soviet HQ",True)],
       [trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="CompleteObj",A0=0,Msg="实验室夺回！",MsgEn="Lab secured!"),
        trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="BattleLab",Act="CompleteObj",A0=0,Msg="仿制设施摧毁！",MsgEn="Copy lab destroyed!"),
        trig(Cond="PlayerBldLost",C0=1,BType="BattleLab",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="完成！",MsgEn="Done!")])

    def s_oa10(ml):
        pmcv(ml,14,78,"Allies")
        ml += ["bld 0 BattleLab 22 70","bld 0 PrismTower 18 68","bld 0 Pillbox 26 72",
               "unit 0 MirageTank 28 76","unit 0 MirageTank 30 74"]
        ebase(ml,74,12,"Soviet",extras=["TeslaCoil"],guards=["Conscript","Rhino","Apocalypse","Kirov"])
    oa("oa10",9,"幻影","Mirage","黑森林：保护爱因斯坦实验室，歼灭苏军。",
       "Black Forest: protect Einstein lab, destroy Soviets.","Germany",13000,"forest",s_oa10,
       [("摧毁苏军指挥部","Destroy Soviet HQ",True),("实验室必须存活","Lab must survive",False)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="苏军击退！",MsgEn="Repelled!"),
        trig(Cond="Time",C0=1800,Act="Reinforce",A0=1,A1=0,A2=40,A3=40,Units="Rhino,Rhino",Once="yes"),
        trig(Cond="Time",C0=4200,Act="Reinforce",A0=1,A1=1,A2=50,A3=50,Units="Apocalypse,Kirov",Once="yes")],
       protect="BattleLab")

    def s_oa11(ml):
        pmcv(ml,14,18,"Allies"); ml += ["bld 0 ChronoSphere 20 22","bld 1 NukeSilo 70 14","bld 1 NukeSilo 78 16","bld 1 NukeSilo 74 22"]
        ebase(ml,72,60,"Soviet"); ml += ["bld 1 NavalYard 80 70","unit 1 Dreadnought 84 74 guard"]
    oa("oa11",10,"辐射尘","Fallout","佛罗里达：守住超时空仪，摧毁全部核弹井。",
       "Florida: protect Chronosphere, destroy all Nuke Silos.","America",14000,"island",s_oa11,
       [("摧毁全部核弹井","Destroy all Nuke Silos",True),("超时空仪存活","Chronosphere lives",False)],
       [trig(Cond="PlayerBldLost",C0=1,BType="NukeSilo",Act="CompleteObj",A0=0,Msg="核威胁解除！",MsgEn="Nukes cleared!"),
        trig(Cond="Time",C0=3000,Act="Reinforce",A0=1,A1=1,A2=74,A3=20,Units="Rhino,Kirov",Once="yes")],
       protect="ChronoSphere")

    def s_oa12(ml):
        psquad(ml,12,80,["Tanya","Chrono","Chrono","GI","GI","PrismTank","IFV","Engineer","NavySEAL"])
        ebase(ml,70,18,"Soviet",extras=["TeslaCoil","IronCurtain","FlakCannon"],
              guards=["Conscript","TeslaTrooper","Apocalypse","Apocalypse","Kirov"])
        ml += ["bld 1 BattleBunker 64 30","unit 1 Apocalypse 66 28 guard"]
    oa("oa12",11,"超时空风暴","Chrono Storm","莫斯科：摧毁克里姆林宫指挥部与铁幕。谭雅存活。",
       "Moscow: destroy Kremlin HQ and Iron Curtain. Tanya lives.","America",15000,"city",s_oa12,
       [("摧毁克里姆林宫指挥部","Destroy Kremlin HQ",True),("摧毁铁幕装置","Destroy Iron Curtain",True),("谭雅存活","Tanya lives",False)],
       [trig(Cond="Always",Act="Reinforce",A0=0,A1=2,A2=20,A3=70,Units="GI,PrismTank,Chrono",Msg="超时空增援！",MsgEn="Chrono reinforce!"),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="指挥部摧毁！",MsgEn="HQ down!"),
        trig(Cond="PlayerBldLost",C0=1,BType="IronCurtain",Act="CompleteObj",A0=1,Msg="铁幕摧毁！",MsgEn="Curtain down!"),
        trig(Cond="Time",C0=2000,Act="Reinforce",A0=1,A1=0,A2=60,A3=40,Units="Apocalypse,TeslaTrooper",Once="yes")],
       hero="Tanya")

    # ---- OS Soviet 12 ----
    def os(key, idx, name, name_en, brief, brief_en, country, money, theme, setup, objs, trigs, ab=SB, au=SU, timer=None, art="", hero=None, protect=None, ai="Allies"):
        nonlocal n
        ml = base_map(f"{key} {name_en}", theme=theme)
        setup(ml)
        emit(key,"official", dict(
            name=name, name_en=name_en, brief=brief, brief_en=brief_en,
            faction="Soviet", ai=ai, country=country, money=money, line="os", idx=idx, track=1,
            ab=ab, au=au, timer=timer, art=art,
            objs=objs, trigs=boot(brief, brief_en, timer=timer)+std_fail(hero, protect)+trigs
        ), ml); n+=1

    def s_os01(ml):
        psquad(ml,12,80,["Conscript","Conscript","Conscript","Rhino","Engineer","FlakTrooper"])
        ebase(ml,70,20,"Allies",extras=[],guards=["GI","GI","Grizzly","IFV"]); ml.append("bld -1 CivHouse 48 48")
    os("os01",0,"红色黎明","Red Dawn","华盛顿：摧毁五角大楼盟军指挥部。",
       "Washington: destroy the Pentagon HQ.","Russia",8000,"city",s_os01,
       [("摧毁五角大楼指挥部","Destroy Pentagon HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="五角大楼陷落！",MsgEn="Pentagon fallen!"),
        trig(Cond="Time",C0=1800,Act="Reinforce",A0=1,A1=1,A2=50,A3=50,Units="GI,Grizzly",Once="yes")],
       ab=SBE, au=SUE)

    def s_os02(ml):
        pmcv(ml,14,70,"Soviet"); ml += ["unit 0 AmphTransport 20 55","unit 0 Rhino 18 72"]
        ebase(ml,70,16,"Allies")
    os("os02",1,"敌对海岸","Hostile Shore","佛罗里达：建立立足点并摧毁盟军指挥部。",
       "Florida: beachhead and destroy Allied HQ.","Russia",10000,"beach",s_os02,
       [("摧毁盟军指挥部","Destroy Allied HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="立足点巩固！",MsgEn="Beachhead secured!"),
        trig(Cond="Time",C0=2400,Act="Reinforce",A0=1,A1=0,A2=40,A3=20,Units="GI,Grizzly,IFV",Once="yes")])

    def s_os03(ml):
        pmcv(ml,12,80,"Soviet"); ml += ["bld 1 BattleLab 48 44","bld -1 OilDerrick 40 50"]
        ebase(ml,76,12,"Allies")
    os("os03",2,"大苹果","Big Apple","纽约：占领美国Battle Lab并摧毁盟军指挥部。",
       "NY: capture Battle Lab and destroy Allied HQ.","Russia",11000,"city",s_os03,
       [("占领盟军实验室","Capture Allied Battle Lab",True),("摧毁盟军指挥部","Destroy Allied HQ",True)],
       [trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="CompleteObj",A0=0,Msg="实验室已夺！",MsgEn="Lab captured!"),
        trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="纽约沦陷！",MsgEn="NY fallen!")])

    def s_os04(ml):
        pmcv(ml,14,78,"Soviet"); ml += ["bld 0 TeslaCoil 20 70","bld 0 SentryGun 24 72"]
        ebase(ml,74,12,"Allies",guards=["GI","Grizzly","PrismTank","IFV"])
    os("os04",3,"祖国前线","Home Front","符拉迪沃斯托克：保卫本土，摧毁入侵盟军。",
       "Vladivostok: defend homeland, destroy Allied invaders.","Russia",12000,"plains",s_os04,
       [("摧毁盟军指挥部","Destroy Allied HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="入侵击退！",MsgEn="Invasion broken!"),
        trig(Cond="Time",C0=2000,Act="Reinforce",A0=1,A1=0,A2=50,A3=40,Units="GI,Grizzly,Chrono",Once="yes"),
        trig(Cond="Time",C0=4500,Act="Reinforce",A0=1,A1=1,A2=60,A3=50,Units="PrismTank,Rocketeer",Once="yes")])

    def s_os05(ml):
        pmcv(ml,12,80,"Soviet")
        ml += ["bld 0 TeslaCoil 48 40","bld 0 TeslaCoil 52 42","bld 0 TeslaCoil 46 44","unit 0 TeslaTrooper 50 46",
               "unit 0 TeslaTrooper 48 48"]  # Eiffel charge proxy
        ebase(ml,74,14,"Allies",guards=["GI","Grizzly","PrismTank","IFV","MirageTank"])
    os("os05",4,"光之城","City of Lights","巴黎：保卫铁塔特斯拉充能节点，摧毁盟军指挥部。",
       "Paris: protect Tesla charge node, destroy Allied HQ.","Russia",12000,"city",s_os05,
       [("摧毁盟军指挥部","Destroy Allied HQ",True),("特斯拉充能节点存活","Tesla node lives",False)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="巴黎守住！",MsgEn="Paris held!"),
        trig(Cond="Time",C0=2400,Act="Reinforce",A0=1,A1=1,A2=48,A3=48,Units="PrismTank,GI,GI",Once="yes")],
       protect="TeslaCoil")

    def s_os06(ml):
        pmcv(ml,16,20,"Soviet"); ml += ["bld 0 NavalYard 22 28","unit 0 Typhoon 28 34","unit 0 Dreadnought 32 36",
               "bld 1 NavalYard 70 68","unit 1 Destroyer 76 72 guard","unit 1 Aegis 72 74 guard","unit 1 AircraftCarrier 80 70 guard"]
        ebase(ml,72,50,"Allies")
    os("os06",5,"潜艇分割","Sub-Divide","珍珠港：摧毁盟军海军船坞。",
       "Pearl Harbor: destroy Allied Naval Yard.","Russia",12500,"island",s_os06,
       [("摧毁盟军船坞","Destroy Allied Naval Yard",True),("摧毁盟军指挥部","Destroy Allied HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="NavalYard",Act="CompleteObj",A0=0,Msg="船坞摧毁！",MsgEn="Yard destroyed!"),
        trig(Cond="PlayerBldLost",C0=1,BType="NavalYard",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="海军肃清！",MsgEn="Navy cleared!")])

    def s_os07(ml):
        pmcv(ml,14,78,"Soviet"); ml += ["bld 0 BattleLab 22 70","bld 0 TeslaCoil 18 68","bld 0 FlakCannon 26 72"]
        ebase(ml,74,12,"Allies",guards=["GI","Chrono","Chrono","PrismTank","IFV"])
    os("os07",6,"超时空防御","Chrono Defense","乌拉尔：保卫Battle Lab，摧毁盟军突袭指挥部。",
       "Urals: protect Battle Lab, destroy Allied raid HQ.","Russia",13000,"forest",s_os07,
       [("摧毁盟军指挥部","Destroy Allied HQ",True),("Battle Lab存活","Battle Lab lives",False)],
       [trig(Cond="Time",C0=1200,Act="Reinforce",A0=1,A1=2,A2=40,A3=40,Units="Chrono,Chrono,GI",Once="yes",Msg="超时空突袭！",MsgEn="Chrono raid!"),
        trig(Cond="Time",C0=3000,Act="Reinforce",A0=1,A1=0,A2=50,A3=50,Units="PrismTank,Chrono",Once="yes"),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="突袭粉碎！",MsgEn="Raid crushed!")],
       protect="BattleLab")

    def s_os08(ml):
        pmcv(ml,12,80,"Soviet"); ml += ["bld 1 BattleLab 48 44"]  # White House proxy capturable
        ebase(ml,76,12,"Allies")
    os("os08",7,"亵渎","Desecration","华盛顿：占领白宫（实验室标点）并摧毁盟军残部。",
       "Washington: capture White House site and destroy Allied HQ.","Russia",13000,"city",s_os08,
       [("占领白宫要点","Capture White House site",True),("摧毁盟军指挥部","Destroy Allied HQ",True)],
       [trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="CompleteObj",A0=0,Msg="白宫已占！",MsgEn="White House taken!"),
        trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="完成！",MsgEn="Done!")])

    def s_os09(ml):
        pmcv(ml,12,80,"Soviet"); ml += ["bld 1 PsychicBeacon 48 46","bld 1 PsychicSensor 52 50"]
        ebase(ml,74,14,"Yuri",guards=["Initiate","LasherTank","GatlingTank","Brute"])
    os("os09",8,"狐与猎犬","The Fox and the Hound","圣安东尼奥：摧毁尤里心灵信标节点。",
       "San Antonio: destroy Yuri Psychic Beacon.","Russia",13500,"desert",s_os09,
       [("摧毁心灵信标","Destroy Psychic Beacon",True),("摧毁尤里指挥部","Destroy Yuri HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="CompleteObj",A0=0,Msg="信标摧毁！",MsgEn="Beacon destroyed!"),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="尤里溃败！",MsgEn="Yuri broken!")],
       ai="Yuri", art="assets/sprites/bld_psychicbeacon.png")

    def s_os10(ml):
        pmcv(ml,14,70,"Soviet"); ml += ["bld 1 WeatherDevice 70 20","bld 1 BattleLab 66 28"]
        ebase(ml,72,50,"Allies",extras=["PatriotMissile"],guards=["GI","Grizzly","PrismTank","Aegis"])
        ml += ["bld 1 NavalYard 80 70","unit 1 Destroyer 84 74 guard"]
    os("os10",9,"风雨同盟","Weathered Alliance","维尔京群岛：摧毁盟军天气控制器。",
       "Virgin Islands: destroy Weather Control Device.","Russia",14000,"island",s_os10,
       [("摧毁天气控制器","Destroy Weather Device",True)],
       [trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="RevealMap",A0=0,A1=70,A2=20,A3=16,Msg="定位完成！",MsgEn="Located!"),
        trig(Cond="PlayerBldLost",C0=1,BType="WeatherDevice",Act="CompleteObj",A0=0,Msg="天气控制器摧毁！",MsgEn="Weather Device down!"),
        trig(Cond="Time",C0=3000,Act="Reinforce",A0=1,A1=1,A2=70,A3=30,Units="PrismTank,GI",Once="yes")])

    def s_os11(ml):
        psquad(ml,12,80,["Boris","Conscript","Conscript","Rhino","Engineer","TeslaTrooper"])
        ebase(ml,70,18,"Yuri",extras=["PsychicTower","GatlingCannon"],guards=["Initiate","Yuri","LasherTank","MasterMind"])
    os("os11",10,"红色革命","Red Revolution","莫斯科：摧毁尤里克里姆林宫总部。鲍里斯存活。",
       "Moscow: destroy Yuri Kremlin HQ. Boris lives.","Russia",14500,"city",s_os11,
       [("摧毁尤里总部","Destroy Yuri HQ",True),("鲍里斯存活","Boris lives",False)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="尤里总部摧毁！",MsgEn="Yuri HQ destroyed!"),
        trig(Cond="Time",C0=2400,Act="Reinforce",A0=1,A1=1,A2=50,A3=40,Units="Initiate,LasherTank,Yuri",Once="yes")],
       ai="Yuri", hero="Boris")

    def s_os12(ml):
        pmcv(ml,14,78,"Soviet"); ml += ["bld 1 ChronoSphere 70 20","bld 1 BattleLab 66 28","bld 1 PrismTower 74 24"]
        ebase(ml,72,55,"Allies",guards=["GI","PrismTank","Chrono","MirageTank","IFV"])
    os("os12",11,"极地风暴","Polar Storm","阿拉斯加：摧毁盟军超时空仪。",
       "Alaska: destroy Allied Chronosphere.","Russia",15000,"plains",s_os12,
       [("摧毁超时空仪","Destroy Chronosphere",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ChronoSphere",Act="CompleteObj",A0=0,Msg="超时空仪摧毁！",MsgEn="Chronosphere destroyed!"),
        trig(Cond="Time",C0=2400,Act="Reinforce",A0=1,A1=0,A2=60,A3=40,Units="Chrono,PrismTank",Once="yes")],
       art="assets/sprites/bld_timemachine.png")

    # ---- YA YR Allied 7 ----
    def ya(key, idx, name, name_en, brief, brief_en, country, money, theme, setup, objs, trigs, ab=AB, au=AU, timer=None, art="", hero=None, protect=None):
        nonlocal n
        ml = base_map(f"{key} {name_en}", theme=theme)
        setup(ml)
        emit(key,"official", dict(
            name=name, name_en=name_en, brief=brief, brief_en=brief_en,
            faction="Allies", ai="Yuri", country=country, money=money, line="ya", idx=idx, track=1,
            ab=ab, au=au, timer=timer, art=art,
            objs=objs, trigs=boot(brief, brief_en, timer=timer)+std_fail(hero, protect)+trigs
        ), ml); n+=1

    def s_ya01(ml):
        pmcv(ml,12,80,"Allies")
        ml += ["bld 0 TimeMachine 24 72","bld -1 TechPowerPlant 28 76","bld -1 TechPowerPlant 34 76",
               "bld -1 TechPowerPlant 40 70","bld 1 PsychicDominator 78 55"]
        ebase(ml,78,12,"Yuri"); ml += ["unit 1 Initiate 74 55 guard","unit 1 GatlingTank 80 58 guard"]
    ya("ya01",0,"时光倒流","Time Lapse","旧金山：占领2座民用电厂启动时间机器，保卫它，摧毁恶魔岛统治仪。",
       "SF: capture 2 TechPowerPlants, protect Time Machine, destroy Dominator.","America",12000,"city",s_ya01,
       [("占领民用电厂","Capture civilian power",True),("摧毁心灵统治仪","Destroy Dominator",True),("时间机器存活","Time Machine lives",False)],
       [trig(Cond="BldCaptured",C0=0,BType="TechPowerPlant",C2=2,Act="CompleteObj",A0=0,Msg="电厂接入！",MsgEn="Power online!"),
        trig(Cond="BldCaptured",C0=0,BType="TechPowerPlant",C2=2,Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicDominator",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="统治仪摧毁！",MsgEn="Dominator down!"),
        trig(Cond="Time",C0=2400,Act="Reinforce",A0=1,A1=1,A2=70,A3=50,Units="Initiate,LasherTank",Once="yes")],
       protect="TimeMachine", art="assets/sprites/bld_timemachine.png")

    def s_ya02(ml):
        pmcv(ml,12,80,"Allies"); ml += ["bld 1 Grinder 40 40","bld 1 Grinder 52 44","bld 1 Grinder 46 52"]
        ebase(ml,76,12,"Yuri")
    ya("ya02",1,"好莱坞虚荣","Hollywood and Vain","洛杉矶：摧毁全部粉碎机与尤里指挥部。",
       "LA: destroy all Grinders and Yuri HQ.","America",12500,"city",s_ya02,
       [("摧毁粉碎机","Destroy Grinders",True),("摧毁尤里指挥部","Destroy Yuri HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="Grinder",Act="CompleteObj",A0=0,Msg="粉碎机清除！",MsgEn="Grinders cleared!"),
        trig(Cond="PlayerBldLost",C0=1,BType="Grinder",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="好莱坞肃清！",MsgEn="Hollywood cleared!")])

    def s_ya03(ml):
        pmcv(ml,12,80,"Allies"); ml += ["bld 1 NukeSilo 70 14","bld 1 NukeSilo 78 16","bld -1 CivHouse 48 48"]
        ebase(ml,74,50,"Yuri")
    ya("ya03",2,"电力游戏","Power Play","西雅图：摧毁尤里核弹井并解放园区。",
       "Seattle: destroy Yuri Nuke Silos and liberate campus.","America",13000,"city",s_ya03,
       [("摧毁核弹井","Destroy Nuke Silos",True),("摧毁尤里指挥部","Destroy Yuri HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="NukeSilo",Act="CompleteObj",A0=0,Msg="井已毁！",MsgEn="Silos down!"),
        trig(Cond="PlayerBldLost",C0=1,BType="NukeSilo",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="园区解放！",MsgEn="Campus free!")])

    def s_ya04(ml):
        pmcv(ml,12,80,"Allies")
        ml += ["bld 1 BioReactor 40 40","bld 1 BioReactor 48 36","bld 1 BioReactor 44 48","bld -1 CivHouse 52 44"]
        ebase(ml,74,14,"Yuri")
    ya("ya04",3,"古墓奇兵","Tomb Raided","埃及：摧毁金字塔周围生物反应堆并拔除尤里基地。",
       "Egypt: destroy Bio Reactors and Yuri base.","America",13000,"desert",s_ya04,
       [("摧毁生物反应堆","Destroy Bio Reactors",True),("摧毁尤里指挥部","Destroy Yuri HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="BioReactor",Act="CompleteObj",A0=0,Msg="反应堆摧毁！",MsgEn="Reactors down!"),
        trig(Cond="PlayerBldLost",C0=1,BType="BioReactor",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="基地清除！",MsgEn="Base cleared!")])

    def s_ya05(ml):
        pmcv(ml,12,80,"Allies"); ml += ["bld 1 CloningVat 48 40","bld 1 CloningVat 54 46"]
        ebase(ml,74,14,"Yuri")
    ya("ya05",4,"南方克隆","Clones Down Under","悉尼：摧毁克隆缸并肃清尤里。",
       "Sydney: destroy Cloning Vats and Yuri HQ.","America",13500,"city",s_ya05,
       [("摧毁克隆缸","Destroy Cloning Vats",True),("摧毁尤里指挥部","Destroy Yuri HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="CloningVat",Act="CompleteObj",A0=0,Msg="克隆缸摧毁！",MsgEn="Vats destroyed!"),
        trig(Cond="PlayerBldLost",C0=1,BType="CloningVat",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="悉尼肃清！",MsgEn="Sydney cleared!")])

    def s_ya06(ml):
        pmcv(ml,12,80,"Allies"); ml += ["bld 0 BattleLab 22 70","bld 0 Pillbox 18 68","bld 0 Pillbox 26 72"]  # Parliament proxy
        ebase(ml,74,14,"Yuri",guards=["Initiate","Yuri","MasterMind","LasherTank"])
    ya("ya06",5,"条约诡计","Trick or Treaty","伦敦：保卫议会，摧毁尤里基地。",
       "London: protect Parliament, destroy Yuri base.","UK",14000,"city",s_ya06,
       [("摧毁尤里指挥部","Destroy Yuri HQ",True),("议会必须存活","Parliament must survive",False)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="议会得救！",MsgEn="Parliament saved!"),
        trig(Cond="Time",C0=2400,Act="Reinforce",A0=1,A1=1,A2=50,A3=40,Units="Initiate,Brute,Yuri",Once="yes")],
       protect="BattleLab")

    def s_ya07(ml):
        pmcv(ml,12,80,"Allies"); ml += ["bld 0 Radar 20 74","bld 1 PsychicDominator 70 40"]  # Soviet radar story beat
        ebase(ml,74,14,"Yuri",extras=["PsychicTower"],guards=["Initiate","YuriPrime","MasterMind","FloatingDisc"])
    ya("ya07",6,"脑死亡","Brain Dead","南极：摧毁心灵统治仪并歼灭尤里。",
       "Antarctica: destroy Psychic Dominator and eliminate Yuri.","America",15000,"plains",s_ya07,
       [("摧毁心灵统治仪","Destroy Dominator",True),("摧毁尤里指挥部","Destroy Yuri HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="PsychicDominator",Act="CompleteObj",A0=0,Msg="统治仪摧毁！",MsgEn="Dominator destroyed!"),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicDominator",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="尤里覆灭！",MsgEn="Yuri ended!")],
       art="assets/sprites/bld_psychicamplifier.png")

    # ---- YS YR Soviet 7 ----
    def ys(key, idx, name, name_en, brief, brief_en, country, money, theme, setup, objs, trigs, ab=SB, au=SU, art="", hero=None, protect=None, ai="Yuri"):
        nonlocal n
        ml = base_map(f"{key} {name_en}", theme=theme)
        setup(ml)
        emit(key,"official", dict(
            name=name, name_en=name_en, brief=brief, brief_en=brief_en,
            faction="Soviet", ai=ai, country=country, money=money, line="ys", idx=idx, track=1,
            ab=ab, au=au, art=art,
            objs=objs, trigs=boot(brief, brief_en)+std_fail(hero, protect)+trigs
        ), ml); n+=1

    def s_ys01(ml):
        pmcv(ml,12,80,"Soviet")
        ml += ["bld 1 TimeMachine 48 40","bld 1 GrandCannon 44 36","bld 1 NavalYard 70 70",
               "unit 1 Destroyer 76 74 guard","bld -1 TechPowerPlant 30 70","bld -1 TechPowerPlant 36 72",
               "bld -1 TechPowerPlant 42 68","bld -1 TechPowerPlant 34 66","bld 1 PsychicDominator 78 50"]
        ebase(ml,74,12,"Yuri")
    ys("ys01",0,"时间偏移","Time Shift","旧金山：夺取时间机器，占领电厂，摧毁统治仪。",
       "SF: seize Time Machine, capture power, destroy Dominator.","Russia",12000,"city",s_ys01,
       [("占领时间机器","Capture Time Machine",True),("摧毁心灵统治仪","Destroy Dominator",True)],
       [trig(Cond="BldCaptured",C0=0,BType="TimeMachine",C2=1,Act="CompleteObj",A0=0,Msg="时间机器到手！",MsgEn="Time Machine seized!"),
        trig(Cond="BldCaptured",C0=0,BType="TimeMachine",C2=1,Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicDominator",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="统治仪摧毁！",MsgEn="Dominator down!")],
       art="assets/sprites/bld_timemachine.png")

    def s_ys02(ml):
        pmcv(ml,12,80,"Soviet"); ml += ["bld 1 ChronoSphere 70 20","bld 1 BattleLab 66 28"]
        ebase(ml,72,55,"Allies",guards=["GI","PrismTank","MirageTank","Chrono"])
    ys("ys02",1,"似曾相识","Deja Vu","黑森林：摧毁爱因斯坦实验室与超时空仪。",
       "Black Forest: destroy Einstein lab and Chronosphere.","Russia",12500,"forest",s_ys02,
       [("摧毁超时空仪","Destroy Chronosphere",True),("摧毁实验室","Destroy Battle Lab",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ChronoSphere",Act="CompleteObj",A0=0,Msg="超时空仪摧毁！",MsgEn="Chronosphere down!"),
        trig(Cond="PlayerBldLost",C0=1,BType="BattleLab",Act="CompleteObj",A0=1,Msg="实验室摧毁！",MsgEn="Lab destroyed!")],
       ai="Allies")

    def s_ys03(ml):
        pmcv(ml,12,80,"Soviet"); ml += ["bld 1 PsychicBeacon 40 40","bld 1 PsychicDominator 70 40"]
        ebase(ml,74,14,"Yuri")
    ys("ys03",2,"洗脑","Brain Wash","伦敦：摧毁心灵信标与统治仪。",
       "London: destroy Beacon and Dominator.","Russia",13000,"city",s_ys03,
       [("摧毁心灵信标","Destroy Beacon",True),("摧毁心灵统治仪","Destroy Dominator",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="CompleteObj",A0=0,Msg="信标摧毁！",MsgEn="Beacon down!"),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicBeacon",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="PsychicDominator",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="统治仪摧毁！",MsgEn="Dominator down!")],
       art="assets/sprites/bld_psychicbeacon.png")

    def s_ys04(ml):
        psquad(ml,12,80,["Boris","Conscript","Conscript","Rhino","Engineer"])
        ml += ["bld -1 TechAirport 48 48"]  # airfield escort target area
        ebase(ml,74,14,"Yuri",guards=["Initiate","LasherTank","GatlingTank","Yuri"])
    ys("ys04",3,"逃亡的罗曼诺夫","Romanov on the Run","摩洛哥：护送鲍里斯（罗曼诺夫）并摧毁尤里机场基地。",
       "Morocco: escort Boris/Romanov, destroy Yuri airfield base.","Russia",13000,"desert",s_ys04,
       [("摧毁尤里机场基地","Destroy Yuri airfield base",True),
        ("UnitInRect会合机场", "Reach the airfield", True),("鲍里斯存活","Boris lives",False)],
       [trig(Cond="UnitInRect",C0=0,C1=44,C2=44,C3=56,C4=56,Act="CompleteObj",A0=1,Msg="抵达机场！",MsgEn="Airfield reached!"),
        trig(Cond="UnitInRect",C0=0,C1=44,C2=44,C3=56,C4=56,Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,RequiresPhase=2,Msg="护送成功！",MsgEn="Escort success!")],
       hero="Boris")

    def s_ys05(ml):
        pmcv(ml,16,20,"Soviet"); ml += ["bld 1 NavalYard 70 70","unit 1 Boomer 76 74 guard","unit 1 Boomer 72 78 guard"]
        ebase(ml,72,50,"Yuri")
    ys("ys05",4,"逃逸速度","Escape Velocity","太平洋岛：摧毁尤里潜艇船坞。",
       "Pacific isle: destroy Yuri submarine pens.","Russia",13500,"island",s_ys05,
       [("摧毁潜艇船坞","Destroy sub pens",True),("摧毁尤里指挥部","Destroy Yuri HQ",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="NavalYard",Act="CompleteObj",A0=0,Msg="船坞摧毁！",MsgEn="Pens destroyed!"),
        trig(Cond="PlayerBldLost",C0=1,BType="NavalYard",Act="SetPhase",A0=2),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="岛屿肃清！",MsgEn="Isle cleared!")])

    def s_ys06(ml):
        pmcv(ml,12,80,"Soviet"); ml += ["deco rock1 20 20 60 60 20"]
        ebase(ml,70,18,"Yuri",extras=["PsychicSensor"],guards=["Initiate","FloatingDisc","MasterMind","LasherTank"])
    ys("ys06",5,"奔向月球","To the Moon","月球：摧毁月球指挥中心。",
       "The Moon: destroy lunar command center.","Russia",14000,"desert",s_ys06,
       [("摧毁月球指挥中心","Destroy lunar command",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="月球基地摧毁！",MsgEn="Lunar base destroyed!"),
        trig(Cond="Time",C0=3000,Act="Reinforce",A0=1,A1=1,A2=50,A3=40,Units="FloatingDisc,Initiate",Once="yes")])

    def s_ys07(ml):
        pmcv(ml,12,80,"Soviet")
        ebase(ml,70,18,"Yuri",extras=["PsychicDominator","CloningVat","Grinder"],
              guards=["Initiate","YuriPrime","MasterMind","Brute","LasherTank"])
    ys("ys07",6,"头脑游戏","Head Games","特兰西瓦尼亚：摧毁尤里要塞。",
       "Transylvania: destroy Yuri's fortress.","Russia",15000,"forest",s_ys07,
       [("摧毁尤里要塞","Destroy Yuri fortress",True),("摧毁心灵统治仪","Destroy Dominator",True)],
       [trig(Cond="PlayerBldLost",C0=1,BType="PsychicDominator",Act="CompleteObj",A0=1,Msg="统治仪摧毁！",MsgEn="Dominator down!"),
        trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="要塞陷落！",MsgEn="Fortress fallen!")])

    # ---- Fusion 32 ----
    fusion = [
        (1,"fc",0,"边境冲突","Border Skirmish","击退越境苏军并摧毁指挥部。","Repel Soviets and destroy HQ.","China","Soviet","China","plains","ConYard",None,9000),
        (2,"fc",1,"防线加固","Fortify the Line","建立防线摧毁苏军前哨。","Fortify and destroy Soviet outpost.","China","Soviet","China","plains","ConYard",None,9500),
        (3,"fc",2,"矿区争夺","Ore Contest","夺取矿区摧毁敌军指挥部。","Secure ore and destroy enemy HQ.","China","Soviet","China","desert","ConYard",None,10000),
        (4,"fc",3,"夜袭","Night Raid","夜袭摧毁苏军雷达与指挥部。","Night raid: destroy Radar and HQ.","China","Soviet","China","forest","Radar",None,10000),
        (5,"fc",4,"港口封锁","Harbor Blockade","封锁港口摧毁苏军船坞。","Blockade harbor; destroy Naval Yard.","China","Soviet","China","island","NavalYard",None,10500),
        (6,"fc",5,"科技夺取","Tech Seize","占领敌方实验室并摧毁指挥部。","Capture enemy lab and destroy HQ.","China","Soviet","China","city","ConYard",None,11000),
        (7,"fc",6,"决战高原","Plateau Showdown","高原决战摧毁苏军主基地。","Plateau showdown: destroy main base.","China","Soviet","China","plains","ConYard",None,12000),
        (8,"fc",7,"龙之反击","Dragon Counter","全面反击拔除苏军最后指挥部。","Full counterattack — last Soviet HQ.","China","Soviet","China","plains","ConYard",None,13000),
        (9,"fa",0,"盟军集结","Allied Muster","集结盟军摧毁苏军入侵指挥部。","Muster Allies; destroy Soviet HQ.","Allies","Soviet","America","plains","ConYard","Tanya",9000),
        (10,"fa",1,"海岸防御","Coast Guard","保卫海岸摧毁苏军登陆指挥部。","Defend coast; destroy landing HQ.","Allies","Soviet","America","island","ConYard",None,9500),
        (11,"fa",2,"城市解放","City Liberate","摧毁心灵信标解放城市。","Destroy Beacon to free the city.","Allies","Soviet","America","city","PsychicBeacon",None,10000),
        (12,"fa",3,"实验室护送","Lab Escort","保护实验室并摧毁敌军。","Protect lab and destroy enemy HQ.","Allies","Soviet","Germany","forest","ConYard",None,10500),
        (13,"fa",4,"海战演习","Naval Drill","摧毁敌军船坞。","Destroy enemy Naval Yard.","Allies","Soviet","America","island","NavalYard",None,11000),
        (14,"fa",5,"棱镜计划","Prism Protocol","摧毁苏军仿制棱镜设施。","Destroy Soviet Prism reverse-eng.","Allies","Soviet","France","plains","BattleLab",None,11500),
        (15,"fa",6,"超时空前夜","Chrono Eve","摧毁核弹井为超时空铺路。","Destroy Nuke Silos before Chrono.","Allies","Soviet","America","island","NukeSilo",None,12000),
        (16,"fa",7,"莫斯科突袭","Moscow Strike","突袭莫斯科指挥部。","Strike Moscow HQ.","Allies","Soviet","America","city","ConYard","Tanya",14000),
        (17,"fs",0,"红色推进","Red Advance","推进摧毁盟军前哨。","Advance; destroy Allied outpost.","Soviet","Allies","Russia","plains","ConYard",None,9000),
        (18,"fs",1,"抢滩","Beachhead","抢滩建立基地摧毁盟军。","Beachhead and destroy Allies.","Soviet","Allies","Russia","island","ConYard",None,9500),
        (19,"fs",2,"占领实验室","Seize Lab","占领盟军Battle Lab。","Capture Allied Battle Lab.","Soviet","Allies","Russia","city","ConYard",None,10000),
        (20,"fs",3,"铁幕试炼","Iron Trial","摧毁盟军指挥部。","Destroy Allied HQ.","Soviet","Allies","Russia","plains","ConYard",None,10500),
        (21,"fs",4,"海狼","Sea Wolf","摧毁盟军海军。","Destroy Allied navy.","Soviet","Allies","Russia","island","NavalYard",None,11000),
        (22,"fs",5,"白宫行动","White House Op","占领白宫要点。","Capture White House site.","Soviet","Allies","Russia","city","ConYard","Boris",12000),
        (23,"fs",6,"内战火花","Civil Spark","摧毁叛军尤里节点。","Destroy rebel Yuri node.","Soviet","Yuri","Russia","city","PsychicBeacon",None,12500),
        (24,"fs",7,"极地终局","Polar End","摧毁超时空仪。","Destroy Chronosphere.","Soviet","Allies","Russia","plains","ChronoSphere",None,14000),
        (25,"fy",0,"心灵觉醒","Psi Awaken","建立尤里力量摧毁盟军前哨。","Raise Yuri; destroy Allied outpost.","Yuri","Allies","Yuri","plains","ConYard",None,9000),
        (26,"fy",1,"信标部署","Beacon Deploy","部署攻势摧毁敌军指挥部。","Push and destroy enemy HQ.","Yuri","Allies","Yuri","city","ConYard",None,9500),
        (27,"fy",2,"粉碎机","The Grinder","扩张并摧毁敌军指挥部。","Expand and destroy enemy HQ.","Yuri","Soviet","Yuri","city","ConYard",None,10000),
        (28,"fy",3,"病毒扩散","Virus Spread","摧毁敌军实验室。","Destroy enemy lab.","Yuri","Allies","Yuri","forest","BattleLab",None,10500),
        (29,"fy",4,"飞碟阴影","Disc Shadow","摧毁盟军空指部。","Destroy Allied Air Force Cmd.","Yuri","Allies","Yuri","plains","AirForceCmd",None,11000),
        (30,"fy",5,"克隆计划","Clone Plan","保护行动摧毁敌军。","Protect ops; destroy enemy HQ.","Yuri","Soviet","Yuri","city","ConYard",None,12000),
        (31,"fy",6,"统治仪","Dominator","摧毁敌军指挥部巩固统治。","Destroy enemy HQ to secure Dominator ops.","Yuri","Allies","Yuri","desert","ConYard",None,13000),
        (32,"fy",7,"全球心控","Global Psi","终局摧毁最后抵抗指挥部。","Finale: destroy last resistance HQ.","Yuri","Allies","Yuri","city","ConYard","Yuri",15000),
    ]
    for idx,line,li,name,name_en,brief,brief_en,fac,ai,country,theme,destroy,hero,money in fusion:
        key=f"mission{idx:02d}"
        size = 80 if idx<=8 else 96
        ml = base_map(f"{key} {name_en}", size=size, theme=theme)
        pmcv(ml, 12, size-16, fac)
        if hero: ml.append(f"unit 0 {hero} 14 {size-18}")
        if destroy == "PsychicBeacon": ml.append("bld 1 PsychicBeacon 48 48")
        if destroy == "ChronoSphere": ml.append("bld 1 ChronoSphere 70 30")
        if destroy == "NukeSilo": ml += ["bld 1 NukeSilo 70 14","bld 1 NukeSilo 78 16"]
        if destroy in ("BattleLab","Radar","NavalYard","AirForceCmd") and destroy != "ConYard":
            ml.append(f"bld 1 {destroy} 48 40")
        if line=="fc" and idx==6:  # tech seize
            ml.append("bld 1 BattleLab 48 40")
        if line=="fs" and idx==19:
            ml.append("bld 1 BattleLab 48 40")
        if line=="fs" and idx==22:
            ml.append("bld 1 BattleLab 48 44")
        if line=="fa" and idx==12:
            ml += ["bld 0 BattleLab 22 70","bld 0 Pillbox 18 68"]
        ebase(ml, size-22, 12, ai, extras=[] if destroy=="ConYard" else None)
        objs = [(brief[:48], brief_en[:60], True)]
        if hero: objs.append((f"{hero}必须存活", f"{hero} must survive", False))
        if line=="fa" and idx==12:
            objs.append(("实验室必须存活","Lab must survive", False))
        ab = CB if fac=="China" else (AB if fac=="Allies" else (SB if fac=="Soviet" else YB))
        au = CU if fac=="China" else (AU if fac=="Allies" else (SU if fac=="Soviet" else YU))
        art = ""
        if destroy=="PsychicBeacon": art="assets/sprites/bld_psychicbeacon.png"
        if destroy=="ChronoSphere": art="assets/sprites/bld_timemachine.png"
        waves = [(2700,"Conscript,Rhino"),(6300,"Conscript,Rhino,FlakTrack"),(10800,"TeslaTrooper,Rhino,V3Launcher")]
        if ai=="Allies": waves=[(2700,"GI,Grizzly"),(6300,"GI,IFV,Grizzly"),(10800,"PrismTank,Rocketeer")]
        if ai=="Yuri": waves=[(2700,"Initiate,LasherTank"),(6300,"Initiate,Brute,GatlingTank"),(10800,"Yuri,Magnetron")]
        trigs = []
        # phase objectives
        if line=="fc" and idx==6:
            trigs += [trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="CompleteObj",A0=0,Msg="实验室已夺！",MsgEn="Lab captured!"),
                      trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="SetPhase",A0=2),
                      trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="完成！",MsgEn="Done!")]
            objs = [("占领敌方实验室","Capture enemy lab",True),("摧毁敌军指挥部","Destroy enemy HQ",True)]
        elif line=="fs" and idx==19:
            trigs += [trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="CompleteObj",A0=0,Msg="实验室已夺！",MsgEn="Lab captured!"),
                      trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="SetPhase",A0=2),
                      trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2)]
            objs = [("占领盟军实验室","Capture Allied lab",True),("摧毁盟军指挥部","Destroy Allied HQ",True)]
            if hero: objs.append((f"{hero}必须存活",f"{hero} must survive",False))
        elif line=="fs" and idx==22:
            trigs += [trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="CompleteObj",A0=0,Msg="白宫已占！",MsgEn="White House taken!"),
                      trig(Cond="BldCaptured",C0=0,BType="BattleLab",C2=1,Act="SetPhase",A0=2),
                      trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2)]
            objs = [("占领白宫要点","Capture White House",True),("摧毁盟军指挥部","Destroy Allied HQ",True),("鲍里斯存活","Boris lives",False)]
        elif destroy != "ConYard":
            trigs += [trig(Cond="PlayerBldLost",C0=1,BType=destroy,Act="CompleteObj",A0=0,Msg="主要目标摧毁！",MsgEn="Primary destroyed!"),
                      trig(Cond="PlayerBldLost",C0=1,BType=destroy,Act="SetPhase",A0=2),
                      trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,RequiresPhase=2,Msg="敌军肃清！",MsgEn="Enemy cleared!")]
            # dual gate: primary device + HQ — fix objs
            objs = [(brief[:40], brief_en[:50], True), ("摧毁敌军指挥部","Destroy enemy HQ", True)]
            if hero: objs.append((f"{hero}必须存活", f"{hero} must survive", False))
            # Fix CompleteObj indices: destroy target -> 0, ConYard -> 1
            trigs = [trig(Cond="PlayerBldLost",C0=1,BType=destroy,Act="CompleteObj",A0=0,Msg="主要目标摧毁！",MsgEn="Primary destroyed!"),
                     trig(Cond="PlayerBldLost",C0=1,BType=destroy,Act="SetPhase",A0=2),
                     trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=1,RequiresPhase=2,Msg="敌军肃清！",MsgEn="Enemy cleared!")]
        else:
            trigs += [trig(Cond="PlayerBldLost",C0=1,BType="ConYard",Act="CompleteObj",A0=0,Msg="指挥部摧毁！",MsgEn="HQ destroyed!")]
        if line=="fa" and idx==12:
            trigs.append(trig(Cond="PlayerBldLost",C0=0,BType="BattleLab",Act="Lose",Msg="实验室被毁。",MsgEn="Lab destroyed."))
        trigs.append(trig(Cond="Time",C0=3600,Act="Reinforce",A0=1,A1=1,A2=60,A3=40,Units=waves[1][1],Once="yes"))
        protect = "BattleLab" if (line=="fa" and idx==12) else None
        emit(key,"fusion", dict(
            name=name, name_en=name_en, brief=brief, brief_en=brief_en,
            faction=fac, ai=ai, country=country, money=money, line=line, idx=li, track=0,
            size=size, ab=ab, au=au, art=art,
            objs=objs, trigs=boot(brief,brief_en)+std_fail(hero,protect)+trigs, waves=waves
        ), ml); n+=1

    print(f"Built {n} full scripted missions")
    return n

if __name__ == "__main__":
    build()
