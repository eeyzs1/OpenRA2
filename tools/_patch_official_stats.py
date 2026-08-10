#!/usr/bin/env python3
"""Patch assets/rules/rules.ini HP / Weapon Damage/Cooldown/Range from YR rulesmd (+ RA2 overrides).

China fusion units (PLA, Type99, Aegis as China-only branding kept) are skipped.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RULES = ROOT / "assets" / "rules" / "rules.ini"
RULESMD = Path(
    r"C:\Users\eeyzs1\.cursor\projects\e-AI-Generated-Projects-OpenRA2"
    r"\agent-tools\c1c0cf75-2722-49e8-a920-160e9d81792d.txt"
)

# OpenRA2 section -> rulesmd TechnoType id
UNIT_MAP = {
    "Unit.GI": "E1",
    "Unit.Conscript": "E2",
    "Unit.AttackDog": "ADOG",
    "Unit.Engineer": "ENGINEER",
    "Unit.GuardianGI": "GGI",
    "Unit.Rocketeer": "JUMPJET",
    "Unit.Sniper": "SNIPE",
    "Unit.Spy": "SPY",
    "Unit.NavySEAL": "GHOST",
    "Unit.Tanya": "TANY",
    "Unit.Chrono": "CLEG",
    "Unit.ChronoCommando": "CCOMAND",
    "Unit.FlakTrooper": "FLAKT",
    "Unit.TeslaTrooper": "SHK",
    "Unit.CrazyIvan": "IVAN",
    "Unit.Terrorist": "TERROR",
    "Unit.Desolator": "DESO",
    "Unit.Boris": "BORIS",
    "Unit.Initiate": "INIT",
    "Unit.Brute": "BRUTE",
    "Unit.Virus": "VIRUS",
    "Unit.Yuri": "YURI",
    "Unit.YuriPrime": "YURIPR",
    "Unit.ChronoIvan": "CIVAN",
    "Unit.PsiCommando": "PTROOP",
    "Unit.ChronoMiner": "CMIN",
    "Unit.WarMiner": "HARV",
    "Unit.SlaveMiner": "SMIN",
    "Unit.Harvester": "HARV",
    "Unit.Grizzly": "MTNK",
    "Unit.Rhino": "HTNK",
    "Unit.LasherTank": "LTNK",
    "Unit.IFV": "FV",
    "Unit.FlakTrack": "HTK",
    "Unit.GatlingTank": "YTNK",
    "Unit.TerrorDrone": "DRON",
    "Unit.V3Launcher": "V3",
    "Unit.TeslaTank": "TTNK",
    "Unit.DemoTruck": "DTRUCK",
    "Unit.Apocalypse": "APOC",
    "Unit.MirageTank": "MGTK",
    "Unit.PrismTank": "SREF",
    "Unit.BattleFortress": "BFRT",
    "Unit.RobotTank": "ROBO",
    "Unit.ChaosDrone": "CAOS",
    "Unit.Magnetron": "TELE",
    "Unit.MasterMind": "MIND",
    "Unit.MCV": "AMCV",
    "Unit.Nighthawk": "SHAD",
    "Unit.Intruder": "ORCA",
    "Unit.BlackEagle": "BEAG",
    "Unit.Kirov": "ZEP",
    "Unit.SiegeChopper": "SCHP",
    "Unit.FloatingDisc": "DISK",
    "Unit.Destroyer": "DEST",
    "Unit.Aegis": "AEGIS",
    "Unit.Dolphin": "DLPH",
    "Unit.AircraftCarrier": "CARRIER",
    "Unit.Typhoon": "SUB",
    "Unit.SeaScorpion": "HYD",
    "Unit.Squid": "SQD",
    "Unit.Dreadnought": "DRED",
    "Unit.AmphTransport": "SAPC",
    "Unit.Boomer": "BSUB",
    "Unit.TankDestroyer": "TNKD",
    "Unit.Hornet": "HORNET",
    "Unit.Slave": "SLAV",
    # OpenRA2 MiG is Soviet attack jet; align HP/cost to Harrier-class ORCA, keep distinct weapon via manual below
    "Unit.MiG": "ORCA",
}

BLD_MAP = {
    "Bld.PowerPlant": "GAPOWR",
    "Bld.TeslaReactor": "NAPOWR",
    "Bld.NuclearReactor": "NANRCT",
    "Bld.OreRefinery": "GAREFN",
    "Bld.Barracks": "GAPILE",
    "Bld.WarFactory": "GAWEAP",
    "Bld.AirForceCmd": "GAAIRC",
    "Bld.Radar": "NARADR",
    "Bld.BattleLab": "GATECH",
    "Bld.OrePurifier": "GAOREP",
    "Bld.IndustrialPlant": "NAINDP",
    "Bld.Grinder": "YAGRND",
    "Bld.CloningVat": "NACLON",
    "Bld.Pillbox": "GAPILL",
    "Bld.SentryGun": "NALASR",
    "Bld.PatriotMissile": "NASAM",
    "Bld.FlakCannon": "NAFLAK",
    "Bld.TeslaCoil": "TESLA",
    "Bld.PrismTower": "ATESLA",
    "Bld.GrandCannon": "GTGCAN",
    "Bld.ChronoSphere": "GACSPH",
    "Bld.WeatherDevice": "GAWEAT",
    "Bld.IronCurtain": "NAIRON",
    "Bld.NukeSilo": "NAMISL",
    "Bld.ConYard": "GACNST",
    "Bld.Wall": "GAWALL",
    "Bld.ServiceDepot": "GADEPT",
    "Bld.BioReactor": "YAPOWR",
    "Bld.BattleBunker": "NABNKR",
    "Bld.GattlingCannon": "YAGGUN",
    "Bld.PsychicTower": "YAPSYT",
    "Bld.TankBunker": "YATNKBNK",
    "Bld.GeneticMutator": "YAGNTC",
    "Bld.PsychicDominator": "YAPPET",
    "Bld.RobotControl": "GAROBO",
    "Bld.SpySat": "GASPYSAT",
    "Bld.GapGenerator": "GAGAP",
    "Bld.PsychicSensor": "NAPSIS",
}

# Skip fusion-only OpenRA2 sections
SKIP = {"Unit.PLA", "Unit.Type99"}

# RA2 1.006 overrides when they differ from YR rulesmd (user: prefer RA2 for shared units).
# Sources: RA2 FAQ / infantry.md ~1.004–1.006; only apply clear documented diffs.
RA2_OVERRIDES = {
    # GI deployed sandbags: RA2 FAQ Damage=15 ROF=15 Range=5 (YR Para Secondary=25)
    "DeployWeapon.GI": {"Damage": 15, "Cooldown": 15, "Range": 5},
}

# Weapons that are special (mind control / place bomb) — keep Damage semantics but align ROF/Range/HP
SPECIAL_PRIMARY_SKIP_DAMAGE = {
    "Unit.Yuri",
    "Unit.YuriPrime",
    "Unit.PsiCommando",
    "Unit.MasterMind",
    "Unit.Magnetron",
    "Unit.CrazyIvan",
    "Unit.ChronoIvan",
    "Unit.Chrono",  # erase weapon uses progress, not raw dmg
    "Unit.V3Launcher",  # launcher Damage=1 spawns missile
    "Unit.AircraftCarrier",
    "Unit.Dreadnought",
    "Unit.Engineer",
    "Unit.Spy",
    "Unit.ChaosDrone",  # PsychGasCreate: official Damage=600 is gas spawn, not direct HP
}


def parse_ini(text: str) -> dict[str, dict[str, str]]:
    secs: dict[str, dict[str, str]] = {}
    cur = None
    for line in text.splitlines():
        m = re.match(r"^\[([^\]]+)\]", line)
        if m:
            cur = m.group(1)
            secs.setdefault(cur, {})
            continue
        if cur is None or not line.strip() or line.lstrip().startswith(";"):
            continue
        if "=" not in line:
            continue
        k, v = line.split("=", 1)
        secs[cur][k.strip()] = v.strip().split(";")[0].strip()
    return secs


def round_range(v: str | None) -> int | None:
    if v is None or v == "":
        return None
    try:
        f = float(v)
    except ValueError:
        return None
    if f < 0:
        return None  # spy MakeupKit Range=-2 etc.
    return max(1, int(round(f)))


def weap(secs: dict, name: str | None) -> tuple[int | None, int | None, int | None]:
    if not name or name.lower() in ("none", "noweapon"):
        return None, None, None
    w = secs.get(name, {})
    d = w.get("Damage")
    r = w.get("ROF")
    rng = w.get("Range")
    di = int(float(d)) if d not in (None, "") else None
    ri = int(float(r)) if r not in (None, "") else None
    return di, ri, round_range(rng)


def set_key(sec: dict[str, str], key: str, value) -> bool:
    if value is None:
        return False
    vs = str(int(value) if isinstance(value, float) and value == int(value) else value)
    if sec.get(key) == vs:
        return False
    sec[key] = vs
    return True


def main() -> None:
    rmd = parse_ini(RULESMD.read_text(encoding="utf-8", errors="replace"))
    rules_text = RULES.read_text(encoding="utf-8", errors="replace")
    rules = parse_ini(rules_text)

    changes: list[str] = []

    def apply_hp_cost_power(sec_name: str, rid: str, *, do_cost: bool = True, do_power: bool = True):
        if sec_name not in rules:
            return
        s = rmd.get(rid, {})
        sec = rules[sec_name]
        if "Strength" in s and set_key(sec, "HP", int(float(s["Strength"]))):
            changes.append(f"{sec_name} HP -> {s['Strength']}")
        if do_cost and "Cost" in s and set_key(sec, "Cost", int(float(s["Cost"]))):
            changes.append(f"{sec_name} Cost -> {s['Cost']}")
        if do_power and "Power" in s and s["Power"] != "" and set_key(sec, "Power", int(float(s["Power"]))):
            changes.append(f"{sec_name} Power -> {s['Power']}")

    def primary_weapon_name(s: dict[str, str]) -> str | None:
        for key in ("Primary", "Weapon1", "Secondary"):
            prim = s.get(key)
            if prim and prim.lower() not in ("none", "noweapon"):
                return prim
        return None

    def elite_weapon_name(s: dict[str, str]) -> str | None:
        for key in ("ElitePrimary", "EliteWeapon1", "Elite"):
            prim = s.get(key)
            if prim and prim.lower() not in ("none", "noweapon"):
                return prim
        return None

    def apply_weapon(sec_name: str, rid: str, *, elite: bool = True):
        if sec_name not in rules:
            return
        s = rmd.get(rid, {})
        sec = rules[sec_name]
        prim = primary_weapon_name(s)
        dmg, rof, rng = weap(rmd, prim)
        skip_dmg = sec_name in SPECIAL_PRIMARY_SKIP_DAMAGE
        if not skip_dmg and set_key(sec, "Weapon.Damage", dmg):
            changes.append(f"{sec_name} Weapon.Damage -> {dmg}")
        if set_key(sec, "Weapon.Cooldown", rof):
            changes.append(f"{sec_name} Weapon.Cooldown -> {rof}")
        if set_key(sec, "Weapon.Range", rng):
            changes.append(f"{sec_name} Weapon.Range -> {rng}")

        if elite:
            ep = elite_weapon_name(s)
            ed, er, e_rng = weap(rmd, ep)
            if ed is not None and "Elite.Damage" in sec and set_key(sec, "Elite.Damage", ed):
                changes.append(f"{sec_name} Elite.Damage -> {ed}")
            if er is not None and "Elite.Cooldown" in sec and set_key(sec, "Elite.Cooldown", er):
                changes.append(f"{sec_name} Elite.Cooldown -> {er}")
            if e_rng is not None and "Elite.Range" in sec and set_key(sec, "Elite.Range", e_rng):
                changes.append(f"{sec_name} Elite.Range -> {e_rng}")

    for oid, rid in UNIT_MAP.items():
        if oid in SKIP:
            continue
        apply_hp_cost_power(oid, rid, do_power=False)
        apply_weapon(oid, rid)

    for oid, rid in BLD_MAP.items():
        apply_hp_cost_power(oid, rid)
        apply_weapon(oid, rid, elite=False)

    # Deploy weapons from Secondary (YR) with RA2 override for GI
    deploy_map = {
        "DeployWeapon.GI": ("E1", "Para"),
        "DeployWeapon.GuardianGI": ("GGI", "MissileLauncher"),
        "DeployWeapon.SiegeChopper": ("SCHP", "160mm"),
    }
    for dsec, (rid, wname) in deploy_map.items():
        if dsec not in rules:
            continue
        dmg, rof, rng = weap(rmd, wname)
        ov = RA2_OVERRIDES.get(dsec, {})
        if "Damage" in ov:
            dmg = ov["Damage"]
        if "Cooldown" in ov:
            rof = ov["Cooldown"]
        if "Range" in ov:
            rng = ov["Range"]
        sec = rules[dsec]
        if set_key(sec, "Damage", dmg):
            changes.append(f"{dsec} Damage -> {dmg}")
        if set_key(sec, "Cooldown", rof):
            changes.append(f"{dsec} Cooldown -> {rof}")
        if set_key(sec, "Range", rng):
            changes.append(f"{dsec} Range -> {rng}")

    # GGI undeployed uses M60 in rulesmd (same as GI).
    if "Unit.GuardianGI" in rules:
        sec = rules["Unit.GuardianGI"]
        if set_key(sec, "Weapon.Damage", 15):
            changes.append("Unit.GuardianGI Weapon.Damage -> 15")
        if set_key(sec, "Weapon.Cooldown", 20):
            changes.append("Unit.GuardianGI Weapon.Cooldown -> 20")
        if set_key(sec, "Weapon.Range", 4):
            changes.append("Unit.GuardianGI Weapon.Range -> 4")

    # Barracks / War Factory power from rulesmd
    if "Bld.Barracks" in rules and set_key(rules["Bld.Barracks"], "Power", -10):
        changes.append("Bld.Barracks Power -> -10")
    if "Bld.WarFactory" in rules and set_key(rules["Bld.WarFactory"], "Power", -25):
        changes.append("Bld.WarFactory Power -> -25")

    # Tech buildings HP
    for oid, rid in {
        "Bld.OilDerrick": "CAOILD",
        "Bld.Hospital": "CAHOSP",
        "Bld.MachineShop": "CAMACH",
        "Bld.TechAirport": "CAAIRP",
        "Bld.TechPowerPlant": "CAPOWR",
        "Bld.TechOutpost": "CAOUTP",
        "Bld.SecretLab": "CASCAD",
    }.items():
        if oid in rules and rid in rmd and "Strength" in rmd[rid]:
            if set_key(rules[oid], "HP", int(float(rmd[rid]["Strength"]))):
                changes.append(f"{oid} HP -> {rmd[rid]['Strength']}")

    # Write back preserving section order as much as possible
    out_lines: list[str] = []
    cur = None
    seen_keys: set[str] = set()
    for line in rules_text.splitlines():
        m = re.match(r"^\[([^\]]+)\]", line)
        if m:
            # flush nothing — keys rewritten inline
            cur = m.group(1)
            seen_keys = set()
            out_lines.append(line)
            continue
        if cur and cur in rules and "=" in line and not line.lstrip().startswith(";"):
            k = line.split("=", 1)[0].strip()
            if k in rules[cur]:
                out_lines.append(f"{k}={rules[cur][k]}")
                seen_keys.add(k)
                continue
        out_lines.append(line)

    RULES.write_text("\n".join(out_lines) + ("\n" if rules_text.endswith("\n") else ""), encoding="utf-8")
    print(f"Wrote {RULES} with {len(changes)} field changes")
    for c in changes:
        print(" ", c)


if __name__ == "__main__":
    main()
