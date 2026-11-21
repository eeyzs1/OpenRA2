#include "game/ai.h"
#include <algorithm>

bool SkirmishAI::produceSourceAircraft(World& world,int category) {
    if(!world.usesRulesProfile() || world.unitQueuedCount(player,category)>=2)return false;
    const auto& p=world.players[player];UnitType type=UnitType::COUNT;int limit=2;
    if(category==1){
        if(p.faction==Faction::Soviet)type=UnitType::Kirov;
        else if(p.faction==Faction::Yuri)type=UnitType::FloatingDisc;
        else if(p.faction==Faction::Allies){type=UnitType::Nighthawk;limit=1;}
    }else if(category==2 && p.faction==Faction::Allies)
        type=p.country==Country::Korea?UnitType::BlackEagle:UnitType::Intruder;
    if(type==UnitType::COUNT)return false;
    // Original AI/AI(MD) triggers include easy ORCA teams; the other aircraft
    // in this roster have only medium/hard teams.
    if(difficulty==AIDiff::Easy && type!=UnitType::Intruder)return false;
    const int selector=world.productionSelector(player,true,(int)type);
    if(selector<0 || !world.nativeProductionType(selector) || world.productionCategory(true,selector)!=category)return false;
    if(!world.nativeTypes[selector-NATIVE_PRODUCTION_BASE]->unit.isAir())return false;
    const auto& item=p.unitProd[category];
    const int planned=world.countUnits(player,type)+(item.active && item.typeIdx==selector)
        +(int)std::count(p.unitQueue[category].begin(),p.unitQueue[category].end(),selector);
    if(planned>=limit || world.productionStartRejection(player,true,selector)!=ProductionRejectReason::None)return false;
    if(type==UnitType::Nighthawk){
        const auto& jumpjet=world.nativeTypes[selector-NATIVE_PRODUCTION_BASE]->jumpjet;
        if(!world.nativeJumpjetEnabled || !jumpjet.available || jumpjet.version<2 || jumpjet.balloon
            || !world.hasBld(player,BldType::Barracks) || !needsTransport(world))return false;
    }
    world.startUnitProd(player,type);return true;
}
