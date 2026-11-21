#include "game/world.h"
#include <algorithm>
#include <cmath>

bool World::hasNativeGuns(const Ent& actor) const {
    return actor.nativeIdentity.definition && actor.nativeIdentity.definition->guns.direct();
}
bool World::hasNativeGatling(const Ent& actor) const {
    return actor.nativeIdentity.definition && actor.nativeIdentity.definition->guns.staged();
}
void World::updateNativeGatling(Ent& e) {
    if(!hasNativeGatling(e) || e.immobilized())return;
    const auto& p=e.nativeIdentity.definition->guns;
    if(tick%2==0) {
        const int cap=p.thresholds[e.vetRank>=2?1:0][2];e.gatlingHeat=std::clamp(e.gatlingHeat,0,cap);
        bool firing=valid(e.target) && (e.isBuilding || e.state==UState::Attacking || e.state==UState::Chasing || e.state==UState::AttackMoving) && !e.selling && e.constructAnim<=0;
        if(firing && e.isBuilding)firing=e.player>=0 && !players[e.player].lowPower() && e.drainedBy==INVALID_EID;
        if(firing){const auto& target=ents[e.target];float x=target.x,y=target.y;
            if(target.isBuilding){const auto& d=bldDef(target);x+=d.w/2.f;y+=d.h/2.f;}
            const auto weapon=effWeapon(e,&target);float sx=e.x,sy=e.y;
            if(e.isBuilding){const auto& d=bldDef(e);sx+=d.w/2.f;sy+=d.h/2.f;}
            firing=isEnemy(e.player,target.player) && weapon.damage>0 && (target.airborne()?weapon.antiAir:weapon.antiGround)
                && distf(sx,sy,x,y)<=weapon.range;
        }
        e.gatlingHeat=firing?std::min(cap,e.gatlingHeat+p.rateUp):p.rateDown?std::max(0,e.gatlingHeat-p.rateDown):0;
    }
    e.gatlingStage=p.stage(e.gatlingHeat,e.vetRank);
}
bool World::hasNativeC4(const Ent& actor) const {
    return hasNativeGuns(actor) && actor.nativeIdentity.definition->guns.selector==2;
}
bool World::nativeWeapon(const Ent& actor,const Ent* target,WeaponDef& weapon) const {
    if(nativeIFVWeapon(actor,weapon))return true;
    if(!hasNativeGuns(actor))return false;
    const auto& profile=actor.nativeIdentity.definition->guns;
    const auto slot=[&](int base) {
        return actor.vetRank>=2 && profile.slots[base+2].present?base+2:base;
    };
    int selected=0;
    const auto& definition=*actor.nativeIdentity.definition;
    const bool aswPolicy=definition.sourceNavalTargeting && (actor.utype==UnitType::Destroyer || actor.utype==UnitType::ASW);
    if(aswPolicy && target && actor.utype==UnitType::ASW && !underwaterTarget(*target)) {
        weapon=WeaponDef{};weapon.damage=0;weapon.range=0;return true;
    }
    if(profile.staged())selected=std::clamp(actor.gatlingStage,0,2)*4+(target && target->airborne()?1:0);
    else if(aswPolicy && target && actor.utype==UnitType::Destroyer)selected=underwaterTarget(*target)?1:0;
    else if(profile.selector==1)selected=actor.deployed?1:0;
    else if(profile.selector==2)selected=target && (target->isBuilding || (!target->isBuilding
        && (unitDef(*target).isNaval() || unitDef(*target).isAmphib())))?1:0;
    else if(profile.selector==0 && target) {
        const int armor=(int)(target->nativeIdentity.definition?target->nativeIdentity.definition->armor:
            target->isBuilding?Armor::Concrete:unitDef(*target).armor);
        const float tx=target->x+(target->isBuilding?bldDef(*target).w*.5f:0),ty=target->y+(target->isBuilding?bldDef(*target).h*.5f:0);
        const float distance=distf(actor.x,actor.y,tx,ty);double best=-1;
        for(int base=0;base<2;++base) {
            const auto& gun=profile.slots[slot(base)];const auto& w=gun.weapon;
            if(!gun.present || w.range<0 || (target->airborne()?!w.antiAir:!w.antiGround))continue;
            const double score=gun.verses[armor]*(distance<=w.range?2:1);
            if(score>best){best=score;selected=base;}
        }
    }
    const auto& gun=profile.slots[slot(selected)];
    if(aswPolicy && actor.utype==UnitType::Destroyer && selected==1 && gun.present) {
        // ASWLauncher is a virtual spawner action. Preserve Range=-2 and AG=no
        // in the immutable source; no projectile is emitted from this weapon.
        weapon=gun.weapon;weapon.range=2.f*std::max(map.w,map.h);weapon.antiGround=true;weapon.antiAir=false;return true;
    }
    // Range=-2 is an original special-action sentinel (e.g. MakeupKit and
    // ASWLauncher), not a conventional projectile's negative firing radius.
    // Its source slot remains intact for the dedicated ability controller.
    if(!gun.present || gun.weapon.range<0){weapon=WeaponDef{};weapon.damage=0;weapon.range=0;return true;}
    weapon=gun.weapon;
    // Sapper is a sabotage action. Invisible5's AG=no prevents ordinary
    // projectile targeting; it does not prohibit the C4 ability on structures
    // or ships. Retain the raw projectile flags in the immutable slot.
    if(profile.selector==2 && selected==1){weapon.antiGround=true;weapon.antiAir=false;}
    scaleNativeWeapon(actor,profile,weapon);
    return true;
}
void World::scaleNativeWeapon(const Ent& actor,const NativeGunProfile& profile,WeaponDef& weapon) const {
    const int originalDamage=weapon.damage;
    if(actor.vetRank>0) {
        const int abilities=profile.abilities[actor.vetRank>=2?1:0];
        if(abilities&1)weapon.damage=(int)(weapon.damage*profile.veteranCombat);
        if(abilities&2)weapon.cooldown=std::max(1,(int)(weapon.cooldown*profile.veteranRof));
    }
    if(actor.crateDmgBoost>0)weapon.damage=(int)(weapon.damage*1.25f);
    const float crate=nativeCrateMultiplier(actor.nativeIdentity,11);
    if(crate!=1){double damage=(double)originalDamage*crate;
        if(actor.vetRank>0 && (profile.abilities[actor.vetRank>=2?1:0]&1))damage*=profile.veteranCombat;
        if(actor.crateDmgBoost>0)damage*=1.25;weapon.damage=(int)damage;}
}
float World::nativeArmorScale(const Ent& actor) const {
    if(!hasNativeGuns(actor) || actor.vetRank<=0)return 1;
    const auto& profile=actor.nativeIdentity.definition->guns;
    return profile.abilities[actor.vetRank>=2?1:0]&4?1/profile.veteranArmor:1;
}
void World::attackTargetPoint(const Ent& actor,const Ent& target,float& x,float& y) const {
    x=target.x;y=target.y;
    if(!target.isBuilding)return;
    const auto& b=bldDef(target);
    if(hasNativeC4(actor)) {
        // Sabotage must reach a foundation edge even for a large structure.
        x=std::clamp(actor.x,target.x+.5f,target.x+b.w-.5f);
        y=std::clamp(actor.y,target.y+.5f,target.y+b.h-.5f);
    } else {x+=b.w*.5f;y+=b.h*.5f;}
}
float World::attackRange(const Ent& actor,const Ent& target) const {
    if(!actor.isBuilding && unitDef(actor).hasC4() && target.isBuilding && !hasNativeGuns(actor))return 2.5f;
    return effWeapon(actor,&target).range;
}

int World::nativeWeaponReference(const WeaponDef& weapon) const {
    if(weapon.nativeType==-2 && nativeWeaponCatalog && weapon.nativeSlot>=0 && weapon.nativeSlot<(int)nativeWeaponCatalog->weapons.size()) {
        const auto& source=nativeWeaponCatalog->weapons[weapon.nativeSlot];
        return weapon.nativeSource==&source && weapon.nativeVerses==source.weapon.nativeVerses?2000000+weapon.nativeSlot:-2;
    }
    if(weapon.nativeType<0)return weapon.nativeType==-1 && !weapon.nativeVerses && weapon.nativeSlot==-1?-1:-2;
    if(weapon.nativeType>=(int)nativeTypes.size() || !weapon.nativeVerses)return -2;
    const auto& d=*nativeTypes[weapon.nativeType];
    if(weapon.nativeSlot>=0) {
        if(weapon.nativeSlot>=(d.guns.staged()?12:4) || !d.guns.direct() || !d.guns.slots[weapon.nativeSlot].present
            || weapon.nativeVerses!=d.guns.slots[weapon.nativeSlot].verses.data()
            || weapon.nativeSource!=d.guns.slots[weapon.nativeSlot].weapon.nativeSource)return -2;
        if(weapon.nativeSlot>=4)return 1000000+weapon.nativeType*12+weapon.nativeSlot;
        return weapon.nativeType*6+2+weapon.nativeSlot;
    }
    if(weapon.nativeSlot!=-1 || !d.hasNativeWeapon)return -2;
    if(weapon.nativeVerses==d.weaponVerses.data())return weapon.nativeType*6;
    if(!d.isBuilding && d.unit.elite && weapon.nativeVerses==d.eliteVerses.data())return weapon.nativeType*6+1;
    return -2;
}
bool World::restoreNativeWeaponReference(WeaponDef& weapon,int code,bool extended) const {
    weapon.nativeVerses=nullptr;weapon.nativeSource=nullptr;weapon.nativeType=weapon.nativeSlot=-1;
    if(code==-1)return true;const int stride=extended?6:2;
    const auto bind=[&](const WeaponDef& source) {
        weapon.nativeSource=source.nativeSource;
        if(source.nativeSource){weapon.projectile=source.projectile;weapon.report=source.report;}
    };
    if(extended && code>=2000000) {
        const int index=code-2000000;
        if(!nativeWeaponCatalog || index>=(int)nativeWeaponCatalog->weapons.size())return false;
        const auto& source=nativeWeaponCatalog->weapons[index].weapon;bind(source);
        weapon.nativeType=-2;weapon.nativeSlot=index;weapon.nativeVerses=source.nativeVerses;return true;
    }
    if(extended && code>=1000000){const int type=(code-1000000)/12,slot=(code-1000000)%12;
        if(type<0 || type>=(int)nativeTypes.size())return false;const auto& d=*nativeTypes[type];
        if(!d.guns.staged() || slot<4 || !d.guns.slots[slot].present)return false;
        weapon.nativeType=type;weapon.nativeSlot=slot;weapon.nativeVerses=d.guns.slots[slot].verses.data();bind(d.guns.slots[slot].weapon);return true;}
    if(code<0 || code>=(int)nativeTypes.size()*stride)return false;
    const int type=code/stride,slot=code%stride;const auto& d=*nativeTypes[type];
    if(slot<2) {
        if(!d.hasNativeWeapon || (slot==1 && (d.isBuilding || !d.unit.elite)))return false;
        weapon.nativeVerses=slot?d.eliteVerses.data():d.weaponVerses.data();
    } else {
        if(!d.guns.direct() || !d.guns.slots[slot-2].present)return false;
        weapon.nativeVerses=d.guns.slots[slot-2].verses.data();weapon.nativeSlot=slot-2;bind(d.guns.slots[slot-2].weapon);
    }
    weapon.nativeType=type;return true;
}
