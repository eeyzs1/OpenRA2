#include "game/world.h"
#include "game/world_priv.h"
#include "game/weapon_sound.h"
#include "game/lang.h"
#include <algorithm>
#include <limits>
#include <utility>

uint64_t World::allocateCombatActor() {
    if(!nativeCrewRules.enabled)return 0;
    if(nextCombatActor>=(uint64_t)std::numeric_limits<int64_t>::max())return 0;
    return nextCombatActor++;
}

void World::initializeCrewIdentities(Ent& actor) {
    if(!nativeCrewRules.enabled)return;
    const auto initialize=[&](auto&& self,Ent::GarrisonedUnit& p)->void {
        if(!p.combatActor)p.combatActor=allocateCombatActor();
        for(auto& child:p.cargo)self(self,child);
    };
    for(auto& p:actor.cargo)initialize(initialize,p);
    for(auto& p:actor.garrison)initialize(initialize,p);
}

const World::Ent::GarrisonedUnit* World::findCrewPassenger(EID carrier,uint64_t identity) const {
    if(!identity || !valid(carrier))return nullptr;
    const auto& e=ents[carrier];const auto& passengers=e.isBuilding?e.garrison:e.cargo;
    for(const auto& p:passengers)if(p.combatActor==identity)return &p;
    return nullptr; // nested cargo does not fire through two containers
}
World::Ent::GarrisonedUnit* World::findCrewPassenger(EID carrier,uint64_t identity) {
    return const_cast<Ent::GarrisonedUnit*>(std::as_const(*this).findCrewPassenger(carrier,identity));
}

World::Ent World::passengerActor(const Ent& carrier,const Ent::GarrisonedUnit& p) const {
    Ent actor;actor.alive=true;actor.player=carrier.player;actor.utype=p.type;actor.nativeIdentity=p.nativeIdentity;
    actor.x=carrier.x;actor.y=carrier.y;actor.hp=p.hp;actor.vetRank=p.vetRank;actor.kills=p.kills;
    actor.combatActor=p.combatActor;actor.crateDmgBoost=p.crateDmgBoost;
    actor.gatlingHeat=p.gatlingHeat;actor.gatlingStage=p.gatlingStage;
    if(carrier.isBuilding){actor.x+=bldDef(carrier).w*.5f;actor.y+=bldDef(carrier).h*.5f;}
    return actor;
}

bool World::nativeCrewFires(const Ent& carrier) const {
    if(!nativeCrewRules.enabled || !carrier.nativeIdentity.definition)return false;
    const auto& d=*carrier.nativeIdentity.definition;
    if(!carrier.isBuilding)return d.crew.openTopped;
    return buildingEntryDomain(carrier)==1 && !d.production.infantryAbsorb && !d.production.unitAbsorb;
}

WeaponDef World::nativeCrewWeapon(const Ent& carrier,const Ent::GarrisonedUnit& passenger,const Ent* target) const {
    WeaponDef weapon;weapon.damage=0;weapon.range=0;
    if(!nativeCrewFires(carrier) || !passenger.nativeIdentity.definition)return weapon;
    const auto actor=passengerActor(carrier,passenger);
    if(carrier.isBuilding && !nativeCrewRules.passengerOccupy) {
        const auto& guns=carrier.nativeIdentity.definition->guns;
        const int slot=carrier.player>=0 && players[carrier.player].faction==Faction::Soviet?1:0;
        if(!guns.slots[slot].present)return weapon;
        weapon=guns.slots[slot].weapon; // original RA2 AlliedOccupyW/SovietOccupyW
    } else if(carrier.isBuilding) {
        const auto& profile=passenger.nativeIdentity.definition->crew;
        const int reference=profile.occupyWeapons[passenger.vetRank>=2?1:0];
        if(!profile.occupier || !nativeWeaponCatalog || reference<0 || reference>=(int)nativeWeaponCatalog->weapons.size())return weapon;
        weapon=nativeWeaponCatalog->weapons[reference].weapon;
        scaleNativeWeapon(actor,passenger.nativeIdentity.definition->guns,weapon);
        weapon.damage=(int)(weapon.damage*nativeCrewRules.occupyDamage);
        weapon.cooldown=std::max(1,(int)(weapon.cooldown*nativeCrewRules.occupyROF));
        weapon.range=(float)nativeCrewRules.occupyRange;
    } else {
        const auto& profile=passenger.nativeIdentity.definition->crew;
        if(nativeCrewRules.version>=2 && profile.transportWeapon>=0) {
            const int reference=profile.transportWeapons[passenger.vetRank>=2?1:0];
            // An explicit empty slot cannot fall back to a target-dependent
            // weapon (notably C4), or imply that the infantry has deployed.
            if(!nativeWeaponCatalog || reference<0 || reference>=(int)nativeWeaponCatalog->weapons.size())return weapon;
            const auto& selected=nativeWeaponCatalog->weapons[reference].weapon;
            if(selected.range<0)return weapon;
            weapon=selected;scaleNativeWeapon(actor,passenger.nativeIdentity.definition->guns,weapon);
        } else weapon=effWeapon(actor,target);
        weapon.range+=nativeCrewRules.openRange;
        weapon.damage=(int)(weapon.damage*nativeCrewRules.openDamage);
    }
    return weapon;
}

void World::updateNativeCrew(EID carrier) {
    if(!valid(carrier))return;
    auto& e=ents[carrier];
    if(!nativeCrewFires(e) || e.player<0 || e.immobilized() || e.selling || e.constructAnim>0)return;
    initializeCrewIdentities(e);
    auto& passengers=e.isBuilding?e.garrison:e.cargo;
    // Store only identities across a shot: callbacks may unload, kill or move a
    // passenger and may reallocate the passenger vector.
    std::vector<uint64_t> identities;for(const auto& p:passengers)identities.push_back(p.combatActor);
    const auto owner=entityRef(carrier);
    const auto shoot=[&](uint64_t identity,const Ent& actor,EID target,const WeaponDef& weapon) {
        Effect muzzle;muzzle.kind=5;muzzle.x=actor.x;muzzle.y=actor.y;muzzle.maxAge=4;effects.push_back(muzzle);
        emitSoundAt(weaponSoundCue(weapon),actor.x,actor.y,carrier);
        emitWeaponProjectile(actor.player,actor.x,actor.y,target,carrier,-1,weapon,identity);
    };
    for(uint64_t identity:identities) {
        if(!valid(owner))break;
        auto* p=findCrewPassenger(carrier,identity);if(!p)continue;
        auto burst=nativeCrewBursts.find(identity);
        if(burst!=nativeCrewBursts.end()) {
            auto& state=burst->second;
            bool validShot=state.carrier==owner && valid(state.target) && state.player==e.player;
            WeaponDef weapon;
            if(validShot) {
                const auto& t=ents[state.target.id];weapon=nativeCrewWeapon(e,*p,&t);
                const auto actor=passengerActor(e,*p);
                float sx=actor.x,sy=actor.y;
                const float tx=t.x+(t.isBuilding?bldDef(t).w*.5f:0),ty=t.y+(t.isBuilding?bldDef(t).h*.5f:0);
                if(e.isBuilding && nativeCrewRules.passengerOccupy){sx=std::clamp(tx,e.x,e.x+bldDef(e).w);sy=std::clamp(ty,e.y,e.y+bldDef(e).h);}
                validShot=isEnemy(e.player,t.player) && t.airborne()==state.air && weaponMultiplier(weapon,t)>0
                    && (t.airborne()?weapon.antiAir:weapon.antiGround) && distf(sx,sy,tx,ty)<=weapon.range;
            }
            if(!validShot){nativeCrewBursts.erase(burst);continue;}
            if(p->confused>0)continue;
            if(state.delay>0)--state.delay;if(state.delay>0)continue;
            const EID target=state.target.id;
            if(--state.remaining>0)state.delay=rng.range(weapon.burstDelayMin,weapon.burstDelayMax);
            else nativeCrewBursts.erase(burst);
            p->atkCd=std::max(2,p->atkCd);const auto actor=passengerActor(e,*p);
            shoot(identity,actor,target,weapon);
            continue;
        }
        if(p->atkCd>0 || p->confused>0)continue;
        EID target=INVALID_EID;WeaponDef selected;float best=std::numeric_limits<float>::max();
        const auto actor=passengerActor(ents[carrier],*p);
        for(EID id:nativeCrewCandidates(e,*p)) {
            if(!valid(id))continue;const auto& t=ents[id];
            if(!t.alive || t.player<0 || !isEnemy(e.player,t.player) || t.parasiting)continue;
            auto w=nativeCrewWeapon(ents[carrier],*p,&t);
            if(w.damage<=0 || (t.airborne()?!w.antiAir:!w.antiGround) || weaponMultiplier(w,t)<=0)continue;
            if(nativeTemporalWeapon(w) && !nativeTemporalCanTarget(carrier,id))continue;
            if(!t.isBuilding) {
                if(t.camouflaged && t.utype!=UnitType::Spy)continue;
                if(t.utype==UnitType::Spy && p->type!=UnitType::AttackDog)continue;
                if((t.utype==UnitType::Typhoon || t.utype==UnitType::Boomer) && t.subReveal<=0) {
                    const float distance=distf(actor.x,actor.y,t.x,t.y);
                    if(!(isDetector(p->type) && distance<=7.f) && distance>2.5f)continue;
                }
            }
            const float tx=t.x+(t.isBuilding?bldDef(t).w*.5f:0),ty=t.y+(t.isBuilding?bldDef(t).h*.5f:0);
            float sx=actor.x,sy=actor.y;
            if(e.isBuilding && nativeCrewRules.passengerOccupy){sx=std::clamp(tx,e.x,e.x+bldDef(e).w);sy=std::clamp(ty,e.y,e.y+bldDef(e).h);}
            const float distance=distf(sx,sy,tx,ty);
            if(distance>w.range || (target!=INVALID_EID && id!=e.target && distance>=best))continue;
            target=id;selected=w;best=distance;if(id==e.target)break;
        }
        if(target==INVALID_EID)continue;
        // The cooldown belongs to this passenger, including after unloading.
        p->atkCd=std::max(1,selected.cooldown);
        if(startNativeCrewTemporal(carrier,identity,target,selected))continue;
        if(selected.burst>1)nativeCrewBursts[identity]={owner,entityRef(target),selected.burst-1,
            rng.range(selected.burstDelayMin,selected.burstDelayMax),e.player,ents[target].airborne()};
        shoot(identity,actor,target,selected);
    }
}

void World::creditCombatKill(uint64_t source,EID victim) {
    if(!source || !valid(victim))return;
    for(EID id=0;id<(EID)ents.size();++id) {
        auto& actor=ents[id];if(!actor.alive)continue;
        if(actor.combatActor==source) {
            if(!actor.nativeIdentity.definition || actor.nativeIdentity.definition->trainable)creditKill(id,victim);
            return;
        }
        const auto promote=[&](auto&& self,Ent::GarrisonedUnit& p)->bool {
            if(p.combatActor==source) {
                if(p.nativeIdentity.definition && !p.nativeIdentity.definition->trainable)return true;
                const auto& v=ents[victim];const int value=v.isBuilding?bldDef(v).cost:unitDef(v).cost;
                if(value<=0)return true;
                ++p.kills;p.veterancyValue=(int)std::min<int64_t>(INT_MAX,(int64_t)p.veterancyValue+value);
                const int cost=std::max(1,unitDef(p).cost);
                while(p.vetRank<2 && p.veterancyValue>=std::ceil(cost*g_gameRules.veteranRatio*(p.vetRank+1))) {
                    ++p.vetRank;
                    if(actor.player>=0)eva(actor.player,formatText(TR(p.vetRank==1?S::EvaPromoteVetFmt:S::EvaPromoteEliteFmt),unitName(p.type)),EvaCue::UnitPromoted);
                }
                return true;
            }
            for(auto& child:p.cargo)if(self(self,child))return true;
            return false;
        };
        for(auto& p:actor.cargo)if(promote(promote,p))return;
        for(auto& p:actor.garrison)if(promote(promote,p))return;
    }
}

void World::retireCombatIdentity(EID id) {
    if(id<0 || id>=(EID)ents.size())return;
    std::erase_if(nativeCrewBursts,[&](const auto& node){return node.second.carrier.id==id || node.second.target.id==id;});
    auto& e=ents[id];e.combatActor=0;
    const auto clear=[&](auto&& self,Ent::GarrisonedUnit& p)->void {p.combatActor=0;for(auto& child:p.cargo)self(self,child);};
    for(auto& p:e.cargo)clear(clear,p);for(auto& p:e.garrison)clear(clear,p);
}
void World::consumeCrewPassenger(EID carrier,uint64_t identity) {
    if(!valid(carrier) || !identity)return;
    stopNativeTemporal(nativeCrewTemporalKey(identity));nativeCrewBursts.erase(identity);
    auto& e=ents[carrier];auto& passengers=e.isBuilding?e.garrison:e.cargo;
    std::erase_if(passengers,[&](const auto& p){return p.combatActor==identity;});
    if(e.isBuilding)recomputePower();
}
