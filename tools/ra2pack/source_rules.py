"""Read one identified original rules edition without mixing RA2 and YR values.

The JSON catalog retains provenance; --profile exports an executable rules package.
Keep faction aliases distinct and retain raw units beside explicit conversions.
Native scenes and the skirmish importer can consume the same weapon/timing data.
"""
import argparse
import ast
import configparser
import hashlib
import json
from pathlib import Path
import re

from gen_native_weapons import export_native_weapons
from gen_native_weapon_catalog import export_native_weapon_catalog
from gen_native_production import production_timing, production_economy, export_support_rules, building_repair_cost, building_sale_values, SOURCE_SW_TYPES
from gen_voices import unit_mapping
from map_format import read_ini
from ra2lib import MixTree
from gen_native_targets import export_native_targets
from gen_native_movement import export_native_movement
from gen_native_jumpjet import export_native_jumpjet
from gen_native_rockets import export_native_rockets
from gen_native_crates import export_native_crates

ROOT = Path(__file__).resolve().parents[2]
BUILDING_ALIASES = {
    "ConYard": ["GACNST", "NACNST", "YACNST"],
    "Barracks": ["GAPILE", "NAHAND", "YABRCK"],
    "WarFactory": ["GAWEAP", "NAWEAP", "YAWEAP"],
    "OreRefinery": ["GAREFN", "NAREFN", "YAREFN"],
    "BattleLab": ["GATECH", "NATECH", "YATECH"],
    "AirForceCmd": ["GAAIRC", "AMRADR"],
    "NavalYard": ["GAYARD", "NAYARD", "YAYARD"],
    "ServiceDepot": ["GADEPT", "NADEPT"],
    "Wall": ["GAWALL", "NAWALL", "GAFWLL"],
    "PrismTower": ["ATESLA"],
    "TeslaCoil": ["TESLA"],
    "PsychicAmplifier": ["NAPSYA"],
    "TimeMachine": ["CATIME01", "CATIME02"],
}


def literal_mapping(name):
    tree = ast.parse((Path(__file__).with_name("gen_assets.py")).read_text(encoding="utf-8"))
    for statement in tree.body:
        if isinstance(statement, ast.Assign) and any(isinstance(key, ast.Name) and key.id == name for key in statement.targets):
            return ast.literal_eval(statement.value)
    raise ValueError("missing asset mapping: " + name)


def enum_names(kind):
    text = (ROOT / "src/game/data.h").read_text(encoding="utf-8")
    body = re.search(r"enum class " + kind + r"\s*:\s*uint8_t\s*\{(.*?)\};", text, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    return [part.split("=")[0].strip() for part in body.split(",") if part.strip() and part.split("=")[0].strip() != "COUNT"]


def select_source(tree, edition, kind="rules"):
    if edition not in ("ra2", "yr"):
        raise ValueError("unknown original rules edition: " + edition)
    if kind not in ("rules", "art", "ai"):
        raise ValueError("unknown original INI kind: " + kind)
    name = kind + (".ini" if edition == "ra2" else "md.ini")
    entries = tree.find_all(name)
    if edition == "yr":
        patched = [(source, data) for source, data in entries
                   if (source.lower().split('/')[0] == "expandmd01.mix" if kind == "ai"
                       else "expandmd01.mix" in source.lower())]
        if patched:
            if kind == "ai" and len(patched) != 1:
                raise ValueError("a unique original source is required: " + name)
            source, data = sorted(patched)[0]
            return source, data, "yr-expandmd01"
        entries = [(source, data) for source, data in entries if source.lower() == "ra2md.mix/localmd.mix"]
    else:
        entries = [(source, data) for source, data in entries if source.lower() == "ra2.mix/local.mix"]
    if len(entries) != 1:
        raise ValueError("a unique original source is required: " + name)
    source, data = entries[0]
    # A base archive is not evidence for the final patched edition.
    return source, data, edition + "-local-base"


def source_asset(tree, edition, name):
    copies = tree.find_all(name)
    roots = (["expandmd01.mix", "ra2md.mix/"] if edition == "yr" else []) + ["ra2.mix/"]
    for root in roots:
        candidates = sorted((source, data) for source, data in copies
                            if source.lower() == root or source.lower().startswith(root))
        if candidates:
            return candidates[0]
    raise FileNotFoundError(f"original {edition} asset missing: {name}")


# The order is the persisted SkirmishMode enumeration. Land Rush is a project
# mode; the original multiplayer-mode catalog does not declare that mode.
MODE_FILES = ["MPBattle", "MPFreeForAll", "MPUnholy", "MPMW", None, "MPMeat", "MPNaval"]


def export_mode_overlays(tree, edition, ini, types):
    ini["Rules.Modes"] = {"Version": "1", "Count": str(len(MODE_FILES))}
    indexes = {identity.lower(): i for i, identity in enumerate(types)}
    for mode, stem in enumerate(MODE_FILES):
        sections, source, data, filename = {}, "", b"", ""
        if stem:
            filename = stem + ("MD" if edition == "yr" else "") + ".ini"
            source, data = source_asset(tree, edition, filename)
            sections = read_ini(data)
        prefix = "Rules.Mode." + str(mode)
        changes, unmapped = [], []
        for identity, fields in sections.items():
            delta = {dst: fields[src] for src, dst in [
                ("techlevel", "TechLevel"), ("prerequisite", "Prerequisite")]
                if src in fields}
            for src, dst in [("crategoodie", "CrateGoodie"), ("allowedtostartinmultiplayer", "StartAllowed")]:
                if src in fields:
                    delta[dst] = str(int(fields[src].lower() in ("yes", "true", "1")))
            if not delta:
                continue
            if identity not in indexes:
                unmapped.append(identity.upper())
                continue
            changes.append({"Type": str(indexes[identity]), **delta})
        growth = not any(fields.get("growthpercentage") == "0" for identity, fields in sections.items()
                         if identity in ("riparius", "cruentus", "vinifera", "aboreus"))
        ini[prefix] = {"Original": str(int(stem is not None)), "File": filename,
                       "Archive": source, "SHA256": hashlib.sha256(data).hexdigest() if stem else "",
                       "OreGrowth": str(int(growth)), "Overrides": str(len(changes)), "Unmapped": ",".join(unmapped)}
        for i, fields in enumerate(changes):
            ini[prefix + ".Override." + str(i)] = fields


def build_catalog(tree, edition, scenario_file=None):
    source, raw, identity = select_source(tree, edition)
    rules = read_ini(raw)
    scenario, scenario_record = {}, None
    if scenario_file:
        from gen_native_maps import core_file
        scenario_source, scenario_raw = core_file(tree, scenario_file)
        scenario = read_ini(scenario_raw)
        scenario_record = {"file": scenario_file, "source": scenario_source, "sha256": hashlib.sha256(scenario_raw).hexdigest()}
    def merged(key):
        return {**rules.get(key.lower(), {}), **scenario.get(key.lower(), {})}
    art_source, art_raw, _ = select_source(tree, edition, "art")
    art = read_ini(art_raw)
    unit_assets, building_assets = unit_mapping(), literal_mapping("BLDS")
    result = {"version": 1, "edition": identity, "source": source,
              "sha256": hashlib.sha256(raw).hexdigest(), "art_source": art_source,
              "art_sha256": hashlib.sha256(art_raw).hexdigest(),
              "source_hz": 15, "logic_hz": 30, "units": {}, "buildings": {}, "unmapped": []}
    if scenario_record:
        result["scenario"] = scenario_record
    config = configparser.ConfigParser(interpolation=None);config.optionxform = str
    indexed = []
    for kind, names, mapping in (("units", enum_names("UnitType"), unit_assets),
                                 ("buildings", enum_names("BldType"), building_assets)):
        for name in names:
            if name == "NativeInfantry" and edition != "ra2":
                continue
            aliases = list(mapping.get(name.lower(), []))
            if kind == "buildings":
                aliases = BUILDING_ALIASES.get(name, aliases)
            elif name == "Engineer":
                aliases = ["ENGINEER", "SENGINEER", "YENGINEER"]
            elif name in ("MCV", "AttackDog", "AmphTransport"):
                for key, values in unit_assets.items():
                    if key.startswith(name.lower() + "."):
                        aliases += values
            objects = []
            for alias in dict.fromkeys(aliases):
                if alias.lower() not in rules:
                    continue
                definition = merged(alias)
                frames, step_ticks = production_timing(definition, merged("general"))
                item = {"id": alias, "raw": definition.copy(), "hp": int(definition.get("strength", "1")),
                        "cost": int(definition.get("cost", "0")), "sight": int(definition.get("sight", "0")),
                        "armor": definition.get("armor", "none"),
                        "production": {"base_source_frames": frames, "step_logic_ticks": step_ticks, "total_logic_ticks": 54 * step_ticks}}
                if alias.lower() in scenario:
                    item["overrides"] = scenario[alias.lower()].copy()
                if kind == "buildings":
                    visual = art.get(definition.get("image", alias).lower(), {})
                    item["foundation"] = visual.get("foundation", "1x1")
                    item["power"] = int(definition.get("power", "0"))
                index = len(indexed);indexed.append(item)
                config["Type." + str(index)] = {"Id": alias, "Building": str(int(kind == "buildings")), "Kind": "0"}
                objects.append(item)
            if objects:
                result[kind][name] = objects
            else:
                result["unmapped"].append(kind + "." + name)
    export_native_weapons(config, rules, scenario)
    for index, item in enumerate(indexed):
        row = config["Type." + str(index)]
        item["guns"] = {key: value for key, value in row.items() if key.startswith("Gun")}
        for slot in range(4):
            prefix = "Gun" + str(slot)
            if row[prefix + "Present"] == "1":
                # Same conversion used by native_weapons_parse.cpp. Raw ROF is
                # retained so a source frame can never be mistaken for a tick.
                item["guns"][prefix + "CooldownTicks"] = str(int(row[prefix + "ROF"]) * 2)
    return result


def export_profile(tree, edition, output):
    """Emit a map-independent scene using the same native runtime contracts."""
    catalog = build_catalog(tree, edition)
    _, raw, _ = select_source(tree, edition);rules = read_ini(raw)
    _, art_raw, _ = select_source(tree, edition, "art");art = read_ini(art_raw)
    general = rules["general"]
    ini = configparser.ConfigParser(interpolation=None);ini.optionxform = str
    label = "RA2 本地基础规则" if edition == "ra2" else "尤里本地基础规则"
    ini["Scene"] = {"Version": "68" if edition == "ra2" else "43", "TemporalVersion": "1", "CrewVersion": "2", "Mode": "Rules", "RulesId": catalog["edition"], "RulesName": label,
                    "Map": "", "SourceSHA256": catalog["sha256"], "SourceArchive": catalog["source"],
                    "SourceArtSHA256": catalog["art_sha256"], "SourceArtArchive": catalog["art_source"],
                    "Houses": "0", "Actors": "0", "Types": "0",
                    "CountryMask": str(sum(1 << i for i in list(range(1, 10)) + ([11] if edition == "yr" else [])))}
    tables = {key: {value.upper() for value in rules.get(key, {}).values()}
              for key in ("infantrytypes", "vehicletypes", "aircrafttypes", "buildingtypes")}
    armor = ["none", "flak", "plate", "light", "medium", "heavy", "wood", "steel", "concrete", "special_1", "special_2"]
    yes = lambda value: str(value).lower() in ("yes", "true", "1")
    if edition == "ra2":
        ini["NativeTransportUnload"] = {"Version":"1", "RateFrames":str(int(float(rules["unload"]["rate"])*900)),
            "RandomFrames":"2", "MovingFrames":"10", "StageFrames":"1"}
        ini["NativeCrushWarning"] = {"Version": "1",
            "PlayerScatter": str(int(yes(rules.get("combatdamage", {}).get("playerscatter", "no")))),
            "ComputerIQ": rules.get("iq", {}).get("maxiqlevels", "5"),
            "ScatterIQ": rules.get("iq", {}).get("scatter", "2")}
    launchers = ["DRED", "V3"] + (["BSUB"] if edition == "yr" else [])
    missiles = [rules[identity.lower()]["spawns"].upper() for identity in launchers]
    units = dict(catalog["units"])
    units["NativeVehicle"] = [{"id": identity, "raw": rules[identity.lower()],
        "hp": int(rules[identity.lower()]["strength"]), "cost": int(rules[identity.lower()].get("cost", "0")),
        "sight": int(rules[identity.lower()].get("sight", "0")), "armor": rules[identity.lower()]["armor"]} for identity in missiles]
    # Append the source carrier after all established slots. Frozen AI and
    # older actor indexes must not move when the ordinary profile grows.
    supplemental = units.pop("NativeInfantry", [])
    types, seen = [], set()
    for kind, group in (("units", units), ("buildings", catalog["buildings"]), ("units", {"NativeInfantry": supplemental})):
        for base, aliases in group.items():
            for item in aliases:
                id, d = item["id"], item["raw"]
                # The generic fusion harvester shares HARV art. Its original
                # gameplay identity is the Soviet War Miner, not a third unit.
                if id == "HARV" and base == "Harvester":
                    continue
                if id in seen:
                    continue
                seen.add(id);index = len(types);types.append(id)
                building = kind == "buildings"
                visual = art.get(d.get("image", id).lower(), {})
                row = {"Id": id, "Name": d.get("name", id), "Art": id.lower(), "Base": base,
                       "Building": str(int(building)), "Kind": "0", "Hp": str(item["hp"]), "Cost": str(item["cost"]),
                       "Sight": str(item["sight"]), "Armor": str(armor.index(item["armor"].lower())),
                       "NativeBody": str(int(id in launchers or id in missiles or id in ("DEST", "ASW", "PDPLANE", "SPYP"))), "NativeRocket": str(int(id in missiles)), "CanPassiveAcquire": str(int(yes(d.get("canpassiveaquire", "yes")))),
                       "NavalTargeting": d.get("navaltargeting", "0"), "LandTargeting": d.get("landtargeting", "0"), "Underwater": str(int(yes(d.get("underwater", "no")))),
                       "Selectable": str(int(yes(d.get("selectable", "yes")))), "Significant": str(int(not yes(d.get("insignificant", "no")))),
                       "OriginalSpeed": d.get("speed", "0"), "OriginalLocomotor": d.get("locomotor", ""),
                       "TransformsTo": d.get("undeploysinto" if building else "deploysinto", ""),
                       "OriginalPlanningSide": d.get("aibaseplanningside", "-1"),
                       "OriginalVeteranSpeed": general.get("veteranspeed", "1"),
                       "VeteranFaster": str(int("FASTER" in d.get("veteranabilities", "").upper().split(","))),
                       "EliteFaster": str(int("FASTER" in (d.get("veteranabilities", "") + "," + d.get("eliteabilities", "")).upper().split(","))),
                       "TooBigToFitUnderBridge": str(int(yes(d.get("toobigtofitunderbridge", "no"))))}
                if edition == "ra2":
                    row.update(SaleCrewed=str(int(yes(d.get("crewed", "no")))),
                               Engineer=str(int(yes(d.get("engineer", "no")))))
                if edition == "ra2" and not building:
                    # The original infantry constructor enables Crushable;
                    # other object types and Crusher default to false.
                    row.update(Crusher=str(int(yes(d.get("crusher", "no")))),
                               Crushable=str(int(yes(d.get("crushable", "yes" if id in tables["infantrytypes"] else "no")))),
                               Fraidycat=str(int(yes(d.get("fraidycat", "no")))))
                if building:
                    wall=yes(d.get("wall", "no"))
                    if not wall and "height" not in visual:raise ValueError((id,"missing original art Height"))
                    match = re.fullmatch(r"(\d+)[xX](\d+)", item["foundation"])
                    if not match:
                        raise ValueError((id, "unsupported foundation", item["foundation"]))
                    row.update(Width=match[1], Height=match[2], Power=str(item["power"]),
                               Wall=str(int(wall)), AirObstacleHeight=str(0 if wall else int(visual["height"])*104),
                               Garrison=d.get("maxnumberoccupants", "0") if yes(d.get("canbeoccupied", "no")) else "0",
                               Capturable=str(int(yes(d.get("capturable", "no")))))
                else:
                    if edition == "ra2": row["IsTrain"] = str(int(yes(d.get("istrain", "no"))))
                    row.update(TransportCapacity=d.get("passengers", "0"), PassengerSize=d.get("size", "1"),
                               PassengerLimit=d.get("sizelimit", "2"), SourceAmmo=d.get("ammo", "0"), SourceSpawns=d.get("spawnsnumber", "0"))
                ini["Type." + str(index)] = row
                infantry = id in tables["infantrytypes"];air = id in tables["aircrafttypes"]
                naval = yes(d.get("naval", "no"))
                category = (5 if yes(d.get("isbasedefense", "no")) else 4) if building else 0 if infantry else 2 if air else 3 if naval else 1
                table = "buildingtypes" if building else "infantrytypes" if infantry else "aircrafttypes" if air else "vehicletypes"
                order = next((i for i, (_, value) in enumerate(sorted(rules.get(table, {}).items(), key=lambda pair: int(pair[0]))) if value.upper() == id), 4095)
                frames, step_ticks = production_timing(d, general)
                ini["NativeProduction.Type." + str(index)] = {
                    "TechLevel": d.get("techlevel", "-1"), "Category": str(category), "Factory": d.get("factory", "none"),
                    "Owner": d.get("owner", ""), "RequiredHouses": d.get("requiredhouses", ""), "ForbiddenHouses": d.get("forbiddenhouses", ""),
                    "Prerequisite": d.get("prerequisite", ""), "BuildLimit": d.get("buildlimit", "2147483647"),
                    "BaseBuildFrames": str(frames), "StepTicks": str(step_ticks), "BaseNormal": str(int(yes(d.get("basenormal", "yes")))),
                    "Naval": str(int(naval)), "Adjacent": d.get("adjacent", "2"), "FreeUnit": d.get("freeunit", ""),
                    "Cameo": visual.get("cameo", ""), "Order": str(order),
                    "InfantryExit": str(1 if yes(d.get("gdibarracks", "no")) else 2 if yes(d.get("nodbarracks", "no")) else 0)}
                if edition == "ra2":
                    ini["NativeProduction.Type." + str(index)]["ROT"] = str(min(127, max(0, int(d.get("rot", "0")))))
                    ini["NativeProduction.Type." + str(index)]["BuildingRepairCost"] = str(building_repair_cost(d,rules,id) if building else 0)
                    human,other=building_sale_values(d,rules,id) if building else (0,0)
                    ini["NativeProduction.Type." + str(index)].update(BuildingSaleHuman=str(human),BuildingSaleOther=str(other))
                ini["NativeProduction.Type." + str(index)].update(production_economy(d, rules, id, edition))
                ini["NativeProduction.Type." + str(index)].update(
                    CrateGoodie=str(int(yes(d.get("crategoodie", "no")))),
                    StartAllowed=str(int(yes(d.get("allowedtostartinmultiplayer", "yes")))))
    if edition == "ra2":
        ini["NativeSaleSurvivors"] = {"Version": "1", **{key: general[key.lower()].upper()
            for key in ("AlliedSurvivorDivisor", "SovietSurvivorDivisor", "AlliedCrew", "SovietCrew", "Engineer", "Technician")}}
    ini["Scene"]["Types"] = str(len(types))
    export_mode_overlays(tree, edition, ini, types)
    export_native_crates(ini, {}, rules)
    crate = rules["craterules"]
    vehicles = [v.upper() for v in rules["vehicletypes"].values()]
    missing = [v for v in vehicles if yes(rules.get(v.lower(), {}).get("crategoodie", "no")) and v not in types]
    if missing:
        raise ValueError((edition, "unrepresented original unit-crate rewards", missing))
    ini["NativeCrates"].update(Version="2", Minimum=crate["crateminimum"], Maximum=crate["cratemaximum"],
        RegenMinutes=crate["crateregen"], FreeMCV=str(int(yes(crate["freemcv"]))), Vehicles=",".join(vehicles),
        BaseUnits=general["baseunit"].upper(), ConstructionYards=rules["ai"]["buildconst"].upper(),
        Harvesters=general["harvesterunit"].upper(), UnitCrateType=crate["unitcratetype"])
    for index, identity in enumerate(types):
        ini["Type." + str(index)]["Trainable"] = str(int(yes(rules[identity.lower()].get("trainable", "yes"))))
    abilities = {rules.get(name.lower(), {}).get('type', '').lower() for name in rules.get("superweapontypes", {}).values()}
    ini["NativeProduction"] = {"Version": "11" if edition == "ra2" else "6", "OreValue": rules["riparius"]["value"], "GemValue": rules["cruentus"]["value"],
        "SuperweaponMask": str(sum(1 << i for i, name in enumerate(SOURCE_SW_TYPES) if name.lower() in abilities)),
        "SpyMoneyFraction": general.get("spymoneystealpercent", ".5"), "SpyBlackoutTicks": str(int(general.get("spypowerblackout", "1000"))*2),
        "SpyTechByOwner": str(int(edition == "ra2")),
        "BuildSpeed": general["buildspeed"], "MultipleFactory": general.get("multiplefactory", "1"),
        "MinLowPowerProductionSpeed": general.get("minlowpowerproductionspeed", ".5"), "MaxLowPowerProductionSpeed": general.get("maxlowpowerproductionspeed", ".8"),
        "LowPowerPenaltyModifier": general.get("lowpowerpenaltymodifier", "1"), "MaximumQueuedObjects": general.get("maximumqueuedobjects", "29")}
    if edition == "ra2":
        # Independently called local RA2 constructor defaults to2; the original
        # MissionHarvest gate consumes nine stages before each pickup.
        ini["NativeProduction"]["HarvesterLoadRate"] = general.get("harvesterloadrate", "2")
        from decimal import Decimal, ROUND_CEILING
        ini["NativeProduction"]["HarvesterDumpFrames"] = str(int((Decimal(general.get("harvesterdumprate", ".016"))*900).to_integral_value(rounding=ROUND_CEILING)))
        ini["NativeProduction"]["PurifierBonus"] = general.get("purifierbonus", ".25")
        ini["NativeProduction"]["BuildingRepairFrames"] = str(int(Decimal(general.get("repairrate", ".016"))*900))
        ini["NativeProduction"]["BuildingRepairStep"] = general.get("repairstep", "8")
    ini["NativeProduction.Prerequisites"] = {key[12:].upper(): value for key, value in general.items() if key.startswith("prerequisite")}
    country_keys = ["", "Americans", "Alliance", "French", "Germans", "British", "Russians", "Confederation", "Africans", "Arabs", "", "YuriCountry"]
    ini["Rules.Countries"] = {str(i): key for i, key in enumerate(country_keys) if key and key.lower() in rules}
    ini["Rules.CountrySpeed"] = {str(i): rules.get(key.lower(), {}).get("speedunitsmult", "1") for i, key in enumerate(country_keys) if key}
    scenario = {"houses": {str(i): "rules_house_" + str(i) for i in range(8)}}
    for i in range(8):
        scenario["rules_house_" + str(i)] = {"country": "Americans"}
        ini["NativeProduction.House." + str(i)] = {"Country": "Americans", "TechLevel": "10"}
    export_native_targets(ini, rules, {})
    export_native_weapons(ini, rules, {}, staged_gatling=True)
    export_native_weapon_catalog(ini, rules, {})
    # Aircraft and jumpjet movement remain distinct from the ground locomotor.
    export_native_movement(ini, scenario, rules, allow_air=True)
    export_native_jumpjet(ini, scenario, rules)
    export_native_rockets(ini, types, rules, {}, art, include_yr=edition == "yr", legacy_patriot=False)
    ini["NativeAircraft"] = {"Version": "2", "Count": "2"}
    for i, identity in enumerate(("CARRIER", "DEST")):
        owner = rules[identity.lower()]
        ini["NativeAircraft." + str(i)] = {"Launcher": str(types.index(identity)), "Aircraft": str(types.index(owner["spawns"].upper())),
            "Capacity": owner["spawnsnumber"], "Regenerate": owner["spawnregenrate"], "Reload": owner["spawnreloadrate"], "LaunchDelay": "20",
            "WeaponSlot": str(i), "UnderwaterOnly": str(i), "NoSpawnAlt": str(int(yes(owner.get("nospawnalt", "no"))))}
    output = Path(output);output.parent.mkdir(parents=True, exist_ok=True)
    from native_voxel_art import missile_poses, launcher_art, provenance
    from ra2lib import load_pal
    get = lambda name: source_asset(tree, edition, name)
    def support_art(name):
        roots = (["expandmd01.mix", "langmd.mix/", "ra2md.mix/"] if edition == "yr" else []) + ["language.mix/", "ra2.mix/"]
        for root in roots:
            copies = sorted((path, raw) for path, raw in tree.find_all(name) if path.lower() == root or path.lower().startswith(root))
            if copies: return copies[0]
        raise FileNotFoundError(('original support art', edition, name))
    export_support_rules(ini, rules, edition, catalog["edition"], output, support_art)
    palette = load_pal(get("unittem.pal")[1]);actor_dir = output.parent / catalog["edition"] / "actors"
    from native_ifv_art import export_ifv_art
    export_ifv_art(ini, get, actor_dir.parent / "ifv")
    actor_report = {"edition": catalog["edition"], "palette": provenance(get, ["unittem.pal"]), "actors": {}}
    for identity in launchers + missiles + ["DEST", "ASW", "PDPLANE"] + (["SPYP"] if edition == "yr" else []):
        definition = rules[identity.lower()];image = definition.get("image", identity).lower()
        alternate = yes(definition.get("nospawnalt", "no"));names = [image + ".vxl", image + ".hva"]
        if identity in missiles:
            fields = missile_poses(get, identity, image, palette, actor_dir)
        else:
            fields = launcher_art(get, identity, image, palette, actor_dir, alternate)
            if alternate:
                names += [image + "wo.vxl", image + "wo.hva"]
        ini["Type." + str(types.index(identity))].update(fields)
        actor_report["actors"][identity] = {"image": image, "layout": fields, "sources": provenance(get, names)}
    (actor_dir.parent / "manifest.json").write_text(json.dumps(actor_report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        ini.write(stream)
    return catalog


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("edition", choices=("ra2", "yr"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--scenario", help="explicit original map layer; never applied to a base catalog")
    parser.add_argument("--profile", action="store_true", help="write an executable source rules profile instead of JSON")
    args = parser.parse_args()
    if args.profile:
        if args.scenario:
            parser.error("a skirmish profile cannot contain a campaign map layer")
        catalog = export_profile(MixTree(), args.edition, args.output)
        print(f"SOURCE PROFILE edition={catalog['edition']} output={args.output}")
        raise SystemExit(0)
    catalog = build_catalog(MixTree(), args.edition, args.scenario)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"SOURCE RULES edition={catalog['edition']} units={len(catalog['units'])} buildings={len(catalog['buildings'])} unmapped={len(catalog['unmapped'])}")
