#include "game/world.h"

DeployOrderResult World::orderDeployment(const std::vector<EID>& selection,bool modesOnly) {
    DeployOrderResult result;std::vector<uint8_t> seen(ents.size(),0);
    for(EID id:selection) {
        if(id<0 || id>=(EID)seen.size() || seen[id]) continue;
        seen[id]=1;
        if(!valid(id)) { ++result.rejected;if(result.reason==DeployRejectReason::None) { result.reason=DeployRejectReason::InvalidTarget;result.failedEntity=id; }continue; }
        const auto& unit=ents[id];
        const bool yard=unit.isBuilding && unit.btype==BldType::ConYard;
        const bool transform=yard || (!unit.isBuilding && (unit.utype==UnitType::MCV || unit.utype==UnitType::SlaveMiner));
        const bool toggle=!unit.isBuilding && (unit.utype==UnitType::Desolator || unit.utype==UnitType::GI
            || unit.utype==UnitType::GuardianGI || unit.utype==UnitType::SiegeChopper || unit.utype==UnitType::V3Launcher);
        const float x=unit.x+(unit.isBuilding?bldDef(unit.btype).w/2.0f:0),y=unit.y+(unit.isBuilding?bldDef(unit.btype).h/2.0f:0);
        DeployRejectReason reason=DeployRejectReason::None;
        if((!transform && !toggle) || (modesOnly && transform)) reason=DeployRejectReason::Unsupported;
        else if(unit.immobilized()) reason=DeployRejectReason::Immobilized;
        else if(unit.deploying()) reason=DeployRejectReason::Busy;
        else if(transform && (unit.mindBy!=INVALID_EID || unit.permaControlled)) reason=DeployRejectReason::Controlled;
        else if(yard && !mcvRepacks) reason=DeployRejectReason::RepackingDisabled;
        else if(!unit.isBuilding && unit.utype==UnitType::MCV && !canDeployMcv(id)) reason=DeployRejectReason::Blocked;
        else if(!unit.isBuilding && unit.utype==UnitType::SiegeChopper && !unit.deployed
            && (!passableFor((int)unit.x,(int)unit.y,0) || bldBlocked((int)unit.x,(int)unit.y)
                || cellHardBlockedForMove((int)unit.x,(int)unit.y,id))) reason=DeployRejectReason::Blocked;
        if(result.feedbackEntity<0) { result.feedbackEntity=id;result.x=x;result.y=y; }
        if(reason!=DeployRejectReason::None) {
            ++result.rejected;
            if(result.reason==DeployRejectReason::None) { result.reason=reason;result.failedEntity=id;result.feedbackEntity=id;result.x=x;result.y=y; }
            continue;
        }
        const bool expanded=!unit.isBuilding && unit.utype==UnitType::MCV;
        EID replacement=id;
        if(transform) replacement=orderDeploy(id);else orderRadDeploy({id});
        if(!valid(replacement)) {
            ++result.rejected;
            if(result.reason==DeployRejectReason::None) { result.reason=DeployRejectReason::InvalidTarget;result.failedEntity=id; }
            continue;
        }
        ++result.accepted;
        if(yard || expanded) {
            if(yard) ++result.packed;else ++result.expanded;
            result.changes.push_back({id,replacement,expanded});
        } else if(ents[replacement].deploying()) {
            if(ents[replacement].state==UState::DeployLanding)++result.deploying;else ++result.undeploying;
        } else if(ents[replacement].deployed || ents[replacement].radDeployed) ++result.deployed;
        else ++result.undeployed;
    }
    return result;
}

void World::updateChopperDeployment(Ent& e,EID id) {
    if(--e.deployAnim>0)return;
    if(e.state==UState::DeployLanding) {
        if(!passableFor((int)e.x,(int)e.y,0) || bldBlocked((int)e.x,(int)e.y)
            || cellHardBlockedForMove((int)e.x,(int)e.y,id)) {
            e.state=UState::DeployTakeoff;e.deployAnim=g_gameRules.chopperLandingTicks;
            DeployOrderResult result;result.rejected=1;result.reason=DeployRejectReason::Blocked;
            result.failedEntity=result.feedbackEntity=id;result.x=e.x;result.y=e.y;emitDeployResult(e.player,result);
        } else {
            e.deployed=true;e.state=UState::DeployUnfolding;e.deployAnim=g_gameRules.chopperDeployTicks;
        }
    } else if(e.state==UState::DeployUnfolding) {
        e.state=UState::Idle;
        DeployOrderResult result;result.accepted=result.deployed=1;result.feedbackEntity=id;result.x=e.x;result.y=e.y;
        emitDeployResult(e.player,result);
    } else if(e.state==UState::DeployFolding) {
        e.deployed=false;e.state=UState::DeployTakeoff;e.deployAnim=g_gameRules.chopperLandingTicks;
    } else {
        e.state=UState::Circling;e.goalX=e.x;e.goalY=e.y;
        DeployOrderResult result;result.accepted=result.undeployed=1;result.feedbackEntity=id;result.x=e.x;result.y=e.y;
        emitDeployResult(e.player,result);
    }
    unitOccSync(id);airOccSync(id);
}

void World::emitDeployResult(int player,const DeployOrderResult& result) {
    if(!result.accepted && !result.rejected) return;
    PresentationEvent event;event.kind=PresentationKind::DeployOrder;event.tick=tick;event.sequence=presentationSequence++;
    event.player=player;event.entity=result.feedbackEntity;event.x=result.x;event.y=result.y;event.deploy=result;
    event.positional=map.inBounds((int)event.x,(int)event.y);
    if(valid(event.entity))captureVisualTarget(event,ents[event.entity]);
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(std::move(event));
}
