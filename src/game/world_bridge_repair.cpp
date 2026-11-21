#include "game/world.h"
#include "game/lang.h"
#include <algorithm>
#include <limits>
#include <map>
#include <tuple>

void World::resetNativeBridgeScenario() {
    nativeBridgeBlueprints.clear();nativeBridgeGroups.clear();nativeBridgesDestroyable=true;map.bridgeCells.clear();
}

void World::rebuildNativeBridgeGroups() {
    nativeBridgeGroups.clear();std::map<std::tuple<int,int,int>,int> at;
    for(int i=0;i<(int)nativeBridgeBlueprints.size();++i) {
        auto& bridge=nativeBridgeBlueprints[i];bridge.group=-1;at.emplace(std::tuple{bridge.type,bridge.x,bridge.y},i);
    }
    for(int first=0;first<(int)nativeBridgeBlueprints.size();++first) {
        if(nativeBridgeBlueprints[first].group>=0)continue;
        const int group=(int)nativeBridgeGroups.size();auto& members=nativeBridgeGroups.emplace_back();
        members.push_back(first);nativeBridgeBlueprints[first].group=group;
        for(size_t head=0;head<members.size();++head) {
            const auto segment=nativeBridgeBlueprints[members[head]];
            const auto& id=nativeTypes[segment.type]->id;const bool alongY=id=="BRIDGE2" || id=="BRIDGEB2";
            for(int sign:{-1,1}) {
                const auto next=at.find({segment.type,segment.x+(alongY?0:sign),segment.y+(alongY?sign:0)});
                if(next==at.end())continue;auto& neighbor=nativeBridgeBlueprints[next->second];
                if(neighbor.group>=0 || neighbor.height!=segment.height)continue;
                neighbor.group=group;members.push_back(next->second);
            }
        }
        std::sort(members.begin(),members.end(),[&](int a,int b) {
            const auto& left=nativeBridgeBlueprints[a];const auto& right=nativeBridgeBlueprints[b];
            return std::pair{left.y,left.x}<std::pair{right.y,right.x};
        });
    }
}

int World::bridgeForHut(EID hut) const {
    if(!valid(hut) || !ents[hut].isBuilding)return -1;
    const auto& building=ents[hut];const auto& definition=building.nativeIdentity.definition;
    if(!definition || !definition->bridgeRepairHut)return -1;
    int result=-1,distance=std::numeric_limits<int>::max();bool ambiguous=false;
    for(int group=0;group<(int)nativeBridgeGroups.size();++group) {
        const auto& members=nativeBridgeGroups[group];if(members.empty())continue;
        for(int direction:{-1,1}) {
            const int end=direction<0?members.front():members.back();
            const auto& source=nativeBridgeBlueprints[end];const auto& id=nativeTypes[source.type]->id;
            const bool alongY=id=="BRIDGE2" || id=="BRIDGEB2";
            const int x=source.x+(alongY?0:direction),y=source.y+(alongY?direction:0);
            if(!map.inBounds(x,y) || map.at(x,y).height!=source.height+4
                || actorSurfaceHeight(building)!=map.at(x,y).height)continue;
            const int dx=(int)building.x-x,dy=(int)building.y-y;
            if(abs(dx)>2 || abs(dy)>2)continue;
            const int score=dx*dx+dy*dy;
            if(score<distance){result=group;distance=score;ambiguous=false;}
            else if(score==distance && group!=result)ambiguous=true;
        }
    }
    return ambiguous?-1:result;
}

EID World::liveBridgeSegment(const NativeBridgeBlueprint& segment) const {
    for(EID id=0;id<(EID)ents.size();++id) {
        const auto& actor=ents[id];
        if(actor.alive && actor.isBuilding && actor.nativeIdentity.type==segment.type
            && actor.nativeIdentity.object==segment.object && actor.x==segment.x && actor.y==segment.y)return id;
    }
    return INVALID_EID;
}

bool World::bridgeNeedsRepair(int group) const {
    if(group<0 || group>=(int)nativeBridgeGroups.size())return false;
    for(int index:nativeBridgeGroups[group]) {
        const auto& segment=nativeBridgeBlueprints[index];const auto id=liveBridgeSegment(segment);
        if(!valid(id) || ents[id].hp<bldDef(ents[id]).hp
            || map.native.tiles[cellIdx(segment.x,segment.y)].overlay!=nativeTypes[segment.type]->overlay)return true;
    }
    return false;
}

bool World::canRestoreBridge(int group) const {
    if(group<0 || group>=(int)nativeBridgeGroups.size() || !map.native.active())return false;
    for(int index:nativeBridgeGroups[group]) {
        const auto& segment=nativeBridgeBlueprints[index];
        if(!map.inBounds(segment.x,segment.y) || map.at(segment.x,segment.y).height!=segment.height)return false;
        const auto overlay=map.native.tiles[cellIdx(segment.x,segment.y)].overlay;
        if(overlay!=255 && overlay!=nativeTypes[segment.type]->overlay)return false;
        const auto& type=nativeTypes[segment.type]->id;const bool alongY=type=="BRIDGE2" || type=="BRIDGEB2";
        for(int offset=-1;offset<=1;++offset) {
            const int x=segment.x+(alongY?offset:0),y=segment.y+(alongY?0:offset);
            for(EID id=unitAtCell(x,y,0);id!=INVALID_EID;id=ents[id].occNext)
                if(ents[id].navLayer==0 && !unitDef(ents[id]).fitsUnderBridge())return false;
        }
    }
    return true;
}

bool World::restoreBridge(int group) {
    if(!bridgeNeedsRepair(group) || !canRestoreBridge(group))return false;
    // Immutable authored keys identify the span even after every dead entity
    // slot has been reused. Never revive an old EID or overwrite another actor.
    for(int index:nativeBridgeGroups[group]) {
        const auto& segment=nativeBridgeBlueprints[index];auto id=liveBridgeSegment(segment);
        if(!valid(id))id=spawnNativeActor(segment.type,segment.house,(float)segment.x,(float)segment.y,256,segment.facing,
            segment.object,segment.tag,"Sleep");
        if(!valid(id))return false;ents[id].hp=bldDef(ents[id]).hp;
        auto& tile=map.native.tiles[cellIdx(segment.x,segment.y)];
        tile.overlay=(uint8_t)nativeTypes[segment.type]->overlay;tile.overlayData=(uint8_t)segment.frame;
    }
    rebuildNativeBridges();resetMovementCorridors();
    movementReservations.resize(movementSlotCount(),INVALID_EID);
    return true;
}

void World::updateBridgeEngineer(Ent& actor,EID id) {
    const auto fail=[&](EngineerRejectReason reason) {
        auto& current=ents[id];
        if(current.feedbackEngineerTarget==current.target)emitEngineerResult(current.player,current.target,EngineerIntent::RepairBridge,{0,1,id,reason});
        current.engineerIntent=EngineerIntent::None;current.feedbackEngineerTarget=INVALID_EID;
        current.target=INVALID_EID;current.clearPath();current.wps.clear();current.blockTick=0;current.state=UState::Idle;
    };
    const auto reason=engineerRejection(id,actor.target,EngineerIntent::RepairBridge);
    if(reason!=EngineerRejectReason::None){fail(reason);return;}
    const auto& hut=ents[actor.target];
    if(interactionWithinReach(actor,hut,1.1f)) {
        const int player=actor.player,tag=hut.nativeEventTag;
        if(!restoreBridge(bridgeForHut(actor.target))){fail(EngineerRejectReason::BridgeBlocked);return;}
        offerNativeTagEvent(tag,1,id);
        eva(player,TR(S::MsgBridgeRestored));if(valid(id))consumeEntity(id);return;
    }
    if(actor.pathIdx>=(int)actor.path.size() || (tick+(uint64_t)id)%30==0) {
        if(repathsThisTick>=8)return;++repathsThisTick;++repathRequests;std::vector<Vec2i> path;
        if(!interactionPath(actor,hut,1.1f,path)){++repathFailures;fail(EngineerRejectReason::Unreachable);return;}
        actor.path=std::move(path);actor.pathIdx=0;
    }
    moveAlongPath(actor,id);
}
