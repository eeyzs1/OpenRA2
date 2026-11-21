#include "game/world.h"
#include "game/world_priv.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
const std::array<std::vector<Vec2i>,129>& cellRings() {
    static const auto rings=[] {
        std::array<std::vector<Vec2i>,129> result;
        for(int y=-128;y<=128;++y)for(int x=-128;x<=128;++x) {
            const int a=std::abs(x),b=std::abs(y),radius=std::max(a,b)+std::min(a,b)/2;
            if(radius<=128)result[radius].push_back({x,y});
        }
        // RA2's first ring is exceptional: NE,N,NW,W,E,SW,S,SE.
        // Subsequent rings use increasing Y then X within this cell metric.
        result[1]={{1,-1},{0,-1},{-1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1}};
        return result;
    }();
    return rings;
}
}

bool World::emitNativeCellLaser(int player,float sx,float sy,EID target,EID source,int sourceSlot,const WeaponDef& weapon,uint64_t sourceActor) {
    const auto catalog=nativeWeaponCatalog;
    if(!catalog || !valid(target))return false;
    EntityMutationScope batch(*this);const auto attacker=entityRef(source);
    struct Shot {WeaponDef weapon;EntityRef target;float sx,sy,tx,ty;bool ground;};
    std::vector<Shot> shots;
    const auto prepare=[&](auto&& self,const WeaponDef& w,float x,float y,EntityRef ref,float tx,float ty,bool ground)->void {
        if(!ground) {
            if(!valid(ref))return;
            const auto& t=ents[ref.id];tx=t.x+(t.isBuilding?bldDef(t).w*.5f:0);ty=t.y+(t.isBuilding?bldDef(t).h*.5f:0);
        }
        const auto* d=w.nativeSource;
        if(d && d->laser && d->projectile->motion.inviso) {
            const auto& p=*d->projectile;int remaining=p.shrapnelCount;
            if(p.shrapnelWeapon>=0 && remaining) {
                if(remaining<0)remaining=valid(attacker)?-remaining-(int)distf(tx,ty,ents[attacker.id].x,ents[attacker.id].y):3;
                const int cx=(int)std::floor(tx),cy=(int)std::floor(ty);
                const EID first=nativeCellFirstObject(cx,cy);
                // The source routine gates on the impact cell's first object,
                // rather than on the bullet's explicit target or armor verses.
                const bool building=valid(first) && ents[first].isBuilding
                    && (!ents[first].nativeIdentity.definition || ents[first].nativeIdentity.definition->kind==0);
                if(valid(first) && !building && remaining>0) {
                    const auto& child=catalog->weapons[p.shrapnelWeapon].weapon;
                    const int range=std::clamp((int)child.range,0,128);
                    for(int ring=1;ring<=range && remaining>0;++ring)for(const auto delta:cellRings()[ring]) {
                        const EID recipient=nativeCellFirstObject(cx+delta.x,cy+delta.y);
                        if(!valid(recipient) || !valid(attacker) || recipient==attacker.id || isAllied(player,ents[recipient].player))continue;
                        self(self,child,tx,ty,entityRef(recipient),0,0,false);
                        if(--remaining==0)break;
                    }
                    while(remaining-->0) {
                        // Two independent inclusive [-2,2] draws per unfilled
                        // fragment, as in RA2. The project's RNG is retained.
                        const int dx=rng.range(-2,2),dy=rng.range(-2,2);
                        self(self,child,tx,ty,{},cx+dx+.5f,cy+dy+.5f,true);
                    }
                }
            }
        }
        shots.push_back({w,ref,x,y,tx,ty,ground});
    };
    // Recipient choice now follows the original cell/object order. Retain the
    // existing synchronous child-before-parent settlement until the original
    // bullet/Logic update timing has independent evidence; do not infer timing
    // merely from the Shrapnel() call preceding the parent's damage call.
    prepare(prepare,weapon,sx,sy,entityRef(target),0,0,false);
    for(const auto& shot:shots) {
        if(!shot.ground && !valid(shot.target))continue;
        const auto* d=shot.weapon.nativeSource;const auto& pd=projectileForWeapon(shot.weapon);
        const EID src=valid(attacker)?attacker.id:INVALID_EID;
        if(d && d->laser) {
            Effect beam;beam.kind=3;beam.aux=2;beam.aux2=(int)d->laserColor;
            beam.aux3=d->houseColor && player>=0 && player<numPlayers?players[player].colorId+1:0;
            beam.x=shot.sx;beam.y=shot.sy;beam.x2=shot.tx;beam.y2=shot.ty;
            beam.maxAge=std::max(1,d->laserDuration*LOGIC_FPS/15);effects.push_back(beam);
        }
        if(!pd.inviso) {
            if(!shot.ground){emitWeaponProjectile(player,shot.sx,shot.sy,shot.target.id,src,sourceSlot,shot.weapon,sourceActor);continue;}
            Projectile p;p.kind=kindFromProjectile(pd,shot.weapon.projSprite);p.player=player;
            p.x=p.ox=shot.sx;p.y=p.oy=shot.sy;p.tx=shot.tx;p.ty=shot.ty;
            p.sourceActor=sourceActor;p.target=INVALID_EID;p.src=src;p.srcGarrisonSlot=sourceSlot;p.w=shot.weapon;
            p.rot=pd.rot;p.arcing=pd.arcing;p.proximity=pd.proximity;p.speed=weaponSpeedCenti(shot.weapon,pd);
            if(pd.vertical)p.z=.8f;initProjectile(p);projs.push_back(p);continue;
        }
        if(shot.ground || shot.weapon.cellSpread>0)applyCellSpreadDamage(shot.weapon,player,shot.tx,shot.ty,src,sourceSlot,false,sourceActor);
        else damage(shot.target.id,(int)(shot.weapon.damage*weaponMultiplier(shot.weapon,ents[shot.target.id])),player,src,sourceSlot,false,sourceActor);
    }
    return true;
}
