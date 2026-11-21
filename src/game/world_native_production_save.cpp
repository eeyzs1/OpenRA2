#include "game/world.h"

bool World::validateNativeProductionState() const {
    if(!nativeProductionRules.available && nativeProductionSerial)return false;
    for(const auto& player:players) {
        if(nativeProductionRules.economy && (player.stolenTech&~7))return false;
        const auto valid=[&](const ProdItem& item,bool unit,int category) {
            if(!validProductionType(unit,item.typeIdx))return false;
            if(!item.active)return true;
            if(nativeProductionRules.available && !nativeProductionType(item.typeIdx))return false;
            if(!nativeProductionType(item.typeIdx))return true;
            if(item.isUnit!=unit)return false;
            return productionCategory(unit,item.typeIdx)==category && item.progress>=0 && item.progress<=54
                && item.ready==(item.progress==54) && item.paid>=0 && item.paid<=item.totalCost
                // Version 2 freezes the actual quote when the order starts.
                // The discounting plant may subsequently lose power, be sold
                // or change owner, so current modifiers cannot validate it.
                && (nativeProductionRules.economy || item.totalCost==(unit?nativeTypes[item.typeIdx-NATIVE_PRODUCTION_BASE]->unit.cost:nativeTypes[item.typeIdx-NATIVE_PRODUCTION_BASE]->building.cost))
                && (!item.ready || item.paid==item.totalCost) && item.workRemainder>=0 && item.workRemainder<=510;
        };
        if(!valid(player.bldProd,false,4) || !valid(player.defProd,false,5))return false;
        for(int category=0;category<PROD_CAT_N;++category) {
            if(!valid(player.unitProd[category],true,category))return false;
            for(int selector:player.unitQueue[category])if(!validProductionType(true,selector)
                || productionCategory(true,selector)!=category || (nativeProductionRules.available && !nativeProductionType(selector)))return false;
        }
    }
    return true;
}

bool World::writeNativeProductionState(FILE* file) const {
    if(!validateNativeProductionState())return false;
    const uint8_t available=nativeProductionRules.available;if(fwrite(&available,1,1,file)!=1)return false;
    return !available || fwrite(&nativeProductionSerial,sizeof(nativeProductionSerial),1,file)==1;
}

bool World::readNativeProductionState(FILE* file) {
    uint8_t available=0;if(fread(&available,1,1,file)!=1 || available>1 || (available!=0)!=nativeProductionRules.available)return false;
    nativeProductionSerial=0;
    return (!available || fread(&nativeProductionSerial,sizeof(nativeProductionSerial),1,file)==1) && validateNativeProductionState();
}
