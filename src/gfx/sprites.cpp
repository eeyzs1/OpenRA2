#include "gfx/sprites.h"
#include "gfx/assets.h"
#include "gfx/model3d.h"
#include "gfx/unitmodels.h"
#include "gfx/bldmodels.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

const Color HOUSE_COLORS[MAX_PLAYERS] = {
    {255, 200, 40, 255},  // 金
    {220, 40, 40, 255},   // 红
    {60, 110, 230, 255},  // 蓝
    {60, 180, 80, 255},   // 绿
    {240, 130, 30, 255},  // 橙
    {80, 200, 220, 255},  // 青
    {170, 80, 200, 255},  // 紫
    {230, 130, 170, 255}, // 粉
};

SpriteBank g_sprites;

int dirFromVec(float dx, float dy) {
    if (dx == 0 && dy == 0) return 2;
    float a = atan2f(dy, dx);            // 屏幕坐标 y 向下
    int d = (int)roundf(a / (3.14159265f / 4.0f));
    d = ((d % 8) + 8) % 8;               // 0=东 顺时针
    return d;
}

// ---------------- 缓存 ----------------
static uint64_t keyOf(int cat, int a, int b, int c, int player) {
    return ((uint64_t)cat << 56) | ((uint64_t)(a & 0xFFFF) << 40) |
           ((uint64_t)(b & 0xFF) << 32) | ((uint64_t)(c & 0xFF) << 24) | (uint64_t)(player & 0xFF);
}

// ---------------- 外部素材覆盖（assets/sprites/*.png，命名约定见 gfx/assets.h） ----------------
static bool loadSpr(PixBuf& out, const char* fmt, ...) {
    char path[192];
    va_list ap; va_start(ap, fmt);
    vsnprintf(path, sizeof(path), fmt, ap);
    va_end(ap);
    return out.loadFromFile(path);
}

// ---------------- RA2 补全：图形别名（已全量绘制专属图形，仅保留兜底入口） ----------------
static UnitType spriteAliasUnit(UnitType t) {
    return t; // 全部单位均有专属图形（外部素材仍可按别名前的原始名覆盖）
}
static BldType spriteAliasBld(BldType t) {
    return t; // 全部建筑均有专属图形
}

Sprite SpriteBank::makeSprite(PixBuf&& pb, int ox, int oy) {
    Sprite s;
    s.tex = pb.toTexture();
    s.ox = ox; s.oy = oy;
    return s;
}

// ---------------- RA2 风格地面投影 ----------------
// 在画布内烘烧软椭圆投影（下偏右，RA2 标志性视觉锚点）
static void bakeShadow(PixBuf& pb, int cx, int cy, int rx, int ry) {
    for (int y = cy - ry; y <= cy + ry; y++)
        for (int x = cx - rx; x <= cx + rx; x++) {
            float ddx = (x - cx) / (float)rx, ddy = (y - cy) / (float)ry;
            float d = ddx * ddx + ddy * ddy;
            if (d > 1.0f) continue;
            uint8_t a = (uint8_t)(86.0f * (1.0f - d));
            pb.blend(x, y, Color{8, 10, 6, a});
        }
}
// 扩充画布（内容偏移到 left,top），用于容纳投影
static PixBuf padCanvas(const PixBuf& src, int left, int top, int right, int bottom) {
    PixBuf r(src.w + left + right, src.h + top + bottom);
    r.blit(src, left, top);
    return r;
}

// ---------------- RA2 风格化后处理（还原度 pass） ----------------
static bool solidPx(Color c) { return c.a > 120; }
// 定向棱边光：RA2 光源在左上——上/左外露边提亮，下/右外露边压暗；细线条与内部像素不变
static void ra2Bevel(PixBuf& p) {
    PixBuf s = p;
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++) {
            Color c = s.get(x, y);
            if (!solidPx(c)) continue;
            bool lite = !solidPx(s.get(x, y - 1)) || !solidPx(s.get(x - 1, y));
            bool dark = !solidPx(s.get(x, y + 1)) || !solidPx(s.get(x + 1, y));
            if (lite == dark) continue;
            if (lite) p.set(x, y, Color{(uint8_t)(c.r + (255 - c.r) * 28 / 100),
                                        (uint8_t)(c.g + (255 - c.g) * 28 / 100),
                                        (uint8_t)(c.b + (255 - c.b) * 28 / 100), c.a});
            else      p.set(x, y, Color{(uint8_t)(c.r * 76 / 100),
                                        (uint8_t)(c.g * 76 / 100),
                                        (uint8_t)(c.b * 76 / 100), c.a});
        }
}
// 深色轮廓：实体像素八邻域的透明处补深色描边（RA2 单位/建筑辨识度核心）
static void ra2Outline(PixBuf& p) {
    PixBuf s = p;
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++) {
            if (s.get(x, y).a != 0) continue;
            bool near = false;
            for (int dy = -1; dy <= 1 && !near; dy++)
                for (int dx = -1; dx <= 1 && !near; dx++)
                    if ((dx || dy) && solidPx(s.get(x + dx, y + dy))) near = true;
            if (near) p.set(x, y, Color{14, 12, 14, 165});
        }
}
// 单位/建筑内容图（成员函数，见下）：基础绘制 + 四边留白 + 棱边光 + 轮廓
// 不含地面投影 —— 投影在运行时对外部文件与程序生成统一烘焙，保证两条路径视觉一致

// ---------------- 地形 ----------------
// 平滑值噪声：格点哈希 + 双线性插值（smoothstep），比白噪声更接近 RA2 地表斑块感
static float vnoise(int x, int y, int scale, uint64_t seed) {
    auto hash = [&](int gx, int gy) {
        uint32_t h = ((uint32_t)gx * 73856093u) ^ ((uint32_t)gy * 19349663u) ^ ((uint32_t)seed * 83492791u);
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        return (float)(h % 1024) / 1024.0f;
    };
    int gx = x / scale, gy = y / scale;
    float fx = (x % scale) / (float)scale, fy = (y % scale) / (float)scale;
    fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
    float a = hash(gx, gy), b = hash(gx + 1, gy), c = hash(gx, gy + 1), d = hash(gx + 1, gy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static uint8_t lerp8(int a, int b, float t) { return (uint8_t)(a + (b - a) * t); }

PixBuf SpriteBank::baseTile(Terrain t, int variant) {
    PixBuf p(TILE_W, TILE_H);
    uint64_t seed = (uint64_t)t * 1000 + variant * 77;
    auto grain = [&](int x, int y) { // 细颗粒（低幅白噪声）
        return (float)((((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)seed * 83492791u)) % 1000) / 1000.0f;
    };
    for (int y = 0; y < TILE_H; y++)
        for (int x = 0; x < TILE_W; x++) {
            // 菱形裁剪
            float dx = fabsf(x - TILE_W / 2.0f) / (TILE_W / 2.0f);
            float dy = fabsf(y - TILE_H / 2.0f) / (TILE_H / 2.0f);
            if (dx + dy > 1.0f) continue;
            float n1 = vnoise(x, y, 8, seed);        // 大斑块
            float n2 = vnoise(x, y, 3, seed + 5);    // 中斑块
            float n = n1 * 0.62f + n2 * 0.38f;       // 0..1
            float g = (grain(x, y) - 0.5f) * 14.0f;  // 细颗粒 ±7
            Color c;
            switch (t) {
                case Terrain::Clear: {
                    // RA2 温带草地：黄绿基调，深绿/浅绿斑块交错，偶发枯草点
                    c = Color{lerp8(64, 106, n), lerp8(102, 148, n), lerp8(44, 70, n), 255};
                    if (n2 > 0.84f && n1 > 0.45f) { c.r = 118; c.g = 122; c.b = 58; } // 枯草
                    break;
                }
                case Terrain::Rough: {
                    // 沙土：黄褐基调 + 深土斑
                    c = Color{lerp8(126, 162, n), lerp8(104, 138, n), lerp8(66, 92, n), 255};
                    if (n2 < 0.16f) { c.r -= 22; c.g -= 20; c.b -= 14; }              // 碎石暗斑
                    break;
                }
                case Terrain::Water: {
                    // 深水蓝绿 + 横向波光条带（RA2 水面有轻微波纹感）
                    float wv = sinf((y + n2 * 6.0f) * 1.2f) * 0.5f + 0.5f; // 波带
                    float m = n * 0.7f + wv * 0.3f;
                    c = Color{lerp8(22, 44, m), lerp8(58, 104, m), lerp8(104, 164, m), 255};
                    if (wv > 0.86f && n2 > 0.55f) { c.r += 22; c.g += 26; c.b += 26; } // 波峰高光
                    break;
                }
                case Terrain::Ore: {
                    // 矿脉：棕黄土地 + 成簇金矿晶体（含高光点）
                    c = Color{lerp8(96, 130, n), lerp8(74, 104, n), lerp8(40, 62, n), 255};
                    if (n2 > 0.66f) {
                        c = Color{lerp8(196, 232, n1), lerp8(148, 186, n1), lerp8(36, 64, n1), 255};
                        if (grain(x * 5 + 3, y * 5 + 1) > 0.80f) c = Color{255, 240, 170, 255}; // 晶面闪光
                    }
                    break;
                }
                case Terrain::Gems: {
                    // 彩矿：深壤 + 翠绿晶簇
                    c = Color{lerp8(56, 84, n), lerp8(66, 96, n), lerp8(50, 72, n), 255};
                    if (n2 > 0.64f) {
                        c = Color{lerp8(60, 120, n1), lerp8(190, 236, n1), lerp8(100, 150, n1), 255};
                        if (grain(x * 5 + 9, y * 5 + 7) > 0.78f) c = Color{220, 255, 225, 255};
                    }
                    break;
                }
                case Terrain::Bridge: {
                    // 木质桥面：棕褐木板 + 横向板缝 + 板间色差
                    float plank = (float)((y / 4) % 2) * 0.10f;
                    float v = 0.75f + n * 0.35f - plank;
                    c = Color{(uint8_t)(150 * v), (uint8_t)(102 * v), (uint8_t)(58 * v), 255};
                    if (y % 4 == 0) { c.r = (uint8_t)(c.r * 0.55f); c.g = (uint8_t)(c.g * 0.55f); c.b = (uint8_t)(c.b * 0.55f); }
                    break;
                }
            }
            c.r = (uint8_t)clampi(c.r + (int)g, 0, 255);
            c.g = (uint8_t)clampi(c.g + (int)g, 0, 255);
            c.b = (uint8_t)clampi(c.b + (int)g, 0, 255);
            // 边缘轻微暗化（弱网格感：过强的菱形描边会让地表呈现棋盘格）
            if (dx + dy > 0.96f) { c.r = (uint8_t)(c.r * 0.88f); c.g = (uint8_t)(c.g * 0.88f); c.b = (uint8_t)(c.b * 0.88f); }
            p.set(x, y, c);
        }
    return p;
}

PixBuf SpriteBank::baseOverlay(Overlay o) {
    switch (o) {
        case Overlay::Tree1: case Overlay::Tree2: case Overlay::Tree3: {
            int s = o == Overlay::Tree1 ? 14 : (o == Overlay::Tree2 ? 18 : 12);
            PixBuf p(s * 2 + 8, s * 3 + 4);
            Rng rng((uint64_t)o * 991);
            int cx = s + 4, baseY = s * 3 + 1;
            // 地面投影（RA2 树木有清晰倒影）
            for (int y = -3; y <= 3; y++)
                for (int x = -s; x <= s; x++) {
                    float d = (x * x) / (float)(s * s) + (y * y) / 9.0f;
                    if (d <= 1.0f) p.blend(cx + 3 + x, baseY + y, Color{8, 10, 6, (uint8_t)(64 * (1.0f - d))});
                }
            // 树干
            p.fillRect(cx - 1, baseY - s / 2 - 2, 3, s / 2 + 2, Color{82, 54, 32, 255});
            p.fillRect(cx, baseY - s / 2 - 2, 1, s / 2 + 2, Color{110, 76, 46, 255});
            // 树冠：多层椭圆（下深上浅，左侧高光，模拟 RA2 树冠体积感）
            for (int i = 0; i < 4; i++) {
                int ry = s - i * s / 5;
                int rw = s + 2 - i * 3;
                int cy = baseY - s / 2 - ry / 2 - i * 3;
                Color g{ (uint8_t)(26 + i * 20), (uint8_t)(84 + i * 26), (uint8_t)(30 + i * 16), 255 };
                p.fillEllipse(cx, cy, rw > 2 ? rw : 2, ry / 2 + 2, g);
                // 顶部高光弧
                p.fillEllipse(cx - rw / 4, cy - ry / 4, rw / 3 > 1 ? rw / 3 : 1, ry / 5 + 1, Color{(uint8_t)(60 + i * 20), (uint8_t)(140 + i * 24), (uint8_t)(56 + i * 16), 255});
            }
            // 叶隙噪点（深色孔洞 + 高光点）
            for (int i = 0; i < s * 2; i++) {
                int px = cx - s + rng.range(0, s * 2), py = baseY - s - rng.range(0, s);
                Color cur = p.get(px, py);
                if (cur.a && cur.g > 60) p.set(px, py, rng.unit() > 0.5f ? Color{18, 58, 22, 255} : Color{104, 168, 78, 220});
            }
            return p;
        }
        case Overlay::Rock1: case Overlay::Rock2: {
            PixBuf p(24, 15);
            // 投影
            p.fillEllipse(12, 11, 9, 3, Color{8, 10, 6, 60});
            Color base{112, 110, 106, 255}, dark{66, 64, 62, 255}, lite{158, 156, 150, 255};
            p.fillEllipse(11, 8, o == Overlay::Rock1 ? 8 : 6, 5, base);
            p.fillEllipse(9, 6, 4, 3, lite);   // 顶部受光面
            p.fillEllipse(14, 10, 4, 2, dark); // 背光面
            p.set(8, 5, Color{190, 188, 182, 255}); // 高光点
            return p;
        }
        default: return PixBuf(1, 1);
    }
}

// ---------------- 载具（朝东为基准） ----------------
static void drawTracks(PixBuf& p, int cx, int cy, int len, int wid) {
    // 上下两条履带（朝东 => 长轴水平）
    p.fillRect(cx - len / 2, cy - wid / 2, len, 4, Pal::TRACK);
    p.fillRect(cx - len / 2, cy + wid / 2 - 4, len, 4, Pal::TRACK);
    for (int i = 0; i < len; i += 3) {
        p.hline(cx - len / 2 + i, cx - len / 2 + i, cy - wid / 2, Pal::TRACK_HI);
        p.hline(cx - len / 2 + i, cx - len / 2 + i, cy + wid / 2 - 1, Pal::TRACK_HI);
    }
}

PixBuf SpriteBank::baseUnitBody(UnitType t, int dir, int frame) {
    // 步兵
    const UnitDef& d = unitDef(t);
    if (d.isInfantry()) {
        PixBuf p(20, 26);
        // 选择面向：E 基准侧视，S 正面，N 背面
        int facing = 0; // 0=E,1=S,2=N
        int dd = dir & 7;
        if (dd == 1 || dd == 2 || dd == 3) facing = 1;
        else if (dd == 5 || dd == 6 || dd == 7) facing = 2;
        bool flip = (dd == 3 || dd == 4 || dd == 5);
        Color uniform = d.type == UnitType::Conscript ? Color{120, 96, 60, 255}
                      : d.type == UnitType::PLA ? Color{86, 110, 60, 255}
                      : d.type == UnitType::AttackDog ? Color{120, 90, 60, 255}
                      : d.type == UnitType::Sniper ? Color{60, 90, 50, 255}        // 狙击手：丛林迷彩
                      : d.type == UnitType::TeslaTrooper ? Color{50, 70, 120, 255} // 磁暴步兵：深蓝重甲
                      : d.type == UnitType::FlakTrooper ? Color{96, 82, 66, 255}   // 高射步兵：灰褐
                      : d.type == UnitType::Tanya ? Color{140, 90, 70, 255}        // 谭雅：棕色作战服
                      : d.type == UnitType::Desolator ? Color{80, 96, 48, 255}     // 辐射工兵：暗绿防化服
                      : d.type == UnitType::Chrono ? Color{70, 90, 130, 255}       // 超时空兵：蓝白作战服
                      : d.type == UnitType::GuardianGI ? Color{70, 76, 92, 255}    // 重装大兵：深灰蓝重甲
                      : d.type == UnitType::CrazyIvan ? Color{110, 60, 52, 255}    // 疯狂伊文：暗红工装
                      : d.type == UnitType::Terrorist ? Color{196, 188, 168, 255}  // 恐怖分子：灰白长袍
                      : d.type == UnitType::NavySEAL ? Color{46, 62, 84, 255}      // 海豹部队：深蓝作战服
                      : d.type == UnitType::Yuri ? Color{96, 84, 110, 255}         // 尤里：暗紫长袍
                      : d.type == UnitType::ChronoCommando ? Color{60, 84, 116, 255} // 超时空突击队：蓝黑
                      : d.type == UnitType::PsiCommando ? Color{84, 76, 108, 255}  // 心灵突击队：紫黑
                      : d.type == UnitType::Initiate ? Color{150, 50, 50, 255}    // 尤里新兵：暗红长袍（心灵火焰）
                      : d.type == UnitType::Brute ? Color{120, 90, 70, 255}       // 狂兽人：棕色肌肉皮甲
                      : d.type == UnitType::Virus ? Color{60, 100, 60, 255}       // 病毒狙击手：绿色生化服
                      : d.type == UnitType::Boris ? Color{90, 70, 50, 255}        // 鲍里斯：苏军棕色作战服
                      : Color{88, 96, 104, 255};
        int cx = 10, by = 25;
        if (d.type == UnitType::AttackDog) {
            // 狗：低矮四足
            p.fillEllipse(cx, by - 6, 7, 3, uniform);
            p.fillEllipse(cx + 6, by - 8, 3, 2, uniform); // 头
            p.set(cx + 8, by - 9, Pal::GUN);
            for (int i = 0; i < 4; i++) p.line(cx - 5 + i * 3, by - 4, cx - 5 + i * 3, by - (frame ? (i % 2) : ((i + 1) % 2)), uniform);
            p.line(cx - 7, by - 7, cx - 9, by - 10, uniform); // 尾
            if (flip) p = p.flipH();
            return p;
        }
        int legOff = frame ? 2 : 0;
        if (facing == 1) { // 正面
            p.fillEllipse(cx, by - 21, 3, 3, Pal::SKIN);                    // 头
            p.fillEllipse(cx, by - 23, 3, 2, Pal::REMAP);                   // 头盔(阵营色)
            p.fillRect(cx - 4, by - 18, 8, 9, uniform);                     // 身
            p.hline(cx - 4, cx + 4, by - 14, Pal::REMAP_DARK);              // 腰带
            p.fillRect(cx - 4, by - 9, 3, 8 - legOff, Color{60, 62, 66, 255});
            p.fillRect(cx + 1, by - 9, 3, 8 - (frame ? 0 : 2), Color{60, 62, 66, 255});
            p.fillRect(cx + 4, by - 17, 6, 2, Pal::GUN);                    // 枪
        } else if (facing == 0) { // 侧视（朝东）
            p.fillEllipse(cx + 1, by - 21, 3, 3, Pal::SKIN);
            p.fillEllipse(cx + 1, by - 23, 3, 2, Pal::REMAP);
            p.fillRect(cx - 3, by - 18, 7, 9, uniform);
            p.hline(cx - 3, cx + 4, by - 14, Pal::REMAP_DARK);
            p.fillRect(cx - 2, by - 9, 3, 8 - legOff, Color{60, 62, 66, 255});
            p.fillRect(cx + 1, by - 9, 3, 8 - (frame ? 0 : 2), Color{60, 62, 66, 255});
            p.fillRect(cx + 2, by - 16, 8, 2, Pal::GUN);
        } else { // 背面
            p.fillEllipse(cx, by - 22, 3, 3, Pal::REMAP);                   // 头盔
            p.fillRect(cx - 4, by - 18, 8, 9, uniform);
            p.fillRect(cx - 3, by - 17, 6, 5, Color{70, 74, 80, 255});      // 背包
            p.hline(cx - 4, cx + 4, by - 14, Pal::REMAP_DARK);
            p.fillRect(cx - 4, by - 9, 3, 8 - legOff, Color{60, 62, 66, 255});
            p.fillRect(cx + 1, by - 9, 3, 8 - (frame ? 0 : 2), Color{60, 62, 66, 255});
        }
        // ---- RA2 体积感明暗：制服上沿受光提亮、下摆压暗、腿端深色军靴（装备层在其后绘制不受影响）----
        auto shade = [&](int x, int y, int dr, int dg, int db) {
            Color c = p.get(x, y);
            if (c.a < 200) return;
            p.set(x, y, Color{(uint8_t)clampi(c.r + dr, 0, 255), (uint8_t)clampi(c.g + dg, 0, 255), (uint8_t)clampi(c.b + db, 0, 255), c.a});
        };
        for (int y = by - 19; y <= by - 10; y++)
            for (int x = cx - 6; x <= cx + 6; x++) {
                if (y <= by - 18) shade(x, y, 22, 22, 20);          // 上沿受光
                else if (y >= by - 11) shade(x, y, -26, -26, -24);  // 下摆背光
            }
        for (int y = by - 3; y <= by - 1; y++)                      // 军靴
            for (int x = cx - 5; x <= cx + 5; x++) shade(x, y, -34, -34, -32);
        // ---- 特殊兵种装备（在基础身体之上叠加）----
        if (d.type == UnitType::Sniper) {
            // 狙击手：超长狙击枪 + 瞄准镜 + 伪装草冠
            if (facing == 0) { // 侧视
                p.fillRect(cx + 2, by - 16, 12, 1, Pal::GUN);      // 长枪管
                p.set(cx + 13, by - 17, Pal::GUN);
                p.fillEllipse(cx + 5, by - 17, 2, 1, Color{120, 200, 255, 255}); // 瞄准镜
            } else if (facing == 1) {
                p.fillRect(cx + 4, by - 17, 8, 1, Pal::GUN);
            }
            p.fillEllipse(cx + (facing == 0 ? 1 : 0), by - 24, 4, 1, Color{70, 110, 50, 255}); // 草冠
        } else if (d.type == UnitType::TeslaTrooper) {
            // 磁暴步兵：肩甲 + 磁暴发射器（蓝色电弧球）
            p.fillRect(cx - 5, by - 19, 10, 2, Color{70, 90, 150, 255}); // 肩甲
            if (facing == 0) {
                p.fillRect(cx + 2, by - 16, 6, 3, Pal::GUN);             // 发射器身
                p.fillEllipse(cx + 8, by - 15, 2, 2, Color{120, 180, 255, 255}); // 电弧球
                p.set(cx + 8, by - 16, Color{220, 240, 255, 255});
            } else if (facing == 1) {
                p.fillEllipse(cx + 5, by - 15, 2, 2, Color{120, 180, 255, 255});
            }
        } else if (d.type == UnitType::FlakTrooper) {
            // 高射步兵：背负式高射炮管（斜向上）
            if (facing == 0) {
                p.line(cx - 2, by - 14, cx + 5, by - 24, Pal::GUN);  // 炮管
                p.fillRect(cx + 4, by - 25, 3, 2, Color{70, 74, 80, 255}); // 炮口
            } else {
                p.line(cx, by - 14, cx + 3, by - 24, Pal::GUN);
            }
            p.fillRect(cx - 3, by - 17, 4, 5, Color{80, 70, 56, 255}); // 弹药背包
        } else if (d.type == UnitType::Tanya) {
            // 谭雅：双枪 + 红色贝雷帽
            p.fillEllipse(cx + (facing == 0 ? 1 : 0), by - 23, 3, 2, Color{170, 50, 40, 255}); // 贝雷帽
            if (facing == 0) {
                p.fillRect(cx + 2, by - 15, 5, 2, Pal::GUN); // 右手枪
                p.fillRect(cx - 5, by - 13, 5, 2, Pal::GUN); // 左手枪
            } else if (facing == 1) {
                p.fillRect(cx - 6, by - 15, 4, 2, Pal::GUN);
                p.fillRect(cx + 3, by - 15, 4, 2, Pal::GUN);
            }
        } else if (d.type == UnitType::Desolator) {
            // 辐射工兵：防毒面具 + 辐射炮 + 胸前警告灯
            p.fillEllipse(cx + (facing == 0 ? 2 : 0), by - 20, 2, 2, Color{40, 44, 40, 255}); // 面具
            p.set(cx + (facing == 0 ? 3 : -1), by - 21, Color{120, 220, 120, 255});          // 目镜
            p.set(cx, by - 15, Color{180, 255, 80, 255});                                    // 警告灯
            if (facing == 0) {
                p.fillRect(cx + 2, by - 15, 8, 3, Color{60, 80, 44, 255}); // 辐射炮身
                p.fillEllipse(cx + 10, by - 14, 2, 2, Color{140, 230, 90, 255}); // 辐射核心
            }
        } else if (d.type == UnitType::Chrono) {
            // 超时空军团兵：背部传送装置 + 手持抹除枪（白色能量口）
            p.fillRect(cx - 4, by - 19, 5, 7, Color{90, 110, 150, 255}); // 背包
            p.set(cx - 3, by - 20, Color{160, 220, 255, 255});           // 背包灯
            if (facing == 0) {
                p.fillRect(cx + 2, by - 16, 7, 3, Color{100, 120, 160, 255}); // 抹除枪
                p.fillEllipse(cx + 9, by - 15, 2, 2, Color{230, 245, 255, 255}); // 能量口
            } else if (facing == 1) {
                p.fillEllipse(cx + 4, by - 15, 2, 2, Color{230, 245, 255, 255});
            }
        } else if (d.type == UnitType::GuardianGI) {
            // 重装大兵：厚肩甲 + 重型冲锋枪
            p.fillRect(cx - 5, by - 19, 10, 3, Color{96, 104, 124, 255}); // 厚肩甲
            if (facing == 0) {
                p.fillRect(cx + 2, by - 15, 9, 3, Pal::GUN);          // 重枪身
                p.fillRect(cx + 9, by - 14, 3, 2, Color{60, 64, 70, 255}); // 弹匣
            } else if (facing == 1) {
                p.fillRect(cx + 4, by - 15, 6, 3, Pal::GUN);
            }
        } else if (d.type == UnitType::CrazyIvan) {
            // 疯狂伊文：炸药背包 + 手持雷管（红色）
            p.fillRect(cx - 4, by - 18, 4, 6, Color{150, 60, 40, 255}); // 炸药包
            p.set(cx - 3, by - 19, Color{255, 120, 60, 255});           // 引线火花
            if (facing == 0) {
                p.fillRect(cx + 3, by - 14, 4, 3, Color{170, 50, 40, 255}); // 手持炸药
            } else if (facing == 1) {
                p.fillRect(cx + 4, by - 14, 4, 3, Color{170, 50, 40, 255});
            }
        } else if (d.type == UnitType::Terrorist) {
            // 恐怖分子：白头巾 + 胸前炸药带（红捆 + 引线）
            p.fillEllipse(cx + (facing == 0 ? 1 : 0), by - 22, 3, 2, Color{235, 230, 215, 255}); // 头巾
            p.fillRect(cx - 3, by - 17, 6, 5, Color{170, 50, 40, 255});  // 炸药带
            p.hline(cx - 3, cx + 3, by - 15, Color{60, 54, 48, 255});    // 捆绳
            p.set(cx + 1, by - 18, Color{255, 150, 70, 255});            // 引线火花
        } else if (d.type == UnitType::NavySEAL) {
            // 海豹部队：蛙人目镜 + 冲锋枪 + 胸前弹带
            p.hline(cx - 2, cx + 3, by - 21, Color{90, 200, 235, 255});  // 目镜
            p.hline(cx - 4, cx + 4, by - 13, Color{150, 130, 80, 255});  // 弹带
            if (facing == 0) {
                p.fillRect(cx + 2, by - 15, 8, 2, Pal::GUN);             // 冲锋枪
                p.fillRect(cx + 5, by - 13, 2, 3, Pal::GUN);             // 弹匣
            } else if (facing == 1) {
                p.fillRect(cx + 4, by - 15, 6, 2, Pal::GUN);
            }
        } else if (d.type == UnitType::Yuri) {
            // 尤里：光头（无头盔）+ 心灵头环 + 长袍下摆
            int hx = cx + (facing == 0 ? 1 : 0);
            p.fillEllipse(hx, by - 23, 3, 2, Pal::SKIN);                 // 盖住阵营头盔→光头
            p.hline(hx - 3, hx + 3, by - 22, Color{200, 170, 60, 255});  // 金头环
            p.set(hx - 1, by - 23, Color{150, 90, 220, 255});            // 心灵宝石
            p.fillRect(cx - 4, by - 10, 8, 9, uniform);                  // 长袍盖腿
            p.hline(cx - 4, cx + 4, by - 2, Color{60, 50, 74, 255});
        } else if (d.type == UnitType::ChronoCommando) {
            // 超时空突击队：贝雷帽 + 小型超时空背包 + 消音冲锋枪
            p.fillEllipse(cx + (facing == 0 ? 1 : 0), by - 23, 3, 2, Color{40, 90, 60, 255}); // 贝雷帽
            p.fillRect(cx - 4, by - 19, 4, 6, Color{90, 110, 150, 255}); // 迷你时空包
            p.set(cx - 3, by - 20, Color{160, 220, 255, 255});           // 背包灯
            if (facing == 0) {
                p.fillRect(cx + 2, by - 15, 8, 2, Pal::GUN);
                p.fillRect(cx + 9, by - 15, 3, 1, Color{90, 94, 100, 255}); // 消音管
            } else if (facing == 1) {
                p.fillRect(cx + 4, by - 15, 6, 2, Pal::GUN);
            }
        } else if (d.type == UnitType::PsiCommando) {
            // 心灵突击队：光头头环（承尤里）+ 战术背带 + 手枪
            int hx = cx + (facing == 0 ? 1 : 0);
            p.fillEllipse(hx, by - 23, 3, 2, Pal::SKIN);
            p.hline(hx - 3, hx + 3, by - 22, Color{170, 140, 220, 255}); // 紫头环
            p.set(hx, by - 23, Color{200, 120, 255, 255});
            p.line(cx - 4, by - 18, cx + 3, by - 12, Color{50, 44, 60, 255}); // 斜背带
            if (facing == 0) p.fillRect(cx + 2, by - 14, 5, 2, Pal::GUN);
        } else if (d.type == UnitType::Initiate) {
            // 尤里新兵：兜帽（无头盔）+ 双手心灵火焰
            int hx = cx + (facing == 0 ? 1 : 0);
            p.fillEllipse(hx, by - 23, 3, 2, uniform);                   // 兜帽盖头盔
            p.set(hx, by - 21, Pal::SKIN);                               // 露脸
            if (facing == 0) {
                p.fillEllipse(cx + 4, by - 13, 2, 2, Color{255, 120, 40, 255}); // 火焰球
                p.set(cx + 5, by - 14, Color{255, 200, 80, 255});
            } else if (facing == 1) {
                p.fillEllipse(cx + 3, by - 13, 2, 2, Color{255, 120, 40, 255});
            }
        } else if (d.type == UnitType::Brute) {
            // 狂兽人：光头 + 巨拳 + 肩刺
            int hx = cx + (facing == 0 ? 1 : 0);
            p.fillEllipse(hx, by - 23, 3, 2, Pal::SKIN);                 // 光头
            p.fillRect(cx - 5, by - 19, 10, 2, Color{90, 66, 50, 255});  // 肩甲皮带
            if (facing == 0) {
                p.fillRect(cx + 4, by - 14, 4, 4, Pal::SKIN);            // 巨拳
                p.set(cx + 6, by - 16, Color{200, 180, 140, 255});
            } else if (facing == 1) {
                p.fillRect(cx - 8, by - 14, 4, 4, Pal::SKIN);
                p.fillRect(cx + 4, by - 14, 4, 4, Pal::SKIN);
            }
        } else if (d.type == UnitType::Virus) {
            // 病毒狙击手：防毒面具 + 长狙击步枪 + 绿色生化罐
            p.fillEllipse(cx + (facing == 0 ? 2 : 0), by - 20, 2, 2, Color{40, 60, 40, 255}); // 面具
            p.fillRect(cx - 3, by - 17, 3, 4, Color{50, 90, 50, 255});   // 生化罐
            p.set(cx - 2, by - 16, Color{120, 255, 120, 255});
            if (facing == 0) {
                p.fillRect(cx + 2, by - 16, 13, 1, Pal::GUN);            // 长狙击枪
                p.set(cx + 14, by - 17, Pal::GUN);
            } else if (facing == 1) {
                p.fillRect(cx + 4, by - 16, 10, 1, Pal::GUN);
            }
        } else if (d.type == UnitType::Boris) {
            // 鲍里斯：苏军军帽 + AK47 + 通话耳机
            p.fillRect(cx - 3, by - 24, 7, 2, Color{70, 50, 40, 255});   // 军帽
            p.set(cx, by - 25, Color{180, 150, 60, 255});                // 帽徽
            p.set(cx - 4, by - 20, Color{60, 60, 60, 255});              // 耳机
            p.set(cx + 4, by - 20, Color{60, 60, 60, 255});
            if (facing == 0) {
                p.fillRect(cx + 2, by - 15, 9, 2, Pal::GUN);             // AK47
                p.fillRect(cx + 8, by - 15, 2, 3, Color{80, 60, 40, 255}); // 弹匣
            } else if (facing == 1) {
                p.fillRect(cx + 4, by - 15, 7, 2, Pal::GUN);
            }
        }
        if (flip) p = p.flipH();
        return p;
    }
    if (d.isAir()) {
        if (t == UnitType::Kirov) {
            // 基洛夫空艇：巨型椭圆气囊 + 尾翼 + 吊舱（朝东基准）
            PixBuf p(64, 48);
            int cx = 32, cy = 20;
            Color body{136, 110, 92, 255}, dark{96, 76, 62, 255};
            p.fillEllipse(cx, cy, 26, 11, body);              // 主气囊
            p.ellipse(cx, cy, 26, 11, dark);
            p.fillEllipse(cx + 16, cy, 9, 8, body);           // 首部收拢
            for (int i = -6; i <= 6; i += 4)                  // 气囊纵线
                p.line(cx - 22, cy + i, cx + 22, cy + i, dark);
            p.hline(cx - 20, cx + 10, cy - 8, Pal::REMAP);    // 阵营条纹
            // 尾翼（西侧上下两片）
            p.line(cx - 24, cy, cx - 30, cy - 8, dark);
            p.line(cx - 24, cy, cx - 30, cy + 8, dark);
            p.line(cx - 30, cy - 8, cx - 27, cy, dark);
            p.line(cx - 30, cy + 8, cx - 27, cy, dark);
            // 吊舱
            p.fillRect(cx - 6, cy + 11, 14, 5, Pal::STEEL_DARK);
            p.rect(cx - 6, cy + 11, 14, 5, Pal::GUN);
            p.set(cx + 6, cy + 13, Pal::GLASS);
            // 螺旋桨
            p.line(cx - 2, cy + 16, cx + 4, cy + 16, Pal::GUN);
            return p;
        }
        if (t == UnitType::Rocketeer) {
            // 火箭飞行兵：飞行姿态步兵 + 背部火箭包 + 尾焰（朝东基准）
            PixBuf p(32, 32);
            int cx = 16, cy = 14;
            Color suit{88, 96, 110, 255};
            p.fillEllipse(cx + 4, cy - 6, 3, 3, Pal::SKIN);   // 头
            p.fillEllipse(cx + 4, cy - 8, 3, 2, Pal::REMAP);  // 头盔
            p.fillRect(cx - 2, cy - 4, 9, 7, suit);           // 身（前倾）
            p.fillRect(cx - 5, cy - 3, 4, 6, Pal::STEEL_DARK);// 火箭包
            p.fillRect(cx + 6, cy - 2, 6, 2, Pal::GUN);       // 卡宾枪
            p.line(cx + 1, cy + 3, cx - 3, cy + 8, suit);     // 摆腿
            p.line(cx + 4, cy + 3, cx + 1, cy + 9, suit);
            // 尾焰
            p.set(cx - 6, cy + 3, Color{255, 200, 90, 255});
            p.set(cx - 7, cy + 4, Color{255, 160, 60, 255});
            p.set(cx - 6, cy + 5, Color{255, 230, 150, 255});
            if (dir) p = p.rotate8(dir);
            return p;
        }
        if (t == UnitType::Nighthawk) {
            // 夜鹰直升机：运直机身 + 主旋翼（半透明扫掠盘）+ 尾梁尾桨 + 起落橇（朝东基准）
            PixBuf p(52, 44);
            int cx = 24, cy = 22;
            Color body{88, 96, 84, 255}, dark{58, 64, 54, 255};
            // 旋翼扫掠盘（两层半透明长条，模拟转动残影）
            p.hline(cx - 20, cx + 16, cy - 9, Color{30, 32, 30, 90});
            p.hline(cx - 16, cx + 20, cy - 8, Color{30, 32, 30, 70});
            p.line(cx - 19, cy - 10, cx + 17, cy - 7, Color{50, 52, 48, 120});
            // 尾梁
            p.fillRect(cx - 20, cy - 1, 12, 3, body);
            p.hline(cx - 20, cx - 9, cy - 1, dark);
            // 尾桨 + 垂尾
            p.line(cx - 20, cy - 5, cx - 20, cy + 5, dark);
            p.line(cx - 21, cy - 4, cx - 19, cy + 4, Color{40, 42, 38, 150});
            p.line(cx - 18, cy, cx - 15, cy - 5, dark);
            // 主机身（圆头运输舱）
            p.fillEllipse(cx + 2, cy + 1, 11, 6, body);
            p.ellipse(cx + 2, cy + 1, 11, 6, dark);
            p.fillEllipse(cx + 2, cy - 1, 9, 4, Color{104, 112, 98, 255}); // 背部受光
            // 座舱玻璃
            p.fillEllipse(cx + 8, cy, 4, 3, Pal::GLASS);
            p.set(cx + 10, cy - 1, Color{200, 235, 250, 255});
            // 旋翼桅杆
            p.fillRect(cx, cy - 8, 3, 5, Pal::GUN);
            p.set(cx + 1, cy - 9, Pal::STEEL_DARK);
            // 舱门机枪 + 阵营条纹
            p.fillRect(cx + 4, cy + 4, 7, 2, Pal::GUN);
            p.hline(cx - 6, cx + 10, cy + 5, Pal::REMAP);
            // 起落橇
            p.hline(cx - 4, cx + 10, cy + 9, Pal::GUN);
            p.line(cx - 1, cy + 6, cx - 1, cy + 9, Pal::GUN);
            p.line(cx + 7, cy + 6, cx + 7, cy + 9, Pal::GUN);
            if (dir) p = p.rotate8(dir);
            return p;
        }
        PixBuf p(44, 44);
        int cx = 22, cy = 22;
        Color body, dark;
        if (t == UnitType::Intruder)      { body = Color{148, 158, 172, 255}; dark = Color{108, 116, 130, 255}; } // 灰蓝
        else if (t == UnitType::MiG)      { body = Color{172, 170, 162, 255}; dark = Color{128, 124, 116, 255}; } // 银灰
        else if (t == UnitType::Hornet)   { body = Color{126, 138, 152, 255}; dark = Color{88, 98, 112, 255};   } // 舰载机：浅钢蓝
        else                              { body = Color{74, 80, 92, 255};   dark = Color{46, 50, 60, 255};   } // 黑鹰：深灰黑
        // 后掠主翼（三角填充，左右对称）
        for (int s = -1; s <= 1; s += 2)
            for (int i = 0; i <= 9; i++) {
                int wx = cx + 2 - i;                 // 翼根向后掠
                p.line(wx, cy + s * 2, wx, cy + s * (2 + i), dark);
            }
        // 水平尾翼
        for (int s = -1; s <= 1; s += 2)
            for (int i = 0; i <= 4; i++) {
                int wx = cx - 10 - i;
                p.line(wx, cy + s * 1, wx, cy + s * (1 + i), dark);
            }
        // 机身
        p.fillEllipse(cx, cy, 13, 3, body);
        p.ellipse(cx, cy, 13, 3, dark);
        // 机头尖
        p.line(cx + 10, cy, cx + 15, cy, body);
        // 座舱盖
        p.fillEllipse(cx + 4, cy, 3, 2, Pal::GLASS);
        // 尾喷口
        p.fillRect(cx - 14, cy - 2, 2, 4, Pal::GUN);
        p.set(cx - 15, cy - 1, Color{255, 160, 60, 255});
        p.set(cx - 15, cy, Color{255, 200, 90, 255});
        // 阵营条纹（机身中线）
        p.hline(cx - 8, cx + 1, cy, Pal::REMAP);
        // 机翼徽记
        p.set(cx - 3, cy - 7, Pal::REMAP);
        p.set(cx - 3, cy + 7, Pal::REMAP);
        if (dir) p = p.rotate8(dir);
        return p;
    }

    // 载具：基准朝东，正方形画布
    int cs = 44; // canvas size
    if (t == UnitType::Apocalypse || t == UnitType::MCV || t == UnitType::BattleFortress) cs = 56;
    if (t == UnitType::Harvester || t == UnitType::ChronoMiner || t == UnitType::WarMiner) cs = 52;
    if (t == UnitType::Destroyer || t == UnitType::Typhoon || t == UnitType::Aegis || t == UnitType::AmphTransport) cs = 56;
    if (t == UnitType::SeaScorpion || t == UnitType::Squid) cs = 48;
    if (t == UnitType::Dreadnought || t == UnitType::AircraftCarrier) cs = 64;
    PixBuf p(cs, cs);
    int cx = cs / 2, cy = cs / 2;

    auto hull = [&](int rx, int ry, Color body) {
        p.fillEllipse(cx, cy, rx, ry, body);
        p.ellipse(cx, cy, rx, ry, Pal::GUN);
    };

    // 船体（朝东）：尖艏长条 + 甲板线
    auto shipHull = [&](int L, int W, Color body) {
        // 主船体
        p.fillEllipse(cx, cy, L / 2, W / 2, body);
        p.ellipse(cx, cy, L / 2, W / 2, Pal::GUN);
        // 尖艏（东侧三角收拢）
        for (int i = 0; i < W / 2; i++) {
            int bx = cx + L / 2 - 2 + i / 2;
            p.line(bx, cy - (W / 2 - i), bx, cy + (W / 2 - i), body);
        }
        // 甲板中线
        p.hline(cx - L / 2 + 3, cx + L / 2 - 2, cy, Color{(uint8_t)(body.r * 0.7f), (uint8_t)(body.g * 0.7f), (uint8_t)(body.b * 0.7f), 255});
        // 水线（阵营色）
        p.hline(cx - L / 2 + 4, cx + L / 2 - 6, cy + W / 2 - 1, Pal::REMAP);
    };

    switch (t) {
        case UnitType::Grizzly: case UnitType::Rhino: case UnitType::Type99:
        case UnitType::MirageTank: case UnitType::TeslaTank: case UnitType::PrismTank: {
            int L = t == UnitType::Type99 ? 30 : 26;
            int W = t == UnitType::Type99 ? 20 : 17;
            drawTracks(p, cx, cy, L, W);
            Color body = t == UnitType::MirageTank ? Color{92, 104, 72, 255} : Pal::STEEL;
            hull(L / 2 - 2, W / 2 - 2, body);
            // 车头驾驶舱
            p.fillEllipse(cx + L / 2 - 8, cy, 4, 4, Pal::STEEL_DARK);
            // 阵营条纹
            p.hline(cx - L / 2 + 4, cx + L / 2 - 10, cy - W / 2 + 3, Pal::REMAP);
            if (t == UnitType::MirageTank) { // 迷彩斑点
                Rng r(7);
                for (int i = 0; i < 10; i++)
                    p.fillEllipse(cx + r.range(-10, 10), cy + r.range(-5, 5), 2, 1, Color{70, 84, 54, 255});
            }
            if (t == UnitType::PrismTank) { // 光棱镜面
                p.fillRect(cx - 4, cy - 5, 8, 10, Color{180, 220, 235, 255});
                p.rect(cx - 4, cy - 5, 8, 10, Pal::STEEL_DARK);
            }
            break;
        }
        case UnitType::Apocalypse: {
            drawTracks(p, cx, cy, 40, 26);
            hull(17, 10, Color{88, 74, 60, 255});
            p.hline(cx - 16, cx + 8, cy - 9, Pal::REMAP);
            p.hline(cx - 16, cx + 8, cy + 9, Pal::REMAP);
            p.fillEllipse(cx + 8, cy, 5, 5, Pal::STEEL_DARK);
            break;
        }
        case UnitType::IFV: case UnitType::FlakTrack: {
            drawTracks(p, cx, cy, 22, 15);
            hull(9, 6, Pal::STEEL_LITE);
            p.fillEllipse(cx + 5, cy, 3, 3, Pal::GLASS);
            p.hline(cx - 7, cx + 3, cy - 5, Pal::REMAP);
            break;
        }
        case UnitType::V3Launcher: {
            drawTracks(p, cx, cy, 24, 16);
            hull(10, 6, Color{100, 92, 70, 255});
            // 背部导弹
            p.fillRect(cx - 10, cy - 3, 16, 6, Color{170, 60, 50, 255});
            p.fillRect(cx + 6, cy - 2, 4, 4, Color{220, 200, 180, 255});
            p.hline(cx - 10, cx + 2, cy - 6, Pal::REMAP);
            break;
        }
        case UnitType::Harvester: {
            drawTracks(p, cx, cy, 34, 22);
            hull(14, 8, Color{110, 104, 88, 255});
            // 货舱
            p.fillRect(cx - 12, cy - 5, 16, 10, Pal::STEEL_DARK);
            if (frame) { // 满载：金矿石
                Rng r(3);
                for (int i = 0; i < 12; i++)
                    p.fillEllipse(cx - 10 + r.range(0, 13), cy - 4 + r.range(0, 8), 2, 1, Pal::ORE_GOLD);
            }
            p.fillEllipse(cx + 10, cy, 4, 5, Pal::GLASS); // 驾驶室
            p.hline(cx - 14, cx - 8, cy - 8, Pal::REMAP);
            break;
        }
        case UnitType::ChronoMiner: {
            // 超时空采矿车：浅色盟军校平车体 + 小型货舱 + 车顶超时空装置（蓝光环）
            drawTracks(p, cx, cy, 30, 19);
            hull(12, 7, Color{128, 134, 142, 255});
            p.fillRect(cx - 11, cy - 4, 13, 8, Color{86, 92, 102, 255}); // 货舱
            if (frame) { // 满载：金矿
                Rng r(3);
                for (int i = 0; i < 9; i++)
                    p.fillEllipse(cx - 9 + r.range(0, 11), cy - 3 + r.range(0, 6), 2, 1, Pal::ORE_GOLD);
            }
            p.fillEllipse(cx + 9, cy, 3, 4, Pal::GLASS);              // 驾驶室
            // 超时空装置：车顶环 + 蓝光芯
            p.ellipse(cx - 2, cy - 7, 5, 2, Color{120, 190, 255, 255});
            p.set(cx - 2, cy - 8, Color{210, 240, 255, 255});
            p.set(cx - 1, cy - 7, Color{150, 210, 255, 255});
            p.hline(cx - 13, cx - 8, cy - 7, Pal::REMAP);
            break;
        }
        case UnitType::WarMiner: {
            // 武装采矿车：苏式重型车体 + 大倾角货斗 + 车顶机枪塔
            drawTracks(p, cx, cy, 36, 24);
            hull(15, 9, Color{96, 84, 66, 255});
            p.fillRect(cx - 13, cy - 6, 18, 12, Color{70, 60, 46, 255}); // 货斗
            p.hline(cx - 13, cx + 5, cy - 6, Color{96, 84, 66, 255});    // 斗沿
            if (frame) { // 满载：金矿
                Rng r(3);
                for (int i = 0; i < 14; i++)
                    p.fillEllipse(cx - 11 + r.range(0, 15), cy - 5 + r.range(0, 9), 2, 1, Pal::ORE_GOLD);
            }
            p.fillEllipse(cx + 11, cy, 4, 5, Pal::GLASS);               // 驾驶室
            // 机枪塔
            p.fillEllipse(cx + 2, cy - 6, 4, 3, Pal::STEEL_DARK);
            p.fillRect(cx + 4, cy - 7, 9, 2, Pal::GUN);
            p.hline(cx - 15, cx - 9, cy - 9, Pal::REMAP);
            p.hline(cx - 15, cx - 9, cy + 9, Pal::REMAP_DARK);
            break;
        }
        case UnitType::TankDestroyer: {
            // 坦克杀手：低矮楔形车体 + 超长身管（固定战斗室风格）
            drawTracks(p, cx, cy, 28, 18);
            // 楔形首上
            for (int i = 0; i < 6; i++) p.hline(cx + 4 + i, cx + 14, cy - 5 + i, Color{86, 92, 100, 255});
            hull(12, 7, Color{96, 102, 110, 255});
            p.fillRect(cx - 8, cy - 4, 12, 8, Color{80, 86, 94, 255});   // 战斗室
            p.rect(cx - 8, cy - 4, 12, 8, Pal::GUN);
            p.fillRect(cx + 4, cy - 1, 20, 3, Pal::GUN);                 // 超长炮管
            p.fillRect(cx + 22, cy - 2, 3, 5, Pal::GUN);                 // 炮口制退器
            p.hline(cx - 12, cx - 4, cy - 6, Pal::REMAP);
            break;
        }
        case UnitType::DemoTruck: {
            // 自爆卡车：民用卡车底盘 + 货厢核弹（绿桶 + 辐射标志）
            drawTracks(p, cx, cy, 26, 16);
            hull(11, 6, Color{110, 96, 70, 255});
            p.fillEllipse(cx + 8, cy, 4, 4, Pal::GLASS);                 // 驾驶室
            p.fillRect(cx - 12, cy - 5, 14, 10, Color{88, 76, 56, 255}); // 货厢
            // 核弹桶
            p.fillEllipse(cx - 5, cy, 5, 4, Color{90, 130, 70, 255});
            p.ellipse(cx - 5, cy, 5, 4, Pal::GUN);
            p.hline(cx - 9, cx - 1, cy, Color{60, 96, 46, 255});
            // 辐射标志（黄底黑标）
            p.fillEllipse(cx - 5, cy, 2, 2, Color{240, 220, 60, 255});
            p.set(cx - 5, cy, Pal::GUN);
            p.hline(cx - 12, cx + 2, cy - 6, Pal::REMAP);
            break;
        }
        case UnitType::RobotTank: {
            // 遥控坦克：悬浮车体（无履带，气垫裙）+ 顶部天线
            p.fillEllipse(cx, cy + 6, 12, 3, Color{30, 32, 36, 160});    // 气垫阴影
            hull(10, 5, Color{120, 128, 140, 255});
            p.hline(cx - 9, cx + 9, cy + 4, Color{70, 76, 88, 255});     // 气垫裙
            p.fillEllipse(cx + 6, cy - 1, 3, 2, Color{140, 150, 165, 255});
            p.line(cx - 4, cy - 3, cx - 4, cy - 10, Pal::GUN);           // 遥控天线
            p.set(cx - 4, cy - 11, Color{255, 90, 80, 255});
            p.hline(cx - 8, cx + 4, cy - 4, Pal::REMAP);
            break;
        }
        case UnitType::BattleFortress: {
            // 战斗要塞：四履带重型底盘 + 堡垒式上层 + 射击孔
            drawTracks(p, cx, cy - 8, 36, 8);
            drawTracks(p, cx, cy + 8, 36, 8);
            p.fillRect(cx - 18, cy - 11, 36, 22, Color{104, 108, 116, 255});
            p.rect(cx - 18, cy - 11, 36, 22, Pal::GUN);
            // 楔形首装甲
            for (int i = 0; i < 5; i++) p.hline(cx + 18, cx + 23 - i, cy - 9 + i * 2, Color{88, 92, 100, 255});
            // 上层堡垒
            p.fillRect(cx - 10, cy - 8, 18, 16, Color{88, 92, 100, 255});
            p.rect(cx - 10, cy - 8, 18, 16, Pal::GUN);
            for (int i = 0; i < 3; i++) {                                 // 射击孔
                p.set(cx - 6 + i * 6, cy - 4, Color{20, 22, 26, 255});
                p.set(cx - 6 + i * 6, cy + 3, Color{20, 22, 26, 255});
            }
            p.fillRect(cx + 8, cy - 4, 6, 8, Pal::GLASS);                 // 驾驶观察窗
            p.hline(cx - 16, cx - 2, cy - 11, Pal::REMAP);
            p.hline(cx - 16, cx - 2, cy + 11, Pal::REMAP_DARK);
            break;
        }
        case UnitType::Dolphin: {
            // 海豚：流线灰身 + 背鳍 + 尾鳍 + 白腹（朝东跃水姿态）
            Color skin{150, 160, 172, 255}, belly{210, 216, 224, 255};
            p.fillEllipse(cx, cy, 13, 5, skin);                           // 主体
            p.fillEllipse(cx + 1, cy + 2, 10, 3, belly);                  // 白腹
            // 吻部
            p.line(cx + 11, cy - 1, cx + 16, cy, skin);
            p.line(cx + 11, cy + 1, cx + 16, cy, belly);
            // 背鳍 + 胸鳍
            p.line(cx - 1, cy - 4, cx + 3, cy - 9, skin);
            p.line(cx + 3, cy - 9, cx + 5, cy - 4, Color{120, 130, 142, 255});
            p.line(cx + 2, cy + 4, cx + 6, cy + 8, Color{120, 130, 142, 255});
            // 尾鳍（上下叉）
            p.line(cx - 12, cy, cx - 17, cy - 5, skin);
            p.line(cx - 12, cy, cx - 17, cy + 5, skin);
            p.set(cx + 9, cy - 2, Pal::GUN);                              // 眼
            // 声呐装置（盟军训练标志：阵营色项圈）
            p.hline(cx + 5, cx + 7, cy - 3, Pal::REMAP);
            break;
        }
        case UnitType::Squid: {
            // 巨型乌贼：暗紫纺锤身 + 环绕触手（朝东）
            Color body{88, 66, 92, 255}, dark{58, 42, 62, 255};
            // 触手（尾部西侧放射状）
            for (int i = -3; i <= 3; i++) {
                if (i == 0) continue;
                int tx = cx - 10 - abs(i) * 2, ty = cy + i * 4;
                p.line(cx - 6, cy + i, tx, ty, dark);
                p.set(tx - 1, ty, body);
            }
            // 两条长捕腕（前伸）
            p.line(cx + 8, cy - 1, cx + 17, cy - 4, dark);
            p.line(cx + 8, cy + 1, cx + 17, cy + 4, dark);
            p.set(cx + 18, cy - 4, body); p.set(cx + 18, cy + 4, body);
            // 纺锤胴体
            p.fillEllipse(cx, cy, 10, 5, body);
            p.ellipse(cx, cy, 10, 5, dark);
            p.fillEllipse(cx - 2, cy - 1, 6, 3, Color{104, 80, 110, 255}); // 受光
            // 鳍 + 眼
            p.line(cx - 7, cy - 4, cx - 10, cy - 8, dark);
            p.line(cx - 7, cy + 4, cx - 10, cy + 8, dark);
            p.fillEllipse(cx + 6, cy - 2, 2, 2, Color{230, 230, 240, 255});
            p.set(cx + 6, cy - 2, Pal::GUN);
            p.hline(cx - 4, cx + 2, cy + 4, Pal::REMAP);                   // 阵营色带
            break;
        }
        case UnitType::MCV: {
            drawTracks(p, cx, cy, 40, 26);
            p.fillRect(cx - 18, cy - 9, 32, 18, Pal::STEEL);
            p.rect(cx - 18, cy - 9, 32, 18, Pal::STEEL_DARK);
            p.fillRect(cx - 16, cy - 7, 10, 14, Pal::STEEL_DARK); // 货舱
            p.fillRect(cx + 8, cy - 6, 6, 12, Pal::GLASS);        // 驾驶室
            p.hline(cx - 18, cx + 12, cy - 9, Pal::REMAP);
            p.hline(cx - 18, cx + 12, cy + 9, Pal::REMAP_DARK);
            break;
        }
        case UnitType::Destroyer: {
            // 驱逐舰：灰蓝舰体 + 前主炮 + 雷达桅杆
            shipHull(34, 13, Color{96, 110, 134, 255});
            p.fillRect(cx + 6, cy - 2, 12, 3, Pal::GUN);                 // 主炮
            p.fillEllipse(cx + 3, cy, 4, 3, Pal::STEEL_DARK);            // 炮塔座
            p.fillRect(cx - 8, cy - 3, 8, 6, Pal::STEEL);                // 舰桥
            p.line(cx - 5, cy - 3, cx - 5, cy - 9, Pal::GUN);            // 桅杆
            p.fillEllipse(cx - 5, cy - 9, 3, 2, Color{170, 200, 235, 255}); // 雷达
            p.hline(cx - 14, cx - 9, cy, Pal::REMAP);
            break;
        }
        case UnitType::Typhoon: {
            // 台风潜艇：深色低舷艇体 + 指挥塔围壳
            shipHull(30, 10, Color{56, 58, 72, 255});
            p.fillRect(cx - 4, cy - 3, 9, 4, Color{74, 76, 92, 255});    // 围壳
            p.line(cx, cy - 3, cx, cy - 9, Pal::STEEL_DARK);             // 潜望镜桅
            p.set(cx, cy - 10, Color{200, 220, 240, 255});
            p.hline(cx - 12, cx + 8, cy + 3, Color{40, 42, 52, 255});    // 压载水线
            break;
        }
        case UnitType::Aegis: {
            // 中华神盾舰：大舰体 + 相控阵盾面 + 垂发井
            shipHull(38, 15, Color{104, 116, 128, 255});
            p.fillRect(cx - 12, cy - 4, 10, 8, Pal::STEEL);              // 舰桥
            p.fillRect(cx - 11, cy - 3, 3, 3, Color{120, 200, 255, 255}); // 盾面 A
            p.fillRect(cx - 6, cy - 3, 3, 3, Color{120, 200, 255, 255});  // 盾面 B
            for (int i = 0; i < 3; i++)                                   // 垂发井
                p.fillRect(cx + 3 + i * 4, cy - 2, 3, 4, Color{60, 66, 76, 255});
            p.line(cx - 8, cy - 4, cx - 8, cy - 11, Pal::GUN);            // 桅杆
            p.hline(cx - 11, cx - 5, cy - 11, Pal::GUN);
            p.hline(cx - 16, cx - 13, cy, Pal::REMAP);
            break;
        }
        case UnitType::AmphTransport: {
            // 两栖运输船：方正登陆艇 + 艏门跳板
            shipHull(30, 14, Color{110, 112, 100, 255});
            p.fillRect(cx + 10, cy - 5, 6, 10, Pal::STEEL_DARK);          // 艏门
            p.line(cx + 10, cy - 5, cx + 16, cy + 5, Pal::GUN);
            p.fillRect(cx - 14, cy - 4, 18, 8, Color{88, 90, 80, 255});   // 载员舱
            p.hline(cx - 14, cx + 4, cy - 4, Pal::REMAP);
            p.fillRect(cx - 8, cy - 7, 6, 3, Pal::GLASS);                 // 驾驶窗
            break;
        }
        case UnitType::TerrorDrone: {
            // 恐怖机器人：圆身 + 四足 + 红眼（机械蜘蛛）
            Color metal{92, 96, 104, 255};
            for (int s = -1; s <= 1; s += 2) { // 四条腿（前后各二）
                p.line(cx - 3, cy + s * 3, cx - 9, cy + s * 8, metal);
                p.line(cx + 3, cy + s * 3, cx + 9, cy + s * 8, metal);
                p.set(cx - 9, cy + s * 8 + (s > 0 ? 1 : -1), Pal::GUN);
                p.set(cx + 9, cy + s * 8 + (s > 0 ? 1 : -1), Pal::GUN);
            }
            p.fillEllipse(cx, cy, 7, 5, metal);          // 圆身
            p.ellipse(cx, cy, 7, 5, Pal::GUN);
            p.fillEllipse(cx + 5, cy, 3, 3, Color{70, 74, 82, 255}); // 头部
            p.set(cx + 6, cy - 1, Color{255, 60, 50, 255});          // 红眼
            p.set(cx + 6, cy + 1, Color{255, 60, 50, 255});
            p.hline(cx - 4, cx + 2, cy - 4, Pal::REMAP);   // 阵营条
            break;
        }
        case UnitType::SeaScorpion: {
            // 海蝎：轻型快艇 + 四联高射炮
            shipHull(26, 10, Color{100, 104, 96, 255});
            p.fillRect(cx - 6, cy - 3, 8, 6, Pal::STEEL);             // 舰桥
            p.fillEllipse(cx + 5, cy, 4, 3, Pal::STEEL_DARK);         // 炮座
            for (int i = -1; i <= 1; i += 2) {                        // 四联炮管
                p.fillRect(cx + 7, cy + i * 2 - 1, 7, 1, Pal::GUN);
                p.fillRect(cx + 7, cy + i * 1 - 0, 6, 1, Pal::GUN);
            }
            p.hline(cx - 10, cx - 6, cy, Pal::REMAP);
            break;
        }
        case UnitType::Dreadnought: {
            // 无畏级战舰：重型舰体 + 双联 V3 导弹发射架
            shipHull(44, 16, Color{88, 92, 104, 255});
            p.fillRect(cx - 14, cy - 4, 10, 8, Pal::STEEL);           // 舰桥
            p.line(cx - 10, cy - 4, cx - 10, cy - 11, Pal::GUN);      // 桅杆
            p.hline(cx - 13, cx - 7, cy - 11, Pal::GUN);
            for (int i = 0; i < 2; i++) {                             // 双导弹架（倾斜）
                int mx = cx + 2 + i * 9;
                p.line(mx, cy + 3, mx + 6, cy - 6, Color{150, 70, 56, 255});
                p.line(mx + 2, cy + 3, mx + 8, cy - 6, Color{170, 80, 60, 255});
                p.set(mx + 7, cy - 7, Color{220, 200, 180, 255});     // 弹头
            }
            p.fillRect(cx - 20, cy - 2, 5, 4, Pal::STEEL_DARK);       // 尾舱
            p.hline(cx - 18, cx - 12, cy, Pal::REMAP);
            break;
        }
        case UnitType::AircraftCarrier: {
            // 航空母舰：全通平直甲板 + 右舷舰岛 + 舰载机
            shipHull(46, 18, Color{96, 104, 116, 255});
            p.fillRect(cx - 21, cy - 6, 40, 12, Color{72, 78, 88, 255}); // 甲板
            p.hline(cx - 21, cx + 19, cy - 6, Color{120, 126, 136, 255}); // 甲板边线
            p.hline(cx - 18, cx + 14, cy, Color{200, 200, 120, 255});     // 甲板中线
            p.fillRect(cx - 6, cy - 11, 8, 6, Pal::STEEL);             // 舰岛
            p.line(cx - 3, cy - 11, cx - 3, cy - 16, Pal::GUN);        // 桅杆
            p.set(cx - 3, cy - 17, Color{170, 200, 235, 255});
            for (int i = 0; i < 3; i++) {                              // 甲板舰载机
                p.fillRect(cx + 4 + i * 5, cy + 2, 3, 2, Color{140, 146, 156, 255});
            }
            p.hline(cx - 20, cx - 14, cy + 5, Pal::REMAP);
            break;
        }
        default: hull(8, 6, Pal::STEEL); break;
    }
    if (dir) p = p.rotate8(dir);
    return p;
}

PixBuf SpriteBank::baseUnitTurret(UnitType t, int dir) {
    int cs = 44;
    if (t == UnitType::Apocalypse) cs = 56;
    PixBuf p(cs, cs);
    int cx = cs / 2, cy = cs / 2;
    switch (t) {
        case UnitType::Grizzly: case UnitType::MirageTank:
            p.fillEllipse(cx, cy, 6, 5, Pal::STEEL);
            p.ellipse(cx, cy, 6, 5, Pal::GUN);
            p.fillRect(cx + 4, cy - 1, 12, 2, Pal::GUN);
            p.hline(cx - 3, cx + 3, cy - 3, Pal::REMAP);
            break;
        case UnitType::Rhino:
            p.fillEllipse(cx, cy, 7, 5, Pal::STEEL);
            p.ellipse(cx, cy, 7, 5, Pal::GUN);
            p.fillRect(cx + 5, cy - 1, 14, 3, Pal::GUN);
            p.hline(cx - 4, cx + 4, cy - 3, Pal::REMAP);
            break;
        case UnitType::Type99:
            p.fillEllipse(cx, cy, 8, 6, Color{96, 104, 80, 255});
            p.ellipse(cx, cy, 8, 6, Pal::GUN);
            p.fillRect(cx + 6, cy - 1, 16, 3, Pal::GUN);
            p.fillRect(cx + 18, cy - 2, 3, 5, Pal::GUN); // 炮口制退器
            p.hline(cx - 5, cx + 5, cy - 4, Pal::REMAP);
            p.set(cx - 2, cy + 2, Pal::GLASS);
            break;
        case UnitType::Apocalypse:
            p.fillEllipse(cx, cy, 9, 7, Color{80, 66, 54, 255});
            p.ellipse(cx, cy, 9, 7, Pal::GUN);
            p.fillRect(cx + 6, cy - 4, 16, 3, Pal::GUN);
            p.fillRect(cx + 6, cy + 1, 16, 3, Pal::GUN); // 双管
            p.hline(cx - 6, cx + 4, cy, Pal::REMAP);
            break;
        case UnitType::PrismTank: {
            p.fillEllipse(cx, cy, 5, 4, Pal::STEEL_DARK);
            // 光棱晶体
            p.line(cx, cy, cx + 7, cy - 5, Color{200, 240, 255, 255});
            p.line(cx + 7, cy - 5, cx + 12, cy, Color{200, 240, 255, 255});
            p.line(cx + 12, cy, cx + 7, cy + 4, Color{150, 210, 240, 255});
            p.line(cx + 7, cy - 5, cx + 7, cy + 4, Color{230, 250, 255, 255});
            break;
        }
        case UnitType::TeslaTank:
            p.fillEllipse(cx, cy, 6, 5, Pal::STEEL);
            p.fillEllipse(cx + 8, cy, 4, 4, Color{120, 180, 255, 255}); // 磁暴球
            p.line(cx + 2, cy, cx + 6, cy, Pal::GUN);
            p.set(cx + 7, cy - 1, Color{220, 240, 255, 255});
            break;
        case UnitType::IFV:
            p.fillEllipse(cx, cy, 4, 3, Pal::STEEL_DARK);
            p.fillRect(cx + 3, cy - 1, 8, 2, Pal::GUN);
            break;
        case UnitType::FlakTrack:
            p.fillEllipse(cx, cy, 5, 4, Pal::STEEL);
            for (int i = -1; i <= 1; i += 2) {
                p.fillRect(cx + 3, cy + i * 2 - 1, 9, 2, Pal::GUN);
            }
            break;
        case UnitType::RobotTank:
            // 遥控坦克：小型单人炮塔 + 短管
            p.fillEllipse(cx, cy, 4, 3, Color{110, 118, 132, 255});
            p.ellipse(cx, cy, 4, 3, Pal::GUN);
            p.fillRect(cx + 3, cy - 1, 9, 2, Pal::GUN);
            p.hline(cx - 2, cx + 2, cy - 2, Pal::REMAP);
            break;
        default: break;
    }
    if (dir) p = p.rotate8(dir);
    return p;
}

bool SpriteBank::hasTurret(UnitType t) const {
    t = spriteAliasUnit(t);
    switch (t) {
        case UnitType::Grizzly: case UnitType::Rhino: case UnitType::Type99:
        case UnitType::Apocalypse: case UnitType::PrismTank: case UnitType::TeslaTank:
        case UnitType::IFV: case UnitType::FlakTrack: case UnitType::MirageTank:
        case UnitType::RobotTank:
        case UnitType::LasherTank: case UnitType::GatlingTank:
        case UnitType::Magnetron: case UnitType::MasterMind: case UnitType::Boomer:
            return true;
        default: return false;
    }
}

// ---------------- 建筑 ----------------
PixBuf SpriteBank::baseBuilding(BldType t, bool constructing) {
    const BldDef& d = bldDef(t);
    int fw = d.w, fh = d.h;
    int halfW = (fw + fh) * TILE_W / 4;
    int halfH = (fw + fh) * TILE_H / 4;
    int wallH = 20 + fw * 4;
    if (t == BldType::ConYard) wallH = 34;
    if (t == BldType::WarFactory) wallH = 32;
    if (t == BldType::NuclearReactor) wallH = 46;
    int decorH = 46;
    int cw = halfW * 2 + 24;
    int ch = halfH * 2 + wallH + decorH + 8;
    PixBuf p(cw, ch);
    int cx = cw / 2;
    int baseCY = ch - 4 - halfH; // 基座菱形中心 y
    Rng rng((uint64_t)t * 31337);

    // 基座混凝土
    p.diamond(cx, baseCY, halfW + 3, halfH + 2, Color{96, 96, 100, 255});
    p.diamond(cx, baseCY, halfW + 1, halfH, Color{130, 130, 134, 255});
    // 基座边缘线
    p.line(cx - halfW - 3, baseCY, cx, baseCY + halfH + 2, Color{70, 70, 74, 255});
    p.line(cx + halfW + 3, baseCY, cx, baseCY + halfH + 2, Color{70, 70, 74, 255});

    if (constructing) {
        // 脚手架
        Color frame{150, 130, 90, 255};
        for (int i = 0; i < 4; i++) {
            int fx = cx - halfW + 6 + i * (halfW * 2 - 12) / 3;
            p.line(fx, baseCY, fx, baseCY - wallH, frame);
            p.line(cx - halfW + 6, baseCY - i * wallH / 4, cx + halfW - 6, baseCY - i * wallH / 4 - 2, frame);
        }
        p.line(cx - halfW + 4, baseCY - wallH, cx + halfW - 4, baseCY - 2, frame);
        p.line(cx + halfW - 4, baseCY - wallH, cx - halfW + 4, baseCY - 2, frame);
        // 塔吊
        p.line(cx, baseCY - wallH - 4, cx, baseCY - wallH - 26, Pal::STEEL_DARK);
        p.line(cx - 14, baseCY - wallH - 26, cx + 10, baseCY - wallH - 26, Pal::STEEL_DARK);
        p.line(cx + 8, baseCY - wallH - 26, cx + 8, baseCY - wallH - 16, Pal::STEEL_DARK);
        return p;
    }

    int topCY = baseCY - wallH;
    Color trim = Pal::REMAP;

    // 围墙：低矮混凝土墙段（1x1 专用，非全尺寸建筑盒）
    if (t == BldType::Wall && !constructing) {
        int wy = baseCY - 8;
        p.diamond(cx, wy, halfW - 2, halfH - 1, Color{150, 148, 140, 255}); // 墙顶
        // 左右墙面
        p.line(cx - halfW + 2, wy, cx, wy + halfH - 1, Color{110, 108, 100, 255});
        p.line(cx + halfW - 2, wy, cx, wy + halfH - 1, Color{92, 90, 84, 255});
        for (int i = 0; i < 3; i++) { // 墙面竖缝
            p.line(cx - halfW + 2 + i * 4, wy + i * 2, cx - halfW + 2 + i * 4, wy + i * 2 + 6, Color{100, 98, 92, 255});
            p.line(cx + halfW - 2 - i * 4, wy + i * 2, cx + halfW - 2 - i * 4, wy + i * 2 + 6, Color{84, 82, 76, 255});
        }
        p.hline(cx - 4, cx + 4, wy - halfH / 2, trim); // 顶部阵营标记
        return p;
    }

    // 调色板：左墙/右墙/屋面/屋顶内凹/窗
    struct BldPal { Color wallL, wallR, roof, roofIn, win; };
    BldPal bp{Color{150, 142, 128, 255}, Color{108, 100, 90, 255},
              Color{80, 78, 74, 255}, Color{62, 60, 57, 255},
              Color{255, 214, 120, 255}};
    switch (t) {
        case BldType::PowerPlant:       bp.wallL={146,152,164,255}; bp.wallR={104,110,122,255}; bp.roof={70,76,90,255};  bp.roofIn={54,58,72,255};  break;
        case BldType::TeslaReactor:     bp.wallL={152,116,82,255};  bp.wallR={110,82,58,255};   bp.roof={78,58,42,255};  bp.roofIn={60,44,32,255};  break;
        case BldType::Barracks:         bp.wallL={158,144,104,255}; bp.wallR={116,104,74,255};  bp.roof={86,76,56,255};  bp.roofIn={66,58,42,255};  break;
        case BldType::WarFactory:       bp.wallL={124,128,136,255}; bp.wallR={88,92,100,255};   bp.roof={60,62,68,255};  bp.roofIn={46,48,54,255};  break;
        case BldType::OreRefinery:      bp.wallL={148,126,86,255};  bp.wallR={108,90,60,255};   bp.roof={74,62,44,255};  bp.roofIn={56,46,32,255};  break;
        case BldType::Radar:            bp.wallL={144,150,154,255}; bp.wallR={104,110,114,255}; bp.roof={70,74,80,255};  bp.roofIn={54,58,64,255};  break;
        case BldType::BattleLab:        bp.wallL={134,144,154,255}; bp.wallR={96,106,116,255};  bp.roof={66,74,84,255};  bp.roofIn={50,56,66,255};  break;
        case BldType::AirForceCmd:      bp.wallL={142,148,154,255}; bp.wallR={102,108,114,255}; bp.roof={64,68,74,255};  bp.roofIn={50,54,60,255};  break;
        case BldType::OrePurifier:      bp.wallL={152,140,102,255}; bp.wallR={112,102,72,255};  bp.roof={76,68,48,255};  bp.roofIn={58,52,36,255};  break;
        case BldType::IndustrialPlant:  bp.wallL={136,118,94,255};  bp.wallR={98,84,64,255};    bp.roof={70,60,48,255};  bp.roofIn={54,46,36,255};  break;
        case BldType::NuclearReactor:   bp.wallL={138,136,130,255}; bp.wallR={100,98,94,255};   bp.roof={72,70,66,255};  bp.roofIn={56,54,50,255};  break;
        case BldType::CloningVat:       bp.wallL={128,140,124,255}; bp.wallR={92,104,88,255};   bp.roof={64,72,60,255};  bp.roofIn={48,56,44,255};  break;
        case BldType::ServiceDepot:     bp.wallL={140,134,112,255}; bp.wallR={102,96,78,255};   bp.roof={72,68,56,255};  bp.roofIn={56,52,42,255};  break;
        case BldType::GapGenerator:     bp.wallL={134,140,152,255}; bp.wallR={96,102,114,255};  bp.roof={66,70,82,255};  bp.roofIn={50,54,66,255};  break;
        case BldType::SpySat:           bp.wallL={142,148,156,255}; bp.wallR={102,108,116,255}; bp.roof={68,72,80,255};  bp.roofIn={52,56,64,255};  break;
        case BldType::PsychicSensor:    bp.wallL={126,116,138,255}; bp.wallR={90,82,102,255};   bp.roof={62,56,74,255};  bp.roofIn={46,42,58,255};  break;
        case BldType::TechAirport:      bp.wallL={150,146,136,255}; bp.wallR={110,106,96,255};  bp.roof={74,72,66,255};  bp.roofIn={58,56,50,255};  break;
        case BldType::SecretLab:        bp.wallL={130,138,130,255}; bp.wallR={94,102,94,255};   bp.roof={64,70,62,255};  bp.roofIn={48,54,46,255};  break;
        case BldType::CivHouse:         bp.wallL={168,150,128,255}; bp.wallR={126,110,90,255};  bp.roof={120,70,54,255}; bp.roofIn={96,54,40,255};  break;
        default: break; // ConYard 与防御设施用默认灰调
    }

    // 主体盒：墙体 + 深色平顶 + 沿口 + 窗格带 + 阵营饰条
    auto mainBox = [&](int inset, int hh, bool withWin = true) {
        int ty = topCY - (hh - wallH); // 顶面中心 y
        int rw = halfW - inset, rh = halfH - inset / 2;
        p.isoBox(cx, ty, rw, rh, hh, bp.roof, bp.wallL, bp.wallR);
        // 屋顶内凹 + 上沿高光
        p.diamond(cx, ty, rw - 2, rh - 1, bp.roofIn);
        p.line(cx - rw + 1, ty, cx, ty - rh, Color{160, 160, 158, 255});
        p.line(cx, ty - rh, cx + rw - 1, ty, Color{146, 146, 144, 255});
        // 窗格（墙带满宽区）
        if (withWin)
            for (int y = ty + 4; y < ty + hh - 3; y += 5)
                for (int x = cx - rw + 5; x < cx + rw - 5; x += 6) {
                    if (abs(x - cx) < 2) continue; // 墙角缝
                    Color wc = ((x * 7 + y * 3) % 5 < 2) ? Color{64, 82, 104, 255} : bp.win;
                    p.set(x, y, wc); p.set(x + 1, y, wc);
                    p.set(x, y + 1, wc); p.set(x + 1, y + 1, wc);
                }
        // 阵营饰条（屋顶下沿一圈）
        p.hline(cx - rw + 2, cx - 2, ty + 2, trim);
        p.hline(cx + 2, cx + rw - 2, ty + 2, trim);
    };
    auto flag = [&](int fx, int fy) {
        p.line(fx, fy, fx, fy - 12, Pal::GUN);
        p.fillRect(fx + 1, fy - 12, 7, 4, trim);
    };

    switch (t) {
        case BldType::ConYard: {
            mainBox(2, wallH);
            // 中央塔楼
            p.isoBox(cx, topCY - 20, 11, 6, 20, Color{70, 70, 76, 255}, bp.wallL, bp.wallR);
            p.diamond(cx, topCY - 20, 9, 5, Color{54, 54, 60, 255});
            p.fillRect(cx - 7, topCY - 29, 14, 4, trim);
            // 吊臂
            p.line(cx + 6, topCY - 24, cx + 28, topCY - 36, Pal::STEEL_DARK);
            p.line(cx + 26, topCY - 36, cx + 26, topCY - 26, Pal::GUN);
            // 大门
            p.fillRect(cx - 6, baseCY - 12, 12, 11, Pal::STEEL_DARK);
            p.rect(cx - 6, baseCY - 12, 12, 11, trim);
            flag(cx - 14, topCY - 8);
            break;
        }
        case BldType::PowerPlant: {
            mainBox(2, wallH - 4);
            // 两个涡轮机房 + 烟囱
            p.fillEllipse(cx - 10, topCY - 2, 7, 5, Color{196, 200, 208, 255});
            p.fillEllipse(cx - 10, topCY - 4, 5, 3, Color{230, 234, 240, 255});
            p.fillEllipse(cx + 8, topCY - 2, 7, 5, Color{196, 200, 208, 255});
            p.fillEllipse(cx + 8, topCY - 4, 5, 3, Color{230, 234, 240, 255});
            p.fillRect(cx + halfW - 12, topCY - 18, 5, 20, Color{150, 96, 72, 255});
            p.hline(cx + halfW - 12, cx + halfW - 7, topCY - 18, Color{210, 140, 100, 255});
            break;
        }
        case BldType::TeslaReactor: {
            mainBox(2, wallH - 6);
            // 大圆顶 + 磁暴球
            p.fillEllipse(cx, topCY, 13, 9, Color{150, 122, 92, 255});
            p.fillEllipse(cx, topCY - 2, 10, 6, Color{170, 142, 108, 255});
            p.fillEllipse(cx, topCY - 6, 6, 5, Color{120, 180, 255, 255});
            p.set(cx - 1, topCY - 8, Color{230, 245, 255, 255});
            break;
        }
        case BldType::NuclearReactor: {
            mainBox(2, wallH - 30, false); // 低矮基座
            // 冷却塔
            p.fillEllipse(cx, baseCY - 8, 16, 9, Color{140, 138, 134, 255});
            p.fillRect(cx - 16, topCY + 4, 32, wallH - 12, Color{160, 158, 154, 255});
            p.fillEllipse(cx, topCY + 4, 16, 8, Color{176, 174, 168, 255});
            p.fillEllipse(cx, topCY + 4, 12, 6, Color{60, 60, 64, 255});
            // 塔身条纹
            p.hline(cx - 15, cx + 15, topCY + 14, Color{130, 70, 60, 255});
            // 辐射标志
            p.fillEllipse(cx + halfW - 10, baseCY - 8, 5, 3, Color{240, 220, 60, 255});
            p.set(cx + halfW - 10, baseCY - 8, Pal::GUN);
            break;
        }
        case BldType::Barracks: {
            mainBox(2, wallH - 6);
            // 大门 + 旗帜
            p.fillRect(cx - 5, baseCY - 12, 10, 11, Pal::GUN);
            p.rect(cx - 5, baseCY - 12, 10, 11, trim);
            p.hline(cx - 5, cx + 5, baseCY - 7, trim);
            flag(cx + 10, topCY + 2);
            break;
        }
        case BldType::WarFactory: {
            mainBox(2, wallH, false);
            // 大库门（含警示条纹）
            p.fillRect(cx - 14, baseCY - 18, 28, 17, Pal::STEEL_DARK);
            for (int i = 0; i < 4; i++) p.hline(cx - 14, cx + 14, baseCY - 15 + i * 4, Pal::STEEL);
            for (int i = 0; i < 7; i++) p.set(cx - 13 + i * 4, baseCY - 2, Color{240, 200, 60, 255});
            // 吊轨
            p.line(cx - halfW + 2, topCY - 10, cx + halfW - 2, topCY - 10, Pal::STEEL_DARK);
            p.line(cx + halfW - 10, topCY - 10, cx + halfW - 10, topCY, Pal::STEEL_DARK);
            p.hline(cx - 8, cx + 8, topCY - 4, trim);
            break;
        }
        case BldType::OreRefinery: {
            mainBox(2, wallH - 8);
            // 矿仓斗
            p.fillRect(cx - halfW + 4, topCY - 8, 16, 14, Pal::STEEL_DARK);
            p.line(cx - halfW + 4, topCY - 8, cx - halfW + 12, topCY - 18, Pal::STEEL_DARK);
            p.line(cx - halfW + 20, topCY - 8, cx - halfW + 12, topCY - 18, Pal::STEEL_DARK);
            p.hline(cx - halfW + 6, cx - halfW + 18, topCY - 12, Pal::ORE_GOLD);
            // 储料罐
            p.fillEllipse(cx + 14, topCY + 2, 9, 7, Color{170, 140, 90, 255});
            p.fillEllipse(cx + 14, topCY - 2, 9, 5, Color{200, 170, 110, 255});
            p.hline(cx - 2, cx + 6, baseCY - 6, Pal::ORE_GOLD); // 散落矿
            break;
        }
        case BldType::Radar: {
            mainBox(2, wallH - 8);
            // 雷达塔 + 大碟
            p.fillRect(cx - 2, topCY - 16, 4, 16, Pal::STEEL_DARK);
            p.fillEllipse(cx + 5, topCY - 22, 11, 6, Color{200, 206, 214, 255});
            p.fillEllipse(cx + 5, topCY - 22, 6, 3, Color{130, 136, 146, 255});
            p.set(cx + 5, topCY - 26, Color{240, 248, 255, 255});
            p.set(cx - 2, topCY - 16, trim);
            break;
        }
        case BldType::BattleLab: {
            mainBox(2, wallH - 4);
            // 发光穹顶 + 天线
            p.fillEllipse(cx, topCY - 2, 11, 8, Color{140, 200, 230, 255});
            p.fillEllipse(cx - 3, topCY - 5, 6, 4, Color{200, 240, 255, 255});
            p.line(cx + 14, topCY - 2, cx + 14, topCY - 18, Pal::GUN);
            p.set(cx + 14, topCY - 19, Color{255, 80, 80, 255});
            break;
        }
        case BldType::AirForceCmd: {
            mainBox(2, wallH - 10);
            // 跑道条纹
            for (int i = 0; i < 3; i++) p.hline(cx - halfW + 4 + i * 9, cx - halfW + 8 + i * 9, baseCY - 2 - i, Color{230, 230, 230, 255});
            // 塔台
            p.fillRect(cx + halfW - 14, topCY - 12, 7, 14, Pal::STEEL);
            p.fillRect(cx + halfW - 15, topCY - 15, 9, 4, Pal::GLASS);
            p.set(cx + halfW - 11, topCY - 17, Color{255, 90, 90, 255});
            flag(cx - 10, topCY + 2);
            break;
        }
        case BldType::Pillbox: {
            // 低矮碉堡
            p.isoBox(cx, baseCY - 12, halfW - 3, halfH - 2, 12, Color{86, 82, 70, 255}, Color{110, 106, 92, 255}, Color{80, 76, 66, 255});
            p.diamond(cx, baseCY - 12, halfW - 5, halfH - 3, Color{70, 66, 56, 255});
            p.hline(cx - halfW + 5, cx + halfW - 5, baseCY - 14, Pal::GUN); // 射击孔
            p.hline(cx - 3, cx + 3, baseCY - 9, trim);
            break;
        }
        case BldType::SentryGun: {
            p.fillEllipse(cx, baseCY - 4, halfW - 4, halfH - 2, Color{110, 110, 116, 255});
            p.fillEllipse(cx, baseCY - 12, 8, 6, Pal::STEEL);
            p.fillEllipse(cx, baseCY - 14, 5, 3, Pal::STEEL_LITE);
            p.fillRect(cx + 5, baseCY - 14, 13, 3, Pal::GUN);
            p.hline(cx - 5, cx + 3, baseCY - 16, trim);
            break;
        }
        case BldType::PrismTower: {
            p.fillEllipse(cx, baseCY - 2, halfW - 3, halfH - 1, Color{110, 110, 116, 255});
            p.fillRect(cx - 3, baseCY - 32, 6, 30, Pal::STEEL);
            p.hline(cx - 4, cx + 4, baseCY - 18, trim);
            p.hline(cx - 4, cx + 4, baseCY - 10, Pal::STEEL_DARK);
            // 水晶
            int py = baseCY - 42;
            p.line(cx, py, cx + 6, py + 5, Color{210, 245, 255, 255});
            p.line(cx + 6, py + 5, cx, py + 10, Color{160, 220, 250, 255});
            p.line(cx, py + 10, cx - 6, py + 5, Color{160, 220, 250, 255});
            p.line(cx - 6, py + 5, cx, py, Color{210, 245, 255, 255});
            p.line(cx - 6, py + 5, cx + 6, py + 5, Color{240, 255, 255, 255});
            p.set(cx, py + 4, Color{255, 255, 255, 255});
            break;
        }
        case BldType::TeslaCoil: {
            p.fillEllipse(cx, baseCY - 2, halfW - 3, halfH - 1, Color{110, 110, 116, 255});
            // 线圈塔
            for (int i = 0; i < 4; i++) p.fillRect(cx - 5 + i / 2, baseCY - 8 - i * 6, 10 - i, 5, Color{(uint8_t)(140 - i * 10), (uint8_t)(104 + i * 6), 88, 255});
            p.fillRect(cx - 2, baseCY - 32, 4, 4, Pal::STEEL_DARK);
            p.fillEllipse(cx, baseCY - 38, 6, 6, Color{120, 180, 255, 255});
            p.set(cx - 1, baseCY - 40, Color{235, 248, 255, 255});
            p.hline(cx - 6, cx + 6, baseCY - 6, trim);
            break;
        }
        case BldType::FlakCannon: {
            p.fillEllipse(cx, baseCY - 2, halfW - 3, halfH - 1, Color{110, 110, 116, 255});
            p.fillRect(cx - 6, baseCY - 14, 12, 10, Pal::STEEL);
            p.rect(cx - 6, baseCY - 14, 12, 10, Pal::STEEL_DARK);
            for (int i = -4; i <= 2; i += 2) p.line(cx + i, baseCY - 14, cx + i + 7, baseCY - 30, Pal::GUN);
            p.hline(cx - 6, cx + 6, baseCY - 12, trim);
            break;
        }
        case BldType::GrandCannon: {
            mainBox(2, wallH - 12);
            // 巨炮炮管（朝南）
            p.fillEllipse(cx, topCY + 2, 11, 8, Pal::STEEL_DARK);
            p.fillEllipse(cx, topCY + 1, 7, 5, Pal::STEEL);
            p.line(cx - 2, topCY + 2, cx + 6, topCY + 28, Pal::GUN);
            p.line(cx + 2, topCY + 2, cx + 10, topCY + 28, Pal::GUN);
            p.hline(cx + 5, cx + 11, topCY + 28, Pal::GUN);
            break;
        }
        case BldType::OrePurifier: {
            mainBox(2, wallH - 6);
            p.fillEllipse(cx - 10, topCY, 7, 5, Color{170, 140, 90, 255});
            p.fillEllipse(cx - 10, topCY - 3, 5, 3, Color{200, 170, 110, 255});
            p.fillEllipse(cx + 10, topCY, 7, 5, Color{170, 140, 90, 255});
            p.fillEllipse(cx + 10, topCY - 3, 5, 3, Color{200, 170, 110, 255});
            p.fillRect(cx - 2, topCY - 12, 5, 10, Color{80, 220, 120, 255});
            p.set(cx, topCY - 13, Color{180, 255, 200, 255});
            break;
        }
        case BldType::IndustrialPlant: {
            mainBox(2, wallH - 4);
            p.fillRect(cx - halfW + 6, topCY - 18, 6, 20, Color{130, 96, 76, 255});
            p.hline(cx - halfW + 6, cx - halfW + 11, topCY - 18, Color{180, 130, 100, 255});
            p.fillRect(cx - halfW + 16, topCY - 13, 5, 15, Color{130, 96, 76, 255});
            p.hline(cx - halfW + 16, cx - halfW + 20, topCY - 13, Color{180, 130, 100, 255});
            break;
        }
        case BldType::NukeSilo: {
            // 低矮环形井壁 + 井盖滑开露出导弹尖
            p.fillEllipse(cx, baseCY - 4, halfW - 2, halfH, Color{120, 118, 112, 255});
            p.fillEllipse(cx, baseCY - 6, halfW - 5, halfH - 2, Color{88, 86, 82, 255});
            // 井盖（左右两片滑开）
            p.fillEllipse(cx - 7, baseCY - 8, 8, 5, Color{150, 148, 142, 255});
            p.fillEllipse(cx + 8, baseCY - 8, 8, 5, Color{150, 148, 142, 255});
            p.hline(cx - 14, cx - 2, baseCY - 10, Pal::GUN);
            p.hline(cx + 3, cx + 15, baseCY - 10, Pal::GUN);
            // 导弹尖（白色弹头 + 红尖）
            p.line(cx, baseCY - 12, cx, baseCY - 26, Color{225, 222, 215, 255});
            p.line(cx - 1, baseCY - 12, cx - 1, baseCY - 24, Color{200, 198, 190, 255});
            p.line(cx + 1, baseCY - 12, cx + 1, baseCY - 24, Color{200, 198, 190, 255});
            p.line(cx, baseCY - 26, cx, baseCY - 30, Color{220, 60, 50, 255});
            // 警示环
            p.hline(cx - halfW + 4, cx - halfW + 10, baseCY - 2, Color{240, 200, 60, 255});
            p.hline(cx + halfW - 10, cx + halfW - 4, baseCY - 2, Color{240, 200, 60, 255});
            // 辐射标志
            p.set(cx - halfW + 6, baseCY - 6, Color{240, 220, 60, 255});
            p.set(cx - halfW + 7, baseCY - 6, Color{240, 220, 60, 255});
            p.set(cx - halfW + 6, baseCY - 5, Pal::GUN);
            // 警戒灯
            p.set(cx + halfW - 6, baseCY - 8, Color{255, 70, 60, 255});
            p.hline(cx - 3, cx + 3, baseCY + halfH - 3, trim);
            break;
        }
        case BldType::WeatherDevice: {
            mainBox(2, wallH - 10);
            // 中央大球体（气象雷达球）
            p.fillEllipse(cx, topCY - 8, 12, 11, Color{210, 216, 224, 255});
            p.fillEllipse(cx - 3, topCY - 11, 6, 5, Color{240, 246, 252, 255});
            p.ellipse(cx, topCY - 8, 12, 11, Color{120, 126, 136, 255});
            // 球体经纬线
            p.ellipse(cx, topCY - 8, 7, 11, Color{150, 156, 166, 255});
            p.hline(cx - 11, cx + 11, topCY - 8, Color{150, 156, 166, 255});
            // 四根放电天线
            for (int s = -1; s <= 1; s += 2) {
                p.line(cx + s * (halfW - 5), topCY + 4, cx + s * (halfW - 5), topCY - 12, Pal::STEEL_DARK);
                p.set(cx + s * (halfW - 5), topCY - 14, Color{140, 200, 255, 255});
                p.set(cx + s * (halfW - 5), topCY - 13, Color{220, 240, 255, 255});
            }
            // 基座蓝色能量环
            p.hline(cx - halfW + 4, cx + halfW - 4, topCY + 6, Color{80, 150, 220, 255});
            p.hline(cx - halfW + 6, cx + halfW - 6, topCY + 8, trim);
            break;
        }
        case BldType::IronCurtain: {
            mainBox(2, wallH - 8);
            // 半球穹顶（暗红能量感）
            p.fillEllipse(cx, topCY - 2, 13, 9, Color{120, 70, 66, 255});
            p.fillEllipse(cx, topCY - 4, 10, 7, Color{150, 84, 78, 255});
            p.fillEllipse(cx - 3, topCY - 6, 5, 3, Color{200, 110, 100, 255});
            // 顶部发射器
            p.fillRect(cx - 2, topCY - 22, 4, 12, Pal::STEEL_DARK);
            p.fillEllipse(cx, topCY - 24, 4, 4, Color{220, 90, 80, 255});
            p.set(cx - 1, topCY - 25, Color{255, 160, 140, 255});
            // 两侧电容柱
            for (int s = -1; s <= 1; s += 2) {
                p.fillRect(cx + s * (halfW - 7) - 2, topCY - 10, 4, 12, Pal::STEEL);
                p.hline(cx + s * (halfW - 7) - 2, cx + s * (halfW - 7) + 2, topCY - 8, trim);
            }
            p.hline(cx - halfW + 4, cx + halfW - 4, topCY + 4, Color{200, 90, 80, 255});
            break;
        }
        case BldType::NavalYard: {
            // 海军船厂：水上平台 + 船坞滑道 + 门式吊机
            mainBox(3, wallH - 14, false);
            // 船坞凹槽（水面色，示意干船坞入口）
            p.diamond(cx + halfW / 3, baseCY + 2, halfW / 2, halfH / 2, Color{34, 84, 140, 255});
            p.line(cx + halfW / 3 - halfW / 2, baseCY + 2, cx + halfW / 3, baseCY + 2 + halfH / 2, Color{20, 60, 110, 255});
            // 滑道轨
            p.line(cx - halfW + 4, baseCY + halfH - 4, cx + halfW / 3, baseCY + 2, Pal::STEEL_DARK);
            p.line(cx - halfW + 6, baseCY + halfH - 2, cx + halfW / 3 + 2, baseCY + 4, Pal::STEEL_DARK);
            // 门式吊机
            p.line(cx - halfW + 6, baseCY - 2, cx - halfW + 6, baseCY - 24, Pal::STEEL_DARK);
            p.line(cx + 2, baseCY - 6, cx + 2, baseCY - 26, Pal::STEEL_DARK);
            p.line(cx - halfW + 4, baseCY - 24, cx + 4, baseCY - 26, Color{200, 170, 60, 255});
            p.line(cx - 6, baseCY - 25, cx - 6, baseCY - 16, Pal::GUN); // 吊钩
            // 在建舰体剪影
            p.fillEllipse(cx - 8, baseCY + 1, 10, 4, Pal::STEEL);
            p.hline(cx - 16, cx + 1, baseCY + 4, Pal::REMAP);
            // 雷达塔
            p.fillRect(cx - halfW + 2, topCY - 10, 5, 12, Pal::STEEL);
            p.set(cx - halfW + 4, topCY - 12, Color{255, 90, 90, 255});
            flag(cx + halfW - 8, topCY + 2);
            break;
        }
        case BldType::OilDerrick: {
            // 科技油井：抽油机（磕头机）+ 储油罐
            mainBox(4, wallH - 18, false);
            // 抽油机支架
            p.line(cx - 6, baseCY - 2, cx - 2, baseCY - 20, Pal::STEEL_DARK);
            p.line(cx + 2, baseCY - 2, cx - 2, baseCY - 20, Pal::STEEL_DARK);
            // 游梁（带驴头）
            p.line(cx - 12, baseCY - 22, cx + 8, baseCY - 18, Color{150, 120, 60, 255});
            p.line(cx - 12, baseCY - 21, cx + 8, baseCY - 17, Color{170, 140, 70, 255});
            p.fillRect(cx - 14, baseCY - 24, 4, 6, Color{150, 120, 60, 255}); // 驴头
            p.line(cx - 12, baseCY - 18, cx - 12, baseCY - 4, Pal::GUN);     // 抽油杆
            // 储油罐
            p.fillEllipse(cx + halfW - 8, baseCY - 4, 6, 5, Color{120, 100, 70, 255});
            p.fillEllipse(cx + halfW - 8, baseCY - 6, 6, 3, Color{150, 128, 90, 255});
            // 油桶
            p.fillRect(cx - halfW + 3, baseCY - 6, 3, 5, Color{90, 70, 50, 255});
            p.fillRect(cx - halfW + 7, baseCY - 5, 3, 4, Color{110, 88, 60, 255});
            break;
        }
        case BldType::Hospital: {
            // 医院：白楼 + 红十字
            mainBox(2, wallH - 8);
            // 屋顶红十字
            p.fillRect(cx - 2, topCY - 12, 4, 10, Color{220, 60, 50, 255});
            p.fillRect(cx - 5, topCY - 9, 10, 4, Color{220, 60, 50, 255});
            // 墙面白化
            p.fillRect(cx - halfW + 4, topCY + 4, halfW * 2 - 8, 3, Color{225, 228, 232, 255});
            // 门口急救灯
            p.set(cx - 1, baseCY - 14, Color{255, 90, 90, 255});
            break;
        }
        case BldType::MachineShop: {
            // 机械商店：车库 + 扳手标志 + 屋顶设备
            mainBox(2, wallH - 10, false);
            // 车库门
            p.fillRect(cx - 10, baseCY - 13, 20, 12, Pal::STEEL_DARK);
            for (int i = 0; i < 3; i++) p.hline(cx - 10, cx + 10, baseCY - 10 + i * 4, Pal::STEEL);
            // 屋顶扳手标志（斜十字简化）
            p.line(cx - 4, topCY - 10, cx + 4, topCY - 4, Color{230, 200, 80, 255});
            p.line(cx - 4, topCY - 4, cx + 4, topCY - 10, Color{230, 200, 80, 255});
            p.fillEllipse(cx, topCY - 7, 3, 3, Color{230, 200, 80, 255});
            // 屋角排气扇
            p.fillRect(cx + halfW - 9, topCY - 4, 5, 4, Pal::STEEL_DARK);
            break;
        }
        case BldType::PatriotMissile: {
            // 爱国者飞弹：低矮基座 + 四联导弹发射箱（仰角）
            mainBox(4, wallH - 12, false);
            int py = baseCY - wallH + 8;
            p.fillEllipse(cx, py - 2, 7, 4, Pal::STEEL_DARK);           // 旋转座
            for (int i = 0; i < 2; i++)                                 // 两排发射箱
                for (int j = 0; j < 2; j++) {
                    int bx = cx - 6 + j * 8, byy = py - 12 - i * 6;
                    p.fillRect(bx, byy, 6, 10, Color{120, 126, 110, 255});
                    p.rect(bx, byy, 6, 10, Pal::GUN);
                    p.set(bx + 2, byy + 1, Color{200, 90, 70, 255});    // 弹头
                }
            p.hline(cx - 8, cx + 8, py + 2, trim);
            break;
        }
        case BldType::ChronoSphere: {
            // 超时空传送仪：穹顶 + 三悬浮水晶 + 环形基座
            mainBox(2, wallH - 14, false);
            int dy = baseCY - wallH + 10;
            p.fillEllipse(cx, dy, halfW - 6, 8, Color{90, 110, 140, 255});  // 穹顶
            p.ellipse(cx, dy, halfW - 6, 8, Color{60, 76, 100, 255});
            for (int i = -1; i <= 1; i++) {                                 // 悬浮水晶
                int kx = cx + i * 10, ky = dy - 14 - (i == 0 ? 6 : 0);
                p.line(kx, ky - 4, kx + 3, ky, Color{170, 225, 255, 255});
                p.line(kx + 3, ky, kx, ky + 4, Color{120, 190, 240, 255});
                p.line(kx, ky + 4, kx - 3, ky, Color{170, 225, 255, 255});
                p.line(kx - 3, ky, kx, ky - 4, Color{210, 240, 255, 255});
            }
            p.ellipse(cx, dy + 4, halfW - 2, 5, trim);                      // 阵营色环
            break;
        }
        case BldType::CloningVat: {
            // 复制中心：三管绿色培养舱 + 连接管廊
            mainBox(2, wallH - 8);
            for (int i = -1; i <= 1; i++) {
                int vx = cx + i * 12;
                p.fillRect(vx - 4, topCY - 18, 8, 20, Color{70, 130, 80, 220});   // 玻璃管
                p.rect(vx - 4, topCY - 18, 8, 20, Pal::GUN);
                p.fillEllipse(vx, topCY - 18, 4, 2, Color{150, 200, 160, 255});   // 管盖
                p.fillEllipse(vx, topCY - 6, 2, 5, Color{40, 90, 50, 255});       // 舱内人形
                p.hline(vx - 3, vx + 3, topCY - 2, Color{120, 220, 130, 180});    // 培养液光
            }
            p.hline(cx - 16, cx + 16, topCY + 2, Pal::STEEL_DARK);                // 管廊
            p.hline(cx - 4, cx + 4, topCY + 4, trim);
            break;
        }
        case BldType::ServiceDepot: {
            // 维修厂：开放式维修台 + 黄黑警示沿 + 起重臂 + 地面工具
            mainBox(4, wallH - 16, false);
            // 维修台面（凹陷色）
            p.diamond(cx, baseCY - 2, halfW - 5, halfH - 2, Color{72, 72, 78, 255});
            // 警示沿（黄黑相间）
            for (int i = 0; i < halfW - 6; i += 4) {
                p.set(cx - halfW + 5 + i, baseCY - 1, Color{240, 200, 60, 255});
                p.set(cx - halfW + 7 + i, baseCY - 1, Color{30, 30, 30, 255});
            }
            // 起重臂（右侧立柱 + 横臂 + 吊钩）
            p.fillRect(cx + halfW - 9, topCY - 6, 4, wallH - 8, Pal::STEEL_DARK);
            p.line(cx + halfW - 8, topCY - 6, cx - 4, topCY - 10, Color{200, 170, 60, 255});
            p.line(cx - 4, topCY - 10, cx - 4, topCY - 2, Pal::GUN);
            p.set(cx - 4, topCY - 1, Color{150, 150, 158, 255});
            // 地面扳手/轮胎
            p.fillEllipse(cx - halfW + 7, baseCY + 2, 3, 2, Color{40, 40, 44, 255});
            p.hline(cx - 2, cx + 4, baseCY + 3, Pal::STEEL_LITE);
            break;
        }
        case BldType::GapGenerator: {
            // 裂缝产生器：肋板塔身 + 顶部三片扰波板 + 蓝色能量芯（1x1）
            p.fillEllipse(cx, baseCY - 2, halfW - 3, halfH - 1, Color{110, 110, 116, 255});
            for (int i = 0; i < 3; i++)                                             // 肋板塔身
                p.fillRect(cx - 6 + i, baseCY - 10 - i * 6, 12 - i * 2, 5, Color{(uint8_t)(120 + i * 12), (uint8_t)(126 + i * 12), (uint8_t)(140 + i * 12), 255});
            p.hline(cx - 6, cx + 6, baseCY - 12, trim);
            // 扰波板（三片斜立板）
            for (int s = -1; s <= 1; s++)
                p.line(cx + s * 5, baseCY - 26, cx + s * 8, baseCY - 38, Color{90, 96, 110, 255});
            // 能量芯
            p.fillEllipse(cx, baseCY - 30, 3, 4, Color{120, 190, 255, 255});
            p.set(cx - 1, baseCY - 32, Color{220, 240, 255, 255});
            break;
        }
        case BldType::SpySat: {
            // 间谍卫星：机房 + 巨型卫星碟（仰角，碟面弧光）
            mainBox(2, wallH - 10);
            // 碟架
            p.line(cx - 4, topCY - 4, cx - 10, topCY - 18, Pal::STEEL_DARK);
            p.line(cx + 4, topCY - 4, cx + 10, topCY - 18, Pal::STEEL_DARK);
            // 大碟（椭圆叠出弧度）
            p.fillEllipse(cx, topCY - 22, 16, 8, Color{200, 206, 214, 255});
            p.fillEllipse(cx, topCY - 23, 11, 5, Color{160, 168, 178, 255});
            p.fillEllipse(cx, topCY - 24, 5, 2, Color{110, 118, 130, 255});
            // 馈源杆 + 馈源头
            p.line(cx, topCY - 22, cx, topCY - 30, Pal::GUN);
            p.set(cx, topCY - 31, Color{120, 200, 255, 255});
            p.set(cx - 6, topCY - 26, Color{240, 248, 255, 255});                   // 碟面高光
            p.set(cx - 2, topCY - 16, trim);
            break;
        }
        case BldType::PsychicSensor: {
            // 心灵探测器：暗紫基座 + 三层天线环 + 顶部心灵球（1x1）
            p.fillEllipse(cx, baseCY - 2, halfW - 3, halfH - 1, Color{100, 92, 110, 255});
            p.fillRect(cx - 4, baseCY - 26, 8, 24, Color{84, 76, 98, 255});
            p.rect(cx - 4, baseCY - 26, 8, 24, Pal::GUN);
            p.hline(cx - 5, cx + 5, baseCY - 8, trim);
            // 三层天线环（渐小）
            p.ellipse(cx, baseCY - 14, 8, 3, Color{150, 120, 190, 255});
            p.ellipse(cx, baseCY - 20, 6, 2, Color{170, 140, 210, 255});
            p.ellipse(cx, baseCY - 25, 4, 2, Color{190, 160, 230, 255});
            // 心灵球
            p.fillEllipse(cx, baseCY - 31, 4, 4, Color{170, 110, 220, 255});
            p.set(cx - 1, baseCY - 33, Color{230, 190, 255, 255});
            break;
        }
        case BldType::BattleBunker: {
            // 战斗碉堡：混凝土重堡 + 多射击孔 + 沙袋圈（1x1）
            p.isoBox(cx, baseCY - 14, halfW - 2, halfH - 1, 14, Color{96, 94, 88, 255}, Color{124, 122, 114, 255}, Color{88, 86, 80, 255});
            p.diamond(cx, baseCY - 14, halfW - 4, halfH - 2, Color{80, 78, 72, 255});
            // 射击孔（三面）
            p.hline(cx - halfW + 5, cx - 2, baseCY - 10, Pal::GUN);
            p.hline(cx + 2, cx + halfW - 5, baseCY - 10, Pal::GUN);
            p.hline(cx - 3, cx + 3, baseCY - 6, Pal::GUN);
            // 沙袋圈
            for (int i = -2; i <= 2; i++)
                p.fillEllipse(cx + i * 5, baseCY + halfH - 3, 3, 2, Color{140, 124, 92, 255});
            p.hline(cx - 3, cx + 3, baseCY - 16, trim);
            break;
        }
        case BldType::TankBunker: {
            // 坦克碉堡：环形混凝土墙（开口朝东，坦克可进驻）（1x1）
            p.diamond(cx, baseCY - 2, halfW - 2, halfH - 1, Color{120, 118, 112, 255});  // 地坪
            // 环形墙（西/北/南三面，东侧开口）
            p.line(cx - halfW + 3, baseCY - 4, cx, baseCY - halfH - 2, Color{150, 148, 140, 255});
            p.line(cx - halfW + 3, baseCY - 4, cx, baseCY + halfH - 4, Color{140, 138, 130, 255});
            p.line(cx - halfW + 4, baseCY - 5, cx - halfW + 4, baseCY + 1, Color{150, 148, 140, 255});
            // 墙顶沿
            p.line(cx - halfW + 3, baseCY - 5, cx, baseCY - halfH - 3, Color{170, 168, 160, 255});
            // 开口两侧门柱
            p.set(cx + halfW - 6, baseCY - 6, Color{150, 148, 140, 255});
            p.set(cx + halfW - 6, baseCY + 2, Color{150, 148, 140, 255});
            p.hline(cx - 2, cx + 2, baseCY - halfH + 1, trim);
            break;
        }
        case BldType::TechAirport: {
            // 科技机场（中立）：民航跑道 + 停机坪 + 塔台（无阵营饰条）
            mainBox(2, wallH - 10, false);
            // 跑道（基座右前侧）+ 中线虚线
            p.diamond(cx + halfW / 4, baseCY + 2, halfW / 2 + 4, halfH / 2, Color{84, 84, 88, 255});
            for (int i = 0; i < 4; i++)
                p.set(cx - 2 + i * 7, baseCY + 2 - i, Color{230, 230, 230, 255});
            // 停机坪小客机剪影
            p.fillEllipse(cx - halfW / 3, baseCY - 2, 8, 2, Color{220, 224, 230, 255});
            p.line(cx - halfW / 3 - 2, baseCY - 2, cx - halfW / 3 + 2, baseCY - 6, Color{200, 204, 210, 255});
            // 塔台
            p.fillRect(cx + halfW - 13, topCY - 14, 7, 16, Color{170, 166, 156, 255});
            p.fillRect(cx + halfW - 15, topCY - 18, 11, 5, Pal::GLASS);
            p.set(cx + halfW - 10, topCY - 20, Color{255, 200, 80, 255});           // 航标灯
            break;
        }
        case BldType::SecretLab: {
            // 秘密实验室（中立）：低调灰绿厂房 + 绿色穹顶 + 排气管
            mainBox(2, wallH - 8, false);
            // 绿色实验穹顶
            p.fillEllipse(cx - 6, topCY - 2, 9, 7, Color{90, 150, 100, 255});
            p.fillEllipse(cx - 8, topCY - 4, 5, 4, Color{140, 210, 150, 255});
            p.ellipse(cx - 6, topCY - 2, 9, 7, Color{50, 90, 56, 255});
            // 排气管（两根，斜出）
            p.line(cx + 8, topCY, cx + 12, topCY - 12, Pal::STEEL_DARK);
            p.line(cx + 12, topCY, cx + 16, topCY - 10, Pal::STEEL_DARK);
            p.set(cx + 12, topCY - 13, Color{160, 220, 170, 200});                   // 绿雾
            p.set(cx + 16, topCY - 11, Color{160, 220, 170, 160});
            // 问号标识（神秘感）
            p.set(cx + halfW - 8, topCY + 3, Color{220, 200, 80, 255});
            p.set(cx + halfW - 8, topCY + 6, Color{220, 200, 80, 255});
            break;
        }
        case BldType::CivHouse: {
            // 民房（中立）：坡屋顶小屋 + 门窗 + 烟囱
            int rh = wallH - 6;
            p.isoBox(cx, topCY, halfW - 2, halfH - 1, rh, Color{150, 146, 140, 255}, bp.wallL, bp.wallR);
            // 坡屋顶（人字三角：两坡面）
            for (int i = 0; i < 8; i++) {
                int rw = (halfW - 2) - i * 2;
                if (rw <= 0) break;
                int ry = topCY - 3 - i * 2;
                p.diamond(cx, ry, rw, (halfH - 1) * (8 - i) / 8, i < 3 ? bp.roof : bp.roofIn);
            }
            p.line(cx - halfW + 2, topCY - 3, cx, topCY - 19, Color{140, 84, 64, 255}); // 屋脊线
            p.line(cx + halfW - 2, topCY - 3, cx, topCY - 19, Color{110, 64, 48, 255});
            // 烟囱 + 烟
            p.fillRect(cx + halfW / 2, topCY - 18, 4, 8, Color{120, 90, 70, 255});
            p.set(cx + halfW / 2 + 1, topCY - 21, Color{200, 200, 200, 140});
            p.set(cx + halfW / 2 + 2, topCY - 24, Color{200, 200, 200, 90});
            // 门 + 窗
            p.fillRect(cx - 3, baseCY - 9, 6, 8, Color{90, 60, 40, 255});
            p.rect(cx - 3, baseCY - 9, 6, 8, Color{60, 40, 26, 255});
            p.fillRect(cx - halfW + 6, baseCY - 10, 4, 4, Color{255, 220, 130, 255}); // 暖窗
            p.fillRect(cx + halfW - 10, baseCY - 10, 4, 4, Color{255, 220, 130, 255});
            break;
        }
        default: mainBox(2, wallH); break;
    }
    // 入口指示灯
    p.set(cx, baseCY + halfH - 2, trim);
    return p;
}

// ---------------- 特效 ----------------
PixBuf SpriteBank::baseExplosion(int frame) {
    // RA2 式火球：白闪星芒 → 湍流橙红火球（噪声边缘+外缘黑烟） → 黑烟余烬 + 四散火星
    float life = frame / (float)(EXPLOSION_FRAMES - 1); // 0..1
    int R = 7 + (int)(frame * 1.8f);
    int S = R * 2 + 12;
    PixBuf p(S, S);
    int cx = S / 2, cy = S / 2;
    Rng rng(frame * 131 + 7);
    // 湍流：角向正弦扰动半径 → 火焰舌状边缘
    auto turb = [&](float ang) {
        return 1.0f + 0.26f * sinf(ang * 3 + frame * 1.7f) + 0.16f * sinf(ang * 7 + frame * 2.9f);
    };
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float dx = (float)(x - cx), dy = (float)(y - cy);
            float d = sqrtf(dx * dx + dy * dy) / R;
            if (d > 1.7f) continue;
            d /= turb(atan2f(dy, dx));
            if (d > 1.0f) continue;
            Color c;
            if (life < 0.22f) {
                // 白炽核心 → 黄边
                c = Color{255, (uint8_t)(250 - d * 90), (uint8_t)(230 - d * 200), 255};
            } else if (life < 0.6f) {
                // 火球：核心白黄 → 橙 → 外缘暗红混入黑烟
                float t = (life - 0.22f) / 0.38f;
                float e = d * d;
                int rr = (int)(255 - e * 90 - t * 40);
                int gg = (int)(212 - e * 165 - t * 95);
                int bb = (int)(96 - e * 86);
                if (d > 0.82f) { // 外缘黑烟团
                    float m = (d - 0.82f) / 0.18f;
                    int sv = (int)(72 - t * 32);
                    rr = (int)(rr * (1 - m) + sv * m);
                    gg = (int)(gg * (1 - m) + sv * m);
                    bb = (int)(bb * (1 - m) + sv * m);
                }
                c = Color{(uint8_t)clampi(rr, 0, 255), (uint8_t)clampi(gg, 0, 255),
                          (uint8_t)clampi(bb, 0, 255), 255};
            } else {
                // 末期：黑烟 + 残余橙烬
                float t = (life - 0.6f) / 0.4f;
                int v = (int)(64 - t * 30);
                c = Color{(uint8_t)v, (uint8_t)v, (uint8_t)(v + 2),
                          (uint8_t)(235 * (1.0f - t * 0.75f))};
                if (d < 0.45f && rng.unit() > 0.55f) // 烬核
                    c = Color{(uint8_t)(200 - t * 140), (uint8_t)(90 - t * 60), 30, c.a};
            }
            p.set(x, y, c);
        }
    // 火星：早中期帧向外飞散的亮点
    if (life < 0.7f) {
        int nSpark = 10 - (int)(life * 10);
        for (int i = 0; i < nSpark; i++) {
            float ang = rng.unit() * 6.2831853f;
            float rr = R * (0.6f + life * 0.9f) + rng.unit() * 4;
            int sx = cx + (int)(cosf(ang) * rr), sy = cy + (int)(sinf(ang) * rr * 0.9f);
            p.set(sx, sy, Color{255, 230, 160, 255});
            if (rng.unit() > 0.5f) p.set(sx + 1, sy, Color{255, 160, 60, 220});
        }
    }
    // 第 0 帧附加白闪十字星芒
    if (frame == 0) {
        for (int i = 0; i < R + 6; i++) {
            uint8_t a = (uint8_t)(255 - i * 30);
            Color ray{255, 255, 240, a};
            p.set(cx + i, cy, ray); p.set(cx - i, cy, ray);
            p.set(cx, cy + i, ray); p.set(cx, cy - i, ray);
        }
    }
    return p;
}

PixBuf SpriteBank::baseMuzzle() {
    // RA2 式枪口焰：十字星芒 + 对角短芒 + 白炽核心
    PixBuf p(14, 14);
    int c = 7;
    for (int i = 0; i < 7; i++) {
        Color ray{255, (uint8_t)(220 - i * 18), (uint8_t)(120 - i * 14), (uint8_t)(255 - i * 32)};
        p.set(c + i, c, ray); p.set(c - i, c, ray);
        p.set(c, c + i, ray); p.set(c, c - i, ray);
    }
    for (int i = 1; i < 4; i++) {
        Color dc{255, (uint8_t)(190 - i * 20), 90, (uint8_t)(220 - i * 50)};
        p.set(c + i, c + i, dc); p.set(c - i, c - i, dc);
        p.set(c + i, c - i, dc); p.set(c - i, c + i, dc);
    }
    p.fillEllipse(c, c, 2, 2, Color{255, 255, 235, 255});
    return p;
}

PixBuf SpriteBank::baseProjectile(int kind, int dir) {
    PixBuf p;
    if (kind == 0) { // 炮弹：灼热弹头 + 高光点
        p.resize(12, 12);
        p.fillEllipse(6, 6, 3, 2, Color{60, 58, 54, 255});
        p.fillEllipse(5, 5, 2, 1, Color{255, 214, 130, 255});
        p.set(4, 4, Color{255, 246, 200, 255});
    } else { // 导弹（朝东基准）：弹体高光 + 红头 + 尾翼 + 尾焰
        p.resize(18, 10);
        p.fillRect(4, 4, 10, 3, Color{204, 204, 210, 255});
        p.fillRect(4, 4, 10, 1, Color{240, 240, 245, 255});   // 顶部高光
        p.line(14, 4, 17, 5, Color{220, 90, 60, 255});        // 弹头
        p.line(14, 6, 17, 5, Color{220, 90, 60, 255});
        p.line(4, 3, 7, 1, Color{160, 160, 165, 255});        // 尾翼
        p.line(4, 7, 7, 8, Color{160, 160, 165, 255});
        p.hline(0, 3, 5, Color{255, 180, 80, 255});           // 尾焰
        p.set(0, 5, Color{255, 220, 120, 200});
        p.set(1, 6, Color{255, 220, 120, 255});
        p.set(2, 6, Color{255, 140, 60, 200});
    }
    if (dir) p = p.rotate8(dir);
    return p;
}

PixBuf SpriteBank::baseSmoke(int frame) {
    // RA2 式烟团：多团瓣叠加 + 上升扩散 + 渐稀 + 顶部亮缘
    float t = frame / (float)(SMOKE_FRAMES - 1); // 0..1
    int R = 5 + frame * 2;
    int S = R * 2 + 10;
    PixBuf p(S, S);
    int cx = S / 2, cy = S / 2;
    uint8_t a = (uint8_t)(150 * (1.0f - t * 0.8f));
    // 4 个烟瓣（随帧旋转扩散、上升）
    for (int l = 0; l < 4; l++) {
        float ang = l * 1.9f + frame * 0.35f;
        int ox = (int)(cosf(ang) * R * 0.4f), oy = (int)(sinf(ang) * R * 0.3f) - (int)(t * 6);
        int lr = R - l * 2; if (lr < 3) lr = 3;
        int v = (int)(96 + l * 14 - t * 30);
        p.fillEllipse(cx + ox, cy + oy, lr, lr * 3 / 4,
                      Color{(uint8_t)v, (uint8_t)v, (uint8_t)(v + 4), a});
    }
    // 顶部受光亮缘
    int hv = (int)(150 - t * 40);
    p.fillEllipse(cx - 1, cy - R / 3 - (int)(t * 6), R / 3, R / 5,
                  Color{(uint8_t)hv, (uint8_t)hv, (uint8_t)(hv + 4), (uint8_t)(a * 0.7f)});
    return p;
}

// ---------------- 内容图（基础绘制 + RA2 风格化后处理） ----------------
PixBuf SpriteBank::unitContentPix(UnitType t, int dir, int fKey) {
    PixBuf pb;
    M3Builder mb;
    if (buildUnitModel3D(t, mb, fKey != 0)) { // 载具/舰船/飞行器：3D 预渲染
        int cs = unitCanvasSize3D(t);
        PixBuf r = m3Render(mb.quads, dir, cs, cs, unitGroundY3D(t),
                            hasTurret(t) ? M3P_BODY : (uint8_t)0xFF, 0, 0, unitScale3D(t));
        pb = padCanvas(r, 2, 2, 2, 2);
    } else { // 步兵/军犬等：2D 绘制
        pb = padCanvas(baseUnitBody(t, dir, fKey), 2, 2, 2, 2); // 留白供轮廓外扩
    }
    ra2Bevel(pb);
    ra2Outline(pb);
    return pb;
}
PixBuf SpriteBank::turretContentPix(UnitType t, int dir) {
    PixBuf pb;
    M3Builder mb;
    float pvx = 0, pvy = 0;
    if (buildUnitModel3D(t, mb, false, &pvx, &pvy)) {
        int cs = unitCanvasSize3D(t);
        pb = m3Render(mb.quads, dir, cs, cs, unitGroundY3D(t), M3P_TURRET, pvx, pvy, unitScale3D(t));
    } else {
        pb = baseUnitTurret(t, dir);
    }
    ra2Bevel(pb);
    ra2Outline(pb);
    return pb;
}
PixBuf SpriteBank::bldContentPix(BldType t, bool constructing) {
    PixBuf pb;
    M3Builder mb;
    if (!constructing && buildBldModel3D(t, mb)) {
        // 3D 预渲染建筑：模型在瓦片坐标系拼装（占地 0..w × 0..h，原点在 (0,0) 角），
        // 需平移使占地中心对准渲染原点（锚点契约：内容画布底中点 = 占地中心地面）
        const BldDef& d = bldDef(t);
        float halfH = (d.w + d.h) * TILE_H / 4.0f;             // 底面菱形半高（屏幕像素）
        float tx = -(d.w - d.h) * (TILE_W / 4.0f);             // 占地中心模型 x → 0
        float ty = (d.w + d.h) * (TILE_W / 4.0f);              // 占地中心模型 y → 0
        for (M3Quad& q : mb.quads)
            for (int k = 0; k < 4; k++) { q.v[k][0] += tx; q.v[k][1] += ty; }
        float maxZ = 0.0f; // 实际最高点 → 顶部余量（塔吊/天线/超武差异大）
        for (const M3Quad& q : mb.quads)
            for (int k = 0; k < 4; k++) maxZ = std::max(maxZ, q.v[k][2]);
        int outW = (d.w + d.h) * TILE_W / 2 + 24;              // 与旧 2D 画布同宽
        int gy = (int)ceilf(halfH + maxZ * 0.866f + 8.0f);
        int outH = gy + (int)ceilf(halfH * 2.0f) + 4;          // 底部 4px 留白
        pb = padCanvas(m3Render(mb.quads, 0, outW, outH, (float)gy), 2, 2, 2, 2);
    } else {
        pb = padCanvas(baseBuilding(t, constructing), 2, 2, 2, 2); // 脚手架/未建模：2D 兜底
    }
    ra2Bevel(pb);
    ra2Outline(pb);
    return pb;
}

// ---------------- 对外获取（带缓存） ----------------
const Sprite& SpriteBank::tile(Terrain t, int variant) {
    uint64_t k = keyOf(1, (int)t, variant, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/tile_%s_%d.png", terrainAssetName(t), variant))
        pb = baseTile(t, variant);
    Sprite s = makeSprite(std::move(pb), TILE_W / 2, 0);
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::overlaySpr(Overlay o) {
    uint64_t k = keyOf(2, (int)o, 0, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/overlay_%s.png", overlayAssetName(o)))
        pb = baseOverlay(o);
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height - 1;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::unitBody(UnitType t, int dir, int frame, int player) {
    UnitType orig = t;
    t = spriteAliasUnit(t);
    dir &= 7;
    // 满载采矿车用 frame=1（只对载具有效；步兵 frame 为行走帧）
    bool isMiner = (t == UnitType::Harvester || t == UnitType::ChronoMiner || t == UnitType::WarMiner);
    int fKey = isMiner ? (frame ? 1 : 0) : (unitDef(t).isInfantry() ? (frame & 1) : 0);
    uint64_t k = keyOf(3, (int)t, dir, fKey, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    // 素材文件优先（assets/sprites/ 由 --gen-assets 离线生成或用户自制），缺失回退程序生成
    bool ext = loadSpr(pb, "assets/sprites/unit_%s_d%d_f%d.png", unitAssetName(orig), dir, fKey)
            || loadSpr(pb, "assets/sprites/unit_%s_d%d.png", unitAssetName(orig), dir)
            || (orig != t && (loadSpr(pb, "assets/sprites/unit_%s_d%d_f%d.png", unitAssetName(t), dir, fKey)
                           || loadSpr(pb, "assets/sprites/unit_%s_d%d.png", unitAssetName(t), dir)));
    if (!ext) pb = unitContentPix(t, dir, fKey);
    // RA2 风格地面投影：仅地面单位（空军/海军不烘投影）；文件素材与程序生成统一烘焙
    const UnitDef& ud = unitDef(t);
    if (!ud.isAir() && !ud.isNaval()) {
        int ow = pb.w, oh = pb.h;
        bool inf = ud.isInfantry();
        PixBuf canvas(ow + 12, oh + 8);
        bakeShadow(canvas, 6 + ow / 2 + 3, 4 + (inf ? oh - 2 : (int)(oh * 0.72f)),
                   inf ? 7 : (int)(ow * 0.30f), inf ? 3 : (int)(oh * 0.10f));
        canvas.blit(pb, 6, 4);
        pb = std::move(canvas);
    }
    pb.remap(Pal::REMAP, HOUSE_COLORS[player]);
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2 + 4;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::unitTurret(UnitType t, int dir, int player) {
    UnitType orig = t;
    t = spriteAliasUnit(t);
    dir &= 7;
    uint64_t k = keyOf(4, (int)t, dir, 0, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    bool ext = loadSpr(pb, "assets/sprites/turret_%s_d%d.png", unitAssetName(orig), dir)
            || (orig != t && loadSpr(pb, "assets/sprites/turret_%s_d%d.png", unitAssetName(t), dir));
    if (!ext) pb = turretContentPix(t, dir);
    pb.remap(Pal::REMAP, HOUSE_COLORS[player]);
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2 + 4;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::building(BldType t, int player, bool constructing) {
    BldType orig = t;
    t = spriteAliasBld(t);
    uint64_t k = keyOf(5, (int)t, constructing ? 1 : 0, 0, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    bool ext = loadSpr(pb, "assets/sprites/bld_%s%s.png", bldAssetName(orig), constructing ? "_scaffold" : "")
            || (orig != t && loadSpr(pb, "assets/sprites/bld_%s%s.png", bldAssetName(t), constructing ? "_scaffold" : ""));
    if (!ext) pb = bldContentPix(t, constructing);
    // RA2 风格地面投影（底部偏右椭圆）；文件素材与程序生成统一烘焙、统一锚点
    int ow = pb.w, oh = pb.h;
    PixBuf canvas(ow + 14, oh + 10);
    bakeShadow(canvas, 6 + ow / 2 + 5, 4 + oh - 6, (int)(ow * 0.40f), 6);
    canvas.blit(pb, 6, 4);
    pb = std::move(canvas);
    pb.remap(Pal::REMAP, player >= 0 ? HOUSE_COLORS[player] : Color{150, 150, 155, 255}); // 中立=灰
    Sprite s = makeSprite(std::move(pb), 0, 0);
    // 锚点 = 内容画布底中点经填充后的坐标（左填 6 上填 4）
    s.ox = (s.tex.width - 14) / 2 + 6;
    s.oy = s.tex.height - 10;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::explosion(int frame) {
    frame = clampi(frame, 0, EXPLOSION_FRAMES - 1);
    uint64_t k = keyOf(6, frame, 0, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/fx_explosion_%d.png", frame))
        pb = baseExplosion(frame);
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::muzzle() {
    uint64_t k = keyOf(7, 0, 0, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/fx_muzzle.png"))
        pb = baseMuzzle();
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::projectile(int kind, int dir) {
    uint64_t k = keyOf(8, kind, dir & 7, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/fx_proj_%d_d%d.png", kind, dir & 7))
        pb = baseProjectile(kind, dir);
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::smoke(int frame) {
    frame = clampi(frame, 0, SMOKE_FRAMES - 1);
    uint64_t k = keyOf(9, frame, 0, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/fx_smoke_%d.png", frame))
        pb = baseSmoke(frame);
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2;
    return cache.emplace(k, s).first->second;
}

// ---------------- 图标 ----------------
static PixBuf makeIcon(const PixBuf& src, int ox, int oy) {
    // 放入 56x44 画布
    PixBuf p(56, 44);
    p.fillRect(0, 0, 56, 44, Color{24, 26, 30, 255});
    // 计算缩放
    float sx = 52.0f / src.w, sy = 40.0f / src.h;
    float sc = sx < sy ? sx : sy;
    if (sc > 1.6f) sc = 1.6f;
    int nw = (int)(src.w * sc), nh = (int)(src.h * sc);
    PixBuf scaled = src.scale(nw > 0 ? nw : 1, nh > 0 ? nh : 1);
    // 提取有效区域（以锚点为准简单整体缩放绘制）
    p.blit(scaled, (56 - nw) / 2, (44 - nh) / 2);
    return p;
}

const Sprite& SpriteBank::iconUnit(UnitType t, int player) {
    uint64_t k = keyOf(10, (int)t, 0, 0, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf ext;
    if (loadSpr(ext, "assets/sprites/icon_unit_%s.png", unitAssetName(t))) {
        ext.remap(Pal::REMAP, HOUSE_COLORS[player]);
        Sprite s = makeSprite(std::move(ext), 0, 0);
        return cache.emplace(k, s).first->second;
    }
    t = spriteAliasUnit(t);
    PixBuf body = unitContentPix(t, 2, 0);
    if (hasTurret(t)) {
        PixBuf tur = turretContentPix(t, 2); // 炮塔画布与车体同尺寸，对齐内容偏移 (2,2)
        body.blit(tur, 2, 2);
    }
    body.remap(Pal::REMAP, HOUSE_COLORS[player]);
    Sprite s = makeSprite(makeIcon(body, 0, 0), 0, 0);
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::iconBld(BldType t, int player) {
    uint64_t k = keyOf(11, (int)t, 0, 0, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf ext;
    if (loadSpr(ext, "assets/sprites/icon_bld_%s.png", bldAssetName(t))) {
        ext.remap(Pal::REMAP, HOUSE_COLORS[player]);
        Sprite s = makeSprite(std::move(ext), 0, 0);
        return cache.emplace(k, s).first->second;
    }
    t = spriteAliasBld(t);
    PixBuf pb = bldContentPix(t, false);
    pb.remap(Pal::REMAP, HOUSE_COLORS[player]);
    Sprite s = makeSprite(makeIcon(pb, 0, 0), 0, 0);
    return cache.emplace(k, s).first->second;
}

void SpriteBank::init() {
    inited = true;
    // 预生成地形瓦片（常用）
    for (int t = 0; t <= (int)Terrain::Bridge; t++)
        for (int v = 0; v < 8; v++) tile((Terrain)t, v);
    for (int o = 1; o <= (int)Overlay::Rock2; o++) overlaySpr((Overlay)o);
    for (int f = 0; f < EXPLOSION_FRAMES; f++) explosion(f);
    for (int f = 0; f < SMOKE_FRAMES; f++) smoke(f);
    muzzle();
}

// ===================== 离线素材生成（--gen-assets） =====================
// 管线：程序绘制 → PNG 文件（assets/sprites/）；游戏运行时直接加载文件，程序生成仅作缺失兜底

// 单位帧键：与 unitBody() 的 fKey 规则一致（采矿车满载帧 / 步兵行走帧 / 其他仅 0）
static std::vector<int> unitFrameKeys(UnitType t) {
    bool isMiner = (t == UnitType::Harvester || t == UnitType::ChronoMiner || t == UnitType::WarMiner);
    if (isMiner || unitDef(t).isInfantry()) return {0, 1};
    return {0};
}

bool SpriteBank::genAssets(const char* outDir) {
    MakeDirectory("assets");
    MakeDirectory(outDir);
    MakeDirectory("assets/preview");
    int n = 0, fail = 0;
    auto save = [&](const PixBuf& pb, const char* fmt, ...) {
        char path[256];
        va_list ap; va_start(ap, fmt);
        vsnprintf(path, sizeof(path), fmt, ap);
        va_end(ap);
        if (pb.saveToFile(path)) n++; else fail++;
    };
    // 地形瓦片（6 类 × 8 变体）
    for (int t = 0; t <= (int)Terrain::Bridge; t++)
        for (int v = 0; v < 8; v++)
            save(baseTile((Terrain)t, v), "%s/tile_%s_%d.png", outDir, terrainAssetName((Terrain)t), v);
    // 地表装饰
    for (int o = 1; o <= (int)Overlay::Rock2; o++)
        save(baseOverlay((Overlay)o), "%s/overlay_%s.png", outDir, overlayAssetName((Overlay)o));
    // 单位（全方向全帧）+ 炮塔
    for (int i = 0; i < (int)UnitType::COUNT; i++) {
        UnitType t = (UnitType)i;
        const char* nm = unitAssetName(t);
        for (int d = 0; d < 8; d++)
            for (int f : unitFrameKeys(t))
                save(unitContentPix(t, d, f), "%s/unit_%s_d%d_f%d.png", outDir, nm, d, f);
        if (hasTurret(t))
            for (int d = 0; d < 8; d++)
                save(turretContentPix(t, d), "%s/turret_%s_d%d.png", outDir, nm, d);
    }
    // 建筑（成品 + 脚手架）
    for (int i = 0; i < (int)BldType::COUNT; i++) {
        BldType t = (BldType)i;
        const char* nm = bldAssetName(t);
        save(bldContentPix(t, false), "%s/bld_%s.png", outDir, nm);
        save(bldContentPix(t, true), "%s/bld_%s_scaffold.png", outDir, nm);
    }
    // 特效
    for (int f = 0; f < EXPLOSION_FRAMES; f++) save(baseExplosion(f), "%s/fx_explosion_%d.png", outDir, f);
    for (int f = 0; f < SMOKE_FRAMES; f++) save(baseSmoke(f), "%s/fx_smoke_%d.png", outDir, f);
    save(baseMuzzle(), "%s/fx_muzzle.png", outDir);
    for (int kind = 0; kind < 2; kind++)
        for (int d = 0; d < 8; d++)
            save(baseProjectile(kind, d), "%s/fx_proj_%d_d%d.png", outDir, kind, d);
    // 图标（与运行时 iconUnit/iconBld 的程序生成路径一致，保留红色占位供运行时换色）
    for (int i = 0; i < (int)UnitType::COUNT; i++) {
        UnitType t = (UnitType)i;
        PixBuf body = unitContentPix(t, 2, 0);
        if (hasTurret(t)) body.blit(turretContentPix(t, 2), 2, 2);
        save(makeIcon(body, 0, 0), "%s/icon_unit_%s.png", outDir, unitAssetName(t));
    }
    for (int i = 0; i < (int)BldType::COUNT; i++)
        save(makeIcon(bldContentPix((BldType)i, false), 0, 0), "%s/icon_bld_%s.png", outDir, bldAssetName((BldType)i));

    // ---------- 审核预览图（assets/preview/，不参与游戏） ----------
    // 通用网格拼版：rows 个条目 × cols 个方向/形态，单元格取最大内容尺寸
    auto sheet = [&](const char* path, const std::vector<std::vector<PixBuf>>& rows, Color bg) {
        int cw = 0, ch = 0;
        for (auto& r : rows)
            for (auto& c : r) { cw = std::max(cw, c.w); ch = std::max(ch, c.h); }
        cw += 8; ch += 8;
        int cols = 0;
        for (auto& r : rows) cols = std::max(cols, (int)r.size());
        PixBuf s(std::max(1, cols * cw), std::max(1, (int)rows.size() * ch));
        s.clear(bg);
        for (size_t y = 0; y < rows.size(); y++)
            for (size_t x = 0; x < rows[y].size(); x++) {
                const PixBuf& c = rows[y][x];
                s.blit(c, (int)x * cw + (cw - c.w) / 2, (int)y * ch + (ch - c.h) / 2);
            }
        s.saveToFile(path);
    };
    Color sheetBg{40, 42, 46, 255};
    {   // 单位：每单位一行（f0 × 8 方向），有第二帧的追加一行 f1；带炮塔的行尾叠加示意
        std::vector<std::vector<PixBuf>> rows;
        for (int i = 0; i < (int)UnitType::COUNT; i++) {
            UnitType t = (UnitType)i;
            for (int f : unitFrameKeys(t)) {
                std::vector<PixBuf> row;
                for (int d = 0; d < 8; d++) {
                    PixBuf c = unitContentPix(t, d, f);
                    if (hasTurret(t) && f == 0) c.blit(turretContentPix(t, d), 2, 2);
                    row.push_back(std::move(c));
                }
                rows.push_back(std::move(row));
            }
        }
        sheet("assets/preview/units.png", rows, sheetBg);
    }
    {   // 建筑：每个一行（成品 | 脚手架）
        std::vector<std::vector<PixBuf>> rows;
        for (int i = 0; i < (int)BldType::COUNT; i++)
            rows.push_back({bldContentPix((BldType)i, false), bldContentPix((BldType)i, true)});
        sheet("assets/preview/buildings.png", rows, sheetBg);
    }
    {   // 地形与装饰
        std::vector<std::vector<PixBuf>> rows;
        for (int t = 0; t <= (int)Terrain::Bridge; t++) {
            std::vector<PixBuf> row;
            for (int v = 0; v < 8; v++) row.push_back(baseTile((Terrain)t, v));
            rows.push_back(std::move(row));
        }
        std::vector<PixBuf> ov;
        for (int o = 1; o <= (int)Overlay::Rock2; o++) ov.push_back(baseOverlay((Overlay)o));
        rows.push_back(std::move(ov));
        sheet("assets/preview/terrain.png", rows, sheetBg);
    }
    {   // 特效：爆炸 12 帧 / 烟 6 帧 / 枪口 / 抛射体 2 类 × 8 方向
        std::vector<std::vector<PixBuf>> rows;
        std::vector<PixBuf> ex, sm, mu, p0, p1;
        for (int f = 0; f < EXPLOSION_FRAMES; f++) ex.push_back(baseExplosion(f));
        for (int f = 0; f < SMOKE_FRAMES; f++) sm.push_back(baseSmoke(f));
        mu.push_back(baseMuzzle());
        for (int d = 0; d < 8; d++) { p0.push_back(baseProjectile(0, d)); p1.push_back(baseProjectile(1, d)); }
        rows = {std::move(ex), std::move(sm), std::move(mu), std::move(p0), std::move(p1)};
        sheet("assets/preview/fx.png", rows, sheetBg);
    }
    {   // 图标墙：单位图标 + 建筑图标（8 列）
        std::vector<std::vector<PixBuf>> rows;
        std::vector<PixBuf> row;
        auto flush = [&]() { if (!row.empty()) { rows.push_back(std::move(row)); row.clear(); } };
        for (int i = 0; i < (int)UnitType::COUNT; i++) {
            UnitType t = (UnitType)i;
            PixBuf body = unitContentPix(t, 2, 0);
            if (hasTurret(t)) body.blit(turretContentPix(t, 2), 2, 2);
            row.push_back(makeIcon(body, 0, 0));
            if (row.size() == 8) flush();
        }
        for (int i = 0; i < (int)BldType::COUNT; i++) {
            row.push_back(makeIcon(bldContentPix((BldType)i, false), 0, 0));
            if (row.size() == 8) flush();
        }
        flush();
        sheet("assets/preview/icons.png", rows, sheetBg);
    }

    TraceLog(LOG_INFO, "gen-assets: %d sprites written to %s (%d failed)", n, outDir, fail);
    printf("gen-assets: %d sprites written to %s, %d failed; previews in assets/preview/\n", n, outDir, fail);
    return fail == 0;
}
