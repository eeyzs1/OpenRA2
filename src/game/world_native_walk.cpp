#include "game/world.h"
#include "game/native_scene_contract.h"
#include "game/native_walk.h"
#include <algorithm>

bool World::usesNativeWalk(const Ent& e) const {
    return nativeRulesBaseVersion>=nativeWalkStepSceneVersion && !e.isBuilding && unitDef(e).isInfantry()
        && e.nativeIdentity.definition && e.nativeIdentity.definition->walkSpeedLeptons>=0;
}

int World::nativeWalkingSpeed(const Ent& e) const {
    const auto& profile=*e.nativeIdentity.definition;
    double bonus=nativeCrateMultiplier(e.nativeIdentity,10);
    if(e.crateSpeedBoost>0)bonus=NativeMovementMath::multiply(bonus,1.25);
    return NativeWalkMath::speed(profile.walkSpeedLeptons,profile.rulesOnly?movementSpeedMultiplier(e.player):1,
        bonus,profile.walkVeteranSpeed[std::clamp(e.vetRank,0,2)]);
}

float World::walkingMovementStep(const Ent& e) const {
    // Original Speed is percent -> 0..255 leptons per15-Hz frame.
    const auto& profile=*e.nativeIdentity.definition;
    float leptons=profile.walkSpeedLeptons*profile.walkVeteranSpeed[std::clamp(e.vetRank,0,2)]*nativeCrateMultiplier(e.nativeIdentity,10);
    if(profile.rulesOnly)leptons*=(float)movementSpeedMultiplier(e.player);
    if(e.crateSpeedBoost>0)leptons*=1.25f;
    return (int)leptons*(15.f/(256.f*LOGIC_FPS));
}
