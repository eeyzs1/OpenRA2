#include "game/world.h"
#include <algorithm>
#include <cmath>
#include <limits>

void World::initializeFactoryVeterancy(Ent& actor,bool veteran) const {
    if(!veteran)return;
    actor.vetRank=1;
    if(veterancyProgressEnabled) {
        const double boundary=std::ceil(std::max(1,unitDef(actor).cost)*(double)g_gameRules.veteranRatio);
        actor.veterancyValue=(int)std::clamp(boundary,0.,(double)std::numeric_limits<int>::max());
    }
}
