#include "game/world.h"
#include <set>
#include <algorithm>

bool World::validateAircraftState() const {
    if(!aswEnabled){
        for(const auto& d:nativeAircraftDefinitions)if(nativeTypes[d.launcher]->unit.type==UnitType::Destroyer)return false;
        for(const auto& e:ents)if(e.alive && !e.isBuilding && e.utype==UnitType::ASW)return false;
    }
    if(!aircraftSpawnersEnabled)return aircraftSpawners.empty() && nativeAircraftDefinitions.empty();
    if(aircraftSpawners.size()>16384)return false;
    std::set<EID> owners,aircraft;
    for(const auto& s:aircraftSpawners) {
        if(!valid(s.owner) || !owners.insert(s.owner.id).second || ents[s.owner.id].isBuilding
            || (ents[s.owner.id].utype!=UnitType::AircraftCarrier && !(aswEnabled && ents[s.owner.id].utype==UnitType::Destroyer))
            || s.player<0 || s.player>=numPlayers || s.definition < -1 || s.definition>=(int)nativeAircraftDefinitions.size()
            || s.nodes.empty() || s.nodes.size()>64 || (s.nodes.size()<64 && (s.pending>>s.nodes.size())) || (s.target.id!=-1 && !valid(s.target))
            || (s.target.id==-1 && s.target.generation) || s.regenerateTicks<0 || s.regenerateTicks>1800000
            || s.reloadTicks<0 || s.reloadTicks>1800000 || s.launchDelayTicks<0 || s.launchDelayTicks>1800000
            || s.launchRemaining<0 || s.launchRemaining>s.launchDelayTicks)return false;
        const auto& owner=ents[s.owner.id];
        const bool destroyer=owner.utype==UnitType::Destroyer;
        const auto childType=destroyer?UnitType::ASW:UnitType::Hornet;
        if(s.definition<0) {
            if(owner.nativeIdentity.definition || usesRulesProfile() || map.native.active()
                || s.nodes.size()!=(size_t)(destroyer?1:std::clamp(unitDef(owner).cargoCap,0,64)))return false;
        } else {
            const auto& d=nativeAircraftDefinitions[s.definition];
            if(owner.nativeIdentity.type!=d.launcher || s.nodes.size()!=(size_t)d.capacity
                || s.regenerateTicks!=d.regenerate*LOGIC_FPS/15 || s.reloadTicks!=d.reload*LOGIC_FPS/15
                || s.launchDelayTicks!=d.launchDelay*LOGIC_FPS/15)return false;
        }
        std::set<int> cargo;
        for(size_t slot=0;slot<s.nodes.size();++slot) {
            const auto& n=s.nodes[slot];if((uint8_t)n.phase>(uint8_t)AircraftPhase::Dead || n.remaining<0)return false;
            const bool stored=n.phase==AircraftPhase::Docked || n.phase==AircraftPhase::Reloading;
            if(stored) {
                if(n.aircraft!=EntityRef{} || n.cargo<0 || n.cargo>=(int)owner.cargo.size() || !cargo.insert(n.cargo).second)return false;
                const auto& p=owner.cargo[n.cargo];
                if(p.type!=childType || p.hp<=0 || p.hp>unitDef(p).hp || p.ammo<0 || p.ammo>unitDef(p).ammo)return false;
                if(s.definition>=0 && p.nativeIdentity.type!=nativeAircraftDefinitions[s.definition].aircraft)return false;
                if(s.definition<0 && p.nativeIdentity.type!=-1)return false;
                if(n.remaining>(n.phase==AircraftPhase::Reloading?s.reloadTicks:0))return false;
            } else if(n.phase==AircraftPhase::Dead) {
                if(n.aircraft!=EntityRef{} || n.cargo!=-1 || n.remaining>s.regenerateTicks)return false;
            } else {
                if(n.cargo!=-1 || n.remaining || !valid(n.aircraft) || !aircraft.insert(n.aircraft.id).second)return false;
                const auto& e=ents[n.aircraft.id];
                if(e.isBuilding || e.utype!=childType || e.player!=s.player || e.airbase!=s.owner.id)return false;
                if(s.definition>=0 && e.nativeIdentity.type!=nativeAircraftDefinitions[s.definition].aircraft)return false;
            }
            if((s.pending&(1ull<<slot)) && n.phase!=AircraftPhase::Docked)return false;
        }
        if(cargo.size()!=owner.cargo.size())return false;
    }
    for(EID id=0;id<(EID)ents.size();++id)if(valid(id) && !ents[id].isBuilding
        && (ents[id].utype==UnitType::AircraftCarrier || (aswEnabled && ents[id].utype==UnitType::Destroyer))) {
        bool supported=!ents[id].nativeIdentity.definition && !usesRulesProfile() && !map.native.active()
            && (ents[id].utype==UnitType::Destroyer || unitDef(ents[id]).cargoCap>0);
        for(const auto& d:nativeAircraftDefinitions)supported|=ents[id].nativeIdentity.type==d.launcher;
        if(supported && !owners.contains(id))return false;
    }
    return true;
}

bool World::writeAircraftState(FILE* file) const {
    if(!file || !validateAircraftState())return false;
    bool ok=true;uint32_t bytes=0;
    const auto item=[&](const auto& v){bytes+=(uint32_t)sizeof(v);if(ok && fwrite(&v,sizeof(v),1,file)!=1)ok=false;};
    const auto ref=[&](EntityRef r){item(r.id);item(r.generation);};
    item((uint8_t)aircraftSpawnersEnabled);item((uint32_t)aircraftSpawners.size());
    for(const auto& s:aircraftSpawners) {
        ref(s.owner);ref(s.target);item(s.player);item(s.definition);item(s.launchRemaining);item(s.pending);
        item(s.regenerateTicks);item(s.reloadTicks);item(s.launchDelayTicks);item((uint32_t)s.nodes.size());
        for(const auto& n:s.nodes){item((uint8_t)n.phase);ref(n.aircraft);item(n.cargo);item(n.remaining);}
    }
    return ok && fwrite(&bytes,sizeof(bytes),1,file)==1;
}
bool World::readAircraftState(FILE* file) {
    if(!file)return false;aircraftSpawners.clear();bool ok=true;uint32_t bytes=0;
    const auto item=[&](auto& v){bytes+=(uint32_t)sizeof(v);if(ok && fread(&v,sizeof(v),1,file)!=1)ok=false;};
    const auto ref=[&](EntityRef& r){item(r.id);item(r.generation);};
    uint8_t enabled=0;uint32_t count=0;item(enabled);item(count);
    if(!ok || enabled>1 || count>16384 || (!enabled && count))return false;
    aircraftSpawnersEnabled=enabled!=0;aircraftSpawners.resize(count);
    for(auto& s:aircraftSpawners) {
        ref(s.owner);ref(s.target);item(s.player);item(s.definition);item(s.launchRemaining);item(s.pending);
        item(s.regenerateTicks);item(s.reloadTicks);item(s.launchDelayTicks);uint32_t nodes=0;item(nodes);
        if(!ok || !nodes || nodes>64)return false;s.nodes.resize(nodes);
        for(auto& n:s.nodes){uint8_t phase=0;item(phase);n.phase=(AircraftPhase)phase;ref(n.aircraft);item(n.cargo);item(n.remaining);}
    }
    uint32_t footer=0;const auto expected=bytes;item(footer);
    return ok && footer==expected && validateAircraftState();
}
