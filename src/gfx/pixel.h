#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include "raylib.h"

// CPU 侧 RGBA 像素画布：程序化素材生成的绘制目标
struct PixBuf {
    int w = 0, h = 0;
    std::vector<Color> px; // RGBA

    PixBuf() = default;
    PixBuf(int w_, int h_) { resize(w_, h_); }

    void resize(int w_, int h_) {
        w = w_; h = h_;
        px.assign((size_t)w * h, Color{0, 0, 0, 0});
    }
    void clear(Color c = Color{0, 0, 0, 0}) { std::fill(px.begin(), px.end(), c); }

    inline Color get(int x, int y) const {
        if (x < 0 || y < 0 || x >= w || y >= h) return Color{0, 0, 0, 0};
        return px[(size_t)y * w + x];
    }
    inline void set(int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        px[(size_t)y * w + x] = c;
    }
    // alpha 混合写入
    inline void blend(int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= w || y >= h || c.a == 0) return;
        if (c.a == 255) { set(x, y, c); return; }
        Color d = get(x, y);
        float a = c.a / 255.0f;
        Color r;
        r.r = (uint8_t)(c.r * a + d.r * (1 - a));
        r.g = (uint8_t)(c.g * a + d.g * (1 - a));
        r.b = (uint8_t)(c.b * a + d.b * (1 - a));
        r.a = (uint8_t)std::min(255, c.a + (int)(d.a * (1 - a)));
        set(x, y, r);
    }

    void hline(int x0, int x1, int y, Color c);
    void fillRect(int x, int y, int w, int h, Color c);
    void rect(int x, int y, int w, int h, Color c);
    void line(int x0, int y0, int x1, int y1, Color c);
    void fillEllipse(int cx, int cy, int rx, int ry, Color c);
    void ellipse(int cx, int cy, int rx, int ry, Color c);
    // 等距菱形（顶视图瓦片）
    void diamond(int cx, int cy, int rw, int rh, Color c);
    // 等距立方体：顶面/左面/右面三色，返回顶部中心 y
    void isoBox(int cx, int topY, int rw, int rh, int height, Color top, Color left, Color right);
    // 把 src 拷贝到本图 (dx,dy)
    void blit(const PixBuf& src, int dx, int dy);
    // 水平镜像
    PixBuf flipH() const;
    // 最近邻缩放
    PixBuf scale(int nw, int nh) const;
    // 简单 45° 倍数旋转（用于方向帧，质量一般但快）
    PixBuf rotate8(int dir45) const; // dir45: 0..7, 顺时针*45°
    // 将 remapFrom 颜色的像素替换为 remapTo（阵营色重映射）
    void remap(Color from, Color to);
    // 调色：乘性调暗/调亮
    void shade(float f);

    // 上传为 GPU 纹理（调用方负责 UnloadTexture）
    Texture2D toTexture() const;

    // 从 PNG 文件加载（转为 RGBA8）；失败返回 false 且本对象不变
    bool loadFromFile(const char* path);
    // 导出为 PNG（纯 CPU 操作，可在无窗口环境使用）
    bool saveToFile(const char* path) const;
};

// 常用调色板
namespace Pal {
    constexpr Color REMAP      = {255, 0, 0, 255};    // 阵营色占位（生成时用纯红，运行时替换）
    constexpr Color REMAP_DARK = {176, 0, 0, 255};
    constexpr Color REMAP_LITE = {255, 128, 128, 255};

    constexpr Color STEEL      = {96, 100, 108, 255};
    constexpr Color STEEL_DARK = {56, 58, 66, 255};
    constexpr Color STEEL_LITE = {160, 166, 176, 255};
    constexpr Color GUN        = {40, 42, 48, 255};
    constexpr Color TRACK      = {30, 30, 34, 255};
    constexpr Color TRACK_HI   = {64, 64, 70, 255};
    constexpr Color GLASS      = {120, 190, 220, 255};
    constexpr Color SKIN       = {224, 180, 140, 255};
    constexpr Color ORE_GOLD   = {214, 164, 40, 255};
    constexpr Color GEM_GRN    = {80, 220, 120, 255};
    constexpr Color MUZZLE     = {255, 220, 120, 255};
}

// 纹理缓存项
struct Sprite {
    Texture2D tex{};
    int ox = 0, oy = 0; // 绘制锚点偏移（像素坐标 = 逻辑点 + offset）
    // 不透明内容包围盒（纹理像素坐标，含 finishBldSprite 的 6/4 边距）
    // 用于选中虚线笼：宽/高贴合可见建筑，而非整幅透明画布
    int visL = 0, visT = 0, visR = 0, visB = 0;
    bool valid() const { return tex.id != 0; }
    int visW() const { return std::max(1, visR - visL + 1); }
    int visH() const { return std::max(1, visB - visT + 1); }
    // 南尖锚点到可见顶缘的屏幕高度（虚线笼 Z）
    int visElev() const { return std::max(8, oy - visT); }
};
