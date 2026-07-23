#include "game/map.h"
#include <cmath>
#include <queue>
#include <cstring>
#include <algorithm>

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
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            Cell& c = at(x, y);
            float v = fbm(x / 18.0f, y / 18.0f, seed);
            if (mapType == 1) {
                // 岛屿：每出生点一座主岛 + 中央争夺岛，其余为海（噪声海岸）
                float best = 1e9f;
                for (auto& sp : outSpawns) {
                    float dx = (float)(x - sp.x), dy = (float)(y - sp.y);
                    best = std::min(best, sqrtf(dx * dx + dy * dy));
                }
                float dx = (float)(x - w / 2), dy = (float)(y - h / 2);
                best = std::min(best, sqrtf(dx * dx + dy * dy) * 1.4f); // 中央岛稍小
                float shore = w / 4.5f;
                float edge = v * 7.0f;
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
            } else {
                // 大陆：fbm 噪声散布湖区
                if (v < 0.30f) c.terrain = Terrain::Water;
                else if (v < 0.38f) c.terrain = Terrain::Rough;
                else c.terrain = Terrain::Clear;
            }
            c.variant = (uint8_t)rng.range(0, 3);
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

struct AStarNode { int x, y; float g, f; int parent; };

bool Map::findPath(int sx, int sy, int tx, int ty, std::vector<Vec2i>& outPath, int maxNodes, int domain) const {
    outPath.clear();
    if (!inBounds(sx, sy) || !inBounds(tx, ty)) return false;
    // domain: 0 陆地 1 水面 2 两栖；建筑占用一律视为不可通行
    auto walkable = [&](int x, int y) {
        if (!inBounds(x, y) || cellBlocked(x, y)) return false;
        const Cell& c = at(x, y);
        if (domain == 1) return c.terrain == Terrain::Water;
        if (domain == 2) return c.passable() || c.terrain == Terrain::Water;
        return c.passable();
    };
    if (!walkable(tx, ty)) {
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
    if (sx == tx && sy == ty) return true;

    const int N = w * h;
    std::vector<float> gScore(N, 1e30f);
    std::vector<int> parent(N, -1);
    std::vector<uint8_t> closed(N, 0);

    auto hFn = [&](int x, int y) {
        float dx = (float)abs(x - tx), dy = (float)abs(y - ty);
        return std::max(dx, dy) + 0.4142f * std::min(dx, dy);
    };
    auto cmp = [](const AStarNode& a, const AStarNode& b) { return a.f > b.f; };
    std::priority_queue<AStarNode, std::vector<AStarNode>, decltype(cmp)> open(cmp);

    int startIdx = sy * w + sx;
    gScore[startIdx] = 0;
    open.push({sx, sy, 0, hFn(sx, sy), -1});

    static const int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int DY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    static const float COST[8] = {1, 1, 1, 1, 1.4142f, 1.4142f, 1.4142f, 1.4142f};

    int expanded = 0;
    while (!open.empty() && expanded++ < maxNodes) {
        AStarNode cur = open.top(); open.pop();
        int ci = cur.y * w + cur.x;
        if (closed[ci]) continue;
        closed[ci] = 1;
        if (cur.x == tx && cur.y == ty) {
            // 回溯
            std::vector<Vec2i> rev;
            int i = ci;
            while (i >= 0 && i != startIdx) {
                rev.push_back({i % w, i / w});
                i = parent[i];
            }
            outPath.assign(rev.rbegin(), rev.rend());
            return true;
        }
        for (int d = 0; d < 8; d++) {
            int nx = cur.x + DX[d], ny = cur.y + DY[d];
            if (!walkable(nx, ny)) continue;
            if (d >= 4) { // 禁止穿对角障碍
                if (!walkable(cur.x + DX[d], cur.y) || !walkable(cur.x, cur.y + DY[d])) continue;
            }
            int ni = ny * w + nx;
            if (closed[ni]) continue;
            float ng = cur.g + COST[d];
            if (ng < gScore[ni]) {
                gScore[ni] = ng;
                parent[ni] = ci;
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
