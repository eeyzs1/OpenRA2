#include "game/world.h"
#include "game/lang.h"
#include <algorithm>
#include <cmath>

bool World::nativeCrateType(int kind) const {
    if(!nativeCrateRules.available || kind<NATIVE_CRATE_BASE || kind>NATIVE_CRATE_BASE+19)return false;
    if(nativeCrateRules.randomCrates){switch(kind-NATIVE_CRATE_BASE){case 0:case 1:case 2:case 8:case 9:case 10:case 11:case 14:case 19:return true;default:return false;}}
    if(!nativeCrateRules.available || kind<NATIVE_CRATE_BASE || kind>=NATIVE_CRATE_BASE+19)return false;
    switch(kind-NATIVE_CRATE_BASE){case 0:case 2:case 9:case 10:case 11:case 13:return true;default:return false;}
}
bool World::createNativeCrate(int powerup,Vec2i point){
    if(powerup<0 || powerup>=19 || !nativeCrateType(NATIVE_CRATE_BASE+powerup) || !map.inBounds(point.x,point.y) || crates.size()>=1024)return false;
    const auto place=[&](int x,int y){if(!map.inBounds(x,y) || !map.passable(x,y) || bldBlocked(x,y) || unitAtCell(x,y)!=INVALID_EID)return false;
        if(std::any_of(crates.begin(),crates.end(),[&](const auto& c){return c.alive && c.x==x && c.y==y;}))return false;
        Crate crate;crate.x=x;crate.y=y;crate.kind=NATIVE_CRATE_BASE+powerup;crates.push_back(crate);return true;};
    if(std::any_of(crates.begin(),crates.end(),[&](const auto& c){return c.alive && c.x==point.x && c.y==point.y;}))return false;
    if(place(point.x,point.y))return true;
    for(int radius=1;radius<=4;++radius)for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x)
        if(std::max(std::abs(x),std::abs(y))==radius && place(point.x+x,point.y+y))return true;
    return false;
}
float World::nativeCrateMultiplier(const NativeActorIdentity& identity,int kind) const {
    if(!nativeCrateRules.available || identity.object.empty())return 1;
    const auto found=nativeCrateBonuses.find(identity.object);if(found==nativeCrateBonuses.end())return 1;
    return kind==9?found->second.armor:kind==10?found->second.speed:found->second.firepower;
}
bool World::pickupNativeCrate(Ent& collector,const Crate& crate){
    if(!nativeCrateType(crate.kind) || collector.player<0 || collector.player>=numPlayers || !collector.nativeIdentity.definition
        || collector.nativeIdentity.definition->kind || collector.airborne() || collector.parasiting || collector.magneticHeight>0)return false;
    if(nativeCrateRules.randomCrates)return pickupSourceCrate(collector,crate);
    const int kind=crate.kind-NATIVE_CRATE_BASE;
    if(kind==0){const auto before=players[collector.player].money;changeMoney(collector.player,nativeCrateRules.soloMoney);
        emitSoundAt(Sfx::Cash,collector.x,collector.y);eva(collector.player,formatText(TR(S::CrateMoney),(int)(players[collector.player].money-before)));}
    else if(kind==2){for(auto& actor:ents)if(actor.alive && actor.player==collector.player && actor.nativeIdentity.definition
            && !actor.nativeIdentity.definition->kind)actor.hp=actor.isBuilding?bldDef(actor).hp:unitDef(actor).hp;
        eva(collector.player,TR(S::CrateHeal));}
    else {
        for(auto& actor:ents){if(!actor.alive || actor.player<0 || !actor.nativeIdentity.definition || actor.nativeIdentity.definition->kind
                || actor.airborne() || actor.parasiting || actor.magneticHeight>0 || actor.nativeIdentity.object.empty())continue;
            const float x=actor.x+(actor.isBuilding?bldDef(actor).w*.5f:0),y=actor.y+(actor.isBuilding?bldDef(actor).h*.5f:0);
            if(distf(x,y,crate.x+.5f,crate.y+.5f)>=nativeCrateRules.radius)continue;
            // The original kind/data are preserved. Minute conversion and the
            // complete RA2 infantry/iron-curtain boundary still need calibration.
            if(kind==13){actor.invuln=std::max(actor.invuln,(int)std::lround(nativeCrateRules.data[kind]*60*LOGIC_FPS));continue;}
            if(kind==10 && (actor.isBuilding || unitDef(actor).isAir()))continue;
            auto& bonus=nativeCrateBonuses[actor.nativeIdentity.object];
            float& value=kind==9?bonus.armor:kind==10?bonus.speed:bonus.firepower;
            if(value==1)value=nativeCrateRules.data[kind];
        }
        if(kind!=13)eva(collector.player,TR(kind==9?S::CrateArmor:kind==10?S::CrateSpeed:S::CratePower));
    }
    return true;
}
bool World::reshroudNativeWaypoint(int radius,Vec2i point){
    if(!nativeCrateRules.available || !nativeEventDefinition || radius<0 || radius>512 || !map.inBounds(point.x,point.y))return false;
    auto& fog=map.fog[nativeEventDefinition->playerHouse];
    for(int y=std::max(0,point.y-radius);y<=std::min(map.h-1,point.y+radius);++y)
        for(int x=std::max(0,point.x-radius);x<=std::min(map.w-1,point.x+radius);++x)
            if((x-point.x)*(x-point.x)+(y-point.y)*(y-point.y)<=radius*radius)fog[cellIdx(x,y)]=FOG_UNSEEN;
    return true;
}
