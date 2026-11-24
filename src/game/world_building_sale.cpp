#include "game/world.h"
#include "game/native_scene_contract.h"
#include <algorithm>
#include <array>
#include <cmath>

int World::nativeSaleHouseSide(int player) const {
    if(player<0 || player>=numPlayers)return -1;
    return players[player].faction==Faction::Allies?0:players[player].faction==Faction::Soviet?1:2;
}

bool World::nativeSaleBuilding(const Ent& building) const {
    const auto& d=building.nativeIdentity.definition;
    return nativeSaleSurvivorRules.enabled && building.isBuilding && d && d->rulesOnly && !d->kind;
}

bool World::validateNativeBuildingSales() const {
    if(nativeSaleSurvivorRules.enabled!=(nativeRulesBaseVersion>=nativeSaleSurvivorsSceneVersion))return false;
    for(const auto& e:ents) {
        if(!e.alive)continue;
        const auto& state=e.nativeBuildingSale;
        if(nativeSaleBuilding(e)) {
            if(state.originalSide< -1 || state.originalSide>2 || (state.pending && !e.selling))return false;
        } else if(state.originalSide!=-2 || state.captured || state.pending)return false;
    }
    return true;
}

int World::nativeBuildingSurvivorCount(const Ent& building) const {
    if(!nativeSaleBuilding(building) || !building.nativeIdentity.definition->crewed)return 0;
    const auto& state=building.nativeBuildingSale;
    int divisor=state.originalSide==1?nativeSaleSurvivorRules.sovietDivisor:nativeSaleSurvivorRules.alliedDivisor;
    if(!divisor)return 0;
    if(state.captured)divisor*=2;
    return std::clamp((int)(nativeBuildingSaleValue(building)/divisor),1,5);
}

int World::nativeBuildingSurvivorType(const Ent& building) {
    const auto& rule=nativeSaleSurvivorRules;const auto& d=*building.nativeIdentity.definition;
    // The original consumes this draw for every uncaptured building, even
    // when its factory is not BuildingType.
    if(!building.nativeBuildingSale.captured && rng.range(0,99)<25 && d.production.factory==4)return rule.engineer;
    if(!d.crewed)return -1;
    const int side=nativeSaleHouseSide(building.player);
    const int crew=side==0?rule.alliedCrew:side==1?rule.sovietCrew:rule.technician;
    // Original IsArmed tests a WeaponType pointer, not damage/target eligibility.
    if(side<0 || (d.guns.slots[0].present && rng.range(0,99)<15))return rule.technician;
    return crew;
}

void World::releaseBuildingSurvivors(EID id) {
    if(!valid(id) || !nativeSaleBuilding(ents[id]) || !ents[id].selling || !ents[id].nativeBuildingSale.pending)return;
    const auto source=entityRef(id);
    ents[id].nativeBuildingSale.pending=false; // Persist before any allocation or callback.
    const Ent building=ents[id];
    const int count=nativeBuildingSurvivorCount(building);
    if(building.player<0 || building.player>=numPlayers)return;
    const auto& foundation=bldDef(building);const int cells=foundation.w*foundation.h;
    bool engineerSeen=false;
    constexpr Vec2f spots[]={{.75f,.25f},{.25f,.75f},{.75f,.75f}};
    constexpr int orders[][3]={{0,1,2},{0,1,2},{1,2,0},{2,0,1}};
    for(int attempt=0;attempt<count && valid(source);++attempt) {
        int type=nativeBuildingSurvivorType(building);
        while(type>=0 && nativeTypes[type]->engineer && engineerSeen)type=nativeBuildingSurvivorType(building);
        if(type<0)return;
        engineerSeen |= nativeTypes[type]->engineer;
        // Use normal creation/identities, but Unlimbo's source facing is fixed.
        const EID crew=spawnUnit(building.player,nativeTypes[type]->unit.type,building.x+.5f,building.y+.5f,type,nullptr,6);
        if(!valid(crew))continue;
        const auto recipient=entityRef(crew);
        const int cell=rng.range(0,cells-1),x=(int)building.x+cell%foundation.w,y=(int)building.y+cell/foundation.w;
        const int start=rng.range(0,3);std::array<bool,3> occupied{};int population=0;bool vehicle=false;
        if(map.inBounds(x,y))for(EID other=unitAtCell(x,y,0);other!=INVALID_EID;other=ents[other].occNext) {
            const auto& foot=ents[other];if(other==crew || foot.navLayer!=0)continue;
            if(!unitDef(foot).isInfantry()){vehicle=true;continue;}
            ++population;
            int nearest=0;float distance=100;
            for(int spot=0;spot<3;++spot)if(!occupied[spot]) {
                const float dx=foot.x-x-spots[spot].x,dy=foot.y-y-spots[spot].y;
                if(dx*dx+dy*dy<distance){nearest=spot;distance=dx*dx+dy*dy;}
            }
            occupied[nearest]=true;
        }
        int chosen=-1;
        if(map.surfacePassable(x,y,0,0,false) && (bldAt(x,y)==id || !bldBlocked(x,y,0)) && !vehicle && population<3)
            for(const int spot:orders[start])if(!occupied[spot]){chosen=spot;break;}
        if(chosen<0){consumeEntity(crew);continue;}
        auto& foot=ents[crew];foot.x=foot.px=x+spots[chosen].x;foot.y=foot.py=y+spots[chosen].y;
        foot.goalX=foot.x;foot.goalY=foot.y;initializeMovementSurface(foot);unitOccSync(crew);
        if(scatterInfantry(recipient,InfantryScatterPolicy::Forced))releaseNativePlayerUnits({recipient.id});
    }
}
