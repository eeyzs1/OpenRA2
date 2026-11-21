#pragma once
#include "core/util.h"
#include <array>
#include <cstdio>
#include <string>
#include <vector>

class Map;

// Original cell identifiers are distinct from our mutable gameplay substrate.
// Never replace unknown native types with a visually similar generated tile.
struct NativeTileInfo {
    uint16_t tile=65535, auxiliary=0;
    uint8_t subtile=0, auxiliary2=0, overlay=255, overlayData=0;
    uint8_t terrainType=0, ramp=0;
};

struct NativeMapInfo {
    std::string assetId, sourceHash, theater;
    int width=0, height=0;
    std::array<int,4> local{};
    std::vector<NativeTileInfo> tiles; // dense canvas, indexed like Map::cells
    bool active() const { return !assetId.empty(); }
};

// RA2NMAP1 is an offline import format, not the World save format. All reads
// validate before assigning the destination. It does not execute mission logic.
bool loadNativeMap(const char* path,Map& destination,int players,std::vector<Vec2i>& spawns);
bool validateNativeMap(const Map& map);
bool writeNativeMapState(FILE* file,const Map& map);
bool readNativeMapState(FILE* file,Map& map);
