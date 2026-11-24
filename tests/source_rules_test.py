"""Source provenance, edition isolation and real map-override regression."""
import configparser
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/ra2pack"))
from source_rules import build_catalog, select_source, export_profile, source_asset
from map_format import read_ini
from hashlib import sha256
from ra2lib import MixTree


def main():
    tree = MixTree()
    if not tree.find_all("rules.ini") or not (ROOT / "assets/maps/native/all01t/scene.ini").exists():
        print("SOURCE RULES SKIP: original rules and exported native scene required")
        return 77
    base = build_catalog(tree, "ra2")
    mission = build_catalog(tree, "ra2", "all01t.map")
    yr = build_catalog(tree, "yr")
    assert base["source"] == "ra2.mix/local.mix"
    crushing=json.loads((ROOT/"tests/fixtures/ra2-crushing-reference.json").read_text(encoding="utf-8"))
    assert base["sha256"]==crushing["sourceRulesSHA256"] and crushing["defaultInfantryCrushable"]
    if yr["source"] == "ra2md.mix/localmd.mix":
        assert yr["edition"] == "yr-local-base"  # No final-patch claim from a base archive.
    else:
        assert "expandmd01.mix" in yr["source"].lower() and yr["edition"] == "yr-expandmd01"
    assert "scenario" not in base and mission["scenario"]["file"] == "all01t.map"
    assert base["units"]["Conscript"][0]["guns"]["Gun0Damage"] == "15"
    assert mission["units"]["Conscript"][0]["guns"]["Gun0Damage"] == "10"
    assert base["units"]["Conscript"][0]["armor"] == "flak"
    assert base["units"]["Conscript"][0]["guns"]["Gun0CooldownTicks"] == "50"
    assert base["units"]["GI"][0]["guns"]["Gun0Damage"] == "15"
    assert base["units"]["GI"][0]["guns"]["Gun0CooldownTicks"] == "40"
    assert base["units"]["Grizzly"][0]["guns"]["Gun0CooldownTicks"] == "120"
    assert {item["id"] for item in yr["units"]["MCV"]} == {"AMCV", "SMCV", "PCV"}
    assert {item["id"] for item in yr["buildings"]["Barracks"]} == {"GAPILE", "NAHAND", "YABRCK"}
    assert "units.PLA" in yr["unmapped"] and "units.Type99" in yr["unmapped"]
    assert yr["buildings"]["PrismTower"][0]["id"] == "ATESLA"
    assert yr["buildings"]["TeslaCoil"][0]["id"] == "TESLA"
    scene = configparser.ConfigParser(interpolation=None);scene.optionxform = str
    scene.read(ROOT / "assets/maps/native/all01t/scene.ini", encoding="utf-8")
    compared = 0
    for name in ("GI", "Conscript", "Grizzly", "Tanya"):
        original = mission["units"][name][0]
        for section in scene.sections():
            row = scene[section]
            if section.startswith("Type.") and row.get("Id") == original["id"]:
                for key, value in original["guns"].items():
                    if not key.endswith("CooldownTicks"):
                        assert row[key] == value, (name, key)
                compared += 1
    assert compared == 4
    class Archives:
        def find_all(self, name):
            return [("ra2md.mix/localmd.mix", b"base"), ("expandmd01.mix", b"patch")]
    assert select_source(Archives(), "yr") == ("expandmd01.mix", b"patch", "yr-expandmd01")
    assert select_source(Archives(), "yr", "art") == ("expandmd01.mix", b"patch", "yr-expandmd01")
    with tempfile.TemporaryDirectory() as work:
        for edition, catalog in (("ra2", base), ("yr", yr)):
            output = Path(work) / (edition + ".ini")
            export_profile(tree, edition, output)
            profile = configparser.ConfigParser(interpolation=None);profile.optionxform = str
            profile.read(output, encoding="utf-8")
            scene = profile["Scene"]
            assert scene["Version"] == ("68" if edition == "ra2" else "43") and scene["Mode"] == "Rules" and scene["Map"] == ""
            carrier=profile["NativeAircraft.0"]
            assert profile["NativeAircraft"]["Version"] == "2" and profile["NativeAircraft"]["Count"] == "2" and carrier["Capacity"] == "3"
            assert carrier["Regenerate"] == "600" and carrier["Reload"] == "150" and carrier["LaunchDelay"] == "20"
            asw=profile["NativeAircraft.1"]
            assert asw["Capacity"] == "1" and asw["Regenerate"] == "400" and asw["Reload"] == "150"
            assert asw["WeaponSlot"] == asw["UnderwaterOnly"] == asw["NoSpawnAlt"] == "1"
            assert scene["Houses"] == scene["Actors"] == "0"
            assert scene["SourceSHA256"] == catalog["sha256"] and scene["SourceArtSHA256"] == catalog["art_sha256"]
            assert scene["SourceArchive"] == catalog["source"] and scene["SourceArtArchive"] == catalog["art_source"]
            types = {profile[name]["Id"]: profile[name] for name in profile.sections() if name.startswith("Type.")}
            if edition == "ra2":
                for reference in crushing["answers"]:
                    row=types[reference["id"]]
                    assert int(row["Armor"])==["none","flak","plate","light","medium","heavy"].index(reference["armor"])
                    assert row["Crusher"]==str(int(reference["crusher"]))
                    assert row["Crushable"]==str(int(reference["crushable"]))
                original=read_ini(select_source(tree,edition)[1])
                survivors=json.loads((ROOT/"tests/fixtures/ra2-sale-survivors-reference.json").read_text(encoding="utf-8"))
                assert survivors["sourceRulesSHA256"]==catalog["sha256"]
                assert dict(profile["NativeSaleSurvivors"]) == {"Version":"1", **{key:original["general"][key.lower()].upper()
                    for key in ("AlliedSurvivorDivisor","SovietSurvivorDivisor","AlliedCrew","SovietCrew","Engineer","Technician")}}
                assert types["CTECH"]["Base"]=="NativeInfantry" and types["CTECH"]["Hp"]=="50" and types["CTECH"]["Trainable"]=="0"
                assert types["CTECH"]["Gun0Present"]=="1" and profile["Type."+str(int(scene["Types"])-1)]["Id"]=="CTECH"
                for identity,row in types.items():
                    raw=original[identity.lower()]
                    assert row["SaleCrewed"]==str(int(raw.get("crewed","no").lower() in ("yes","true","1")))
                    assert row["Engineer"]==str(int(raw.get("engineer","no").lower() in ("yes","true","1")))
                assert dict(profile['NativeTransportUnload']) == {'Version':'1',
                    'RateFrames':str(int(float(original['unload']['rate'])*900)),
                    'RandomFrames':'2','MovingFrames':'10','StageFrames':'1'}
                assert dict(profile["NativeCrushWarning"]) == {"Version":"1",
                    "PlayerScatter":str(int(original["combatdamage"]["playerscatter"].lower()=="yes")),
                    "ComputerIQ":original["iq"]["maxiqlevels"],"ScatterIQ":original["iq"]["scatter"]}
                infantry={identity.upper() for identity in original["infantrytypes"].values()}
                yes=lambda value: str(value).lower() in ("yes","true","1")
                for identity,row in types.items():
                    if row["Building"]=="1":
                        assert "Crusher" not in row and "Crushable" not in row
                        continue
                    raw=original[identity.lower()]
                    assert row['IsTrain']==str(int(yes(raw.get('istrain','no'))))
                    assert row["Crusher"]==str(int(yes(raw.get("crusher","no"))))
                    assert row["Crushable"]==str(int(yes(raw.get("crushable","yes" if identity in infantry else "no"))))
                    assert row["Fraidycat"]==str(int(yes(raw.get("fraidycat","no"))))
                for identity in ("MTNK","HTNK","HTK","CMIN","HARV","SREF","MGTK"):
                    assert types[identity]["Crusher"]=="1"
                assert types["FV"]["Crusher"]=="0" and types["E2"]["Crushable"]=="1"
                assert types["CLEG"]["Crushable"]==types["SHK"]["Crushable"]==types["DESO"]["Crushable"]=="0"
            else:
                assert all("Crusher" not in row and "Crushable" not in row for row in types.values())
            assert {row["Id"] for row in types.values() if row.get("Base") == "AirForceCmd"} == {"GAAIRC", "AMRADR"}
            def production(identity):
                section = next(name for name in profile.sections() if name.startswith("Type.") and profile[name]["Id"] == identity)
                return profile["NativeProduction." + section]
            assert profile["NativeProduction"]["Version"] == ("11" if edition == "ra2" else "6")
            if edition == "ra2":
                assert profile["NativeProduction"]["HarvesterLoadRate"] == "2"
                assert profile["NativeProduction"]["HarvesterDumpFrames"] == "15"
                assert float(profile["NativeProduction"]["PurifierBonus"]) == .25
                for identity in ("MTNK", "HTNK", "APOC", "CMIN", "HARV"):
                    assert production(identity)["ROT"] == "5"
                repair = json.loads((ROOT/"tests/fixtures/ra2-building-repair-reference.json").read_text(encoding="utf-8"))
                assert base["sha256"] == repair["sourceRules"]["sha256"]
                assert profile["NativeProduction"]["BuildingRepairFrames"] == "14"
                assert profile["NativeProduction"]["BuildingRepairStep"] == "8"
                for identity, observed in repair["buildings"].items():
                    assert int(production(identity)["BuildingRepairCost"]) == observed["fee"], identity
                assert production("MTNK")["BuildingRepairCost"] == "0"
                sale = json.loads((ROOT/"tests/fixtures/ra2-building-sale-reference.json").read_text(encoding="utf-8"))
                assert base["sha256"] == sale["sourceRules"]["sha256"]
                for identity, observed in sale["buildings"].items():
                    assert int(production(identity)["BuildingSaleHuman"]) == observed["human"], identity
                    assert int(production(identity)["BuildingSaleOther"]) == observed["other"], identity
                yard = json.loads((ROOT/"tests/fixtures/ra2-conyard-sale-reference.json").read_text(encoding="utf-8"))
                assert base["sha256"] == yard["sourceRules"]["sha256"]
                for identity,observed in yard["types"].items():
                    assert types[identity]["TransformsTo"] == observed["undeploysInto"]
                    assert int(production(identity)["BuildingSaleHuman"]) == observed["humanRefund"]
                    assert int(production(identity)["BuildingSaleOther"]) == observed["otherRefund"]
                for identity,row in types.items():
                    if row["Building"]=="0":
                        assert production(identity)["BuildingSaleHuman"] == production(identity)["BuildingSaleOther"] == "0"
            else:
                assert "HarvesterLoadRate" not in profile["NativeProduction"]
                assert "BuildingRepairFrames" not in profile["NativeProduction"]
                assert "BuildingRepairCost" not in production("GAPOWR")
                assert all("BuildingSaleHuman" not in production(identity) and "BuildingSaleOther" not in production(identity) for identity in types)
            assert profile["NativeProduction"]["OreValue"] == "25" and profile["NativeProduction"]["GemValue"] == "50"
            assert float(profile["NativeProduction"]["SpyMoneyFraction"]) == .5
            assert profile["NativeProduction"]["SpyBlackoutTicks"] == "2000"
            assert profile["NativeProduction"]["SpyTechByOwner"] == str(int(edition == "ra2"))
            assert production("NACNST")["Spyable"] == "0"
            assert production("NAREFN")["SpyEffect"] == "1"
            assert production("NAPOWR")["SpyEffect"] == "2"
            assert production("NARADR")["SpyEffect"] == production("GAAIRC")["SpyEffect"] == "3"
            for lab, mask in [("GATECH", "1"), ("NATECH", "2")] + ([("YATECH", "4")] if edition == "yr" else []):
                assert production(lab)["SpyEffect"] == "6" and production(lab)["SpyTechMask"] == mask
            assert production("NAMISL")["SpyEffect"] == "7"
            assert production("GAPILE")["SpyEffect"] == "4" and production("GAWEAP")["SpyEffect"] == "5"
            assert production("NACLON")["Cloning"] == "1"
            assert int(profile["NativeProduction"]["SuperweaponMask"]) & 15 == 15
            assert int(profile["NativeProduction"]["SuperweaponMask"]) & 64 == (64 if edition == "yr" else 0)
            assert production("NAMISL")["SuperweaponMask"] == "1"
            assert production("NAIRON")["SuperweaponMask"] == "4"
            assert production("GATECH")["SuperweaponMask"] == ("64" if edition == "yr" else "0")
            assert production("NARADR")["SupportMask"] == ("4" if edition == "yr" else "0")
            assert production("AMRADR")["SupportMask"] == "2"
            assert production("CAAIRP")["SupportMask"] == "1"
            for i in range(4):
                support=profile['NativeProduction.Support.'+str(i)]
                available=edition=='yr' or i<2
                assert support['Available']==str(int(available))
                assert support['RechargeTicks']==('7200' if available else '0') and support['IsPowered']=='0'
                if available:
                    icon=Path(work)/catalog['edition']/'support'/Path(support['Icon']).name
                    assert icon.is_file() and sha256(icon.read_bytes()).hexdigest()==support['IconSHA256']
                else:assert support['Icon']==''
            assert profile['NativeProduction.Support.0']['AlliedCount']=='6' and profile['NativeProduction.Support.0']['SovietCount']=='9'
            assert profile['NativeProduction.Support.1']['SovietInfantry']=='E1' and profile['NativeProduction.Support.1']['SovietCount']=='8'
            assert production("CCOMAND")["RequiredStolenTech"] == "1"
            assert production("PTROOP")["RequiredStolenTech"] == ("4" if edition == "yr" else "2")
            assert production("CIVAN")["RequiredStolenTech"] == ("2" if edition == "yr" else "1")
            if edition == "yr":
                assert production("YAGRND")["Grinding"] == "1" and production("YAGRND")["Passengers"] == "0"
                assert production("YAPOWR")["InfantryAbsorb"] == "1" and production("YAPOWR")["Passengers"] == "5"
                assert production("YAPOWR")["ExtraPower"] == "100"
                for unit, refund in [("E1", "100"), ("BRUTE", "250"), ("SLAV", "0"), ("TANY", "750")]:
                    assert production(unit)["Soylent"] == refund
                assert production("NAINDP")["FactoryPlant"] == production("NAINDP")["Powered"] == "1"
                assert float(production("NAINDP")["UnitsCostBonus"]) == .75
            assert production("AMRADR")["RequiredHouses"] == "Americans" and production("GAAIRC")["ForbiddenHouses"] == "Americans"
            assert profile["Rules.Modes"]["Count"] == "7"
            source = read_ini(select_source(tree, edition)[1])
            delivery=profile['NativeProduction.Delivery']
            assert delivery['FlightLevel']==source['general']['flightlevel']
            assert delivery['ParachuteMaxFallRate']==source['general']['parachutemaxfallrate']
            assert delivery['ParadropRadius']==source['general']['paradropradius']
            for identity,base_type in [('PDPLANE','CargoPlane')] + ([('SPYP','SpyPlane')] if edition=='yr' else []):
                plane=types[identity];original=source[identity.lower()]
                assert plane['Base']==base_type and plane['Hp']==original['strength'] and plane['SourceAmmo']==original['ammo']
                assert plane['NativeBody']=='1' and plane['Selectable']==plane['CanPassiveAcquire']=='0'
                assert plane['FlightVersion']=='1' and plane['OriginalSpeed']==original['speed'] and plane['FlightROT']==original['rot']
            if edition=='yr':
                assert delivery['SpyRevealRadius']==source['spycameraweapon']['damage']
                assert delivery['SpyCameraRange']==source['spycameraweapon']['range']
            assert profile["NativeCrates"]["Version"] == "2"
            assert profile["NativeCrates"]["Vehicles"].split(",") == [v.upper() for v in source["vehicletypes"].values()]
            for identity, row in types.items():
                assert production(identity)["CrateGoodie"] == str(int(source[identity.lower()].get("crategoodie", "no").lower() in ["yes", "true", "1"]))
                assert row["Trainable"] == str(int(source[identity.lower()].get("trainable", "yes").lower() in ["yes", "true", "1"]))
            for name, value in profile["NativeCrates.Powerups"].items():
                assert value == source["powerups"][name.lower()]
            if edition == "yr":
                source_rules = read_ini(select_source(tree, "yr")[1])
                for identity in ["YTNK", "YAGGUN"]:
                    row, original = types[identity], source_rules[identity.lower()]
                    assert row["GunSelector"] == "4" and row["GunStages"] == "3"
                    assert row["GunRateUp"] == "1" and row["GunRateDown"] == "50"
                    assert [row["GunStage" + str(i)] for i in [1, 2, 3]] == ["200", "400", "600"]
                    assert [row["GunEliteStage" + str(i)] for i in [1, 2, 3]] == ["100", "200", "300"]
                    for stage in range(3):
                        keys = ["weapon" + str(stage*2+1), "weapon" + str(stage*2+2),
                                "eliteweapon" + str(stage*2+1), "eliteweapon" + str(stage*2+2)]
                        for offset, key in enumerate(keys):
                            prefix = "Gun" + str(stage*4+offset)
                            weapon = source_rules[original[key].lower()]
                            assert row[prefix + "Id"] == original[key]
                            assert row[prefix + "Damage"] == weapon["damage"] and row[prefix + "ROF"] == weapon["rof"]
                            assert row[prefix + "Range"] == weapon["range"] and row[prefix + "Burst"] == weapon["burst"]
            for mode, stem in [(3, "MPMW"), (5, "MPMeat"), (6, "MPNaval")]:
                prefix = "Rules.Mode." + str(mode)
                filename = stem + ("MD" if edition == "yr" else "") + ".ini"
                archive, raw_mode = source_asset(tree, edition, filename)
                original_mode = read_ini(raw_mode)
                assert profile[prefix]["File"] == filename and profile[prefix]["Archive"] == archive
                assert profile[prefix]["SHA256"] == sha256(raw_mode).hexdigest()
                overrides = {}
                for i in range(int(profile[prefix]["Overrides"])):
                    delta = profile[prefix + ".Override." + str(i)]
                    identity = profile["Type." + delta["Type"]]["Id"]
                    overrides[identity] = delta
                    for key, raw_key in [("TechLevel", "techlevel"), ("Prerequisite", "prerequisite")]:
                        if key in delta:
                            assert delta[key] == original_mode[identity.lower()][raw_key]
                if mode == 3:
                    assert overrides["GAWEAP"]["Prerequisite"] == ("GAPILE,GACNST" if edition == "yr" else "GAPILE")
                    assert overrides["AMRADR"]["Prerequisite"] == ("GAPILE,GACNST" if edition == "yr" else "GAPILE")
                    assert profile[prefix]["OreGrowth"] == ("0" if edition == "yr" else "1")
                elif mode == 5:
                    assert overrides["ORCA"]["TechLevel"] == overrides["GATECH"]["TechLevel"] == "11"
                    assert "BEAG" not in overrides and "GAAIRC" not in overrides and "AMRADR" not in overrides
                else:
                    assert overrides["MTNK"]["TechLevel"] == overrides["ZEP"]["TechLevel"] == "11"
            assert types["ORCA"]["FlightVersion"] == types["BEAG"]["FlightVersion"] == "1"
            assert types["ORCA"]["OriginalSpeed"] == "14" and types["ORCA"]["FlightROT"] == "3"
            assert types["HORNET"]["OriginalSpeed"] == types["ASW"]["OriginalSpeed"] == "12"
            assert types["HORNET"]["FlightROT"] == "3" and types["ASW"]["FlightROT"] == "5"
            jumpjets = {name for name, row in types.items() if row.get("JumpjetVersion") == "2"}
            assert jumpjets == ({"JUMPJET", "ZEP", "SHAD", "SCHP", "DISK"} if edition == "yr" else {"JUMPJET", "ZEP", "SHAD"})
            for name, speed, turn, climb, height, acceleration, crash in [
                ("JUMPJET", 30, 10, 20, 500, 10, 25), ("ZEP", 5, 2, 6, 750, 10, 12), ("SHAD", 30, 6, 10, 500, 12, 40)
            ] + ([("SCHP", 30, 6, 10, 500, 12, 40), ("DISK", 16, 100, 8, 750, 10, 15)] if edition == "yr" else []):
                row = types[name]
                for key, value in [("Speed", speed), ("TurnRate", turn), ("Climb", climb), ("Height", height), ("Acceleration", acceleration), ("Crash", crash)]:
                    assert float(row["Jumpjet" + key]) == value, (edition, name, key, row["Jumpjet" + key])
                assert row["SourceAir"] == "1" and row["JumpjetInfantry"] == str(int(name == "JUMPJET"))
                assert row["JumpjetBalloon"] == str(int(name in {"JUMPJET", "ZEP", "DISK"}))
            assert int(scene["Types"]) == len(types)
            assert types["HARV"]["Base"] == "WarMiner" and types["E2"]["Gun0Damage"] == "15"
            assert types["NAREFN"]["OriginalPlanningSide"] == "1" and types["SMCV"]["TransformsTo"] == "NACNST"
            assert types["NACNST"]["TransformsTo"] == "SMCV" and "SENGINEER" in types
            assert types["GACNST"]["AirObstacleHeight"] == "416" and types["NACNST"]["AirObstacleHeight"] == "624"
            assert types["GAWALL"]["Wall"] == "1" and types["GAWALL"]["AirObstacleHeight"] == "0"
            assert types["SPY"]["Gun0Range"] == "-2" and types["AEGIS"]["Gun0Speed"] == "120"
            assert types["SREF"]["Gun0Present"] == types["SREF"]["Gun2Present"] == "1"
            assert types["SREF"]["Gun0Id"] == "Comet" and types["SREF"]["Gun2Id"] == "SuperComet"
            assert types["SREF"]["Gun0Damage"] == "100" and types["SREF"]["Gun0Range"] == "10"
            assert types["ASW"]["Hp"] == "30" and types["ASW"]["SourceAmmo"] == "1" and types["ASW"]["Base"] == "ASW"
            assert types["ASW"]["NavalTargeting"] == "2" and types["ASW"]["LandTargeting"] == "1"
            assert types["DEST"]["Gun1Range"] == "-2" and types["DEST"]["Gun1AG"] == "0"
            assert types["DEST"]["ArtFrames"] == "16" and types["ASW"]["ArtFrames"] == "8"
            assert profile["NativeRockets"]["Count"] == ("3" if edition == "yr" else "2")
            assert types["DMISL"]["NativeRocket"] == "1" and types["DMISL"]["Hp"] == "50" and types["DMISL"]["ArtFrames"] == "136"
            assert profile["NativeRocket.0"]["Damage"] == "300" and profile["NativeRocket.0"]["Altitude"] == "768"
            assert int(scene["CountryMask"]) & (1 << 10) == 0
            assert bool(int(scene["CountryMask"]) & (1 << 11)) == (edition == "yr")
            if edition == "yr":
                assert types["YAREFN"]["OriginalPlanningSide"] == "2" and "YENGINEER" in types
                assert profile["NativeRocket.2"]["WeaponSlot"] == "1" and profile["NativeRocket.2"]["Range"] == "20"
    print(f"SOURCE RULES PASS map_override=1 aliases_preserved=1 native_profiles={compared} ra2_units={len(base['units'])} yr_units={len(yr['units'])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
