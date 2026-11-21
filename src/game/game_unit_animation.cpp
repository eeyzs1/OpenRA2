#include "game/game.h"
#include "gfx/sprites.h"
#include <algorithm>
#include <cmath>

namespace {
int interpolatedFacing(int previous,int current,float alpha) {
    const int delta=((current-previous+4)&7)-4;
    return (int)std::lround(4.f*previous+4.f*delta*std::clamp(alpha,0.f,1.f))&31;
}
}

int Game::unitBodyFacing(const World::Ent& e) const {
    if(world.usesNativeGround(e) && e.nativeGround.initialized && g_sprites.hasFineFacings(e.utype)) {
        const auto current=world.nativeHullHeading(e,world.tick);
        return NativeGroundMath::facing32(uint16_t(int(e.previousHullHeading)+(int)std::lround(
            NativeGroundMath::delta(e.previousHullHeading,current)*std::clamp(interpAlpha,0.f,1.f))));
    }
    return g_sprites.hasFineFacings(e.utype)?interpolatedFacing(e.previousDir,e.dir,interpAlpha):(e.dir&7)*4;
}

int Game::unitTurretFacing(const World::Ent& e) const {
    if(world.usesNativeGround(e) && e.nativeGround.initialized && g_sprites.hasFineFacings(e.utype)) {
        const auto current=world.nativeTurretHeading(e,world.tick);
        return NativeGroundMath::facing32(uint16_t(int(e.previousTurretHeading)+(int)std::lround(
            NativeGroundMath::delta(e.previousTurretHeading,current)*std::clamp(interpAlpha,0.f,1.f))));
    }
    return g_sprites.hasFineFacings(e.utype)?interpolatedFacing(e.previousTurretDir,e.turretDir,interpAlpha):(e.turretDir&7)*4;
}

const Sprite* Game::swimmingSprite(const World::Ent& e,UnitType drawType,int color) const {
    if(!unitDef(e).canSwim() || e.navLayer!=0 || e.airborne() || e.magneticHeight>0
        || !world.map.inBounds((int)e.x,(int)e.y) || world.map.at((int)e.x,(int)e.y).terrain!=Terrain::Water)return nullptr;
    const auto& info=g_sprites.animInfo(drawType);
    if(e.fireAnim>0 && info.wetFire>0) {
        const int phase=std::clamp((g_gameRules.fireAnimationTicks-e.fireAnim)*info.wetFire/std::max(1,g_gameRules.fireAnimationTicks),0,info.wetFire-1);
        return &g_sprites.unitAnim(drawType,UAnim::WetFire,e.dir,phase,color);
    }
    const bool moving=std::abs(e.x-e.px)>1e-5f || std::abs(e.y-e.py)>1e-5f;
    if(moving && info.swim>0)return &g_sprites.unitAnim(drawType,UAnim::Swim,e.dir,(e.walkAnim/std::max(1,info.swimRate))%info.swim,color);
    if(info.tread>0)return &g_sprites.unitAnim(drawType,UAnim::Tread,e.dir,(world.tick/std::max(1,info.treadRate))%info.tread,color);
    return nullptr;
}
