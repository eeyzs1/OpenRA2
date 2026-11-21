#pragma once
#include "game/map.h"
#include "core/ini.h"
#include <array>
#include <string_view>

namespace OriginalSkirmishMaps {
struct Choice {const char* id;int width,height,players;};
inline constexpr std::array<Choice,2> choices{{{"mp08t2",40,80,2},{"mp15s4",98,99,4}}};
const Choice* find(std::string_view id);
struct Data {
    const Choice* choice=nullptr;
    Map terrain;
    Ini spec;
    std::string terrainPath,terrainHash;
    std::vector<Vec2i> starts;
    int types=0,actors=0,overrides=0;
};
bool load(std::string_view id,Data& result,int players=2);
bool composeRules(const std::string& rules,const Data& map,std::string& result);
}
