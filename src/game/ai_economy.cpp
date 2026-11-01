#include "game/ai.h"

bool SkirmishAI::recoverEconomy(World& world) {
    if(world.skirmishMode==SkirmishMode::Megawealth)return false;
    bool receiver=false,collector=false;
    for(const auto& unit:world.ents) {
        if(!unit.alive || unit.player!=player)continue;
        if(!unit.isBuilding && unitDef(unit.utype).canHarvet())collector=true;
        if(unit.isBuilding && unit.btype==BldType::OreRefinery && unit.constructAnim<=0 && !unit.selling)receiver=true;
    }
    const auto type=harvesterType(world.players[player].faction);
    const auto& def=unitDef(type);
    auto& p=world.players[player];
    auto cancelUnits=[&]() {
        for(int category=0;category<PROD_CAT_N;++category) {
            while(!p.unitQueue[category].empty())world.cancelUnitProd(player,(UnitType)p.unitQueue[category].back());
            if(p.unitProd[category].active)world.cancelUnitProd(player,(UnitType)p.unitProd[category].typeIdx);
        }
    };
    auto rebuild=[&](BldType building) {
        if(!world.modeAllowsBuilding(player,building) || !world.prereqMet(player,bldDef(building))
            || !world.hasBld(player,BldType::ConYard))return false;
        for(const auto& entity:world.ents)
            if(entity.alive && entity.isBuilding && entity.player==player && entity.btype==building
                && !entity.selling && entity.constructAnim>0)return true;
        // A paid, ready building can be placed by update before another order
        // replaces it. Do not repeatedly cancel finished infrastructure.
        if(p.bldProd.active && p.bldProd.ready)return true;
        if(p.bldProd.active && p.bldProd.typeIdx!=(int)building)
            world.cancelBldProd(player,(BldType)p.bldProd.typeIdx);
        cancelUnits();
        if(p.defProd.active)world.cancelBldProd(player,(BldType)p.defProd.typeIdx);
        if(!p.bldProd.active)world.startBldProd(player,building);
        else world.holdBldProd(player,building,false);
        return p.bldProd.active;
    };
    // Restore the receiver even while surviving miners still carry cargo.
    // Custom build orders and optional technology must not starve this rebuild.
    if(!receiver && (collector || !world.hasBld(player,BldType::OreRefinery))) {
        if(rebuild(BldType::OreRefinery))return true;
    }
    if(p.powerSabotage<=0 && p.powerMade<p.powerUsed) {
        const auto power=p.faction==Faction::Allies?BldType::PowerPlant:
            p.faction==Faction::Yuri?BldType::BioReactor:BldType::TeslaReactor;
        if(rebuild(power))return true;
    }
    if(collector)return false;
    // Fusion Yuri refineries replenish free slaves every 90 ticks. Let that
    // existing recovery work instead of liquidating production to buy a miner.
    if(receiver && world.players[player].faction==Faction::Yuri)return false;
    // Recovery needs a real collector production path and a completed receiver.
    // Missing facilities still belong to the normal base rebuilding policy.
    if(!receiver)return false;
    if(!world.hasFactoryFor(player,def))return rebuild(BldType::WarFactory);
    if(!world.unitPrereqMet(player,def))return false;
    const int category=def.prodCat();
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
