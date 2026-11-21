#include "game/world.h"
#include "game/world_priv.h"
#include <cmath>

namespace {
constexpr float DockApproachTolerance=.02f;
constexpr uint16_t DockEast=0x4000,DockWest=0xc000;

bool canUseGroundRefinery(const World::Ent& refinery,int player) {
    return refinery.isBuilding && refinery.btype==BldType::OreRefinery && refinery.player==player
        && !refinery.selling && refinery.constructAnim==0 && !refinery.immobilized();
}

bool needsDockExit(const World& world,const World::Ent& miner) {
    const auto& dock=miner.nativeDock;
    const bool chronoAlign=dock.phase==NativeDockPhase::Align && world.nativeTravelEnabled() && miner.utype==UnitType::ChronoMiner;
    if(dock.phase!=NativeDockPhase::ForwardEntry && !chronoAlign)return dock.phase>=NativeDockPhase::Enter;
    if(!world.valid(dock.receiver))return false;
    const auto& refinery=world.ents[dock.receiver.id];
    return (int)miner.x==(int)(refinery.x+3.5f) && (!chronoAlign || miner.y==refinery.y+1.5f);
}
}

bool World::usesNativeGround(const Ent& e) const {
    return nativeGroundEnabled() && !e.isBuilding && e.nativeIdentity.definition
        && e.nativeIdentity.definition->movement.available()
        && !unitDef(e).isNaval() && !unitDef(e).isAir() && !unitDef(e).isInfantry();
}
void World::initializeNativeGround(Ent& e) {
    if(!usesNativeGround(e) || e.nativeGround.initialized)return;
    auto& s=e.nativeGround;s.initialized=true;
    s.hull.start=s.hull.target=NativeGroundMath::fromDir(e.dir);
    s.turret.start=s.turret.target=NativeGroundMath::fromDir(e.turretDir);
    s.hull.since=s.turret.since=tick/2;
    e.previousHullHeading=s.hull.target;e.previousTurretHeading=s.turret.target;
}
uint16_t World::nativeHullHeading(const Ent& e,uint64_t atTick) const {
    return e.nativeGround.initialized?NativeGroundMath::current(e.nativeGround.hull,atTick/2):NativeGroundMath::fromDir(e.dir);
}
uint16_t World::nativeTurretHeading(const Ent& e,uint64_t atTick) const {
    return e.nativeGround.initialized?NativeGroundMath::current(e.nativeGround.turret,atTick/2):NativeGroundMath::fromDir(e.turretDir);
}
void World::updateNativeGroundFacing(Ent& e) {
    if(!usesNativeGround(e))return;initializeNativeGround(e);
    e.dir=NativeGroundMath::dir(nativeHullHeading(e,tick));
    e.turretDir=NativeGroundMath::dir(nativeTurretHeading(e,tick));
}
void World::desireNativeHull(Ent& e,uint16_t heading) {
    initializeNativeGround(e);
    NativeGroundMath::desire(e.nativeGround.hull,heading,e.nativeIdentity.definition->production.rotationRate,tick/2);
    updateNativeGroundFacing(e);
}
void World::desireNativeTurret(Ent& e,uint16_t heading) {
    initializeNativeGround(e);
    NativeGroundMath::desire(e.nativeGround.turret,heading,e.nativeIdentity.definition->production.rotationRate,tick/2);
    updateNativeGroundFacing(e);
}
bool World::nativeGroundAimAligned(const Ent& e,uint16_t heading) const {
    // Keep the established weapon admission cone; use the actual timed angle.
    const auto actual=unitHasTurret(e.utype)?nativeTurretHeading(e,tick):nativeHullHeading(e,tick);
    return std::abs(NativeGroundMath::delta(actual,heading))<=8192;
}
bool World::aimNativeGround(Ent& e,float x,float y) {
    const auto wanted=NativeGroundMath::vector(x-e.x,y-e.y);
    if(unitHasTurret(e.utype))desireNativeTurret(e,wanted);
    else {desireNativeHull(e,wanted);desireNativeTurret(e,wanted);}
    return nativeGroundAimAligned(e,wanted);
}

bool World::nativeDockAvailable(EID receiver,EID miner) const {
    const auto ref=entityRef(receiver);
    for(EID i=0;i<(EID)ents.size();++i)if(i!=miner && ents[i].alive) {
        const auto& s=ents[i].nativeDock;
        if(s.receiver==ref && ents[i].claimsNativeDock())return false;
    }
    return true;
}

bool World::nativeDockTerrainPassable(const Ent& miner, EID receiver) const {
    const auto& refinery = ents[receiver];
    const int x = (int)refinery.x + 4, y = (int)refinery.y + 1, layer = refinery.navLayer;
    return movementPassable(miner, x, y, layer) && movementPassable(miner, x - 1, y, layer, false)
        && bldAt(x - 1, y) == receiver && !bldBlocked(x, y, layer)
        && std::abs(map.surfaceHeight(x, y, layer) - map.surfaceHeight(x - 1, y, layer)) <= MAP_MAX_CLIMB;
}

bool World::continuesNativeDock(const Ent& miner, EID receiver) const {
    if (miner.hasPendingDockReturn() && miner.nativeDock.phase != NativeDockPhase::Approach)
        return valid(receiver) && miner.pendingDockRefinery == entityRef(receiver);
    if (!usesNativeGround(miner)
            || (!miner.claimsNativeDock() && miner.chronoReturn.phase != ChronoReturnPhase::Waiting)
            || (miner.state != UState::HarvestReturn && miner.state != UState::HarvestUnload)
            || !valid(receiver) || miner.nativeDock.receiver != entityRef(receiver)) return false;
    if (miner.chronoReturn.phase != ChronoReturnPhase::None) return chronoReceiverUsable(miner, receiver);
    return canUseGroundRefinery(ents[receiver], miner.player) && nativeDockTerrainPassable(miner, receiver);
}

bool World::ownsNativeDockRefinery(const Ent& miner, EntityRef receiver) const {
    if (miner.player < 0 || !valid(receiver)) return false;
    const auto& building = ents[receiver.id];
    return building.isBuilding && building.btype == BldType::OreRefinery && building.player == miner.player;
}

bool World::beginNativeHarvesterReturn(Ent& e,EID id,EID preferred,bool preferOpenApproach) {
    if(!nativeGroundEnabled() || !usesNativeGround(e))return false;
    // A fresh order inside the bay must first complete the physical exit.
    if(needsDockExit(*this,e)) {
        const auto requested = entityRef(preferred);
        e.pendingDockRefinery = ownsNativeDockRefinery(e, requested) ? requested : EntityRef{};
        e.cancelNativeDockTiming();
        e.nativeDock.phase=NativeDockPhase::Exit;e.state=UState::HarvestReturn;e.clearPath();return true;
    }
    const bool timedChrono = nativeTravelEnabled() && e.utype==UnitType::ChronoMiner;
    // A new explicit command seeds the destination before entering HarvestReturn.
    const bool pending = e.hasPendingDockReturn()
        || (preferred != INVALID_EID && e.pendingDockRefinery.id == preferred);
    const auto requested = pending && ownsNativeDockRefinery(e, e.pendingDockRefinery)
        ? e.pendingDockRefinery : EntityRef{};
    if (requested.id != INVALID_EID) {
        if (!timedChrono && repathsThisTick >= 8) {
            const bool sameApproach = e.state == UState::HarvestReturn
                && e.nativeDock.phase == NativeDockPhase::Approach && e.nativeDock.receiver == requested;
            if (!sameApproach) {
                // The normal return update retries after the route budget resets.
                e.nativeDock = {};
                e.dockRefinery = INVALID_EID;
                e.clearPath();
                e.nativeMovement = {};
                e.blockTick = 0;
                e.state = UState::HarvestReturn;
                initializeNativeGround(e);
            }
            return true;
        }
        preferred = requested.id;
    }
    e.pendingDockRefinery = {};
    if(timedChrono) {
        beginChronoReturn(e,id,preferred);
        return true;
    }
    EID best=INVALID_EID;float bestScore=1e9f;std::vector<Vec2i> route;
    auto consider=[&](EID candidate,bool requireOpen=false){
        if(!valid(candidate))return;
        const auto& b=ents[candidate];
        if(!canUseGroundRefinery(b,e.player) || !nativeDockTerrainPassable(e,candidate))return;
        const int qx=(int)b.x+4,qy=(int)b.y+1;
        if(requireOpen && cellHardBlockedForMove(qx,qy,id,b.navLayer))return;
        // Prefer a free refinery over waiting behind another miner, without
        // claiming it until this vehicle reaches the approach cell.
        const float score=distf(e.x,e.y,qx+.5f,qy+.5f)+(nativeDockAvailable(candidate,id)?0:12);
        if(score>=bestScore || repathsThisTick>=8)return;
        std::vector<Vec2i> path;++repathsThisTick;++repathRequests;
        if(!map.findPath((int)e.x,(int)e.y,qx,qy,path,20000,0,nullptr,e.navLayer,b.navLayer,unitDef(e).fitsUnderBridge(),nativeGateAccess(e))){++repathFailures;return;}
        best=candidate;bestScore=score;route=std::move(path);
    };
    if(valid(preferred))consider(preferred);
    if(preferOpenApproach) {
        // Keep the original route available while looking for a free entrance.
        bestScore=1e9f;
        for(EID i=0;i<(EID)ents.size();++i)consider(i,true);
        if(best==INVALID_EID && repathsThisTick>=8)return true;
    }
    if(best==INVALID_EID)for(EID i=0;i<(EID)ents.size();++i)consider(i);
    e.nativeDock={};e.dockRefinery=best;e.clearPath();e.blockTick=0;
    if(best==INVALID_EID){e.state=UState::Idle;return true;}
    const auto& b=ents[best];const int qx=(int)b.x+4,qy=(int)b.y+1;
    // A route starting in the approach cell still has a physical leg to its center.
    if(route.empty() && e.navLayer==b.navLayer && (int)e.x==qx && (int)e.y==qy
        && distf(e.x,e.y,qx+.5f,qy+.5f)>=DockApproachTolerance)route.push_back({qx,qy});
    e.nativeDock.phase=NativeDockPhase::Approach;e.nativeDock.receiver=entityRef(best);
    if (e.utype == UnitType::WarMiner && best == requested.id) e.pendingDockRefinery = requested;
    e.state=UState::HarvestReturn;e.path=std::move(route);
    // The return teleport lands at the approach, never grants a load remotely.
    if(e.utype==UnitType::ChronoMiner && !cellHardBlockedForMove(qx,qy,id,b.navLayer)) {
        teleportHarvester(e,id,qx,qy,b.navLayer);
    }
    return true;
}

bool World::updateNativeDock(Ent& e,EID id) {
    if (e.pendingDockRefinery.id != INVALID_EID && e.state != UState::HarvestReturn) e.pendingDockRefinery = {};
    if(updateChronoReturn(e,id))return true;
    if(!nativeGroundEnabled() || e.nativeDock.phase==NativeDockPhase::None)return false;
    auto& s=e.nativeDock;
    if(!valid(s.receiver)) {
        e.cancelNativeDockTiming();
        s={};e.dockRefinery=INVALID_EID;
        if(e.state==UState::HarvestReturn || e.state==UState::HarvestUnload)beginNativeHarvesterReturn(e,id,INVALID_EID);
        return true;
    }
    const auto& b=ents[s.receiver.id];const float dockX=b.x+3.5f,queueX=b.x+4.5f,y=b.y+1.5f;
    const bool returning=e.state==UState::HarvestReturn || e.state==UState::HarvestUnload;
    // External relocation (for example a Chronosphere) breaks the old radio
    // reservation instead of driving a docking manoeuvre across the map.
    if(s.phase>=NativeDockPhase::Align && (std::abs(e.y-y)>.03f || e.x<dockX-.03f || e.x>queueX+.03f)) {
        e.cancelNativeDockTiming();
        s={};e.dockRefinery=INVALID_EID;if(returning)beginNativeHarvesterReturn(e,id,INVALID_EID);return true;
    }
    // An occupant of an unusable bay can still take the evacuation path,
    // including a miner loaded from an older save inside a cliffside bay.
    const bool evacuating = s.phase==NativeDockPhase::Exit && !e.waitingForNativeDeparture();
    const bool traversable = evacuating || nativeDockTerrainPassable(e, s.receiver.id);
    if(!returning || !canUseGroundRefinery(b,e.player) || !traversable) {
        e.cancelNativeDockTiming();
        if(needsDockExit(*this,e))s.phase=NativeDockPhase::Exit;
        else {
            s={};e.dockRefinery=INVALID_EID;
            if(returning){beginNativeHarvesterReturn(e,id,INVALID_EID);return true;}
            return false;
        }
    }
    if(s.phase==NativeDockPhase::Approach) {
        if(!nativeDockAvailable(s.receiver.id,id)) {
            // Do not park on the only exit while its current occupant leaves.
            if(!e.hasPendingDockReturn() && ++e.blockTick>=45){e.blockTick=0;beginNativeHarvesterReturn(e,id,INVALID_EID);}
            return true;
        }
        const auto minerIdentity=entityRef(id),receiver=s.receiver;
        const int approachLayer=b.navLayer;
        moveAlongPath(e,id);
        if(!valid(minerIdentity) || !valid(receiver))return true;
        auto& miner=ents[id];
        if((miner.state!=UState::HarvestReturn && miner.state!=UState::HarvestUnload) || miner.nativeDock.phase!=NativeDockPhase::Approach
            || miner.nativeDock.receiver!=receiver)return true;
        if(miner.navLayer==approachLayer && distf(miner.x,miner.y,queueX,y)<DockApproachTolerance) {
            miner.pendingDockRefinery = {};
            miner.clearPath();miner.nativeMovement={};miner.digTimer=0;
            miner.nativeDock.phase=nativeTravelEnabled() && miner.utype==UnitType::WarMiner
                ? NativeDockPhase::ForwardEntry : NativeDockPhase::Align;
        } else if(miner.pathIdx>=(int)miner.path.size()) {
            const bool blocked=cellHardBlockedForMove((int)queueX,(int)y,id,approachLayer);
            beginNativeHarvesterReturn(miner,id,receiver.id,blocked);
        }
        return true;
    }
    const bool forwardEntry=s.phase==NativeDockPhase::ForwardEntry;
    const bool beforeBay=forwardEntry && (int)e.x!=(int)dockX;
    const auto face=[&](uint16_t heading) {
        desireNativeHull(e,heading);
        if(!unitHasTurret(e.utype) || !valid(e.target))desireNativeTurret(e,heading);
    };
    face(beforeBay?DockWest:DockEast);
    const auto beginUnload=[&] {
        s.phase=NativeDockPhase::Unload;
        e.state=UState::HarvestUnload;
        e.digTimer=nativeTravelEnabled()?-NativeUnloadTiming::startupTicks:0;
    };
    if(s.phase==NativeDockPhase::Align) {
        if(nativeHullHeading(e,tick)==DockEast) {
            if(e.utype==UnitType::ChronoMiner && e.x==dockX && e.y==y)beginUnload();
            else s.phase=NativeDockPhase::Enter;
        }
        return true;
    }
    if(forwardEntry && e.x==dockX && e.y==y) {
        if(nativeHullHeading(e,tick)==DockEast)beginUnload();
        return true;
    }
    if(forwardEntry || s.phase==NativeDockPhase::Enter || e.exitingNativeDock()) {
        const bool exiting=e.exitingNativeDock();
        const bool driveExit=s.phase==NativeDockPhase::DriveExit;
        if((beforeBay && nativeHullHeading(e,tick)!=DockWest)
            || (exiting && nativeHullHeading(e,tick)!=DockEast))return true;
        if(e.waitingForNativeDeparture() && ++e.digTimer<0)return true;
        const float destination=exiting?queueX:dockX;
        const float dx=destination-e.x,dy=y-e.y,distance=std::hypot(dx,dy);
        const auto& profile=e.nativeIdentity.definition->movement;
        const float step=std::max(1,profile.maxSpeed)/(512.f)*float(movementSpeedMultiplier(e.player));
        const auto ore=e.oreCell;
        const float amount=driveExit?nativeMovementStep(e,(int)queueX,(int)y,e.navLayer,ore):std::min(step,distance);
        if(driveExit && amount<=0 && distance>0)return true;
        const float nx=distance>1e-6f?e.x+dx*amount/distance:destination,ny=distance>1e-6f?e.y+dy*amount/distance:y;
        const EID building=bldAt((int)nx,(int)ny);
        const bool passable=passableFor((int)nx,(int)ny,0,e.navLayer) && (building==INVALID_EID || building==s.receiver.id);
        if(!passable || cellHardBlockedForMove((int)nx,(int)ny,id,e.navLayer)) {
            if(driveExit)e.nativeMovement.credit=0;
            if(passable && exiting && ++e.blockTick>=10) {
                yieldIdleCorridor(id,unitAtCell((int)nx,(int)ny,e.navLayer));e.blockTick=0;
            }
            return true;
        }
        e.blockTick=0;
        e.x=nx;e.y=ny;unitOccSync(id);++e.walkAnim;
        if(beforeBay && (int)e.x==(int)dockX)face(DockEast);
        if(distance<=amount+1e-5f) {
            e.x=destination;e.y=y;unitOccSync(id);
            if(!exiting) {
                if(!forwardEntry || nativeHullHeading(e,tick)==DockEast)beginUnload();
            } else {
                s={};e.dockRefinery=INVALID_EID;
                if (returning) {
                    if (e.oreLoad > 0 || e.hasPendingDockReturn()) beginNativeHarvesterReturn(e,id,INVALID_EID);
                    else finishHarvesterUnload(e);
                }
                // The reserved leg ends at the apron center. A straight ore
                // route can spend this half-frame's remaining travel there.
                if(driveExit) {
                    const Vec2i next{(int)queueX+1,(int)y};
                    const bool continuesStraight=e.state==UState::HarvestGo && e.oreCell==ore
                        && !e.path.empty() && e.path.front()==next
                        && movementStep(e,(int)queueX,(int)y,e.navLayer,next.x,next.y)==e.navLayer;
                    if(!continuesStraight)e.nativeMovement={};
                    else if(e.nativeMovement.credit>1e-8)moveAlongPath(e,id);
                }
            }
        }
        return true;
    }
    if(s.phase==NativeDockPhase::Unload) {
        if(++e.digTimer<2*nativeProductionRules.harvesterDumpFrames)return true;
        e.digTimer=0;
        const int ordinary=e.oreLoad-e.gemLoad;
        if(!e.oreLoad) {
            s.phase=NativeDockPhase::Exit;
            const auto ore=e.oreCell;
            if(nativeTravelEnabled() && e.utype==UnitType::WarMiner && e.x==dockX && e.y==y && e.autoHarvest
                && map.inBounds(ore.x,ore.y) && map.at(ore.x,ore.y).ore>0) {
                e.digTimer=-NativeUnloadTiming::departureWaitTicks;
                const auto& profile=e.nativeIdentity.definition->movement;
                if(nativeMovementRules.available && profile.locomotor==NativeLocomotor::Drive && profile.maxSpeed>0) {
                    s.phase=NativeDockPhase::DriveExit;
                    e.nativeMovement={};
                }
            }
            return true;
        }
        const int quantity=ordinary>0?ordinary:e.gemLoad,value=ordinary>0?nativeProductionRules.oreValue:nativeProductionRules.gemValue;
        const auto& owner = players[e.player];
        int purifiers = 0;
        // The original power fraction is one when the house draws no power.
        if (owner.powerUsed == 0 || !owner.lowPower()) {
            for (const auto& building : ents) {
                if (building.alive && building.isBuilding && building.player == e.player
                    && building.btype == BldType::OrePurifier && !building.constructAnim && !building.selling) {
                    ++purifiers;
                }
            }
        }
        const Credits base=Credits(quantity)*value;
        const Credits bonus=(Credits)(float(quantity)*purifiers*nativeProductionRules.purifierBonus*value);
        const Credits credited=creditIncome(e.player,base+bonus);
        harvestedIncome[e.player]+=credited;
        if(credited>0)emitSound(Sfx::Cash,.55f,e.player);
        e.oreLoad-=quantity;if(!ordinary)e.gemLoad-=quantity;
    }
    return true;
}
