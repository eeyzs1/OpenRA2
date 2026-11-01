#include "game/ai.h"

bool SkirmishAI::recoverEconomy(World& world) {
    if(world.skirmishMode==SkirmishMode::Megawealth)return false;
    bool receiver=false;
    for(const auto& unit:world.ents) {
        if(!unit.alive || unit.player!=player)continue;
        if(!unit.isBuilding && unitDef(unit.utype).canHarvet())return false;
        if(unit.isBuilding && unit.btype==BldType::OreRefinery && unit.constructAnim<=0 && !unit.selling)receiver=true;
    }
    const auto type=harvesterType(world.players[player].faction);
    const auto& def=unitDef(type);
    // Fusion Yuri refineries replenish free slaves every 90 ticks. Let that
    // existing recovery work instead of liquidating production to buy a miner.
    if(receiver && world.players[player].faction==Faction::Yuri)return false;
    // Recovery needs a real collector production path and a completed receiver.
    // Missing facilities still belong to the normal base rebuilding policy.
    if(!receiver || !world.unitPrereqMet(player,def) || !world.hasFactoryFor(player,def))return false;
    auto& p=world.players[player];const int category=def.prodCat();
    for(int cat=0;cat<PROD_CAT_N;++cat) {
        // Pending orders have no paid work. Clear them before cancelling the
        // active blocker, so queue promotion cannot put another tank in front.
        while(!p.unitQueue[cat].empty())world.cancelUnitProd(player,(UnitType)p.unitQueue[cat].back());
        auto& active=p.unitProd[cat];
        if(active.active && (cat!=category || active.typeIdx!=(int)type))
            world.cancelUnitProd(player,(UnitType)active.typeIdx);
    }
    // Reclaim actual paid work; no stipend, synthetic sale or free harvester.
    // Cancelling also prevents other queues from consuming the recovery budget.
    if(p.bldProd.active)world.cancelBldProd(player,(BldType)p.bldProd.typeIdx);
    if(p.defProd.active)world.cancelBldProd(player,(BldType)p.defProd.typeIdx);
    if(!p.unitProd[category].active)world.startUnitProd(player,type);
    else world.holdUnitProd(player,type,false);
    return true;
}
