#include "game/world.h"
#include "game/world_priv.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
int areaDistance(float x,float y,float tx,float ty) {
    const auto quantize=[](float v){return (int64_t)std::llround((double)v*256);};
    const int64_t dx=quantize(x)-quantize(tx),dy=quantize(y)-quantize(ty);
    const int64_t squared=dx*dx+dy*dy;
    int64_t distance=(int64_t)std::sqrt((double)squared);
    while((distance+1)*(distance+1)<=squared)++distance;
    while(distance*distance>squared)--distance;
    return (int)distance;
}
}

void World::applyCellSpreadDamage(const WeaponDef& weapon,int player,float x,float y,EID source,int sourceSlot,bool chaosGas,uint64_t sourceActor) {
    if(weapon.cellSpread<0 || weapon.cellSpread>11 || !std::isfinite(weapon.cellSpread)
        || !std::isfinite(weapon.percentAtMax) || weapon.percentAtMax<0 || weapon.percentAtMax>1
        || !std::isfinite(x) || !std::isfinite(y) || weapon.damage<=0
        || x<-12 || y<-12 || x>map.w+12 || y>map.h+12)return;
    // Capture all recipients and amounts before damage can run evacuation or Lua
    // callbacks. Newly created entities are not recipients of this older blast.
    struct Batch {
        World& world;
        Batch(World& w):world(w){++world.areaDamageDepth;}
        ~Batch(){
            if(--world.areaDamageDepth==0 && !world.updateInProgress && world.entityMutationDepth==0) {
                world.freeList.insert(world.freeList.end(),world.retiredDuringUpdate.begin(),world.retiredDuringUpdate.end());
                world.retiredDuringUpdate.clear();
            }
        }
    } batch(*this);
    const int range=(int)(weapon.cellSpread*256);
    const float margin=(range+2)/256.f;
    const int impactX=(int)std::floor(x),impactY=(int)std::floor(y);
    struct Recipient { EID id;int distance,amount; };
    std::vector<Recipient> recipients;
    for(EID id=0;id<(EID)ents.size();++id) {
        const auto& target=ents[id];
        if(!target.alive || id==source || (target.parasiting && target.utype==UnitType::TerrorDrone))continue;
        if(weapon.nativeSource && weapon.nativeSource->warhead && !weapon.nativeSource->warhead->affectsAllies
            && player>=0 && target.player>=0 && isAllied(player,target.player))continue;
        if(!std::isfinite(target.x) || !std::isfinite(target.y))continue;
        if(target.airborne()?!weapon.antiAir:!weapon.antiGround)continue;
        const float versus=weaponMultiplier(weapon,target);
        if(!std::isfinite(versus) || versus<=0)continue;
        auto amountAt=[&](int distance)->int64_t {
            if(distance>range)return 0;
            const float factor=range>0?1-(1-weapon.percentAtMax)*distance/range:1;
            const int64_t attenuated=(int64_t)((float)weapon.damage*factor);
            return std::max<int64_t>(0,(int64_t)((float)attenuated*versus));
        };
        auto queue=[&](int distance) {
            const auto amount=amountAt(distance);
            if(amount>0)recipients.push_back({id,distance,(int)std::min<int64_t>(amount,std::numeric_limits<int>::max())});
        };
        if(target.isBuilding) {
            const auto& def=bldDef(target);
            if(x<target.x-margin || x>target.x+def.w+margin || y<target.y-margin || y>target.y+def.h+margin)continue;
            // Each occupied cell contributes a separate hit. Do not merge these:
            // intermediate damage can evacuate a garrison before a later hit kills
            // the building. The batch quarantines IDs across all callbacks.
            for(int cy=0;cy<def.h;++cy)for(int cx=0;cx<def.w;++cx) {
                const int bx=(int)target.x+cx,by=(int)target.y+cy;
                const int distance=bx==impactX && by==impactY?0:areaDistance(x,y,bx+.5f,by+.5f);
                queue(distance);
            }
        } else {
            // Current simulation uses XY positions. Original spherical altitude
            // rules are a separate calibration, not an implicit cylinder claim.
            if(std::abs(x-target.x)>margin || std::abs(y-target.y)>margin)continue;
            queue(areaDistance(x,y,target.x,target.y));
        }
    }
    std::stable_sort(recipients.begin(),recipients.end(),[](const auto& a,const auto& b) {
        if(a.id!=b.id)return a.id<b.id;
        return a.distance<b.distance;
    });
    for(const auto& recipient:recipients) {
        if(!valid(recipient.id))continue;
        damage(recipient.id,recipient.amount,player,source,sourceSlot,false,sourceActor);
        if(chaosGas && valid(recipient.id)) {
            auto& target=ents[recipient.id];
            if(!target.isBuilding && !unitDef(target).isAir() && !psychicImmune(target.utype) && target.invuln==0) {
                target.confused=180;target.target=INVALID_EID;
            }
        }
    }
}
