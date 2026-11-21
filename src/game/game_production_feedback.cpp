#include "game/game.h"

S Game::productionRejectText(ProductionRejectReason reason) {
    switch(reason) {
        case ProductionRejectReason::Unavailable:return S::MsgProductionUnavailable;
        case ProductionRejectReason::MissingFactory:return S::MsgProductionFactory;
        case ProductionRejectReason::MissingPrerequisite:return S::MsgProductionPrerequisite;
        case ProductionRejectReason::QueueFull:return S::MsgProductionFull;
        case ProductionRejectReason::UniqueLimit:return S::MsgProductionUnique;
        case ProductionRejectReason::Busy:return S::MsgQueueBusy;
        case ProductionRejectReason::NoOrder:return S::MsgProductionNoOrder;
        case ProductionRejectReason::Ready:return S::MsgProductionReady;
        default:return S::MsgProductionInvalid;
    }
}

std::string Game::productionFeedbackText(const ProductionOrderResult& result) const {
    std::string text;
    if(!result.accepted) text=TR(productionRejectText(result.reason));
    else if(result.action==ProductionAction::Start) text=TR(result.queued?S::MsgProductionQueued:S::MsgProductionStarted);
    else if(result.action==ProductionAction::Pause) text=TR(S::MsgProductionPaused);
    else if(result.action==ProductionAction::Resume) text=TR(S::MsgProductionResumed);
    else if(result.queued) text=TR(S::MsgProductionRemoved);
    else text=TextFormat(TR(S::MsgProductionCanceled),result.refund);
    const int count=result.unit?(int)UnitType::COUNT:(int)BldType::COUNT;
    if(world.nativeProductionType(result.type)) {
        const auto& d=*world.nativeTypes[result.type-NATIVE_PRODUCTION_BASE];
        return std::string(d.isBuilding?bldName(d.building.type):unitName(d.unit.type))+(g_lang?": ":"：")+text;
    }
    if(result.type>=0 && result.type<count)
        text=std::string(result.unit?unitName((UnitType)result.type):bldName((BldType)result.type))+(g_lang?": ":"：")+text;
    return text;
}

std::string Game::productionFacilityFeedback(bool unit,int type) const {
    const auto facilities=world.productionFacilities(localPlayer,unit,type);
    if(!facilities.present || facilities.operational) return {};
    if(facilities.frozen==facilities.present) return TR(S::TipFacilityFrozen);
    if(facilities.drained==facilities.present) return TR(S::TipFacilityDrained);
    return TR(S::TipFacilityWaiting);
}
