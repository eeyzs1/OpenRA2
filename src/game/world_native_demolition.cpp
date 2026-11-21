#include "game/world.h"
#include "game/world_priv.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
constexpr float levelLeptons=104.f;
bool covers(const World::NativeBridgeBlueprint& bridge,const NativeActorDefinition& d,int x,int y) {
    const bool alongY=d.id=="BRIDGE2" || d.id=="BRIDGEB2";
    return alongY?y==bridge.y && std::abs(x-bridge.x)<=1:x==bridge.x && std::abs(y-bridge.y)<=1;
}
int distance3D(float x,float y,float z,float tx,float ty,float tz) {
    const auto q=[](float v){return (int64_t)std::llround((double)v*256);};
    const int64_t dx=q(x)-q(tx),dy=q(y)-q(ty),dz=(int64_t)std::llround(z)-(int64_t)std::llround(tz);
    const int64_t square=dx*dx+dy*dy+dz*dz;int64_t result=(int64_t)std::sqrt((double)square);
    while((result+1)*(result+1)<=square)++result;while(result*result>square)--result;return (int)result;
}
}

void World::collapseNativeBridgeAt(int x,int y) {
    int group=-1;
    for(int g=0;g<(int)nativeBridgeGroups.size() && group<0;++g)
        for(int i:nativeBridgeGroups[g])if(covers(nativeBridgeBlueprints[i],*nativeTypes[nativeBridgeBlueprints[i].type],x,y)){group=g;break;}
    if(group<0)return;
    // The original action repeatedly advances bridge destruction in the same
    // call. Our bridge representation stores the final unsupported span; it
    // does not yet animate the intermediate damage/end-cap transitions.
    std::vector<EID> segments;
    for(int i:nativeBridgeGroups[group]){const auto id=liveBridgeSegment(nativeBridgeBlueprints[i]);if(valid(id))segments.push_back(id);}
    for(EID id:segments)if(valid(id))kill(id,true,true);
}

bool World::nativeDemolitionAt(int waypoint) {
    if(!nativeEventDefinition || !nativeEventDefinition->demolition.available)return false;
    const auto point=nativeWaypoint(waypoint);if(!map.inBounds(point.x,point.y))return false;
    const auto& d=nativeEventDefinition->demolition;
    float z=map.surfaceHeight(point.x,point.y,0)*levelLeptons;
    // The bridge's original height remains the blast plane after a preceding
    // action destroyed it. Never move subsequent charges down to the water.
    for(const auto& bridge:nativeBridgeBlueprints)if(covers(bridge,*nativeTypes[bridge.type],point.x,point.y)) {
        z=(bridge.height+4)*levelLeptons;break;
    }
    struct Batch {
        World& world;Batch(World& w):world(w){++world.areaDamageDepth;}
        ~Batch(){if(--world.areaDamageDepth==0 && !world.updateInProgress && world.entityMutationDepth==0){
            world.freeList.insert(world.freeList.end(),world.retiredDuringUpdate.begin(),world.retiredDuringUpdate.end());world.retiredDuringUpdate.clear();}}
    } batch(*this);
    WeaponDef weapon;weapon.damage=100;weapon.nativeVerses=d.verses.data();
    const int radius=(int)(d.spread*256);
    // RA2 stores coordinates in leptons. Keep the four 1/3-cell offsets at
    // their integer positions instead of accumulating floating-point thirds.
    for(const auto offset:{Vec2i{0,0},Vec2i{-85,-85},Vec2i{85,-85},Vec2i{-85,85},Vec2i{85,85}}) {
        const float x=point.x+.5f+offset.x/256.f,y=point.y+.5f+offset.y/256.f;
        struct Hit{EID actor;int distance,amount;};std::vector<Hit> hits;
        for(EID id=0;id<(EID)ents.size();++id) {
            const auto& e=ents[id];if(!e.alive || (e.parasiting && e.utype==UnitType::TerrorDrone))continue;
            const auto& identity=e.nativeIdentity.definition;
            if(identity && (identity->kind==3 || (identity->kind==1 && !d.wood) || (identity->wall && !d.wall)))continue;
            float height=actorSurfaceHeight(e)*levelLeptons;
            if(e.supportAir || (identity && (identity->nativeRocket || e.nativeJumpjet.motion.started)))height=e.nativeAltitude;
            else if(e.airborne())height+=d.flightLevel*e.flightFraction();
            else height+=e.magneticHeight*19.5f; // existing 3 px display lift per magnetic step
            const float versus=weaponMultiplier(weapon,e);if(versus<=0 || !std::isfinite(versus))continue;
            const auto queue=[&](float tx,float ty) {
                int distance=distance3D(x,y,z,tx,ty,height);
                // Zero CellSpread is a cell-local detonation, not an exact
                // XY-point test. All five blasts remain inside this cell.
                if(radius==0) {
                    if((int)std::floor(tx)!=point.x || (int)std::floor(ty)!=point.y || std::abs(height-z)>.5f)return;
                    distance=0;
                } else if(distance>radius)return;
                const float factor=radius?1+(d.percentAtMax-1)*distance/radius:1;
                const int amount=std::max(0,(int)((int)(100*factor)*versus));if(amount)hits.push_back({id,distance,amount});
            };
            if(e.isBuilding) {
                const auto& b=bldDef(e);for(int by=0;by<b.h;++by)for(int bx=0;bx<b.w;++bx)queue(e.x+bx+.5f,e.y+by+.5f);
            } else queue(e.x,e.y);
        }
        std::stable_sort(hits.begin(),hits.end(),[](const auto& a,const auto& b){return a.actor!=b.actor?a.actor<b.actor:a.distance<b.distance;});
        for(const auto& hit:hits)if(valid(hit.actor))damage(hit.actor,hit.amount,-1);
    }
    collapseNativeBridgeAt(point.x,point.y);return true;
}
