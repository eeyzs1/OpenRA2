#include "game/world.h"

bool World::hasNativeIFV(const Ent& actor) const {
    return !actor.isBuilding && actor.nativeIdentity.definition && actor.nativeIdentity.definition->ifv.gunner;
}

int World::nativeIFVMode(const Ent& actor) const {
    if(!hasNativeIFV(actor))return -1;
    if(actor.cargo.empty() || !actor.cargo.front().nativeIdentity.definition)return 0;
    return actor.cargo.front().nativeIdentity.definition->ifv.passengerMode;
}

int World::nativeIFVTurret(const Ent& actor) const {
    const int mode=nativeIFVMode(actor);if(mode<0)return -1;
    const auto& profile=actor.nativeIdentity.definition->ifv;
    return mode<(int)profile.modes.size()?profile.modes[mode].turret:-1;
}

bool World::nativeIFVWeapon(const Ent& actor,WeaponDef& weapon) const {
    const int mode=nativeIFVMode(actor);if(mode<0)return false;
    const auto& definition=*actor.nativeIdentity.definition;const auto& profile=definition.ifv;
    if(mode>=(int)profile.modes.size()){weapon={};weapon.damage=0;weapon.range=0;return true;}
    const int ref=profile.weapons[profile.modes[mode].weapon][actor.vetRank>=2?1:0];
    weapon=definition.weaponCatalog->weapons[ref].weapon;
    if(profile.version>=2) {
        weapon.burstDelayMin=profile.burstDelayMin*LOGIC_FPS/15;
        weapon.burstDelayMax=profile.burstDelayMax*LOGIC_FPS/15;
    }
    scaleNativeWeapon(actor,definition.guns,weapon);return true;
}

void World::nativeIFVCrewChanged(EID id) {
    if(!valid(id) || !hasNativeIFV(ents[id]))return;
    stopNativeTemporal(id);
    auto& actor=ents[id];actor.cancelBurst();
    if((actor.state==UState::Chasing || actor.state==UState::Attacking) && attackRejection(id,actor.target)!=AttackRejectReason::None) {
        actor.target=actor.feedbackAttackTarget=INVALID_EID;actor.clearPath();actor.wps.clear();actor.state=UState::Idle;
    }
}
