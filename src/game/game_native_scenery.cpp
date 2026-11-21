#include "game/game.h"
#include "gfx/sprites.h"

namespace {
Color rgbColor(uint32_t value) {return Color{(uint8_t)(value>>16),(uint8_t)(value>>8),(uint8_t)value,255};}
}

Color Game::radarHouseColor(int player,int fallback) const {
    if(!world.nativeScenarioSource.empty() && !world.usesRulesProfile() && player>=0 && player<world.numPlayers)return rgbColor(world.nativeColors[player]);
    return fallback>=0 && fallback<8?HOUSE_COLORS[fallback]:GRAY;
}

void Game::drawNativeSceneryRadar(float scale) const {
    if(world.nativeScenarioSource.empty())return;
    auto draw=[&](int x,int y,int type,bool remembered) {
        if(type<0 || type>=(int)world.nativeTypes.size())return;
        const auto& spec=*world.nativeTypes[type];if(!spec.kind)return;
        Color color=rgbColor(spec.radarRGB);if(remembered){color.r/=2;color.g/=2;color.b/=2;}
        DrawRectangle((int)(x*scale),(int)(y*scale),(int)(spec.building.w*scale)+1,(int)(spec.building.h*scale)+1,color);
    };
    for(const auto& [key,memory]:fogKnowledge.buildings)
        if(!buildingVisible(memory.x,memory.y,memory.type,memory.nativeType) && buildingExplored(memory))
            draw(memory.x,memory.y,memory.nativeType,true);
    for(const auto& actor:world.ents)
        if(actor.alive && actor.nativeIdentity.definition && actor.nativeIdentity.definition->kind
            && buildingVisible((int)actor.x,(int)actor.y,actor.btype,actor.nativeIdentity.type))
            draw((int)actor.x,(int)actor.y,actor.nativeIdentity.type,false);
}
