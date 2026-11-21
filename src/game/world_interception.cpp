#include "game/world.h"
#include "game/world_priv.h"
#include <cstring>

void World::interceptProjectile(Projectile& projectile) {
    // Missile targets still use the legacy projectile representation. A hit
    // resolves here rather than spawning a second, independently tracked
    // counter-missile; flight/target acquisition fidelity remains unfinished.
    // Readiness and damage, however, must use the actual defending weapon.
    for(EID id=0;id<(EID)ents.size();++id) {
        auto& e=ents[id];
        if(!e.alive || e.player<0 || !isEnemy(e.player,projectile.player) || e.atkCd>0
            || e.immobilized() || e.deploying() || e.selling || e.constructAnim>0 || e.parasiting || e.burstRemaining>0)continue;
        const auto weapon=e.isBuilding?bldDef(e).weapon:(unitDef(e).hasAirWeapon?unitDef(e).airWeapon:effWeapon(e));
        if(!weapon.antiAir || weapon.damage<=0 || (e.isBuilding && (players[e.player].lowPower() || valid(e.drainedBy))))continue;
        if(!e.isBuilding && unitDef(e).ammo>0 && e.ammo<=0)continue;
        const int damage=scaledWeaponDamage(weapon.damage,weaponVsArmor(weapon,Armor::Special2,false,false));
        if(damage<=0)continue;
        float x=e.x,y=e.y;if(e.isBuilding){x+=bldDef(e).w/2.f;y+=bldDef(e).h/2.f;}
        if(distf(x,y,projectile.x,projectile.y)>weapon.range)continue;
        e.atkCd=std::max(1,weapon.cooldown);e.fireAnim=6;e.turretDir=dirFromVec(projectile.x-x,projectile.y-y);
        if(!e.isBuilding && unitDef(e).ammo>0)--e.ammo;
        PresentationEvent event;event.kind=PresentationKind::Muzzle;event.tick=tick;event.sequence=presentationSequence++;
        event.entity=id;event.player=e.player;event.x=x;event.y=y;event.targetX=projectile.x;event.targetY=projectile.y;
        captureVisualTarget(event,e);if(presentationEvents.size()>=4096)presentationEvents.pop_front();presentationEvents.push_back(event);
        if(weapon.projSprite && !strcmp(weapon.projSprite,"missile"))emitSoundAt(Sfx::Missile,x,y,id);
        else if(weapon.projSprite && !strcmp(weapon.projSprite,"flak"))emitSoundAt(Sfx::Flak,x,y,id);
        projectile.hp-=damage;explodeAt(projectile.x,projectile.y,0);
        if(projectile.hp<=0){projectile.alive=false;return;}
    }
}
