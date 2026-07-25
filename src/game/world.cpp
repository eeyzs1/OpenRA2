#include "game/world.h"
#include "game/lang.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

// ===================== 初始化 =====================
void World::init(int w, int h, uint64_t seed, int numHumans, int numAI, const std::vector<Faction>& factions, int mapType,
                 const char* mapFile, bool noStartForce) {
    // 全局状态复位：支持局内"重新开始"不留残局
    ents.clear();
    freeList.clear();
    projs.clear();
    effects.clear();
    nukes.clear();
    evaQueue.clear();
    tick = 0;
    rng = Rng(seed);
    numPlayers = numHumans + numAI;
    // P7：手工地图（地形/实体/出生点来自 maps/xxx.txt）；加载失败回退程序生成
    std::vector<Vec2i> spawns;
    std::vector<PendingEnt> pend;
    bool hand = mapFile && loadHandMap(mapFile, numPlayers, spawns, pend);
    if (mapFile && !hand) TraceLog(LOG_WARNING, "RA2 hand map failed, fallback to generate: %s", mapFile);
    if (!hand) map.generate(w, h, seed, numPlayers, spawns, mapType);
    map.initFog(numPlayers);
    bldOcc.assign((size_t)map.w * map.h, -1);
    map.bldOccRef = &bldOcc; // 寻路避开建筑占用
    players.assign(numPlayers, Player{});

    for (int i = 0; i < numPlayers; i++) {
        Player& p = players[i];
        p.active = true;
        p.isAI = i >= numHumans;
        p.faction = factions[i % factions.size()];
        p.colorId = i;
        p.money = 10000;
        p.name = p.isAI ? ("AI-" + std::to_string(i)) : (g_lang ? "Commander" : "指挥官");
        if (hand && noStartForce) continue; // 手工突击队地图：开局部队全部由地图文件放置
        // 出生点：一辆基地车 + 护卫
        Vec2i sp = spawns[i];
        EID mcv = spawnUnit(i, UnitType::MCV, (float)sp.x + 0.5f, (float)sp.y + 0.5f);
        (void)mcv;
        // 初始护卫坦克
        UnitType tankT = p.faction == Faction::Allies ? UnitType::Grizzly
                       : p.faction == Faction::Soviet ? UnitType::Rhino : UnitType::Type99;
        spawnUnit(i, tankT, sp.x + 2.5f, sp.y + 1.5f);
        spawnUnit(i, tankT, sp.x - 1.5f, sp.y + 2.5f);
        UnitType infT = p.faction == Faction::Allies ? UnitType::GI
                      : p.faction == Faction::Soviet ? UnitType::Conscript : UnitType::PLA;
        spawnUnit(i, infT, sp.x + 1.5f, sp.y - 1.5f);
        spawnUnit(i, infT, sp.x - 0.5f, sp.y + 3.5f);
        map.reveal(i, sp.x, sp.y, 10);
    }
    if (hand) {
        // 地图文件预置实体（建筑/单位；精炼厂附赠矿车与生产放置一致，保障 AI 经济启动）
        for (const PendingEnt& pe : pend) {
            if (pe.isBld) {
                BldType bt = (BldType)pe.typeIdx;
                spawnBuilding(pe.player, bt, pe.x, pe.y, true);
                if (bt == BldType::OreRefinery && pe.player >= 0) {
                    const BldDef& d = bldDef(bt);
                    for (int r = 1; r < 6; r++) {
                        int sx = pe.x + d.w / 2, sy = pe.y + d.h + r - 1;
                        if (map.passable(sx, sy) && !bldBlocked(sx, sy) && unitAtCell(sx, sy) == INVALID_EID) {
                            spawnUnit(pe.player, harvesterType(players[pe.player].faction), sx + 0.5f, sy + 0.5f);
                            break;
                        }
                    }
                }
            } else {
                EID uid = spawnUnit(pe.player, (UnitType)pe.typeIdx, pe.x + 0.5f, pe.y + 0.5f);
                if (pe.guard && valid(uid) && unitDef(ents[uid].utype).weapon.damage > 0)
                    ents[uid].guard = true;
            }
        }
        // 开局视野：每个玩家以其首个实体为中心揭示（无基地车部队的突击队关卡）
        for (int i = 0; i < numPlayers; i++)
            for (auto& e : ents)
                if (e.alive && e.player == i) { map.reveal(i, (int)e.x, (int)e.y, 10); break; }
    } else {
        placeNeutralTechs();
    }
}

// ===================== P7：手工地图加载 =====================
// 文本格式（# 后为注释，指令按序执行）：
//   size <w> <h>                    尺寸（须为首个指令）
//   fill <terrain>                  全图填充
//   rect <terrain> <x> <y> <w> <h>  矩形填充
//   blob <terrain> <cx> <cy> <r>    圆形填充（矿脉/湖泊）
//   deco <overlay> <x> <y> <w> <h> <n>  矩形内确定性撒布 n 个装饰（树/岩石）
//   spawn <player> <x> <y>          覆盖默认出生点（四角/边中）
//   unit <player> <type> <x> <y> [guard]  预置单位（-1 中立；guard 警戒驻守）
//   bld  <player> <type> <x> <y>    预置建筑（-1 中立科技建筑）
// terrain: clear rough water ore gems bridge    overlay: tree1 tree2 tree3 rock1 rock2
namespace hm {
const std::pair<const char*, Terrain> kTerr[] = {
    {"clear", Terrain::Clear}, {"rough", Terrain::Rough}, {"water", Terrain::Water},
    {"ore", Terrain::Ore}, {"gems", Terrain::Gems}, {"bridge", Terrain::Bridge},
};
const std::pair<const char*, Overlay> kOver[] = {
    {"tree1", Overlay::Tree1}, {"tree2", Overlay::Tree2}, {"tree3", Overlay::Tree3},
    {"rock1", Overlay::Rock1}, {"rock2", Overlay::Rock2},
};
const std::pair<const char*, UnitType> kUnit[] = {
    {"MCV", UnitType::MCV}, {"Harvester", UnitType::Harvester},
    {"GI", UnitType::GI}, {"Conscript", UnitType::Conscript}, {"PLA", UnitType::PLA},
    {"Engineer", UnitType::Engineer}, {"AttackDog", UnitType::AttackDog}, {"Spy", UnitType::Spy},
    {"FlakTrooper", UnitType::FlakTrooper}, {"TeslaTrooper", UnitType::TeslaTrooper},
    {"Sniper", UnitType::Sniper}, {"Tanya", UnitType::Tanya}, {"Desolator", UnitType::Desolator},
    {"Chrono", UnitType::Chrono}, {"GuardianGI", UnitType::GuardianGI}, {"CrazyIvan", UnitType::CrazyIvan},
    {"Grizzly", UnitType::Grizzly}, {"Rhino", UnitType::Rhino}, {"Type99", UnitType::Type99},
    {"FlakTrack", UnitType::FlakTrack}, {"IFV", UnitType::IFV},
    {"PrismTank", UnitType::PrismTank}, {"TeslaTank", UnitType::TeslaTank}, {"MirageTank", UnitType::MirageTank},
    {"V3Launcher", UnitType::V3Launcher}, {"Apocalypse", UnitType::Apocalypse}, {"TerrorDrone", UnitType::TerrorDrone},
    {"Intruder", UnitType::Intruder}, {"MiG", UnitType::MiG}, {"BlackEagle", UnitType::BlackEagle},
    {"Kirov", UnitType::Kirov}, {"Rocketeer", UnitType::Rocketeer},
    {"Destroyer", UnitType::Destroyer}, {"Typhoon", UnitType::Typhoon}, {"Aegis", UnitType::Aegis},
    {"SeaScorpion", UnitType::SeaScorpion}, {"Dreadnought", UnitType::Dreadnought},
    {"AircraftCarrier", UnitType::AircraftCarrier}, {"AmphTransport", UnitType::AmphTransport},
    {"ChronoMiner", UnitType::ChronoMiner}, {"WarMiner", UnitType::WarMiner},
    {"TankDestroyer", UnitType::TankDestroyer}, {"Terrorist", UnitType::Terrorist}, {"DemoTruck", UnitType::DemoTruck},
    {"Nighthawk", UnitType::Nighthawk}, {"Dolphin", UnitType::Dolphin}, {"Squid", UnitType::Squid},
    {"RobotTank", UnitType::RobotTank}, {"BattleFortress", UnitType::BattleFortress}, {"Hornet", UnitType::Hornet},
    {"NavySEAL", UnitType::NavySEAL}, {"Yuri", UnitType::Yuri},
    {"ChronoCommando", UnitType::ChronoCommando}, {"PsiCommando", UnitType::PsiCommando},
};
const std::pair<const char*, BldType> kBld[] = {
    {"ConYard", BldType::ConYard}, {"PowerPlant", BldType::PowerPlant}, {"TeslaReactor", BldType::TeslaReactor},
    {"NuclearReactor", BldType::NuclearReactor}, {"Barracks", BldType::Barracks}, {"WarFactory", BldType::WarFactory},
    {"OreRefinery", BldType::OreRefinery}, {"Radar", BldType::Radar}, {"BattleLab", BldType::BattleLab},
    {"AirForceCmd", BldType::AirForceCmd}, {"NavalYard", BldType::NavalYard},
    {"Pillbox", BldType::Pillbox}, {"SentryGun", BldType::SentryGun}, {"PrismTower", BldType::PrismTower},
    {"TeslaCoil", BldType::TeslaCoil}, {"FlakCannon", BldType::FlakCannon}, {"GrandCannon", BldType::GrandCannon},
    {"PatriotMissile", BldType::PatriotMissile}, {"Wall", BldType::Wall},
    {"OrePurifier", BldType::OrePurifier}, {"IndustrialPlant", BldType::IndustrialPlant},
    {"NukeSilo", BldType::NukeSilo}, {"WeatherDevice", BldType::WeatherDevice}, {"IronCurtain", BldType::IronCurtain},
    {"ChronoSphere", BldType::ChronoSphere},
    {"OilDerrick", BldType::OilDerrick}, {"Hospital", BldType::Hospital}, {"MachineShop", BldType::MachineShop},
    {"CloningVat", BldType::CloningVat}, {"ServiceDepot", BldType::ServiceDepot}, {"GapGenerator", BldType::GapGenerator},
    {"SpySat", BldType::SpySat}, {"PsychicSensor", BldType::PsychicSensor},
    {"BattleBunker", BldType::BattleBunker}, {"TankBunker", BldType::TankBunker},
    {"TechAirport", BldType::TechAirport}, {"SecretLab", BldType::SecretLab}, {"CivHouse", BldType::CivHouse},
};
template <typename T, size_t N>
bool lookup(const std::pair<const char*, T>(&tbl)[N], const char* name, T& out) {
    for (const auto& p : tbl)
        if (strcmp(p.first, name) == 0) { out = p.second; return true; }
    return false;
}
// 确定性散列（地图撒布/贴图变体：与种子无关，保证手工地图可复现）
uint64_t hash3(uint64_t a, uint64_t b, uint64_t c) {
    uint64_t x = a * 0x8C8674F5C7A5A5B5ull ^ b * 0xC2B2AE3D27D4EB4Full ^ c * 0x9E3779B97F4A7C15ull;
    x ^= x >> 29; x *= 0x9E3779B97F4A7C15ull; x ^= x >> 32;
    return x;
}
} // namespace hm

bool World::loadHandMap(const char* path, int numPlayers, std::vector<Vec2i>& spawns, std::vector<PendingEnt>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    bool ok = true, sized = false;
    char line[256];
    int lineNo = 0;
    auto bad = [&](const char* what) {
        TraceLog(LOG_WARNING, "RA2 hand map %s:%d bad %s: %s", path, lineNo, what, line);
        ok = false;
    };
    auto setTerr = [&](int x, int y, Terrain t) {
        if (!map.inBounds(x, y)) return;
        Cell& c = map.at(x, y);
        c.terrain = t;
        if (t == Terrain::Ore) c.ore = c.oreMax = 300;
        else if (t == Terrain::Gems) c.ore = c.oreMax = 150;
        else c.ore = c.oreMax = 0;
    };
    while (ok && fgets(line, sizeof(line), f)) {
        lineNo++;
        char* hash = strchr(line, '#');
        if (hash) *hash = 0;
        char kw[32];
        if (sscanf(line, "%31s", kw) != 1) continue;
        if (!strcmp(kw, "size")) {
            int W = 0, H = 0;
            if (sized || sscanf(line, "%*s %d %d", &W, &H) != 2 || W < 32 || H < 32 || W > 256 || H > 256) { bad("size"); break; }
            map.w = W; map.h = H;
            map.cells.assign((size_t)W * H, Cell{});
            sized = true;
            // 默认出生点：与 Map::generate 相同的四角 + 边中分布
            spawns.clear();
            int m = 10;
            const Vec2i corners[] = {
                {m, m}, {W - m - 1, H - m - 1}, {W - m - 1, m}, {m, H - m - 1},
                {W / 2, m}, {W / 2, H - m - 1}, {m, H / 2}, {W - m - 1, H / 2},
            };
            for (int i = 0; i < numPlayers && i < 8; i++) spawns.push_back(corners[i]);
        } else if (!sized) {
            bad("directive before size");
            break;
        } else if (!strcmp(kw, "fill")) {
            char tn[32];
            Terrain t;
            if (sscanf(line, "%*s %31s", tn) != 1 || !hm::lookup(hm::kTerr, tn, t)) { bad("fill"); break; }
            for (int y = 0; y < map.h; y++)
                for (int x = 0; x < map.w; x++) setTerr(x, y, t);
        } else if (!strcmp(kw, "rect")) {
            char tn[32]; int x, y, rw, rh; Terrain t;
            if (sscanf(line, "%*s %31s %d %d %d %d", tn, &x, &y, &rw, &rh) != 5 || !hm::lookup(hm::kTerr, tn, t)) { bad("rect"); break; }
            for (int dy = 0; dy < rh; dy++)
                for (int dx = 0; dx < rw; dx++) setTerr(x + dx, y + dy, t);
        } else if (!strcmp(kw, "blob")) {
            char tn[32]; int cx, cy, r; Terrain t;
            if (sscanf(line, "%*s %31s %d %d %d", tn, &cx, &cy, &r) != 4 || !hm::lookup(hm::kTerr, tn, t)) { bad("blob"); break; }
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++)
                    if (dx * dx + dy * dy <= r * r) setTerr(cx + dx, cy + dy, t);
        } else if (!strcmp(kw, "deco")) {
            char tn[32]; int x, y, rw, rh, n; Overlay ov;
            if (sscanf(line, "%*s %31s %d %d %d %d %d", tn, &x, &y, &rw, &rh, &n) != 6 || !hm::lookup(hm::kOver, tn, ov)
                || rw <= 0 || rh <= 0 || n < 0) { bad("deco"); break; }
            for (int i = 0, placed = 0; i < n * 8 && placed < n; i++) {
                int px = x + (int)(hm::hash3((uint64_t)x, (uint64_t)y, (uint64_t)i * 2) % (uint64_t)rw);
                int py = y + (int)(hm::hash3((uint64_t)x, (uint64_t)y, (uint64_t)i * 2 + 1) % (uint64_t)rh);
                if (!map.inBounds(px, py)) continue;
                Cell& c = map.at(px, py);
                if (c.overlay != Overlay::None) continue;
                if (c.terrain != Terrain::Clear && c.terrain != Terrain::Rough) continue;
                c.overlay = ov;
                placed++;
            }
        } else if (!strcmp(kw, "spawn")) {
            int p, x, y;
            if (sscanf(line, "%*s %d %d %d", &p, &x, &y) != 3 || p < 0 || p >= numPlayers) { bad("spawn"); break; }
            spawns[p] = {x, y};
        } else if (!strcmp(kw, "unit")) {
            int p, x, y; char tn[32], extra[32] = "";
            UnitType ut;
            int got = sscanf(line, "%*s %d %31s %d %d %31s", &p, tn, &x, &y, extra);
            if (got < 4 || !hm::lookup(hm::kUnit, tn, ut) || p >= numPlayers) { bad("unit"); break; }
            PendingEnt pe;
            pe.isBld = false; pe.player = p; pe.typeIdx = (int)ut; pe.x = x; pe.y = y;
            pe.guard = got >= 5 && !strcmp(extra, "guard");
            out.push_back(pe);
        } else if (!strcmp(kw, "bld")) {
            int p, x, y; char tn[32];
            BldType bt;
            if (sscanf(line, "%*s %d %31s %d %d", &p, tn, &x, &y) != 4 || !hm::lookup(hm::kBld, tn, bt) || p >= numPlayers) { bad("bld"); break; }
            PendingEnt pe;
            pe.isBld = true; pe.player = p; pe.typeIdx = (int)bt; pe.x = x; pe.y = y;
            out.push_back(pe);
        } else {
            bad("unknown directive");
            break;
        }
    }
    fclose(f);
    if (!ok || !sized || (int)spawns.size() < numPlayers) return false;
    // 贴图变体：确定性散列（与 generate 的 rng.range(0,3) 同范围）
    for (int y = 0; y < map.h; y++)
        for (int x = 0; x < map.w; x++)
            map.at(x, y).variant = (uint8_t)(hm::hash3((uint64_t)x, (uint64_t)y, 7) & 3);
    return true;
}

// 中立科技建筑：随机撒布油井/医院/机械店/科技机场/秘密实验室/民房（player=-1，工程师占领后生效）
void World::placeNeutralTechs() {
    int area = map.w * map.h;
    int nOil = std::max(2, area / 1800);
    int nHosp = std::max(1, area / 3600);
    int nShop = std::max(1, area / 3600);
    int nAirport = area >= 90 * 90 ? 1 : 0;             // 大图保证至少 1 座科技机场
    int nLab = 1;                                       // 秘密实验室（解锁国家特色科技）
    int nHouse = std::max(4, area / 900);               // 民房集群散布（驻军掩体）
    struct Want { BldType t; int n; };
    const Want wants[] = {
        {BldType::OilDerrick, nOil}, {BldType::Hospital, nHosp}, {BldType::MachineShop, nShop},
        {BldType::TechAirport, nAirport}, {BldType::SecretLab, nLab}, {BldType::CivHouse, nHouse},
    };
    std::vector<Vec2i> placed;
    for (const Want& wnt : wants) {
        const BldDef& d = bldDef(wnt.t);
        for (int k = 0; k < wnt.n; k++) {
            for (int tries = 0; tries < 200; tries++) {
                int bx = rng.range(4, map.w - d.w - 4);
                int by = rng.range(4, map.h - d.h - 4);
                // 占地可通行且非矿脉
                bool ok = true;
                for (int dy = 0; dy < d.h && ok; dy++)
                    for (int dx = 0; dx < d.w && ok; dx++) {
                        const Cell& c = map.at(bx + dx, by + dy);
                        if (!c.passable() || c.ore > 0) ok = false;
                    }
                if (!ok) continue;
                // 与其他中立建筑保持距离，避免扎堆
                for (const Vec2i& p : placed)
                    if (abs(p.x - bx) < 8 && abs(p.y - by) < 8) { ok = false; break; }
                if (!ok) continue;
                // 避开玩家出生区（出生点 7 格内不放，避免堵住基地展开）
                for (const Ent& e : ents)
                    if (e.alive && e.player >= 0 && abs((int)e.x - bx) < 7 && abs((int)e.y - by) < 7) { ok = false; break; }
                if (!ok) continue;
                spawnBuilding(-1, wnt.t, bx, by, true);
                placed.push_back({bx, by});
                break;
            }
        }
    }
}

EID World::allocEnt() {
    if (!freeList.empty()) {
        int id = freeList.back(); freeList.pop_back();
        ents[id] = Ent{};
        ents[id].alive = true;
        return id;
    }
    ents.push_back(Ent{});
    ents.back().alive = true;
    return (int)ents.size() - 1;
}

EID World::spawnUnit(int player, UnitType t, float x, float y) {
    EID id = allocEnt();
    Ent& e = ents[id];
    e.isBuilding = false;
    e.player = player;
    e.utype = t;
    e.x = x; e.y = y;
    e.hp = unitDef(t).hp;
    e.dir = rng.range(0, 7);
    e.turretDir = e.dir;
    if (unitDef(t).isAir()) {
        e.ammo = unitDef(t).ammo;
        e.goalX = x; e.goalY = y;
        e.state = UState::Circling;
    }
    // 航空母舰：出厂自带满编舰载机（RA2 原作：3 架大黄蜂）
    if (t == UnitType::AircraftCarrier) e.cargo.assign(unitDef(t).cargoCap, UnitType::Hornet);
    return id;
}

EID World::spawnBuilding(int player, BldType t, int bx, int by, bool free_) {
    const BldDef& d = bldDef(t);
    EID id = allocEnt();
    Ent& e = ents[id];
    e.isBuilding = true;
    e.player = player;
    e.btype = t;
    e.x = (float)bx; e.y = (float)by;
    e.hp = d.hp;
    e.rallyX = bx + d.w / 2; e.rallyY = by + d.h + 1;
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++)
            bldOcc[cellIdx(bx + dx, by + dy)] = id + 1;
    if (!free_ && player >= 0) players[player].money -= d.cost;
    recomputePower();
    // 超武建筑落成：向其他玩家发出侦测警告（RA2 原作设定）
    if (bldProvidesSW(t) != SWType::COUNT && tick > 10) {
        for (int p = 0; p < numPlayers; p++)
            if (p != player) eva(p, TextFormat(TR(S::EvaDetectEnemySWFmt), bldName(t)));
    }
    return id;
}

void World::kill(EID id) {
    if (!valid(id)) return;
    Ent& e = ents[id];
    e.alive = false;
    freeList.push_back(id);
    if (e.isBuilding) {
        const BldDef& d = bldDef(e.btype);
        for (int dy = 0; dy < d.h; dy++)
            for (int dx = 0; dx < d.w; dx++) {
                int cx = (int)e.x + dx, cy = (int)e.y + dy;
                if (map.inBounds(cx, cy)) bldOcc[cellIdx(cx, cy)] = -1;
            }
        explodeAt(e.x + d.w / 2.0f, e.y + d.h / 2.0f, 2);
        recomputePower();
        // 驻军随建筑一同阵亡（RA2 原作设定）
        e.garrison.clear();
    } else {
        explodeAt(e.x, e.y, unitDef(e.utype).isInfantry() ? 0 : 1);
        // 基洛夫空艇被击落：坠毁冲击波（RA2 原作签名机制）
        if (e.utype == UnitType::Kirov) {
            explodeAt(e.x, e.y, 2);
            const float R = 3.0f;
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& o = ents[i];
                if (!o.alive || o.invuln > 0) continue;
                if (!o.isBuilding && unitDef(o.utype).isAir() && o.state != UState::Landed) continue;
                float ox = o.x, oy = o.y;
                if (o.isBuilding) { ox += bldDef(o.btype).w / 2.0f; oy += bldDef(o.btype).h / 2.0f; }
                float d = distf(ox, oy, e.x, e.y);
                if (d > R) continue;
                damage((int)i, (int)(400 * (1.0f - d / (R + 1.0f))), e.player);
            }
        }
        // 宿主被毁：寄生其上的恐怖机器人脱离并存活（RA2 原作设定）
        if (e.parasite != INVALID_EID && valid(e.parasite)) {
            Ent& dr = ents[e.parasite];
            dr.parasiting = false;
            dr.parasiteHost = INVALID_EID;
            dr.x = e.x; dr.y = e.y;
            dr.state = UState::Idle;
            dr.path.clear();
        }
        // 恐怖机器人被消灭（维修厂摘除）：解除宿主寄生标记
        if (e.parasiting && valid(e.parasiteHost)) ents[e.parasiteHost].parasite = INVALID_EID;
        // 心灵控制：被控单位阵亡 → 解除控制者链接（RA2 原作）
        if (e.mindBy != INVALID_EID && valid(e.mindBy) && ents[e.mindBy].mindTarget == id)
            ents[e.mindBy].mindTarget = INVALID_EID;
        // 控制者阵亡 → 被控单位恢复原属（RA2 原作）
        if (e.mindTarget != INVALID_EID) mindControlRelease(e);
    }
    checkDefeat();
}

// ===================== 查询 =====================
bool World::bldBlocked(int x, int y) const {
    if (!map.inBounds(x, y)) return true;
    return bldOcc[cellIdx(x, y)] > 0;
}

EID World::bldAt(int bx, int by) const {
    if (!map.inBounds(bx, by)) return INVALID_EID;
    int v = bldOcc[cellIdx(bx, by)];
    return v > 0 ? v - 1 : INVALID_EID;
}

EID World::unitAtCell(int x, int y) const {
    for (size_t i = 0; i < ents.size(); i++) {
        const Ent& e = ents[i];
        if (!e.alive || e.isBuilding) continue;
        if (e.parasiting) continue; // 寄生中的机器人附着在宿主上，不占格
        if (unitDef(e.utype).isAir() && e.state != UState::Landed) continue; // 飞行中不占格
        if ((int)e.x == x && (int)e.y == y) return (int)i;
    }
    return INVALID_EID;
}

bool World::hasBld(int player, BldType t) const {
    for (const Ent& e : ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == t) return true;
    return false;
}

int World::countUnits(int player, UnitType t) const {
    int n = 0;
    for (const Ent& e : ents)
        if (e.alive && !e.isBuilding && e.player == player && e.utype == t) n++;
    return n;
}

int World::countBlds(int player, BldType t) const {
    int n = 0;
    for (const Ent& e : ents)
        if (e.alive && e.isBuilding && e.player == player && e.btype == t) n++;
    return n;
}

bool World::prereqMet(int player, const BldDef& d) const {
    // 国家限制（RA2 原作：如巨炮仅法国可建）；秘密实验室占领后可解锁（见 capture 处理）
    if (d.countryReq != Country::None && players[player].country != d.countryReq
        && players[player].secretLabUnlock != (int)d.countryReq) return false;
    return d.prereq == BldType::COUNT || hasBld(player, d.prereq);
}

bool World::unitPrereqMet(int player, const UnitDef& u) const {
    // 国家限制（RA2 原作：如狙击手仅英国、磁能坦克仅苏俄）；秘密实验室解锁亦放行
    if (u.countryReq != Country::None && players[player].country != u.countryReq
        && players[player].secretLabUnlock != (int)u.countryReq) return false;
    // 偷科技单位（RA2 原作：间谍渗透敌作战实验室后解锁，见 applySpyEffect）
    int stBit = stolenTechBit(u.type);
    if (stBit && !(players[player].stolenTech & stBit)) return false;
    return u.prereq == BldType::COUNT || hasBld(player, u.prereq);
}

bool World::hasFactoryFor(int player, const UnitDef& u) const {
    for (const Ent& e : ents)
        if (e.alive && e.isBuilding && e.player == player && isFactoryFor(e.btype, u)) return true;
    return false;
}

EID World::findNearestEnemy(int player, float x, float y, float maxR, bool includeBlds, const WeaponDef* w, UnitType seeker) {
    EID best = INVALID_EID;
    float bd = maxR;
    for (size_t i = 0; i < ents.size(); i++) {
        const Ent& e = ents[i];
        if (!e.alive || e.player < 0 || !isEnemy(player, e.player)) continue;
        if (e.parasiting && e.utype == UnitType::TerrorDrone) continue; // 寄生中的机器人不可被索敌（乌贼缠绕仍可被攻击摘除）
        if (e.isBuilding && !includeBlds) continue;
        if (!e.isBuilding) {
            if (e.camouflaged) continue; // 幻影伪装：无法被自动索敌（手动点选仍可）
            // 间谍伪装：除军犬外无法被自动索敌（RA2 原作：军犬嗅探）
            if (e.utype == UnitType::Spy && seeker != UnitType::AttackDog) continue;
            // 台风潜艇下潜隐身：仅反潜探测单位在 7 格内、或任何单位贴脸（2.5 格）可发现
            if (e.utype == UnitType::Typhoon && e.subReveal <= 0) {
                float sd = distf(x, y, e.x, e.y);
                if (!(isDetector(seeker) && sd <= 7.0f) && sd > 2.5f) continue;
            }
            // 心灵控制者索敌：跳过免疫目标与已被控制单位（RA2 原作）
            if ((seeker == UnitType::Yuri || seeker == UnitType::PsiCommando)
                && (psychicImmune(e.utype) || e.mindBy != INVALID_EID)) continue;
        }
        // 尤里无法控制建筑（心灵突击队有 C4 可炸建筑，不在此过滤）
        if (e.isBuilding && seeker == UnitType::Yuri) continue;
        // 武器射界过滤：空中目标需 antiAir，地面目标需 antiGround
        if (w) {
            bool airT = !e.isBuilding && unitDef(e.utype).isAir() && e.state != UState::Landed;
            if (airT && !w->antiAir) continue;
            if (!airT && !w->antiGround) continue;
            if (w->navalOnly) {
                // 鱼雷类：仅水上目标（舰船或水上建筑）
                bool onWater = e.isBuilding
                    ? map.at((int)e.x + bldDef(e.btype).w / 2, (int)e.y + bldDef(e.btype).h / 2).terrain == Terrain::Water
                    : map.at((int)e.x, (int)e.y).terrain == Terrain::Water;
                if (!onWater) continue;
            }
        }
        float ex = e.x, ey = e.y;
        if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
        float d = distf(x, y, ex, ey);
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

// 反潜探测单位（驱逐舰/神盾舰/海蝎/海豚，RA2 原作为驱逐舰声呐/海豚）
bool World::isDetector(UnitType t) const {
    return t == UnitType::Destroyer || t == UnitType::Aegis || t == UnitType::SeaScorpion || t == UnitType::Dolphin;
}

// 单位可见性：潜艇隐身时仅本家/探测单位/贴脸可见
bool World::visibleTo(const Ent& e, int viewer) const {
    if (e.parasiting) return e.player == viewer || e.utype == UnitType::Squid; // 寄生机器人仅本家可见；乌贼缠绕可见（可被攻击摘除）
    if (e.player == viewer || viewer < 0) return true;
    if (e.isBuilding) return true;
    if (e.utype == UnitType::Typhoon && e.subReveal <= 0) {
        for (const Ent& o : ents) {
            if (!o.alive || o.isBuilding || o.player != viewer) continue;
            if (isDetector(o.utype) && distf(o.x, o.y, e.x, e.y) <= 7.0f) return true;
            if (distf(o.x, o.y, e.x, e.y) <= 2.5f) return true;
        }
        return false;
    }
    return true;
}

// IFV 载兵武器（RA2 原作：多功能步兵车武器随乘员改变；工程师=维修，见 updateUnit）
static const WeaponDef& ifvWeapon(UnitType cargo) {
    static const WeaponDef wGun{25, 6, 16, false, true, "bullet", 1.2f, 0.8f, 0.5f};   // 步兵机枪（默认）
    static const WeaponDef wSnp{60, 9, 60, false, true, "bullet", 1.0f, 0.05f, 0.05f}; // +狙击手：狙击炮
    static const WeaponDef wTsl{30, 6, 36, false, true, "tesla", 1.2f, 1.0f, 0.8f};    // +磁暴步兵：磁暴炮
    static const WeaponDef wFlk{14, 7, 20, true, true, "flak", 1.0f, 0.7f, 0.5f};      // +高射炮兵：加强防空
    static const WeaponDef wRad{50, 6, 30, false, true, "rad", 2.5f, 0.3f, 0.1f};      // +辐射工兵：辐射炮
    static const WeaponDef wMis{40, 7, 28, false, true, "missile", 0.3f, 1.6f, 0.5f};  // +重装大兵：反装甲导弹
    static const WeaponDef wTny{80, 7, 12, false, true, "bullet", 1.6f, 0.5f, 1.1f};   // +谭雅：重型机炮
    static const WeaponDef wChr{1, 7, 20, false, true, "chrono", 1.0f, 1.0f, 0.0f};    // +超时空军团兵：抹除炮
    switch (cargo) {
        case UnitType::Sniper: return wSnp;
        case UnitType::TeslaTrooper: return wTsl;
        case UnitType::FlakTrooper: return wFlk;
        case UnitType::Desolator: return wRad;
        case UnitType::GuardianGI: return wMis;
        case UnitType::Tanya: return wTny;
        case UnitType::Chrono: return wChr;
        default: return wGun;
    }
}

// 有效武器：部署形态 / IFV 载兵 / 精英军衔 综合
WeaponDef World::effWeapon(const Ent& e) const {
    if (e.isBuilding) return bldDef(e.btype).weapon;
    const UnitDef& ud = unitDef(e.utype);
    if (e.deployed && e.utype == UnitType::GuardianGI) return ggiDeployedWeapon();
    if (e.deployed && e.utype == UnitType::GI) return giDeployedWeapon();
    if (e.utype == UnitType::IFV && !e.cargo.empty()) return ifvWeapon(e.cargo[0]);
    if (e.vetRank >= 2 && ud.elite) return *ud.elite;
    WeaponDef w = ud.weapon;
    if (e.vetRank > 0) w.damage = (int)(w.damage * (1.0f + 0.15f * e.vetRank));
    return w;
}

// ===================== 指令 =====================
void World::orderMove(const std::vector<EID>& sel, float x, float y, bool attackMove) {
    int n = 0;
    for (EID id : sel) {
        if (!valid(id)) continue;
        Ent& e = ents[id];
        if (e.isBuilding) {
            continue;
        }
        const UnitDef& ud = unitDef(e.utype);
        e.target = INVALID_EID;
        e.guard = false;
        e.radDeployed = false; // 移动命令自动收起辐射部署
        e.deployed = false;    // 移动命令自动收起重装大兵部署
        e.goalX = x; e.goalY = y;
        // 目标点按单位散开（方阵）
        int cols = (int)ceilf(sqrtf((float)sel.size()));
        float ox = x + (n % cols - cols / 2) * 1.0f;
        float oy = y + (n / cols) * 1.0f;
        n++;
        if (ud.isAir()) {
            // 战机：直线飞行，无视地形
            e.goalX = ox; e.goalY = oy;
            e.orbitA = (float)(id % 8) * 0.785f;
            e.state = attackMove ? UState::AttackMoving : UState::Moving;
            continue;
        }
        if (e.utype == UnitType::Chrono) {
            // 超时空军团兵：传送移动（RA2 原作设定）
            if (chronoJump(e, ox, oy)) e.guard = attackMove; // 传送后警戒=攻击移动等效
            continue;
        }
        std::vector<Vec2i> path;
        if (map.findPath((int)e.x, (int)e.y, (int)ox, (int)oy, path, 20000, ud.pathDomain())) {
            e.path = std::move(path);
            e.pathIdx = 0;
            e.state = attackMove ? UState::AttackMoving : UState::Moving;
        } else {
            e.state = UState::Idle;
        }
        if (unitDef(e.utype).canHarvet() && !attackMove) {
            // 移动后恢复自动采矿由 updateHarvester 处理
            e.oreCell = {-1, -1};
        }
    }
}

void World::orderAttack(const std::vector<EID>& sel, EID target) {
    if (!valid(target)) return;
    const Ent& t = ents[target];
    float tx = t.x, ty = t.y;
    if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
    bool airT = !t.isBuilding && unitDef(t.utype).isAir() && t.state != UState::Landed;
    // 目标是否在水上（鱼雷类武器限定）
    bool waterT = t.isBuilding
        ? map.at((int)t.x + bldDef(t.btype).w / 2, (int)t.y + bldDef(t.btype).h / 2).terrain == Terrain::Water
        : map.at((int)t.x, (int)t.y).terrain == Terrain::Water;
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e.utype);
        if (ud.weapon.damage == 0) {
            // 间谍渗透（RA2 原作）：无武器但可指定敌方建筑为渗透目标
            if (e.utype == UnitType::Spy && t.isBuilding && isEnemy(e.player, t.player)) {
                e.target = target;
                e.guard = false;
                std::vector<Vec2i> path;
                map.findPath((int)e.x, (int)e.y, (int)tx, (int)ty, path, 20000, ud.pathDomain());
                e.path = std::move(path);
                e.pathIdx = 0;
                e.state = UState::Chasing;
            }
            continue;
        }
        // 射界检查：打空中目标需 antiAir，打地面需 antiGround；鱼雷仅限水上目标（IFV 按载兵武器判定）
        WeaponDef ew = effWeapon(e);
        if (airT && !ew.antiAir) continue;
        if (!airT && !ew.antiGround) continue;
        if (ew.navalOnly && !waterT) continue;
        e.target = target;
        e.guard = false;
        float d = distf(e.x, e.y, tx, ty);
        if (d <= ew.range) {
            e.state = UState::Attacking;
            e.path.clear();
        } else {
            if (ud.isAir()) {
                e.state = UState::Chasing; // 战机直飞，无需寻路
            } else {
                std::vector<Vec2i> path;
                map.findPath((int)e.x, (int)e.y, (int)tx, (int)ty, path, 20000, ud.pathDomain());
                e.path = std::move(path);
                e.pathIdx = 0;
                e.state = UState::Chasing;
            }
        }
    }
}

void World::orderHarvest(const std::vector<EID>& sel, int x, int y) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (!unitDef(e.utype).canHarvet()) continue;
        e.oreCell = {x, y};
        e.target = INVALID_EID;
        e.guard = false;
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, x, y, path);
        e.path = std::move(path);
        e.pathIdx = 0;
        e.state = UState::HarvestGo;
    }
}

void World::orderStop(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        // 乌贼主动松开缠绕（RA2 原作：可命令乌贼撤退）
        if (e.parasiting && e.utype == UnitType::Squid) {
            if (valid(e.parasiteHost)) ents[e.parasiteHost].parasite = INVALID_EID;
            e.parasiting = false;
            e.parasiteHost = INVALID_EID;
        }
        e.path.clear();
        e.target = INVALID_EID;
        e.guard = false;
        if (unitDef(e.utype).isAir()) {
            // 战机：停机中的保持停机，飞行中的改为原地盘旋
            if (e.state != UState::Landed) {
                e.goalX = e.x; e.goalY = e.y;
                e.state = UState::Circling;
            }
        } else {
            e.state = UState::Idle;
        }
    }
}

// X 散布：各自前往周围随机空格（规避范围伤害与碾压）
void World::orderScatter(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e.utype);
        if (ud.isAir()) continue; // 战机不散布
        int dom = ud.pathDomain();
        e.target = INVALID_EID;
        e.guard = false;
        // 在 2~4 格内找随机可通行落点
        for (int tries = 0; tries < 8; tries++) {
            int dx = rng.range(-4, 4), dy = rng.range(-4, 4);
            if (abs(dx) < 2 && abs(dy) < 2) continue;
            int nx = (int)e.x + dx, ny = (int)e.y + dy;
            if (!passableFor(nx, ny, dom) || bldBlocked(nx, ny) || unitAtCell(nx, ny) != INVALID_EID) continue;
            std::vector<Vec2i> path;
            if (map.findPath((int)e.x, (int)e.y, nx, ny, path, 20000, dom)) {
                e.path = std::move(path);
                e.pathIdx = 0;
                e.state = UState::Moving;
                e.goalX = nx + 0.5f; e.goalY = ny + 0.5f;
                break;
            }
        }
    }
}

// G 警戒：驻守原地，按视野半径索敌（比普通 Idle 的射程+2 更远）
void World::orderGuard(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (unitDef(e.utype).weapon.damage == 0) continue;
        e.path.clear();
        e.target = INVALID_EID;
        e.guard = true;
        if (!unitDef(e.utype).isAir()) e.state = UState::Idle;
    }
}

// 登船寻路目标：运输船所在格对步兵不可走（停在水面）时，取其附近最近的可走格
bool World::boardGoal(const Ent& t, int domain, int& gx, int& gy) const {
    gx = (int)t.x; gy = (int)t.y;
    if (passableFor(gx, gy, domain) && !bldBlocked(gx, gy)) return true;
    for (int r = 1; r <= 3; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int nx = (int)t.x + dx, ny = (int)t.y + dy;
                if (passableFor(nx, ny, domain) && !bldBlocked(nx, ny)) { gx = nx; gy = ny; return true; }
            }
    return false;
}

// 步兵登上运输载具：走到旁边后进入货舱
void World::orderBoard(const std::vector<EID>& sel, EID transportId) {
    if (!valid(transportId) || ents[transportId].isBuilding) return;
    const Ent& t = ents[transportId];
    if (unitDef(t.utype).cargoCap == 0) return;
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding || id == transportId) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e.utype);
        if (!ud.isInfantry()) continue; // 仅步兵可装载
        int gx, gy;
        if (!boardGoal(t, ud.pathDomain(), gx, gy)) continue; // 周围无可靠岸点
        e.target = transportId;
        e.guard = false;
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, gx, gy, path, 20000, ud.pathDomain());
        e.path = std::move(path);
        e.pathIdx = 0;
        e.state = UState::Boarding;
    }
}

// 运输载具卸下乘员：放到周围陆地空格（RA2 原作要求邻近陆地）
void World::orderUnload(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        // 先拷贝货舱清单（spawnUnit 可能触发 ents 扩容，引用会悬空）
        std::vector<UnitType> out;
        int owner = -1;
        float ex = 0, ey = 0;
        {
            Ent& e = ents[id];
            if (unitDef(e.utype).cargoCap == 0 || e.cargo.empty()) continue;
            out = e.cargo;
            e.cargo.clear();
            owner = e.player;
            ex = e.x; ey = e.y;
        }
        for (int r = 1; r <= 2 && !out.empty(); r++)
            for (int dy = -r; dy <= r && !out.empty(); dy++)
                for (int dx = -r; dx <= r && !out.empty(); dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = (int)ex + dx, ny = (int)ey + dy;
                    if (!map.passable(nx, ny) || map.at(nx, ny).terrain == Terrain::Water) continue;
                    if (bldBlocked(nx, ny) || unitAtCell(nx, ny) != INVALID_EID) continue;
                    spawnUnit(owner, out.back(), nx + 0.5f, ny + 0.5f);
                    out.pop_back();
                }
        // 放不下的乘员返还货舱
        bool allOut = out.empty();
        if (!allOut && valid(id)) ents[id].cargo = std::move(out);
        if (owner >= 0) {
            if (allOut) eva(owner, TR(S::EvaUnloadDone));
            else eva(owner, TR(S::EvaUnloadFail));
        }
    }
}

// 步兵/车辆进驻建筑（RA2 原作：民房/战斗碉堡进驻步兵，坦克碉堡进驻车辆，驻军后从内部向外射击）
void World::orderGarrison(const std::vector<EID>& sel, EID bldId) {
    if (!valid(bldId) || !ents[bldId].isBuilding) return;
    Ent& b = ents[bldId];
    const BldDef& bd = bldDef(b.btype);
    if (bd.garrisonCap == 0) return;
    int dom = garrisonDomain(b.btype); // 1 步兵 2 车辆
    if (b.player >= 0 && b.player != ents[sel.empty() ? 0 : sel[0]].player) return;
    // 建筑旁的可走格（驻军入口）
    int gx = -1, gy = -1;
    float cx = b.x + bd.w / 2.0f, cy = b.y + bd.h / 2.0f;
    for (int r = 1; r <= 3 && gx < 0; r++)
        for (int dy = -r; dy <= r && gx < 0; dy++)
            for (int dx = -r; dx <= r && gx < 0; dx++) {
                int nx = (int)cx + dx, ny = (int)cy + dy;
                if (map.passable(nx, ny) && !bldBlocked(nx, ny)) { gx = nx; gy = ny; }
            }
    if (gx < 0) return;
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e.utype);
        // 进驻类型匹配：碉堡类别决定可进驻对象
        if (dom == 1 && !ud.isInfantry()) continue;
        if (dom == 2 && (ud.isInfantry() || ud.isAir() || ud.pathDomain() != 0 || ud.canHarvet())) continue;
        if (dom == 0) continue;
        if ((int)b.garrison.size() >= bd.garrisonCap) break; // 驻满
        e.target = bldId;
        e.guard = false;
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, gx, gy, path, 20000, ud.pathDomain());
        e.path = std::move(path);
        e.pathIdx = 0;
        e.state = UState::Boarding; // 复用登船状态机：接近目标建筑后进驻
    }
}

// 建筑撤出驻军：放到周围可走格（中立民房撤空后恢复中立）
void World::orderUngarrison(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || !ents[id].isBuilding) continue;
        // 先拷贝驻军清单并清空（spawnUnit 可能触发 ents 扩容，引用会悬空）
        std::vector<UnitType> out;
        int owner = -1;
        BldType bt = BldType::COUNT;
        float cx = 0, cy = 0;
        {
            Ent& b = ents[id];
            if (b.garrison.empty()) continue;
            out = b.garrison;
            b.garrison.clear();
            owner = b.player;
            bt = b.btype;
            const BldDef& bd = bldDef(bt);
            cx = b.x + bd.w / 2.0f; cy = b.y + bd.h / 2.0f;
            if (bt == BldType::CivHouse) b.player = -1; // 中立民房撤空恢复中立
        }
        for (int r = 1; r <= 3 && !out.empty(); r++)
            for (int dy = -r; dy <= r && !out.empty(); dy++)
                for (int dx = -r; dx <= r && !out.empty(); dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = (int)cx + dx, ny = (int)cy + dy;
                    if (!map.passable(nx, ny) || map.at(nx, ny).terrain == Terrain::Water) continue;
                    if (bldBlocked(nx, ny) || unitAtCell(nx, ny) != INVALID_EID) continue;
                    spawnUnit(owner, out.back(), nx + 0.5f, ny + 0.5f);
                    out.pop_back();
                }
        // 放不下的驻军返还建筑（极少见：建筑被围死）
        if (!out.empty() && valid(id)) {
            Ent& b = ents[id];
            if (b.player < 0) b.player = owner; // 恢复归属以容纳驻军
            b.garrison = std::move(out);
        } else if (owner >= 0) {
            eva(owner, TR(S::EvaUnloadDone));
        }
    }
}

// 车辆开往维修厂：到位后持续扣钱维修，并摘除恐怖机器人寄生（RA2 原作）
void World::orderService(const std::vector<EID>& sel, EID depotId) {
    if (!valid(depotId) || !ents[depotId].isBuilding || ents[depotId].btype != BldType::ServiceDepot) return;
    Ent& d = ents[depotId];
    const BldDef& dd = bldDef(d.btype);
    float cx = d.x + dd.w / 2.0f, cy = d.y + dd.h / 2.0f;
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e.utype);
        if (ud.isInfantry() || ud.isAir() || ud.pathDomain() != 0 || ud.canHarvet()) continue; // 仅地面战斗车辆
        if (e.player != d.player) continue;
        if (e.hp >= ud.hp && e.parasite == INVALID_EID) continue; // 满血且无寄生
        e.target = depotId;
        e.guard = false;
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, (int)cx, (int)cy, path, 20000, 0);
        e.path = std::move(path);
        e.pathIdx = 0;
        e.state = UState::Moving; // 到位后在 updateUnit 中持续维修
    }
}

// 超时空传送：瞬移到目标点附近最近可走空格；距离越远相位不适（冻结）越久
bool World::chronoJump(Ent& e, float gx, float gy) {
    int bx = -1, by = -1;
    for (int r = 0; r <= 3 && bx < 0; r++)
        for (int dy = -r; dy <= r && bx < 0; dy++)
            for (int dx = -r; dx <= r && bx < 0; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int nx = (int)gx + dx, ny = (int)gy + dy;
                if (map.passable(nx, ny) && !bldBlocked(nx, ny) && unitAtCell(nx, ny) == INVALID_EID) {
                    bx = nx; by = ny;
                }
            }
    if (bx < 0) { e.state = UState::Idle; return false; }
    float dist = distf(e.x, e.y, bx + 0.5f, by + 0.5f);
    // 出发与到达的传送特效
    Effect w1; w1.kind = 9; w1.x = e.x; w1.y = e.y; w1.maxAge = 20; effects.push_back(w1);
    e.x = bx + 0.5f; e.y = by + 0.5f;
    e.path.clear();
    e.state = UState::Idle;
    e.tpSick = std::min(600, 30 + (int)(dist * 2.5f)); // RA2：跳跃越远冻结越久
    Effect w2; w2.kind = 9; w2.x = e.x; w2.y = e.y; w2.maxAge = 24; effects.push_back(w2);
    g_sfx.playAt(Sfx::Deploy, e.x, e.y);
    return true;
}

// 辐射工兵：部署/收起辐射区（部署后不能移动，持续范围伤害）
// 重装大兵：部署/收起反装甲炮（部署后不能移动、不可被碾压，射程与反甲伤害提升）
void World::orderRadDeploy(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (e.utype == UnitType::Desolator) {
            e.radDeployed = !e.radDeployed;
            if (e.radDeployed) {
                e.path.clear();
                e.target = INVALID_EID;
                e.guard = false;
                e.state = UState::Idle;
            }
        } else if (e.utype == UnitType::GuardianGI || e.utype == UnitType::GI) {
            // 重装大兵反装甲炮 / 美国大兵沙袋工事：部署后不可移动，武器强化
            e.deployed = !e.deployed;
            if (e.deployed) {
                e.path.clear();
                e.target = INVALID_EID;
                e.guard = false;
                e.state = UState::Idle;
                g_sfx.playAt(Sfx::Deploy, e.x, e.y);
            }
        }
    }
}

// ===================== EVA 播报 =====================
void World::eva(int player, const std::string& text) {
    if (player < 0 || player >= numPlayers || players[player].defeated) return;
    if (evaQueue.size() > 24) evaQueue.pop_front(); // 防积压
    evaQueue.push_back({player, text});
}

void World::evaAll(const std::string& text) {
    for (int p = 0; p < numPlayers; p++) eva(p, text);
}

void World::orderDeploy(EID id) {
    if (!valid(id)) return;
    Ent& e = ents[id];
    if (e.utype != UnitType::MCV) return;
    int bx = (int)e.x - 1, by = (int)e.y - 1;
    // 首个建造厂不受建造半径限制，只检查占地
    const BldDef& d = bldDef(BldType::ConYard);
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int cx = bx + dx, cy = by + dy;
            if (!map.passable(cx, cy) || map.at(cx, cy).terrain == Terrain::Bridge) return;
            if (bldOcc[cellIdx(cx, cy)] > 0) return;
        }
    int pl = e.player;
    e.alive = false;
    freeList.push_back(id);
    spawnBuilding(pl, BldType::ConYard, bx, by, true);
    map.reveal(pl, bx + 1, by + 1, 8);
    g_sfx.playAt(Sfx::Deploy, (float)bx + 1, (float)by + 1);
}

void World::orderCapture(const std::vector<EID>& sel, EID bldId) {
    if (!valid(bldId) || !ents[bldId].isBuilding) return;
    Ent& b = ents[bldId];
    if (!bldDef(b.btype).capturable) return;
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (e.utype != UnitType::Engineer) continue;
        // 走到建筑旁
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, (int)b.x, (int)b.y, path);
        e.path = std::move(path);
        e.pathIdx = 0;
        e.target = bldId;
        e.state = UState::Chasing; // 到达判定在 updateUnit 处理工程师
    }
}

// 工程师修复己方受损建筑（进入后回满并消耗）
void World::orderRepair(const std::vector<EID>& sel, EID bldId) {
    if (!valid(bldId) || !ents[bldId].isBuilding) return;
    Ent& b = ents[bldId];
    if (b.hp >= bldDef(b.btype).hp) return; // 满血不修
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (e.utype != UnitType::Engineer) continue;
        if (e.player != b.player) continue;
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, (int)b.x, (int)b.y, path);
        e.path = std::move(path);
        e.pathIdx = 0;
        e.target = bldId;
        e.state = UState::Chasing;
    }
}

// ===================== 生产 =====================
// 分类生产队列（RA2 原作）：步兵/车辆/空军/海军各自独立排队，当前项空则立即开工，否则排入队尾
bool World::startUnitProd(int player, UnitType t) {
    Player& p = players[player];
    const UnitDef& u = unitDef(t);
    if (!unitPrereqMet(player, u)) return false;
    if (!hasFactoryFor(player, u)) return false;
    int cat = u.prodCat();
    ProdItem& pr = p.unitProd[cat];
    if (!pr.active) {
        if (p.money < u.cost) return false;
        pr.active = true;
        pr.isUnit = true;
        pr.typeIdx = (int)t;
        pr.progress = 0;
        pr.ready = false;
        return true;
    }
    // 排入队尾（排队项最多 7 个，含进行中共 8 个）
    if ((int)p.unitQueue[cat].size() >= 7) return false;
    p.unitQueue[cat].push_back((int)t);
    return true;
}

// 取消一个该类型：先取消队尾排队项，再取消进行中项（返还资金，队首递补）
void World::cancelUnitProd(int player, UnitType t) {
    Player& p = players[player];
    int cat = unitDef(t).prodCat();
    auto& q = p.unitQueue[cat];
    for (auto it = q.rbegin(); it != q.rend(); ++it) {
        if (*it == (int)t) { q.erase(std::next(it).base()); return; }
    }
    ProdItem& pr = p.unitProd[cat];
    if (pr.active && pr.typeIdx == (int)t) {
        const UnitDef& u = unitDef(t);
        int refunded = u.cost - u.cost * pr.progress / std::max(1, u.buildTime);
        p.money += refunded;
        pr = ProdItem{};
        if (!q.empty()) {
            int nt = q.front(); q.pop_front();
            pr.active = true; pr.isUnit = true; pr.typeIdx = nt; pr.progress = 0; pr.ready = false;
        }
    }
}

// 该类别排队总数（含进行中项）
int World::unitQueuedCount(int player, int cat) const {
    const Player& p = players[player];
    int n = (int)p.unitQueue[cat].size();
    if (p.unitProd[cat].active) n++;
    return n;
}

bool World::startBldProd(int player, BldType t) {
    Player& p = players[player];
    const BldDef& d = bldDef(t);
    if (p.bldProd.active) return false;
    if (!hasBld(player, BldType::ConYard)) return false;
    if (!prereqMet(player, d)) return false;
    if (p.money < d.cost) return false;
    p.bldProd.active = true;
    p.bldProd.isUnit = false;
    p.bldProd.typeIdx = (int)t;
    p.bldProd.progress = 0;
    p.bldProd.ready = false;
    return true;
}

void World::cancelProd(int player, bool isUnit) {
    // 单位取消走 cancelUnitProd（分类队列）；此处仅处理建筑生产
    if (isUnit) return;
    ProdItem& pr = players[player].bldProd;
    if (!pr.active) return;
    // 返还剩余造价
    int cost = bldDef((BldType)pr.typeIdx).cost;
    int time = bldDef((BldType)pr.typeIdx).buildTime;
    int refunded = cost - cost * pr.progress / std::max(1, time);
    players[player].money += refunded;
    pr = ProdItem{};
}

bool World::canPlace(BldType t, int bx, int by, int player) const {
    const BldDef& d = bldDef(t);
    bool naval = (t == BldType::NavalYard); // 船厂必须全建于水面
    // 占地检查
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int x = bx + dx, y = by + dy;
            if (naval) {
                if (!map.inBounds(x, y) || map.at(x, y).terrain != Terrain::Water) return false;
            } else {
                if (!map.passable(x, y)) return false;
                if (map.at(x, y).terrain == Terrain::Bridge) return false; // 桥面不可建筑（RA2 原作）
            }
            if (bldOcc[cellIdx(x, y)] > 0) return false;
        }
    // 必须靠近己方建筑（建造半径 6 格）
    bool nearBase = false;
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.player != player) continue;
        const BldDef& ed = bldDef(e.btype);
        float cx = e.x + ed.w / 2.0f, cy = e.y + ed.h / 2.0f;
        float nx = bx + d.w / 2.0f, ny = by + d.h / 2.0f;
        if (distf(cx, cy, nx, ny) < 8.0f) { nearBase = true; break; }
    }
    return nearBase;
}

bool World::placeBuilding(int player, BldType t, int bx, int by) {
    Player& p = players[player];
    if (!p.bldProd.active || !p.bldProd.ready || p.bldProd.typeIdx != (int)t) return false;
    if (!canPlace(t, bx, by, player)) return false;
    const BldDef& d = bldDef(t);
    spawnBuilding(player, t, bx, by, true); // 钱已在生产中扣除
    p.bldProd = ProdItem{};
    if (p.placingBld == t) p.placingBld = BldType::COUNT;
    g_sfx.playAt(Sfx::Place, (float)bx + d.w / 2.0f, (float)by + d.h / 2.0f);
    // 精炼厂附赠一辆采矿车（RA2 原作设定）：出生在厂房旁空格，Idle 后自动采矿
    // 矿车型号按阵营：盟军超时空矿车 / 苏军武装矿车 / 中国普通矿车
    if (t == BldType::OreRefinery) {
        for (int r = 1; r < 6; r++) {
            int sx = bx + d.w / 2, sy = by + d.h + r - 1;
            if (map.passable(sx, sy) && !bldBlocked(sx, sy) && unitAtCell(sx, sy) == INVALID_EID) {
                spawnUnit(player, harvesterType(players[player].faction), sx + 0.5f, sy + 0.5f);
                break;
            }
        }
    }
    return true;
}

void World::setRally(EID factory, int x, int y) {
    if (!valid(factory) || !ents[factory].isBuilding) return;
    ents[factory].rallyX = x;
    ents[factory].rallyY = y;
}

void World::sellBuilding(EID id) {
    if (!valid(id) || !ents[id].isBuilding) return;
    Ent& e = ents[id];
    if (e.player < 0) return; // 中立建筑不可出售
    players[e.player].money += bldDef(e.btype).cost / 2;
    g_sfx.playAt(Sfx::Sell, e.x, e.y);
    kill(id);
}

bool World::repairBuilding(EID id) {
    if (!valid(id) || !ents[id].isBuilding) return false;
    Ent& e = ents[id];
    if (e.player < 0) return false; // 中立建筑不可维修
    const BldDef& d = bldDef(e.btype);
    if (e.hp >= d.hp) return false;
    int missing = d.hp - e.hp;
    int cost = missing * d.cost / d.hp / 2;
    if (players[e.player].money < cost) return false;
    players[e.player].money -= cost;
    e.hp = d.hp;
    g_sfx.playAt(Sfx::Click, e.x, e.y);
    return true;
}

// ===================== 超级武器 =====================
bool World::swAvailable(int player, SWType t) const {
    if (player < 0 || player >= numPlayers || players[player].defeated) return false;
    return hasBld(player, swDef(t).fromBld);
}

bool World::launchSW(int player, SWType t, float tx, float ty) {
    Player& p = players[player];
    if (!swAvailable(player, t) || !p.swReady[(int)t]) return false;
    p.swReady[(int)t] = false;
    p.swCharge[(int)t] = 0;
    switch (t) {
        case SWType::Nuke: {
            Nuke n;
            n.active = true; n.player = player;
            n.tx = tx; n.ty = ty;
            n.timer = 75; // 2.5 秒落地
            nukes.push_back(n);
            evaAll(TR(S::EvaNukeLaunched));
            g_sfx.play(Sfx::NukeLaunch, 0.9f);
            break;
        }
        case SWType::Lightning: {
            p.stormTimer = 30 * 14; // 持续 14 秒
            p.stormX = tx; p.stormY = ty;
            p.stormBoltCd = 0;
            evaAll(TR(S::EvaStormComing));
            g_sfx.playAt(Sfx::Storm, tx, ty);
            break;
        }
        case SWType::IronCurtain: {
            // 目标点 3 格内己方单位/建筑无敌 20 秒；步兵无法承受铁幕能量直接死亡（RA2 原作设定）
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& e = ents[i];
                if (!e.alive) continue;
                float ex = e.x, ey = e.y;
                if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
                if (distf(ex, ey, tx, ty) > 3.0f) continue;
                if (!e.isBuilding && unitDef(e.utype).isInfantry()) { kill((int)i); continue; }
                if (e.player == player) e.invuln = 30 * 20;
            }
            g_sfx.playAt(Sfx::IronCurtain, tx, ty);
            // 铁幕扩散特效
            Effect ef;
            ef.kind = 8; ef.x = tx; ef.y = ty; ef.maxAge = 40;
            effects.push_back(ef);
            break;
        }
        case SWType::ChronoShift: {
            // 将超时空传送仪周边 8 格内的己方车辆传送至目标点（AI 用；人类玩家由 HUD 以选中单位调用 chronoShiftUnits）
            EID sphere = INVALID_EID;
            for (size_t i = 0; i < ents.size(); i++) {
                const Ent& e = ents[i];
                if (e.alive && e.isBuilding && e.player == player && e.btype == BldType::ChronoSphere) { sphere = (int)i; break; }
            }
            if (sphere != INVALID_EID) {
                const Ent& s = ents[sphere];
                float sx = s.x + 1.5f, sy = s.y + 1.0f;
                std::vector<EID> grp;
                for (size_t i = 0; i < ents.size(); i++) {
                    Ent& e = ents[i];
                    if (!e.alive || e.isBuilding || e.player != player) continue;
                    const UnitDef& ud = unitDef(e.utype);
                    if (ud.isInfantry() || ud.isAir()) continue;
                    if (distf(e.x, e.y, sx, sy) <= 8.0f) grp.push_back((int)i);
                }
                chronoShiftUnits(grp, tx, ty);
            }
            eva(player, TR(S::EvaChronoStart));
            break;
        }
        default: break;
    }
    return true;
}

void World::updateSW() {
    // 充能
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (!p.active || p.defeated) continue;
        for (int i = 0; i < (int)SWType::COUNT; i++) {
            if (p.swReady[i]) continue;
            if (!swAvailable(pi, (SWType)i)) { p.swCharge[i] = 0; continue; } // 建筑被毁清空
            if (p.lowPower()) continue; // 低电暂停充能
            if (++p.swCharge[i] >= swDef((SWType)i).chargeTime) {
                p.swReady[i] = true;
                eva(pi, TextFormat(TR(S::EvaSWReadyFmt), swName((SWType)i)));
                if (pi == 0) g_sfx.play(Sfx::SWReady, 0.85f);
            }
        }
    }

    // 核弹飞行与爆炸
    for (auto& n : nukes) {
        if (!n.active) continue;
        if (--n.timer > 0) continue;
        n.active = false;
        // 大范围伤害：中心 1000，半径 6 格递减
        const float R = 6.0f;
        for (size_t i = 0; i < ents.size(); i++) {
            Ent& e = ents[i];
            if (!e.alive) continue;
            float ex = e.x, ey = e.y;
            if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
            float d = distf(ex, ey, n.tx, n.ty);
            if (d > R) continue;
            int dmg = (int)(1000 * (1.0f - d / (R + 1.0f)));
            // 核爆辐射尘：半径内步兵直接致死（RA2 原作设定）
            if (!e.isBuilding && unitDef(e.utype).isInfantry()) dmg = 10000;
            // 铁幕单位免疫
            if (e.invuln > 0) continue;
            damage((int)i, dmg, n.player);
        }
        // 蘑菇云 + 冲击波 + 余波小爆
        Effect ef;
        ef.kind = 6; ef.x = n.tx; ef.y = n.ty; ef.maxAge = 110;
        effects.push_back(ef);
        for (int k = 0; k < 8; k++) {
            float a = k * 0.785f;
            Effect ex2;
            ex2.kind = 0;
            ex2.x = n.tx + cosf(a) * (2.0f + k % 3);
            ex2.y = n.ty + sinf(a) * (2.0f + k % 3);
            ex2.maxAge = 18;
            effects.push_back(ex2);
        }
        g_sfx.playAt(Sfx::NukeBlast, n.tx, n.ty);
    }
    nukes.erase(std::remove_if(nukes.begin(), nukes.end(), [](const Nuke& n) { return !n.active; }), nukes.end());

    // 闪电风暴：周期性随机落雷
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (p.stormTimer <= 0) continue;
        p.stormTimer--;
        if (--p.stormBoltCd > 0) continue;
        p.stormBoltCd = 11; // 每 11 帧一道雷
        float a = rng.unit() * 6.2832f;
        float r = sqrtf(rng.unit()) * 5.5f;
        float bx = p.stormX + cosf(a) * r;
        float by = p.stormY + sinf(a) * r * 0.7f;
        // 落雷伤害（半径 1.6，对地）
        for (size_t i = 0; i < ents.size(); i++) {
            Ent& e = ents[i];
            if (!e.alive) continue;
            if (!e.isBuilding && unitDef(e.utype).isAir() && e.state != UState::Landed) continue; // 打不到空中
            float ex = e.x, ey = e.y;
            if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
            if (distf(ex, ey, bx, by) > 1.6f) continue;
            if (e.invuln > 0) continue;
            damage((int)i, 130, pi);
        }
        // 天降闪电特效（从天上到地面）
        Effect ef;
        ef.kind = 7; ef.x = bx; ef.y = by; ef.maxAge = 9;
        effects.push_back(ef);
        explodeAt(bx, by, 0);
        g_sfx.playAt(Sfx::Lightning, bx, by);
    }
}

// ===================== 更新 =====================
void World::update() {
    tick++;
    updateSW();
    // EVA 播报节流倒计时 + 间谍效果计时
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (p.evaBaseCd > 0) p.evaBaseCd--;
        if (p.evaMinerCd > 0) p.evaMinerCd--;
        if (p.evaUnitCd > 0) p.evaUnitCd--;
        if (p.powerSabotage > 0) p.powerSabotage--;
        if (p.revealTimer > 0) p.revealTimer--;
    }
    spawnCrateTick();    // 周期性生成补给箱
    updateTimedBombs();  // 疯狂伊文炸弹倒计时
    regrowOre();         // 矿脉缓慢再生
    updateParadrop();    // 伞兵充能（美国空指部/科技机场）
    // 生产进度
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (!p.active || p.defeated) continue;
        float rate = p.lowPower() ? 0.5f : 1.0f; // 低电减产
        // 单位生产：步兵/车辆/空军/海军 4 类独立队列（RA2 原作）
        for (int cat = 0; cat < PROD_CAT_N; cat++) {
            ProdItem& pr = p.unitProd[cat];
            if (!pr.active) continue;
            const UnitDef& u = unitDef((UnitType)pr.typeIdx);
            // 多工厂加速：每个额外同类生产建筑 +50% 速度（上限 2.5x，RA2 原作设定）
            int fac = 0;
            for (const Ent& e : ents)
                if (e.alive && e.isBuilding && e.player == pi && isFactoryFor(e.btype, u)) fac++;
            float speed = rate * std::min(2.5f, 1.0f + 0.5f * std::max(0, fac - 1));
            float perTick = (float)u.cost / u.buildTime * speed;
            if (p.money <= 0) continue; // 资金不足暂停
            p.money -= (int)ceilf(perTick);
            pr.progress++;
            if (pr.progress >= (int)(u.buildTime / speed)) {
                spawnFromFactory(pi, u);
                pr = ProdItem{};
                // 队首递补
                if (!p.unitQueue[cat].empty()) {
                    int nt = p.unitQueue[cat].front();
                    p.unitQueue[cat].pop_front();
                    pr.active = true; pr.isUnit = true; pr.typeIdx = nt; pr.progress = 0; pr.ready = false;
                }
                if (pi == 0) g_sfx.play(Sfx::Ready, 0.7f); // 本家单位就绪提示
            }
        }
        // 建筑生产（单队列）
        {
            ProdItem& pr = p.bldProd;
            if (pr.active && !pr.ready) {
                const BldDef& d = bldDef((BldType)pr.typeIdx);
                float perTick = (float)d.cost / d.buildTime * rate;
                if (p.money > 0) {
                    p.money -= (int)ceilf(perTick);
                    pr.progress++;
                    if (pr.progress >= (int)(d.buildTime / rate)) {
                        pr.ready = true; // 建筑就绪等待放置（AI 会直接放）
                        if (pi == 0) g_sfx.play(Sfx::Ready, 0.7f);
                    }
                }
            }
        }
    }

    // 实体更新
    for (size_t i = 0; i < ents.size(); i++) {
        if (!ents[i].alive) continue;
        if (ents[i].isBuilding) updateBuilding(ents[i], (int)i);
        else updateUnit(ents[i], (int)i);
    }

    // 弹道
    for (auto& pr : projs) {
        if (!pr.alive) continue;
        // 大型导弹可被拦截（RA2 原作：爱国者/高炮/神盾在射程内击落 V3/无畏舰导弹）
        if (pr.hp > 0 && tick % 5 == 0) {
            for (size_t i = 0; i < ents.size(); i++) {
                const Ent& d = ents[i];
                if (!d.alive || d.player < 0 || !isEnemy(d.player, pr.player)) continue;
                WeaponDef dw = d.isBuilding ? bldDef(d.btype).weapon : unitDef(d.utype).weapon;
                if (!dw.antiAir || dw.damage <= 0) continue;
                if (d.isBuilding && players[d.player].lowPower()) continue; // 低电防御停摆
                float dx = d.x, dy = d.y;
                if (d.isBuilding) { dx += bldDef(d.btype).w / 2.0f; dy += bldDef(d.btype).h / 2.0f; }
                if (distf(dx, dy, pr.x, pr.y) > dw.range) continue;
                pr.hp -= 9;
                explodeAt(pr.x, pr.y, 0); // 拦截命中的小爆炸
                if (pr.hp <= 0) pr.alive = false; // 导弹被击落：凌空爆炸，不造成地面伤害
                break; // 每轮至多一个拦截火力
            }
            if (!pr.alive) continue;
        }
        float tx = pr.tx, ty = pr.ty;
        if (valid(pr.target)) {
            const Ent& t = ents[pr.target];
            tx = t.x; ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
        }
        float d = distf(pr.x, pr.y, tx, ty);
        float spd = pr.kind == ProjKind::Missile ? 0.25f : 0.6f;
        if (d < spd + 0.1f) {
            // 命中
            if (valid(pr.target)) {
                const Ent& t = ents[pr.target];
                float mult = 1.0f;
                if (t.isBuilding) mult = pr.w.vsBuilding;
                else mult = unitDef(t.utype).isInfantry() ? pr.w.vsInfantry : pr.w.vsVehicle;
                damage(pr.target, (int)(pr.w.damage * mult), pr.player, pr.src);
            }
            // 溅射伤害（V3 火箭等）：命中点范围内所有实体按距离衰减
            if (pr.w.splash > 0) {
                for (size_t i = 0; i < ents.size(); i++) {
                    Ent& o = ents[i];
                    if (!o.alive || (int)i == pr.target) continue;
                    if (o.player == pr.player) continue; // 不误伤己方
                    float ox = o.x, oy = o.y;
                    if (o.isBuilding) { ox += bldDef(o.btype).w / 2.0f; oy += bldDef(o.btype).h / 2.0f; }
                    float od = distf(tx, ty, ox, oy);
                    if (od > pr.w.splash) continue;
                    float mult = o.isBuilding ? pr.w.vsBuilding
                                 : (unitDef(o.utype).isInfantry() ? pr.w.vsInfantry : pr.w.vsVehicle);
                    float falloff = 1.0f - od / (pr.w.splash + 0.5f) * 0.6f; // 中心 100%，边缘 40%
                    damage((int)i, (int)(pr.w.damage * mult * falloff), pr.player, pr.src);
                }
            }
            if (pr.kind != ProjKind::Bullet) explodeAt(tx, ty, pr.kind == ProjKind::Missile ? 1 : 0);
            pr.alive = false;
        } else {
            pr.x += (tx - pr.x) / d * spd;
            pr.y += (ty - pr.y) / d * spd;
            if (++pr.trail > 600) pr.alive = false;
        }
    }
    projs.erase(std::remove_if(projs.begin(), projs.end(), [](const Projectile& p) { return !p.alive; }), projs.end());

    // 特效
    for (auto& ef : effects) if (ef.alive && ++ef.age >= ef.maxAge) ef.alive = false;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return !e.alive; }), effects.end());

    // 自动采矿（空闲采矿车找矿）
    for (size_t i = 0; i < ents.size(); i++) {
        Ent& e = ents[i];
        if (!e.alive || e.isBuilding || !unitDef(e.utype).canHarvet()) continue;
        if (e.state == UState::Idle) {
            Vec2i ore;
            if (map.findNearestOre((int)e.x, (int)e.y, 40, ore)) {
                e.oreCell = ore;
                std::vector<Vec2i> path;
                if (map.findPath((int)e.x, (int)e.y, ore.x, ore.y, path)) {
                    e.path = std::move(path); e.pathIdx = 0;
                    e.state = UState::HarvestGo;
                }
            }
        }
    }

    // 迷雾
    for (int pi = 0; pi < numPlayers; pi++) updateFog(pi);
    applyGapShroud(); // 裂缝产生器黑幕最后覆盖（RA2 原作：间谍卫星也无法穿透）
}

void World::updateUnit(Ent& e, EID id) {
    const UnitDef& ud = unitDef(e.utype);
    if (e.atkCd > 0) e.atkCd--;
    if (e.invuln > 0) e.invuln--;
    // 心灵控制链接维护：被控单位消失（运输装载/进驻等消耗路径）则清空控制者链接
    if (e.mindTarget != INVALID_EID && !valid(e.mindTarget)) e.mindTarget = INVALID_EID;
    if (e.mindBy != INVALID_EID && !valid(e.mindBy)) { e.player = e.origPlayer; e.mindBy = INVALID_EID; e.origPlayer = -1; }
    if (ud.isAir()) { updateAircraft(e, id); return; }
    // 超时空传送后相位不适：完全冻结
    if (e.tpSick > 0) { e.tpSick--; return; }
    // 超时空抹除进度：>0 期间冻结且免疫伤害；累积超阈值即抹除，否则缓慢衰减
    if (e.chrono > 0) {
        int threshold = ud.hp / 3 + 20;
        if (e.chrono >= threshold) {
            Effect ef; ef.kind = 9; ef.x = e.x; ef.y = e.y; ef.maxAge = 26; effects.push_back(ef);
            g_sfx.playAt(Sfx::Tesla, e.x, e.y);
            if (e.player >= 0 && players[e.player].evaUnitCd <= 0) {
                eva(e.player, TR(S::EvaUnitLost));
                players[e.player].evaUnitCd = 150;
            }
            e.alive = false;
            freeList.push_back(id);
            checkDefeat();
            return;
        }
        if (tick % 2 == 0) e.chrono--; // 衰减：停止攻击后约 2 倍时间恢复
        return; // 冻结中不能行动
    }
    // 恐怖机器人寄生中：附着宿主持续啃噬（RA2 原作：仅维修厂可摘除）
    if (e.parasiting) {
        if (!valid(e.parasiteHost)) {
            // 宿主已毁（kill 中通常会脱离复位；此处兜底）
            e.parasiting = false;
            e.state = UState::Idle;
            return;
        }
        Ent& h = ents[e.parasiteHost];
        e.x = h.x; e.y = h.y; // 跟随宿主
        if (tick % 18 == (uint64_t)(id % 18)) {
            damage(e.parasiteHost, 12, e.player, id);
            // 电火花特效提示宿主被寄生
            Effect sp; sp.kind = 2; sp.x = h.x; sp.y = h.y;
            sp.x2 = h.x + 0.4f; sp.y2 = h.y + 0.4f; sp.maxAge = 5;
            effects.push_back(sp);
        }
        return;
    }
    // 辐射工兵已部署：不能移动/普攻，周期性辐射范围伤害
    if (e.radDeployed) {
        if (tick % 12 == (uint64_t)(id % 12)) {
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& o = ents[i];
                if (!o.alive || o.isBuilding || (int)i == id) continue;
                if (unitDef(o.utype).isAir()) continue;
                if (o.utype == UnitType::Desolator) continue; // 辐射工兵免疫辐射
                if (distf(o.x, o.y, e.x, e.y) > 3.0f) continue;
                int dmg = unitDef(o.utype).isInfantry() ? 14 : 4;
                damage((int)i, dmg, e.player);
            }
            // 绿色辐射辉光
            Effect ef; ef.kind = 10;
            ef.x = e.x + (rng.unit() - 0.5f) * 4.0f; ef.y = e.y + (rng.unit() - 0.5f) * 4.0f;
            ef.maxAge = 22; effects.push_back(ef);
        }
        return;
    }
    // 幻影坦克：静止积累伪装成树（移动/开火解除，见 moveAlongPath/fireWeapon）
    if (e.utype == UnitType::MirageTank && !e.camouflaged && e.state == UState::Idle) {
        if (++e.camoTick >= 90) e.camouflaged = true;
    }
    // 精英军衔：缓慢自愈（RA2 原作设定）
    if (e.vetRank >= 2 && e.hp < ud.hp && tick % 45 == (uint64_t)(id % 45)) e.hp++;
    // 航空母舰：舰载机整备补充（RA2 原作：损失后缓慢再造，上限 3 架）
    if (e.utype == UnitType::AircraftCarrier && (int)e.cargo.size() < ud.cargoCap
        && tick % 240 == (uint64_t)(id % 240))
        e.cargo.push_back(UnitType::Hornet);
    // IFV + 工程师：维修车模式，周期修复周围受损友军车辆（RA2 原作签名组合）
    if (e.utype == UnitType::IFV && !e.cargo.empty() && e.cargo[0] == UnitType::Engineer
        && tick % 25 == (uint64_t)(id % 25)) {
        for (Ent& o : ents) {
            if (!o.alive || o.isBuilding || o.player != e.player) continue;
            const UnitDef& od = unitDef(o.utype);
            if (od.isInfantry() || od.isAir() || o.hp >= od.hp) continue;
            if (distf(o.x, o.y, e.x, e.y) > 4.0f) continue;
            o.hp = std::min(od.hp, o.hp + 20);
            Effect ef; ef.kind = 5; ef.x = o.x; ef.y = o.y; ef.maxAge = 6; effects.push_back(ef);
            break; // 每轮修一辆
        }
    }
    // 战斗要塞：载员轮流对外射击（RA2 原作：乘员从射击孔开火，射程+1）
    if (e.utype == UnitType::BattleFortress && !e.cargo.empty() && tick % 8 == (uint64_t)(id % 8)) {
        int slot = (int)((tick / 8 + id) % e.cargo.size());
        WeaponDef pw = unitDef(e.cargo[slot]).weapon;
        if (pw.damage > 0) {
            pw.range += 1;
            pw.damage = (int)(pw.damage * 1.1f);
            EID tgt = findNearestEnemy(e.player, e.x, e.y, (float)pw.range, true, &pw, e.cargo[slot]);
            if (tgt != INVALID_EID) {
                const Ent& t = ents[tgt];
                float tx = t.x, ty = t.y;
                if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
                Projectile p;
                p.kind = strcmp(pw.projSprite, "flak") == 0 ? ProjKind::Flak : ProjKind::Bullet;
                p.player = e.player;
                p.x = e.x; p.y = e.y; p.tx = tx; p.ty = ty; p.target = tgt; p.src = id; p.w = pw;
                projs.push_back(p);
            }
        }
    }
    // 台风潜艇：开火暴露计时衰减
    if (e.subReveal > 0) e.subReveal--;
    // 重装大兵/美国大兵已部署：不能移动，以强化武器迎战（RA2 原作设定）
    if ((e.utype == UnitType::GuardianGI || e.utype == UnitType::GI) && e.deployed) {
        const WeaponDef& dw = e.utype == UnitType::GuardianGI ? ggiDeployedWeapon() : giDeployedWeapon();
        if (!valid(e.target)) {
            e.target = findNearestEnemy(e.player, e.x, e.y, (float)dw.range, true, &dw, e.utype);
            if (e.target == INVALID_EID) { e.state = UState::Idle; return; }
        }
        const Ent& t = ents[e.target];
        float tx = t.x, ty = t.y;
        if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
        if (distf(e.x, e.y, tx, ty) > dw.range) { e.target = INVALID_EID; e.state = UState::Idle; return; }
        e.dir = dirFromVec(tx - e.x, ty - e.y);
        e.turretDir = e.dir;
        if (e.atkCd <= 0) { fireWeapon(e, id, e.target); e.atkCd = dw.cooldown; }
        return;
    }
    // 重伤冒烟
    if (e.hp < ud.hp / 2 && !ud.isInfantry() && tick % 25 == (uint64_t)(id % 25)) {
        Effect sm;
        sm.kind = 1; sm.x = e.x; sm.y = e.y; sm.maxAge = 30;
        effects.push_back(sm);
    }

    // 维修厂停靠维修（RA2 原作：车辆到位后持续扣钱回血，并摘除恐怖机器人寄生）
    if (!ud.isInfantry() && e.target != INVALID_EID && valid(e.target)
        && (e.state == UState::Idle || e.state == UState::Moving)) {
        Ent& dp = ents[e.target];
        if (dp.isBuilding && dp.btype == BldType::ServiceDepot && dp.player == e.player) {
            const BldDef& dd = bldDef(dp.btype);
            float cx = dp.x + dd.w / 2.0f, cy = dp.y + dd.h / 2.0f;
            if (distf(e.x, e.y, cx, cy) <= std::max(dd.w, dd.h) / 2.0f + 2.0f) {
                if (e.parasite != INVALID_EID) { // 摘除寄生
                    if (valid(e.parasite)) kill(e.parasite);
                    e.parasite = INVALID_EID;
                }
                if (e.hp >= ud.hp) {
                    e.target = INVALID_EID; // 修满离开
                } else if (tick % 5 == 0) {
                    Player& p = players[e.player];
                    if (p.money >= 6) { p.money -= 6; e.hp = std::min(ud.hp, e.hp + 12); }
                }
                if (e.state == UState::Moving && e.pathIdx >= (int)e.path.size()) e.state = UState::Idle;
                return; // 维修期间不索敌不移动
            }
        }
    }

    // 工程师到达目标建筑：占领
    if (e.utype == UnitType::Engineer && e.target != INVALID_EID && valid(e.target)) {
        Ent& b = ents[e.target];
        if (b.isBuilding && b.player != e.player) {
            const BldDef& bd = bldDef(b.btype);
            float bx = b.x + bd.w / 2.0f, by = b.y + bd.h / 2.0f;
            if (distf(e.x, e.y, bx, by) < std::max(bd.w, bd.h) / 2.0f + 1.5f) {
                eva(b.player, TextFormat(TR(S::EvaBldCapturedFmt), bldName(b.btype)));
                eva(e.player, TextFormat(TR(S::EvaCapturedFmt), bldName(b.btype)));
                b.player = e.player;
                b.hp = bd.hp;
                applyCaptureEffect(b, e.player); // 科技机场/秘密实验室等特殊效果
                recomputePower();
                e.alive = false;
                freeList.push_back(id);
                return;
            }
        }
        // 己方受损建筑：进入修复（RA2 原作：瞬间回满，工程师消耗）
        if (b.isBuilding && b.player == e.player && b.hp < bldDef(b.btype).hp) {
            const BldDef& bd = bldDef(b.btype);
            float bx = b.x + bd.w / 2.0f, by = b.y + bd.h / 2.0f;
            if (distf(e.x, e.y, bx, by) < std::max(bd.w, bd.h) / 2.0f + 1.5f) {
                b.hp = bd.hp;
                eva(e.player, TextFormat(TR(S::EvaEngRepairedFmt), bldName(b.btype)));
                g_sfx.playAt(Sfx::Place, bx, by);
                e.alive = false;
                freeList.push_back(id);
                return;
            }
        }
    }

    // 间谍到达目标建筑：渗透生效（RA2 原作：间谍消耗，按建筑类型产生效果）
    if (e.utype == UnitType::Spy && e.target != INVALID_EID && valid(e.target)) {
        Ent& b = ents[e.target];
        if (b.isBuilding && isEnemy(e.player, b.player)) {
            const BldDef& bd = bldDef(b.btype);
            float bx = b.x + bd.w / 2.0f, by = b.y + bd.h / 2.0f;
            if (distf(e.x, e.y, bx, by) < std::max(bd.w, bd.h) / 2.0f + 1.5f) {
                applySpyEffect(e, b, id);
                return;
            }
        }
    }

    switch (e.state) {
        case UState::Idle: {
            // 自动索敌（警戒模式按视野半径，普通按射程+2）
            const WeaponDef ew = effWeapon(e);
            if (ew.damage > 0) {
                float scanR = e.guard ? (float)std::max(ud.sight, ew.range + 2)
                                      : (float)(ew.range + 2);
                EID en = findNearestEnemy(e.player, e.x, e.y, scanR, true, &ew, e.utype);
                if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; }
            }
            break;
        }
        case UState::Moving:
        case UState::AttackMoving: {
            const WeaponDef ew = effWeapon(e);
            if (e.state == UState::AttackMoving && ew.damage > 0) {
                EID en = findNearestEnemy(e.player, e.x, e.y, (float)(ew.range + 1), true, &ew, e.utype);
                if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; break; }
            }
            moveAlongPath(e, id);
            if (e.pathIdx >= (int)e.path.size()) e.state = UState::Idle;
            break;
        }
        case UState::Chasing: {
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            const Ent& t = ents[e.target];
            const WeaponDef ew = effWeapon(e);
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            float d = distf(e.x, e.y, tx, ty);
            // C4 爆破手攻击建筑需贴脸（2.5 格），而非武器射程
            float effR = (ud.hasC4() && t.isBuilding) ? 2.5f : (float)ew.range;
            if (d <= effR) {
                e.path.clear();
                e.state = UState::Attacking;
            } else {
                // 超时空军团兵/超时空突击队追击：直接传送至目标射程边缘
                if (e.utype == UnitType::Chrono || e.utype == UnitType::ChronoCommando) {
                    float nx = tx - (tx - e.x) / d * (effR * 0.8f);
                    float ny = ty - (ty - e.y) / d * (effR * 0.8f);
                    chronoJump(e, nx, ny);
                    break;
                }
                // 每 30 帧重寻路
                if (e.path.empty() || (tick % 30) == 0) {
                    std::vector<Vec2i> path;
                    if (map.findPath((int)e.x, (int)e.y, (int)tx, (int)ty, path, 20000, ud.pathDomain())) {
                        e.path = std::move(path); e.pathIdx = 0;
                    }
                }
                moveAlongPath(e, id);
            }
            break;
        }
        case UState::Attacking: {
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            const Ent& t = ents[e.target];
            const WeaponDef ew = effWeapon(e);
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            float d = distf(e.x, e.y, tx, ty);
            if (d > ew.range + 1) { e.state = UState::Chasing; break; }
            // C4 爆破手：建筑目标超出贴脸距离 → 重新贴近
            if (ud.hasC4() && t.isBuilding && d > 2.5f) { e.state = UState::Chasing; break; }
            // 尤里无法控制建筑：放弃目标
            if (e.utype == UnitType::Yuri && t.isBuilding) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            // 航空母舰：放飞舰载机空袭（RA2 原作：大黄蜂起飞→投弹→返航整备）
            if (e.utype == UnitType::AircraftCarrier) {
                if (e.atkCd <= 0 && !e.cargo.empty()) {
                    e.cargo.pop_back();
                    EID tgt0 = e.target;
                    int pl = e.player;
                    float sx0 = e.x, sy0 = e.y;
                    EID h = spawnUnit(pl, UnitType::Hornet, sx0, sy0); // 注意：spawnUnit 可能扩容 ents，e/t 引用失效
                    Ent& he = ents[h];
                    he.target = tgt0;
                    he.airbase = id; // 母舰（返航归舰）
                    he.state = UState::Chasing;
                    g_sfx.playAt(Sfx::Missile, sx0, sy0);
                    ents[id].atkCd = 70;
                }
                break;
            }
            // 恐怖机器人：贴脸后附着宿主（RA2 原作：钻入车辆内部持续破坏）
            if (e.utype == UnitType::TerrorDrone && !t.isBuilding) {
                const UnitDef& td = unitDef(t.utype);
                if (!td.isInfantry() && !td.isAir() && !td.isNaval() && t.parasite == INVALID_EID && d <= 1.4f) {
                    Ent& host = ents[e.target];
                    host.parasite = id;
                    e.parasiting = true;
                    e.parasiteHost = e.target;
                    e.target = INVALID_EID;
                    e.path.clear();
                    Effect sp; sp.kind = 2; sp.x = host.x; sp.y = host.y;
                    sp.x2 = host.x + 0.4f; sp.y2 = host.y + 0.4f; sp.maxAge = 8;
                    effects.push_back(sp);
                    g_sfx.playAt(Sfx::Tesla, host.x, host.y);
                    break;
                }
            }
            // 巨型乌贼：贴身缠绕敌舰（RA2 原作：宿主定身+持续挤压，可被反潜火力攻击摘除）
            if (e.utype == UnitType::Squid && !t.isBuilding) {
                const UnitDef& td = unitDef(t.utype);
                if ((td.isNaval() || td.isAmphib()) && t.parasite == INVALID_EID && d <= 1.6f) {
                    Ent& host = ents[e.target];
                    host.parasite = id;
                    e.parasiting = true;
                    e.parasiteHost = e.target;
                    e.target = INVALID_EID;
                    e.path.clear();
                    Effect sp; sp.kind = 2; sp.x = host.x; sp.y = host.y;
                    sp.x2 = host.x - 0.4f; sp.y2 = host.y + 0.4f; sp.maxAge = 8;
                    effects.push_back(sp);
                    g_sfx.playAt(Sfx::Torpedo, host.x, host.y);
                    break;
                }
            }
            // 尤里/心灵突击队：心灵控制地面单位（RA2 原作：夺取敌方单位控制权，同一时刻仅一个）
            if (ud.isPsychic() && !t.isBuilding) {
                if (psychicImmune(t.utype) || t.mindBy != INVALID_EID) {
                    // 免疫/已被控制：放弃该目标，避免无效贴身
                    e.target = INVALID_EID; e.state = UState::Idle;
                    break;
                }
                if (e.atkCd <= 0) {
                    mindControlTake(e, id, e.target);
                    e.atkCd = ew.cooldown;
                }
                break;
            }
            // C4 爆破手（谭雅/海豹/超时空突击队/心灵突击队）：近身建筑安放 C4；会游泳的还可炸舰船
            if (ud.hasC4() && e.atkCd <= 0) {
                bool navalTgt = !t.isBuilding && (unitDef(t.utype).isNaval() || unitDef(t.utype).isAmphib());
                if ((t.isBuilding || (navalTgt && ud.canSwim())) && d <= 2.5f) {
                    TimedBomb b;
                    b.x = tx; b.y = ty; b.timer = 45; b.player = e.player;
                    b.attachedTo = e.target; b.dmg = 6000; b.radius = 0.6f;
                    timedBombs.push_back(b);
                    Effect mz; mz.kind = 5; mz.x = tx; mz.y = ty; mz.maxAge = 6;
                    effects.push_back(mz);
                    g_sfx.playAt(Sfx::Click, tx, ty);
                    e.atkCd = 60; // 撤离间隙
                    break;
                }
            }
            // C4 爆破手不对建筑开枪：等待下次爆破冷却（RA2 原作：谭雅/海豹对建筑仅用 C4）
            if (ud.hasC4() && t.isBuilding) break;
            // 面向目标
            int wantDir = dirFromVec(tx - e.x, ty - e.y);
            e.turretDir = wantDir;
            if (!g_sprites.hasTurret(e.utype)) e.dir = wantDir;
            if (e.atkCd <= 0) {
                fireWeapon(e, id, e.target);
                e.atkCd = ew.cooldown;
            }
            break;
        }
        case UState::HarvestGo: case UState::HarvestDig: case UState::HarvestReturn: case UState::HarvestUnload:
            updateHarvester(e, id);
            break;
        case UState::Boarding: {
            // 步兵登船/进驻建筑：接近目标后进入货舱或驻军
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            Ent& t = ents[e.target];
            // 进驻建筑（民房/战斗碉堡/坦克碉堡）
            if (t.isBuilding) {
                const BldDef& bd = bldDef(t.btype);
                int gdom = garrisonDomain(t.btype);
                bool fit = (gdom == 1 && ud.isInfantry())
                        || (gdom == 2 && !ud.isInfantry() && !ud.isAir() && ud.pathDomain() == 0 && !ud.canHarvet());
                if (bd.garrisonCap == 0 || !fit || (t.player >= 0 && t.player != e.player)) { e.target = INVALID_EID; e.state = UState::Idle; break; }
                float bx = t.x + bd.w / 2.0f, by = t.y + bd.h / 2.0f;
                if (distf(e.x, e.y, bx, by) < std::max(bd.w, bd.h) / 2.0f + 1.5f) {
                    if ((int)t.garrison.size() < bd.garrisonCap) {
                        t.garrison.push_back(e.utype);
                        if (t.player < 0) t.player = e.player; // 进驻中立民房：归属占领方
                        e.alive = false;
                        freeList.push_back(id); // 已进驻（不触发爆炸）
                    } else {
                        e.target = INVALID_EID;
                        e.state = UState::Idle; // 驻满
                    }
                    break;
                }
                if (e.path.empty() || (tick % 30) == 0) {
                    std::vector<Vec2i> path;
                    if (map.findPath((int)e.x, (int)e.y, (int)bx, (int)by, path, 20000, ud.pathDomain())) {
                        e.path = std::move(path); e.pathIdx = 0;
                    }
                }
                moveAlongPath(e, id);
                break;
            }
            // 登上运输载具
            {
                const UnitDef& td = unitDef(t.utype);
                if (t.player != e.player || td.cargoCap == 0) { e.target = INVALID_EID; e.state = UState::Idle; break; }
                if (distf(e.x, e.y, t.x, t.y) < 1.6f) {
                    if ((int)t.cargo.size() < td.cargoCap) {
                        t.cargo.push_back(e.utype);
                        e.alive = false;
                        freeList.push_back(id); // 已登船（不触发爆炸）
                    } else {
                        e.target = INVALID_EID;
                        e.state = UState::Idle; // 舱满
                    }
                    break;
                }
                if (e.path.empty() || (tick % 30) == 0) {
                    int gx, gy;
                    if (boardGoal(t, ud.pathDomain(), gx, gy)) {
                        std::vector<Vec2i> path;
                        if (map.findPath((int)e.x, (int)e.y, gx, gy, path, 20000, ud.pathDomain())) {
                            e.path = std::move(path); e.pathIdx = 0;
                        }
                    }
                }
                moveAlongPath(e, id);
            }
            break;
        }
    }
}

// ===================== 战机 =====================
Vec2f World::airPadPos(const Ent& af, int slot) const {
    const BldDef& d = bldDef(af.btype);
    static const float off[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f}};
    return {af.x + d.w / 2.0f + off[slot & 3][0], af.y + d.h / 2.0f + off[slot & 3][1]};
}

bool World::flyToward(Ent& e, float tx, float ty) {
    const UnitDef& ud = unitDef(e.utype);
    float d = distf(e.x, e.y, tx, ty);
    float step = 1.0f / ud.speed;
    if (d <= step) {
        e.x = tx; e.y = ty;
        return true;
    }
    e.dir = dirFromVec(tx - e.x, ty - e.y);
    e.turretDir = e.dir;
    e.x += (tx - e.x) / d * step;
    e.y += (ty - e.y) / d * step;
    return false;
}

void World::updateAircraft(Ent& e, EID id) {
    const UnitDef& ud = unitDef(e.utype);
    // 重伤冒烟
    if (e.hp < ud.hp / 2 && e.state != UState::Landed && tick % 25 == (uint64_t)(id % 25)) {
        Effect sm;
        sm.kind = 1; sm.x = e.x; sm.y = e.y; sm.maxAge = 30;
        effects.push_back(sm);
    }

    switch (e.state) {
        case UState::Idle: // 容错：空中单位不应 Idle
            e.goalX = e.x; e.goalY = e.y;
            e.state = UState::Circling;
            break;
        case UState::Moving:
        case UState::AttackMoving: {
            bool hasAmmo = ud.ammo == 0 || e.ammo > 0; // ammo=0 为无限弹药（基洛夫/火箭飞行兵）
            if (e.state == UState::AttackMoving && ud.weapon.damage > 0 && hasAmmo) {
                EID en = findNearestEnemy(e.player, e.x, e.y, (float)(ud.weapon.range + 3), true, &ud.weapon, e.utype);
                if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; break; }
            }
            if (flyToward(e, e.goalX, e.goalY) || distf(e.x, e.y, e.goalX, e.goalY) < 0.4f) {
                e.state = UState::Circling; // 到达后盘旋待命
            }
            break;
        }
        case UState::Chasing: {
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = UState::Circling; break; }
            const Ent& t = ents[e.target];
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            if (ud.ammo > 0 && e.ammo <= 0) { e.state = UState::Returning; break; }
            if (distf(e.x, e.y, tx, ty) <= ud.weapon.range) {
                e.state = UState::Attacking;
                e.orbitA = atan2f(e.y - ty, e.x - tx);
            } else {
                flyToward(e, tx, ty);
            }
            break;
        }
        case UState::Attacking: {
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = (ud.ammo == 0 || e.ammo > 0) ? UState::Circling : UState::Returning; break; }
            const Ent& t = ents[e.target];
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            if (ud.ammo > 0 && e.ammo <= 0) { e.state = UState::Returning; break; }
            // 环绕目标盘旋投弹
            e.orbitA += 0.10f;
            float r = ud.weapon.range * 0.75f;
            flyToward(e, tx + cosf(e.orbitA) * r, ty + sinf(e.orbitA) * r);
            if (distf(e.x, e.y, tx, ty) <= ud.weapon.range && e.atkCd <= 0) {
                fireWeapon(e, id, e.target);
                if (ud.ammo > 0) e.ammo--;
                e.atkCd = ud.weapon.cooldown;
                if (ud.ammo > 0 && e.ammo <= 0) { e.target = INVALID_EID; e.state = UState::Returning; }
            }
            break;
        }
        case UState::Circling: {
            // 舰载机：不自主盘旋（完成攻击即返航归舰）
            if (e.utype == UnitType::Hornet) { e.state = UState::Returning; break; }
            // 绕目标点小半径盘旋，自动索敌
            e.orbitA += 0.10f;
            flyToward(e, e.goalX + cosf(e.orbitA) * 1.5f, e.goalY + sinf(e.orbitA) * 1.5f);
            if (ud.weapon.damage > 0 && (ud.ammo == 0 || e.ammo > 0)) {
                EID en = findNearestEnemy(e.player, e.x, e.y, (float)(ud.weapon.range + 3), true, &ud.weapon, e.utype);
                if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; }
            }
            break;
        }
        case UState::Returning: {
            // 舰载机：返航归舰（母舰已毁则坠毁，RA2 原作设定）
            if (e.utype == UnitType::Hornet) {
                if (!valid(e.airbase) || ents[e.airbase].player != e.player
                    || ents[e.airbase].utype != UnitType::AircraftCarrier) {
                    kill(id); // 无家可归：坠毁
                    return;
                }
                Ent& cv = ents[e.airbase];
                if (flyToward(e, cv.x, cv.y) || distf(e.x, e.y, cv.x, cv.y) < 0.6f) {
                    if ((int)cv.cargo.size() < unitDef(cv.utype).cargoCap) cv.cargo.push_back(UnitType::Hornet);
                    e.alive = false; // 成功着舰：回收（不触发爆炸）
                    freeList.push_back(id);
                }
                return;
            }
            // 校验基地；被毁则找其他空指部
            if (!valid(e.airbase) || ents[e.airbase].player != e.player) {
                e.airbase = INVALID_EID;
                for (size_t i = 0; i < ents.size(); i++) {
                    const Ent& b = ents[i];
                    if (b.alive && b.isBuilding && b.player == e.player && b.btype == BldType::AirForceCmd) {
                        e.airbase = (int)i; break;
                    }
                }
                if (e.airbase == INVALID_EID) {
                    // 无家可归：原地盘旋
                    e.goalX = e.x; e.goalY = e.y;
                    e.state = UState::Circling;
                    break;
                }
            }
            Vec2f pad = airPadPos(ents[e.airbase], id);
            if (flyToward(e, pad.x, pad.y) || distf(e.x, e.y, pad.x, pad.y) < 0.3f) {
                e.x = pad.x; e.y = pad.y;
                e.state = UState::Landed;
                e.rearmTimer = 0;
            }
            break;
        }
        case UState::Landed: {
            // 停机装填：每 20 帧补 1 发
            if (e.ammo < ud.ammo && ++e.rearmTimer >= 20) {
                e.rearmTimer = 0;
                e.ammo++;
            }
            break;
        }
        default:
            e.state = UState::Circling;
            break;
    }
}

void World::moveAlongPath(Ent& e, EID id) {
    if (e.pathIdx >= (int)e.path.size()) return;
    // 乌贼缠绕：宿主舰船被定身无法移动（RA2 原作签名机制）
    if (e.parasite != INVALID_EID && valid(e.parasite) && ents[e.parasite].utype == UnitType::Squid) return;
    const UnitDef& ud = unitDef(e.utype);
    Vec2i next = e.path[e.pathIdx];
    // 目标格被其他单位占据：等待或绕行；长期被堵则放弃路径
    EID occ = unitAtCell(next.x, next.y);
    if (occ != INVALID_EID && occ != id) {
        Ent& o = ents[occ];
        const UnitDef& od = unitDef(o.utype);
        // 坦克碾压（RA2 原作）：重甲车辆直接碾死敌方无甲步兵（磁暴步兵等轻甲、已部署重装大兵不可碾）
        if (o.player != e.player && ud.move == MoveType::Vehicle && ud.armor == Armor::Heavy
            && od.isInfantry() && od.armor == Armor::None && o.invuln == 0 && !o.deployed) {
            g_sfx.playAt(Sfx::Crush, o.x, o.y);
            Player& vp = players[o.player];
            if (vp.evaUnitCd <= 0) { eva(o.player, TR(S::EvaUnitLost)); vp.evaUnitCd = 150; }
            kill(occ);
            // 格子已空，继续走正常移动流程
        } else {
        // 同阵营空闲单位挡路：偶尔把它轻推到旁边空格，避免基地拥堵死锁
        if (o.player == e.player && o.state == UState::Idle && rng.chance(0.12f)) {
            int dx = rng.range(-1, 1), dy = rng.range(-1, 1);
            int nx = (int)o.x + dx, ny = (int)o.y + dy;
            if ((dx || dy) && passableFor(nx, ny, od.pathDomain()) && !bldBlocked(nx, ny) && unitAtCell(nx, ny) == INVALID_EID) {
                o.x = nx + 0.5f; o.y = ny + 0.5f;
            }
        }
        // 简单避让：横向随机偏移尝试
        if (rng.chance(0.3f)) {
            int dx = rng.range(-1, 1), dy = rng.range(-1, 1);
            if (passableFor((int)e.x + dx, (int)e.y + dy, ud.pathDomain()) && unitAtCell((int)e.x + dx, (int)e.y + dy) == INVALID_EID
                && !bldBlocked((int)e.x + dx, (int)e.y + dy)) {
                e.x += dx * 0.5f; e.y += dy * 0.5f;
            }
        }
        if (++e.blockTick > 60) { // 堵 2 秒：重寻路，失败则放弃
            e.blockTick = 0;
            int gx = e.path.back().x, gy = e.path.back().y;
            // 终点格被占（如集结点上站着人）：永远到不了，直接放弃
            EID gOcc = unitAtCell(gx, gy);
            if (gOcc != INVALID_EID && gOcc != id) { e.path.clear(); return; }
            std::vector<Vec2i> path;
            if (map.findPath((int)e.x, (int)e.y, gx, gy, path, 20000, ud.pathDomain())) { e.path = std::move(path); e.pathIdx = 0; }
            else e.path.clear();
        }
        return;
        } // !碾压
    }
    if (bldBlocked(next.x, next.y)) {
        // 重新寻路
        std::vector<Vec2i> path;
        int gx = e.path.back().x, gy = e.path.back().y;
        if (map.findPath((int)e.x, (int)e.y, gx, gy, path, 20000, ud.pathDomain())) { e.path = std::move(path); e.pathIdx = 0; }
        else e.path.clear();
        return;
    }
    if (++e.moveTick >= ud.speed) {
        e.moveTick = 0;
        e.blockTick = 0;
        float tx = next.x + 0.5f, ty = next.y + 0.5f;
        e.dir = dirFromVec(tx - e.x, ty - e.y);
        if (!g_sprites.hasTurret(e.utype)) e.turretDir = e.dir;
        e.x = tx; e.y = ty;
        e.pathIdx++;
        if (ud.isInfantry() && (++e.walkAnim % 4 == 0)) e.walkFrame ^= 1;
        if (e.camouflaged) { e.camouflaged = false; e.camoTick = 0; } // 幻影移动解除伪装
        pickupCrates(e); // 驶入补给箱：拾取
    }
}

void World::updateHarvester(Ent& e, EID id) {
    const int CAP = 20; // 载矿容量（单位）
    switch (e.state) {
        case UState::HarvestGo: {
            moveAlongPath(e, id);
            if (e.oreCell.x < 0 || map.at(e.oreCell.x, e.oreCell.y).ore <= 0) {
                Vec2i ore;
                if (map.findNearestOre((int)e.x, (int)e.y, 40, ore)) {
                    e.oreCell = ore;
                    std::vector<Vec2i> path;
                    if (map.findPath((int)e.x, (int)e.y, ore.x, ore.y, path)) { e.path = std::move(path); e.pathIdx = 0; }
                } else e.state = UState::Idle;
            } else if (distf(e.x, e.y, e.oreCell.x + 0.5f, e.oreCell.y + 0.5f) < 2.0f) {
                // 站到矿边即可开挖（避免多车抢同一格互相堵死）
                e.state = UState::HarvestDig;
                e.digTimer = 0;
            } else if (e.path.empty()) {
                // 路径被放弃但矿点仍在：重新寻路
                std::vector<Vec2i> path;
                if (map.findPath((int)e.x, (int)e.y, e.oreCell.x, e.oreCell.y, path)) {
                    e.path = std::move(path); e.pathIdx = 0;
                } else e.state = UState::Idle;
            }
            break;
        }
        case UState::HarvestDig: {
            if (++e.digTimer >= 20) {
                e.digTimer = 0;
                int got = map.harvestAt(e.oreCell.x, e.oreCell.y, 1);
                e.oreLoad += got;
                if (got == 0 || e.oreLoad >= CAP) {
                    // 找最近的己方精炼厂
                    EID ref = INVALID_EID;
                    float bd = 1e9f;
                    for (size_t i = 0; i < ents.size(); i++) {
                        const Ent& b = ents[i];
                        if (!b.alive || !b.isBuilding || b.player != e.player || b.btype != BldType::OreRefinery) continue;
                        float d = distf(e.x, e.y, b.x + 1.5f, b.y + 1.0f);
                        if (d < bd) { bd = d; ref = (int)i; }
                    }
                    if (ref == INVALID_EID) { e.state = UState::Idle; break; }
                    e.dockRefinery = ref;
                    const Ent& b = ents[ref];
                    // 超时空采矿车：满载瞬移回精炼厂（RA2 原作签名机制，返程空车正常行驶）
                    if (e.utype == UnitType::ChronoMiner) {
                        Effect ef1; ef1.kind = 9; ef1.x = e.x; ef1.y = e.y; ef1.maxAge = 20; effects.push_back(ef1);
                        const BldDef& rbd = bldDef(b.btype);
                        for (int r = 1; r < 6; r++) {
                            int sx = (int)b.x + 1, sy = (int)b.y + rbd.h + r - 1;
                            if (map.passable(sx, sy) && !bldBlocked(sx, sy) && unitAtCell(sx, sy) == INVALID_EID) {
                                e.x = sx + 0.5f; e.y = sy + 0.5f; break;
                            }
                        }
                        Effect ef2; ef2.kind = 9; ef2.x = e.x; ef2.y = e.y; ef2.maxAge = 20; effects.push_back(ef2);
                        g_sfx.playAt(Sfx::Tesla, e.x, e.y);
                        e.path.clear();
                        e.state = UState::HarvestUnload;
                        e.digTimer = 0;
                        break;
                    }
                    std::vector<Vec2i> path;
                    if (map.findPath((int)e.x, (int)e.y, (int)b.x + 1, (int)b.y + bldDef(b.btype).h, path)) {
                        e.path = std::move(path); e.pathIdx = 0;
                        e.state = UState::HarvestReturn;
                    } else e.state = UState::Idle;
                }
            }
            break;
        }
        case UState::HarvestReturn: {
            if (!valid(e.dockRefinery)) { e.state = UState::Idle; break; }
            moveAlongPath(e, id);
            const Ent& b = ents[e.dockRefinery];
            if (distf(e.x, e.y, b.x + 1.5f, b.y + bldDef(b.btype).h + 0.5f) < 2.0f) {
                e.state = UState::HarvestUnload;
                e.digTimer = 0;
            }
            if (e.pathIdx >= (int)e.path.size() && e.state == UState::HarvestReturn) {
                e.state = UState::HarvestUnload;
                e.digTimer = 0;
            }
            break;
        }
        case UState::HarvestUnload: {
            if (++e.digTimer >= 30) {
                e.digTimer = 0;
                if (e.oreLoad > 0) {
                    e.oreLoad--;
                    players[e.player].money += 35; // 每单位矿 35 金
                } else {
                    // 返回矿区
                    if (e.player == 0) g_sfx.play(Sfx::Cash, 0.55f); // 卸矿完成提示
                    e.dockRefinery = INVALID_EID;
                    if (e.oreCell.x >= 0 && map.at(e.oreCell.x, e.oreCell.y).ore > 0) {
                        std::vector<Vec2i> path;
                        if (map.findPath((int)e.x, (int)e.y, e.oreCell.x, e.oreCell.y, path)) {
                            e.path = std::move(path); e.pathIdx = 0;
                            e.state = UState::HarvestGo;
                            break;
                        }
                    }
                    e.state = UState::Idle;
                }
            }
            break;
        }
        default: break;
    }
}

void World::updateBuilding(Ent& e, EID id) {
    const BldDef& bd = bldDef(e.btype);
    if (e.atkCd > 0) e.atkCd--;
    if (e.invuln > 0) e.invuln--;
    // 建筑被超时空武器照射：冻结且累积抹除（中立建筑同样可被抹除）
    if (e.chrono > 0) {
        int threshold = bd.hp / 3 + 20;
        if (e.chrono >= threshold) {
            Effect ef; ef.kind = 9; ef.x = e.x + bd.w / 2.0f; ef.y = e.y + bd.h / 2.0f; ef.maxAge = 30; effects.push_back(ef);
            g_sfx.playAt(Sfx::Tesla, ef.x, ef.y);
            kill(id); // 抹除（含占地清理）
            return;
        }
        if (tick % 2 == 0) e.chrono--;
        return; // 冻结：停产停火（生产进度由玩家队列管理，不受影响）
    }
    // 中立科技建筑效果（被占领后 player>=0 生效；bldAnim 兼作计时器）
    if (e.player >= 0) {
        e.bldAnim++;
        if (e.btype == BldType::OilDerrick && e.bldAnim % 100 == 0) {
            players[e.player].money += 25; // 油井持续资金
            if (e.player == 0) g_sfx.play(Sfx::Cash, 0.35f);
        } else if (e.btype == BldType::Hospital && e.bldAnim % 60 == 0) {
            // 医院：全体己方步兵持续回血
            for (Ent& o : ents)
                if (o.alive && !o.isBuilding && o.player == e.player && unitDef(o.utype).isInfantry())
                    o.hp = std::min(unitDef(o.utype).hp, o.hp + 3);
        } else if (e.btype == BldType::MachineShop && e.bldAnim % 60 == 0) {
            // 机械商店：全体己方车辆/舰船持续维修
            for (Ent& o : ents)
                if (o.alive && !o.isBuilding && o.player == e.player && !unitDef(o.utype).isInfantry())
                    o.hp = std::min(unitDef(o.utype).hp, o.hp + 4);
        }
    }
    // 磁暴线圈：附近磁暴步兵为其充电（RA2 原作：充电后伤害+50%，低电仍可开火）
    if (e.btype == BldType::TeslaCoil && e.player >= 0) {
        e.teslaCharge = 0;
        float cx = e.x + bd.w / 2.0f, cy = e.y + bd.h / 2.0f;
        float tpx = 0, tpy = 0;
        for (const Ent& o : ents)
            if (o.alive && !o.isBuilding && o.player == e.player && o.utype == UnitType::TeslaTrooper
                && distf(o.x, o.y, cx, cy) <= 2.6f) {
                if (e.teslaCharge == 0) { tpx = o.x; tpy = o.y; }
                e.teslaCharge++;
            }
        if (e.teslaCharge > 0 && tick % 10 == (uint64_t)(id % 10)) {
            Effect arc; arc.kind = 2; arc.x = tpx; arc.y = tpy; arc.x2 = cx; arc.y2 = cy; arc.maxAge = 7;
            effects.push_back(arc);
        }
    }
    bool bldCanFire = bd.weapon.damage > 0 && e.player >= 0
        && (!players[e.player].lowPower() || (e.btype == BldType::TeslaCoil && e.teslaCharge > 0));
    if (bldCanFire) {
        float cx = e.x + bd.w / 2.0f, cy = e.y + bd.h / 2.0f;
        if (!valid(e.target) || distf(cx, cy, ents[e.target].x, ents[e.target].y) > bd.weapon.range + 1) {
            e.target = findNearestEnemy(e.player, cx, cy, (float)bd.weapon.range, false, &bd.weapon);
            if (e.target == INVALID_EID) e.target = findNearestEnemy(e.player, cx, cy, (float)bd.weapon.range, true, &bd.weapon);
        }
        if (valid(e.target) && e.atkCd <= 0) {
            // 光棱塔串联（RA2 原作签名机制）：8 格内友军光棱塔汇聚光束，每座 +75% 伤害
            if (e.btype == BldType::PrismTower) {
                int sup = 0;
                float supPos[8][2];
                for (size_t i = 0; i < ents.size() && sup < 8; i++) {
                    Ent& o = ents[i];
                    if (!o.alive || !o.isBuilding || (int)i == id) continue;
                    if (o.btype != BldType::PrismTower || o.player != e.player) continue;
                    float ox = o.x + bd.w / 2.0f, oy = o.y + bd.h / 2.0f;
                    if (distf(ox, oy, cx, cy) > 8.0f) continue;
                    supPos[sup][0] = ox; supPos[sup][1] = oy; sup++;
                }
                const Ent& t = ents[e.target];
                float tx = t.x, ty = t.y;
                if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
                float mult = t.isBuilding ? bd.weapon.vsBuilding
                           : (unitDef(t.utype).isInfantry() ? bd.weapon.vsInfantry : bd.weapon.vsVehicle);
                int dmg = (int)(bd.weapon.damage * mult * (1.0f + 0.75f * sup));
                damage(e.target, dmg, e.player, id);
                // 主光束 + 各支援塔汇聚光束
                Effect ef; ef.kind = 3; ef.x = cx; ef.y = cy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 12;
                effects.push_back(ef);
                for (int k = 0; k < sup; k++) {
                    Effect sb; sb.kind = 3; sb.x = supPos[k][0]; sb.y = supPos[k][1];
                    sb.x2 = cx; sb.y2 = cy; sb.maxAge = 10;
                    effects.push_back(sb);
                }
                g_sfx.playAt(Sfx::Prism, cx, cy);
                e.atkCd = bd.weapon.cooldown;
            } else {
                fireWeapon(e, id, e.target);
                e.atkCd = bd.weapon.cooldown;
            }
        }
    }
    // 驻军轮流出击（RA2 原作：进驻步兵从建筑内对外射击，射程+1）
    if (!e.garrison.empty() && e.player >= 0) garrisonFire(e, id);
}

// 驻军火力：每 8 帧一名驻军射击一次（轮换），享受射程与伤害加成
void World::garrisonFire(Ent& b, EID id) {
    if (tick % 8 != (uint64_t)(id % 8)) return;
    const BldDef& bd = bldDef(b.btype);
    float cx = b.x + bd.w / 2.0f, cy = b.y + bd.h / 2.0f;
    int slot = (int)((tick / 8 + id) % b.garrison.size());
    UnitType gt = b.garrison[slot];
    WeaponDef w = unitDef(gt).weapon;
    if (w.damage <= 0) return;
    w.range += 1;                          // 驻军射程加成
    w.damage = (int)(w.damage * 1.2f);     // 驻军伤害加成
    EID tgt = findNearestEnemy(b.player, cx, cy, (float)w.range, true, &w, gt);
    if (tgt == INVALID_EID) return;
    const Ent& t = ents[tgt];
    float tx = t.x, ty = t.y;
    if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
    Projectile p;
    p.kind = strcmp(w.projSprite, "flak") == 0 ? ProjKind::Flak : ProjKind::Bullet;
    p.player = b.player;
    p.x = cx; p.y = cy; p.tx = tx; p.ty = ty; p.target = tgt; p.src = id; p.w = w;
    projs.push_back(p);
    Effect mz; mz.kind = 5; mz.x = cx; mz.y = cy; mz.maxAge = 4;
    effects.push_back(mz);
    g_sfx.playAt(Sfx::Shot, cx, cy);
}

void World::fireWeapon(Ent& e, EID id, EID targetId) {
    if (!valid(targetId)) return;
    (void)id;
    const Ent& t = ents[targetId];
    // 航空母舰不直接开火（舰载机空袭，见 updateUnit Attacking 分支）
    if (!e.isBuilding && e.utype == UnitType::AircraftCarrier) return;
    WeaponDef w = e.isBuilding ? bldDef(e.btype).weapon : effWeapon(e); // 含部署/IFV载兵/精英军衔
    // 磁暴线圈充电加成：伤害 +50%
    if (e.isBuilding && e.btype == BldType::TeslaCoil && e.teslaCharge > 0)
        w.damage = (int)(w.damage * 1.5f);
    float sx = e.x, sy = e.y;
    if (e.isBuilding) { sx += bldDef(e.btype).w / 2.0f; sy += bldDef(e.btype).h / 2.0f; }
    float tx = t.x, ty = t.y;
    if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }

    // 疯狂伊文：攻击 = 在目标上安放定时炸弹（5 秒后爆炸，RA2 原作设定）
    if (!e.isBuilding && e.utype == UnitType::CrazyIvan) {
        TimedBomb b;
        b.x = tx; b.y = ty; b.timer = 30 * 5; b.player = e.player; b.attachedTo = targetId;
        timedBombs.push_back(b);
        Effect mz;
        mz.kind = 5; mz.x = tx; mz.y = ty; mz.maxAge = 6;
        effects.push_back(mz);
        g_sfx.playAt(Sfx::Click, tx, ty);
        return;
    }
    // 心灵波：控制效果在攻击状态机（mindControlTake）结算；对无效目标（建筑等）不发弹
    if (!e.isBuilding && unitDef(e.utype).isPsychic()) return;
    // 台风潜艇开火后暴露 3 秒（可被反潜单位索敌）
    if (!e.isBuilding && e.utype == UnitType::Typhoon) e.subReveal = 90;
    const char* ps = w.projSprite;
    // 幻影坦克开火解除伪装
    if (!e.isBuilding && e.utype == UnitType::MirageTank) { e.camouflaged = false; e.camoTick = 0; }
    // 开火音效（按弹道类型）
    if (strcmp(ps, "tesla") == 0) g_sfx.playAt(Sfx::Tesla, sx, sy);
    else if (strcmp(ps, "prism") == 0) g_sfx.playAt(Sfx::Prism, sx, sy);
    else if (strcmp(ps, "chrono") == 0) g_sfx.playAt(Sfx::Tesla, sx, sy);
    else if (strcmp(ps, "rad") == 0) g_sfx.playAt(Sfx::Shot, sx, sy);
    else if (strcmp(ps, "bullet") == 0) g_sfx.playAt(Sfx::Shot, sx, sy);
    else if (strcmp(ps, "flak") == 0) g_sfx.playAt(Sfx::Flak, sx, sy);
    else if (strcmp(ps, "missile") == 0) g_sfx.playAt(Sfx::Missile, sx, sy);
    else if (strcmp(ps, "naval") == 0) g_sfx.playAt(Sfx::NavalCannon, sx, sy);
    else if (strcmp(ps, "torpedo") == 0) g_sfx.playAt(Sfx::Torpedo, sx, sy);
    else g_sfx.playAt(Sfx::Cannon, sx, sy);
    // 开火口焰特效
    if (strcmp(ps, "tesla") != 0 && strcmp(ps, "prism") != 0) {
        Effect mz;
        mz.kind = 5; mz.x = sx; mz.y = sy; mz.maxAge = 4;
        effects.push_back(mz);
    }
    if (strcmp(ps, "tesla") == 0) {
        // 磁暴：瞬时电弧
        float mult = t.isBuilding ? w.vsBuilding : (unitDef(t.utype).isInfantry() ? w.vsInfantry : w.vsVehicle);
        damage(targetId, (int)(w.damage * mult), e.player, id);
        Effect ef;
        ef.kind = 2; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 8;
        effects.push_back(ef);
    } else if (strcmp(ps, "chrono") == 0) {
        // 超时空抹除枪：不造成伤害，叠加目标相位进度（冻结→抹除）
        Ent& te = ents[targetId];
        te.chrono += 20;
        Effect ef;
        ef.kind = 3; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 10;
        effects.push_back(ef);
    } else if (strcmp(ps, "prism") == 0) {
        float mult = t.isBuilding ? w.vsBuilding : (unitDef(t.utype).isInfantry() ? w.vsInfantry : w.vsVehicle);
        damage(targetId, (int)(w.damage * mult), e.player, id);
        Effect ef;
        ef.kind = 3; ef.x = sx; ef.y = sy; ef.x2 = tx; ef.y2 = ty; ef.maxAge = 10;
        effects.push_back(ef);
    } else if (strcmp(ps, "bullet") == 0 || strcmp(ps, "rad") == 0) {
        Projectile p;
        p.kind = ProjKind::Bullet; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        projs.push_back(p);
    } else if (strcmp(ps, "flak") == 0) {
        Projectile p;
        p.kind = ProjKind::Flak; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        projs.push_back(p);
    } else if (strcmp(ps, "missile") == 0) {
        Projectile p;
        p.kind = ProjKind::Missile; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        if (w.splash >= 1.0f) p.hp = 45; // 大型导弹（V3/无畏舰）：可被防空火力拦截（RA2 原作）
        projs.push_back(p);
    } else if (strcmp(ps, "naval") == 0 || strcmp(ps, "torpedo") == 0) {
        Projectile p;
        p.kind = ProjKind::Shell; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        projs.push_back(p);
    } else { // shell
        Projectile p;
        p.kind = ProjKind::Shell; p.player = e.player;
        p.x = sx; p.y = sy; p.tx = tx; p.ty = ty; p.target = targetId; p.src = id; p.w = w;
        projs.push_back(p);
    }
}

void World::damage(EID id, int dmg, int byPlayer, EID byEnt) {
    if (!valid(id) || dmg <= 0) return;
    Ent& e = ents[id];
    if (e.invuln > 0) return; // 铁幕无敌
    e.hp -= dmg;
    // 中立单位/建筑（player=-1）：无玩家状态，仅扣血与摧毁，跳过 EVA 与反击
    if (e.player < 0) {
        if (e.hp <= 0) { creditKill(byEnt, id); kill(id); }
        return;
    }
    Player& owner = players[e.player];
    // EVA 遇袭播报（节流，避免刷屏）
    if (byPlayer >= 0 && byPlayer != e.player) {
        if (e.isBuilding && owner.evaBaseCd <= 0) {
            eva(e.player, TR(S::EvaBaseAttack));
            owner.evaBaseCd = 480; // 16 秒
        } else if (!e.isBuilding && unitDef(e.utype).canHarvet() && owner.evaMinerCd <= 0) {
            eva(e.player, TR(S::EvaHarvAttack));
            owner.evaMinerCd = 480;
        }
    }
    if (e.hp <= 0) {
        if (e.isBuilding) eva(e.player, TextFormat(TR(S::EvaBldDestroyedFmt), bldName(e.btype)));
        else if (!unitDef(e.utype).isInfantry() && owner.evaUnitCd <= 0) {
            eva(e.player, TR(S::EvaUnitLost));
            owner.evaUnitCd = 150; // 5 秒
        }
        creditKill(byEnt, id);
        kill(id);
        return;
    }
    // 被打的单位反击
    if (!e.isBuilding && e.state == UState::Idle && byPlayer >= 0) {
        const UnitDef& ud = unitDef(e.utype);
        EID attacker = findNearestEnemy(e.player, e.x, e.y, 20, true, &ud.weapon);
        if (attacker != INVALID_EID) { e.target = attacker; e.state = UState::Chasing; }
    }
}

// 军衔经验（RA2 原作）：3 杀升老兵，再 6 杀升精英；老兵+15% 伤害，精英+30% 且缓慢自愈
void World::creditKill(EID byEnt, EID victim) {
    (void)victim;
    if (!valid(byEnt)) return;
    Ent& a = ents[byEnt];
    if (a.isBuilding) return; // 防御建筑不记军衔
    a.kills++;
    int need = a.vetRank == 0 ? 3 : 9;
    if (a.vetRank < 2 && a.kills >= need) {
        a.vetRank++;
        if (a.player == 0) {
            eva(0, TextFormat(TR(a.vetRank == 1 ? S::EvaPromoteVetFmt : S::EvaPromoteEliteFmt), unitName(a.utype)));
            g_sfx.play(Sfx::Ready, 0.6f);
        }
    }
}

void World::explodeAt(float x, float y, int big) {
    Effect ef;
    ef.kind = big >= 2 ? 4 : 0;
    ef.x = x; ef.y = y;
    ef.maxAge = big >= 2 ? 30 : 18;
    effects.push_back(ef);
    g_sfx.playAt(big >= 2 ? Sfx::BigExplosion : Sfx::Explosion, x, y);
}

void World::spawnFromFactory(int player, const UnitDef& u) {
    // 找第一个对应工厂
    for (size_t i = 0; i < ents.size(); i++) {
        Ent& b = ents[i];
        if (!b.alive || !b.isBuilding || b.player != player || !isFactoryFor(b.btype, u)) continue;
        const BldDef& bd = bldDef(b.btype);
        // 限弹药战机：直接落在空指部停机位（基洛夫/火箭飞行兵无限弹药，走地面出厂流程后升空）
        if (u.isAir() && u.ammo > 0) {
            EID nu = spawnUnit(player, u.type, 0, 0);
            Ent& ne = ents[nu];
            Vec2f pad = airPadPos(b, nu);
            ne.x = pad.x; ne.y = pad.y;
            ne.goalX = pad.x; ne.goalY = pad.y;
            ne.airbase = (int)i;
            ne.state = UState::Landed;
            ne.ammo = u.ammo;
            if (players[player].vetCat[u.prodCat()]) ne.vetRank = 1; // 间谍渗透工厂加成
            return;
        }
        // 出生点：建筑下方最近空格（海军单位须落在水面）
        int dom = u.pathDomain();
        for (int r = 1; r < 8; r++) {
            int sx = (int)b.x + bd.w / 2, sy = (int)b.y + bd.h + r - 1;
            if (passableFor(sx, sy, dom) && !bldBlocked(sx, sy) && unitAtCell(sx, sy) == INVALID_EID) {
                EID nu = spawnUnit(player, u.type, sx + 0.5f, sy + 0.5f);
                Ent& ne0 = ents[nu];
                if (players[player].vetCat[u.prodCat()]) ne0.vetRank = 1; // 间谍渗透工厂加成
                // 复制中心（RA2 原作）：兵营造步兵时，复制中心旁免费复制一个同类型
                if (u.isInfantry()) {
                    for (size_t ci = 0; ci < ents.size(); ci++) {
                        Ent& cv = ents[ci];
                        if (!cv.alive || !cv.isBuilding || cv.player != player || cv.btype != BldType::CloningVat) continue;
                        const BldDef& cbd = bldDef(cv.btype);
                        for (int r = 1; r < 6; r++) {
                            int cx2 = (int)cv.x + cbd.w / 2, cy2 = (int)cv.y + cbd.h + r - 1;
                            if (map.passable(cx2, cy2) && !bldBlocked(cx2, cy2) && unitAtCell(cx2, cy2) == INVALID_EID) {
                                EID dup = spawnUnit(player, u.type, cx2 + 0.5f, cy2 + 0.5f);
                                if (players[player].vetCat[u.prodCat()]) ents[dup].vetRank = 1;
                                r = 99; break;
                            }
                        }
                        break; // 一座复制中心即可生效
                    }
                }
                // 走向集结点
                if (b.rallyX >= 0) {
                    Ent& ne = ents[nu];
                    if (u.isAir()) {
                        // 空中单位：直线飞往集结点，无需寻路
                        ne.goalX = (float)b.rallyX; ne.goalY = (float)b.rallyY;
                        ne.state = UState::Moving;
                    } else {
                        std::vector<Vec2i> path;
                        if (map.findPath(sx, sy, b.rallyX, b.rallyY, path, 20000, dom)) {
                            ne.path = std::move(path); ne.pathIdx = 0;
                            ne.state = UState::Moving;
                            ne.goalX = (float)b.rallyX; ne.goalY = (float)b.rallyY;
                        }
                    }
                }
                return;
            }
        }
    }
}

void World::recomputePower() {
    for (auto& p : players) { p.powerMade = 0; p.powerUsed = 0; }
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.player < 0) continue;
        int pw = bldDef(e.btype).power;
        if (pw > 0) players[e.player].powerMade += pw;
        else players[e.player].powerUsed -= pw;
    }
}

void World::checkDefeat() {
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (!p.active || p.defeated) continue;
        bool hasAny = false;
        for (const Ent& e : ents)
            if (e.alive && e.player == pi) { hasAny = true; break; }
        if (!hasAny) p.defeated = true;
    }
}

void World::updateFog(int player) {
    map.clearVisible(player);
    // 间谍渗透雷达 或 间谍卫星（RA2 原作：建成后全图常亮，被毁后恢复战争迷雾）
    if (players[player].revealTimer > 0 || hasBld(player, BldType::SpySat)) {
        for (int y = 0; y < map.h; y++)
            for (int x = 0; x < map.w; x++)
                map.reveal(player, x, y, 0);
        return;
    }
    for (const Ent& e : ents) {
        if (!e.alive || e.player != player) continue;
        int sight;
        int cx, cy;
        if (e.isBuilding) {
            sight = bldDef(e.btype).sight;
            cx = (int)e.x + bldDef(e.btype).w / 2;
            cy = (int)e.y + bldDef(e.btype).h / 2;
        } else {
            sight = unitDef(e.utype).sight;
            cx = (int)e.x; cy = (int)e.y;
        }
        map.reveal(player, cx, cy, sight);
    }
}

// 裂缝产生器（RA2 原作）：黑幕半径内敌军迷雾打回不可见（间谍卫星也无法穿透；低电时失效）
void World::applyGapShroud() {
    const int R = 10;
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.btype != BldType::GapGenerator || e.player < 0) continue;
        if (players[e.player].lowPower()) continue;
        int cx = (int)e.x, cy = (int)e.y;
        for (int p = 0; p < numPlayers; p++) {
            if (!isEnemy(p, e.player)) continue;
            auto& f = map.fog[p];
            for (int dy = -R; dy <= R; dy++)
                for (int dx = -R; dx <= R; dx++) {
                    if (dx * dx + dy * dy > R * R) continue;
                    int x = cx + dx, y = cy + dy;
                    if (!map.inBounds(x, y)) continue;
                    f[(size_t)y * map.w + x] = FOG_UNSEEN;
                }
        }
    }
}

// ===================== 伞兵支援（RA2 原作：美国空指部 / 科技机场） =====================
bool World::hasParadropSource(int player) const {
    if (player < 0 || player >= numPlayers) return false;
    bool hasAFC = false;
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.player != player) continue;
        if (e.btype == BldType::TechAirport) return true; // 科技机场：任何阵营均提供伞兵
        if (e.btype == BldType::AirForceCmd) hasAFC = true;
    }
    // 美国特色：空指部附带伞兵支援
    return hasAFC && countryHasParadrop(players[player].country);
}

void World::updateParadrop() {
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (!p.active || p.defeated) continue;
        if (!hasParadropSource(pi)) { p.paradropCharge = 0; p.paradropReady = false; continue; }
        if (p.paradropReady) continue;
        if (++p.paradropCharge >= PARADROP_TIME) {
            p.paradropReady = true;
            eva(pi, TextFormat(TR(S::EvaSWReadyFmt), TR(S::Paradrop)));
            if (pi == 0) g_sfx.play(Sfx::Ready, 0.7f);
        }
    }
}

// 空投一波基础步兵到目标点周围（RA2 原作：8 名，按阵营给美国大兵/动员兵/解放军）
void World::orderParadrop(int player, float x, float y) {
    if (player < 0 || player >= numPlayers) return;
    Player& p = players[player];
    if (!p.paradropReady || !hasParadropSource(player)) return;
    if (!map.inBounds((int)x, (int)y) || map.at((int)x, (int)y).terrain == Terrain::Water) return;
    UnitType infT = p.faction == Faction::Allies ? UnitType::GI
                  : p.faction == Faction::Soviet ? UnitType::Conscript : UnitType::PLA;
    int dropped = 0;
    for (int k = 0; k < 8; k++) {
        bool ok = false;
        for (int r = 0; r <= 3 && !ok; r++)
            for (int dy = -r; dy <= r && !ok; dy++)
                for (int dx = -r; dx <= r && !ok; dx++) {
                    if (std::max(abs(dx), abs(dy)) != r) continue;
                    int nx = (int)x + dx, ny = (int)y + dy;
                    if (!map.passable(nx, ny) || map.at(nx, ny).terrain == Terrain::Water) continue;
                    if (bldBlocked(nx, ny) || unitAtCell(nx, ny) != INVALID_EID) continue;
                    EID u = spawnUnit(player, infT, nx + 0.5f, ny + 0.5f);
                    Effect ef; ef.kind = 9; ef.x = nx + 0.5f; ef.y = ny + 0.5f; ef.maxAge = 18; // 落地光效
                    effects.push_back(ef);
                    (void)u;
                    dropped++;
                    ok = true;
                }
    }
    if (dropped == 0) return; // 无落点：不扣充能
    p.paradropReady = false;
    p.paradropCharge = 0;
    eva(player, TR(S::EvaParadropDrop));
    g_sfx.playAt(Sfx::Place, x, y);
    // 空投区域短暂显形（双方都能看到伞兵落地）
    map.reveal(player, (int)x, (int)y, 6);
}

// 工程师占领特殊效果（RA2 原作：科技机场给伞兵、秘密实验室随机解锁国家特色科技）
void World::applyCaptureEffect(Ent& b, int newOwner) {
    if (newOwner < 0 || newOwner >= numPlayers) return;
    Player& p = players[newOwner];
    if (b.btype == BldType::TechAirport) {
        eva(newOwner, TR(S::EvaAirportCaptured)); // 伞兵充能由 updateParadrop 自动推进
    } else if (b.btype == BldType::SecretLab && p.secretLabUnlock == 0) {
        // 随机解锁一个本阵营其他国家（排除自身国家）的特色科技
        std::vector<Country> pool;
        for (Country c : countriesOf(p.faction))
            if (c != p.country) pool.push_back(c);
        if (!pool.empty()) {
            Country c = pool[rng.range(0, (int)pool.size() - 1)];
            p.secretLabUnlock = (int)c;
            eva(newOwner, TextFormat(TR(S::EvaSecretLabFmt), countryName(c)));
        }
    }
}

// ===================== 存档/读档 =====================
namespace {
// 二进制序列化助手：任何一步读写失败即标记 ok=false（调用方整体放弃）
struct Ser {
    FILE* f;
    bool ok = true;
    template <typename T> void w(const T& v) { if (ok && fwrite(&v, sizeof(v), 1, f) != 1) ok = false; }
    template <typename T> void r(T& v) { if (ok && fread(&v, sizeof(v), 1, f) != 1) ok = false; }
    void wbuf(const void* p, size_t n) { if (ok && n && fwrite(p, 1, n, f) != n) ok = false; }
    void rbuf(void* p, size_t n) { if (ok && n && fread(p, 1, n, f) != n) ok = false; }
    void wstr(const std::string& s) { uint32_t n = (uint32_t)s.size(); w(n); wbuf(s.data(), n); }
    void rstr(std::string& s) {
        uint32_t n = 0; r(n);
        if (!ok || n > (1u << 20)) { ok = false; return; }
        s.resize(n);
        rbuf(s.data(), n);
    }
};

// projSprite 是 const char*：存档写字符串，读档映射回静态字面量（悬空指针防护）
static const char* kProjSprites[] = {"shell", "bullet", "flak", "tesla", "prism", "missile"};
void serWeapon(Ser& s, const WeaponDef& wd) {
    s.w(wd.damage); s.w(wd.range); s.w(wd.cooldown);
    s.w(wd.antiAir); s.w(wd.antiGround);
    std::string ps = wd.projSprite ? wd.projSprite : "shell";
    s.wstr(ps);
    s.w(wd.vsInfantry); s.w(wd.vsVehicle); s.w(wd.vsBuilding);
    s.w(wd.navalOnly); s.w(wd.splash);
}
void deserWeapon(Ser& s, WeaponDef& wd) {
    s.r(wd.damage); s.r(wd.range); s.r(wd.cooldown);
    s.r(wd.antiAir); s.r(wd.antiGround);
    std::string ps; s.rstr(ps);
    wd.projSprite = "shell";
    for (const char* k : kProjSprites)
        if (ps == k) { wd.projSprite = k; break; }
    s.r(wd.vsInfantry); s.r(wd.vsVehicle); s.r(wd.vsBuilding);
    s.r(wd.navalOnly); s.r(wd.splash);
}
void serProd(Ser& s, const ProdItem& p) {
    s.w(p.active); s.w(p.isUnit); s.w(p.typeIdx); s.w(p.progress); s.w(p.ready);
}
void deserProd(Ser& s, ProdItem& p) {
    s.r(p.active); s.r(p.isUnit); s.r(p.typeIdx); s.r(p.progress); s.r(p.ready);
}
} // namespace

bool World::saveGame(FILE* f) const {
    Ser s{f};
    s.wbuf("RA2WRLD4", 8);
    s.w(tick); s.w(numPlayers); s.w(rng.s);
    s.w(cratesEnabled); s.w(aiAlliance);
    // 地图（含矿石余量与迷雾）
    s.w(map.w); s.w(map.h);
    for (const Cell& c : map.cells) {
        uint8_t t = (uint8_t)c.terrain, o = (uint8_t)c.overlay;
        s.w(t); s.w(o); s.w(c.variant); s.w(c.ore); s.w(c.oreMax);
    }
    for (int p = 0; p < numPlayers; p++)
        s.wbuf(map.fog[p].data(), map.fog[p].size());
    s.wbuf(bldOcc.data(), bldOcc.size() * sizeof(int));
    // 玩家
    for (const Player& p : players) {
        s.w(p.active); s.w(p.isAI); s.w(p.defeated);
        uint8_t fac = (uint8_t)p.faction;
        s.w(fac);
        uint8_t ctry = (uint8_t)p.country;
        s.w(ctry);
        s.w(p.secretLabUnlock); s.w(p.paradropCharge); s.w(p.paradropReady);
        s.w(p.colorId); s.w(p.money); s.wstr(p.name);
        serProd(s, p.bldProd);
        for (int c = 0; c < PROD_CAT_N; c++) serProd(s, p.unitProd[c]);
        for (int c = 0; c < PROD_CAT_N; c++) {
            uint32_t n = (uint32_t)p.unitQueue[c].size();
            s.w(n);
            for (int t : p.unitQueue[c]) s.w(t);
        }
        uint8_t pb = (uint8_t)p.placingBld;
        s.w(pb);
        s.w(p.powerSabotage); s.w(p.revealTimer);
        for (int c = 0; c < PROD_CAT_N; c++) s.w(p.vetCat[c]);
        s.w(p.stolenTech);
        s.w(p.aiDifficulty);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.w(p.swCharge[i]);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.w(p.swReady[i]);
        s.w(p.stormTimer); s.w(p.stormX); s.w(p.stormY); s.w(p.stormBoltCd);
        s.w(p.evaBaseCd); s.w(p.evaMinerCd); s.w(p.evaUnitCd);
    }
    // 实体
    {
        uint32_t n = (uint32_t)ents.size();
        s.w(n);
        for (const Ent& e : ents) {
            s.w(e.alive); s.w(e.isBuilding); s.w(e.player);
            uint8_t ut = (uint8_t)e.utype, bt = (uint8_t)e.btype, st = (uint8_t)e.state;
            s.w(ut); s.w(bt);
            s.w(e.x); s.w(e.y); s.w(e.dir); s.w(e.turretDir); s.w(e.hp);
            uint32_t pn = (uint32_t)e.path.size();
            s.w(pn);
            for (const Vec2i& wp : e.path) { s.w(wp.x); s.w(wp.y); }
            s.w(e.pathIdx); s.w(e.moveTick); s.w(e.blockTick);
            s.w(e.walkFrame); s.w(e.walkAnim);
            s.w(st); s.w(e.atkCd); s.w(e.target); s.w(e.goalX); s.w(e.goalY);
            s.w(e.oreLoad); s.w(e.oreCell.x); s.w(e.oreCell.y); s.w(e.dockRefinery); s.w(e.digTimer);
            s.w(e.invuln);
            s.w(e.ammo); s.w(e.rearmTimer); s.w(e.airbase); s.w(e.orbitA);
            s.w(e.rallyX); s.w(e.rallyY); s.w(e.bldAnim); s.w(e.undeploy); s.w(e.guard);
            uint32_t cn = (uint32_t)e.cargo.size();
            s.w(cn);
            for (UnitType t : e.cargo) { uint8_t ct = (uint8_t)t; s.w(ct); }
            s.w(e.chrono); s.w(e.tpSick); s.w(e.camouflaged); s.w(e.camoTick);
            s.w(e.radDeployed); s.w(e.deployed); s.w(e.subReveal);
            s.w(e.kills); s.w(e.vetRank);
            // 驻军/寄生/磁暴充电
            uint32_t gn = (uint32_t)e.garrison.size();
            s.w(gn);
            for (UnitType t : e.garrison) { uint8_t gt = (uint8_t)t; s.w(gt); }
            s.w(e.parasite); s.w(e.parasiteHost); s.w(e.parasiting); s.w(e.teslaCharge);
            // P6 心灵控制
            s.w(e.mindBy); s.w(e.mindTarget); s.w(e.origPlayer);
        }
        uint32_t fn = (uint32_t)freeList.size();
        s.w(fn);
        for (int id : freeList) s.w(id);
    }
    // 投射物 / 特效 / 核弹 / 补给箱 / 定时炸弹 / EVA 队列
    {
        uint32_t n = (uint32_t)projs.size();
        s.w(n);
        for (const Projectile& p : projs) {
            s.w(p.alive);
            uint8_t k = (uint8_t)p.kind;
            s.w(k);
            s.w(p.player); s.w(p.x); s.w(p.y); s.w(p.tx); s.w(p.ty);
            s.w(p.target); s.w(p.src);
            serWeapon(s, p.w);
            s.w(p.speed); s.w(p.trail); s.w(p.hp);
        }
    }
    {
        uint32_t n = (uint32_t)effects.size();
        s.w(n);
        for (const Effect& e : effects) {
            s.w(e.alive); s.w(e.kind); s.w(e.x); s.w(e.y); s.w(e.x2); s.w(e.y2); s.w(e.age); s.w(e.maxAge);
        }
    }
    {
        uint32_t n = (uint32_t)nukes.size();
        s.w(n);
        for (const Nuke& nk : nukes) { s.w(nk.active); s.w(nk.player); s.w(nk.tx); s.w(nk.ty); s.w(nk.timer); }
    }
    {
        uint32_t n = (uint32_t)crates.size();
        s.w(n);
        for (const Crate& c : crates) { s.w(c.alive); s.w(c.x); s.w(c.y); s.w(c.kind); }
    }
    {
        uint32_t n = (uint32_t)timedBombs.size();
        s.w(n);
        for (const TimedBomb& b : timedBombs) { s.w(b.x); s.w(b.y); s.w(b.timer); s.w(b.player); s.w(b.attachedTo); s.w(b.dmg); s.w(b.radius); }
    }
    {
        uint32_t n = (uint32_t)evaQueue.size();
        s.w(n);
        for (const EvaEvent& ev : evaQueue) { s.w(ev.player); s.wstr(ev.text); }
    }
    return s.ok;
}

bool World::loadGame(FILE* f) {
    Ser s{f};
    char magic[8];
    s.rbuf(magic, 8);
    if (!s.ok || memcmp(magic, "RA2WRLD4", 8) != 0) return false;
    s.r(tick); s.r(numPlayers); s.r(rng.s);
    s.r(cratesEnabled); s.r(aiAlliance);
    // 地图
    s.r(map.w); s.r(map.h);
    if (!s.ok || map.w <= 0 || map.h <= 0 || map.w > 512 || map.h > 512) return false;
    map.cells.resize((size_t)map.w * map.h);
    for (Cell& c : map.cells) {
        uint8_t t = 0, o = 0;
        s.r(t); s.r(o); s.r(c.variant); s.r(c.ore); s.r(c.oreMax);
        c.terrain = (Terrain)t; c.overlay = (Overlay)o;
    }
    map.fog.assign(numPlayers, std::vector<uint8_t>((size_t)map.w * map.h));
    for (int p = 0; p < numPlayers; p++)
        s.rbuf(map.fog[p].data(), map.fog[p].size());
    bldOcc.assign((size_t)map.w * map.h, -1);
    s.rbuf(bldOcc.data(), bldOcc.size() * sizeof(int));
    map.bldOccRef = &bldOcc; // 重新挂载寻路占用表
    // 玩家
    players.assign(numPlayers, Player{});
    for (Player& p : players) {
        s.r(p.active); s.r(p.isAI); s.r(p.defeated);
        uint8_t fac = 0;
        s.r(fac); p.faction = (Faction)fac;
        uint8_t ctry = 0;
        s.r(ctry); p.country = (Country)ctry;
        s.r(p.secretLabUnlock); s.r(p.paradropCharge); s.r(p.paradropReady);
        s.r(p.colorId); s.r(p.money); s.rstr(p.name);
        deserProd(s, p.bldProd);
        for (int c = 0; c < PROD_CAT_N; c++) deserProd(s, p.unitProd[c]);
        for (int c = 0; c < PROD_CAT_N; c++) {
            uint32_t n = 0;
            s.r(n);
            if (n > 64) { s.ok = false; break; }
            p.unitQueue[c].clear();
            for (uint32_t i = 0; i < n; i++) { int t = 0; s.r(t); p.unitQueue[c].push_back(t); }
        }
        uint8_t pb = 0;
        s.r(pb); p.placingBld = (BldType)pb;
        s.r(p.powerSabotage); s.r(p.revealTimer);
        for (int c = 0; c < PROD_CAT_N; c++) s.r(p.vetCat[c]);
        s.r(p.stolenTech);
        s.r(p.aiDifficulty);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.r(p.swCharge[i]);
        for (int i = 0; i < (int)SWType::COUNT; i++) s.r(p.swReady[i]);
        s.r(p.stormTimer); s.r(p.stormX); s.r(p.stormY); s.r(p.stormBoltCd);
        s.r(p.evaBaseCd); s.r(p.evaMinerCd); s.r(p.evaUnitCd);
    }
    // 实体
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 65536) return false;
        ents.assign(n, Ent{});
        for (Ent& e : ents) {
            s.r(e.alive); s.r(e.isBuilding); s.r(e.player);
            uint8_t ut = 0, bt = 0, st = 0;
            s.r(ut); s.r(bt);
            e.utype = (UnitType)ut; e.btype = (BldType)bt;
            s.r(e.x); s.r(e.y); s.r(e.dir); s.r(e.turretDir); s.r(e.hp);
            uint32_t pn = 0;
            s.r(pn);
            if (pn > 4096) { s.ok = false; break; }
            e.path.resize(pn);
            for (Vec2i& wp : e.path) { s.r(wp.x); s.r(wp.y); }
            s.r(e.pathIdx); s.r(e.moveTick); s.r(e.blockTick);
            s.r(e.walkFrame); s.r(e.walkAnim);
            s.r(st); e.state = (UState)st;
            s.r(e.atkCd); s.r(e.target); s.r(e.goalX); s.r(e.goalY);
            s.r(e.oreLoad); s.r(e.oreCell.x); s.r(e.oreCell.y); s.r(e.dockRefinery); s.r(e.digTimer);
            s.r(e.invuln);
            s.r(e.ammo); s.r(e.rearmTimer); s.r(e.airbase); s.r(e.orbitA);
            s.r(e.rallyX); s.r(e.rallyY); s.r(e.bldAnim); s.r(e.undeploy); s.r(e.guard);
            uint32_t cn = 0;
            s.r(cn);
            if (cn > 64) { s.ok = false; break; }
            e.cargo.resize(cn);
            for (UnitType& t : e.cargo) { uint8_t ct = 0; s.r(ct); t = (UnitType)ct; }
            s.r(e.chrono); s.r(e.tpSick); s.r(e.camouflaged); s.r(e.camoTick);
            s.r(e.radDeployed); s.r(e.deployed); s.r(e.subReveal);
            s.r(e.kills); s.r(e.vetRank);
            // 驻军/寄生/磁暴充电
            uint32_t gn = 0;
            s.r(gn);
            if (gn > 64) { s.ok = false; break; }
            e.garrison.resize(gn);
            for (UnitType& t : e.garrison) { uint8_t gt = 0; s.r(gt); t = (UnitType)gt; }
            s.r(e.parasite); s.r(e.parasiteHost); s.r(e.parasiting); s.r(e.teslaCharge);
            // P6 心灵控制
            s.r(e.mindBy); s.r(e.mindTarget); s.r(e.origPlayer);
        }
        uint32_t fn = 0;
        s.r(fn);
        if (fn > 65536) s.ok = false;
        freeList.clear();
        for (uint32_t i = 0; s.ok && i < fn; i++) { int id = 0; s.r(id); freeList.push_back(id); }
    }
    // 投射物 / 特效 / 核弹 / 补给箱 / 定时炸弹 / EVA 队列
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 8192) return false;
        projs.assign(n, Projectile{});
        for (Projectile& p : projs) {
            s.r(p.alive);
            uint8_t k = 0;
            s.r(k); p.kind = (ProjKind)k;
            s.r(p.player); s.r(p.x); s.r(p.y); s.r(p.tx); s.r(p.ty);
            s.r(p.target); s.r(p.src);
            deserWeapon(s, p.w);
            s.r(p.speed); s.r(p.trail); s.r(p.hp);
        }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 8192) return false;
        effects.assign(n, Effect{});
        for (Effect& e : effects) {
            s.r(e.alive); s.r(e.kind); s.r(e.x); s.r(e.y); s.r(e.x2); s.r(e.y2); s.r(e.age); s.r(e.maxAge);
        }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 64) return false;
        nukes.assign(n, Nuke{});
        for (Nuke& nk : nukes) { s.r(nk.active); s.r(nk.player); s.r(nk.tx); s.r(nk.ty); s.r(nk.timer); }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 1024) return false;
        crates.assign(n, Crate{});
        for (Crate& c : crates) { s.r(c.alive); s.r(c.x); s.r(c.y); s.r(c.kind); }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 1024) return false;
        timedBombs.assign(n, TimedBomb{});
        for (TimedBomb& b : timedBombs) { s.r(b.x); s.r(b.y); s.r(b.timer); s.r(b.player); s.r(b.attachedTo); s.r(b.dmg); s.r(b.radius); }
    }
    {
        uint32_t n = 0;
        s.r(n);
        if (!s.ok || n > 256) return false;
        evaQueue.clear();
        for (uint32_t i = 0; i < n; i++) {
            EvaEvent ev;
            s.r(ev.player); s.rstr(ev.text);
            evaQueue.push_back(std::move(ev));
        }
    }
    if (!s.ok) return false;
    recomputePower();
    return true;
}

// ===================== 心灵控制（P6，RA2 原作：尤里/心灵突击队） =====================
void World::mindControlRelease(Ent& yuri) {
    if (yuri.mindTarget == INVALID_EID) return;
    if (valid(yuri.mindTarget)) {
        Ent& t = ents[yuri.mindTarget];
        if (t.mindBy != INVALID_EID) {
            t.player = t.origPlayer;      // 恢复原属（RA2 原作：控制者死亡则解放）
            t.mindBy = INVALID_EID;
            t.origPlayer = -1;
            t.state = UState::Idle; t.target = INVALID_EID; t.path.clear();
        }
    }
    yuri.mindTarget = INVALID_EID;
}

void World::mindControlTake(Ent& yuri, EID yid, EID tid) {
    if (!valid(tid)) return;
    Ent& t = ents[tid];
    if (t.isBuilding || psychicImmune(t.utype) || t.mindBy != INVALID_EID) return;
    if (t.player < 0 || !isEnemy(yuri.player, t.player)) return;
    mindControlRelease(yuri); // RA2 原作：同一时刻仅控制一个单位，控制新目标即释放旧目标
    Ent& tt = ents[tid];      // release 不会触发实体扩容，但保险起见重新取引用
    tt.origPlayer = tt.player;
    tt.player = yuri.player;
    tt.mindBy = yid;
    yuri.mindTarget = tid;
    tt.state = UState::Idle; tt.target = INVALID_EID; tt.path.clear();
    // 心灵波特效（控制者→目标）
    Effect fx; fx.kind = 2; fx.x = yuri.x; fx.y = yuri.y; fx.x2 = tt.x; fx.y2 = tt.y; fx.maxAge = 12;
    effects.push_back(fx);
    g_sfx.playAt(Sfx::Tesla, tt.x, tt.y);
    if (yuri.player == 0) eva(0, TR(S::EvaMindGain));
    if (tt.origPlayer == 0) eva(0, TR(S::EvaMindLost));
}

// ===================== 间谍渗透 =====================
// RA2 原作效果：精炼厂=偷钱，电厂=断电，雷达=获取视野，兵营/工厂=新单位直接老兵，高科=破坏超武
void World::applySpyEffect(Ent& spy, Ent& bld, EID spyId) {
    int victim = bld.player;
    Player& sp = players[spy.player];
    const BldDef& bd = bldDef(bld.btype);
    switch (bld.btype) {
        case BldType::OreRefinery: {
            int steal = players[victim].money * 2 / 5; // 窃取对方 40% 资金
            players[victim].money -= steal;
            sp.money += steal;
            eva(spy.player, TextFormat(TR(S::SpyStealMoneyFmt), steal));
            if (victim >= 0) eva(victim, TR(S::SpyMoneyVictim));
            break;
        }
        case BldType::PowerPlant: case BldType::TeslaReactor: case BldType::NuclearReactor: {
            if (victim >= 0) {
                players[victim].powerSabotage = 30 * 30; // 断电 30 秒
                eva(victim, TR(S::SpyPowerVictim));
            }
            eva(spy.player, TR(S::SpyPowerOk));
            break;
        }
        case BldType::Radar: {
            sp.revealTimer = 30 * 60; // 全图视野 60 秒
            eva(spy.player, TR(S::SpyRadarOk));
            if (victim >= 0) {
                // RA2 原作：雷达被渗透 → 受害方战争迷雾重置（已探索区域重新遮蔽）
                for (auto& c : map.fog[victim])
                    if (c == FOG_SEEN) c = FOG_UNSEEN;
                eva(victim, TR(S::SpyRadarVictim));
            }
            break;
        }
        case BldType::Barracks:
            sp.vetCat[0] = true;
            eva(spy.player, TR(S::SpyBarracks));
            break;
        case BldType::WarFactory: case BldType::AirForceCmd:
            sp.vetCat[1] = true; sp.vetCat[2] = true;
            eva(spy.player, TR(S::SpyFactory));
            break;
        case BldType::NavalYard:
            sp.vetCat[3] = true;
            eva(spy.player, TR(S::SpyNavy));
            break;
        case BldType::BattleLab: {
            // 渗透高科：窃取 $1500 + RA2 原作偷科技 —— 盟高科→超时空突击队，苏/中高科→心灵突击队
            if (victim >= 0) {
                int steal = std::min(1500, players[victim].money);
                players[victim].money -= steal;
                sp.money += steal;
                int bit = (players[victim].faction == Faction::Allies) ? 1 : 2;
                if (!(sp.stolenTech & bit)) {
                    sp.stolenTech |= bit;
                    eva(spy.player, TR(bit == 1 ? S::SpyTechChrono : S::SpyTechPsi));
                }
                eva(victim, TR(S::SpyLabVictim));
            }
            eva(spy.player, TR(S::SpyLabOk));
            break;
        }
        // RA2 原作：渗透超武建筑 → 重置其充能倒计时
        case BldType::NukeSilo: case BldType::WeatherDevice:
        case BldType::IronCurtain: case BldType::ChronoSphere: {
            if (victim >= 0) {
                for (int i = 0; i < (int)SWType::COUNT; i++) { players[victim].swCharge[i] = 0; players[victim].swReady[i] = false; }
                eva(victim, TR(S::SpySWVictim));
            }
            eva(spy.player, TR(S::SpySWReset));
            break;
        }
        default:
            sp.revealTimer = 30 * 15; // 其他建筑：短暂全图侦查
            eva(spy.player, TextFormat(TR(S::SpyGenericFmt), bldName(bld.btype)));
            break;
    }
    g_sfx.playAt(Sfx::Eva, spy.x, spy.y);
    spy.alive = false; // 间谍消耗（RA2 原作设定）
    freeList.push_back(spyId);
}

// ===================== 疯狂伊文定时炸弹 =====================
void World::updateTimedBombs() {
    for (auto& b : timedBombs) {
        if (b.timer < 0) continue;
        // 附着目标：跟随其位置（被摧毁则留在原地）
        if (valid(b.attachedTo)) {
            const Ent& t = ents[b.attachedTo];
            b.x = t.x; b.y = t.y;
            if (t.isBuilding) { b.x += bldDef(t.btype).w / 2.0f; b.y += bldDef(t.btype).h / 2.0f; }
        }
        if (--b.timer > 0) continue;
        // 爆炸：按炸弹规格（伊文 400/2.5 格；谭雅 C4 6000/0.6 格单点爆破）
        const float R = b.radius;
        for (size_t i = 0; i < ents.size(); i++) {
            Ent& e = ents[i];
            if (!e.alive || e.invuln > 0) continue;
            float ex = e.x, ey = e.y;
            if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
            float d = distf(ex, ey, b.x, b.y);
            if (d > R) continue;
            int dmg = (int)(b.dmg * (1.0f - d / (R + 1.0f)));
            damage((int)i, dmg, b.player);
        }
        explodeAt(b.x, b.y, 1);
        b.timer = -1;
    }
    timedBombs.erase(std::remove_if(timedBombs.begin(), timedBombs.end(), [](const TimedBomb& b) { return b.timer < 0; }), timedBombs.end());
}

// ===================== 补给箱 =====================
void World::spawnCrateTick() {
    if (!cratesEnabled) return;
    if (tick % (30 * 40) != 0) return; // 每 40 秒尝试生成
    if ((int)crates.size() >= 6) return;
    for (int tries = 0; tries < 40; tries++) {
        int x = rng.range(2, map.w - 3), y = rng.range(2, map.h - 3);
        if (!map.passable(x, y) || bldBlocked(x, y)) continue;
        Crate c;
        c.x = x; c.y = y; c.kind = rng.range(0, 2);
        crates.push_back(c);
        break;
    }
}

void World::pickupCrates(Ent& e) {
    if (unitDef(e.utype).isAir()) return;
    for (auto& c : crates) {
        if (!c.alive) continue;
        if ((int)e.x != c.x || (int)e.y != c.y) continue;
        c.alive = false;
        g_sfx.playAt(Sfx::Cash, e.x, e.y);
        if (c.kind == 0) {
            players[e.player].money += 1000;
            if (e.player == 0) eva(0, TR(S::CrateMoney));
        } else if (c.kind == 1) {
            for (Ent& o : ents)
                if (o.alive && !o.isBuilding && o.player == e.player) o.hp = unitDef(o.utype).hp;
            if (e.player == 0) eva(0, TR(S::CrateHeal));
        } else {
            e.vetRank = std::min(2, e.vetRank + 1);
            if (e.player == 0) eva(0, TR(S::CrateVet));
        }
    }
    crates.erase(std::remove_if(crates.begin(), crates.end(), [](const Crate& c) { return !c.alive; }), crates.end());
}

// ===================== 矿脉再生 =====================
// RA2 矿钻等效：矿脉以极慢速度恢复，避免残局经济彻底枯竭
void World::regrowOre() {
    if (tick % 120 != 0) return; // 每 4 秒一批
    for (int k = 0; k < 32; k++) {
        int x = rng.range(0, map.w - 1), y = rng.range(0, map.h - 1);
        Cell& c = map.at(x, y);
        if (c.oreMax <= 0 || c.ore >= c.oreMax) continue;
        if (c.ore == 0) {
            // 采空的格子（已变 Rough）：无占用才恢复矿脉地形
            if (bldBlocked(x, y) || unitAtCell(x, y) != INVALID_EID) continue;
            c.terrain = c.oreMax <= 150 ? Terrain::Gems : Terrain::Ore;
        }
        c.ore++;
    }
}

// ===================== 超时空传送 =====================
// RA2 原作：传送车辆至目标点（步兵无法承受传送，会直接死亡；空军不可传送）
void World::chronoShiftUnits(const std::vector<EID>& sel, float tx, float ty) {
    bool anyInf = false;
    for (EID id : sel) {
        if (!valid(id)) continue;
        Ent& e = ents[id];
        if (e.isBuilding) continue;
        const UnitDef& ud = unitDef(e.utype);
        if (ud.isAir()) continue;
        if (ud.isInfantry()) { anyInf = true; kill(id); continue; } // 原作设定：传送步兵即死
        chronoJump(e, tx, ty);
    }
    if (anyInf) eva(0, TR(S::EvaInfNoChrono));
    g_sfx.playAt(Sfx::IronCurtain, tx, ty);
    Effect ef;
    ef.kind = 8; ef.x = tx; ef.y = ty; ef.maxAge = 40;
    effects.push_back(ef);
}

// ===================== P8 联机：命令执行与校验和 =====================
// 联机双端对称执行：命令内容决定一切，返回值与 UI 反馈由各端本地表现（不影响模拟）
void World::applyCmd(int player, const Cmd& c) {
    if (player < 0 || player >= numPlayers) return;
    switch (c.type) {
        case Cmd::Move:        orderMove(c.ids, c.x, c.y, c.attackMove); break;
        case Cmd::Attack:      orderAttack(c.ids, c.a); break;
        case Cmd::Harvest:     orderHarvest(c.ids, c.a, c.b); break;
        case Cmd::Stop:        orderStop(c.ids); break;
        case Cmd::Deploy:      if (!c.ids.empty()) orderDeploy(c.ids[0]); break;
        case Cmd::Capture:     orderCapture(c.ids, c.a); break;
        case Cmd::Repair:      orderRepair(c.ids, c.a); break;
        case Cmd::Scatter:     orderScatter(c.ids); break;
        case Cmd::Guard:       orderGuard(c.ids); break;
        case Cmd::Board:       orderBoard(c.ids, c.a); break;
        case Cmd::Unload:      orderUnload(c.ids); break;
        case Cmd::Garrison:    orderGarrison(c.ids, c.a); break;
        case Cmd::Ungarrison:  orderUngarrison(c.ids); break;
        case Cmd::RadDeploy:   orderRadDeploy(c.ids); break;
        case Cmd::Paradrop:    orderParadrop(player, c.x, c.y); break;
        case Cmd::Service:     orderService(c.ids, c.a); break;
        case Cmd::StartUnitProd:  startUnitProd(player, (UnitType)c.a); break;
        case Cmd::CancelUnitProd: cancelUnitProd(player, (UnitType)c.a); break;
        case Cmd::StartBldProd:   startBldProd(player, (BldType)c.a); break;
        case Cmd::CancelBldProd:  cancelProd(player, false); break;
        case Cmd::PlaceBuilding:  placeBuilding(player, (BldType)c.a, (int)c.x, (int)c.y); break;
        case Cmd::SetRally:    setRally(c.ids.empty() ? INVALID_EID : c.ids[0], (int)c.x, (int)c.y); break;
        case Cmd::SellBuilding:
            if (!c.ids.empty() && valid(c.ids[0]) && ents[c.ids[0]].player == player
                && ents[c.ids[0]].btype != BldType::ConYard) sellBuilding(c.ids[0]);
            break;
        case Cmd::RepairBuilding:
            if (!c.ids.empty() && valid(c.ids[0]) && ents[c.ids[0]].player == player) repairBuilding(c.ids[0]);
            break;
        case Cmd::LaunchSW:    launchSW(player, (SWType)c.a, c.x, c.y); break;
        default: break;
    }
}

// FNV-1a 校验和：覆盖影响模拟的全部状态（联机双端定期比对，不一致即不同步）
uint32_t World::checksum() const {
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix((uint32_t)tick); mix((uint32_t)numPlayers);
    mix((uint32_t)rng.s ^ (uint32_t)(rng.s >> 32));
    mix(cratesEnabled ? 1u : 0u); mix(aiAlliance ? 1u : 0u);
    for (const Player& p : players) {
        mix((uint32_t)p.money); mix((uint32_t)p.powerMade); mix((uint32_t)p.powerUsed);
        mix(p.defeated ? 1u : 0u);
    }
    for (const Ent& e : ents) {
        if (!e.alive) continue;
        mix((uint32_t)e.player); mix(e.hp);
        mix((uint32_t)(e.x * 64.0f)); mix((uint32_t)(e.y * 64.0f)); // 1/64 格精度量化
        mix((uint32_t)e.state); mix((uint32_t)e.target);
    }
    return h;
}
