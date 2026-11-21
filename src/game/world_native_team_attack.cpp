#include "game/world.h"
#include "game/world_priv.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const NativeTargetProfile* profile(const World::Ent& actor) {
    const auto* definition=actor.nativeIdentity.definition.get();
    return definition && definition->targeting.available?&definition->targeting:nullptr;
}
struct WeaponRating {bool present=false;double effectiveness=0;float range=0;};
WeaponRating weaponRating(const World& world,const World::Ent& actor,const World::Ent& target,float distance) {
    if(world.hasNativeGuns(actor)) {
        const auto weapon=world.effWeapon(actor,&target);
        return {weapon.damage>0,weapon.damage>0?weaponMultiplier(weapon,target):0,weapon.range};
    }
    if(const auto* source=profile(actor)) {
        const int armor=(int)(target.nativeIdentity.definition?target.nativeIdentity.definition->armor:
            target.isBuilding?Armor::Concrete:unitDef(target).armor);
        WeaponRating best;double weight=-1;
        for(const auto& weapon:source->weapons) {
            if(!weapon.present || (target.airborne()?!weapon.air:!weapon.ground))continue;
            const double effectiveness=weapon.verses[armor];
            const double score=effectiveness*(distance<=weapon.range?2:1);
            if(score>weight){weight=score;best={true,effectiveness,weapon.range};}
        }
        return best;
    }
    const auto weapon=world.effWeapon(actor,&target);
    return {weapon.damage>0,weapon.damage>0?weaponMultiplier(weapon,target):0,weapon.range};
}
bool category(const World::Ent& actor,int quarry) {
    const auto* source=profile(actor);
    const bool building=actor.isBuilding;
    const bool infantry=!building && unitDef(actor).isInfantry();
    const bool vehicle=(!building && !infantry && !actor.airborne())
        || (building && actor.btype!=BldType::ConYard && source && source->undeploys);
    switch(quarry) {
    case 1:case 8:return true;
    case 2:return building;
    case 3:return source?source->storage:building?actor.btype==BldType::OreRefinery:unitDef(actor).canHarvet();
    case 4:return infantry;
    case 5:return vehicle;
    case 6:return building && (source?source->factory:actor.btype==BldType::ConYard || isRallyBuilding(actor.btype));
    case 7:return building && (source?source->weapons[0].present:bldDef(actor).weapon.damage>0);
    case 9:return building && bldDef(actor).power>0;
    default:return false;
    }
}
// A command-local movement-zone approximation using the project's actual
// layered surfaces. It is derived state, never a saved visibility/RNG input.
std::vector<uint8_t> movementZone(const World& world,const World::Ent& actor) {
    if(actor.airborne() || unitDef(actor).isAir())return {};
    std::vector<uint8_t> zone(world.movementSlotCount(),0);
    if(!world.map.inBounds((int)actor.x,(int)actor.y))return zone;
    const int area=(int)world.map.cells.size(),domain=unitDef(actor).pathDomain();
    const bool under=unitDef(actor).fitsUnderBridge();const auto access=world.nativeGateAccess(actor);
    const int start=actor.navLayer*area+world.cellIdx((int)actor.x,(int)actor.y);
    std::vector<int> queue{start};zone[start]=1;
    constexpr Vec2i steps[]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
    for(size_t head=0;head<queue.size();++head) {
        const int slot=queue[head],layer=slot/area,x=slot%area%world.map.w,y=slot%area/world.map.w;
        for(auto step:steps) {
            const int nx=x+step.x,ny=y+step.y;
            const int nextLayer=world.map.stepLayer(x,y,layer,nx,ny,domain,true,under,access);
            if(nextLayer<0)continue;const int next=nextLayer*area+world.cellIdx(nx,ny);
            if(!zone[next]){zone[next]=1;queue.push_back(next);}
        }
    }
    return zone;
}
bool inZone(const World& world,const World::Ent& actor,const std::vector<uint8_t>& zone) {
    if(zone.empty() || actor.airborne())return true;
    const int x=(int)actor.x,y=(int)actor.y,layer=actor.navLayer;
    const auto reached=[&](int tx,int ty) {
        return world.map.inBounds(tx,ty) && zone[layer*world.map.cells.size()+world.cellIdx(tx,ty)];
    };
    if(!actor.isBuilding)return reached(x,y);
    const auto& building=bldDef(actor);
    for(int ty=y-1;ty<=y+building.h;++ty)for(int tx=x-1;tx<=x+building.w;++tx)if(reached(tx,ty))return true;
    return false;
}
bool discovered(const World& world,const World::Ent& actor,int player) {
    const int width=actor.isBuilding?bldDef(actor).w:1,height=actor.isBuilding?bldDef(actor).h:1;
    for(int y=(int)actor.y;y<(int)actor.y+height;++y)for(int x=(int)actor.x;x<(int)actor.x+width;++x)
        if(world.map.inBounds(x,y) && world.map.fog[player][world.cellIdx(x,y)]>0)return true;
    return false;
}
}

EID World::nativeTeamAttackTarget(int index,int quarry) const {
    if(!nativeEventDefinition || index<0 || index>=(int)nativeEvents.teams.size() || quarry<1 || quarry>9)return INVALID_EID;
    const auto& team=nativeEvents.teams[index];EID leader=nativeTeamLeader(team);
    if(!valid(leader))return INVALID_EID;
    const auto& actor=ents[leader];const auto* source=profile(actor);if(!source)return INVALID_EID;
    const bool human=actor.player==nativeEventDefinition->playerHouse;
    const auto zone=movementZone(*this,actor);
    EID best=INVALID_EID;double rating=-std::numeric_limits<double>::infinity();
    // The whole-map scan considers airborne candidates before ground actors.
    // Equal scores keep the first stable entity ID, without consuming RNG.
    for(int pass=0;pass<2;++pass)for(EID id=0;id<(EID)ents.size();++id) {
        if(!valid(id) || id==leader)continue;const auto& target=ents[id];
        if(target.airborne()!=(pass==0) || target.hp<=0 || target.parasiting || target.player<0
            || !isEnemy(actor.player,target.player) || !category(target,quarry))continue;
        const auto* targetSource=profile(target);const auto* definition=target.nativeIdentity.definition.get();
        if((definition && (definition->kind!=0 || definition->nativeRocket))
            || (targetSource && (!targetSource->legal || targetSource->invisible))
            || !map.inBounds((int)target.x,(int)target.y) || !visibleTo(target,actor.player) || !inZone(*this,target,zone))continue;
        if(!target.isBuilding && (target.camouflaged || target.utype==UnitType::Spy)
            && !source->detectDisguise && (human || !source->aiDetectDisguise))continue;
        if(human && !target.airborne() && !discovered(*this,target,actor.player))continue;
        if(!target.airborne() && source->weapons[0].present && !source->weapons[0].ground)continue;
        const bool explicitBuilding=quarry==2 || quarry==3 || quarry==6 || quarry==7 || quarry==9;
        if(unitDef(actor).isInfantry()) {
            const bool organic=source->weapons[0].present && source->weapons[0].verses[(int)Armor::Heavy]==0;
            if(organic && !(!target.isBuilding && unitDef(target).isInfantry())
                && !(target.isBuilding && (explicitBuilding || (!human && unitDef(actor).hasC4()))))continue;
            if(human && target.isBuilding && !explicitBuilding)continue;
        }
        const float tx=target.x+(target.isBuilding?bldDef(target).w*.5f:0),ty=target.y+(target.isBuilding?bldDef(target).h*.5f:0);
        const float distance=distf(actor.x,actor.y,tx,ty);const auto mine=weaponRating(*this,actor,target,distance);
        if(!mine.present || mine.effectiveness<=0)continue;
        const auto theirs=weaponRating(*this,target,actor,distance);
        const double maximum=target.isBuilding?bldDef(target).hp:unitDef(target).hp;
        const auto& c=source->coefficients;
        double score=c[1]*theirs.effectiveness*(target.target==leader?-1:1);
        score+=c[2]*(targetSource?targetSource->specialThreat:0);
        score+=c[0]*mine.effectiveness;
        score+=c[3]*std::clamp(target.hp/maximum,0.0,1.0);
        // The reference engine's ground whole-map branch measures distance
        // in leptons, while its weapon range remains in cells.
        score+=c[4]*std::max(0.0,(double)distance*(target.airborne()?1:256)-mine.range);
        score+=100000;
        if(quarry==9)score+=bldDef(target).power*1000.0;
        score=std::floor(score);if(score==0)continue;score=std::max(1.0,score);
        if(score>rating){rating=score;best=id;}
    }
    return best;
}

void World::updateNativeTeamAttack(int index) {
    auto& team=nativeEvents.teams[index];const auto& definition=nativeEventDefinition->teams[team.definition];
    const int quarry=nativeEventDefinition->scripts[definition.script].steps[team.step].y;
    // RA2's additional occupied/tech categories still require calibration.
    // Do not treat those known categories, or a missing old source profile,
    // as an empty successful scan.
    if(quarry==10 || quarry==11 || definition.onlyTargetEnemy){team.blockedScript=0;return;}
    for(EID id:team.members)if(valid(id) && !profile(ents[id])){team.blockedScript=0;return;}
    if(team.advancePending){advanceNativeTeamScript(index);return;}
    team.lineStarted=true;team.blockedScript=-1;
    bool allUseAmmo=true,anyAmmo=false,able=false;
    for(EID id:team.members)if(valid(id)) {
        const auto& actor=ents[id];const auto& type=unitDef(actor);
        allUseAmmo &= type.ammo>0;anyAmmo |= type.ammo==0 || actor.ammo>0;
        able |= !type.isAir() || type.weapon.damage<=0 || type.ammo==0 || actor.ammo>0;
    }
    if(!able || (allUseAmmo && !anyAmmo)) {
        team.focus=-1;team.waitUntil=0;team.advancePending=true;return;
    }
    if(valid(team.focus) && !isEnemy(team.memberHouse,ents[team.focus].player))team.focus=-1;
    if(!valid(team.focus)) {
        team.focus=nativeTeamAttackTarget(index,quarry);
        team.waitUntil=0;
        if(!valid(team.focus)){team.focus=-1;team.advancePending=true;return;}
    }
    if(tick<team.waitUntil)return;
    bool accepted=false;
    for(EID id:team.members)if(valid(id)) {
        const auto& actor=ents[id];
        if(actor.target==team.focus && (actor.state==UState::Attacking || actor.state==UState::Chasing)){accepted=true;continue;}
        accepted |= orderAttack({id},team.focus,false).accepted!=0;
    }
    // A rejected public path/order does not mean the category is exhausted.
    // Keep the real mission target and retry instead of skipping the line.
    team.blockedScript=accepted?-1:0;
    if(!accepted)team.waitUntil=tick+LOGIC_FPS;
}
