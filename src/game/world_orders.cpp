#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

void World::orderMove(const std::vector<EID>& sel, float x, float y, bool attackMove, bool append) {
    // MCV Repacks：选中建造厂下达移动 → 先打包成基地车再寻路
    std::vector<EID> units;
    units.reserve(sel.size());
    for (EID id : sel) {
        if (!valid(id)) continue;
        Ent& e = ents[id];
        if (e.isBuilding && e.btype == BldType::ConYard && mcvRepacks) {
            EID mcv = packConYardToMcv(id);
            if (valid(mcv)) units.push_back(mcv);
            continue;
        }
        if (!e.isBuilding) units.push_back(id);
    }
    int n = 0;
    for (EID id : units) {
        if (!valid(id)) continue;
        Ent& e = ents[id];
        const UnitDef& ud = unitDef(e.utype);
        float lx = x, ly = y; // 本单元目标点（append 时可能被队首替换，不能污染共享参数）
        if (append) { // 路径点追加：入队；移动中则到位自动接续，空闲立即启程
            e.wps.push_back({lx, ly});
            if (e.state == UState::Moving || e.state == UState::AttackMoving) continue;
            lx = e.wps.front().first; ly = e.wps.front().second;
            e.wps.pop_front();
        } else {
            e.wps.clear();
        }
        e.target = INVALID_EID;
        e.guard = false;
        // 部署态不可移动：须先收起（部署键）才能走
        if (e.deployed || e.radDeployed) continue;
        if (ud.canHarvet()) e.autoHarvest = false; // 手动移动暂停自动寻矿
        e.goalX = lx; e.goalY = ly;
        // 目标点按单位散开（方阵）—— RA2 标准间距 1.5 格
        int cols = (int)ceilf(sqrtf((float)units.size()));
        float ox = lx + (n % cols - cols / 2) * 1.5f;
        float oy = ly + (n / cols) * 1.5f;
        n++;
        if (ud.isAir()) {
            // 战机：直线飞行，无视地形
            e.goalX = ox; e.goalY = oy;
            e.orbitA = (float)(id % 8) * 0.785f;
            e.state = attackMove ? UState::AttackMoving : UState::Moving;
            continue;
        }
        if (e.utype == UnitType::Chrono || e.utype == UnitType::ChronoCommando
            || e.utype == UnitType::ChronoIvan) {
            // 超时空系：传送移动（RA2/YR：军团兵、突击队、超时空伊文）
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
            // Move 将 autoHarvest=false；卸完后不自动再采，直到显式采矿令
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
        e.wps.clear(); // 直接攻击指令取消路径点队列
        if (ud.weapon.damage == 0) {
            // 间谍：点敌方步兵=伪装；点敌方建筑=渗透
            if (e.utype == UnitType::Spy && isEnemy(e.player, t.player)) {
                if (!t.isBuilding && unitDef(t.utype).isInfantry()) {
                    // RA2：选中后「攻击」目标兵种 → 立刻变成该形象（含敌方色）
                    e.camouflaged = true;
                    int cid = (t.player >= 0 && t.player < numPlayers) ? players[t.player].colorId : 0;
                    e.camoTick = (int)t.utype | ((cid & 0xFF) << 16);
                    e.target = INVALID_EID;
                    e.path.clear();
                    e.state = UState::Idle;
                    g_sfx.playAt(Sfx::Click, e.x, e.y);
                } else if (t.isBuilding) {
                    e.target = target;
                    e.guard = false;
                    int ax = (int)tx, ay = (int)ty;
                    approachBuildingCell(*this, (int)e.x, (int)e.y, t, ud.pathDomain(), ax, ay);
                    std::vector<Vec2i> path;
                    map.findPath((int)e.x, (int)e.y, ax, ay, path, 20000, ud.pathDomain());
                    e.path = std::move(path);
                    e.pathIdx = 0;
                    e.state = UState::Chasing;
                }
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
            e.path.clear(); // 射程内停步开火（含炮塔单位；RA2：到位再打）
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
        e.wps.clear();
        e.oreCell = {x, y};
        e.target = INVALID_EID;
        e.guard = false;
        e.autoHarvest = true; // 显式采矿命令恢复自动寻矿
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, x, y, path);
        e.path = std::move(path);
        e.pathIdx = 0;
        e.state = UState::HarvestGo;
    }
}

void World::orderReturnToRefinery(const std::vector<EID>& sel, EID preferredRef) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (!unitDef(e.utype).canHarvet()) continue;
        if (e.utype == UnitType::SlaveMiner && e.deployed) continue;
        e.wps.clear();
        e.target = INVALID_EID;
        e.guard = false;
        e.autoHarvest = true; // 显式回厂后卸完可再出发
        beginHarvesterReturn(e, preferredRef);
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
        e.wps.clear(); // 停止清空路径点队列
        e.target = INVALID_EID;
        e.guard = false;
        if (unitDef(e.utype).canHarvet())
            e.autoHarvest = false; // Stop 后暂停自动寻矿，直到再次下达采矿命令
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
        e.wps.clear();
        e.target = INVALID_EID;
        e.guard = false;
        // 在 2~4 格内找随机可通行落点
        for (int tries = 0; tries < 8; tries++) {
            int dx = rng.range(-4, 4), dy = rng.range(-4, 4);
            if (abs(dx) < 2 && abs(dy) < 2) continue;
            int nx = (int)e.x + dx, ny = (int)e.y + dy;
            if (!passableStep((int)e.x, (int)e.y, nx, ny, dom) || bldBlocked(nx, ny) || unitAtCell(nx, ny) != INVALID_EID) continue;
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
        e.wps.clear();
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

// 运输载具卸下乘员：放到周围陆地空格（RA2 原作要求邻近陆地）；恢复生命/军衔
void World::orderUnload(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        // 先拷贝货舱清单（spawnUnit 可能触发 ents 扩容，引用会悬空）
        std::vector<Ent::GarrisonedUnit> out;
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
                    Ent::GarrisonedUnit saved = out.back();
                    out.pop_back();
                    EID nid = spawnUnit(owner, saved.type, nx + 0.5f, ny + 0.5f);
                    if (valid(nid)) {
                        Ent& u = ents[nid];
                        u.hp = std::clamp(saved.hp, 1, unitDef(saved.type).hp);
                        u.kills = saved.kills;
                        u.veterancyValue = saved.veterancyValue;
                        u.vetRank = std::clamp(saved.vetRank, 0, 2);
                    }
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
    if (sel.empty() || !valid(bldId) || !ents[bldId].isBuilding) return;
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
        // RA2 手册：民房仅大兵系可进；战斗碉堡等仍允许一般步兵
        if (b.btype == BldType::CivHouse && !canGarrisonCivHouse(e.utype)) continue;
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
void World::evacuateGarrison(EID bldId) {
    if (!valid(bldId) || !ents[bldId].isBuilding) return;
    // 先拷贝驻军清单并清空（spawnUnit 可能触发 ents 扩容，引用会悬空）
    std::vector<Ent::GarrisonedUnit> out;
    int owner = -1;
    BldType bt = BldType::COUNT;
    float cx = 0, cy = 0;
    {
        Ent& b = ents[bldId];
        if (b.garrison.empty()) return;
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
                Ent::GarrisonedUnit saved = out.back();
                EID uid = spawnUnit(owner, saved.type, nx + 0.5f, ny + 0.5f);
                Ent& restored = ents[uid];
                restored.hp = std::clamp(saved.hp, 1, unitDef(saved.type).hp);
                restored.kills = saved.kills;
                restored.veterancyValue = saved.veterancyValue;
                restored.vetRank = saved.vetRank;
                out.pop_back();
            }
    // 放不下的驻军返还建筑（极少见：建筑被围死）
    if (!out.empty() && valid(bldId)) {
        Ent& b = ents[bldId];
        if (b.player < 0) b.player = owner; // 恢复归属以容纳驻军
        b.garrison = std::move(out);
    }
    recomputePower();
}

void World::orderUngarrison(const std::vector<EID>& sel) {
    for (EID id : sel) {
        if (!valid(id) || !ents[id].isBuilding || ents[id].garrison.empty()) continue;
        int owner = ents[id].player;
        evacuateGarrison(id);
        if (owner >= 0 && valid(id) && ents[id].garrison.empty())
            eva(owner, TR(S::EvaUnloadDone));
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
        if (e.hp >= maxHpFor(e, ud) && e.parasite == INVALID_EID) continue; // 满血且无寄生
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
        } else if (e.utype == UnitType::SiegeChopper) {
            // 攻城直升机：部署=降落转入远程炮击模式 / 收起=重新升空（YR 原作设定）
            e.deployed = !e.deployed;
            if (e.deployed) {
                e.path.clear();
                e.target = INVALID_EID;
                e.guard = false;
                e.goalX = e.x; e.goalY = e.y;
                e.state = UState::Idle;
                g_sfx.playAt(Sfx::Deploy, e.x, e.y);
            }
        } else if (e.utype == UnitType::V3Launcher) {
            // V3 火箭发射车：须部署才可开火；部署后不可移动
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
    // YR 1.001：普通/永久心控下的 MCV/建造厂不可展开或打包
    if (e.mindBy != INVALID_EID || e.permaControlled) return;
    // 奴隶矿车：部署后成为移动卸货点并自动产奴（YR 奴隶经济主路径）
    if (e.utype == UnitType::SlaveMiner && !e.isBuilding) {
        e.deployed = !e.deployed;
        e.path.clear();
        e.state = UState::Idle;
        g_sfx.playAt(Sfx::Deploy, e.x, e.y);
        return;
    }
    // MCV Repacks：建造厂打包回基地车
    if (e.isBuilding && e.btype == BldType::ConYard) {
        packConYardToMcv(id);
        return;
    }
    if (e.utype != UnitType::MCV) return;
    if (!canDeployMcv(id)) return;
    int bx = (int)e.x - 1, by = (int)e.y - 1;
    int pl = e.player;
    e.alive = false;
    freeList.push_back(id);
    spawnBuilding(pl, BldType::ConYard, bx, by, true);
    map.reveal(pl, bx + 1, by + 1, 8);
    g_sfx.playAt(Sfx::Deploy, (float)bx + 1, (float)by + 1);
}

EID World::packConYardToMcv(EID id) {
    if (!valid(id)) return INVALID_EID;
    Ent& e = ents[id];
    if (!e.isBuilding || e.btype != BldType::ConYard) return INVALID_EID;
    if (!mcvRepacks) return INVALID_EID;
    if (e.mindBy != INVALID_EID || e.permaControlled) return INVALID_EID;
    int pl = e.player;
    float sx = e.x + 1.5f, sy = e.y + 1.5f;
    const BldDef& d = bldDef(e.btype);
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int cx = (int)e.x + dx, cy = (int)e.y + dy;
            if (map.inBounds(cx, cy)) bldOcc[cellIdx(cx, cy)] = -1;
        }
    e.alive = false;
    freeList.push_back(id);
    recomputePower();
    EID mcv = spawnUnit(pl, UnitType::MCV, sx, sy);
    g_sfx.playAt(Sfx::Deploy, sx, sy);
    return mcv;
}

void World::orderCapture(const std::vector<EID>& sel, EID bldId) {
    if (!valid(bldId) || !ents[bldId].isBuilding) return;
    Ent& b = ents[bldId];
    if (!bldDef(b.btype).capturable) return;
    for (EID id : sel) {
        if (!valid(id) || ents[id].isBuilding) continue;
        Ent& e = ents[id];
        if (e.utype != UnitType::Engineer) continue;
        if (e.player == b.player) continue; // 不可占己方
        // 走到建筑贴边（非脚印中心，避免被占地挡死）
        int ax = (int)b.x, ay = (int)b.y;
        const UnitDef& ud = unitDef(e.utype);
        approachBuildingCell(*this, (int)e.x, (int)e.y, b, ud.pathDomain(), ax, ay);
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, ax, ay, path, 20000, ud.pathDomain());
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
        int ax = (int)b.x, ay = (int)b.y;
        const UnitDef& ud = unitDef(e.utype);
        approachBuildingCell(*this, (int)e.x, (int)e.y, b, ud.pathDomain(), ax, ay);
        std::vector<Vec2i> path;
        map.findPath((int)e.x, (int)e.y, ax, ay, path, 20000, ud.pathDomain());
        e.path = std::move(path);
        e.pathIdx = 0;
        e.target = bldId;
        e.state = UState::Chasing;
    }
}

// ===================== 生产 =====================
// 分类生产队列（RA2 原作）：步兵/车辆/空军/海军各自独立排队，当前项空则立即开工，否则排入队尾

