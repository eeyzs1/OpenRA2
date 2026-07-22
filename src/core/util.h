#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

// 等距瓦片尺寸（像素）
constexpr int TILE_W = 64;
constexpr int TILE_H = 32;

// 游戏逻辑帧率（RA2 约 15fps 逻辑，这里用 30）
constexpr int LOGIC_FPS = 30;

struct Vec2i {
    int x = 0, y = 0;
    bool operator==(const Vec2i& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2i& o) const { return !(*this == o); }
};

struct Vec2f {
    float x = 0, y = 0;
};

// 确定性随机数（用于地形生成与素材噪声）
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    uint64_t next() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s;
    }
    int range(int lo, int hi) { return lo + (int)(next() % (uint64_t)(hi - lo + 1)); }
    float unit() { return (float)((next() >> 40) & 0xFFFFFF) / (float)0xFFFFFF; }
    bool chance(float p) { return unit() < p; }
};

// 坐标换算：瓦片坐标 -> 屏幕像素（菱形地图）
inline void tileToScreen(int tx, int ty, int& sx, int& sy) {
    sx = (tx - ty) * (TILE_W / 2);
    sy = (tx + ty) * (TILE_H / 2);
}

// 屏幕像素 -> 瓦片坐标（返回所在瓦片）
inline void screenToTile(float sx, float sy, int& tx, int& ty) {
    float fx = sx / (TILE_W / 2.0f);
    float fy = sy / (TILE_H / 2.0f);
    tx = (int)floorf((fx + fy) / 2.0f);
    ty = (int)floorf((fy - fx) / 2.0f);
}

inline float distf(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1, dy = y2 - y1;
    return sqrtf(dx * dx + dy * dy);
}

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
