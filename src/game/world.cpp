#include "game/world.h"
#include "game/lang.h"
#include "game/script.h"
#include "core/content.h"
#include "game/presentation_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <atomic>

uint64_t World::nextPresentationEpoch() {
    static std::atomic<uint64_t> sequence{0};
    return ++sequence;
}


// ===================== 初始化 =====================
bool World::init(int w, int h, uint64_t seed, int numHumans, int numAI, const std::vector<Faction>& factions, int mapType,
                 const char* mapFile, bool noStartForce, bool requireMap) {
    presentationEpoch=nextPresentationEpoch();
    presentationEvents.clear(); presentationSequence = 0;
    repathsThisTick = 0; repathRequests = repathFailures = 0; movementReservations.clear(); resetMovementCorridors();
    std::fill_n(producedUnits, MAX_PLAYERS, 0); std::fill_n(harvestedIncome, MAX_PLAYERS, 0); std::fill_n(combatDamage, MAX_PLAYERS, 0);
    // 全局状态复位：支持局内"重新开始"不留残局
    ents.clear();
    freeList.clear();
    updateInProgress=false;areaDamageDepth=0;retiredDuringUpdate.clear();trafficGates.clear();
    projs.clear();
    effects.clear();
    nukes.clear();
    crates.clear();
    timedBombs.clear();
    evaQueue.clear();
    tick = 0;
    rng = Rng(seed);
    numPlayers = numHumans + numAI;
    // P7：手工地图（地形/实体/出生点来自 maps/xxx.txt）；加载失败回退程序生成
    std::vector<Vec2i> spawns;
    std::vector<PendingEnt> pend;
    bool hand = false;
    std::string resolvedMap;
    if (mapFile && mapFile[0]) {
        resolvedMap = contentResolve(mapFile);
        if (resolvedMap.empty()) resolvedMap = mapFile;
        hand = loadHandMap(resolvedMap.c_str(), numPlayers, spawns, pend);
        if (!hand) std::fprintf(stderr, "RA2 hand map failed%s: %s\n", requireMap ? "" : ", fallback to generate", resolvedMap.c_str());
    }
    if (requireMap && !hand) return false;
    if (!hand) map.generate(w, h, seed, numPlayers, spawns, mapType);
    map.initFog(numPlayers);
    bldOcc.assign((size_t)map.w * map.h, -1);
    unitOccHead.assign((size_t)map.w * map.h, INVALID_EID);
    airBucketW = (map.w + AIR_BUCKET - 1) / AIR_BUCKET;
    airOccHead.assign((size_t)airBucketW * ((map.h + AIR_BUCKET - 1) / AIR_BUCKET), INVALID_EID);
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
        // 初始护卫：必须在 MCV 展开 4×4 脚印外（脚印= (sp-1)..(sp+2)），否则无法展开建造厂
        UnitType tankT = p.faction == Faction::Allies ? UnitType::Grizzly
                       : p.faction == Faction::Soviet ? UnitType::Rhino
                       : p.faction == Faction::Yuri ? UnitType::LasherTank : UnitType::Type99;
        spawnUnit(i, tankT, sp.x + 3.5f, sp.y + 1.5f);
        spawnUnit(i, tankT, sp.x - 2.5f, sp.y + 1.5f);
        UnitType infT = p.faction == Faction::Allies ? UnitType::GI
                      : p.faction == Faction::Soviet ? UnitType::Conscript
                      : p.faction == Faction::Yuri ? UnitType::Initiate : UnitType::PLA;
        spawnUnit(i, infT, sp.x + 1.5f, sp.y - 2.5f);
        spawnUnit(i, infT, sp.x + 1.5f, sp.y + 3.5f);
        map.reveal(i, sp.x, sp.y, 10);
    }
    if (hand) {
        // 地图文件预置实体（建筑/单位；精炼厂附赠矿车与生产放置一致，保障 AI 经济启动）
        for (const PendingEnt& pe : pend) {
            if (pe.isBld) {
                BldType bt = (BldType)pe.typeIdx;
                EID bid = spawnBuilding(pe.player, bt, pe.x, pe.y, true);
                if (pe.hpOverride > 0 && valid(bid)) ents[bid].hp = pe.hpOverride;
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
                if (pe.hpOverride > 0 && valid(uid)) ents[uid].hp = pe.hpOverride;
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
    return true;
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
    {"Initiate", UnitType::Initiate}, {"Brute", UnitType::Brute}, {"Virus", UnitType::Virus},
    {"LasherTank", UnitType::LasherTank}, {"GatlingTank", UnitType::GatlingTank},
    {"Magnetron", UnitType::Magnetron}, {"MasterMind", UnitType::MasterMind},
    {"FloatingDisc", UnitType::FloatingDisc}, {"Boomer", UnitType::Boomer},
    {"Boris", UnitType::Boris}, {"SiegeChopper", UnitType::SiegeChopper}, {"ChaosDrone", UnitType::ChaosDrone},
    {"Slave", UnitType::Slave}, {"SlaveMiner", UnitType::SlaveMiner},
    {"YuriPrime", UnitType::YuriPrime}, {"ChronoIvan", UnitType::ChronoIvan},
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
        std::fprintf(stderr, "RA2 hand map %s:%d bad %s: %s", path, lineNo, what, line);
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
            const UnitVariant* uv = nullptr;
            int got = sscanf(line, "%*s %d %31s %d %d %31s", &p, tn, &x, &y, extra);
            if (got < 4 || !resolveUnitSpawn(tn, ut, &uv) || p >= numPlayers) { bad("unit"); break; }
            PendingEnt pe;
            pe.isBld = false; pe.player = p; pe.typeIdx = (int)ut; pe.x = x; pe.y = y;
            pe.guard = got >= 5 && !strcmp(extra, "guard");
            if (uv && uv->hp > 0) pe.hpOverride = uv->hp;
            out.push_back(pe);
        } else if (!strcmp(kw, "bld")) {
            int p, x, y; char tn[32];
            BldType bt;
            const BldVariant* bv = nullptr;
            if (sscanf(line, "%*s %d %31s %d %d", &p, tn, &x, &y) != 4 || !resolveBldSpawn(tn, bt, &bv) || p >= numPlayers) { bad("bld"); break; }
            PendingEnt pe;
            pe.isBld = true; pe.player = p; pe.typeIdx = (int)bt; pe.x = x; pe.y = y;
            if (bv && bv->hp > 0) pe.hpOverride = bv->hp;
            out.push_back(pe);
        } else {
            bad("unknown directive");
            break;
        }
    }
    fclose(f);
    if (!ok || !sized || (int)spawns.size() < numPlayers) return false;
    // 贴图变体：确定性散列（与 generate 的 rng.range(0,7) 同范围）
    for (int y = 0; y < map.h; y++)
        for (int x = 0; x < map.w; x++)
            map.at(x, y).variant = (uint8_t)(hm::hash3((uint64_t)x, (uint64_t)y, 7) & 7);
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
    int nTechPP = area >= 96 * 96 ? 2 : 1;              // 科技电厂（占领后 +200 电力）
    int nTechOut = area >= 96 * 96 ? 1 : 0;             // 科技前哨站（占领后全单位维修+回血）
    struct Want { BldType t; int n; };
    const Want wants[] = {
        {BldType::OilDerrick, nOil}, {BldType::Hospital, nHosp}, {BldType::MachineShop, nShop},
        {BldType::TechAirport, nAirport}, {BldType::SecretLab, nLab}, {BldType::CivHouse, nHouse},
        {BldType::TechPowerPlant, nTechPP}, {BldType::TechOutpost, nTechOut},
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

void World::invalidateEntityReferences(EID id) {
    if(id<0 || id>=(EID)ents.size()) return;
    // No allocation in ents here: callers may be retiring a slot from an update.
    const float x=ents[id].x,y=ents[id].y;
    float tx=x,ty=y;
    if(ents[id].isBuilding) { tx+=bldDef(ents[id].btype).w/2.0f;ty+=bldDef(ents[id].btype).h/2.0f; }
    bool powerChanged=false;
    for(EID i=0;i<(EID)ents.size();++i) {
        auto& unit=ents[i];
        std::erase_if(unit.failedPursuits,[&](const auto& failure) { return failure.target==id; });
        if(!unit.alive) continue;
        if(unit.burstTarget==id || i==id)unit.cancelBurst();
        if(unit.target==id) {
            if(unit.state==UState::Servicing) { endService(unit,i,ServiceRejectReason::InvalidTarget); }
            if(unit.feedbackEntryTarget==id)
                emitEntryResult(unit.player,id,unit.feedbackEntryKind,{0,1,i,EntryRejectReason::InvalidTarget});
            if(unit.engineerIntent!=EngineerIntent::None && unit.feedbackEngineerTarget==id)
                emitEngineerResult(unit.player,id,unit.engineerIntent,{0,1,i,EngineerRejectReason::InvalidTarget});
            unit.target=INVALID_EID;
        }
        if(unit.feedbackAttackTarget==id) unit.feedbackAttackTarget=INVALID_EID;
        if(unit.feedbackEngineerTarget==id) unit.feedbackEngineerTarget=INVALID_EID;
        if(unit.feedbackEntryTarget==id) unit.feedbackEntryTarget=INVALID_EID;
        if(unit.airbase==id) unit.airbase=INVALID_EID;
        if(unit.dockRefinery==id) unit.dockRefinery=INVALID_EID;
        if(unit.parasite==id) unit.parasite=INVALID_EID;
        if(unit.parasiteHost==id) {
            unit.parasiteHost=INVALID_EID;unit.parasiting=false;unit.x=x;unit.y=y;
            unit.state=UState::Idle;unit.target=INVALID_EID;unit.clearPath();unitOccSync(i);airOccSync(i);
        }
        if(unit.mindTarget==id) unit.mindTarget=INVALID_EID;
        std::erase(unit.mindTargets,id);
        if(unit.mindBy==id) {
            if(!unit.permaControlled && unit.origPlayer>=0) unit.player=unit.origPlayer;
            unit.mindBy=INVALID_EID;unit.origPlayer=-1;unit.target=INVALID_EID;
            unit.state=UState::Idle;unit.clearPath();unit.engineerIntent=EngineerIntent::None;
        }
        if(unit.magneticBy==id) unit.magneticBy=INVALID_EID; // retain height for next-tick falling damage
        if(unit.drainedBy==id) { unit.drainedBy=INVALID_EID;powerChanged=true; }
    }
    for(auto& projectile:projs) {
        if(projectile.target==id) {
            projectile.target=INVALID_EID;
            if(projectile.rot>0) { projectile.tx=tx;projectile.ty=ty; }
        }
        if(projectile.src==id) { projectile.src=INVALID_EID;projectile.srcGarrisonSlot=-1; }
    }
    for(auto& bomb:timedBombs) if(bomb.attachedTo==id) {
        bomb.attachedTo=INVALID_EID;bomb.x=tx;bomb.y=ty;
    }
    if(powerChanged) recomputePower();
}

void World::consumeEntity(EID id) {
    if(!valid(id)) return;
    ents[id].engineerIntent=EngineerIntent::None;ents[id].feedbackEngineerTarget=INVALID_EID;
    ents[id].alive=false;
    unitOccSync(id); airOccSync(id);
    invalidateEntityReferences(id);
    retireEntitySlot(id);
}

void World::retireEntitySlot(EID id) {
    // An outer weapon/update may still hold the dead entity by reference.
    // Never overwrite that slot until all simulation callbacks have returned.
    if(updateInProgress || areaDamageDepth>0) retiredDuringUpdate.push_back(id);
    else freeList.push_back(id);
}

EID World::allocEnt() {
    if (!freeList.empty()) {
        int id = freeList.back(); freeList.pop_back();
        // A reused slot is a new target even when its type and position match.
        invalidateEntityReferences(id); // also handles references in historical snapshots
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
    e.px = x; e.py = y; // 渲染插值起点（避免从 (0,0) 拉花）
    e.hp = unitDef(t).hp;
    e.dir = rng.range(0, 7);
    e.turretDir = e.dir;
    if (unitDef(t).isAir()) {
        e.ammo = unitDef(t).ammo;
        e.goalX = x; e.goalY = y;
        e.state = UState::Circling;
    }
    // 航空母舰：出厂自带满编舰载机（RA2 原作：3 架大黄蜂）
    if (t == UnitType::AircraftCarrier) {
        Ent::GarrisonedUnit slot{};
        slot.type = UnitType::Hornet;
        slot.hp = unitDef(UnitType::Hornet).hp;
        e.cargo.assign(unitDef(t).cargoCap, slot);
    }
    unitOccSync(id);
    airOccSync(id);
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
    e.rallyX = std::clamp(bx+d.w/2,0,map.w-1);e.rallyY=std::clamp(by+d.h+1,0,map.h-1);
    if(t==BldType::AirForceCmd || t==BldType::OreRefinery) e.rallyX=e.rallyY=-1;
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++)
            bldOcc[cellIdx(bx + dx, by + dy)] = id + 1;
    if (!free_ && player >= 0) players[player].money -= d.cost;
    // 建造动画（mk 关键帧序列）：仅玩家现建；地图/中立预置直接完整显示
    if (!free_) {
        e.constructAnim = g_gameRules.buildingAnimationTicks; // 每帧 5 tick
    }
    resetMovementCorridors();
    recomputePower();
    // 超武建筑落成：向其他玩家发出侦测警告（RA2 原作设定）
    if (bldProvidesSW(t) != SWType::COUNT && tick > 10) {
        for (int p = 0; p < numPlayers; p++)
            if (p != player) eva(p, formatText(TR(S::EvaDetectEnemySWFmt), bldName(t)));
    }
    if (g_script.boundTo(this)) g_script.onBuildingComplete(id, player, t);
    return id;
}

void World::kill(EID id, bool explode) {
    if (!valid(id)) return;
    Ent e = ents[id]; // death effects/scripts may spawn and reallocate ents
    // 脚本 hook：死亡事件（在 alive 置 false 前捕获类型/玩家）
    bool wasBld = e.isBuilding;
    int deadPlayer = e.player;
    UnitType deadUtype = e.utype;
    BldType deadBtype = e.btype;
    bool wasSelling = e.selling;
    ents[id].engineerIntent=EngineerIntent::None;ents[id].feedbackEngineerTarget=INVALID_EID;
    ents[id].alive = false;
    unitOccSync(id); // alive=false → 从占位链表摘除
    airOccSync(id);
    if (e.isBuilding) {
        resetMovementCorridors();
        const BldDef& d = bldDef(e.btype);
        for (int dy = 0; dy < d.h; dy++)
            for (int dx = 0; dx < d.w; dx++) {
                int cx = (int)e.x + dx, cy = (int)e.y + dy;
                if (map.inBounds(cx, cy)) bldOcc[cellIdx(cx, cy)] = -1;
            }
        if (explode) explodeAt(e.x + d.w / 2.0f, e.y + d.h / 2.0f, 2);
        recomputePower();
        // 驻军随建筑一同阵亡（RA2 原作设定）；出售拆除时驻军同样清空
        e.garrison.clear();
        // 核电站熔毁：仅击毁时触发（出售不熔毁）
        if (explode && !wasSelling && deadBtype == BldType::NuclearReactor) {
            float cx = e.x + d.w / 2.0f, cy = e.y + d.h / 2.0f;
            const float R = 4.5f;
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& o = ents[i];
                if (!o.alive || o.invuln > 0) continue;
                if (o.airborne()) continue;
                float ox = o.x, oy = o.y;
                if (o.isBuilding) { ox += bldDef(o.btype).w / 2.0f; oy += bldDef(o.btype).h / 2.0f; }
                float dist = distf(ox, oy, cx, cy);
                if (dist > R) continue;
                int dmg = (int)(350 * (1.0f - dist / (R + 1.0f)));
                if (!o.isBuilding && unitDef(o.utype).isInfantry()) dmg = (int)(dmg * 1.6f);
                damage((int)i, dmg, deadPlayer);
            }
            for (int wave = 0; wave < 4; wave++) {
                TimedBomb b;
                b.x = cx; b.y = cy; b.player = deadPlayer;
                b.dmg = 80; b.radius = 3.5f; b.timer = 30 + wave * 45;
                timedBombs.push_back(b);
            }
            Effect ef; ef.kind = 12; ef.x = cx; ef.y = cy; ef.maxAge = 90;
            effects.push_back(ef);
        }
    } else {
        // 步兵有序列死亡动画（art.ini Die1）：播放倒地序列；车辆/无素材单位保持爆炸
        if (unitDef(e.utype).isInfantry()) {
            Effect da; da.kind = 10; da.x = e.x; da.y = e.y;
            da.maxAge = g_gameRules.deathAnimationTicks; // 每相位 2 tick
            da.aux = (int)e.utype; da.aux2 = e.dir;
            da.aux3 = e.player >= 0 ? players[e.player].colorId : -1;
            effects.push_back(da);
        } else {
            explodeAt(e.x, e.y, unitDef(e.utype).isInfantry() ? 0 : 1);
        }
        // 基洛夫空艇被击落：坠毁冲击波（RA2 原作签名机制）
        if (e.utype == UnitType::Kirov) {
            explodeAt(e.x, e.y, 2);
            const float R = 3.0f;
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& o = ents[i];
                if (!o.alive || o.invuln > 0) continue;
                if (o.airborne()) continue;
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
            dr.clearPath();
            unitOccSync(e.parasite);airOccSync(e.parasite);
        }
        // 恐怖机器人被消灭（维修厂摘除）：解除宿主寄生标记
        if (e.parasiting && valid(e.parasiteHost)) ents[e.parasiteHost].parasite = INVALID_EID;
        // 心灵控制：被控单位阵亡 → 解除控制者链接（RA2 原作）
        if (e.mindBy != INVALID_EID && valid(e.mindBy) && ents[e.mindBy].mindTarget == id)
            ents[e.mindBy].mindTarget = INVALID_EID;
        // 控制者阵亡 → 被控单位恢复原属（RA2 原作）
        if (e.mindTarget != INVALID_EID) mindControlRelease(e);
        for (EID tid : e.mindTargets) {
            if (!valid(tid)) continue;
            Ent& t = ents[tid];
            if (t.mindBy == id && !t.permaControlled) {
                t.player = t.origPlayer;
                t.mindBy = INVALID_EID;
                t.origPlayer = -1;
                t.state = UState::Idle; t.target = INVALID_EID; t.clearPath();
            }
        }
        e.mindTargets.clear();
        // 飞碟/磁电坦克被毁时，被影响对象在后续更新自动恢复/坠落。
    }
    ents[id].garrison.clear();ents[id].mindTargets.clear();ents[id].mindTarget=e.mindTarget;
    invalidateEntityReferences(id);
    // Publish the slot only after recursive effects and script callbacks finish.
    // 脚本 hook：死亡/被毁事件
    if (g_script.boundTo(this)) {
        if (wasBld) g_script.onBuildingDestroyed(id, deadPlayer, deadBtype);
        else g_script.onUnitKilled(id, deadPlayer, deadUtype);
    }
    checkDefeat();
    retireEntitySlot(id);
}

// ===================== 查询 =====================


// ===================== 伞兵支援（RA2 原作：美国空指部 / 科技机场） =====================

// ===================== 心灵控制（P6，RA2 原作：尤里/心灵突击队） =====================
// RA2 原作：传送车辆至目标点（步兵无法承受传送，会直接死亡；空军不可传送）
// 联机双端对称执行：命令内容决定一切，返回值与 UI 反馈由各端本地表现（不影响模拟）
