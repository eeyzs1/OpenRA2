#include "game/world.h"
#include "game/presentation_events.h"
#include <limits>
#include <tuple>

namespace {
Vec2i chronoLandingCell(const World::Ent& refinery,bool directBerth) {
    return {(int)refinery.x+(directBerth?3:4),(int)refinery.y+1};
}
}

bool World::chronoReceiverUsable(const Ent& e,EID receiver) const {
    if(!valid(receiver) || e.player<0)return false;
    const auto& refinery=ents[receiver];
    if(!productionFacilityOperational(refinery) || refinery.player!=e.player || refinery.btype!=BldType::OreRefinery)return false;
    return nativeDockTerrainPassable(e,receiver);
}

bool World::chronoLandingAvailable(EID id,EID receiver,bool directBerth) const {
    const auto& refinery=ents[receiver];
    const auto [x,y]=chronoLandingCell(refinery,directBerth);
    const int layer=refinery.navLayer;
    if(!nativeDockAvailable(receiver,id) || cellHardBlockedForMove(x,y,id,layer))return false;
    if(movementReservations.size()!=movementSlotCount())return true;
    const EID reserved=movementReservations[movementSlot(x,y,layer)];
    return reserved==INVALID_EID || reserved==id;
}

void World::beginChronoReturn(Ent& e,EID id,EID preferred,bool directBerth) {
    e.clearPath();e.nativeMovement={};e.blockTick=e.digTimer=0;
    if(e.player<0) {
        e.cancelChronoReturn();e.state=UState::Idle;e.autoHarvest=false;
        return;
    }
    e.state=UState::HarvestReturn;
    const bool explicitReceiver=chronoReceiverUsable(e,preferred);
    EID receiver=explicitReceiver?preferred:INVALID_EID;
    bool ready=explicitReceiver && chronoLandingAvailable(id,preferred,directBerth);
    if(!explicitReceiver) {
        std::tuple<bool,float,EID> best{true,std::numeric_limits<float>::infinity(),INVALID_EID};
        for(EID candidate=0;candidate<(EID)ents.size();++candidate) {
            if(!chronoReceiverUsable(e,candidate))continue;
            const auto& refinery=ents[candidate];
            const bool free=chronoLandingAvailable(id,candidate,directBerth);
            const auto [x,y]=chronoLandingCell(refinery,directBerth);
            const float dx=x+.5f-e.x,dy=y+.5f-e.y;
            const auto score=std::tuple{!free,dx*dx+dy*dy,candidate};
            if(score<best){best=score;receiver=candidate;ready=free;}
        }
    }
    e.dockRefinery=receiver;
    e.nativeDock=receiver==INVALID_EID?Ent::DockState{}:Ent::DockState{NativeDockPhase::Approach,entityRef(receiver)};
    e.chronoReturn={ready?ChronoReturnPhase::Charging:ChronoReturnPhase::Waiting,
        ready?ChronoReturnTiming::ready:ChronoReturnTiming::retry,explicitReceiver,directBerth};
}

void World::teleportHarvester(Ent& e,EID id,int x,int y,int layer) {
    Effect departure;
    departure.kind=9;departure.aux=(int)ChronoVisual::Departure;
    departure.x=e.x;departure.y=e.y;departure.maxAge=20;
    effects.push_back(departure);
    e.x=e.px=x+.5f;e.y=e.py=y+.5f;e.navLayer=(uint8_t)layer;
    e.clearPath();e.nativeMovement={};unitOccSync(id);
    Effect arrival;
    arrival.kind=9;arrival.x=e.x;arrival.y=e.y;arrival.maxAge=20;
    effects.push_back(arrival);
    emitSoundAt(Sfx::Tesla,e.x,e.y);
}

bool World::updateChronoReturn(Ent& e,EID id) {
    auto& warp=e.chronoReturn;
    if(warp.phase==ChronoReturnPhase::None)return false;
    if(e.state!=UState::HarvestReturn) {
        e.cancelChronoReturn();
        return false;
    }
    const auto receiver=e.nativeDock.receiver;
    const bool directBerth=warp.directBerth;
    if(warp.phase==ChronoReturnPhase::Waiting) {
        if(e.hasChronoYieldPath()) {
            const auto identity=entityRef(id);
            moveAlongPath(e,id);
            if(valid(identity)) {
                auto& miner=ents[id];
                if(miner.state==UState::HarvestReturn && miner.nativeDock.receiver==receiver
                    && miner.hasChronoYieldPath() && miner.pathIdx>=(int)miner.path.size()) {
                    miner.clearPath();miner.nativeMovement={};
                }
            }
            return true;
        }
        if(--warp.ticksLeft==0) {
            beginChronoReturn(e,id,warp.preferReceiver && valid(receiver)?receiver.id:INVALID_EID,directBerth);
            if(warp.phase==ChronoReturnPhase::Waiting && valid(e.nativeDock.receiver)) {
                const auto& refinery=ents[e.nativeDock.receiver.id];
                const auto [x,y]=chronoLandingCell(refinery,directBerth);
                const EID blocker=unitAtCell(x,y,refinery.navLayer);
                if(blocker!=id)yieldIdleCorridor(id,blocker);
            }
        }
        return true;
    }
    if(!valid(receiver) || !chronoReceiverUsable(e,receiver.id)) {
        beginChronoReturn(e,id,INVALID_EID,directBerth);
        return true;
    }
    if(--warp.ticksLeft>0)return true;
    if(warp.phase==ChronoReturnPhase::Charging) {
        if(!chronoLandingAvailable(id,receiver.id,directBerth)) {
            beginChronoReturn(e,id,warp.preferReceiver?receiver.id:INVALID_EID,directBerth);
            return true;
        }
        const auto& refinery=ents[receiver.id];
        const auto [x,y]=chronoLandingCell(refinery,directBerth);
        const int layer=refinery.navLayer;
        if(e.x!=x+.5f || e.y!=y+.5f || e.navLayer!=layer)teleportHarvester(e,id,x,y,layer);
        e.previousNavLayer=e.goalLayer=e.navLayer;
    }
    warp={};
    e.nativeDock.phase=NativeDockPhase::Align;
    return true;
}
