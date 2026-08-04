#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

bool World::startUnitProd(int player, UnitType t) {
    Player& p = players[player];
    const UnitDef& u = unitDef(t);
    if (!modeAllowsUnit(player, t) || !unitPrereqMet(player, u)) return false;
    if (!hasFactoryFor(player, u)) return false;
    if (isHero(t)) {
        for (const Ent& e : ents) {
            if (!e.alive || e.player != player) continue;
            if (!e.isBuilding && e.utype == t) return false;
            for (const Ent::GarrisonedUnit& cargo : e.cargo) if (cargo.type == t) return false;
            for (const Ent::GarrisonedUnit& gu : e.garrison) if (gu.type == t) return false;
        }
        for (int c = 0; c < PROD_CAT_N; ++c) {
            if (p.unitProd[c].active && p.unitProd[c].typeIdx == (int)t) return false;
            for (int queued : p.unitQueue[c]) if (queued == (int)t) return false;
        }
    }
    int cat = u.prodCat();
    ProdItem& pr = p.unitProd[cat];
    if (!pr.active) {
        // RA2：缺钱也可开工，生产按 tick 扣款，钱不够则暂停进度
        pr.active = true;
        pr.isUnit = true;
        pr.typeIdx = (int)t;
        pr.progress = 0;
        pr.paid = 0;
        pr.held = false;
        pr.totalCost = unitProductionCost(player, t);
        pr.ready = false;
        return true;
    }
    // 排入队尾（RA2 约 30/类；含进行中）
    if ((int)p.unitQueue[cat].size() >= 29) return false;
    p.unitQueue[cat].push_back((int)t);
    return true;
}

void World::holdUnitProd(int player, UnitType t, bool hold) {
    Player& p = players[player];
    int cat = unitDef(t).prodCat();
    ProdItem& pr = p.unitProd[cat];
    if (pr.active && pr.typeIdx == (int)t && !pr.ready) pr.held = hold;
}

void World::holdBldProd(int player, BldType t, bool hold) {
    Player& p = players[player];
    ProdItem& pr = isDefenseBld(t) ? p.defProd : p.bldProd;
    if (pr.active && pr.typeIdx == (int)t && !pr.ready) pr.held = hold;
}

int World::harvesterCapacity(UnitType t) {
    switch (t) {
        case UnitType::ChronoMiner: return 20; // CMIN Storage=20
        case UnitType::Harvester:
        case UnitType::WarMiner:   return 40; // HARV Storage=40
        case UnitType::Slave:      return 5;
        case UnitType::SlaveMiner: return 20;
        default: return 20;
    }
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
        p.money = std::min(g_gameRules.maxMoney, p.money + pr.paid);
        pr = ProdItem{};
        if (!q.empty()) {
            int nt = q.front(); q.pop_front();
            pr.active = true; pr.isUnit = true; pr.typeIdx = nt; pr.progress = 0;
            pr.paid = 0; pr.held = false;
            pr.totalCost = unitProductionCost(player, (UnitType)nt); pr.ready = false;
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

ProdItem& World::bldQueueFor(int player, BldType t) {
    return isDefenseBld(t) ? players[player].defProd : players[player].bldProd;
}
const ProdItem& World::bldQueueFor(int player, BldType t) const {
    return isDefenseBld(t) ? players[player].defProd : players[player].bldProd;
}

bool World::startBldProd(int player, BldType t) {
    Player& p = players[player];
    const BldDef& d = bldDef(t);
    ProdItem& pr = bldQueueFor(player, t);
    if (pr.active || !modeAllowsBuilding(player, t)) return false;
    if (!hasBld(player, BldType::ConYard)) return false;
    if (!prereqMet(player, d)) return false;
    if (isUniqueBld(t) && countBlds(player, t) > 0) return false;
    // RA2：建筑/防御双队列可并行；缺钱也可开工，按 tick 扣款
    pr.active = true;
    pr.isUnit = false;
    pr.typeIdx = (int)t;
    pr.progress = 0;
    pr.paid = 0;
    pr.held = false;
    pr.totalCost = d.cost;
    pr.ready = false;
    return true;
}

void World::cancelBldProd(int player, BldType t) {
    ProdItem& pr = bldQueueFor(player, t);
    if (!pr.active || pr.typeIdx != (int)t) return;
    players[player].money = std::min(g_gameRules.maxMoney, players[player].money + pr.paid);
    pr = ProdItem{};
}

void World::cancelProd(int player, bool isUnit) {
    // 单位取消走 cancelUnitProd（分类队列）；此处取消活跃建筑队列（优先建筑栏）
    if (isUnit) return;
    Player& p = players[player];
    ProdItem* pr = nullptr;
    if (p.bldProd.active) pr = &p.bldProd;
    else if (p.defProd.active) pr = &p.defProd;
    if (!pr) return;
    p.money = std::min(g_gameRules.maxMoney, p.money + pr->paid);
    *pr = ProdItem{};
}

int World::unitProductionCost(int player, UnitType t) const {
    const UnitDef& u = unitDef(t);
    bool vehicle = u.prodCat() == 1;
    bool plant = player >= 0 && player < numPlayers && hasBld(player, BldType::IndustrialPlant);
    return industrialPlantUnitCost(u.cost, vehicle, plant);
}

bool World::canPlace(BldType t, int bx, int by, int player) const {
    const BldDef& d = bldDef(t);
    bool naval = (t == BldType::NavalYard); // 船厂必须全建于水面
    // 占地检查（地形 + 既有建筑 + 地面单位 —— RA2 不可压单位建造）
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
            if (groundUnitBlocksCell(x, y, INVALID_EID)) return false;
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

bool World::canDeployMcv(EID id) const {
    if (!valid(id) || ents[id].isBuilding || ents[id].utype != UnitType::MCV) return false;
    const Ent& e = ents[id];
    int bx = (int)e.x - 1, by = (int)e.y - 1;
    const BldDef& d = bldDef(BldType::ConYard);
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int cx = bx + dx, cy = by + dy;
            if (!map.inBounds(cx, cy)) return false;
            if (!map.passable(cx, cy) || map.at(cx, cy).terrain == Terrain::Bridge) return false;
            if (bldOcc[cellIdx(cx, cy)] > 0) return false;
            if (groundUnitBlocksCell(cx, cy, id)) return false;
        }
    return true;
}

bool World::placeBuilding(int player, BldType t, int bx, int by) {
    Player& p = players[player];
    ProdItem& pr = bldQueueFor(player, t);
    if (!pr.active || !pr.ready || pr.typeIdx != (int)t) return false;
    if (!canPlace(t, bx, by, player)) return false;
    const BldDef& d = bldDef(t);
    spawnBuilding(player, t, bx, by, true); // 钱已在生产中扣除
    pr = ProdItem{};
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
    if (!isRallyBuilding(ents[factory].btype)) return;
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
    if (e.hp >= d.hp) { e.repairing = false; return false; }
    // RA2：切换持续维修（每 tick 扣款回血），再次点击可取消
    e.repairing = !e.repairing;
    if (e.repairing) g_sfx.playAt(Sfx::Click, e.x, e.y);
    return e.repairing;
}

// ===================== 超级武器 =====================

bool World::spawnFromFactory(int player, const UnitDef& u) {
    // 找第一个对应工厂
    for (size_t i = 0; i < ents.size(); i++) {
        Ent& b = ents[i];
        if (!b.alive || !b.isBuilding || b.player != player || !isFactoryFor(b.btype, u)) continue;
        const BldDef& bd = bldDef(b.btype);
        // 缓存工厂标量：后续 spawnUnit 可能触发 ents 扩容，持有的 Ent& 会悬空
        const float fx = b.x, fy = b.y;
        const int rallyX = b.rallyX, rallyY = b.rallyY;
        // 限弹药战机：直接落在空指部停机位（基洛夫/火箭飞行兵无限弹药，走地面出厂流程后升空）
        if (u.isAir() && u.ammo > 0) {
            EID nu = spawnUnit(player, u.type, 0, 0);
            Vec2f pad = airPadPos(ents[i], nu);
            Ent& ne = ents[nu];
            ne.x = pad.x; ne.y = pad.y;
            ne.goalX = pad.x; ne.goalY = pad.y;
            ne.airbase = (int)i;
            ne.state = UState::Landed;
            ne.ammo = u.ammo;
            if (players[player].vetCat[u.prodCat()]) ne.vetRank = 1; // 间谍渗透工厂加成
            return true;
        }
        // 出生点：建筑下方最近空格（海军单位须落在水面）
        int dom = u.pathDomain();
        for (int r = 1; r < 8; r++) {
            int sx = (int)fx + bd.w / 2, sy = (int)fy + bd.h + r - 1;
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
                // 走向集结点（用缓存的 rally 值：spawnUnit 后 b 已悬空）
                if (rallyX >= 0) {
                    Ent& ne = ents[nu];
                    if (u.isAir()) {
                        // 空中单位：直线飞往集结点，无需寻路
                        ne.goalX = (float)rallyX; ne.goalY = (float)rallyY;
                        ne.state = UState::Moving;
                    } else {
                        std::vector<Vec2i> path;
                        if (map.findPath(sx, sy, rallyX, rallyY, path, 20000, dom)) {
                            ne.path = std::move(path); ne.pathIdx = 0;
                            ne.state = UState::Moving;
                            ne.goalX = (float)rallyX; ne.goalY = (float)rallyY;
                        }
                    }
                }
                return true;
            }
        }
    }
    return false; // 所有合法出口均堵塞：保留完成项，待下一帧重试
}

void World::recomputePower() {
    for (auto& p : players) { p.powerMade = 0; p.powerUsed = 0; }
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.player < 0) continue;
        int pw = bldDef(e.btype).power;
        if (pw > 0 && e.drainedBy != INVALID_EID) pw = 0;
        if (e.btype == BldType::BioReactor)
            pw += (int)e.garrison.size() * g_gameRules.bioReactorPowerPerOccupant;
        if (pw > 0) players[e.player].powerMade += pw;
        else players[e.player].powerUsed -= pw;
    }
}

void World::checkDefeat() {
    for (int pi = 0; pi < numPlayers; pi++) {
        Player& p = players[pi];
        if (!p.active || p.defeated) continue;
        bool hasAny = false;
        for (const Ent& e : ents) {
            if (!e.alive || e.player != pi) continue;
            if (!shortGame || e.isBuilding || e.utype == UnitType::MCV) { hasAny = true; break; }
        }
        if (!hasAny) {
            p.defeated = true;
            g_script.onPlayerDefeated(pi);
        }
    }
}

