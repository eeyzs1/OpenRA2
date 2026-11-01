#include "game/world.h"
#include <algorithm>

ProductionRejectReason World::productionStartRejection(int player,bool unit,int type) const {
    if(player<0 || player>=numPlayers || type<0 || type>=(unit?(int)UnitType::COUNT:(int)BldType::COUNT))
        return ProductionRejectReason::InvalidType;
    const auto& p=players[player];
    if(unit) {
        const auto t=(UnitType)type;const auto& def=unitDef(t);const int cat=def.prodCat();
        if(!modeAllowsUnit(player,t)) return ProductionRejectReason::Unavailable;
        if(!hasFactoryFor(player,def)) return ProductionRejectReason::MissingFactory;
        if(!unitPrereqMet(player,def)) return ProductionRejectReason::MissingPrerequisite;
        if(isHero(t)) {
            for(const auto& e:ents) if(e.alive && e.player==player) {
                if(!e.isBuilding && e.utype==t) return ProductionRejectReason::UniqueLimit;
                if(cargoUnitCount(e.cargo,t) || cargoUnitCount(e.garrison,t))return ProductionRejectReason::UniqueLimit;
            }
            for(int c=0;c<PROD_CAT_N;++c) {
                if(p.unitProd[c].active && p.unitProd[c].typeIdx==type) return ProductionRejectReason::UniqueLimit;
                for(int queued:p.unitQueue[c]) if(queued==type) return ProductionRejectReason::UniqueLimit;
            }
        }
        if(p.unitProd[cat].active && p.unitQueue[cat].size()>=29) return ProductionRejectReason::QueueFull;
    } else {
        const auto t=(BldType)type;
        if(bldQueueFor(player,t).active) return ProductionRejectReason::Busy;
        if(!modeAllowsBuilding(player,t)) return ProductionRejectReason::Unavailable;
        if(!hasBld(player,BldType::ConYard)) return ProductionRejectReason::MissingFactory;
        if(!prereqMet(player,bldDef(t))) return ProductionRejectReason::MissingPrerequisite;
        if(isUniqueBld(t) && countBlds(player,t)>0) return ProductionRejectReason::UniqueLimit;
    }
    return ProductionRejectReason::None;
}

void World::applyProductionCommand(int player,const Cmd& command) {
    if(player<0 || player>=numPlayers) return;
    ProductionOrderResult result;result.type=command.a;
    result.unit=command.type==Cmd::StartUnitProd || command.type==Cmd::CancelUnitProd || command.type==Cmd::HoldUnitProd;
    result.action=command.type==Cmd::CancelUnitProd || command.type==Cmd::CancelBldProd?ProductionAction::Cancel:
        command.type==Cmd::HoldUnitProd || command.type==Cmd::HoldBldProd?
            (command.b?ProductionAction::Pause:ProductionAction::Resume):ProductionAction::Start;
    auto& p=players[player];
    // Preserve the explicit historical COUNT alias, never turn malformed types into a cancellation.
    if(command.type==Cmd::CancelBldProd && result.type==(int)BldType::COUNT) {
        if(p.bldProd.active) result.type=p.bldProd.typeIdx;
        else if(p.defProd.active) result.type=p.defProd.typeIdx;
        else result.reason=ProductionRejectReason::NoOrder;
    }
    const int limit=result.unit?(int)UnitType::COUNT:(int)BldType::COUNT;
    if(result.reason==ProductionRejectReason::None && (result.type<0 || result.type>=limit))
        result.reason=ProductionRejectReason::InvalidType;
    if(result.reason==ProductionRejectReason::None) {
        const auto ut=(UnitType)result.type;const auto bt=(BldType)result.type;
        ProdItem& prod=result.unit?p.unitProd[unitDef(ut).prodCat()]:bldQueueFor(player,bt);
        if(result.action==ProductionAction::Start) {
            result.reason=productionStartRejection(player,result.unit,result.type);
            if(result.reason==ProductionRejectReason::None) {
                result.queued=prod.active;
                result.accepted=result.unit?startUnitProd(player,ut):startBldProd(player,bt);
            }
        } else if(result.action==ProductionAction::Cancel) {
            if(result.unit) {
                const auto& q=p.unitQueue[unitDef(ut).prodCat()];
                result.queued=std::find(q.begin(),q.end(),result.type)!=q.end();
            }
            if(!result.queued && (!prod.active || prod.typeIdx!=result.type)) result.reason=ProductionRejectReason::NoOrder;
            else {
                const Credits money=p.money;
                if(result.unit) cancelUnitProd(player,ut);else cancelBldProd(player,bt);
                result.refund=(int)(p.money-money);result.accepted=true;
            }
        } else if(!prod.active || prod.typeIdx!=result.type) result.reason=ProductionRejectReason::NoOrder;
        else if(prod.ready) result.reason=ProductionRejectReason::Ready;
        else {
            if(result.unit) holdUnitProd(player,ut,command.b!=0);else holdBldProd(player,bt,command.b!=0);
            result.accepted=true;
        }
        result.queueLength=result.unit?unitQueuedCount(player,unitDef(ut).prodCat()):(prod.active?1:0);
    }
    PresentationEvent event;event.kind=PresentationKind::ProductionOrder;event.tick=tick;
    event.sequence=presentationSequence++;event.player=player;event.production=result;
    for(EID id=0;id<(EID)ents.size();++id) {
        const auto& e=ents[id];
        if(!e.alive || !e.isBuilding || e.player!=player) continue;
        const bool producer=result.unit?result.type>=0 && result.type<(int)UnitType::COUNT && isFactoryFor(e.btype,unitDef((UnitType)result.type)):
            e.btype==BldType::ConYard;
        if(!producer) continue;
        event.entity=id;event.x=e.x+bldDef(e.btype).w/2.f;event.y=e.y+bldDef(e.btype).h/2.f;event.positional=true;break;
    }
    if(presentationEvents.size()>=4096) presentationEvents.pop_front();
    presentationEvents.push_back(event);
}
