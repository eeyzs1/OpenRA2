#include "game/world.h"
#include "game/actor_state_transfer.h"
#include <algorithm>
#include <limits>
#include <cmath>

namespace {
using Phase=World::AircraftPhase;
int sourceTicks(int frames){return frames*LOGIC_FPS/15;}
int regeneration(const World& w,const World::AircraftSpawner& s) {
    (void)w;return s.regenerateTicks;
}
int reloading(const World& w,const World::AircraftSpawner& s) {
    (void)w;return s.reloadTicks;
}
int launchDelay(const World& w,const World::AircraftSpawner& s) {
    (void)w;return s.launchDelayTicks;
}
bool newAircraft(World& w,World::AircraftSpawner& s,World::Ent::GarrisonedUnit& p) {
    p={};p.type=w.ents[s.owner.id].utype==UnitType::Destroyer?UnitType::ASW:UnitType::Hornet;
    if(s.definition>=0) {
        if(w.nativeProductionSerial==std::numeric_limits<uint64_t>::max())return false;
        const int type=w.nativeAircraftDefinitions[s.definition].aircraft;
        p.nativeIdentity={type,w.nativeTypes[type],"production:"+std::to_string(s.player)+":"+std::to_string(w.nativeProductionSerial++),"",false};
    }
    p.hp=unitDef(p).hp;p.ammo=unitDef(p).ammo;p.combatActor=w.allocateCombatActor();return true;
}
bool targeting(const World& w,const World::AircraftSpawner& s) {
    if(!w.valid(s.owner) || !w.valid(s.target))return false;
    const auto& owner=w.ents[s.owner.id];const auto& target=w.ents[s.target.id];
    return w.aircraftTargets(s.owner.id,s.target.id) && (target.player<0 || w.isEnemy(owner.player,target.player));
}
void dispatch(World& w,World::AircraftSpawner& s) {
    for(auto& n:s.nodes)if(n.phase==Phase::Preparing && w.valid(n.aircraft)) {
        n.phase=Phase::Attacking;auto& e=w.ents[n.aircraft.id];e.target=s.target.id;e.state=UState::Chasing;
    }
}
void recall(World& w,World::AircraftSpawner& s) {
    s.pending=0;
    for(auto& node:s.nodes)if(w.valid(node.aircraft)) {
        auto& e=w.ents[node.aircraft.id];e.target=INVALID_EID;e.clearPath();e.wps.clear();e.attackMove={};
        e.state=UState::Returning;node.phase=Phase::Returning;
    }
}
void synchronizeOwner(World& w,World::AircraftSpawner& s) {
    if(w.ents[s.owner.id].player==s.player)return;
    s.player=w.ents[s.owner.id].player;recall(w,s);s.target={};
    for(auto& n:s.nodes)if(w.valid(n.aircraft)){auto& e=w.ents[n.aircraft.id];w.changeActorOwner(n.aircraft.id,s.player);e.origPlayer=-1;e.mindBy=INVALID_EID;}
}
}

bool World::hasAircraftSpawner(EID actor) const {
    if(!valid(actor) || ents[actor].isBuilding || (ents[actor].utype!=UnitType::AircraftCarrier && ents[actor].utype!=UnitType::Destroyer))return false;
    for(const auto& s:aircraftSpawners)if(s.owner==entityRef(actor))return true;
    return false;
}
bool World::managedAircraft(EID actor) const {
    if(valid(actor) && ents[actor].supportAir)return true;
    if(!valid(actor) || ents[actor].isBuilding || (ents[actor].utype!=UnitType::Hornet && ents[actor].utype!=UnitType::ASW))return false;
    const auto ref=entityRef(actor);
    for(const auto& s:aircraftSpawners)if(s.owner.id==ents[actor].airbase)
        for(const auto& node:s.nodes)if(node.aircraft==ref)return true;
    return false;
}
int World::dockedAircraft(EID actor,bool readyOnly) const {
    for(const auto& s:aircraftSpawners)if(valid(s.owner) && s.owner.id==actor) {
        int n=0;for(const auto& node:s.nodes)n+=node.phase==Phase::Docked || (!readyOnly && node.phase==Phase::Reloading);return n;
    }
    return valid(actor)?(int)ents[actor].cargo.size():0;
}
int World::aircraftCapacity(EID actor) const {
    for(const auto& s:aircraftSpawners)if(valid(s.owner) && s.owner.id==actor)return (int)s.nodes.size();
    return valid(actor)?unitDef(ents[actor]).cargoCap:0;
}
bool World::initializeAircraftSpawner(EID actor) {
    if(!aircraftSpawnersEnabled || !valid(actor) || ents[actor].isBuilding)return false;
    const bool destroyer=ents[actor].utype==UnitType::Destroyer;
    if((!destroyer && ents[actor].utype!=UnitType::AircraftCarrier) || (destroyer && !aswEnabled))return false;
    if(hasAircraftSpawner(actor))return true;
    AircraftSpawner s;s.owner=entityRef(actor);s.player=ents[actor].player;
    int capacity=destroyer?1:std::clamp(unitDef(ents[actor]).cargoCap,0,64);
    if(ents[actor].nativeIdentity.definition) {
        for(int i=0;i<(int)nativeAircraftDefinitions.size();++i)if(nativeAircraftDefinitions[i].launcher==ents[actor].nativeIdentity.type){s.definition=i;break;}
        if(s.definition<0)return false;
        capacity=nativeAircraftDefinitions[s.definition].capacity;
    } else if(usesRulesProfile() || map.native.active())return false;
    if(capacity<=0)return false;
    s.regenerateTicks=s.definition<0?(destroyer?g_gameRules.destroyerRegenerateTicks:g_gameRules.carrierRegenerateTicks):sourceTicks(nativeAircraftDefinitions[s.definition].regenerate);
    s.reloadTicks=s.definition<0?(destroyer?g_gameRules.destroyerReloadTicks:g_gameRules.carrierReloadTicks):sourceTicks(nativeAircraftDefinitions[s.definition].reload);
    s.launchDelayTicks=s.definition<0?(destroyer?g_gameRules.destroyerLaunchDelayTicks:g_gameRules.carrierLaunchDelayTicks):sourceTicks(nativeAircraftDefinitions[s.definition].launchDelay);
    auto& cargo=ents[actor].cargo;
    for(int slot=0;slot<capacity;++slot) {
        Ent::GarrisonedUnit p;if(!newAircraft(*this,s,p))return false;
        AircraftNode node;node.cargo=(int)cargo.size();cargo.push_back(std::move(p));s.nodes.push_back(node);
    }
    aircraftSpawners.push_back(std::move(s));return true;
}

bool World::launchAircraft(EID actor,EID target) {
    if(!valid(actor) || !valid(target) || ents[actor].immobilized())return false;
    const auto& parent=ents[actor];
    if((parent.state!=UState::Attacking && parent.state!=UState::Chasing) || parent.target!=target)return false;
    float tx=0,ty=0;attackTargetPoint(parent,ents[target],tx,ty);
    if(distf(parent.x,parent.y,tx,ty)>attackRange(parent,ents[target]))return false;
    for(auto& s:aircraftSpawners)if(s.owner==entityRef(actor)) {
        if(s.target!=entityRef(target)){s.target=entityRef(target);s.pending=0;}
        if(!targeting(*this,s) || s.launchRemaining>0)return false;
        if(!s.pending)for(size_t i=0;i<s.nodes.size();++i)if(s.nodes[i].phase==Phase::Docked)s.pending|=1ull<<i;
        if(!s.pending)return false;
        size_t slot=0;while(!(s.pending&(1ull<<slot)))++slot;auto& node=s.nodes[slot];
        auto& owner=ents[actor];const auto stored=owner.cargo[node.cargo];
        const EID plane=spawnUnit(owner.player,stored.type,owner.x,owner.y,stored.nativeIdentity.type,
            stored.nativeIdentity.type>=0?&stored.nativeIdentity:nullptr);
        if(!valid(plane))return false;
        ActorStateTransfer::restore(ents[plane],stored);ents[plane].airbase=actor;ents[plane].state=UState::Circling;
        ents[plane].target=INVALID_EID;ents[plane].guard=false;
        const int removed=node.cargo;owner.cargo.erase(owner.cargo.begin()+removed);
        for(auto& other:s.nodes)if(other.cargo>removed)--other.cargo;
        node.cargo=-1;node.aircraft=entityRef(plane);node.phase=Phase::Preparing;
        s.pending&=~(1ull<<slot);s.launchRemaining=launchDelay(*this,s);
        emitSoundAt(Sfx::Missile,owner.x,owner.y,actor);
        if(!s.pending)dispatch(*this,s);
        return true;
    }
    return false;
}

void World::stopAircraft(EID actor) {
    if(!hasAircraftSpawner(actor))return;
    for(auto& s:aircraftSpawners)if(s.owner==entityRef(actor)){recall(*this,s);s.target={};}
}

void World::updateAircraftSpawners() {
    for(auto& s:aircraftSpawners) {
        auto& owner=ents[s.owner.id];
        synchronizeOwner(*this,s);
        const bool ordered=(owner.state==UState::Attacking || owner.state==UState::Chasing) && valid(owner.target);
        if(ordered && aircraftTargets(s.owner.id,owner.target) && (ents[owner.target].player<0 || isEnemy(owner.player,ents[owner.target].player))
            && s.target!=entityRef(owner.target)) {
            s.target=entityRef(owner.target);s.pending=0;
            for(auto& n:s.nodes)if(valid(n.aircraft) && n.phase==Phase::Attacking){ents[n.aircraft.id].target=s.target.id;ents[n.aircraft.id].state=UState::Chasing;}
        }
        if((ordered && !aircraftTargets(s.owner.id,owner.target)) || !targeting(*this,s)){recall(*this,s);s.target={};}
        else if(!ordered && s.pending){s.pending=0;dispatch(*this,s);} // moving the carrier does not cancel an airborne sortie
        if(owner.immobilized())continue;
        if(s.launchRemaining>0)--s.launchRemaining;
        for(auto& node:s.nodes) {
            if(node.phase!=Phase::Dead && node.phase!=Phase::Reloading)continue;
            if(node.remaining>0)--node.remaining;
            if(node.remaining>0)continue;
            if(node.phase==Phase::Dead) {
                Ent::GarrisonedUnit p;if(!newAircraft(*this,s,p))continue;
                node.cargo=(int)owner.cargo.size();owner.cargo.push_back(std::move(p));
            } else {
                auto& p=owner.cargo[node.cargo];p.hp=unitDef(p).hp;p.ammo=unitDef(p).ammo;
            }
            node.phase=Phase::Docked;
        }
    }
}

bool World::updateSpawnedAircraft(EID actor) {
    if(!valid(actor) || (ents[actor].utype!=UnitType::Hornet && ents[actor].utype!=UnitType::ASW))return false;
    for(auto& s:aircraftSpawners)if(s.owner.id==ents[actor].airbase)for(size_t slot=0;slot<s.nodes.size();++slot) {
        auto& node=s.nodes[slot];if(node.aircraft!=entityRef(actor))continue;
        synchronizeOwner(*this,s);
        auto& e=ents[actor];const auto& owner=ents[s.owner.id];
        if(e.state==UState::Returning || e.ammo<=0){node.phase=Phase::Returning;e.state=UState::Returning;e.target=INVALID_EID;}
        if(node.phase==Phase::Preparing) {
            const float a=(float)slot*2.0943951f;
            flyToward(e,owner.x+std::cos(a),owner.y+std::sin(a));return true;
        }
        if(node.phase==Phase::Returning) {
            if(flyToward(e,owner.x,owner.y) || distf(e.x,e.y,owner.x,owner.y)<.6f) {
                auto p=ActorStateTransfer::capture(e);p.nativeIdentity.sleeping=false;
                node.cargo=(int)ents[s.owner.id].cargo.size();ents[s.owner.id].cargo.push_back(std::move(p));
                node.aircraft={};node.phase=Phase::Reloading;node.remaining=reloading(*this,s);
                consumeEntity(actor);
            }
            return true;
        }
        return false;
    }
    return false;
}

void World::retireAircraftActor(EID actor) {
    for(auto& s:aircraftSpawners) {
        if(s.owner.id==actor) {
            recall(*this,s);
            for(auto& node:s.nodes)if(valid(node.aircraft))ents[node.aircraft.id].airbase=INVALID_EID;
        }
        if(s.target.id==actor){recall(*this,s);s.target={};}
        if(actor>=0 && actor<(EID)ents.size() && !ents[actor].isBuilding && (ents[actor].utype==UnitType::Hornet || ents[actor].utype==UnitType::ASW))
        for(auto& node:s.nodes)if(node.aircraft.id==actor) {
            node.aircraft={};node.cargo=-1;node.phase=Phase::Dead;node.remaining=regeneration(*this,s);
        }
    }
    std::erase_if(aircraftSpawners,[&](const auto& s){return s.owner.id==actor;});
}

bool World::underwaterTarget(const Ent& target) const {
    if(target.isBuilding || target.airborne())return false;
    const auto& d=target.nativeIdentity.definition;
    if(d && d->sourceNavalTargeting)return d->underwater;
    return target.utype==UnitType::Typhoon || target.utype==UnitType::Boomer || target.utype==UnitType::Dolphin || target.utype==UnitType::Squid;
}

bool World::aircraftTargets(EID owner,EID target) const {
    if(!valid(owner) || !valid(target) || ents[target].airborne())return false;
    for(const auto& s:aircraftSpawners)if(s.owner==entityRef(owner)) {
        const bool underwaterOnly=s.definition>=0?nativeAircraftDefinitions[s.definition].underwaterOnly:ents[owner].utype==UnitType::Destroyer;
        return underwaterOnly?underwaterTarget(ents[target]):(!aswEnabled || !underwaterTarget(ents[target]));
    }
    return false;
}
