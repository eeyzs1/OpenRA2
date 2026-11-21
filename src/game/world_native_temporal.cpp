#include "game/world.h"
#include <algorithm>
#include <cmath>

bool World::nativeTemporalWeapon(const WeaponDef& weapon) const {
    return nativeTemporalEnabled && weapon.nativeSource && weapon.nativeSource->warhead
        && weapon.nativeSource->warhead->temporal;
}

bool World::nativeTemporalCanTarget(EID source,EID target) const {
    if(!nativeTemporalEnabled || !valid(source) || !valid(target) || source==target)return false;
    const auto& victim=ents[target];
    return ents[source].nativeTemporalHead==INVALID_EID && victim.invuln<=0 && !victim.parasiting
        && victim.nativeIdentity.definition && victim.nativeIdentity.definition->warpable;
}

int World::nativeTemporalDamage(int64_t source) const {
    auto found=nativeTemporalBeams.find(source);
    if(found!=nativeTemporalBeams.end() && found->second.passenger) {
        const auto& beam=found->second;
        const auto* passenger=findCrewPassenger(beam.owner.id,beam.passenger);
        if(!valid(beam.owner) || !passenger)return 0;
        const auto weapon=nativeCrewWeapon(ents[beam.owner.id],*passenger);
        return nativeTemporalWeapon(weapon)?std::max(0,weapon.nativeSource->weapon.damage):0;
    }
    if(source<0 || source>INT_MAX || !valid((EID)source))return 0;
    const auto weapon=effWeapon(ents[(EID)source]);
    return nativeTemporalWeapon(weapon)?std::max(0,weapon.nativeSource->weapon.damage):0;
}

bool World::startNativeTemporal(EID source,EID target,const WeaponDef& weapon) {
    return bindNativeTemporal(source,source,0,target,weapon);
}
bool World::startNativeCrewTemporal(EID carrier,uint64_t passenger,EID target,const WeaponDef& weapon) {
    if(!nativeTemporalWeapon(weapon))return false;
    if(!nativeCrewRules.enabled || !passenger || !findCrewPassenger(carrier,passenger))return true;
    return bindNativeTemporal(nativeCrewTemporalKey(passenger),carrier,passenger,target,weapon);
}
bool World::bindNativeTemporal(int64_t source,EID carrier,uint64_t passenger,EID target,const WeaponDef& weapon) {
    if(!nativeTemporalWeapon(weapon))return false;
    auto previous=nativeTemporalBeams.find(source);
    if(previous!=nativeTemporalBeams.end() && valid(previous->second.owner)
        && previous->second.target==entityRef(target) && valid(previous->second.target))return true;
    stopNativeTemporal(source);
    if(!nativeTemporalCanTarget(carrier,target))return true;
    auto& victim=ents[target];
    NativeTemporalBeam beam;beam.owner=entityRef(carrier);beam.passenger=passenger;beam.target=entityRef(target);
    const int64_t head=victim.nativeTemporalHead;
    if(head==INVALID_EID) {
        const int strength=victim.isBuilding?bldDef(victim).hp:unitDef(victim).hp;
        beam.remaining=(int64_t)strength*10;victim.nativeTemporalHead=source;
    } else {
        auto found=nativeTemporalBeams.find(head);if(found==nativeTemporalBeams.end())return true;
        beam.next=head;beam.prev=found->second.prev;found->second.prev=source;
        if(beam.prev!=INVALID_EID)nativeTemporalBeams.at(beam.prev).next=source;
    }
    nativeTemporalBeams.emplace(source,beam);
    if(!ents[carrier].isBuilding)ents[carrier].fireAnim=g_gameRules.fireAnimationTicks;
    emitSoundAt(Sfx::Tesla,ents[carrier].x,ents[carrier].y,carrier);
    stopNativeTemporal(target);stopNativeCrewTemporal(target);
    if(head==INVALID_EID && victim.isBuilding)recomputePower();return true;
}

void World::stopNativeTemporal(int64_t source) {
    auto found=nativeTemporalBeams.find(source);
    if(found==nativeTemporalBeams.end())return;
    const auto beam=found->second;
    const bool targetSlot=beam.target.id>=0 && beam.target.id<(EID)ents.size()
        && ents[beam.target.id].generation==beam.target.generation;
    if(beam.next==INVALID_EID) {
        if(targetSlot)ents[beam.target.id].nativeTemporalHead=beam.prev;
        if(beam.prev!=INVALID_EID) {
            auto promoted=nativeTemporalBeams.find(beam.prev);
            if(promoted!=nativeTemporalBeams.end()) {
                promoted->second.next=INVALID_EID;promoted->second.remaining=beam.remaining;
            }
        }
    } else {
        auto next=nativeTemporalBeams.find(beam.next);
        if(next!=nativeTemporalBeams.end())next->second.prev=beam.prev;
        auto prev=nativeTemporalBeams.find(beam.prev);
        if(prev!=nativeTemporalBeams.end())prev->second.next=beam.next;
    }
    nativeTemporalBeams.erase(found);
    if(targetSlot && beam.next==INVALID_EID && beam.prev==INVALID_EID
        && ents[beam.target.id].alive && ents[beam.target.id].isBuilding)recomputePower();
}

void World::retireNativeTemporalActor(EID actor) {
    stopNativeTemporal(actor);stopNativeCrewTemporal(actor);
    if(actor<0 || actor>=(EID)ents.size())return;
    while(ents[actor].nativeTemporalHead!=INVALID_EID) {
        const int64_t head=ents[actor].nativeTemporalHead;
        if(!nativeTemporalBeams.contains(head)){ents[actor].nativeTemporalHead=INVALID_EID;break;}
        stopNativeTemporal(head);
    }
}

void World::stopNativeCrewTemporal(EID carrier) {
    std::vector<int64_t> sources;
    for(const auto& [key,beam]:nativeTemporalBeams)if(beam.passenger && beam.owner.id==carrier)sources.push_back(key);
    for(auto source:sources)stopNativeTemporal(source);
}

void World::updateNativeTemporalSource(EID source) {
    std::erase_if(nativeCrewBursts,[&](const auto& node){const auto& b=node.second;
        return b.carrier.id==source && (!valid(b.carrier) || !valid(b.target) || !findCrewPassenger(source,node.first)
            || ents[source].player!=b.player);});
    if(nativeTemporalBeams.empty())return;
    std::vector<int64_t> sources;
    if(nativeTemporalBeams.contains(source))sources.push_back(source);
    if(nativeCrewRules.enabled)for(const auto& [key,beam]:nativeTemporalBeams)
        if(beam.passenger && beam.owner.id==source)sources.push_back(key);
    for(int64_t key:sources) {
        auto found=nativeTemporalBeams.find(key);if(found==nativeTemporalBeams.end())continue;
        const auto beam=found->second;
        if(!valid(beam.owner) || !valid(beam.target)){stopNativeTemporal(key);continue;}
        const auto& owner=ents[source];const auto& target=ents[beam.target.id];
        float sx=owner.x,sy=owner.y,tx=target.x,ty=target.y;
        if(owner.isBuilding){sx+=bldDef(owner).w*.5f;sy+=bldDef(owner).h*.5f;}
        float range=0;bool firing=true;
        if(beam.passenger) {
            firing=nativeCrewFires(owner) && findCrewPassenger(source,beam.passenger);
            range=(float)(owner.isBuilding?nativeCrewRules.occupyRange:nativeCrewRules.warpDistance);
            tx+=target.isBuilding?bldDef(target).w*.5f:0;ty+=target.isBuilding?bldDef(target).h*.5f:0;
        } else {
            attackTargetPoint(owner,target,tx,ty);range=attackRange(owner,target);
            firing=owner.target==beam.target.id && (owner.isBuilding || owner.state==UState::Attacking);
        }
        if(!firing || owner.immobilized() || owner.selling || owner.constructAnim>0
            || (owner.player>=0 && target.player>=0 && !isEnemy(owner.player,target.player))
            || nativeTemporalDamage(key)<=0 || distf(sx,sy,tx,ty)>range)stopNativeTemporal(key);
    }
}

void World::updateNativeTemporalTarget(EID target) {
    if(!valid(target) || ents[target].nativeTemporalHead==INVALID_EID || tick%2!=0)return;
    const int64_t head=ents[target].nativeTemporalHead;
    auto found=nativeTemporalBeams.find(head);
    if(found==nativeTemporalBeams.end())return;
    int64_t amount=0,cursor=head;
    // RA2 0x6e247e compares the recursive argument with 50 using JG, then
    // recurses before adding this helper. Isolated original bytes confirm a
    // head plus 51 helpers; this is deliberately not a guessed 50-node cap.
    for(int count=0;cursor!=INVALID_EID && count<=51;++count) {
        auto node=nativeTemporalBeams.find(cursor);if(node==nativeTemporalBeams.end())break;
        amount+=nativeTemporalDamage(cursor);cursor=node->second.prev;
    }
    found->second.remaining-=amount;
    if(found->second.remaining>0)return;
    const auto owner=found->second.owner;const uint64_t passenger=found->second.passenger;
    EntityMutationScope mutation(*this);
    if(passenger)creditCombatKill(passenger,target);
    else if(valid(owner) && (!ents[owner.id].nativeIdentity.definition || ents[owner.id].nativeIdentity.definition->trainable))creditKill(owner.id,target);
    Effect erase;erase.kind=9;erase.aux=(int)ChronoVisual::Erase;erase.x=ents[target].x;erase.y=ents[target].y;erase.maxAge=26;
    if(ents[target].isBuilding){erase.x+=bldDef(ents[target]).w*.5f;erase.y+=bldDef(ents[target]).h*.5f;}
    effects.push_back(erase);
    // The original registers destruction and uninitializes the victim. It does
    // not deliver a lethal ReceiveDamage or trigger an ordinary physical death.
    kill(target,false,false,true);
}

bool World::nativeTemporalAbilityOnline(int player,uint32_t mask,bool support) const {
    if(!nativeTemporalEnabled || nativeTemporalBeams.empty())return true;
    for(const auto& actor:ents) {
        if(!actor.alive || !actor.isBuilding || actor.player!=player || actor.selling || actor.constructAnim>0
            || actor.nativeTemporalHead!=INVALID_EID || !actor.nativeIdentity.definition)continue;
        const auto& production=actor.nativeIdentity.definition->production;
        if((support?production.supportMask:production.superweaponMask)&mask)return true;
    }
    return false;
}

std::vector<Effect> World::nativeTemporalVisuals() const {
    std::vector<Effect> result;result.reserve(nativeTemporalBeams.size());
    for(const auto& [source,beam]:nativeTemporalBeams) {
        if(!valid(beam.owner) || !valid(beam.target))continue;
        const auto& owner=ents[source];const auto& target=ents[beam.target.id];
        Effect effect;effect.kind=3;effect.maxAge=1;effect.x=owner.x;effect.y=owner.y;effect.x2=target.x;effect.y2=target.y;
        if(owner.isBuilding){effect.x+=bldDef(owner).w*.5f;effect.y+=bldDef(owner).h*.5f;}
        if(target.isBuilding){effect.x2+=bldDef(target).w*.5f;effect.y2+=bldDef(target).h*.5f;}
        result.push_back(effect);
    }
    return result;
}
