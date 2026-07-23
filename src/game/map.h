#pragma once
#include "core/util.h"
#include <vector>
#include <cstdint>

// 地形类型
enum class Terrain : uint8_t {
    Clear = 0,   // 草地/平地
    Rough,       // 粗糙地面（可通行）
    Water,       // 水面（不可通行，除非海军）
    Ore,         // 金矿
    Gems,        // 彩矿（更值钱）
};

// 地表装饰物
enum class Overlay : uint8_t {
    None = 0,
    Tree1, Tree2, Tree3,
    Rock1, Rock2,
};

struct Cell {
    Terrain terrain = Terrain::Clear;
    Overlay overlay = Overlay::None;
    uint8_t variant = 0;      // 瓦片贴图变体
    int16_t ore = 0;          // 剩余矿石量
    int16_t oreMax = 0;       // 矿石上限（生成时记录，用于矿脉缓慢再生）
    bool passable() const {
        return terrain != Terrain::Water && overlay != Overlay::Rock1 && overlay != Overlay::Rock2;
    }
};

// 战争迷雾状态
enum FogState : uint8_t { FOG_UNSEEN = 0, FOG_SEEN = 1, FOG_VISIBLE = 2 };

class Map {
public:
    int w = 0, h = 0;
    std::vector<Cell> cells;
    // 每玩家迷雾: [player][cell]
    std::vector<std::vector<uint8_t>> fog;

    // mapType: 0 大陆（默认，散布湖区）1 岛屿（出生点各自成岛，中央争夺岛）2 湖泊（中央大湖环绕陆地）
    void generate(int w_, int h_, uint64_t seed, int numPlayers, std::vector<Vec2i>& outSpawns, int mapType = 0);

    inline bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    inline Cell& at(int x, int y) { return cells[(size_t)y * w + x]; }
    inline const Cell& at(int x, int y) const { return cells[(size_t)y * w + x]; }

    bool passable(int x, int y) const { return inBounds(x, y) && cells[(size_t)y * w + x].passable(); }

    // 采集矿石：返回采集到的量
    int harvestAt(int x, int y, int want);

    // 找最近的矿石格
    bool findNearestOre(int sx, int sy, int maxR, Vec2i& out) const;

    // A* 寻路（8 方向）；domain: 0 陆地 1 水面 2 两栖
    // 返回路径点（含终点，不含起点）；失败返回 false
    bool findPath(int sx, int sy, int tx, int ty, std::vector<Vec2i>& outPath, int maxNodes = 20000, int domain = 0) const;

    // 建筑占用表（由 World 挂载，寻路时视为不可通行）；元素为 eid+1，0/-1 为无建筑
    const std::vector<int>* bldOccRef = nullptr;
    bool cellBlocked(int x, int y) const {
        return bldOccRef && inBounds(x, y) && (*bldOccRef)[(size_t)y * w + x] > 0;
    }

    // 迷雾
    void initFog(int numPlayers);
    void clearVisible(int player);
    void reveal(int player, int cx, int cy, int radius);
    FogState fogAt(int player, int x, int y) const;

private:
    float noise2(int x, int y, uint64_t seed) const;
    float fbm(float x, float y, uint64_t seed) const;
};
