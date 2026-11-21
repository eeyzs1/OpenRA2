#include "game/world.h"
#include "game/world_priv.h"

ServiceRejectReason World::serviceRejection(EID id,EID target) const {
    if(!valid(target) || !ents[target].isBuilding || ents[target].selling) return ServiceRejectReason::InvalidTarget;
    if(!serviceFacility(ents[target].btype) || ents[target].constructAnim>0) return ServiceRejectReason::Unsupported;
    if(!valid(id) || ents[id].isBuilding) return ServiceRejectReason::Incompatible;
    const auto& unit=ents[id];const auto& def=unitDef(unit);
    if(unit.player<0 || unit.player!=ents[target].player) return ServiceRejectReason::WrongOwner;
    if(def.isInfantry() || def.isAir() || def.pathDomain()!=0 || def.canHarvet()) return ServiceRejectReason::Incompatible;
    if(unit.deployed || unit.radDeployed) return ServiceRejectReason::Deployed;
    if(unit.hp>=maxHpFor(unit,def) && unit.parasite==INVALID_EID) return ServiceRejectReason::FullHealth;
    return ServiceRejectReason::None;
}

float World::serviceReach(const Ent& depot) const {
    // Stand beside the footprint; the former extra two cells allowed remote repair.
    const auto& def=bldDef(depot);
    return std::max(def.w,def.h)/2.f+0.75f;
}

ServiceOrderResult World::orderService(const std::vector<EID>& selected,EID depot,bool feedback) {
    ServiceOrderResult result;std::vector<uint8_t> seen(ents.size(),0);
    for(EID id:selected) {
        if(!valid(id) || seen[id]) continue;
        seen[id]=1;auto reason=serviceRejection(id,depot);std::vector<Vec2i> path;
        const bool continuing=ents[id].state==UState::Servicing && ents[id].target==depot;
        if(reason==ServiceRejectReason::None && !continuing
            && !interactionPath(ents[id],ents[depot],serviceReach(ents[depot]),path)) reason=ServiceRejectReason::Unreachable;
        if(reason!=ServiceRejectReason::None) {
            ++result.rejected;
            if(result.reason==ServiceRejectReason::None) { result.reason=reason;result.failedEntity=id; }
            continue;
        }
        auto& unit=ents[id];
        unit.attackMove={};
        if(!continuing) {
            unit.path=std::move(path);unit.pathIdx=0;unit.wps.clear();unit.blockTick=0;
            unit.target=depot;unit.state=UState::Servicing;unit.guard=false;
            unit.engineerIntent=EngineerIntent::None;
            unit.feedbackEngineerTarget=unit.feedbackEntryTarget=unit.feedbackAttackTarget=INVALID_EID;
        }
        if(feedback) { unit.feedbackServiceTarget=depot;unit.feedbackServiceStatus=ServiceStatus::Accepted; }
        ++result.accepted;
    }
    return result;
}

void World::emitServiceResult(int player,EID depot,const ServiceOrderResult& result,ServiceStatus status) {
    if(!result.accepted && !result.rejected) return;
    PresentationEvent event;event.kind=PresentationKind::ServiceOrder;event.tick=tick;event.sequence=presentationSequence++;
    event.player=player;event.entity=result.failedEntity>=0?result.failedEntity:depot;event.service=result;event.serviceStatus=status;
    if(valid(depot)) {
        const auto& target=ents[depot];event.positional=true;event.x=target.x;event.y=target.y;
        if(target.isBuilding) { event.x+=bldDef(target).w/2.f;event.y+=bldDef(target).h/2.f; }
    }
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(event);
}

void World::endService(Ent& unit,EID id,ServiceRejectReason reason) {
    if(unit.feedbackServiceTarget!=INVALID_EID && unit.feedbackServiceTarget==unit.target)
        emitServiceResult(unit.player,unit.target,reason==ServiceRejectReason::None?ServiceOrderResult{1,0,id}:
            ServiceOrderResult{0,1,id,reason},reason==ServiceRejectReason::None?ServiceStatus::Completed:ServiceStatus::Failed);
    unit.feedbackServiceTarget=INVALID_EID;unit.target=INVALID_EID;unit.clearPath();unit.wps.clear();unit.blockTick=0;unit.state=UState::Idle;
}

void World::updateService(Ent& unit,EID id) {
    const EID serviceTarget=unit.target;
    const auto reason=serviceRejection(id,unit.target);
    if(reason!=ServiceRejectReason::None) {
        endService(unit,id,reason==ServiceRejectReason::FullHealth?ServiceRejectReason::None:reason);return;
    }
    const auto& depot=ents[unit.target];
    if(interactionWithinReach(unit,depot,serviceReach(depot))) {
        unit.clearPath();unit.blockTick=0;
        auto status=[&](ServiceStatus next) {
            if(unit.feedbackServiceTarget==unit.target && unit.feedbackServiceStatus!=next)
                emitServiceResult(unit.player,unit.target,{1,0,id},next);
            unit.feedbackServiceStatus=next;
        };
        if(players[unit.player].lowPower()) { status(ServiceStatus::WaitingPower);return; }
        if(unit.parasite!=INVALID_EID) {
            const EID drone=unit.parasite;unit.parasite=INVALID_EID;
            if(valid(drone)) kill(drone);
            // Death scripts may kill either participant or issue a replacement order.
            if(!unit.alive || unit.state!=UState::Servicing || unit.target!=serviceTarget) return;
            const auto afterRemoval=serviceRejection(id,unit.target);
            if(afterRemoval!=ServiceRejectReason::None && afterRemoval!=ServiceRejectReason::FullHealth) {
                endService(unit,id,afterRemoval);return;
            }
        }
        const int maxHp=maxHpFor(unit,unitDef(unit));
        if(unit.hp>=maxHp) { endService(unit,id,ServiceRejectReason::None);return; }
        if(players[unit.player].money<6) { status(ServiceStatus::WaitingMoney);return; }
        status(ServiceStatus::Working);
        if(tick%5==0) {
            players[unit.player].money-=6;unit.hp=std::min(maxHp,unit.hp+12);
            if(unit.hp>=maxHp) endService(unit,id,ServiceRejectReason::None);
        }
        return;
    }
    if(unit.pathIdx>=(int)unit.path.size() || (tick+(uint64_t)id)%30==0) {
        if(repathsThisTick>=8) return;
        ++repathsThisTick;++repathRequests;std::vector<Vec2i> path;
        if(!interactionPath(unit,depot,serviceReach(depot),path)) {
            ++repathFailures;endService(unit,id,ServiceRejectReason::Unreachable);return;
        }
        unit.path=std::move(path);unit.pathIdx=0;
    }
    moveAlongPath(unit,id);
}
