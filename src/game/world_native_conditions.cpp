#include "game/world.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace {
bool same(const std::string& a,const std::string& b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](unsigned char l,unsigned char r){return std::tolower(l)==std::tolower(r);});
}
}

void World::startNativeConditions() {
    nativeConditions={};
    if(!nativeEvents.active || !nativeEventDefinition || !nativeEventDefinition->extendedConditions)return;
    nativeConditions.active=true;
    nativeConditions.lastBuilt.assign(numPlayers,{-1,-1,-1,-1});
    nativeConditions.locals=nativeEventDefinition->initialLocals;
    // Scenario placement and the opening view cannot invent discovery events.
    updateNativeDiscovery(true);
}

bool World::nativeExtendedCondition(const NativeEventCondition& event,int occurrence,EID,int runtime) const {
    if(!nativeConditions.active)return false;
    const auto& trigger=nativeEventDefinition->triggers[nativeEvents.triggers[runtime].definition];
    switch(event.type) {
    case 4:case 6:case 33:return occurrence==event.type;
    case 19:case 20:case 21:case 22:
        return trigger.house>=0 && trigger.house<(int)nativeConditions.lastBuilt.size()
            && event.value>=0 && nativeConditions.lastBuilt[trigger.house][event.type-19]==event.value;
    case 36:case 37:return event.value>=0 && event.value<50 && (nativeConditions.locals[event.value]!=0)==(event.type==36);
    default:return false;
    }
}

void World::recordNativeBuilt(int player,int type) {
    if(!nativeConditions.active || player<0 || player>=numPlayers || type<0 || type>=(int)nativeTypes.size())return;
    const auto& d=*nativeTypes[type];
    const auto find=[&](const auto& entries){for(const auto& [index,id]:entries)if(same(id,d.id))return index;return -1;};
    if(d.isBuilding){const int index=find(nativeEventDefinition->buildingTypes);if(index>=0)nativeConditions.lastBuilt[player][0]=index;return;}
    for(int category=0;category<3;++category){const int index=find(nativeEventDefinition->mobileTypes[category]);
        if(index>=0){nativeConditions.lastBuilt[player][category+1]=index;return;}}
}

void World::setNativeLocal(int index,bool value) {
    if(!nativeConditions.active || index<0 || index>=50 || (nativeConditions.locals[index]!=0)==value)return;
    nativeConditions.locals[index]=(uint8_t)value;
    // Changes reset all dependent timers, even disabled trigger instances.
    for(auto& state:nativeEvents.triggers)for(const auto& event:nativeEventDefinition->triggers[state.definition].events)
        if((event.type==36 || event.type==37) && event.value==index){state.timerStart=tick;break;}
}

void World::updateNativeDiscovery(bool seed) {
    if(!nativeConditions.active)return;
    // consumeEntity also retires engineers and transformed actors. Boarding
    // uses that same path, so retain only keys still live or inside cargo
    // after the complete frame has finished moving passenger identities.
    std::set<std::string> live;
    const auto passenger=[&](auto&& self,const Ent::GarrisonedUnit& p)->void{if(!p.nativeIdentity.object.empty())live.insert(p.nativeIdentity.object);for(const auto& c:p.cargo)self(self,c);};
    for(const auto& e:ents)if(e.alive){if(!e.nativeIdentity.object.empty())live.insert(e.nativeIdentity.object);
        for(const auto& p:e.cargo)passenger(passenger,p);for(const auto& p:e.garrison)passenger(passenger,p);}
    std::erase_if(nativeConditions.discoveredHumans,[&](const auto& row){return !live.contains(row.first);});
    uint8_t humans=0;for(int house=0;house<numPlayers;++house)if(players[house].active && !players[house].isAI)humans|=(uint8_t)(1u<<house);
    const int count=(int)ents.size();
    for(EID id=0;id<count;++id) {
        if(!valid(id) || ents[id].nativeIdentity.object.empty() || !ents[id].nativeIdentity.definition)continue;
        const std::string key=ents[id].nativeIdentity.object;
        for(int house=0;house<numPlayers;++house) {
            if(!(humans&(1u<<house)) || !valid(id) || ents[id].nativeIdentity.object!=key)continue;
            const auto& e=ents[id];bool visible=false;
            const int width=e.isBuilding?bldDef(e).w:1,height=e.isBuilding?bldDef(e).h:1;
            for(int y=0;y<height && !visible;++y)for(int x=0;x<width;++x)
                if(map.fogAt(house,(int)e.x+x,(int)e.y+y)==FOG_VISIBLE){visible=true;break;}
            if(!visible)continue;
            uint8_t& mask=nativeConditions.discoveredHumans[key];const uint8_t bit=(uint8_t)(1u<<house);
            if(mask&bit)continue;
            const uint8_t strangers=mask&humans&(uint8_t)~(e.player>=0?(1u<<e.player):0u);
            mask|=bit;
            if(!seed && house!=e.player && !strangers)offerNativeTagEvent(e.nativeEventTag,4,id);
        }
    }
}

void World::offerNativeDamage(EID victim,EID attacker,bool lethal) {
    if(!nativeConditions.active || !valid(victim) || !valid(attacker))return;
    const std::string key=ents[victim].nativeIdentity.object;
    offerNativeTagEvent(ents[victim].nativeEventTag,6,victim);
    // The credited-kill path also offers Discovered before Destroyed.
    if(lethal && valid(victim) && ents[victim].nativeIdentity.object==key)
        offerNativeTagEvent(ents[victim].nativeEventTag,4,victim);
}

uint32_t World::nativeSelectionIdentity(EID actor) const {
    if(!valid(actor) || ents[actor].nativeIdentity.object.empty())return 0;
    uint32_t h=2166136261u;for(unsigned char c:ents[actor].nativeIdentity.object)h=(h^c)*16777619u;
    return h?h:1;
}

void World::selectNativeActor(int player,EID actor,uint32_t identity) {
    if(!nativeConditions.active || player<0 || player>=numPlayers || players[player].isAI || !valid(actor))return;
    const auto& e=ents[actor];
    if(e.player!=player || !e.nativeIdentity.definition || !e.nativeIdentity.definition->selectable
        || !identity || nativeSelectionIdentity(actor)!=identity)return;
    offerNativeTagEvent(e.nativeEventTag,33,actor);
}
