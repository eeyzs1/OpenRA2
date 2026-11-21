#include "game/world.h"
#include "core/binary_state_io.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace {
bool transfer(BinaryStateIO& io,std::vector<NativeLauncherState>& launchers,std::vector<NativeRocketState>& rockets) {
    if(!io.vector(launchers,16384))return false;
    for(auto& l:launchers){io.value(l.actor);io.value(l.definition);io.value(l.target);io.value(l.nextLaunch);
        if(!io.vector(l.nodes,16))return false;for(auto& node:l.nodes){io.value(node.missile);io.value(node.readyAt);}}
    if(!io.vector(rockets,16384))return false;
    for(auto& r:rockets) {
        io.value(r.actor);io.value(r.definition);io.value(r.source);io.value(r.slot);uint8_t phase=(uint8_t)r.phase;io.value(phase);r.phase=(NativeRocketPhase)phase;
        io.value(r.phaseTicks);io.value(r.altitude);io.value(r.speed);io.value(r.pitch);io.value(r.heading);
        io.value(r.destinationX);io.value(r.destinationY);io.value(r.destinationZ);io.flag(r.elite);io.flag(r.cruised);io.flag(r.diving);
    }
    return io.ok;
}
}

bool World::validateNativeRocketState() const {
    for(const auto& p:projs)if(nativeWeaponReference(p.w)<-1)return false;
    if(nativeRocketDefinitions.empty()) {
        if(!nativeLaunchers.empty() || !nativeRockets.empty())return false;
        for(const auto& actor:ents)if(actor.alive && actor.nativeIdentity.definition && actor.nativeIdentity.definition->nativeRocket)return false;
        return true;
    }
    if(nativeLaunchers.size()>16384 || nativeRockets.size()>16384)return false;
    std::set<EID> launchers,rockets,attached;
    const auto timerValid=[&](uint64_t deadline,int sourceFrames) {
        return deadline<=tick || deadline-tick<=(uint64_t)sourceFrames*LOGIC_FPS/15;
    };
    for(const auto& l:nativeLaunchers) {
        if(!valid(l.actor) || !launchers.insert(l.actor).second || l.definition<0 || l.definition>=(int)nativeRocketDefinitions.size()
            || nativeLauncherDefinition(ents[l.actor])!=l.definition || (l.target!=-1 && !valid(l.target))
            || l.nodes.size()!=(size_t)nativeRocketDefinitions[l.definition].capacity)return false;
        const auto& d=nativeRocketDefinitions[l.definition];
        if(!timerValid(l.nextLaunch,d.launchDelay))return false;int takingOff=0;
        for(const auto& node:l.nodes) {
            if(!timerValid(node.readyAt,std::max(d.regenerate,d.reload)))return false;
            if(node.missile!=-1) {
                if(!valid(node.missile) || !attached.insert(node.missile).second || node.readyAt>tick)return false;
                ++takingOff;
            }
        }
        if(takingOff>1)return false;
    }
    for(const auto& r:nativeRockets) {
        if(!valid(r.actor) || !rockets.insert(r.actor).second || r.definition<0 || r.definition>=(int)nativeRocketDefinitions.size()
            || !ents[r.actor].nativeIdentity.definition || ents[r.actor].nativeIdentity.type!=nativeRocketDefinitions[r.definition].missile
            || (r.source!=-1 && (!valid(r.source) || nativeLauncherDefinition(ents[r.source])!=r.definition))
            || r.slot < -1 || r.slot>=nativeRocketDefinitions[r.definition].capacity || (uint8_t)r.phase>2 || r.phaseTicks<0 || r.phaseTicks>10000000)return false;
        for(float value:{r.altitude,r.speed,r.pitch,r.heading,r.destinationX,r.destinationY,r.destinationZ})if(!std::isfinite(value))return false;
        if(r.altitude < -10000 || r.altitude>200000 || r.speed<0 || r.speed>255 || std::abs(r.pitch)>1.58f || std::abs(r.heading)>100000
            || r.destinationX<0 || r.destinationY<0 || r.destinationX>=map.w || r.destinationY>=map.h || r.destinationZ<0 || r.destinationZ>200000)return false;
        if(r.phase==NativeRocketPhase::Flight) {if(r.slot!=-1 || attached.contains(r.actor))return false;}
        else {
            const auto& d=nativeRocketDefinitions[r.definition];
            const int duration=(r.phase==NativeRocketPhase::Pause?d.pause:d.tilt)*LOGIC_FPS/15;
            if(r.phaseTicks>=std::max(1,duration) || r.speed!=0 || r.cruised || r.diving)return false;
            if(r.source<0 || r.slot<0 || !attached.contains(r.actor))return false;bool found=false;
            for(const auto& l:nativeLaunchers)if(l.actor==r.source && l.nodes[r.slot].missile==r.actor)found=true;
            if(!found)return false;
        }
    }
    for(EID id=0;id<(EID)ents.size();++id)if(valid(id)) {
        if(ents[id].nativeIdentity.definition && ents[id].nativeIdentity.definition->nativeRocket && !rockets.contains(id))return false;
        if(nativeLauncherDefinition(ents[id])>=0 && !launchers.contains(id))return false;
    }
    for(const auto& r:nativeRockets)if(r.source!=-1 && !launchers.contains(r.source))return false;
    for(EID actor:attached)if(!rockets.contains(actor))return false;
    return true;
}

bool World::writeNativeRocketState(FILE* file) const {
    if(!validateNativeRocketState())return false;
    BinaryStateIO io{file};auto launchers=nativeLaunchers;auto rockets=nativeRockets;
    if(!nativeRocketDefinitions.empty() && !transfer(io,launchers,rockets))return false;
    if(nativeTypes.empty())return true;
    uint32_t count=(uint32_t)projs.size();io.value(count);
    for(const auto& p:projs){int code=nativeWeaponReference(p.w);io.value(code);}
    return io.ok;
}

bool World::readNativeRocketState(FILE* file,bool extendedWeapons) {
    nativeLaunchers.clear();nativeRockets.clear();
    BinaryStateIO io{file,true};
    if(!nativeRocketDefinitions.empty() && !transfer(io,nativeLaunchers,nativeRockets))return false;
    if(nativeTypes.empty() || (!extendedWeapons && nativeRocketDefinitions.empty()))return validateNativeRocketState();
    uint32_t count=0;io.value(count);if(!io.ok || count!=projs.size())return false;
    for(auto& p:projs) {
        int code=-1;io.value(code);if(!io.ok || !restoreNativeWeaponReference(p.w,code,extendedWeapons))return false;
    }
    if(!validateNativeRocketState())return false;
    for(const auto& r:nativeRockets){auto& e=ents[r.actor];e.nativeAltitude=e.previousNativeAltitude=r.altitude;e.nativePitch=r.pitch;}
    return true;
}
