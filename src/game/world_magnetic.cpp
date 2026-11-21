#include "game/world.h"
#include <algorithm>

bool World::updateMagnetic(Ent& unit,EID id) {
    if(unit.magneticBy==INVALID_EID && unit.magneticHeight==0) return false;
    if(valid(unit.magneticBy)) {
        const auto& source=ents[unit.magneticBy];
        if(source.utype==UnitType::Magnetron && !source.isBuilding && !source.immobilized()
            && source.target==id && isEnemy(source.player,unit.player)
            && distf(unit.x,unit.y,source.x,source.y)<=effWeapon(source,&unit).range) {
            unit.magneticHeight=std::min(12,unit.magneticHeight+1);
            // This is suspended travel: its projection may cross obstacles,
            // but the vehicle is absent from the physical ground index.
            unit.x+=(source.x-unit.x)*0.025f;unit.y+=(source.y-unit.y)*0.025f;
            return true;
        }
    }
    const int height=unit.magneticHeight;
    unit.magneticBy=INVALID_EID;unit.magneticHeight=0;
    if(height==0) return false;
    const int x=(int)unit.x,y=(int)unit.y;
    const int layer=map.preferredLayer(x,y,unitDef(unit).pathDomain());
    if(!movementPassable(unit,x,y,layer) || bldBlocked(x,y,layer) || cellHardBlockedForMove(x,y,id,layer)) {
        // A crash must not eject a parasite into an illegal ground cell.
        if(valid(unit.parasite)) kill(unit.parasite,false);
        kill(id);return true;
    }
    unit.navLayer=unit.previousNavLayer=unit.goalLayer=(uint8_t)layer;
    unitOccSync(id);airOccSync(id);
    damage(id,height*12,-1);
    return true; // Resume the retained order on the next simulation tick.
}
