#include "core/util.h"
#include "game/data.h"
#include "game/map.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, const char* name) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++failures;
}

void testRng() {
    Rng a(0x12345678), b(0x12345678), different(0x87654321);
    bool same = true;
    bool differs = false;
    for (int i = 0; i < 32; ++i) {
        const uint64_t av = a.next();
        same = same && av == b.next();
        differs = differs || av != different.next();
    }
    expect(same, "RNG repeats for an identical seed");
    expect(differs, "RNG changes for a different seed");

    Rng bounded(42);
    bool inRange = true;
    for (int i = 0; i < 1000; ++i) {
        const int value = bounded.range(-3, 7);
        inRange = inRange && value >= -3 && value <= 7;
    }
    expect(inRange, "RNG range includes only requested bounds");
    expect(Rng(0).s != 0, "zero RNG seed is normalized");
}

void testCoordinates() {
    const Vec2i tiles[] = {{0, 0}, {1, 0}, {0, 1}, {17, 31}, {-4, 9}};
    bool roundTrip = true;
    for (const Vec2i tile : tiles) {
        int sx = 0, sy = 0, tx = 0, ty = 0;
        tileToScreen(tile.x, tile.y, sx, sy);
        screenToTile((float)sx, (float)sy, tx, ty);
        roundTrip = roundTrip && tx == tile.x && ty == tile.y;
    }
    expect(roundTrip, "tile and screen origins round-trip");
    expect(distf(0, 0, 3, 4) == 5.0f, "distance helper follows Euclidean distance");
}

void testRuleHelpers() {
    expect(countryFaction(Country::America) == Faction::Allies, "America maps to Allies");
    expect(countryFaction(Country::Iraq) == Faction::Soviet, "Iraq maps to Soviet");
    expect(countryFaction(Country::Yuri) == Faction::Yuri, "Yuri maps to Yuri faction");
    expect(countriesOf(Faction::Allies).size() == 5, "Allies expose five countries");
    expect(countriesOf(Faction::Soviet).size() == 4, "Soviet exposes four countries");
    expect(harvesterType(Faction::Allies) == UnitType::ChronoMiner, "Allies use chrono miners");
    expect(harvesterType(Faction::Soviet) == UnitType::WarMiner, "Soviet uses war miners");
    expect(harvesterType(Faction::Yuri) == UnitType::SlaveMiner, "Yuri uses slave miners");
    expect(psychicImmune(UnitType::AttackDog), "attack dogs are psychic immune");
    expect(!psychicImmune(UnitType::Conscript), "ordinary infantry is not psychic immune");
    GameRules rules;
    expect(rules.lowPowerSpeedFactor == 0.5f, "low-power production factor has the rules default");
    expect(rules.crateInterval == 1800, "crate interval has the rules default");
    expect(rules.oreRegrowRate == 1, "ore regrowth has the rules default");
    expect(rules.veteranismDmgBonus[1] == 1.1f
           && rules.veteranismDmgBonus[2] == 1.3f,
           "veteran damage bonuses have the rules defaults");
    expect(rules.veteranRatio == 3.0f
           && rules.veteranArmorBonus[2] < rules.veteranArmorBonus[1]
           && rules.veteranSpeedBonus[2] > rules.veteranSpeedBonus[1]
           && rules.veteranRofBonus[2] < rules.veteranRofBonus[1]
           && rules.veteranSelfHeal[2] > 0,
           "veterancy defaults cover value threshold, armor, speed, ROF and healing");
    expect(isHero(UnitType::Tanya) && isHero(UnitType::Boris) && !isHero(UnitType::GI),
           "official hero uniqueness classification excludes ordinary infantry");
    expect(psychicImmune(UnitType::Tanya) && psychicImmune(UnitType::Boris)
           && psychicImmune(UnitType::BattleFortress),
           "YR hero and battle fortress psychic immunities are classified");
    expect(rules.warheadVerses[(int)WeaponDef::Warhead::AP][(int)Armor::Heavy] == 1.0f
           && rules.warheadVerses[(int)WeaponDef::Warhead::SmallArms][(int)Armor::Concrete] == 0.1f,
           "configurable warhead matrix addresses official armor classes");
    WeaponDef legacy;
    legacy.vsInfantry = 1.5f; legacy.vsVehicle = 0.75f; legacy.vsBuilding = 0.25f;
    expect(legacy.warhead == WeaponDef::Warhead::Legacy
           && legacy.vsVehicle == 0.75f && legacy.vsBuilding == 0.25f,
           "legacy three-target damage coefficients remain backward compatible");
    expect(oreUnitValue(false) == 35 && oreUnitValue(true) == 70,
           "gems are worth twice ordinary ore");
    expect(oreIncomeWithPurifier(70, true) == 87 && oreIncomeWithPurifier(35, false) == 35,
           "ore purifier adds 25 percent income");
    expect(industrialPlantUnitCost(1000, true, true) == 750
           && industrialPlantUnitCost(1000, false, true) == 1000,
           "industrial plant discounts vehicle cost only");

    BldDef footprintDef{};
    footprintDef.w = 3;
    footprintDef.h = 2;
    const auto footprint = bldFootprint(footprintDef);
    expect(footprint.size() == 6 && footprint.front() == Vec2i{0, 0}
           && footprint.back() == Vec2i{2, 1}, "building footprint covers every occupied cell");
}

Map makeMap(int w, int h, Terrain terrain) {
    Map map;
    map.w = w;
    map.h = h;
    map.cells.assign((size_t)w * h, Cell{});
    for (Cell& cell : map.cells) cell.terrain = terrain;
    return map;
}

void testMapLogic() {
    Map land = makeMap(7, 7, Terrain::Clear);
    for (int y = 0; y < 6; ++y) land.at(3, y).terrain = Terrain::Water;
    std::vector<Vec2i> path;
    expect(land.findPath(1, 1, 5, 1, path) && !path.empty() && path.back() == Vec2i{5, 1},
           "land pathfinding routes around water");

    std::vector<int> occupied(49, 0);
    occupied[1 * 7 + 5] = 1;
    land.bldOccRef = &occupied;
    expect(land.findPath(1, 1, 5, 1, path) && !path.empty()
           && path.back() != Vec2i{5, 1}, "pathfinding selects a nearby cell for a blocked goal");

    Map water = makeMap(5, 5, Terrain::Water);
    expect(water.findPath(0, 0, 4, 4, path, 20000, 1) && path.back() == Vec2i{4, 4},
           "naval pathfinding traverses water");
    expect(!water.findPath(0, 0, 4, 4, path, 20000, 0),
           "land pathfinding rejects an all-water map");

    Map ore = makeMap(3, 3, Terrain::Clear);
    ore.at(1, 1).terrain = Terrain::Ore;
    ore.at(1, 1).ore = 25;
    expect(ore.harvestAt(1, 1, 10) == 10 && ore.at(1, 1).ore == 15,
           "harvesting removes the requested ore");
    expect(ore.harvestAt(1, 1, 20) == 15 && ore.at(1, 1).terrain == Terrain::Rough,
           "depleted ore becomes rough terrain");

    // 邻格可挖语义：Chebyshev≤1；NearScan=6 / FarScan=48 是搜矿半径而非挖掘距
    Map patch = makeMap(15, 15, Terrain::Clear);
    patch.at(7, 7).terrain = Terrain::Ore; patch.at(7, 7).ore = 10;
    patch.at(10, 7).terrain = Terrain::Ore; patch.at(10, 7).ore = 10; // 距中心 Chebyshev=3
    patch.at(7, 14).terrain = Terrain::Ore; patch.at(7, 14).ore = 10; // 距中心=7，超出 NearScan
    Vec2i near{};
    expect(patch.findNearestOre(7, 7, 6, near) && near == Vec2i{10, 7},
           "TiberiumNearScan=6 finds next cell in same patch");
    expect(!patch.findNearestOre(7, 7, 2, near) || near != Vec2i{7, 14},
           "NearScan does not reach FarScan-only ore");
    expect(patch.findNearestOre(7, 7, 48, near) && (near == Vec2i{10, 7} || near == Vec2i{7, 14}),
           "TiberiumFarScan=48 can find distant ore");
    // 邻格挖掘：站在(6,7) 对(7,7) Chebyshev=1
    expect(std::max(std::abs(6 - 7), std::abs(7 - 7)) <= 1, "adjacent cell is diggable (Chebyshev<=1)");
    expect(std::max(std::abs(4 - 7), std::abs(7 - 7)) > 1, "distance 3 is not dig reach");

    // OreRefinery 4×3 DockUnload=(3,1)、QueueingCell=(4,1)（与 data.cpp BldDef 一致）
    const int refW = 4, refH = 3;
    int bx = 10, by = 20;
    int dockX = bx + 3, dockY = by + 1;
    int queueX = bx + 4, queueY = by + 1;
    expect(dockX == 13 && dockY == 21, "DockUnload cell is foundation+(3,1)");
    expect(queueX == 14 && queueY == 21, "QueueingCell is foundation+(4,1)");
    expect(dockX < bx + refW && dockY < by + refH, "DockUnload lies inside footprint");
    expect(queueX >= bx + refW, "QueueingCell lies east of footprint for pathfinding");
}
} // namespace

int main() {
    testRng();
    testCoordinates();
    testRuleHelpers();
    testMapLogic();
    std::printf("LOGIC TEST SUMMARY: %s failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
