#pragma once
#include "game/world.h"

// Visit live actors and independently owned contents for a player in stored order.
// This is deliberately separate from save traversal, which includes dead slots.
template<class Visitor>
void visitLivePlayerContents(const World& world,int player,Visitor&& visitor) {
    const auto cargo=[&](auto&& self,const World::Ent::GarrisonedUnit& passenger,int containerOwner)->void {
        const int owner=World::passengerOwner(passenger,containerOwner);
        if(owner==player)visitor(passenger);
        for(const auto& child:passenger.cargo)self(self,child,owner);
    };
    for(const auto& actor:world.ents)if(actor.alive) {
        if(actor.player==player)visitor(actor);
        for(const auto& passenger:actor.cargo)cargo(cargo,passenger,actor.player);
        for(const auto& passenger:actor.garrison)cargo(cargo,passenger,actor.player);
    }
}

// Persistence and its ownership checksum include historical entity slots too.
// Callers validate the existing passenger depth/count bounds before encoding.
template<class WorldType,class Visitor>
void visitStoredPassengers(WorldType& world,Visitor&& visitor) {
    const auto cargo=[&](auto&& self,auto& passenger)->void {
        visitor(passenger);
        for(auto& child:passenger.cargo)self(self,child);
    };
    for(auto& actor:world.ents) {
        for(auto& passenger:actor.cargo)cargo(cargo,passenger);
        for(auto& passenger:actor.garrison)cargo(cargo,passenger);
    }
}
