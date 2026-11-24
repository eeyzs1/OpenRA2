#include "game/world.h"
#include "game/native_scene_contract.h"
#include "game/lang.h"

bool World::usesNativeTransportUnload(const Ent& e) const {
    return nativeTransportUnloadRules.available && nativeRulesBaseVersion>=nativeTransportUnloadSceneVersion
        && !e.isBuilding && e.utype!=UnitType::AircraftCarrier && e.nativeIdentity.definition
        && !unitDef(e).isAir() && !unitDef(e).isInfantry() && unitDef(e).cargoCap>0;
}

bool World::validateNativeTransportUnload() const {
    for(const auto& e:ents) {
        const auto& s=e.nativeTransportUnload;
        if(!s.active()) {if(s.ticks)return false;continue;}
        if(!e.alive || !usesNativeTransportUnload(e) || s.phase>NativeTransportUnloadPhase::Finish
            || s.ticks<0 || s.ticks>nativeTransportUnloadRules.maxTicks())return false;
        if((s.phase==NativeTransportUnloadPhase::Prepare || s.phase==NativeTransportUnloadPhase::Ready)
            && e.cargo.empty())return false;
    }
    return true;
}

bool World::updateNativeTransportUnload(EID id) {
    auto& e=ents[id];auto& mission=e.nativeTransportUnload;
    if(!mission.active())return false;
    if(!usesNativeTransportUnload(e)) {mission={};return false;}
    // Completing a route is distinct from admitting a new Move command.
    const bool moving=mission.phase==NativeTransportUnloadPhase::Prepare
        && (e.pathIdx<(int)e.path.size() || !e.wps.empty());
    if(mission.ticks>0 && --mission.ticks>0)return !moving;
    const auto& rules=nativeTransportUnloadRules;
    auto rate=[&]{return (rules.rateFrames+rng.range(0,rules.randomFrames))*2;};
    switch(mission.phase) {
    case NativeTransportUnloadPhase::Prepare: {
        if(moving){mission.ticks=rules.movingFrames*2;return false;}
        const auto saved=mission;
        orderStop({id},false);
        ents[id].nativeTransportUnload=saved;
        auto& next=ents[id].nativeTransportUnload;
        if(ents[id].cargo.empty()){next.phase=NativeTransportUnloadPhase::Finish;next.ticks=rate();return true;}
        const bool sourceExit=nativeRulesBaseVersion>=nativeTransportExitSceneVersion;
        const auto exit=sourceExit?nativeTransportExit(ents[id]):NativeTransportMath::Exit{};
        const bool noExit=sourceExit?exit.direction<0:releaseCells(ents[id],unitDef(ents[id].cargo.back()),2).empty();
        if(noExit) {
            next.phase=NativeTransportUnloadPhase::Finish;next.ticks=rate();return true;
        }
        if(sourceExit)desireNativeHull(ents[id],static_cast<uint16_t>(exit.facing*8192));
        next.phase=NativeTransportUnloadPhase::Ready;next.ticks=rules.stageFrames*2;return true;
    }
    case NativeTransportUnloadPhase::Ready:
        if(e.nativeGround.initialized && nativeHullHeading(e,tick)!=e.nativeGround.hull.target) {
            mission.ticks=rate();return true;
        }
        mission.phase=NativeTransportUnloadPhase::Release;mission.ticks=rules.stageFrames*2;return true;
    case NativeTransportUnloadPhase::Release: {
        if(e.cargo.empty()) {mission.phase=NativeTransportUnloadPhase::Finish;mission.ticks=rate();return true;}
        const auto carrier=entityRef(id);const int owner=e.player;
        const auto released=releaseContainer(id,ReleaseKind::Unload,1);
        if(!valid(carrier))return true;
        // Cell-entry callbacks may delete or replace the carrier. Reacquire
        // its generation-bound identity after every spawn/callback.
        if(ents[id].nativeTransportUnload.phase==NativeTransportUnloadPhase::Release)
            ents[id].nativeTransportUnload.ticks=rate();
        if(released.released)emitReleaseResult(owner,id,ReleaseKind::Unload,released);
        return true;
    }
    case NativeTransportUnloadPhase::Finish:
        rate(); // original stage4 also takes the common mission-rate path
        mission={};
        if(e.cargo.empty() && e.player>=0)eva(e.player,TR(S::EvaUnloadDone));
        return true;
    case NativeTransportUnloadPhase::None:return false;
    }
    return true;
}
