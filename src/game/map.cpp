#include "game/map.h"
#include <cmath>
#include <queue>
#include <cstring>
#include <algorithm>
#include <limits>

float Map::noise2(int x, int y, uint64_t seed) const {
    uint64_t hsh = (uint64_t)x * 0x8C8674F5C7A5A5B5ull ^ (uint64_t)y * 0xC2B2AE3D27D4EB4Full ^ seed;
    hsh ^= hsh >> 29; hsh *= 0x9E3779B97F4A7C15ull; hsh ^= hsh >> 32;
    return (float)(hsh & 0xFFFFFF) / (float)0xFFFFFF;
}

float Map::fbm(float x, float y, uint64_t seed) const {
    float sum = 0, amp = 1, freq = 1, norm = 0;
    for (int o = 0; o < 4; o++) {
        float fx = x * freq, fy = y * freq;
        int ix = (int)floorf(fx), iy = (int)floorf(fy);
        float tx = fx - ix, ty = fy - iy;
        tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
        float a = noise2(ix, iy, seed + o * 131);
        float b = noise2(ix + 1, iy, seed + o * 131);
        float c = noise2(ix, iy + 1, seed + o * 131);
        float d = noise2(ix + 1, iy + 1, seed + o * 131);
        float v = a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
        sum += v * amp; norm += amp;
        amp *= 0.5f; freq *= 2.0f;
    }
    return sum / norm;
}

void Map::generate(int w_, int h_, uint64_t seed, int numPlayers, std::vector<Vec2i>& outSpawns, int mapType) {
    bridgeCells.clear();
    native={};
    w = w_; h = h_;
    cells.assign((size_t)w * h, Cell{});
    Rng rng(seed);

    // 出生点：对角分布（先于地形确定，岛屿地形围绕出生点生成）
    outSpawns.clear();
    int m = 10; // 距边缘
    std::vector<Vec2i> corners = {
        {m, m}, {w - m - 1, h - m - 1}, {w - m - 1, m}, {m, h - m - 1},
        {w / 2, m}, {w / 2, h - m - 1}, {m, h / 2}, {w - m - 1, h / 2},
    };
    for (int i = 0; i < numPlayers && i < (int)corners.size(); i++)
        outSpawns.push_back(corners[i]);

    // 1. 基础地形：按地图类型生成水域格局
    // 0 大陆 1 岛屿 2 湖泊 3 群岛 4 海岸 5 河谷 6 山地
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            Cell& c = at(x, y);
            float v = fbm(x / 18.0f, y / 18.0f, seed);
            if (mapType == 1 || mapType == 3) {
                // 岛屿 / 群岛：出生点岛屿 + 中央岛；群岛额外撒碎岛
                float best = 1e9f;
                for (auto& sp : outSpawns) {
                    float dx = (float)(x - sp.x), dy = (float)(y - sp.y);
                    best = std::min(best, sqrtf(dx * dx + dy * dy));
                }
                float dx = (float)(x - w / 2), dy = (float)(y - h / 2);
                best = std::min(best, sqrtf(dx * dx + dy * dy) * 1.4f);
                float shore = (mapType == 3) ? (w / 6.5f) : (w / 4.5f);
                float edge = v * 7.0f;
                if (mapType == 3) {
                    float isle = fbm(x / 9.0f, y / 9.0f, seed ^ 0xA15E);
                    if (isle > 0.62f) best = std::min(best, shore * 0.55f);
                }
                if (best < shore - 5 + edge) c.terrain = Terrain::Clear;
                else if (best < shore + edge) c.terrain = v < 0.45f ? Terrain::Rough : Terrain::Clear;
                else c.terrain = Terrain::Water;
            } else if (mapType == 2) {
                // 湖泊：中央大湖（噪声湖岸），四周环陆地
                float dx = (float)(x - w / 2), dy = (float)(y - h / 2);
                float dc = sqrtf(dx * dx + dy * dy);
                float lakeR = w / 4.0f + (v - 0.5f) * 10.0f;
                if (dc < lakeR) c.terrain = Terrain::Water;
                else if (dc < lakeR + 3) c.terrain = Terrain::Rough;
                else if (v < 0.34f) c.terrain = Terrain::Rough;
                else c.terrain = Terrain::Clear;
            } else if (mapType == 4) {
                // 海岸：一侧大海，内侧大陆（噪声岸线）
                float nx = (float)x / (float)std::max(1, w - 1);
                float shore = 0.38f + (v - 0.5f) * 0.12f;
                if (nx < shore - 0.04f) c.terrain = Terrain::Water;
                else if (nx < shore + 0.03f) c.terrain = v < 0.5f ? Terrain::Rough : Terrain::Clear;
                else if (v < 0.32f) c.terrain = Terrain::Rough;
                else c.terrain = Terrain::Clear;
            } else if (mapType == 5) {
                // 河谷：纵贯地图的弯曲河道 + 两岸陆地
                float cx = w * 0.5f + (fbm(y / 14.0f, 3.0f, seed ^ 0x51BEULL) - 0.5f) * (w * 0.35f);
                float d = fabsf((float)x - cx);
                float half = 3.5f + v * 2.5f;
                if (d < half) c.terrain = Terrain::Water;
                else if (d < half + 2.5f) c.terrain = Terrain::Rough;
                else if (v < 0.36f) c.terrain = Terrain::Rough;
                else c.terrain = Terrain::Clear;
            } else if (mapType == 6) {
                // 山地：大量 Rough，少量水洼
                if (v < 0.22f) c.terrain = Terrain::Water;
                else if (v < 0.55f) c.terrain = Terrain::Rough;
                else c.terrain = Terrain::Clear;
            } else {
                // 大陆：fbm 噪声散布湖区
                if (v < 0.30f) c.terrain = Terrain::Water;
                else if (v < 0.38f) c.terrain = Terrain::Rough;
                else c.terrain = Terrain::Clear;
            }
            c.variant = (uint8_t)rng.range(0, 7);
        }

    // Coast starts belong to its mainland, rather than isolated artificial ocean pads.
    if(mapType==4) {
        const int left=w/2,right=w-m-1,top=m,bottom=h-m-1;
        const int upper=top+(bottom-top)/3,lower=top+2*(bottom-top)/3;
        const Vec2i coast[]={{left,top},{right,bottom},{right,top},{left,bottom},{left,upper},{right,lower},{right,upper},{left,lower}};
        for(size_t i=0;i<outSpawns.size();++i)outSpawns[i]=coast[i];
    }

    // 2. 出生点周围整平
    for (Vec2i sp : outSpawns) {
        for (int dy = -5; dy <= 5; dy++)
            for (int dx = -5; dx <= 5; dx++) {
                int x = sp.x + dx, y = sp.y + dy;
                if (inBounds(x, y)) { at(x, y).terrain = Terrain::Clear; at(x, y).overlay = Overlay::None; }
            }
    }

    // 3. 矿脉：每出生点附近一片主矿 + 若干散矿
    auto placeOreBlob = [&](int cx, int cy, int r, Terrain t, int amount) {
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int x = cx + dx, y = cy + dy;
                if (!inBounds(x, y)) continue;
                float d = sqrtf((float)(dx * dx + dy * dy)) / r;
                if (d <= 1.0f && rng.chance(1.1f - d)) {
                    Cell& c = at(x, y);
                    if (c.terrain == Terrain::Water) continue;
                    c.terrain = t;
                    c.ore = (int16_t)amount;
                    c.oreMax = (int16_t)amount; // 记录上限：矿脉缓慢再生用
                    c.overlay = Overlay::None;
                }
            }
    };
    for (auto& sp : outSpawns) {
        // 主矿在出生点朝向地图中心方向偏移
        int dirx = (sp.x < w / 2) ? 1 : -1;
        int diry = (sp.y < h / 2) ? 1 : -1;
        placeOreBlob(sp.x + dirx * 9, sp.y + diry * 7, 5, Terrain::Ore, 300);
        placeOreBlob(sp.x + dirx * 16, sp.y + diry * 12, 3, Terrain::Ore, 300);
        placeOreBlob(sp.x + dirx * 12, sp.y + diry * 18, 2, Terrain::Gems, 150);
    }
    for (int i = 0; i < 8; i++)
        placeOreBlob(rng.range(12, w - 12), rng.range(12, h - 12), rng.range(2, 4),
                     rng.chance(0.8f) ? Terrain::Ore : Terrain::Gems, 300);

    // 4. 树木与岩石装饰
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            Cell& c = at(x, y);
            if (c.terrain == Terrain::Clear && c.overlay == Overlay::None) {
                float t = fbm(x / 7.0f + 100, y / 7.0f + 100, seed ^ 0x5EED);
                if (t > 0.72f) c.overlay = (Overlay)(int)rng.range(1, 3); // Tree1..3
                else if (t < 0.05f) c.overlay = rng.chance(0.5f) ? Overlay::Rock1 : Overlay::Rock2;
            }
        }
    // 出生点周围清树
    for (auto& sp : outSpawns)
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++)
                if (inBounds(sp.x + dx, sp.y + dy)) at(sp.x + dx, sp.y + dy).overlay = Overlay::None;

    // 5. 桥梁（RA2 标志性战术地形）：横跨 1~4 格狭窄水域、两端接陆地的直线通道
    //    岛屿图海宽不生成；大陆/湖泊图在湖岸窄处生成，成为陆军跨水捷径
    // 5. 桥梁：岛屿/群岛海面过宽不生成；其余类型在窄水域架桥
    if (mapType != 1 && mapType != 3) {
        auto isLand = [&](int x, int y) {
            return inBounds(x, y) && at(x, y).terrain != Terrain::Water && at(x, y).terrain != Terrain::Bridge;
        };
        int want = std::max(2, (w * h) / 3000);
        for (int tries = 0; tries < want * 80 && want > 0; tries++) {
            bool horiz = rng.chance(0.5f);
            int x = rng.range(3, w - 4), y = rng.range(3, h - 4);
            if (at(x, y).terrain != Terrain::Water) continue;
            // 从水格向负向找陆端
            int dx = horiz ? 1 : 0, dy = horiz ? 0 : 1;
            int sx = x, sy = y;
            while (inBounds(sx - dx, sy - dy) && at(sx - dx, sy - dy).terrain == Terrain::Water) { sx -= dx; sy -= dy; }
            // sx,sy = 水段首格；前一格必须是陆地
            if (!isLand(sx - dx, sy - dy)) continue;
            // 量水段长度
            int len = 0;
            while (inBounds(sx + dx * len, sy + dy * len) && at(sx + dx * len, sy + dy * len).terrain == Terrain::Water) len++;
            if (len < 1 || len > 4) continue;
            // 水段末端后必须是陆地
            if (!isLand(sx + dx * len, sy + dy * len)) continue;
            // 远离出生点（避免堵基地），且桥身两侧不全为陆（保证真的是跨水通道）
            bool nearSpawn = false;
            for (auto& sp : outSpawns)
                if (abs(sp.x - sx) < 9 && abs(sp.y - sy) < 9) { nearSpawn = true; break; }
            if (nearSpawn) continue;
            for (int i = 0; i < len; i++) {
                Cell& c = at(sx + dx * i, sy + dy * i);
                c.terrain = Terrain::Bridge;
                c.overlay = Overlay::None;
                c.ore = 0;
            }
            want--;
        }
    }

    // 6. 格高度：默认全 0（平坦 TMP）。高度字段保留存档/编辑器；随机丘陵会破坏 TMP 一体感，不做自动抬升。
    for (Cell& c : cells) {
        if (c.terrain == Terrain::Water || c.terrain == Terrain::Bridge) c.height = 0;
        else c.height = 0;
    }
    finalizeSkirmishStarts(outSpawns,mapType);
}

int Map::harvestAt(int x, int y, int want) {
    if (!inBounds(x, y)) return 0;
    Cell& c = at(x, y);
    if (c.ore <= 0) return 0;
    int got = std::min((int)c.ore, want);
    c.ore -= (int16_t)got;
    if (c.ore <= 0) {
        c.terrain = Terrain::Rough; // 采空
        c.ore = 0;
    }
    return got;
}

bool Map::findNearestOre(int sx, int sy, int maxR, Vec2i& out) const {
    for (int r = 1; r <= maxR; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int x = sx + dx, y = sy + dy;
                if (inBounds(x, y) && at(x, y).ore > 0) { out = {x, y}; return true; }
            }
    return false;
}

struct AStarNode { int x, y; int64_t g, f; int parent; };

bool Map::findPath(int sx, int sy, int tx, int ty, std::vector<Vec2i>& outPath, int maxNodes, int domain, const std::vector<uint8_t>* congestion,int startLayer,int goalLayer,bool underBridge,uint8_t gateAccess) const {
    if(!hasBridgeDecks() && (startLayer>0 || goalLayer>0)){outPath.clear();return false;}
    if(hasBridgeDecks())return findLayeredPath(sx,sy,startLayer,tx,ty,goalLayer<0?preferredLayer(tx,ty,domain):goalLayer,tx+.5f,ty+.5f,0,outPath,maxNodes,domain,congestion,-1,underBridge,gateAccess);
    return findPathImpl(sx,sy,tx,ty,tx+0.5f,ty+0.5f,0,outPath,maxNodes,domain,congestion,gateAccess);
}

bool Map::findPathToRange(int sx,int sy,float tx,float ty,float range,std::vector<Vec2i>& outPath,int maxNodes,int domain,const std::vector<uint8_t>* congestion,int startLayer,int goalLayer,int goalHeight,bool underBridge,uint8_t gateAccess) const {
    outPath.clear();
    if(!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(range) || range<=0 || tx<0 || ty<0 || tx>=w || ty>=h) return false;
    if(hasBridgeDecks() || goalHeight>=0)return findLayeredPath(sx,sy,startLayer,(int)tx,(int)ty,goalLayer,tx,ty,range,outPath,maxNodes,domain,congestion,goalHeight,underBridge,gateAccess);
    return findPathImpl(sx,sy,(int)tx,(int)ty,tx,ty,range,outPath,maxNodes,domain,congestion,gateAccess);
}

bool Map::findPathImpl(int sx,int sy,int tx,int ty,float goalX,float goalY,float range,std::vector<Vec2i>& outPath,int maxNodes,int domain,const std::vector<uint8_t>* congestion,uint8_t gateAccess) const {
    outPath.clear();
    if (!inBounds(sx, sy) || !inBounds(tx, ty)) return false;
    // domain: 0 陆地 1 水面 2 两栖；建筑占用一律视为不可通行
    auto walkable = [&](int x, int y) {
        if (!inBounds(x, y) || cellBlocked(x, y,gateAccess)) return false;
        // 255 denotes an immovable occupant; lower values are soft congestion.
        if(congestion && congestion->size()==cells.size() && (*congestion)[(size_t)y*w+x]==255 && (x!=sx || y!=sy)) return false;
        const Cell& c = at(x, y);
        if(c.blocked)return false;
        if (domain == 1) return c.terrain == Terrain::Water;
        if (domain == 2) return c.passable() || c.terrain == Terrain::Water;
        return c.passable();
    };
    if (range<=0 && !walkable(tx, ty)) {
        // 目标不可通行：找最近可通行格
        bool found = false;
        for (int r = 1; r <= 3 && !found; r++)
            for (int dy = -r; dy <= r && !found; dy++)
                for (int dx = -r; dx <= r && !found; dx++) {
                    int x = tx + dx, y = ty + dy;
                    if (walkable(x, y)) { tx = x; ty = y; found = true; }
                }
        if (!found) return false;
    }
    auto reached=[&](int x,int y) {
        if(range<=0) return x==tx && y==ty;
        float dx=x+0.5f-goalX,dy=y+0.5f-goalY;
        return dx*dx+dy*dy<=range*range;
    };
    if (reached(sx,sy)) return true;

    const int N = w * h;
    if ((int)astarG.size() != N) {
        astarG.assign((size_t)N, 0);
        astarParent.assign((size_t)N, -1);
        astarOpenStamp.assign((size_t)N, 0);
        astarClosedStamp.assign((size_t)N, 0);
        astarGen = 1;
    }
    if (++astarGen == 0) {
        std::fill(astarOpenStamp.begin(), astarOpenStamp.end(), 0);
        std::fill(astarClosedStamp.begin(), astarClosedStamp.end(), 0);
        astarGen = 1;
    }
    const uint32_t gen = astarGen;

    auto gAt = [&](int i) -> int64_t {
        return astarOpenStamp[(size_t)i] == gen ? astarG[(size_t)i] : std::numeric_limits<int64_t>::max()/4;
    };
    auto setG = [&](int i, int64_t g, int par) {
        astarOpenStamp[(size_t)i] = gen;
        astarG[(size_t)i] = g;
        astarParent[(size_t)i] = par;
    };
    auto closed = [&](int i) {
        return astarClosedStamp[(size_t)i] == gen;
    };
    auto markClosed = [&](int i) {
        astarClosedStamp[(size_t)i] = gen;
    };

    auto hFn = [&](int x, int y) -> int64_t {
        if(range>0) return (int64_t)(std::max(0.0f,distf(x+0.5f,y+0.5f,goalX,goalY)-range)*9999.9f);
        const int dx=abs(x-tx),dy=abs(y-ty);
        return 10000LL*std::max(dx,dy)+4142LL*std::min(dx,dy);
    };
    // Integer octile costs avoid tiny accumulated float differences expanding
    // an entire equal-cost rectangle. Prefer the command line, then progress
    // and stable cell order; all secondary criteria preserve shortest paths.
    auto cmp = [=](const AStarNode& a, const AStarNode& b) {
        if(a.f!=b.f)return a.f>b.f;
        const int64_t crossA=std::abs((int64_t)(a.x-sx)*(ty-sy)-(int64_t)(a.y-sy)*(tx-sx));
        const int64_t crossB=std::abs((int64_t)(b.x-sx)*(ty-sy)-(int64_t)(b.y-sy)*(tx-sx));
        if(crossA!=crossB)return crossA>crossB;
        if(a.g!=b.g)return a.g<b.g;
        return a.y!=b.y?a.y>b.y:a.x>b.x;
    };
    std::priority_queue<AStarNode, std::vector<AStarNode>, decltype(cmp)> open(cmp);

    int startIdx = sy * w + sx;
    setG(startIdx, 0, -1);
    open.push({sx, sy, 0, hFn(sx, sy), -1});

    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static constexpr int64_t COST[8] = {10000,10000,10000,10000,14142,14142,14142,14142};

    int expanded = 0;
    while (!open.empty() && expanded++ < maxNodes) {
        AStarNode cur = open.top(); open.pop();
        int ci = cur.y * w + cur.x;
        if (closed(ci)) continue;
        markClosed(ci);
        if (reached(cur.x,cur.y)) {
            std::vector<Vec2i> rev;
            int i = ci;
            while (i >= 0 && i != startIdx) {
                rev.push_back({i % w, i / w});
                i = astarParent[(size_t)i];
            }
            outPath.assign(rev.rbegin(), rev.rend());
            return true;
        }
        for (int d = 0; d < 8; d++) {
            int nx = cur.x + DX[d], ny = cur.y + DY[d];
            if (!walkable(nx, ny)) continue;
            if (!climbOk(cur.x, cur.y, nx, ny, domain)) continue;
            if (d >= 4) { // 禁止穿对角障碍
                if (!walkable(cur.x + DX[d], cur.y) || !walkable(cur.x, cur.y + DY[d])) continue;
                if (!climbOk(cur.x, cur.y, cur.x + DX[d], cur.y, domain)
                    || !climbOk(cur.x, cur.y, cur.x, cur.y + DY[d], domain)
                    || !climbOk(cur.x + DX[d], cur.y, nx, ny, domain)
                    || !climbOk(cur.x, cur.y + DY[d], nx, ny, domain)) continue;
            }
            int ni = ny * w + nx;
            if (closed(ni)) continue;
            int64_t ng = cur.g + COST[d];
            if (congestion && congestion->size() == (size_t)N) ng += 10000LL*(*congestion)[ni];
            if (ng < gAt(ni)) {
                setG(ni, ng, ci);
                open.push({nx, ny, ng, ng + hFn(nx, ny), (int)ci});
            }
        }
    }
    return false;
}

void Map::initFog(int numPlayers) {
    fog.assign(numPlayers, std::vector<uint8_t>((size_t)w * h, FOG_UNSEEN));
}

void Map::clearVisible(int player) {
    auto& f = fog[player];
    for (auto& v : f) if (v == FOG_VISIBLE) v = FOG_SEEN;
}

void Map::reveal(int player, int cx, int cy, int radius) {
    auto& f = fog[player];
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > r2) continue;
            int x = cx + dx, y = cy + dy;
            if (inBounds(x, y)) f[(size_t)y * w + x] = FOG_VISIBLE;
        }
}

FogState Map::fogAt(int player, int x, int y) const {
    if (!inBounds(x, y)) return FOG_UNSEEN;
    return (FogState)fog[player][(size_t)y * w + x];
}
