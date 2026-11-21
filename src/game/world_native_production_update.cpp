#include "game/world.h"
#include "game/production_work.h"
#include "game/lang.h"
#include <algorithm>
#include <limits>

int World::nativeProductionStepTicks(int player,int type) const {
    const auto& profile=nativeTypes[type]->production;
    int frames=profile.baseFrames,factories=0,operational=0;
    for(const auto& factory:ents)if(factory.player==player && nativeFactorySupports(factory,type)) {
        ++factories;if(productionFacilityOperational(factory))++operational;
    }
    if(!operational)return 0;
    const auto& p=players[player];
    // Source low-power bounds are retained. The exact RA2 power ladder and
    // per-factory/difficulty timer refresh still require executable calibration.
    if(p.lowPower()) {
        const float ratio=p.powerUsed>0?std::clamp((float)p.powerMade/p.powerUsed,0.f,1.f):1;
        const float speed=std::clamp(ratio*nativeProductionRules.powerPenalty,nativeProductionRules.minLowPower,nativeProductionRules.maxLowPower);
        frames=(int)std::min(100000000.f,frames/speed);
    }
    for(int i=1;i<factories && nativeProductionRules.multipleFactory>0;++i)
        frames=(int)std::min(100000000.f,frames*nativeProductionRules.multipleFactory);
    return 2*std::clamp(frames/54,1,255);
}

bool World::advanceNativeProduction(int player,ProdItem& item) {
    if(item.ready)return true;if(item.held)return false;
    const int interval=nativeProductionStepTicks(player,item.typeIdx-NATIVE_PRODUCTION_BASE);
    item.ready=advanceNativeProductionWork({item.workRemainder,item.progress,item.paid},players[player].money,item.totalCost,interval);
    return item.ready;
}

EID World::spawnProductionActor(int player,UnitType type,float x,float y,int nativeType) {
    if(nativeType<0)return spawnUnit(player,type,x,y);
    if(nativeProductionSerial==std::numeric_limits<uint64_t>::max())return INVALID_EID;
    const auto key="production:"+std::to_string(player)+":"+std::to_string(nativeProductionSerial++);
    return spawnNativeActor(nativeType,player,x,y,256,0,key,"","Guard");
}

void World::updateNativeUnitProduction(int player,int category) {
    auto& item=players[player].unitProd[category];
    const int type=item.typeIdx-NATIVE_PRODUCTION_BASE;const auto& unit=nativeTypes[type]->unit;
    if(advanceNativeProduction(player,item) && spawnFromFactory(player,unit,type)) {
        ++producedUnits[player];item={};auto& queue=players[player].unitQueue[category];
        if(!queue.empty()){const int next=queue.front();queue.pop_front();activateNativeProduction(player,item,next);}
        eva(player,TR(S::EvaUnitReady),EvaCue::UnitReady);
    }
}

void World::updateNativeBuildingProduction(int player,ProdItem& item) {
    if(!item.ready && advanceNativeProduction(player,item))eva(player,TR(S::EvaConstructionComplete),EvaCue::ConstructionComplete);
}
