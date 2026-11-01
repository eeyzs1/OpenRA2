#include "game/ai.h"
#include <algorithm>

std::vector<EID> SkirmishAI::assignAirDefense(World& w) {
    std::vector<EID> threats,responding;
    for(size_t id=0;id<w.ents.size();++id) {
        const auto& enemy=w.ents[id];
        if(!enemy.alive || enemy.isBuilding || !w.isEnemy(player,enemy.player) || !enemy.airborne()
            || w.map.fogAt(player,(int)enemy.x,(int)enemy.y)!=FOG_VISIBLE)continue;
        bool nearAsset=false;
        for(const auto& asset:w.ents) {
            if(!asset.alive || !asset.isBuilding || asset.player!=player || asset.selling)continue;
            const auto& def=bldDef(asset.btype);
            if(distf(enemy.x,enemy.y,asset.x+def.w/2.f,asset.y+def.h/2.f)<=10){nearAsset=true;break;}
        }
        if(nearAsset)threats.push_back((EID)id);
    }
    if(threats.empty())return responding;
    struct Candidate { EID unit,target;float distance;bool immediate; };
    std::vector<Candidate> candidates;
    for(size_t id=0;id<w.ents.size();++id) {
        const auto& unit=w.ents[id];
        if(!unit.alive || unit.isBuilding || unit.player!=player || unit.immobilized() || unit.parasiting
            || unitDef(unit.utype).isAir() || unitDef(unit.utype).canHarvet()
            || unit.state==UState::Boarding || unit.state==UState::Servicing
            || (w.valid(unit.parasite) && w.ents[unit.parasite].utype==UnitType::Squid))continue;
        EID target=INVALID_EID;float nearest=24.f;
        for(EID threat:threats) {
            const auto& enemy=w.ents[threat];const auto weapon=w.effWeapon(unit,&enemy);
            if(weapon.damage<=0 || !weapon.antiAir || w.attackRejection((EID)id,threat)!=AttackRejectReason::None)continue;
            const float distance=distf(unit.x,unit.y,enemy.x,enemy.y);
            if(unit.guard && distance>weapon.range)continue;
            bool deferred=false;
            if(distance>weapon.range)for(const auto& failure:unit.failedPursuits)
                deferred |= failure.target==threat && w.activePursuitFailure(unit,failure);
            if(deferred || distance>=nearest)continue;
            nearest=distance;target=threat;
        }
        if(target!=INVALID_EID)candidates.push_back({(EID)id,target,nearest,nearest<=w.effWeapon(unit,&w.ents[target]).range});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
        if(a.immediate!=b.immediate)return a.immediate>b.immediate;
        if(a.distance!=b.distance)return a.distance<b.distance;
        return a.unit<b.unit;
    });
    const size_t limit=std::clamp(threats.size()*2,size_t{2},size_t{8});
    for(const auto& candidate:candidates) {
        if(responding.size()>=limit)break;
        auto& unit=w.ents[candidate.unit];const auto& target=w.ents[candidate.target];
        if(unit.target==candidate.target && (unit.state==UState::Chasing || unit.state==UState::Attacking)) {
            responding.push_back(candidate.unit);continue;
        }
        const bool approach=candidate.distance>w.effWeapon(unit,&target).range;
        if(approach && w.repathsThisTick>=8) {
            // Keep the candidate out of global regrouping while its approach
            // waits for budget; do not record a false unreachable result.
            if(unit.state==UState::Moving || unit.state==UState::AttackMoving || unit.state==UState::Chasing)
                w.orderStop({candidate.unit});
            responding.push_back(candidate.unit);continue;
        }
        if(approach){++w.repathsThisTick;++w.repathRequests;}
        const bool guarding=unit.guard;
        const auto result=w.orderAttack({candidate.unit},candidate.target);
        unit.guard=guarding;
        if(result.accepted>0)responding.push_back(candidate.unit);
        else if(result.reason==AttackRejectReason::Unreachable) {
            ++w.repathFailures;w.rememberPursuitFailure(candidate.unit,candidate.target);
        }
    }
    return responding;
}
