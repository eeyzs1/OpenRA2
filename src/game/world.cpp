#include "game/world.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

// ===================== 初始化 =====================
void World::init(int w, int h, uint64_t seed, int numHumans, int numAI, const std::vector<Faction>& factions, int mapType) {
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
    std::vector<Vec2i> spawns;
    map.generate(w, h, seed, numPlayers, spawns, mapType);
    map.initFog(numPlayers);
    bldOcc.assign((size_t)w * h, -1);
    map.bldOccRef = &bldOcc; // 寻路避开建筑占用
    players.assign(numPlayers, Player{});

    for (int i = 0; i < numPlayers; i++) {
        Player& p = players[i];
        p.active = true;
        p.isAI = i >= numHumans;
        p.faction = factions[i % factions.size()];
        p.colorId = i;
        p.money = 10000;
        p.name = p.isAI ? ("AI-" + std::to_string(i)) : "指挥官";
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
    placeNeutralTechs();
}

// 中立科技建筑：随机撒布油井/医院/机械店（player=-1，工程师占领后生效）
void World::placeNeutralTechs() {
    int area = map.w * map.h;
    int nOil = std::max(2, area / 1800);
    int nHosp = std::max(1, area / 3600);
    int nShop = std::max(1, area / 3600);
    struct Want { BldType t; int n; };
    const Want wants[] = {{BldType::OilDerrick, nOil}, {BldType::Hospital, nHosp}, {BldType::MachineShop, nShop}};
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
            if (p != player) eva(p, std::string("警告：侦测到敌方") + d.name);
    }
    return id;
}

void World::kill(EID id) {
    if (!valid(id)) return;
    Ent& e = ents[id];
    if (e.isBuilding) {
        const BldDef& d = bldDef(e.btype);
        for (int dy = 0; dy < d.h; dy++)
            for (int dx = 0; dx < d.w; dx++) {
                int cx = (int)e.x + dx, cy = (int)e.y + dy;
                if (map.inBounds(cx, cy)) bldOcc[cellIdx(cx, cy)] = -1;
            }
        explodeAt(e.x + d.w / 2.0f, e.y + d.h / 2.0f, 2);
        recomputePower();
    } else {
        explodeAt(e.x, e.y, unitDef(e.utype).isInfantry() ? 0 : 1);
    }
    e.alive = false;
    freeList.push_back(id);
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
    return d.prereq == BldType::COUNT || hasBld(player, d.prereq);
}

bool World::unitPrereqMet(int player, const UnitDef& u) const {
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
        }
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

// 反潜探测单位（驱逐舰/神盾舰/海蝎，RA2 原作为驱逐舰声呐/海豚）
bool World::isDetector(UnitType t) const {
    return t == UnitType::Destroyer || t == UnitType::Aegis || t == UnitType::SeaScorpion;
}

// 单位可见性：潜艇隐身时仅本家/探测单位/贴脸可见
bool World::visibleTo(const Ent& e, int viewer) const {
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
        if (e.utype == UnitType::Harvester && !attackMove) {
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
        // 射界检查：打空中目标需 antiAir，打地面需 antiGround；鱼雷仅限水上目标
        if (airT && !ud.weapon.antiAir) continue;
        if (!airT && !ud.weapon.antiGround) continue;
        if (ud.weapon.navalOnly && !waterT) continue;
        e.target = target;
        e.guard = false;
        float d = distf(e.x, e.y, tx, ty);
        if (d <= ud.weapon.range) {
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
        if (e.utype != UnitType::Harvester) continue;
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
        Ent& e = ents[id];
        if (unitDef(e.utype).cargoCap == 0 || e.cargo.empty()) continue;
        for (int r = 1; r <= 2 && !e.cargo.empty(); r++)
            for (int dy = -r; dy <= r && !e.cargo.empty(); dy++)
                for (int dx = -r; dx <= r && !e.cargo.empty(); dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = (int)e.x + dx, ny = (int)e.y + dy;
                    if (!map.passable(nx, ny) || map.at(nx, ny).terrain == Terrain::Water) continue;
                    if (bldBlocked(nx, ny) || unitAtCell(nx, ny) != INVALID_EID) continue;
                    spawnUnit(e.player, e.cargo.back(), nx + 0.5f, ny + 0.5f);
                    e.cargo.pop_back();
                }
        if (e.cargo.empty()) eva(e.player, "卸载完成");
        else eva(e.player, "警告：无可靠岸地点，无法卸载");
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
        } else if (e.utype == UnitType::GuardianGI) {
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
            if (!map.passable(cx, cy) || bldOcc[cellIdx(cx, cy)] > 0) return;
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
    if (t == BldType::OreRefinery) {
        for (int r = 1; r < 6; r++) {
            int sx = bx + d.w / 2, sy = by + d.h + r - 1;
            if (map.passable(sx, sy) && !bldBlocked(sx, sy) && unitAtCell(sx, sy) == INVALID_EID) {
                spawnUnit(player, UnitType::Harvester, sx + 0.5f, sy + 0.5f);
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
            evaAll("警告：核弹已发射");
            g_sfx.play(Sfx::NukeLaunch, 0.9f);
            break;
        }
        case SWType::Lightning: {
            p.stormTimer = 30 * 14; // 持续 14 秒
            p.stormX = tx; p.stormY = ty;
            p.stormBoltCd = 0;
            evaAll("警告：闪电风暴接近中");
            g_sfx.playAt(Sfx::Storm, tx, ty);
            break;
        }
        case SWType::IronCurtain: {
            // 目标点 3 格内己方单位/建筑无敌 20 秒
            for (size_t i = 0; i < ents.size(); i++) {
                Ent& e = ents[i];
                if (!e.alive || e.player != player) continue;
                float ex = e.x, ey = e.y;
                if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
                if (distf(ex, ey, tx, ty) <= 3.0f) e.invuln = 30 * 20;
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
            eva(player, "超时空传送启动");
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
                eva(pi, std::string(swDef((SWType)i).name) + "已就绪");
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
        if (!e.alive || e.isBuilding || e.utype != UnitType::Harvester) continue;
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
}

void World::updateUnit(Ent& e, EID id) {
    const UnitDef& ud = unitDef(e.utype);
    if (e.atkCd > 0) e.atkCd--;
    if (e.invuln > 0) e.invuln--;
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
                eva(e.player, "单位损失");
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
    // 台风潜艇：开火暴露计时衰减
    if (e.subReveal > 0) e.subReveal--;
    // 重装大兵已部署：不能移动，以反装甲炮迎战（RA2 原作设定）
    if (e.utype == UnitType::GuardianGI && e.deployed) {
        const WeaponDef& dw = ggiDeployedWeapon();
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

    // 工程师到达目标建筑：占领
    if (e.utype == UnitType::Engineer && e.target != INVALID_EID && valid(e.target)) {
        Ent& b = ents[e.target];
        if (b.isBuilding && b.player != e.player) {
            const BldDef& bd = bldDef(b.btype);
            float bx = b.x + bd.w / 2.0f, by = b.y + bd.h / 2.0f;
            if (distf(e.x, e.y, bx, by) < std::max(bd.w, bd.h) / 2.0f + 1.5f) {
                eva(b.player, std::string("建筑被占领：") + bd.name);
                eva(e.player, std::string("已占领：") + bd.name);
                b.player = e.player;
                b.hp = bd.hp;
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
                eva(e.player, std::string("工程师已修复：") + bd.name);
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
            if (ud.weapon.damage > 0) {
                float scanR = e.guard ? (float)std::max(ud.sight, ud.weapon.range + 2)
                                      : (float)(ud.weapon.range + 2);
                EID en = findNearestEnemy(e.player, e.x, e.y, scanR, true, &ud.weapon, e.utype);
                if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; }
            }
            break;
        }
        case UState::Moving:
        case UState::AttackMoving: {
            if (e.state == UState::AttackMoving && ud.weapon.damage > 0) {
                EID en = findNearestEnemy(e.player, e.x, e.y, (float)(ud.weapon.range + 1), true, &ud.weapon, e.utype);
                if (en != INVALID_EID) { e.target = en; e.state = UState::Chasing; break; }
            }
            moveAlongPath(e, id);
            if (e.pathIdx >= (int)e.path.size()) e.state = UState::Idle;
            break;
        }
        case UState::Chasing: {
            if (!valid(e.target)) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            const Ent& t = ents[e.target];
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            float d = distf(e.x, e.y, tx, ty);
            if (d <= ud.weapon.range) {
                e.path.clear();
                e.state = UState::Attacking;
            } else {
                // 超时空军团兵追击：直接传送至目标射程边缘
                if (e.utype == UnitType::Chrono) {
                    float nx = tx - (tx - e.x) / d * (ud.weapon.range * 0.8f);
                    float ny = ty - (ty - e.y) / d * (ud.weapon.range * 0.8f);
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
            float tx = t.x, ty = t.y;
            if (t.isBuilding) { tx += bldDef(t.btype).w / 2.0f; ty += bldDef(t.btype).h / 2.0f; }
            float d = distf(e.x, e.y, tx, ty);
            if (d > ud.weapon.range + 1) { e.state = UState::Chasing; break; }
            // 面向目标
            int wantDir = dirFromVec(tx - e.x, ty - e.y);
            e.turretDir = wantDir;
            if (!g_sprites.hasTurret(e.utype)) e.dir = wantDir;
            if (e.atkCd <= 0) {
                fireWeapon(e, id, e.target);
                e.atkCd = ud.weapon.cooldown;
            }
            break;
        }
        case UState::HarvestGo: case UState::HarvestDig: case UState::HarvestReturn: case UState::HarvestUnload:
            updateHarvester(e, id);
            break;
        case UState::Boarding: {
            // 步兵登船：接近运输载具后进入货舱
            if (!valid(e.target) || ents[e.target].isBuilding) { e.target = INVALID_EID; e.state = UState::Idle; break; }
            {
                Ent& t = ents[e.target];
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
            if (vp.evaUnitCd <= 0) { eva(o.player, "单位损失"); vp.evaUnitCd = 150; }
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
    if (bd.weapon.damage > 0 && e.player >= 0 && !players[e.player].lowPower()) {
        float cx = e.x + bd.w / 2.0f, cy = e.y + bd.h / 2.0f;
        if (!valid(e.target) || distf(cx, cy, ents[e.target].x, ents[e.target].y) > bd.weapon.range + 1) {
            e.target = findNearestEnemy(e.player, cx, cy, (float)bd.weapon.range, false, &bd.weapon);
            if (e.target == INVALID_EID) e.target = findNearestEnemy(e.player, cx, cy, (float)bd.weapon.range, true, &bd.weapon);
        }
        if (valid(e.target) && e.atkCd <= 0) {
            fireWeapon(e, id, e.target);
            e.atkCd = bd.weapon.cooldown;
        }
    }
}

void World::fireWeapon(Ent& e, EID id, EID targetId) {
    if (!valid(targetId)) return;
    (void)id;
    const Ent& t = ents[targetId];
    WeaponDef w = e.isBuilding ? bldDef(e.btype).weapon : unitDef(e.utype).weapon;
    // 重装大兵部署后切换为反装甲炮
    if (!e.isBuilding && e.utype == UnitType::GuardianGI && e.deployed) w = ggiDeployedWeapon();
    // 军衔伤害加成：老兵 +15%，精英 +30%
    if (!e.isBuilding && e.vetRank > 0) w.damage = (int)(w.damage * (1.0f + 0.15f * e.vetRank));
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
            eva(e.player, "警告：基地遭受攻击");
            owner.evaBaseCd = 480; // 16 秒
        } else if (!e.isBuilding && e.utype == UnitType::Harvester && owner.evaMinerCd <= 0) {
            eva(e.player, "警告：采矿车遭受攻击");
            owner.evaMinerCd = 480;
        }
    }
    if (e.hp <= 0) {
        if (e.isBuilding) eva(e.player, std::string("建筑被摧毁：") + bldDef(e.btype).name);
        else if (!unitDef(e.utype).isInfantry() && owner.evaUnitCd <= 0) {
            eva(e.player, "单位损失");
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
            eva(0, std::string(unitDef(a.utype).name) + (a.vetRank == 1 ? "晋升为老兵" : "晋升为精英"));
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
    // 间谍渗透雷达：全图可见
    if (players[player].revealTimer > 0) {
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
    s.wbuf("RA2WRLD1", 8);
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
            s.w(p.speed); s.w(p.trail);
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
        for (const TimedBomb& b : timedBombs) { s.w(b.x); s.w(b.y); s.w(b.timer); s.w(b.player); s.w(b.attachedTo); }
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
    if (!s.ok || memcmp(magic, "RA2WRLD1", 8) != 0) return false;
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
            s.r(p.speed); s.r(p.trail);
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
        for (TimedBomb& b : timedBombs) { s.r(b.x); s.r(b.y); s.r(b.timer); s.r(b.player); s.r(b.attachedTo); }
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
            eva(spy.player, "间谍渗透：窃取资金 $" + std::to_string(steal));
            if (victim >= 0) eva(victim, "警告：精炼厂被间谍渗透，资金失窃");
            break;
        }
        case BldType::PowerPlant: case BldType::TeslaReactor: case BldType::NuclearReactor: {
            if (victim >= 0) {
                players[victim].powerSabotage = 30 * 30; // 断电 30 秒
                eva(victim, "警告：电厂被间谍破坏，电力瘫痪");
            }
            eva(spy.player, "间谍渗透：敌方电力瘫痪 30 秒");
            break;
        }
        case BldType::Radar: {
            sp.revealTimer = 30 * 60; // 全图视野 60 秒
            eva(spy.player, "间谍渗透：已获取敌方雷达数据");
            if (victim >= 0) eva(victim, "警告：雷达站被间谍渗透");
            break;
        }
        case BldType::Barracks:
            sp.vetCat[0] = true;
            eva(spy.player, "间谍渗透：新兵营单位直接晋升老兵");
            break;
        case BldType::WarFactory: case BldType::AirForceCmd:
            sp.vetCat[1] = true; sp.vetCat[2] = true;
            eva(spy.player, "间谍渗透：新车辆/空军单位直接晋升老兵");
            break;
        case BldType::NavalYard:
            sp.vetCat[3] = true;
            eva(spy.player, "间谍渗透：新海军单位直接晋升老兵");
            break;
        case BldType::BattleLab: {
            // 渗透高科：窃取 $1500 并重置对方超武充能
            if (victim >= 0) {
                int steal = std::min(1500, players[victim].money);
                players[victim].money -= steal;
                sp.money += steal;
                for (int i = 0; i < (int)SWType::COUNT; i++) { players[victim].swCharge[i] = 0; players[victim].swReady[i] = false; }
                eva(victim, "警告：作战实验室被间谍渗透");
            }
            eva(spy.player, "间谍渗透：窃取技术资料，敌方超武充能已重置");
            break;
        }
        default:
            sp.revealTimer = 30 * 15; // 其他建筑：短暂全图侦查
            eva(spy.player, std::string("间谍渗透：") + bd.name);
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
        // 爆炸：半径 2.5 格，中心 400 伤害递减
        const float R = 2.5f;
        for (size_t i = 0; i < ents.size(); i++) {
            Ent& e = ents[i];
            if (!e.alive || e.invuln > 0) continue;
            float ex = e.x, ey = e.y;
            if (e.isBuilding) { ex += bldDef(e.btype).w / 2.0f; ey += bldDef(e.btype).h / 2.0f; }
            float d = distf(ex, ey, b.x, b.y);
            if (d > R) continue;
            int dmg = (int)(400 * (1.0f - d / (R + 1.0f)));
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
            if (e.player == 0) eva(0, "补给箱：获得资金 $1000");
        } else if (c.kind == 1) {
            for (Ent& o : ents)
                if (o.alive && !o.isBuilding && o.player == e.player) o.hp = unitDef(o.utype).hp;
            if (e.player == 0) eva(0, "补给箱：全体单位完全治疗");
        } else {
            e.vetRank = std::min(2, e.vetRank + 1);
            if (e.player == 0) eva(0, "补给箱：单位军衔晋升");
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
    if (anyInf) eva(0, "警告：步兵无法承受超时空传送");
    g_sfx.playAt(Sfx::IronCurtain, tx, ty);
    Effect ef;
    ef.kind = 8; ef.x = tx; ef.y = ty; ef.maxAge = 40;
    effects.push_back(ef);
}
