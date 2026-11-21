#include "game/world.h"
#include <array>
#include <cmath>

namespace {
constexpr int PadCount=4;
Vec2f padPosition(const World::Ent& base,int slot) {
    const auto& d=bldDef(base);
    constexpr float offsets[PadCount][2]={{-.5f,-.5f},{.5f,-.5f},{-.5f,.5f},{.5f,.5f}};
    return {base.x+d.w/2.f+offsets[slot][0],base.y+d.h/2.f+offsets[slot][1]};
}
}

bool World::airfieldHasSpace(EID base,EID actor) const {
    if(!valid(base) || !ents[base].isBuilding || ents[base].selling || ents[base].constructAnim>0)return false;
    int assigned=0;
    for(EID id=0;id<(EID)ents.size();++id){const auto& e=ents[id];
        if(id!=actor && e.alive && !e.isBuilding && e.player==ents[base].player && e.airbase==base
            && unitDef(e).isAir() && unitDef(e).ammo>0 && ++assigned>=PadCount)return false;
    }
    return true;
}

Vec2f World::airPadPos(const Ent& base,EID actor) const {
    // Grounded aircraft keep their physical pads. Allocate the other claims in
    // entity order, including aircraft currently away on a mission. Everything
    // needed to reconstruct these claims is already in the World snapshot.
    std::array<EID,PadCount> claims;claims.fill(INVALID_EID);
    const auto belongs=[&](EID id){const auto& e=ents[id];return e.alive && !e.isBuilding && e.player==base.player
        && valid(e.airbase) && &ents[e.airbase]==&base && unitDef(e).isAir() && unitDef(e).ammo>0;};
    for(EID id=0;id<(EID)ents.size();++id)if(belongs(id) && ents[id].state==UState::Landed) {
        const auto& e=ents[id];
        for(int slot=0;slot<PadCount;++slot){const auto p=padPosition(base,slot);
            if(claims[slot]==INVALID_EID && std::abs(e.x-p.x)<.001f && std::abs(e.y-p.y)<.001f){claims[slot]=id;break;}}
    }
    for(EID id=0;id<(EID)ents.size();++id)if(belongs(id)) {
        bool assigned=false;for(EID claim:claims)assigned |= claim==id;if(assigned)continue;
        for(int offset=0;offset<PadCount;++offset){const int slot=(id+offset)&3;
            if(claims[slot]==INVALID_EID){claims[slot]=id;break;}}
    }
    for(int slot=0;slot<PadCount;++slot)if(claims[slot]==actor)return padPosition(base,slot);
    // Older saves can contain overbooked airfields. Their Returning controller
    // diverts excess aircraft before this query; retain a finite fallback.
    return padPosition(base,actor&3);
}
