#include "game/world.h"
#include <algorithm>
#include <cmath>

std::vector<EID> World::nativeCrewCandidates(const Ent& carrier,const Ent::GarrisonedUnit& passenger) const {
    const bool fixed=!carrier.isBuilding && nativeCrewRules.version>=2 && passenger.nativeIdentity.definition
        && passenger.nativeIdentity.definition->crew.transportWeapon>=0;
    float range=0;
    if(carrier.isBuilding && nativeCrewRules.passengerOccupy)range=(float)nativeCrewRules.occupyRange;
    else if(fixed) {
        // The fixed source slot need not be one of the ordinary four gun
        // slots. Scan using its actual rank-specific range, including bonus.
        range=nativeCrewWeapon(carrier,passenger).range;
    }
    else if(const auto* definition=carrier.isBuilding?carrier.nativeIdentity.definition.get():passenger.nativeIdentity.definition.get())
        for(const auto& gun:definition->guns.slots)if(gun.present)range=std::max(range,gun.weapon.range);
    if(!carrier.isBuilding && !fixed)range+=nativeCrewRules.openRange;
    const int radius=(int)std::ceil(range)+1+(carrier.isBuilding?std::max(bldDef(carrier).w,bldDef(carrier).h):0);
    const int cx=(int)(carrier.x+(carrier.isBuilding?bldDef(carrier).w*.5f:0));
    const int cy=(int)(carrier.y+(carrier.isBuilding?bldDef(carrier).h*.5f:0));
    std::vector<EID> result;
    // Reuse the simulation's ground chains, building footprints and air buckets.
    // Distant armies do not add a full-world scan to every passenger's ROF.
    for(int y=std::max(0,cy-radius);y<=std::min(map.h-1,cy+radius);++y)
        for(int x=std::max(0,cx-radius);x<=std::min(map.w-1,cx+radius);++x) {
            const auto cell=(size_t)cellIdx(x,y);
            if(cell<unitOccHead.size())for(EID id=unitOccHead[cell];id!=INVALID_EID;id=ents[id].occNext)result.push_back(id);
            if(cell<bldOcc.size() && bldOcc[cell]>0)result.push_back(bldOcc[cell]-1);
        }
    if(airBucketW>0 && !airOccHead.empty()) {
        const int height=(map.h+AIR_BUCKET-1)/AIR_BUCKET;
        for(int y=std::max(0,(cy-radius)/AIR_BUCKET);y<=std::min(height-1,(cy+radius)/AIR_BUCKET);++y)
            for(int x=std::max(0,(cx-radius)/AIR_BUCKET);x<=std::min(airBucketW-1,(cx+radius)/AIR_BUCKET);++x)
                for(EID id=airOccHead[(size_t)y*airBucketW+x];id!=INVALID_EID;id=ents[id].airNext)result.push_back(id);
    }
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
