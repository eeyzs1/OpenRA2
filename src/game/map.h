#pragma once
#include "core/util.h"
#include "game/native_map.h"
#include <vector>
#include <cstdint>

// 地形类型
enum class Terrain : uint8_t {
    Clear = 0,   // 草地/平地
    Rough,       // 粗糙地面（可通行）
    Water,       // 水面（不可通行，除非海军）
    Ore,         // 金矿
    Gems,        // 彩矿（更值钱）
    Bridge,      // 桥梁（横跨水域的陆地通道：地面/两栖可走，海军不可通过，不可建筑）
};

// 地表装饰物
enum class Overlay : uint8_t {
    None = 0,
    Tree1, Tree2, Tree3,
    Rock1, Rock2,
};

struct Cell {
    bool present = true;     // false outside a native map's diamond canvas
    bool blocked = false;    // native cliff/wall mask, independent of artwork
    Terrain terrain = Terrain::Clear;
    Overlay overlay = Overlay::None;
    uint8_t variant = 0;      // 瓦片贴图变体
    uint8_t height = 0;       // RA2 式格高度（0..3 常见；绘制 Y 抬升，寻路限爬升）
    int16_t ore = 0;          // 剩余矿石量
    int16_t oreMax = 0;       // 矿石上限（生成时记录，用于矿脉缓慢再生）
    bool passable() const {
        return present && !blocked && terrain != Terrain::Water && overlay != Overlay::Rock1 && overlay != Overlay::Rock2;
    }
};

// 地面/两栖邻格最大高度差；海军忽略
constexpr int MAP_MAX_CLIMB = 1;

// 战争迷雾状态
enum FogState : uint8_t { FOG_UNSEEN = 0, FOG_SEEN = 1, FOG_VISIBLE = 2 };

class Map {
public:
    struct BridgeCell {
        int owner=-1; // source segment entity; derived, rebuilt from live native actors
        uint8_t height=0,axis=0; // axis 0 runs along x, axis 1 along y
    };
    int w = 0, h = 0;
    std::vector<Cell> cells;
    NativeMapInfo native;
    std::vector<BridgeCell> bridgeCells;
    struct GateCell { int owner=-1,house=-1; bool open=false; };
    std::vector<GateCell> gateCells; // derived; retains building identity even while traversable
    bool hasBridgeDecks() const {return !bridgeCells.empty();}
    bool hasDeck(int x,int y) const {return inBounds(x,y) && bridgeCells.size()==cells.size() && bridgeCells[(size_t)y*w+x].owner>=0;}
    int surfaceHeight(int x,int y,int layer=0) const;
    bool surfacePassable(int x,int y,int domain,int layer,bool buildings=true,bool underBridge=true,uint8_t gateAccess=0) const;
    int preferredLayer(int x,int y,int domain) const;
    // A four-level deck separation makes the destination layer unique for a
    // legal adjacent step (maximum climb is one). Routes retain XY waypoints;
    // current layer plus each step reconstructs their exact surface sequence.
    int stepLayer(int ax,int ay,int layer,int bx,int by,int domain,bool corners=true,bool underBridge=true,uint8_t gateAccess=0) const;
    // 每玩家迷雾: [player][cell]
    std::vector<std::vector<uint8_t>> fog;

    // mapType: 0 大陆（默认，散布湖区）1 岛屿（出生点各自成岛，中央争夺岛）2 湖泊（中央大湖环绕陆地）
    void generate(int w_, int h_, uint64_t seed, int numPlayers, std::vector<Vec2i>& outSpawns, int mapType = 0);

    inline bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && cells[(size_t)y*w+x].present; }
    inline Cell& at(int x, int y) { return cells[(size_t)y * w + x]; }
    inline const Cell& at(int x, int y) const { return cells[(size_t)y * w + x]; }

    bool passable(int x, int y) const { return inBounds(x, y) && cells[(size_t)y * w + x].passable(); }

    // 邻格爬升：domain 1（海军）始终 true；地面/两栖要求 |Δheight| <= MAP_MAX_CLIMB
    bool climbOk(int ax, int ay, int bx, int by, int domain) const {
        if (domain == 1) return true;
        if (!inBounds(ax, ay) || !inBounds(bx, by)) return false;
        if(hasBridgeDecks())return std::abs(surfaceHeight(ax,ay,preferredLayer(ax,ay,domain))-surfaceHeight(bx,by,preferredLayer(bx,by,domain)))<=MAP_MAX_CLIMB;
        int dh = (int)at(ax, ay).height - (int)at(bx, by).height;
        if (dh < 0) dh = -dh;
        return dh <= MAP_MAX_CLIMB;
    }

    // 采集矿石：返回采集到的量
    int harvestAt(int x, int y, int want);

    // 找最近的矿石格
    bool findNearestOre(int sx, int sy, int maxR, Vec2i& out) const;

    // A* 寻路（8 方向）；domain: 0 陆地 1 水面 2 两栖
    // 返回路径点（含终点，不含起点）；失败返回 false
    bool findPath(int sx, int sy, int tx, int ty, std::vector<Vec2i>& outPath, int maxNodes = 20000, int domain = 0, const std::vector<uint8_t>* congestion = nullptr,int startLayer=-1,int goalLayer=-1,bool underBridge=true,uint8_t gateAccess=0) const;
    // Optional congestion: 0..254 soft cost, 255 impassable (except starting cell).
    bool findPathToRange(int sx,int sy,float tx,float ty,float range,std::vector<Vec2i>& outPath,int maxNodes=20000,int domain=0,const std::vector<uint8_t>* congestion=nullptr,int startLayer=-1,int goalLayer=-1,int goalHeight=-1,bool underBridge=true,uint8_t gateAccess=0) const;

    // 建筑占用表（由 World 挂载，寻路时视为不可通行）；元素为 eid+1，0/-1 为无建筑
    const std::vector<int>* bldOccRef = nullptr;
    bool cellBlocked(int x, int y,uint8_t gateAccess=0) const {
        if(!bldOccRef || !inBounds(x,y))return false;
        const size_t index=(size_t)y*w+x;const int occupant=(*bldOccRef)[index];
        if(occupant<=0)return false;
        if(gateCells.size()==cells.size()) {
            const auto& gate=gateCells[index];
            if(gate.owner==occupant-1 && (gate.open || (gate.house>=0 && (gateAccess&(1u<<gate.house)))))return false;
        }
        return true;
    }

    // 迷雾
    void initFog(int numPlayers);
    void clearVisible(int player);
    void reveal(int player, int cx, int cy, int radius);
    FogState fogAt(int player, int x, int y) const;

private:
    bool findLayeredPath(int sx,int sy,int startLayer,int tx,int ty,int goalLayer,float goalX,float goalY,float range,std::vector<Vec2i>& out,int maxNodes,int domain,const std::vector<uint8_t>* costs,int goalHeight=-1,bool underBridge=true,uint8_t gateAccess=0) const;
    void finalizeSkirmishStarts(const std::vector<Vec2i>& starts,int mapType);
    bool findPathImpl(int sx,int sy,int tx,int ty,float goalX,float goalY,float range,std::vector<Vec2i>& outPath,int maxNodes,int domain,const std::vector<uint8_t>* congestion,uint8_t gateAccess) const;
    float noise2(int x, int y, uint64_t seed) const;
    float fbm(float x, float y, uint64_t seed) const;

    // A* 持久 scratch（findPath 为 const，用 mutable + generation 懒重置）
    mutable std::vector<int64_t> astarG;
    mutable std::vector<int> astarParent;
    mutable std::vector<uint32_t> astarOpenStamp;   // == gen 表示本轮已写过 g/parent
    mutable std::vector<uint32_t> astarClosedStamp; // == gen 表示已闭合
    mutable uint32_t astarGen = 1;
};
