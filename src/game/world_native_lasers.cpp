#include "game/world.h"
#include "game/world_priv.h"
#include <algorithm>
#include <cmath>

bool World::emitNativeLaser(int player,float sx,float sy,EID target,EID source,int sourceSlot,const WeaponDef& weapon,uint64_t sourceActor) {
    if(!weapon.nativeSource || !weapon.nativeSource->laser || !projectileForWeapon(weapon).inviso || !valid(target))return false;
    const auto catalog=nativeWeaponCatalog;if(!catalog)return false;
    if(catalog->version>=2)return emitNativeCellLaser(player,sx,sy,target,source,sourceSlot,weapon,sourceActor);
    EntityMutationScope batch(*this);const auto attacker=entityRef(source);
    struct Shot {WeaponDef weapon;EntityRef target;float sx,sy,tx,ty;};
    std::vector<Shot> shots;
    const auto prepare=[&](auto&& self,const WeaponDef& w,float x,float y,EntityRef ref)->void {
        if(!valid(ref))return;
        const auto& t=ents[ref.id];const auto* definition=w.nativeSource;
        const float tx=t.x+(t.isBuilding?bldDef(t).w*.5f:0),ty=t.y+(t.isBuilding?bldDef(t).h*.5f:0);
        if(definition && definition->laser && definition->projectile->motion.inviso && !t.isBuilding) {
            const auto& p=*definition->projectile;
            if(p.shrapnelWeapon>=0 && p.shrapnelCount>0) {
                const auto& child=catalog->weapons[p.shrapnelWeapon].weapon;
                struct Candidate {EntityRef ref;float distance;};std::vector<Candidate> candidates;
                for(EID id=0;id<(EID)ents.size();++id) {
                    const auto& other=ents[id];
                    if(!other.alive || id==ref.id || id==source || other.player<0 || !isEnemy(player,other.player)
                        || other.airborne() || other.parasiting || weaponMultiplier(child,other)<=0)continue;
                    const float ox=other.x+(other.isBuilding?bldDef(other).w*.5f:0),oy=other.y+(other.isBuilding?bldDef(other).h*.5f:0);
                    const float distance=distf(tx,ty,ox,oy);
                    if(child.antiGround && distance<=child.range)candidates.push_back({entityRef(id),distance});
                }
                // Stable distance/identity order is deterministic. Exact original
                // cell-spread iteration and empty-ground rays remain uncalibrated.
                std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
                    return a.distance!=b.distance?a.distance<b.distance:a.ref.id<b.ref.id;
                });
                const size_t count=std::min(candidates.size(),(size_t)p.shrapnelCount);
                for(size_t i=0;i<count;++i)self(self,child,tx,ty,candidates[i].ref);
            }
        }
        shots.push_back({w,ref,x,y,tx,ty});
    };
    // Freeze the complete finite recipient graph before any damage callbacks.
    // Evacuees and reused slots cannot be selected as split targets of this shot.
    prepare(prepare,weapon,sx,sy,entityRef(target));
    for(const auto& shot:shots) {
        if(!valid(shot.target))continue;const auto* d=shot.weapon.nativeSource;
        const EID src=valid(attacker)?attacker.id:INVALID_EID;
        if(!d || !d->laser || !projectileForWeapon(shot.weapon).inviso) {
            emitWeaponProjectile(player,shot.sx,shot.sy,shot.target.id,src,sourceSlot,shot.weapon,sourceActor);continue;
        }
        Effect beam;beam.kind=3;beam.aux=2;beam.aux2=(int)d->laserColor;
        beam.aux3=d->houseColor && player>=0 && player<numPlayers?players[player].colorId+1:0;
        beam.x=shot.sx;beam.y=shot.sy;beam.x2=shot.tx;beam.y2=shot.ty;beam.maxAge=std::max(1,d->laserDuration*LOGIC_FPS/15);
        effects.push_back(beam);
        if(shot.weapon.cellSpread>0)applyCellSpreadDamage(shot.weapon,player,shot.tx,shot.ty,src,sourceSlot,false,sourceActor);
        else damage(shot.target.id,(int)(shot.weapon.damage*weaponMultiplier(shot.weapon,ents[shot.target.id])),player,src,sourceSlot,false,sourceActor);
    }
    return true;
}
