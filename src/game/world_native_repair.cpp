#include "game/world.h"
#include "game/world_priv.h"
#include <algorithm>

bool World::nativeRepairMode(const Ent& actor) const {
    if(actor.isBuilding || !actor.nativeIdentity.definition || !actor.nativeIdentity.definition->weaponCatalog
        || actor.nativeIdentity.definition->weaponCatalog->version<3)return false;
    const auto weapon=effWeapon(actor);
    return weapon.nativeSource && weapon.damage<0;
}

bool World::nativeRepairIntent(const std::vector<EID>& actors,EID target) const {
    if(!valid(target))return false;
    for(const auto id:actors)if(valid(id) && id!=target && nativeRepairMode(ents[id])
        && ents[id].player>=0 && ents[target].player>=0 && isAllied(ents[id].player,ents[target].player))return true;
    return false;
}

AttackRejectReason World::nativeRepairRejection(EID actor,EID target,const WeaponDef& weapon) const {
    if(!valid(actor) || ents[actor].isBuilding || !valid(target) || actor==target || !weapon.nativeSource || weapon.damage>=0)return AttackRejectReason::InvalidTarget;
    const auto& source=ents[actor];const auto& patient=ents[target];
    if(source.player<0 || patient.player<0 || !isAllied(source.player,patient.player))return AttackRejectReason::InvalidTarget;
    if(patient.parasiting || patient.selling || patient.chrono>0 || patient.nativeTemporalHead!=INVALID_EID)return AttackRejectReason::Unsupported;
    if(patient.nativeIdentity.definition && !patient.nativeIdentity.definition->vehicleRepairable)return AttackRejectReason::Ineffective;
    if(!patient.nativeIdentity.definition && (patient.isBuilding || unitDef(patient).isInfantry()))return AttackRejectReason::Ineffective;
    if(patient.airborne()?!weapon.antiAir:!weapon.antiGround)return patient.airborne()?AttackRejectReason::NoAntiAir:AttackRejectReason::NoAntiGround;
    if(weaponMultiplier(weapon,patient)<=0)return AttackRejectReason::Ineffective;
    const int cap=patient.isBuilding?bldDef(patient).hp:maxHpFor(patient,unitDef(patient));
    if(patient.hp>=cap && !valid(patient.parasite))return AttackRejectReason::FullHealth;
    return AttackRejectReason::None;
}

void World::acquireNativeRepairTarget(Ent& actor,EID id) {
    if(actor.state!=UState::Idle || valid(actor.target) || !nativeRepairMode(actor))return;
    if(actor.nativeIdentity.definition && !actor.nativeIdentity.definition->canPassiveAcquire)return;
    const auto weapon=effWeapon(actor);EID best=INVALID_EID;float distance=weapon.range;
    const auto consider=[&](EID target) {
        if(nativeRepairRejection(id,target,weapon)!=AttackRejectReason::None)return;
        const auto& patient=ents[target];const float x=patient.x+(patient.isBuilding?bldDef(patient).w*.5f:0);
        const float y=patient.y+(patient.isBuilding?bldDef(patient).h*.5f:0),d=distf(actor.x,actor.y,x,y);
        if(d<distance || (d==distance && (best==INVALID_EID || target<best))){best=target;distance=d;}
    };
    const int radius=(int)std::ceil(weapon.range)+1;
    for(int y=std::max(0,(int)actor.y-radius);y<=std::min(map.h-1,(int)actor.y+radius);++y)
        for(int x=std::max(0,(int)actor.x-radius);x<=std::min(map.w-1,(int)actor.x+radius);++x) {
            const auto cell=cellIdx(x,y);
            for(EID target=unitOccHead[cell];target!=INVALID_EID;target=ents[target].occNext)consider(target);
            if(bldOcc[cell]>0)consider(bldOcc[cell]-1);
        }
    if(best!=INVALID_EID){actor.target=best;actor.state=UState::Attacking;}
}

bool World::applyNativeRepair(const WeaponDef& weapon,int player,EID target,EID source) {
    (void)player;
    if(!weapon.nativeSource || weapon.damage>=0)return false;
    if(!valid(source) || nativeRepairRejection(source,target,weapon)!=AttackRejectReason::None)return true;
    EntityMutationScope mutation(*this);const auto patient=entityRef(target);
    const EID parasite=ents[target].parasite;
    if(valid(parasite)){kill(parasite,false);if(!valid(patient))return true;}
    auto& actor=ents[target];const int cap=actor.isBuilding?bldDef(actor).hp:maxHpFor(actor,unitDef(actor));
    actor.hp=(int)std::min<int64_t>(cap,(int64_t)actor.hp-(int64_t)weapon.damage);
    Effect effect;effect.kind=5;effect.x=actor.x;effect.y=actor.y;effect.maxAge=6;effects.push_back(effect);
    return true;
}
