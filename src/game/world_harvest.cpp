#include "game/world.h"
#include "game/world_priv.h"
#include "game/lang.h"
#include "game/script.h"
#include "game/presentation_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {
// Project dock offsets; the actual footprint comes from loaded rules.
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
bool unloadApproach(const World& world,float x,float y,int height) {
    for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
        const int tx=(int)x+dx,ty=(int)y+dy;
        const int layer=world.map.preferredLayer(tx,ty,0);
        if(world.passableFor(tx,ty,0,layer) && !world.bldBlocked(tx,ty,layer) && distf(tx+0.5f,ty+0.5f,x,y)<1.9f
            && (!world.map.hasBridgeDecks() || abs(world.map.surfaceHeight(tx,ty,layer)-height)<=MAP_MAX_CLIMB))return true;
    }
    return false;
}
bool harvestReceiver(const World::Ent& unit,int player) {
    return unit.alive && unit.player==player && ((unit.isBuilding && unit.btype==BldType::OreRefinery)
        || (!unit.isBuilding && unit.utype==UnitType::SlaveMiner && unit.deployed));
}
bool canDigOre(const World::Ent& e, Vec2i ore) {
    return (int)e.x == ore.x && (int)e.y == ore.y;
}
bool findOrePath(const World& world, const World::Ent& e, Vec2i ore, std::vector<Vec2i>& path) {
    if (!world.map.findPath((int)e.x,(int)e.y,ore.x,ore.y,path,20000,0,nullptr,e.navLayer,-1,
            unitDef(e).fitsUnderBridge(),world.nativeGateAccess(e))) return false;
    // Movement can snap blocked goals to a nearby cell, but digging needs the ore cell.
    if (path.empty() ? canDigOre(e, ore) : path.back() == ore) return true;
    path.clear();
    return false;
}
bool besideCell(float ex, float ey, int cx, int cy) {
    return std::max(std::abs((int)ex - cx), std::abs((int)ey - cy)) <= 1;
}

void enterDig(World& world, World::Ent& e, int ox, int oy) {
    e.clearPath();
    e.pathIdx = 0;
    e.state = UState::HarvestDig;
    e.digTimer = 0;
    e.dir = dirFromVec(ox + 0.5f - e.x, oy + 0.5f - e.y);
    e.turretDir = e.dir;
    world.emitSoundAt(Sfx::Dig, ox + 0.5f, oy + 0.5f);
}
} // namespace

bool World::findNearestReachableOre(int sx, int sy, int maxR, Vec2i& out, EID mover) const {
    if(!map.inBounds(sx,sy) || maxR<0) return false;
    // A single flood search finds a reachable harvesting position. Repeated A*
    // for every ore cell is pathological when deposits are behind a blocked base.
    std::vector<uint8_t> visited(map.cells.size(),0);
    std::vector<Vec2i> frontier{{sx,sy}};
    visited[(size_t)sy*map.w+sx]=1;
    static constexpr int DX[8]={1,-1,0,0,1,1,-1,-1};
    static constexpr int DY[8]={0,0,1,-1,1,-1,1,-1};
    for(size_t head=0;head<frontier.size();++head) {
        const auto cell=frontier[head];
        // Return a reachable ore tile, never an arbitrary fallback position
        // beside an inaccessible/covered deposit.
        if(std::max(std::abs(sx-cell.x),std::abs(sy-cell.y))<=maxR
            && map.at(cell.x,cell.y).ore>0 && map.passable(cell.x,cell.y)
            && !bldBlocked(cell.x,cell.y)
            && (mover==INVALID_EID || !cellHardBlockedForMove(cell.x,cell.y,mover))) { out=cell; return true; }
        for(int d=0;d<8;++d) {
            int x=cell.x+DX[d],y=cell.y+DY[d];
            if(!passableStep(cell.x,cell.y,x,y,0) || bldBlocked(x,y)) continue;
            if(d>=4 && (!passableStep(cell.x,cell.y,cell.x+DX[d],cell.y,0)
                || !passableStep(cell.x,cell.y,cell.x,cell.y+DY[d],0)
                || bldBlocked(cell.x+DX[d],cell.y) || bldBlocked(cell.x,cell.y+DY[d]))) continue;
            size_t index=(size_t)y*map.w+x;
            if(visited[index]) continue;
            visited[index]=1; frontier.push_back({x,y});
        }
    }
    return false;
}

void World::beginHarvesterReturn(Ent& e, EID mover, EID preferredRef, bool avoidOccupied) {
    if(beginNativeHarvesterReturn(e,mover,preferredRef))return;
    EID ref=INVALID_EID;float best=1e9f;std::vector<Vec2i> route;
    std::vector<uint8_t> congestion;
    if(avoidOccupied) {
        congestion.assign(movementSlotCount(),0);
        for(const auto& unit:ents) {
            if(unit.occCell<0 || unit.occCell>=(int)congestion.size())continue;
            const int x=unit.occCell%map.w,y=unit.occCell/map.w;
            if(cellHardBlockedForMove(x,y,mover,unit.navLayer))congestion[movementSlot(x,y,unit.navLayer)]=255;
        }
    }
    auto consider=[&](EID id) {
        const auto& receiver=ents[id];
        if(!harvestReceiver(receiver,e.player)) return;
        float x,y;
        if(receiver.isBuilding && receiver.btype==BldType::OreRefinery) refineryDockCenter(receiver,x,y);
        else if(!receiver.isBuilding && receiver.utype==UnitType::SlaveMiner && receiver.deployed) { x=receiver.x;y=receiver.y; }
        else return;
        const float distance=distf(e.x,e.y,x,y);if(distance>=best) return;
        const int height=actorSurfaceHeight(receiver);
        if(!unloadApproach(*this,x,y,height) || repathsThisTick>=8) return;
        std::vector<Vec2i> path;++repathsThisTick;++repathRequests;
        if(!map.findPathToRange((int)e.x,(int)e.y,x,y,1.9f,path,20000,0,avoidOccupied?&congestion:nullptr,e.navLayer,-1,height,unitDef(e).fitsUnderBridge(),nativeGateAccess(e))) { ++repathFailures;return; }
        ref=id;best=distance;route=std::move(path);
    };
    if(valid(preferredRef)) consider(preferredRef);
    if(ref==INVALID_EID) for(size_t id=0;id<ents.size();++id) consider((EID)id);
    // A temporary traffic snapshot may have no route. Keep the existing order
    // so normal yielding can continue and retry after occupants move.
    if(avoidOccupied && ref==INVALID_EID)return;
    e.dockRefinery=ref;
    if(ref==INVALID_EID) { e.clearPath();e.state=UState::Idle;return; }
    const auto& receiver=ents[ref];
    if(e.utype==UnitType::ChronoMiner && receiver.isBuilding) {
        float cx,cy;refineryDockCenter(receiver,cx,cy);int qx,qy;refineryQueueCell(receiver,qx,qy);
        bool placed=false;float x=e.x,y=e.y;
        for(int radius=0;radius<3 && !placed;++radius)
            for(int dy=-radius;dy<=radius && !placed;++dy) for(int dx=-radius;dx<=radius && !placed;++dx) {
                if(std::max(abs(dx),abs(dy))!=radius) continue;
                int tx=qx+dx,ty=qy+dy;
                const int layer=map.preferredLayer(tx,ty,0);
                if(passableFor(tx,ty,0,layer) && !bldBlocked(tx,ty,layer) && unitAtCell(tx,ty,layer)==INVALID_EID
                    && distf(tx+0.5f,ty+0.5f,cx,cy)<1.9f
                    && (!map.hasBridgeDecks() || abs(map.surfaceHeight(tx,ty,layer)-actorSurfaceHeight(receiver))<=MAP_MAX_CLIMB))
                    { x=tx+0.5f;y=ty+0.5f;placed=true; }
            }
        if(placed) {
            Effect departure;departure.kind=9;departure.aux=(int)ChronoVisual::Departure;departure.x=e.x;departure.y=e.y;departure.maxAge=20;effects.push_back(departure);
            e.x=e.px=x;e.y=e.py=y;initializeMovementSurface(e);unitOccSync(mover);
            Effect arrival;arrival.kind=9;arrival.x=x;arrival.y=y;arrival.maxAge=20;effects.push_back(arrival);
            emitSoundAt(Sfx::Tesla,x,y);e.clearPath();e.state=UState::HarvestUnload;e.digTimer=0;return;
        }
    }
    e.path=std::move(route);e.pathIdx=0;e.blockTick=0;e.state=UState::HarvestReturn;
}

void World::updateHarvester(Ent& e, EID id) {
    const int CAP = harvesterCapacity(e.utype);
    if(usesNativeGround(e) && (e.state==UState::HarvestReturn || e.state==UState::HarvestUnload)) {
        // A receiver lost between updates must re-enter the calibrated dock
        // controller, never fall through to historical range-based cashout.
        if(e.nativeDock.phase==NativeDockPhase::None)beginNativeHarvesterReturn(e,id,e.dockRefinery);
        return;
    }
    auto beginReturnToRefinery = [&]() { beginHarvesterReturn(e,id); };
    if(nativeTravelEnabled() && e.utype==UnitType::ChronoMiner && e.oreLoad>=CAP) {
        beginReturnToRefinery();
        return;
    }

    switch (e.state) {
        case UState::HarvestGo: {
            moveAlongPath(e, id);
            if (e.oreCell.x < 0 || !map.inBounds(e.oreCell.x, e.oreCell.y) || map.at(e.oreCell.x, e.oreCell.y).ore <= 0) {
                Vec2i ore;
                if (findNearestReachableOre((int)e.x, (int)e.y, kTiberiumFarScan, ore)) {
                    e.oreCell = ore;
                    std::vector<Vec2i> path;
                    if (findOrePath(*this, e, ore, path)) {
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
            if (canDigOre(e, e.oreCell)) {
                enterDig(*this, e, e.oreCell.x, e.oreCell.y);
            } else if (e.path.empty() || e.pathIdx >= (int)e.path.size()) {
                std::vector<Vec2i> path;
                if (findOrePath(*this, e, e.oreCell, path)) {
                    e.path = std::move(path); e.pathIdx = 0;
                } else {
                    // 寻路失败：换可达近矿 / 有货回厂 / 空车 Idle
                    Vec2i alt;
                    bool rerouted = false;
                    if (findNearestReachableOre((int)e.x, (int)e.y, kTiberiumNearScan, alt)
                        && (alt.x != e.oreCell.x || alt.y != e.oreCell.y)) {
                        e.oreCell = alt;
                        if (canDigOre(e, alt)) {
                            enterDig(*this, e, alt.x, alt.y);
                            rerouted = true;
                        } else if (findOrePath(*this, e, alt, path)) {
                            e.path = std::move(path); e.pathIdx = 0;
                            rerouted = true;
                        }
                    }
                    if (!rerouted) {
                        if (e.oreLoad > 0) beginReturnToRefinery();
                        else if (++e.blockTick > 45) {
                            e.blockTick = 0;
                            e.oreCell = {-1, -1}; // 放弃不可达目标，Idle FarScan 另选
                            e.state = UState::Idle;
                        }
                    }
                }
            } else if (e.blockTick==LOGIC_FPS && repathsThisTick<8 && e.path[e.pathIdx]==e.oreCell) {
                const int layer=movementLayerAt(e,e.oreCell.x,e.oreCell.y);
                const EID working=unitAtCell(e.oreCell.x,e.oreCell.y,layer);
                if (valid(working) && isAllied(e.player,ents[working].player) && ents[working].state==UState::HarvestDig) {
                    Vec2i spare;
                    std::vector<Vec2i> route;
                    if (findNearestReachableOre((int)e.x,(int)e.y,kTiberiumNearScan,spare,id)
                        && repathUnit(id,spare.x,spare.y,route,working)
                        && (route.empty()?canDigOre(e,spare):route.back()==spare)) {
                        e.clearPath();
                        e.path=std::move(route);
                        e.oreCell=spare;
                        e.blockTick=0;
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
            // Local RA2 StageClass advances nine mining stages at LoadRate
            // original frames each. One original frame spans two logic ticks.
            const int pickupTicks=nativeProductionRules.version>=7?18*nativeProductionRules.harvesterLoadRate:20;
            if (++e.digTimer >= pickupTicks) {
                e.digTimer = 0;
                bool gems = map.at(e.oreCell.x, e.oreCell.y).terrain == Terrain::Gems;
                int got = map.harvestAt(e.oreCell.x, e.oreCell.y, 1);
                e.oreLoad += got;
                if (gems) e.gemLoad += got;
                if (e.oreLoad >= CAP) {
                    beginReturnToRefinery();
                } else if (got == 0) {
                    Vec2i next;
                    if (findNearestReachableOre(e.oreCell.x, e.oreCell.y, kTiberiumNearScan, next)) {
                        e.oreCell = next;
                        if (canDigOre(e, next)) {
                            enterDig(*this, e, next.x, next.y);
                        } else {
                            e.state = UState::HarvestGo;
                            e.clearPath(); e.pathIdx = 0;
                        }
                    } else if (e.oreLoad > 0) {
                        beginReturnToRefinery();
                    } else if (findNearestReachableOre((int)e.x, (int)e.y, kTiberiumFarScan, next)) {
                        e.oreCell = next;
                        e.state = UState::HarvestGo;
                        e.clearPath(); e.pathIdx = 0;
                    } else {
                        e.state = UState::Idle;
                    }
                }
            }
            break;
        }
        case UState::HarvestReturn: {
            bool usable=valid(e.dockRefinery);
            if(usable) {
                const auto& receiver=ents[e.dockRefinery];usable=harvestReceiver(receiver,e.player);
                if(receiver.isBuilding && receiver.btype==BldType::OreRefinery) {
                    float x,y;refineryDockCenter(receiver,x,y);usable &= unloadApproach(*this,x,y,actorSurfaceHeight(receiver));
                } else usable &= !receiver.isBuilding && receiver.utype==UnitType::SlaveMiner && receiver.deployed;
            }
            if (!usable) {
                beginReturnToRefinery();
                break;
            }
            if(e.blockTick>=45 && repathsThisTick<8)
                beginHarvesterReturn(e,id,INVALID_EID,true);
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
            atDock &= !map.hasBridgeDecks() || abs(actorSurfaceHeight(e)-actorSurfaceHeight(b))<=MAP_MAX_CLIMB;
            if (atDock) {
                e.state = UState::HarvestUnload;
                e.digTimer = 0;
            } else if (e.path.empty() || e.pathIdx >= (int)e.path.size()) {
                beginReturnToRefinery();
            }
            break;
        }
        case UState::HarvestUnload: {
            if(!valid(e.dockRefinery) || !harvestReceiver(ents[e.dockRefinery],e.player)
                || (map.hasBridgeDecks() && abs(actorSurfaceHeight(e)-actorSurfaceHeight(ents[e.dockRefinery]))>MAP_MAX_CLIMB)) {
                beginReturnToRefinery();break;
            }
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
                    const int value=gems?nativeProductionRules.gemValue:nativeProductionRules.oreValue;
                    int income = oreIncomeWithPurifier(value, hasBld(e.player, BldType::OrePurifier));
                    harvestedIncome[e.player] += creditIncome(e.player,income);
                }
                e.oreLoad -= take;
            }
            if (e.oreLoad <= 0) {
                finishHarvesterUnload(e);
            }
            break;
        }
        default: break;
    }
}

void World::finishHarvesterUnload(Ent& e) {
    const bool timedGround = usesNativeGround(e);
    if (!timedGround && e.player == 0) emitSound(Sfx::Cash, 0.55f);
    e.dockRefinery = INVALID_EID;
    Vec2i ore = e.oreCell;
    std::vector<Vec2i> path;
    const auto planRoute = [&] { return findOrePath(*this, e, ore, path); };
    bool haveOre = ore.x >= 0 && map.inBounds(ore.x, ore.y) && map.at(ore.x, ore.y).ore > 0;
    if (haveOre && !canDigOre(e, ore)) haveOre = planRoute();
    if (!haveOre && ore.x >= 0 && !(nativeTravelEnabled() && e.utype==UnitType::ChronoMiner)) {
        haveOre = findNearestReachableOre(ore.x, ore.y, kTiberiumNearScan, ore);
        // The old field's neighbors can be cut off from the refinery too.
        if (haveOre && timedGround)
            haveOre = ore != e.oreCell && (canDigOre(e, ore) || planRoute());
    }
    if (!haveOre)
        haveOre = findNearestReachableOre((int)e.x, (int)e.y, kTiberiumFarScan, ore);
    if (haveOre && e.autoHarvest) {
        const bool routed = !path.empty() || planRoute();
        if (routed || !timedGround || canDigOre(e, ore)) {
            e.oreCell = ore;
            if (routed) {
                e.path = std::move(path);
                e.pathIdx = 0;
            } else {
                e.clearPath();
            }
            e.state = UState::HarvestGo;
            return;
        }
    }
    e.state = UState::Idle;
}
