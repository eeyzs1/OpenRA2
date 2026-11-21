#include "game/world.h"
#include "game/native_house_geometry.h"
#include "game/native_source_coordinates.h"
#include <algorithm>
#include <cctype>

namespace {
std::string lower(std::string value){for(char& c:value)c=(char)std::tolower((unsigned char)c);return value;}
std::vector<uint64_t> buildings(const World& world,int house) {
    std::vector<uint64_t> result;
    for(const auto& actor:world.ents)if(actor.alive && actor.isBuilding && actor.player==house && actor.hp>0
        && actor.nativeIdentity.definition && !actor.nativeIdentity.definition->kind)result.push_back(actor.combatActor);
    std::sort(result.begin(),result.end());return result;
}
Vec2i location(const World& world,const World::Ent& actor,bool center=false) {
    // Native scene/project building anchors refer to their source cell.
    const Vec2i raw{(int)((actor.x+(actor.isBuilding?.5f:0))*256),(int)((actor.y+(actor.isBuilding?.5f:0))*256)};
    if(center && world.nativeAIRules.hasOriginalSourceCoordinates() && actor.isBuilding) {
        const auto& type=bldDef(actor);
        const auto point=nativeSourceBuildingCoordinates({raw.x,raw.y,0},type.w,type.h);
        return {point.x,point.y};
    }
    return raw;
}
Vec2i clearCenter(const World& world,Vec2i center) {
    if(center==Vec2i{})return center;
    const int cx=center.x/256,cy=center.y/256;
    if(world.nativeAIRules.hasOriginalNearbyGeometry()) {
        const auto point=world.nativeNearbyPoint({cx,cy},0);
        return {point.x*256+128,point.y*256+128};
    }
    if(world.map.passable(cx,cy))return center;
    // The original NearbyLocation query is an interface boundary. Retain
    // the shared terrain clearance until its source search order is ported.
    Vec2i best{};int64_t distance=INT64_MAX;
    for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x)if(world.map.passable(x,y)) {
        const int64_t dx=x-cx,dy=y-cy,d=dx*dx+dy*dy;
        if(d<distance){distance=d;best={x*256+128,y*256+128};}
    }
    return best;
}
}

void World::rebuildNativeHouseInventory() {
    nativeHouseInventory.clear();
    if(!nativeAIRules.hasOriginalTeamControl())return;
    for(int house=0;house<numPlayers;++house)nativeHouseInventory.push_back(buildings(*this,house));
}

void World::initializeNativeHouseControl() {
    nativeHouseInventory.clear();
    nativeTeamMemberCounts.clear();
    if(!nativeAIRules.hasOriginalTeamControl())return;
    nativeAI.centers.assign(numPlayers,{});
    for(int house=0;house<numPlayers;++house) {
        auto& state=nativeAI.centers[house];state.initialized=true;
        const Ent* firstYard=nullptr;const Ent* base=nullptr;
        for(const auto& actor:ents)if(actor.alive && actor.player==house && actor.nativeIdentity.definition) {
            const auto id=lower(actor.nativeIdentity.definition->id);
            if(actor.isBuilding && actor.btype==BldType::ConYard && (!firstYard || actor.combatActor<firstYard->combatActor))firstYard=&actor;
            if(actor.isBuilding && std::find(nativeAIRules.baseBuildings.begin(),nativeAIRules.baseBuildings.end(),id)!=nativeAIRules.baseBuildings.end()) {
                if(!base || actor.combatActor>base->combatActor)base=&actor;
            }
        }
        // The opening list at House+0x54 is ConYards, not vehicles. With only
        // an MCV the original center remains empty. BuildConst then prefers
        // the last matching building over the initial first-yard fallback.
        const auto* anchor=base?base:firstYard;
        if(anchor){const auto point=location(*this,*anchor);state.center={(point.x/256)*256+128,(point.y/256)*256+128};}
    }
    rebuildNativeHouseInventory();
}

void World::updateNativeHouseControl() {
    if(!nativeAIRules.hasOriginalTeamControl() || nativeAI.centers.size()!=(size_t)numPlayers || nativeHouseInventory.size()!=(size_t)numPlayers)return;
    for(int house=0;house<numPlayers;++house) {
        auto current=buildings(*this,house);if(current==nativeHouseInventory[house])continue;
        nativeHouseInventory[house]=std::move(current);
        std::vector<NativeHouseBuildingPoint> points;
        for(const auto& actor:ents)if(actor.alive && actor.isBuilding && actor.player==house && actor.nativeIdentity.definition && !actor.nativeIdentity.definition->kind) {
            const auto point=location(*this,actor,true);
            points.push_back({point.x,point.y,nativeProductionCost(house,actor.nativeIdentity.type),actor.hp,false});
        }
        nativeAI.centers[house].center=clearCenter(*this,nativeHouseWeightedCenter(points));
    }
}
