#pragma once
#include "game/world.h"

namespace ActorStateTransfer {
namespace detail {
// Only persistent fields already carried by GarrisonedUnit belong here.
// Ownership, position, navigation and controller links stay with the caller.
template<class To,class From> void copy(To& to,const From& from) {
    to.hp=from.hp;
    to.kills=from.kills;
    to.veterancyValue=from.veterancyValue;
    to.vetRank=from.vetRank;
    to.combatActor=from.combatActor;
    to.oreLoad=from.oreLoad;
    to.gemLoad=from.gemLoad;
    to.ammo=from.ammo;
    to.atkCd=from.atkCd;
    to.invuln=from.invuln;
    to.crateDmgBoost=from.crateDmgBoost;
    to.crateArmorBoost=from.crateArmorBoost;
    to.crateSpeedBoost=from.crateSpeedBoost;
    to.airstrikeCd=from.airstrikeCd;
    to.confused=from.confused;
    to.gatlingHeat=from.gatlingHeat;
    to.gatlingStage=from.gatlingStage;
    to.autoHarvest=from.autoHarvest;
    to.permaControlled=from.permaControlled;
    to.cargo=from.cargo;
    to.nativeIdentity=from.nativeIdentity;
    to.nativeEventTag=from.nativeEventTag;
    to.nativeLastEnteredCell=from.nativeLastEnteredCell;
}
}
inline World::Ent::GarrisonedUnit capture(const World::Ent& actor) {
    World::Ent::GarrisonedUnit passenger{};
    passenger.type=actor.utype;
    detail::copy(passenger,actor);
    return passenger;
}
inline void restore(World::Ent& actor,const World::Ent::GarrisonedUnit& passenger) {
    detail::copy(actor,passenger);
}
}
