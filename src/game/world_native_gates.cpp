#include "game/world.h"
#include <algorithm>
#include <cmath>

namespace {
bool isGate(const World::Ent& actor) {
    return actor.alive && actor.isBuilding && actor.nativeIdentity.definition && actor.nativeIdentity.definition->gate;
}
bool inside(const World::Ent& gate,int x,int y) {
    const auto& d=bldDef(gate);
    return x>=gate.x && y>=gate.y && x<gate.x+d.w && y<gate.y+d.h;
}
bool approaching(const World::Ent& actor,const World::Ent& gate) {
    if(actor.pathIdx>=(int)actor.path.size() || actor.immobilized() || actor.state==UState::Idle)return false;
    const auto next=actor.path[actor.pathIdx];const int x=(int)actor.x,y=(int)actor.y;
    if(std::abs(next.x-x)>1 || std::abs(next.y-y)>1)return false;
    return inside(gate,next.x,next.y) || (next.x!=x && next.y!=y && (inside(gate,next.x,y) || inside(gate,x,next.y)));
}
bool hasGateTypes(const World& world) {
    return std::any_of(world.nativeTypes.begin(),world.nativeTypes.end(),[](const auto& d){return d->gate;});
}
}

uint8_t World::nativeGateAccess(const Ent& actor) const {
    if(map.gateCells.empty() || actor.player<0 || actor.player>=numPlayers)return 0;
    uint8_t result=0;
    // A gate's owner decides whom it opens for. Native alliances are directed.
    for(int owner=0;owner<numPlayers;++owner)if(isAllied(owner,actor.player))result|=(uint8_t)(1u<<owner);
    return result;
}

void World::rebuildNativeGates() {
    map.gateCells.clear();
    for(EID id=0;id<(EID)ents.size();++id) {
        const auto& gate=ents[id];if(!isGate(gate))continue;
        if(map.gateCells.empty())map.gateCells.resize(map.cells.size());
        const auto& d=bldDef(gate);
        for(int y=(int)gate.y;y<gate.y+d.h;++y)for(int x=(int)gate.x;x<gate.x+d.w;++x)
            if(map.inBounds(x,y) && bldOcc[cellIdx(x,y)]==id+1)
                map.gateCells[cellIdx(x,y)]={id,gate.player,gate.nativeGatePhase==NativeGatePhase::Open};
    }
}

bool World::waitingForNativeGate(const Ent& actor,Vec2i next) const {
    if(map.gateCells.empty() || actor.airborne())return false;
    const auto check=[&](int x,int y) {
        if(!map.inBounds(x,y))return false;const auto& cell=map.gateCells[cellIdx(x,y)];
        return valid(cell.owner) && cell.owner+1==bldOcc[cellIdx(x,y)] && !cell.open
            && isAllied(ents[cell.owner].player,actor.player)
            && std::abs(actorSurfaceHeight(actor)-actorSurfaceHeight(ents[cell.owner]))<=MAP_MAX_CLIMB;
    };
    const int x=(int)actor.x,y=(int)actor.y;
    if(std::abs(next.x-x)>1 || std::abs(next.y-y)>1)return false;
    return check(next.x,next.y) || (next.x!=x && next.y!=y && (check(next.x,y) || check(x,next.y)));
}

void World::updateNativeGates() {
    if(map.gateCells.empty())return;
    for(EID id=0;id<(EID)ents.size();++id) {
        auto& gate=ents[id];if(!isGate(gate))continue;
        const auto& d=*gate.nativeIdentity.definition;bool occupied=false,wanted=false;
        for(const auto& actor:ents) {
            if(!actor.alive || actor.isBuilding || actor.airborne() || actor.parasiting || actor.magneticHeight>0
                || std::abs(actorSurfaceHeight(actor)-actorSurfaceHeight(gate))>MAP_MAX_CLIMB)continue;
            occupied|=inside(gate,(int)actor.x,(int)actor.y);
            if(isAllied(gate.player,actor.player) && approaching(actor,gate))wanted=true;
        }
        if(wanted || occupied) {
            gate.nativeGateHold=(wanted || gate.nativeGatePhase!=NativeGatePhase::Closed)?d.gateCloseTicks:0;
            if(wanted || gate.nativeGatePhase==NativeGatePhase::Closing) {
                if(gate.nativeGatePhase==NativeGatePhase::Closed || gate.nativeGatePhase==NativeGatePhase::Closing)
                    gate.nativeGatePhase=NativeGatePhase::Opening;
            }
        } else if(gate.nativeGateHold>0)--gate.nativeGateHold;
        switch(gate.nativeGatePhase) {
        case NativeGatePhase::Closed: break;
        case NativeGatePhase::Opening:
            gate.nativeGateProgress=std::min(d.gateOpenTicks,gate.nativeGateProgress+1);
            if(gate.nativeGateProgress==d.gateOpenTicks)gate.nativeGatePhase=NativeGatePhase::Open;
            break;
        case NativeGatePhase::Open:
            if(!occupied && !wanted && !gate.nativeGateHold)gate.nativeGatePhase=NativeGatePhase::Closing;
            break;
        case NativeGatePhase::Closing:
            gate.nativeGateProgress=std::max(0,gate.nativeGateProgress-1);
            if(!gate.nativeGateProgress)gate.nativeGatePhase=NativeGatePhase::Closed;
            break;
        }
    }
    rebuildNativeGates();
}

bool World::writeNativeGateState(FILE* file) const {
    // Ordinary worlds retain their old inactive tail. The source definition
    // unambiguously says whether this v34 extension exists.
    if(!hasGateTypes(*this))return true;
    uint32_t count=0;for(const auto& actor:ents)if(isGate(actor))++count;
    if(fwrite(&count,sizeof(count),1,file)!=1)return false;
    for(EID id=0;id<(EID)ents.size();++id) {
        const auto& gate=ents[id];if(!isGate(gate))continue;const auto& d=*gate.nativeIdentity.definition;
        const uint8_t phase=(uint8_t)gate.nativeGatePhase;
        if(phase>3 || gate.nativeGateProgress<0 || gate.nativeGateProgress>d.gateOpenTicks
            || gate.nativeGateHold<0 || gate.nativeGateHold>d.gateCloseTicks
            || (phase==0 && (gate.nativeGateProgress || gate.nativeGateHold))
            || (phase==2 && gate.nativeGateProgress!=d.gateOpenTicks))return false;
        const int32_t entity=id,progress=gate.nativeGateProgress,hold=gate.nativeGateHold;
        if(fwrite(&entity,sizeof(entity),1,file)!=1 || fwrite(&phase,1,1,file)!=1
            || fwrite(&progress,sizeof(progress),1,file)!=1 || fwrite(&hold,sizeof(hold),1,file)!=1)return false;
    }
    return true;
}

bool World::readNativeGateState(FILE* file) {
    if(!hasGateTypes(*this)){rebuildNativeGates();return true;}
    uint32_t count=0,expected=0;for(const auto& actor:ents)if(isGate(actor))++expected;
    if(fread(&count,sizeof(count),1,file)!=1 || count!=expected)return false;
    int32_t previous=-1;
    for(uint32_t i=0;i<count;++i) {
        int32_t id=0,progress=0,hold=0;uint8_t phase=0;
        if(fread(&id,sizeof(id),1,file)!=1 || fread(&phase,1,1,file)!=1 || fread(&progress,sizeof(progress),1,file)!=1
            || fread(&hold,sizeof(hold),1,file)!=1 || id<=previous || !valid(id) || !isGate(ents[id]))return false;
        auto& gate=ents[id];const auto& d=*gate.nativeIdentity.definition;
        if(phase>3 || progress<0 || progress>d.gateOpenTicks || hold<0 || hold>d.gateCloseTicks
            || (phase==0 && (progress || hold)) || (phase==2 && progress!=d.gateOpenTicks))return false;
        previous=id;gate.nativeGatePhase=(NativeGatePhase)phase;gate.nativeGateProgress=progress;gate.nativeGateHold=hold;
    }
    rebuildNativeGates();return true;
}
