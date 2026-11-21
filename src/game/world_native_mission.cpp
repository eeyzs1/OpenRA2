#include "game/world.h"
#include <algorithm>
#include <charconv>

namespace {
enum class OwnedKind { None,Unit,Building };
OwnedKind mobileKind(const NativeActorIdentity& identity,UnitType type) {
    const auto& d=identity.definition;
    if(d && (!d->significant || d->kind))return OwnedKind::None;
    const auto& u=d?d->unit:unitDef(type);
    // Rocketeer is an InfantryType even though its movement is airborne.
    return u.isAir() && type!=UnitType::Rocketeer?OwnedKind::None:OwnedKind::Unit;
}
OwnedKind buildingKind(const NativeActorDefinition* d,BldType type) {
    if(d && (!d->significant || d->kind))return OwnedKind::None;
    if(!d && type==BldType::Wall)return OwnedKind::None;
    if(d && d->targeting.undeploys && type!=BldType::ConYard)return OwnedKind::Unit;
    return OwnedKind::Building;
}
bool sourceHouse(const std::string& text,int& house) {
    const auto r=std::from_chars(text.data(),text.data()+text.size(),house);
    return r.ec==std::errc{} && r.ptr==text.data()+text.size();
}
}

bool World::nativeHouseEliminated(int originalHouse,int event) const {
    if(originalHouse<0 || originalHouse>1023 || (event!=9 && event!=10 && event!=11))return false;
    const int house=nativeHouseNumber(originalHouse);if(house<0)return true;
    const auto counts=[&](OwnedKind kind){return kind!=OwnedKind::None && (event==11 || (event==9)==(kind==OwnedKind::Unit));};
    const auto passenger=[&](auto&& self,const Ent::GarrisonedUnit& p)->bool {
        if(counts(mobileKind(p.nativeIdentity,p.type)))return true;
        for(const auto& child:p.cargo)if(self(self,child))return true;return false;
    };
    for(const auto& e:ents)if(e.alive && e.player==house) {
        const auto kind=e.isBuilding?buildingKind(e.nativeIdentity.definition.get(),e.btype):mobileKind(e.nativeIdentity,e.utype);
        if(counts(kind))return false;
        for(const auto& p:e.cargo)if(passenger(passenger,p))return false;
        for(const auto& p:e.garrison)if(passenger(passenger,p))return false;
    }
    // Production creates an owned object before it can be placed/released.
    // Unstarted queue entries do not create another object yet.
    const auto& p=players[house];
    for(const auto& job:nativeAI.jobs)if(job.house==house && job.type>=0 && job.type<(int)nativeTypes.size()){
        NativeActorIdentity identity;identity.definition=nativeTypes[job.type];if(counts(mobileKind(identity,identity.definition->unit.type)))return false;}
    for(const auto& item:p.unitProd)if(item.active && item.isUnit) {
        if(nativeProductionType(item.typeIdx)){NativeActorIdentity identity;identity.definition=nativeTypes[item.typeIdx-NATIVE_PRODUCTION_BASE];
            if(counts(mobileKind(identity,identity.definition->unit.type)))return false;}
        else if(item.typeIdx>=0 && item.typeIdx<(int)UnitType::COUNT && counts(mobileKind({},(UnitType)item.typeIdx)))return false;
    }
    for(const auto* item:{&p.bldProd,&p.defProd})if(item->active && !item->isUnit) {
        if(nativeProductionType(item->typeIdx)){const auto& d=nativeTypes[item->typeIdx-NATIVE_PRODUCTION_BASE];if(counts(buildingKind(d.get(),d->building.type)))return false;continue;}
        if(item->typeIdx<0 || item->typeIdx>=(int)BldType::COUNT)continue;
        const auto type=(BldType)item->typeIdx;const NativeActorDefinition* definition=nullptr;
        for(const auto& d:nativeTypes)if(d->isBuilding && !d->kind && d->building.type==type){definition=d.get();break;}
        if(counts(buildingKind(definition,type)))return false;
    }
    return true;
}

bool World::concludeNativeMission(bool win,int runtime) {
    if(!nativeEventDefinition || !nativeEvents.active || runtime<0 || runtime>=(int)nativeEvents.triggers.size())return false;
    // A native mission ends on its first authored terminal action. Do not
    // turn a later failure timer into a victory after the mission has ended.
    if(nativeEvents.outcome==NativeMissionOutcome::Running) {
        nativeEvents.outcome=win?NativeMissionOutcome::Victory:NativeMissionOutcome::Defeat;
        nativeEvents.outcomeAt=tick;nativeEvents.outcomeTrigger=nativeEvents.triggers[runtime].definition;
    }
    return true;
}

MatchOutcome World::nativeMissionOutcome(int player) const {
    if(!nativeEvents.active || !nativeEventDefinition || player!=nativeEventDefinition->playerHouse)return MatchOutcome::Running;
    if(nativeEvents.outcome==NativeMissionOutcome::Victory)return MatchOutcome::Victory;
    if(nativeEvents.outcome==NativeMissionOutcome::Defeat)return MatchOutcome::Defeat;
    return MatchOutcome::Running;
}

bool World::validateNativeMissionOutcome() const {
    if((uint8_t)nativeEvents.outcome>2)return false;
    if(nativeEvents.outcome==NativeMissionOutcome::Running)return nativeEvents.outcomeAt==0 && nativeEvents.outcomeTrigger==-1;
    if(!nativeEvents.active || !nativeEventDefinition || nativeEvents.outcomeAt>tick || nativeEvents.outcomeTrigger<0
        || nativeEvents.outcomeTrigger>=(int)nativeEventDefinition->triggers.size())return false;
    const auto& source=nativeEventDefinition->triggers[nativeEvents.outcomeTrigger];
    const int action=nativeEvents.outcome==NativeMissionOutcome::Victory?1:2;bool authored=false;
    for(const auto& a:source.actions)if(a.type==action) {
        int house=-1;authored |= sourceHouse(a.args[1],house) && nativeHouseNumber(house)==nativeEventDefinition->playerHouse;
    }
    if(!authored)return false;
    return std::any_of(nativeEvents.triggers.begin(),nativeEvents.triggers.end(),[&](const auto& t){
        return t.definition==nativeEvents.outcomeTrigger && t.firings>0;
    });
}
