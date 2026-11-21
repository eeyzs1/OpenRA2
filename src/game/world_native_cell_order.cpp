#include "game/world.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace {
int occupiedCell(const World& world,const World::Ent& e) {
    if(!e.alive || e.isBuilding || e.parasiting || e.magneticHeight>0
        || (e.airborne() && e.state!=UState::DeployLanding)
        || !std::isfinite(e.x) || !std::isfinite(e.y)
        || e.x<0 || e.y<0 || e.x>=world.map.w || e.y>=world.map.h)return -1;
    return world.cellIdx((int)e.x,(int)e.y);
}
}

bool World::nativeCellOrderEnabled() const {
    return nativeWeaponCatalog && nativeWeaponCatalog->version>=2;
}

void World::syncNativeCellOrder(Ent& e) {
    if(!nativeCellOrderEnabled())return;
    const int cell=occupiedCell(*this,e),layer=cell<0?-1:e.navLayer;
    if(cell==e.nativeOrderCell && layer==e.nativeOrderLayer)return;
    e.nativeOrderCell=cell;e.nativeOrderLayer=layer;
    if(cell<0){e.nativeCellOrder=0;return;}
    if(nativeCellSerial==std::numeric_limits<uint64_t>::max()) {
        // Preserve relative order if a very long session exhausts the counter.
        // This changes only order labels, never object-chain precedence.
        std::vector<Ent*> ordered;
        for(auto& actor:ents)if(actor.nativeCellOrder)ordered.push_back(&actor);
        std::sort(ordered.begin(),ordered.end(),[](const auto* a,const auto* b){return a->nativeCellOrder<b->nativeCellOrder;});
        nativeCellSerial=0;for(auto* actor:ordered)actor->nativeCellOrder=++nativeCellSerial;
    }
    e.nativeCellOrder=++nativeCellSerial;
}

EID World::nativeCellFirstObject(int x,int y) const {
    if(!map.inBounds(x,y))return INVALID_EID;
    EID first=INVALID_EID;uint64_t order=0;
    for(EID id=unitOccHead[cellIdx(x,y)];id!=INVALID_EID;id=ents[id].occNext) {
        const auto& e=ents[id];
        // The source shrapnel routine reads FirstObject, not AltObject on a
        // bridge. Movable objects enter at the head; buildings enter at the tail.
        if(e.alive && e.nativeOrderLayer==0 && e.nativeCellOrder>order){first=id;order=e.nativeCellOrder;}
    }
    return first!=INVALID_EID?first:bldAt(x,y);
}

bool World::validateNativeCellOrder() const {
    const bool enabled=nativeCellOrderEnabled();std::set<uint64_t> used;
    if(!enabled && nativeCellSerial)return false;
    for(const auto& e:ents) {
        if(!enabled){if(e.nativeCellOrder || e.nativeOrderCell!=-1 || e.nativeOrderLayer!=-1)return false;continue;}
        const int cell=occupiedCell(*this,e),layer=cell<0?-1:e.navLayer;
        if(e.nativeOrderCell!=cell || e.nativeOrderLayer!=layer)return false;
        if(cell<0){if(e.nativeCellOrder)return false;}
        else if(!e.nativeCellOrder || e.nativeCellOrder>nativeCellSerial || !used.insert(e.nativeCellOrder).second)return false;
    }
    return true;
}

bool World::writeNativeCellOrder(FILE* file) const {
    if(!file || !validateNativeCellOrder())return false;
    const uint32_t header=0x4f4c4543,version=1,footer=0x454e444f;
    const uint8_t enabled=nativeCellOrderEnabled();
    if(std::fwrite(&header,4,1,file)!=1 || std::fwrite(&version,4,1,file)!=1 || std::fwrite(&enabled,1,1,file)!=1)return false;
    uint32_t bytes=13;
    if(enabled) {
        const uint32_t count=(uint32_t)ents.size();bytes+=12+count*8;
        if(std::fwrite(&nativeCellSerial,8,1,file)!=1 || std::fwrite(&count,4,1,file)!=1)return false;
        for(const auto& e:ents)if(std::fwrite(&e.nativeCellOrder,8,1,file)!=1)return false;
    }
    return std::fwrite(&footer,4,1,file)==1 && std::fwrite(&bytes,4,1,file)==1;
}

bool World::readNativeCellOrder(FILE* file) {
    uint32_t header=0,version=0,footer=0,bytes=0,expected=13;uint8_t enabled=0;
    if(!file || std::fread(&header,4,1,file)!=1 || std::fread(&version,4,1,file)!=1 || std::fread(&enabled,1,1,file)!=1
        || header!=0x4f4c4543 || version!=1 || enabled>1 || (enabled!=0)!=nativeCellOrderEnabled())return false;
    nativeCellSerial=0;for(auto& e:ents){e.nativeCellOrder=0;e.nativeOrderCell=e.nativeOrderLayer=-1;}
    if(enabled) {
        uint32_t count=0;
        if(std::fread(&nativeCellSerial,8,1,file)!=1 || std::fread(&count,4,1,file)!=1 || count!=ents.size())return false;
        expected+=12+count*8;
        for(auto& e:ents) {
            if(std::fread(&e.nativeCellOrder,8,1,file)!=1)return false;
            e.nativeOrderCell=occupiedCell(*this,e);e.nativeOrderLayer=e.nativeOrderCell<0?-1:e.navLayer;
        }
    }
    return std::fread(&footer,4,1,file)==1 && footer==0x454e444f && std::fread(&bytes,4,1,file)==1
        && bytes==expected && validateNativeCellOrder();
}
