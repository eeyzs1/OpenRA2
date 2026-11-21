#pragma once
#include "game/world.h"

// Visit live containers for a player, then cargo and garrison in their stored
// order. Stored passengers currently inherit their container's runtime player.
// This is deliberately separate from save traversal, which includes dead slots.
template<class Visitor>
void visitLivePlayerContents(const World& world,int player,Visitor&& visitor) {
    const auto cargo=[&](auto&& self,const World::Ent::GarrisonedUnit& passenger)->void {
        visitor(passenger);
        for(const auto& child:passenger.cargo)self(self,child);
    };
    for(const auto& actor:world.ents)if(actor.alive && actor.player==player) {
        visitor(actor);
        for(const auto& passenger:actor.cargo)cargo(cargo,passenger);
        for(const auto& passenger:actor.garrison)cargo(cargo,passenger);
    }
}
