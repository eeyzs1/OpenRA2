#include "game/ai.h"
#include <algorithm>

namespace {
const UnitDef& product(const World& world,int selector) {
    return world.nativeProductionType(selector)?world.nativeTypes[selector-NATIVE_PRODUCTION_BASE]->unit:unitDef((UnitType)selector);
}
bool supports(const World& world,const World::Ent& factory,int selector) {
    return world.nativeProductionType(selector)?world.nativeFactorySupports(factory,selector-NATIVE_PRODUCTION_BASE):isFactoryFor(factory.btype,product(world,selector));
}
}

bool SkirmishAI::constructionSitePreservesExits(const World& world,const BldDef& building,int x,int y) const {
    const auto covered=[&](Vec2i point){return point.x>=x && point.x<x+building.w && point.y>=y && point.y<y+building.h;};
    for(EID id=0;id<(EID)world.ents.size();++id) {
        const auto& factory=world.ents[id];
        if(!factory.alive || !factory.isBuilding || factory.player!=player || factory.selling)continue;
        const auto preserves=[&](const UnitDef& unit) {
            if(unit.isAir() && unit.ammo>0)return true;
            const auto exits=world.factoryExitCells(id,unit);
            return exits.empty() || std::any_of(exits.begin(),exits.end(),[&](Vec2i point){return !covered(point);});
        };
        // Protect actual paid products, including captured factory identities
        // and amphibious products whose movement domain differs from a tank.
        for(const auto& order:world.players[player].unitProd)
            if(order.active && world.validProductionType(true,order.typeIdx) && supports(world,factory,order.typeIdx)
                && !preserves(product(world,order.typeIdx)))return false;
        int category=-1;
        if(factory.nativeIdentity.definition && factory.nativeIdentity.definition->production.available) {
            const auto& profile=factory.nativeIdentity.definition->production;
            category=profile.factory==1 && profile.naval?3:profile.factory;
        } else if(factory.btype==BldType::Barracks || factory.btype==BldType::CloningVat)category=0;
        else if(factory.btype==BldType::WarFactory)category=1;
        else if(factory.btype==BldType::NavalYard)category=3;
        if(category==0 && !preserves(unitDef(UnitType::GI)))return false;
        if(category==1 && !preserves(unitDef(UnitType::MCV)))return false;
        if(category==3 && !preserves(unitDef(UnitType::Typhoon)))return false;
    }
    if(building.type==BldType::WarFactory) {
        // A new factory needs a passable door and one way out of its apron.
        // Ignore temporary unit occupancy, which exit-clearing already owns.
        const auto& unit=unitDef(UnitType::MCV);const int ex=x+building.w/2,ey=y+building.h;
        const int layer=world.map.preferredLayer(ex,ey,unit.pathDomain());
        if(!world.map.surfacePassable(ex,ey,unit.pathDomain(),layer,true,unit.fitsUnderBridge()))return false;
        constexpr Vec2i directions[]={{0,1},{-1,0},{1,0},{0,-1}};
        for(auto delta:directions) {
            const Vec2i next{ex+delta.x,ey+delta.y};
            if(!covered(next) && world.map.stepLayer(ex,ey,layer,next.x,next.y,unit.pathDomain(),true,unit.fitsUnderBridge())>=0)return true;
        }
        return false;
    }
    return true;
}

bool SkirmishAI::canDeployConstructionVehicleAt(const World& world,EID id,int x,int y) const {
    if(!world.canDeployMcvAt(id,x,y))return false;
    const int type=world.usesRulesProfile()?world.rulesTransformIndex(world.ents[id]):-1;
    const auto& building=type>=0?world.nativeTypes[type]->building:bldDef(BldType::ConYard);
    return constructionSitePreservesExits(world,building,x-1,y-1);
}

bool SkirmishAI::recoverFactoryProduction(World& world) {
    auto& house=world.players[player];
    World::Ent replacement;replacement.alive=replacement.isBuilding=true;replacement.player=player;replacement.btype=BldType::WarFactory;
    if(world.usesRulesProfile()) {
        const int type=world.rulesTypeIndex(player,false,(int)BldType::WarFactory);
        if(type<0)return false;
        replacement.nativeIdentity.type=type;replacement.nativeIdentity.definition=world.nativeTypes[type];
    }
    bool blocked=false;
    for(const auto& order:house.unitProd) {
        if(!order.active || !order.ready || !world.validProductionType(true,order.typeIdx)
            || world.productionCategory(true,order.typeIdx)!=1 || !supports(world,replacement,order.typeIdx))continue;
        const auto& unit=product(world,order.typeIdx);
        if(unit.isAir())continue;
        bool present=false,open=false;
        for(EID id=0;id<(EID)world.ents.size();++id) {
            const auto& factory=world.ents[id];
            if(!factory.alive || !factory.isBuilding || factory.player!=player || factory.selling || !supports(world,factory,order.typeIdx))continue;
            present=true;
            if(!world.factoryExitCells(id,unit).empty()){open=true;break;}
        }
        if(present && !open){blocked=true;break;}
    }
    if(!blocked || !world.hasBld(player,BldType::ConYard) || !world.modeAllowsBuilding(player,BldType::WarFactory)
        || !world.prereqMet(player,bldDef(BldType::WarFactory)))return false;
    // Finish any existing paid building first. A ready but unplaceable backup
    // remains queued; never cancel the vehicle or enqueue repeated factories.
    if(!house.bldProd.active && house.money>=Credits(world.buildingProductionCost(player,BldType::WarFactory)))
        world.startBldProd(player,BldType::WarFactory);
    return true;
}
