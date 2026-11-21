#include "game/world.h"
#include <algorithm>
#include <cmath>
#include <limits>

int World::nativeProductionCost(int player,int type) const {
    if(player<0 || player>=numPlayers || type<0 || type>=(int)nativeTypes.size())return 0;
    const auto& product=*nativeTypes[type];
    const int base=product.isBuilding?product.building.cost:product.unit.cost;
    if(!nativeProductionRules.economy || !base)return base;
    const int category=product.production.category;
    const int bonus=category==3?1:category>=4?category-1:category;
    double price=base;
    for(const auto& actor:ents) {
        if(actor.player!=player || !productionFacilityOperational(actor) || !actor.nativeIdentity.definition)continue;
        const auto& plant=actor.nativeIdentity.definition->production;
        if(!plant.factoryPlant || (plant.powered && players[player].lowPower()))continue;
        if(plant.costBonus[bonus]==0)return 0;
        price*=plant.costBonus[bonus];
    }
    // Orders freeze the quote. Later power/ownership changes affect new orders,
    // never rewrite money already paid or its exact cancellation refund.
    return (int)std::clamp(std::floor(price),0.,(double)std::numeric_limits<int>::max());
}

Credits World::grindingValue(const Ent& unit) const {
    if(nativeProductionRules.version>=3 && unit.nativeIdentity.definition) {
        const auto& profile=unit.nativeIdentity.definition->production;
        return profile.soylent>=0?profile.soylent:nativeProductionCost(unit.player,unit.nativeIdentity.type);
    }
    return (Credits)(unitDef(unit).cost*g_gameRules.grinderRefund);
}
