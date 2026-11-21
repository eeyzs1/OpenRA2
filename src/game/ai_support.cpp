#include "game/ai.h"
#include <algorithm>
#include <cstdlib>

void SkirmishAI::doIndependentSupport(World& world) {
    const KnownBuilding* target=nullptr;
    for(const auto& building:knownBuildings) {
        if(building.owner<0 || !world.isEnemy(player,building.owner))continue;
        if(!target || (building.type==BldType::ConYard && target->type!=BldType::ConYard))target=&building;
    }
    const int x=target?target->x+1:world.map.w/2,y=target?target->y+1:world.map.h/2;
    for(int i=0;i<SUPPORT_COUNT;++i) {
        const auto type=(SupportType)i;
        if(!world.supportReady(player,type))continue;
        if(type==SupportType::SpyPlane || type==SupportType::PsychicReveal) {
            world.orderSupport(player,type,x+.5f,y+.5f);continue;
        }
        if(!target)continue; // Never discover an unseen base from the live entity list.
        bool used=false;
        for(int radius=0;radius<8 && !used;++radius)
            for(int dy=-radius;dy<=radius && !used;++dy)
                for(int dx=-radius;dx<=radius && !used;++dx)
                    if(std::max(std::abs(dx),std::abs(dy))==radius)
                        used=world.orderSupport(player,type,x+dx+.5f,y+dy+.5f);
    }
}
