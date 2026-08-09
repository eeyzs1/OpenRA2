#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {
// RA2 OreRefinery 4×3：DockUnload=(3,1) 东侧垫；QueueingCell=(4,1) 在地基外可寻路
constexpr int kTiberiumNearScan = 6;
constexpr int kTiberiumFarScan = 48;

void refineryDockUnload(const World::Ent& b, int& dx, int& dy) {
    dx = (int)b.x + 3;
    dy = (int)b.y + 1;
}
void refineryQueueCell(const World::Ent& b, int& qx, int& qy) {
    qx = (int)b.x + 4;
    qy = (int)b.y + 1;
}
void refineryDockCenter(const World::Ent& b, float& cx, float& cy) {
    cx = b.x + 3.5f;
    cy = b.y + 1.5f;
}
bool onOreCell(float ex, float ey, int ox, int oy) {
    // Dig only when standing on the ore tile (adjacent Dig looks premature).
    return (int)ex == ox && (int)ey == oy;
}
bool besideCell(float ex, float ey, int cx, int cy) {
    return std::max(std::abs((int)ex - cx), std::abs((int)ey - cy)) <= 1;
}
} // namespace

void World::updateHarvester(Ent& e, EID id) {
    const int CAP = harvesterCapacity(e.utype);
    auto beginReturnToRefinery = [&]() {
        EID ref = INVALID_EID;
        float bd = 1e9f;
        for (size_t i = 0; i < ents.size(); i++) {
            const Ent& b = ents[i];
            if (!b.alive || b.player != e.player) continue;
            float dx, dy;
            if (b.isBuilding && b.btype == BldType::OreRefinery) {
                refineryDockCenter(b, dx, dy);
            } else if (!b.isBuilding && b.utype == UnitType::SlaveMiner && b.deployed) {
                dx = b.x; dy = b.y;
            } else continue;
            float d = distf(e.x, e.y, dx, dy);
            if (d < bd) { bd = d; ref = (int)i; }
        }
        if (ref == INVALID_EID) { e.state = UState::Idle; return; }
        e.dockRefinery = ref;
        const Ent& b = ents[ref];
        int pathTx, pathTy;
        if (b.isBuilding) {
            // 优先队列格（地基外）；否则 DockUnload（寻路会落到邻格）
            refineryQueueCell(b, pathTx, pathTy);
            if (!map.passable(pathTx, pathTy) || bldBlocked(pathTx, pathTy))
                refineryDockUnload(b, pathTx, pathTy);
        } else {
            pathTx = (int)b.x; pathTy = (int)b.y;
        }
        // 超时空采矿车：满载瞬移到东侧垫附近（RA2：仅满载回厂瞬移）
        if (e.utype == UnitType::ChronoMiner && b.isBuilding) {
            Effect ef1; ef1.kind = 9; ef1.x = e.x; ef1.y = e.y; ef1.maxAge = 20; effects.push_back(ef1);
            int qx, qy; refineryQueueCell(b, qx, qy);
            bool placed = false;
            for (int r = 0; r < 5 && !placed; r++) {
                for (int dy = -r; dy <= r && !placed; dy++)
                    for (int dx = -r; dx <= r && !placed; dx++) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r && r > 0) continue;
                        int sx = qx + dx, sy = qy + dy;
                        if (map.passable(sx, sy) && !bldBlocked(sx, sy) && unitAtCell(sx, sy) == INVALID_EID) {
                            e.x = sx + 0.5f; e.y = sy + 0.5f; placed = true;
                        }
                    }
            }
            Effect ef2; ef2.kind = 9; ef2.x = e.x; ef2.y = e.y; ef2.maxAge = 20; effects.push_back(ef2);
            g_sfx.playAt(Sfx::Tesla, e.x, e.y);
            e.path.clear();
            e.state = UState::HarvestUnload;
            e.digTimer = 0;
            return;
        }
        std::vector<Vec2i> path;
        if (map.findPath((int)e.x, (int)e.y, pathTx, pathTy, path)) {
            e.path = std::move(path); e.pathIdx = 0;
            e.state = UState::HarvestReturn;
        } else {
            e.path.clear(); e.pathIdx = 0;
            e.state = UState::HarvestReturn; // 下帧重寻，勿满载 Idle
        }
    };

    switch (e.state) {
        case UState::HarvestGo: {
            moveAlongPath(e, id);
            if (e.oreCell.x < 0 || !map.inBounds(e.oreCell.x, e.oreCell.y) || map.at(e.oreCell.x, e.oreCell.y).ore <= 0) {
                Vec2i ore;
                if (map.findNearestOre((int)e.x, (int)e.y, kTiberiumFarScan, ore)) {
                    e.oreCell = ore;
                    std::vector<Vec2i> path;
                    if (map.findPath((int)e.x, (int)e.y, ore.x, ore.y, path)) {
                        e.path = std::move(path); e.pathIdx = 0;
                    } else if (e.oreLoad > 0) {
                        beginReturnToRefinery();
                        break;
                    }
                } else if (e.oreLoad > 0) {
                    beginReturnToRefinery();
                    break;
                } else {
                    e.state = UState::Idle;
                    break;
                }
            }
            // Dig only on the target ore cell
            if (onOreCell(e.x, e.y, e.oreCell.x, e.oreCell.y)) {
                e.path.clear();
                e.pathIdx = 0;
                e.state = UState::HarvestDig;
                e.digTimer = 0;
                e.dir = dirFromVec(e.oreCell.x + 0.5f - e.x, e.oreCell.y + 0.5f - e.y);
                e.turretDir = e.dir;
                g_sfx.playAt(Sfx::Dig, e.oreCell.x + 0.5f, e.oreCell.y + 0.5f);
            } else if (e.path.empty() || e.pathIdx >= (int)e.path.size()) {
                std::vector<Vec2i> path;
                if (map.findPath((int)e.x, (int)e.y, e.oreCell.x, e.oreCell.y, path)) {
                    if (path.empty()) {
                        // Empty path = already at rewritten goal (≤3 from ore). Dig only on ore.
                        if (onOreCell(e.x, e.y, e.oreCell.x, e.oreCell.y)) {
                            e.state = UState::HarvestDig;
                            e.digTimer = 0;
                            e.dir = dirFromVec(e.oreCell.x + 0.5f - e.x, e.oreCell.y + 0.5f - e.y);
                            e.turretDir = e.dir;
                            g_sfx.playAt(Sfx::Dig, e.oreCell.x + 0.5f, e.oreCell.y + 0.5f);
                        } else {
                            bool stepped = false;
                            for (int dy = -1; dy <= 1 && !stepped; dy++)
                                for (int dx = -1; dx <= 1 && !stepped; dx++) {
                                    int ax = e.oreCell.x + dx, ay = e.oreCell.y + dy;
                                    if (!map.passable(ax, ay) || bldBlocked(ax, ay)) continue;
                                    std::vector<Vec2i> ap;
                                    if (map.findPath((int)e.x, (int)e.y, ax, ay, ap) && !ap.empty()) {
                                        e.path = std::move(ap); e.pathIdx = 0;
                                        stepped = true;
                                    }
                                }
                            if (!stepped) {
                                if (e.oreLoad > 0) beginReturnToRefinery();
                                else e.state = UState::Idle;
                            }
                        }
                    } else {
                        e.path = std::move(path); e.pathIdx = 0;
                    }
                } else {
                    // 寻路失败：换近矿 / 有货回厂 / 空车 Idle，避免 HarvestGo 永久空转
                    Vec2i alt;
                    bool rerouted = false;
                    if (map.findNearestOre((int)e.x, (int)e.y, kTiberiumNearScan, alt)
                        && (alt.x != e.oreCell.x || alt.y != e.oreCell.y)) {
                        e.oreCell = alt;
                        if (onOreCell(e.x, e.y, alt.x, alt.y)) {
                            e.state = UState::HarvestDig;
                            e.digTimer = 0;
                            e.dir = dirFromVec(alt.x + 0.5f - e.x, alt.y + 0.5f - e.y);
                            e.turretDir = e.dir;
                            g_sfx.playAt(Sfx::Dig, alt.x + 0.5f, alt.y + 0.5f);
                            rerouted = true;
                        } else if (map.findPath((int)e.x, (int)e.y, alt.x, alt.y, path)) {
                            e.path = std::move(path); e.pathIdx = 0;
                            rerouted = true;
                        }
                    }
                    if (!rerouted) {
                        if (e.oreLoad > 0) beginReturnToRefinery();
                        else if (++e.blockTick > 45) {
                            e.blockTick = 0;
                            e.state = UState::Idle; // 交给自动采矿下轮 FarScan
                        }
                    }
                }
            }
            break;
        }
        case UState::HarvestDig: {
            if (tick % 7 == (uint64_t)(id % 7)) {
                Effect dust; dust.kind = 11;
                dust.x = e.oreCell.x + 0.5f + (rng.unit() - 0.5f) * 0.6f;
                dust.y = e.oreCell.y + 0.5f + (rng.unit() - 0.5f) * 0.6f;
                dust.maxAge = 14; effects.push_back(dust);
            }
            // Dig 音仅在进入 HarvestDig 时播一次（见上），此处不再每轮 digTimer 重触发
            if (++e.digTimer >= 20) {
                e.digTimer = 0;
                bool gems = map.at(e.oreCell.x, e.oreCell.y).terrain == Terrain::Gems;
                int got = map.harvestAt(e.oreCell.x, e.oreCell.y, 1);
                e.oreLoad += got;
                if (gems) e.gemLoad += got;
                if (e.oreLoad >= CAP) {
                    beginReturnToRefinery();
                } else if (got == 0) {
                    // NearScan：同矿脉下一格；失败且有货则回厂，空车则 FarScan
                    Vec2i next;
                    if (map.findNearestOre(e.oreCell.x, e.oreCell.y, kTiberiumNearScan, next)) {
                        e.oreCell = next;
                        if (onOreCell(e.x, e.y, next.x, next.y)) {
                            e.state = UState::HarvestDig;
                            e.digTimer = 0;
                            e.dir = dirFromVec(next.x + 0.5f - e.x, next.y + 0.5f - e.y);
                            e.turretDir = e.dir;
                            g_sfx.playAt(Sfx::Dig, next.x + 0.5f, next.y + 0.5f);
                        } else {
                            e.state = UState::HarvestGo;
                            e.path.clear(); e.pathIdx = 0;
                        }
                    } else if (e.oreLoad > 0) {
                        beginReturnToRefinery();
                    } else if (map.findNearestOre((int)e.x, (int)e.y, kTiberiumFarScan, next)) {
                        e.oreCell = next;
                        e.state = UState::HarvestGo;
                        e.path.clear(); e.pathIdx = 0;
                    } else {
                        e.state = UState::Idle;
                    }
                }
            }
            break;
        }
        case UState::HarvestReturn: {
            if (!valid(e.dockRefinery)) {
                beginReturnToRefinery(); // Idle 有货转入、或精炼厂被毁：重寻停靠
                break;
            }
            moveAlongPath(e, id);
            const Ent& b = ents[e.dockRefinery];
            float dx, dy;
            if (b.isBuilding) refineryDockCenter(b, dx, dy);
            else { dx = b.x; dy = b.y; }
            int dockX, dockY;
            if (b.isBuilding) refineryDockUnload(b, dockX, dockY);
            else { dockX = (int)b.x; dockY = (int)b.y; }
            bool atDock = distf(e.x, e.y, dx, dy) < 2.0f
                       || besideCell(e.x, e.y, dockX, dockY);
            if (atDock) {
                e.state = UState::HarvestUnload;
                e.digTimer = 0;
            } else if (e.path.empty() || e.pathIdx >= (int)e.path.size()) {
                int pathTx, pathTy;
                if (b.isBuilding) {
                    refineryQueueCell(b, pathTx, pathTy);
                    if (!map.passable(pathTx, pathTy) || bldBlocked(pathTx, pathTy))
                        refineryDockUnload(b, pathTx, pathTy);
                } else {
                    pathTx = (int)b.x; pathTy = (int)b.y;
                }
                std::vector<Vec2i> path;
                if (map.findPath((int)e.x, (int)e.y, pathTx, pathTy, path) && !path.empty()) {
                    e.path = std::move(path); e.pathIdx = 0;
                } else if (++e.blockTick > 90) {
                    // 长期到不了：清 dock 再寻；仍无则 Idle（有货时 autoHarvest 会再拉回）
                    e.blockTick = 0;
                    e.dockRefinery = INVALID_EID;
                    beginReturnToRefinery();
                }
            }
            break;
        }
        case UState::HarvestUnload: {
            if (valid(e.dockRefinery) && tick % 6 == (uint64_t)(id % 6)) {
                const Ent& rb = ents[e.dockRefinery];
                Effect pour; pour.kind = 11;
                if (rb.isBuilding) {
                    float cx, cy; refineryDockCenter(rb, cx, cy);
                    pour.x = cx + (rng.unit() - 0.5f) * 0.5f;
                    pour.y = cy + (rng.unit() - 0.5f) * 0.3f;
                } else {
                    pour.x = rb.x + (rng.unit() - 0.5f) * 0.5f;
                    pour.y = rb.y + (rng.unit() - 0.5f) * 0.3f;
                }
                pour.maxAge = 12; effects.push_back(pour);
            }
            int chunk = std::max(1, CAP / 20);
            if (e.oreLoad > 0) {
                int take = std::min(chunk, e.oreLoad);
                for (int i = 0; i < take; i++) {
                    bool gems = e.gemLoad > 0;
                    if (gems) e.gemLoad--;
                    int income = oreIncomeWithPurifier(oreUnitValue(gems), hasBld(e.player, BldType::OrePurifier));
                    players[e.player].money = std::min(g_gameRules.maxMoney, players[e.player].money + income);
                }
                e.oreLoad -= take;
            }
            if (e.oreLoad <= 0) {
                if (e.player == 0) g_sfx.play(Sfx::Cash, 0.55f);
                e.dockRefinery = INVALID_EID;
                Vec2i ore = e.oreCell;
                bool haveOre = ore.x >= 0 && map.inBounds(ore.x, ore.y) && map.at(ore.x, ore.y).ore > 0;
                if (!haveOre && ore.x >= 0)
                    haveOre = map.findNearestOre(ore.x, ore.y, kTiberiumNearScan, ore);
                if (!haveOre)
                    haveOre = map.findNearestOre((int)e.x, (int)e.y, kTiberiumFarScan, ore);
                if (haveOre && e.autoHarvest) {
                    e.oreCell = ore;
                    std::vector<Vec2i> path;
                    if (map.findPath((int)e.x, (int)e.y, ore.x, ore.y, path)) {
                        e.path = std::move(path); e.pathIdx = 0;
                    } else {
                        e.path.clear(); e.pathIdx = 0;
                    }
                    e.state = UState::HarvestGo;
                } else {
                    e.state = UState::Idle;
                }
            }
            break;
        }
        default: break;
    }
}

