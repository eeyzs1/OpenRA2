# 从 MIX rules.ini / art.ini 同步 Cost/Strength/Foundation 到 assets/rules/rules.ini
# 用法: python sync_rules_from_mix.py
import os, re
from ra2lib import MixTree

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RULES = os.path.join(ROOT, "assets", "rules", "rules.ini")

# OpenRA2 section -> MIX section(s)（取第一个命中）
MAP = {
    "Unit.MCV": ["AMCV", "SMCV", "PCV"],
    "Unit.GI": ["E1"],
    "Unit.Conscript": ["E2"],
    "Unit.Engineer": ["ENGINEER"],
    "Unit.AttackDog": ["ADOG", "DOG"],
    "Unit.Grizzly": ["MTNK"],
    "Unit.Rhino": ["HTNK"],
    "Unit.IFV": ["FV"],
    "Unit.PrismTank": ["SREF"],
    "Unit.TeslaTank": ["TTNK"],
    "Unit.Apocalypse": ["APOC"],
    "Unit.Kirov": ["ZEP"],
    "Unit.Harvester": ["HARV"],
    "Unit.ChronoMiner": ["CMIN"],
    "Unit.WarMiner": ["HARV"],
    "Unit.Tanya": ["TANY"],
    "Unit.Boris": ["BORIS", "SNIPE"],
    "Bld.ConYard": ["GACNST", "NACNST"],
    "Bld.PowerPlant": ["GAPOWR"],
    "Bld.TeslaReactor": ["NAPOWR"],
    "Bld.NuclearReactor": ["NANRCT"],
    "Bld.Barracks": ["GAPILE", "NAHAND"],
    "Bld.WarFactory": ["GAWEAP", "NAWEAP"],
    "Bld.OreRefinery": ["GAREFN", "NAREFN"],
    "Bld.Radar": ["GARADR", "NARADR"],
    "Bld.BattleLab": ["GATECH", "NATECH"],
    "Bld.AirForceCmd": ["GAAIRC"],
    "Bld.NavalYard": ["GAYARD", "NAYARD"],
    "Bld.Pillbox": ["GAPILL"],
    "Bld.SentryGun": ["NALASR"],
    "Bld.PrismTower": ["GAPRIS", "ATESLA"],
    "Bld.TeslaCoil": ["TESLA"],
    "Bld.FlakCannon": ["NAFLAK"],
    "Bld.PatriotMissile": ["NASAM"],
    "Bld.OrePurifier": ["GAOREP"],
    "Bld.IndustrialPlant": ["NAINDP"],
    "Bld.NukeSilo": ["NAMISL"],
    "Bld.WeatherDevice": ["GAWEAT"],
    "Bld.IronCurtain": ["NAIRON"],
    "Bld.ChronoSphere": ["GACSPH"],
    "Bld.ServiceDepot": ["GADEPT"],
    "Bld.Hospital": ["CAHOSP"],
    "Bld.CivHouse": ["CAHSE01"],
    "Bld.PsychicSensor": ["NAPSIS"],
    "Bld.TechOutpost": ["CAOUTP"],
    "Bld.CloningVat": ["NACLON"],
    "Bld.OilDerrick": ["CAOILD"],
    "Bld.GapGenerator": ["GAGAP"],
    "Bld.GrandCannon": ["GTGCAN"],
}

def parse_ini(txt):
    secs, cur = {}, None
    for line in txt.splitlines():
        line = line.strip()
        if not line or line.startswith(";") or line.startswith("//"):
            continue
        m = re.match(r"\[(.+?)\]", line)
        if m:
            # art.ini 偶发损坏节名如 "GACSPH]  ;[GACHRO" —— 取到第一个 ]
            cur = m.group(1).split("]")[0].strip()
            secs.setdefault(cur, {})
            continue
        if "=" in line and cur is not None:
            k, v = line.split("=", 1)
            secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs

def parse_foundation(s):
    if not s:
        return None
    m = re.match(r"(\d+)\s*[xX]\s*(\d+)", s.strip())
    if not m:
        return None
    return int(m.group(1)), int(m.group(2))

def sub_field(text, sec, key, val):
    text2, n = re.subn(
        rf"(\[{re.escape(sec)}\][^\[]*?)(^{re.escape(key)}=)\d+",
        rf"\1\g<2>{val}",
        text, count=1, flags=re.M | re.S)
    return text2, n

def main():
    T = MixTree()
    _, raw = T.find("rules.ini")
    if not raw:
        print("FAIL: rules.ini not in MIX")
        return 1
    mix = parse_ini(raw.decode("latin-1", "replace"))
    _, art_raw = T.find("art.ini")
    art = parse_ini(art_raw.decode("latin-1", "replace")) if art_raw else {}

    with open(RULES, "r", encoding="utf-8") as f:
        text = f.read()

    updates = []
    for sec, cands in MAP.items():
        src = next((mix[c] for c in cands if c in mix), None)
        if src:
            cost = src.get("Cost")
            strength = src.get("Strength")
            if cost:
                text, n = sub_field(text, sec, "Cost", cost)
                if n:
                    updates.append(f"{sec} Cost={cost}")
            if strength:
                text, n = sub_field(text, sec, "HP", strength)
                if n:
                    updates.append(f"{sec} HP={strength}")

        # Foundation 在 art.ini（占地 W×H）
        if sec.startswith("Bld."):
            art_src = next((art[c] for c in cands if c in art), None)
            # ChronoSphere 节名偶发损坏
            if not art_src and "GACSPH" in cands:
                for k, v in art.items():
                    if k.startswith("GACSPH") and v.get("Foundation"):
                        art_src = v
                        break
            if art_src:
                wh = parse_foundation(art_src.get("Foundation"))
                if wh:
                    text, n1 = sub_field(text, sec, "W", wh[0])
                    text, n2 = sub_field(text, sec, "H", wh[1])
                    if n1 or n2:
                        updates.append(f"{sec} Foundation={wh[0]}x{wh[1]}")

    with open(RULES, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"updated {len(updates)} fields:")
    for u in updates:
        print(" ", u)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
