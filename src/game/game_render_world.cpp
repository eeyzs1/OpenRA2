#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "gfx/assets.h"
#include "sfx/sound.h"
#include "rlgl.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <vector>

#include "gfx/bld_cage_data.inc"

// 血条/角标必须与单位同一绘制路径：RenderTexture 下 DrawRectangle 与 DrawTexture
// 可能不一致，表现为框选逻辑在左上、贴图在右下。用 1×1 白纹理解算矩形。
static Texture2D g_worldRectTex{};
static void ensureWorldRectTex() {
    if (g_worldRectTex.id != 0) return;
    Image img = GenImageColor(1, 1, WHITE);
    g_worldRectTex = LoadTextureFromImage(img);
    UnloadImage(img);
}
static void fillRectWorld(float x, float y, float w, float h, Color c) {
    if (w <= 0.f || h <= 0.f) return;
    ensureWorldRectTex();
    DrawTexturePro(g_worldRectTex, {0, 0, 1, 1}, {x, y, w, h}, {0, 0}, 0.f, c);
}
void Game::flushWorldOverlayRects() {
    // 叠层已在 zoom 矩阵内用 DrawTexturePro 画完；保留接口供调用方清空语义
}

// Selection cage: Foundation parallelogram at geo SE (bldScreenPos), elev from fit.
// Global nudge + N px toward S along both diamond edges (E→S and W→S).
// Collision / place still use full tile Foundation (BldDef w×h).
static constexpr float kCageNudgeX = -2.0f;
static constexpr float kCageNudgeY = +1.0f;
static constexpr float kCageEdgeNudgePx = 4.0f; // along E→S and W→S
static constexpr float kCageDownNudgePx = 3.0f; // along Ts→S (screen +Y)
struct BldCageParams { float eArmX, eArmY, wArmX, wArmY, elev, offX, offY; };
// meas.footW/H > 0：仅选框占地（可小于 BldDef 碰撞占地）；0 = 跟 BldDef。
static bool lookupBldCageMeas(const char* stem, float& elev, float& offX, float& offY,
                              int& footW, int& footH) {
    for (int i = 0; i < kBldCageMeasN; i++) {
        if (std::strcmp(kBldCageMeas[i].stem, stem) == 0) {
            elev = kBldCageMeas[i].elev;
            offX = kBldCageMeas[i].offX;
            offY = kBldCageMeas[i].offY;
            if (kBldCageMeas[i].footW > 0 && kBldCageMeas[i].footH > 0) {
                footW = kBldCageMeas[i].footW;
                footH = kBldCageMeas[i].footH;
            }
            return true;
        }
    }
    return false;
}
static BldCageParams resolveBldCage(const char* stem, int footW, int footH, const Sprite& spr) {
    footW = std::max(footW, 1);
    footH = std::max(footH, 1);
    float elev = (float)TILE_H, offX = 0.0f, offY = 0.0f;
    bool haveMeas = lookupBldCageMeas(stem, elev, offX, offY, footW, footH);
    footW = std::max(footW, 1);
    footH = std::max(footH, 1);
    // Iso Foundation from SE tip: E along h cells, W along w cells.
    BldCageParams c{
        (float)footH * (TILE_W / 2.0f),
        -(float)footH * (TILE_H / 2.0f),
        -(float)footW * (TILE_W / 2.0f),
        -(float)footW * (TILE_H / 2.0f),
        elev, offX, offY};
    if (!haveMeas) {
        float halfD = (float)(footW + footH) * (TILE_H / 4.0f);
        c.elev = std::max((float)spr.visElev() - halfD, 8.0f);
    }
    // Soft roof cap only — never crush elev by full foundation depth.
    float roof = std::max(8.0f, (float)spr.visElev() - 4.0f);
    c.elev = std::min(c.elev, roof);
    c.elev = std::clamp(c.elev, 8.0f, 280.0f);
    // 与 fit_bld_cages / 逐栋核对截图一致：全局 nudge + 边向/向下 nudge
    // （fitted offX/offY 是相对 geo SE 的微调，nudge 是引擎侧统一座椅偏移）
    c.offX += kCageNudgeX;
    c.offY += kCageNudgeY;
    auto addAlong = [&](float dx, float dy) {
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 1e-3f) {
            c.offX += kCageEdgeNudgePx * dx / len;
            c.offY += kCageEdgeNudgePx * dy / len;
        }
    };
    addAlong(-c.eArmX, -c.eArmY); // E → S
    addAlong(-c.wArmX, -c.wArmY); // W → S
    c.offY += kCageDownNudgePx;
    return c;
}

static void footCorners(Vector2 bs, const BldCageParams& cage, Vector2& bn, Vector2& be, Vector2& bw) {
    be = {bs.x + cage.eArmX, bs.y + cage.eArmY};
    bw = {bs.x + cage.wArmX, bs.y + cage.wArmY};
    bn = {bs.x + cage.eArmX + cage.wArmX, bs.y + cage.eArmY + cage.wArmY};
}

// 连续值噪声：晶格哈希 + 双线性插值（世界像素坐标 → 跨瓦片连续，无逐格重置）
static inline uint32_t tHash(int x, int y, uint64_t seed) {
    uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)seed * 83492791u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
static float tNoise(float x, float y, int stride, uint64_t seed) {
    int gx = (int)floorf(x / stride), gy = (int)floorf(y / stride);
    float fx = x / stride - gx, fy = y / stride - gy;
    fx = fx * fx * (3.0f - 2.0f * fx); fy = fy * fy * (3.0f - 2.0f * fy);
    auto h = [&](int ix, int iy) { return (float)(tHash(ix, iy, seed) % 1024) / 1024.0f; };
    float a = h(gx, gy), b = h(gx + 1, gy), c = h(gx, gy + 1), d = h(gx + 1, gy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static uint8_t lerp8(int a, int b, float t) { return (uint8_t)(a + (int)((b - a) * t)); }

void Game::bakeTerrain() {
    double bakeT0 = GetTime();
    int w = world.map.w, h = world.map.h;
    if (w <= 0 || h <= 0) return;
    terrainOX = (float)(h - 1) * (TILE_W / 2);
    terrainW = (w + h - 2) * (TILE_W / 2) + TILE_W;
    terrainH = (w + h - 2) * (TILE_H / 2) + TILE_H;
    // GPU 纹理上限钳制（常见上限 8192）：128 大地图 8193px 超界，降到 8000 内
    terrainSC = std::min(TERRAIN_SC, std::min(8000.0f / terrainW, 8000.0f / terrainH));
    int bw = (int)(terrainW * terrainSC) + 1, bh = (int)(terrainH * terrainSC) + 1;
    PixBuf pb(bw, bh);
    const uint64_t seed = 20260723; // 固定种子：同一地形类型烘焙观感一致
    // 真实 RA2 地形瓦片（gen_terrain.py 自 isotemp.mix 提取，64x32 菱形）：
    // 文件存在则逐像素采样。启动时 SpriteBank 已要求瓦片齐全；此处仅处理采样缝隙。
    PixBuf tilePx[6][8];
    bool tileOk[6][8] = {};
    {
        static const char* kTileNames[6] = {"clear", "rough", "water", "ore", "gems", "bridge"};
        for (int ti = 0; ti < 6; ti++) {
            if (ti == (int)Terrain::Ore || ti == (int)Terrain::Gems) continue;
            for (int v = 0; v < 8; v++) {
                char path[192];
                snprintf(path, sizeof(path), "assets/sprites/tile_%s_%d.png", kTileNames[ti], v);
                tileOk[ti][v] = tilePx[ti][v].loadFromFile(path)
                             && tilePx[ti][v].w == TILE_W && tilePx[ti][v].h == TILE_H;
            }
        }
    }
    // 岸线过渡瓦片（RA2 shore 系列，按邻水 mask 索引；gen_terrain.py 自 isotemp.mix 扫描分类提取）
    PixBuf shorePx[16][2];
    bool shoreOk[16][2] = {};
    for (int m = 1; m < 16; m++)
        for (int v = 0; v < 2; v++) {
            char path[192];
            snprintf(path, sizeof(path), "assets/sprites/tile_shore_m%d_%d.png", m, v);
            shoreOk[m][v] = shorePx[m][v].loadFromFile(path)
                         && shorePx[m][v].w == TILE_W && shorePx[m][v].h == TILE_H;
        }
    // 预计算每格邻水 mask（仅陆格：bit0 +x / bit1 +y / bit2 -x / bit3 -y 邻格为水）
    std::vector<uint8_t> shoreMask((size_t)w * h, 0);
    for (int ty = 0; ty < h; ty++)
        for (int tx = 0; tx < w; tx++) {
            Terrain tt = world.map.at(tx, ty).terrain;
            if (tt == Terrain::Water || tt == Terrain::Bridge) continue;
            uint8_t m = 0;
            if (world.map.inBounds(tx + 1, ty) && world.map.at(tx + 1, ty).terrain == Terrain::Water) m |= 1;
            if (world.map.inBounds(tx, ty + 1) && world.map.at(tx, ty + 1).terrain == Terrain::Water) m |= 2;
            if (world.map.inBounds(tx - 1, ty) && world.map.at(tx - 1, ty).terrain == Terrain::Water) m |= 4;
            if (world.map.inBounds(tx, ty - 1) && world.map.at(tx, ty - 1).terrain == Terrain::Water) m |= 8;
            shoreMask[(size_t)ty * w + tx] = m;
        }
    for (int by = 0; by < bh; by++) {
        float sy = by / terrainSC;
        for (int bx = 0; bx < bw; bx++) {
            float sx = bx / terrainSC - terrainOX; // 等距世界坐标（tileToScreen 域）
            // 不做抖动：偏移会在菱形瓦片边缘制造可见缝（RA2 地面应一体）
            float jx = sx, jy = sy;
            float fx = jx / (TILE_W / 2.0f), fy = jy / (TILE_H / 2.0f);
            int tx = (int)floorf((fx + fy) / 2.0f), ty = (int)floorf((fy - fx) / 2.0f);
            if (!world.map.inBounds(tx, ty)) continue; // 地图外透明（显示黑底）
            Terrain t = world.map.at(tx, ty).terrain;
            Color c;
            // 优先采样真实 RA2 瓦片：变体按 5x5 格块哈希选取（块内恒定 → 成片纹理；
            // 88% 概率用主变体 0，降低格间硬跳变的棋盘感）；
            int ti = (int)t;
            uint32_t vh = tHash(tx / 5, ty / 5, seed + 13);
            int tv = (vh % 100 < 94) ? 0 : 1 + (int)(vh % 7);
            bool fromTile = false;
            int ppx = 0, ppy = 0;
            auto sampleTileAt = [&](int txi, int tyi, const PixBuf& src, float sxw, float syw) -> bool {
                tileToScreen(txi, tyi, ppx, ppy);
                int ix = (int)floorf(sxw - ppx) + TILE_W / 2, iy = (int)floorf(syw - ppy);
                if ((unsigned)ix >= (unsigned)TILE_W || (unsigned)iy >= (unsigned)TILE_H) return false;
                Color tc = src.px[(size_t)iy * TILE_W + ix];
                if (tc.a < 16) return false; // 半透明边缘也算命中，减少菱形缝
                c = tc; return true;
            };
            // 岸线瓦片：与 clear 同套 NEAREST 采样；变体跟块哈希，避免岸线逐格跳变
            uint8_t sm = shoreMask[(size_t)ty * w + tx];
            if (sm) {
                int sv = (vh % 100 < 88) ? 0 : (int)((tHash(tx / 5, ty / 5, seed + 29) >> 4) & 1);
                if (shoreOk[sm][sv]) {
                    fromTile = sampleTileAt(tx, ty, shorePx[sm][sv], jx, jy)
                            || sampleTileAt(tx, ty, shorePx[sm][sv], sx, sy);
                }
                // 岸线 miss 时试邻格同 mask，与 clear 邻格补采样一致
                if (!fromTile) {
                    static const int nob[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                    for (auto& o : nob) {
                        int nx = tx + o[0], ny = ty + o[1];
                        if (!world.map.inBounds(nx, ny)) continue;
                        if (shoreMask[(size_t)ny * w + nx] != sm) continue;
                        if (shoreOk[sm][sv] && sampleTileAt(nx, ny, shorePx[sm][sv], sx, sy)) {
                            fromTile = true; break;
                        }
                    }
                }
            }
            if (!fromTile && tileOk[ti][tv]) {
                fromTile = sampleTileAt(tx, ty, tilePx[ti][tv], jx, jy)
                        || sampleTileAt(tx, ty, tilePx[ti][tv], sx, sy);
                // 菱形边缝：抖动采样 miss 时试邻格同类型瓦片
                if (!fromTile) {
                    static const int nob[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
                    for (auto& o : nob) {
                        int nx = tx + o[0], ny = ty + o[1];
                        if (!world.map.inBounds(nx, ny)) continue;
                        if (world.map.at(nx, ny).terrain != t) continue;
                        if (sampleTileAt(nx, ny, tilePx[ti][tv], sx, sy)) { fromTile = true; break; }
                    }
                }
            }
            if (!fromTile) {
            // 连续噪声（回退配色用；真实瓦片采样成功时无需计算，省 ~40% 烘焙耗时）
            float nBig = tNoise(sx, sy, 56, seed);
            float nMid = tNoise(sx, sy, 15, seed + 5);
            float n = nBig * 0.60f + nMid * 0.40f;
            float grain = ((float)(tHash(bx, by, seed + 11) % 256) / 255.0f - 0.5f) * 8.0f;
            switch (t) {
                case Terrain::Clear: {
                    // 草地：黄绿基调 + 大斑块；低频噪声处掺入泥土斑（RA2 地图常见的裸地）
                    float patch = tNoise(sx, sy, 90, seed + 23);
                    float dirt = std::clamp((patch - 0.60f) / 0.28f, 0.0f, 1.0f);
                    dirt = dirt * dirt * (3.0f - 2.0f * dirt);
                    c = Color{lerp8(58, 102, n), lerp8(94, 146, n), lerp8(42, 68, n), 255};
                    if (nMid > 0.86f && nBig > 0.45f) { c.r = 116; c.g = 120; c.b = 56; } // 枯草点
                    if (dirt > 0.0f) {
                        Color d{lerp8(118, 154, nMid), lerp8(96, 128, nMid), lerp8(58, 84, nMid), 255};
                        c.r = lerp8(c.r, d.r, dirt); c.g = lerp8(c.g, d.g, dirt); c.b = lerp8(c.b, d.b, dirt);
                    }
                    break;
                }
                case Terrain::Rough: {
                    c = Color{lerp8(122, 158, n), lerp8(100, 134, n), lerp8(62, 90, n), 255};
                    if (nMid < 0.14f) { c.r -= 24; c.g -= 22; c.b -= 16; } // 碎石暗斑
                    break;
                }
                case Terrain::Water: {
                    // 深水 + 横向波光条带（连续波形跨瓦片）
                    float wv = sinf((sy + nMid * 9.0f) * 0.55f) * 0.5f + 0.5f;
                    float m = n * 0.62f + wv * 0.38f;
                    c = Color{lerp8(16, 38, m), lerp8(50, 96, m), lerp8(96, 158, m), 255};
                    if (wv > 0.88f && nMid > 0.55f) { c.r += 20; c.g += 24; c.b += 24; } // 波峰高光
                    break;
                }
                case Terrain::Ore:
                case Terrain::Gems: {
                    // 矿格下垫采空土地（矿脉瓦片动态绘制在上层；采空后地形变 Rough 与之一致）
                    c = Color{lerp8(96, 126, n), lerp8(76, 104, n), lerp8(46, 68, n), 255};
                    break;
                }
                case Terrain::Bridge: {
                    float plank = (float)(((int)sy / 4) % 2) * 0.10f;
                    float v = 0.72f + n * 0.38f - plank;
                    c = Color{(uint8_t)(148 * v), (uint8_t)(100 * v), (uint8_t)(56 * v), 255};
                    if ((int)sy % 4 == 0) { c.r = (uint8_t)(c.r * 0.55f); c.g = (uint8_t)(c.g * 0.55f); c.b = (uint8_t)(c.b * 0.55f); }
                    break;
                }
            }
            c.r = (uint8_t)clampi(c.r + (int)grain, 0, 255);
            c.g = (uint8_t)clampi(c.g + (int)grain, 0, 255);
            c.b = (uint8_t)clampi(c.b + (int)grain, 0, 255);
            } // !fromTile 回退配色（真实瓦片像素自带纹理，不再加颗粒噪）
            // 直写像素数组：循环边界已保证不越界；且避免 set()/get() 内联函数的
            // 分支模式在同循环读写同一缓冲区时触发 MSVC /O2 自动向量化错误（曾致一半写入丢失）
            pb.px[(size_t)by * bw + bx] = c;
        }
    }
    // 不做跨格糊边：RA2 靠 TMP/岸线/变体一体感，强糊只会脏且仍露格子
    // （缝隙靠邻格采样与岸线模板；见 gen_terrain.py）
    if (terrainTex.id) UnloadTexture(terrainTex);
    terrainTex = pb.toTexture();
    SetTextureFilter(terrainTex, TEXTURE_FILTER_POINT); // 点采样 = 原作锐利瓦片，非柔糊放大
    fogMaskTick = -1; // 迷雾遮罩需按新地图重建
    TraceLog(LOG_INFO, "bakeTerrain: %dx%d px in %.1f ms", bw, bh, (GetTime() - bakeT0) * 1000.0);
}

// ---- 迷雾软遮罩：1/4 分辨率 alpha + 盒模糊（UNSEEN 255 / SEEN 112 / VISIBLE 0）----
void Game::bakeFogMask() {
    if (terrainW <= 0 || terrainH <= 0) return;
    const int FS = 8; // 遮罩缩小倍数（与 drawFogLayer 一致）
    int fw = terrainW / FS + 2, fh = terrainH / FS + 2;
    PixBuf pb(fw, fh); // 默认全 0 = 可见
    int w = world.map.w, h = world.map.h;
    for (int ty = 0; ty < h; ty++)
        for (int tx = 0; tx < w; tx++) {
            FogState fs = world.map.fogAt(localPlayer, tx, ty);
            if (fs == FOG_VISIBLE) continue;
            uint8_t a = fs == FOG_UNSEEN ? 255 : 170; // 已探索更暗，贴近原作 shroud 不透明度
            int px, py;
            tileToScreen(tx, ty, px, py);
            py -= heightScreenY(world.map.at(tx, ty).height);
            float mx = (px + terrainOX) / FS, my = (float)py / FS; // 菱形顶（遮罩域）
            // 填充菱形（世界 64x32 → 遮罩 8x4；略放大保证无漏缝；max 规则保证 UNSEEN 优先）
            for (int dy = -1; dy <= 5; dy++)
                for (int dx = -5; dx <= 5; dx++) {
                    if (fabsf(dx) / 5.0f + fabsf(dy - 2) / 3.0f > 1.0f) continue;
                    int mx2 = (int)mx + dx, my2 = (int)my + dy;
                    if (a >= pb.get(mx2, my2).a) pb.set(mx2, my2, Color{0, 0, 0, a});
                }
        }
    if (fogMaskTex.id) UnloadTexture(fogMaskTex);
    fogMaskTex = pb.toTexture();
    SetTextureFilter(fogMaskTex, TEXTURE_FILTER_POINT); // 点采样：更接近 RA2 瓦片迷雾边缘
}

void Game::drawWorld() {
    int viewW = SCREEN_W - sidebarW;
    float visW = (float)viewW / camZoom;
    float visH = (float)SCREEN_H / camZoom;
    // ---- 整图地表：1 次 draw（烘焙纹理源矩形裁剪到可视区；区外留黑底） ----
    if (terrainTex.id) {
        Rectangle src{(camX + terrainOX) * terrainSC, camY * terrainSC,
                      visW * terrainSC, visH * terrainSC};
        Rectangle dst{0, 0, visW, visH};
        const float inv = 1.0f / terrainSC;
        if (src.x < 0) { dst.x -= src.x * inv; dst.width += src.x * inv; src.width += src.x; src.x = 0; }
        if (src.y < 0) { dst.y -= src.y * inv; dst.height += src.y * inv; src.height += src.y; src.y = 0; }
        float maxX = terrainW * terrainSC, maxY = terrainH * terrainSC;
        if (src.x + src.width > maxX) { float over = src.x + src.width - maxX; src.width -= over; dst.width -= over * inv; }
        if (src.y + src.height > maxY) { float over = src.y + src.height - maxY; src.height -= over; dst.height -= over * inv; }
        if (src.width > 0 && src.height > 0)
            DrawTexturePro(terrainTex, src, dst, {0, 0}, 0, WHITE);
    }
    // ---- 矿脉/彩矿：动态瓦片（采空/再生会变化，不参与烘焙；烘焙层在矿格下垫了采空土地） ----
    int x0, y0, x1, y1;
    screenToTile(camX, camY - 64, x0, y0);
    screenToTile(camX + visW + 64, camY + visH + 64, x1, y1);
    int x2, y2, x3, y3;
    screenToTile(camX + visW + 64, camY - 128, x2, y2);
    screenToTile(camX - 64, camY + visH + 128, x3, y3);
    int minTX = std::min({x0, x1, x2, x3}) - 1, maxTX = std::max({x0, x1, x2, x3}) + 1;
    int minTY = std::min({y0, y1, y2, y3}) - 1, maxTY = std::max({y0, y1, y2, y3}) + 1;
    minTX = std::max(0, minTX); minTY = std::max(0, minTY);
    maxTX = std::min(world.map.w - 1, maxTX); maxTY = std::min(world.map.h - 1, maxTY);

    for (int ty = minTY; ty <= maxTY; ty++)
        for (int tx = minTX; tx <= maxTX; tx++) {
            const Cell& c = world.map.at(tx, ty);
            if (c.terrain != Terrain::Ore && c.terrain != Terrain::Gems) continue;
            int px, py;
            tileToScreen(tx, ty, px, py);
            py -= heightScreenY(c.height);
            int sx = px - camX, sy = py - camY;
            if (sx < -TILE_W || sx > visW + TILE_W || sy < -TILE_H || sy > visH + TILE_H) continue;
            const Sprite& s = g_sprites.tile(c.terrain, c.variant & 7);
            DrawTexture(s.tex, sx - TILE_W / 2, sy, WHITE);
        }
}

void Game::drawEntities() {
    int viewW = SCREEN_W - sidebarW;
    float visW = (float)viewW / camZoom;
    float visH = (float)SCREEN_H / camZoom;
    // 深度排序绘制项
    struct Item { float depth; int kind; int id; }; // kind 0 单位 1 建筑 2 树
    std::vector<Item> items;
    items.reserve(world.ents.size() + 512);
    for (size_t i = 0; i < world.ents.size(); i++) {
        const World::Ent& e = world.ents[i];
        if (!e.alive) continue;
        if (e.isBuilding) {
            const BldDef& d = bldDef(e.btype);
            // 深度用占地东南角：单位走在建筑南侧时压在贴图之上，避免「穿模被挡」
            items.push_back({e.x + (d.w - 1) + e.y + (d.h - 1) + 0.05f, 1, (int)i});
        } else {
            // 飞行单位始终最后绘制（浮于空中，压过地面一切）
            bool flying = unitDef(e.utype).isAir() && e.state != UState::Landed;
            items.push_back({e.x + e.y + (flying ? 1000.0f : 0.0f), 0, (int)i});
        }
    }
    // 树木：仅扫描可见瓦片范围（与 drawWorld 同边界，避免全图扫描）
    {
        int x0, y0, x1, y1, x2, y2, x3, y3;
        screenToTile(camX, camY - 64, x0, y0);
        screenToTile(camX + visW + 64, camY + visH + 64, x1, y1);
        screenToTile(camX + visW + 64, camY - 128, x2, y2);
        screenToTile(camX - 64, camY + visH + 128, x3, y3);
        int minTX = std::max(0, std::min({x0, x1, x2, x3}) - 1);
        int maxTX = std::min(world.map.w - 1, std::max({x0, x1, x2, x3}) + 1);
        int minTY = std::max(0, std::min({y0, y1, y2, y3}) - 1);
        int maxTY = std::min(world.map.h - 1, std::max({y0, y1, y2, y3}) + 1);
        for (int ty = minTY; ty <= maxTY; ty++)
            for (int tx = minTX; tx <= maxTX; tx++) {
                const Cell& c = world.map.at(tx, ty);
                if (c.overlay >= Overlay::Tree1 && c.overlay <= Overlay::Tree3) {
                    int px, py;
                    tileToScreen(tx, ty, px, py);
                    py -= heightScreenY(c.height);
                    int sx = px - camX, sy = py - camY;
                    if (sx < -64 || sx > visW + 64 || sy < -96 || sy > visH + 64) continue;
                    items.push_back({(float)(tx + ty) + 0.9f, 2, ty * world.map.w + tx});
                }
            }
    }
    // 补给箱（地面道具，参与深度排序）
    for (size_t i = 0; i < world.crates.size(); i++) {
        const World::Crate& c = world.crates[i];
        if (!c.alive) continue;
        if (world.map.fogAt(localPlayer, c.x, c.y) != FOG_VISIBLE) continue;
        items.push_back({(float)(c.x + c.y) + 0.5f, 3, (int)i});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });
    // 选择集 O(1) 查找
    std::unordered_set<EID> selSet(sel.begin(), sel.end());

    for (const Item& it : items) {
        if (it.kind == 3) {
            // RA2 补给箱：theater SHP（crate.tem）提取的 overlay_crate.png
            const World::Crate& c = world.crates[it.id];
            int px, py;
            tileToScreen(c.x, c.y, px, py);
            if (world.map.inBounds(c.x, c.y))
                py -= heightScreenY(world.map.at(c.x, c.y).height);
            const Sprite& cs = g_sprites.crateSpr();
            int sx = px - (int)camX - cs.ox;
            int sy = py - (int)camY + TILE_H / 2 - cs.oy;
            DrawTexture(cs.tex, sx, sy, WHITE);
            continue;
        }
        if (it.kind == 2) {
            int tx = it.id % world.map.w, ty = it.id / world.map.w;
            const Cell& c = world.map.at(tx, ty);
            FogState fs = world.map.fogAt(localPlayer, tx, ty);
            if (fs == FOG_UNSEEN) continue;
            int px, py;
            tileToScreen(tx, ty, px, py);
            py -= heightScreenY(c.height);
            const Sprite& s = g_sprites.overlaySpr(c.overlay);
            Color tint = fs == FOG_SEEN ? Color{140, 140, 140, 255} : WHITE;
            DrawTexture(s.tex, px - (int)camX - s.ox, py + TILE_H / 2 - (int)camY - s.oy, tint);
            continue;
        }
        const World::Ent& e = world.ents[it.id];
        if (!e.isBuilding) {
            const UnitDef& ud = unitDef(e.utype);
            // 恐怖机器人钻入车底后隐藏（逻辑仍附着）
            if (e.parasiting) continue;
            bool flying = ud.isAir() && e.state != UState::Landed;
            FogState fs = world.map.fogAt(localPlayer, (int)e.x, (int)e.y);
            if (e.player != localPlayer && fs != FOG_VISIBLE) continue;
            Vector2 p = unitScreenPos(e);
            if (p.x < -80 || p.x > visW + 80 || p.y < -80 || p.y > visH + 80) continue;
            int cid = world.players[e.player].colorId;
            // RA2 式阴影：地面单位贴图已含烘焙投影，勿再叠运行时椭圆（否则坦克/矿车发黑）
            if (flying) {
                DrawEllipse((int)p.x, (int)p.y + 4, 10, 4, Color{0, 0, 0, 20});
                DrawEllipse((int)p.x, (int)p.y + 4, 6, 2, Color{0, 0, 0, 40});
            }
            if (flying) p.y -= AIR_ALT;
            // 幻影坦克伪装：敌方视角画成树（己方仍见车体）
            if (e.camouflaged && e.player != localPlayer) {
                Overlay tree = (Overlay)(1 + (it.id % 3)); // Tree1..Tree3
                const Sprite& ts = g_sprites.overlaySpr(tree);
                DrawTexture(ts.tex, (int)p.x - ts.ox, (int)p.y - ts.oy, WHITE);
                continue;
            }
            // 间谍伪装：已手动伪装则画成目标兵种+敌方色；未伪装画本体
            UnitType drawType = e.utype;
            int drawCid = cid;
            if (e.utype == UnitType::Spy && e.camouflaged) {
                int dt = e.camoTick & 0xFFFF;
                if (dt >= 0 && dt < (int)UnitType::COUNT && unitDef((UnitType)dt).isInfantry()) {
                    drawType = (UnitType)dt;
                    drawCid = (e.camoTick >> 16) & 0xFF;
                }
            }
            // 被炸/寄生掀起：左右交替抬高一侧
            float rockDeg = 0.f;
            if (e.rockTilt > 0 && !flying) {
                int side = (e.rockTilt / 6) & 1;
                rockDeg = side ? 12.f : -12.f;
                p.y -= 4;
            }
            // 动画状态机选择（art.ini 序列）：开火 > 行走 > 部署站姿 > 站立
            const UnitAnimInfo& ai = g_sprites.animInfo(drawType);
            const Sprite* bodyP;
            // 超时空军团兵：攻击时持续开火帧；相位不适时闪烁
            bool chronoHoldFire = (drawType == UnitType::Chrono && e.state == UState::Attacking && ai.fire > 0);
            if ((e.fireAnim > 0 || chronoHoldFire) && ai.fire > 0) {
                int phase;
                if (chronoHoldFire)
                    phase = ((int)world.tick / 2) % ai.fire;
                else
                    phase = (ai.fire * 2 - e.fireAnim) / 2; // 0..fire-1
                if (phase < 0) phase = 0;
                if (phase >= ai.fire) phase = ai.fire - 1;
                bodyP = &g_sprites.unitAnim(drawType, UAnim::Fire, e.dir, phase, drawCid);
            } else if ((unitDef(drawType).isInfantry() || drawType == UnitType::TerrorDrone) && ai.walk > 0
                       && !e.path.empty() && e.pathIdx < (int)e.path.size()
                       && e.state != UState::Idle && e.state != UState::Attacking && e.state != UState::Landed
                       && drawType != UnitType::Chrono) { // 超时空军团兵传送移动，不用行走帧
                bodyP = &g_sprites.unitAnim(drawType, UAnim::Walk, e.dir, e.walkFrame, drawCid);
            } else if (e.deployed && ai.dep) {
                bodyP = &g_sprites.unitAnim(drawType, UAnim::Dep, e.dir, 0, drawCid);
            } else if (ud.canHarvet() && e.state == UState::HarvestUnload) {
                bodyP = &g_sprites.unitUnload(drawType, e.dir, drawCid);
            } else {
                // 站立：步兵/载具站立帧恒 f0；矿车 f0/f1=空/满（勿用 walkFrame，否则缺 f1 会掉回程序占位图）
                int stf = ud.canHarvet()
                    ? ((e.oreLoad >= World::harvesterCapacity(e.utype)) ? 1 : 0)
                    : 0;
                bodyP = &g_sprites.unitBody(drawType, e.dir, stf, drawCid);
            }
            const Sprite& body = *bodyP;
            Color tint = (e.player != localPlayer && fs == FOG_SEEN) ? Color{120, 120, 120, 255} : WHITE;
            // 超时空相位不适：半透明蓝闪
            if (e.tpSick > 0 && (drawType == UnitType::Chrono || drawType == UnitType::ChronoCommando
                                 || drawType == UnitType::ChronoIvan)) {
                bool flash = ((world.tick / 4) % 2) != 0;
                tint = flash ? Color{140, 200, 255, 200} : Color{180, 220, 255, 160};
            }
            int drawX = (int)p.x - body.ox;
            int drawY = (int)p.y - body.oy;
            if (rockDeg != 0.f) {
                Rectangle src{0, 0, (float)body.tex.width, (float)body.tex.height};
                Rectangle dst{(float)p.x, (float)p.y, (float)body.tex.width, (float)body.tex.height};
                Vector2 origin{(float)body.ox, (float)body.oy};
                DrawTexturePro(body.tex, src, dst, origin, rockDeg, tint);
            } else {
                DrawTexture(body.tex, drawX, drawY, tint);
            }
            if (g_sprites.hasTurret(drawType)) {
                const Sprite& tur = g_sprites.unitTurret(drawType, e.turretDir, drawCid);
                if (rockDeg != 0.f) {
                    Rectangle src{0, 0, (float)tur.tex.width, (float)tur.tex.height};
                    Rectangle dst{(float)p.x, (float)p.y, (float)tur.tex.width, (float)tur.tex.height};
                    Vector2 origin{(float)tur.ox, (float)tur.oy};
                    DrawTexturePro(tur.tex, src, dst, origin, rockDeg, tint);
                } else {
                    DrawTexture(tur.tex, drawX + (body.ox - tur.ox), drawY + (body.oy - tur.oy), tint);
                }
            }
            // RA2：单位选中显示血条 + 角标（叠层在 zoom 外 flush，避免与贴图错位）
            bool selected = selSet.count(it.id) > 0;
            int boxX = drawX, boxY = drawY, boxW = body.tex.width, boxH = body.tex.height;
            // 用不透明包围盒收紧血条/角标（与 unitScreenRect 一致）
            if (body.visR > body.visL && body.visB > body.visT) {
                boxX = drawX + body.visL;
                boxY = drawY + body.visT;
                boxW = body.visW();
                boxH = body.visH();
            } else if (!ud.isAir() && !ud.isNaval()) {
                // 回退：finishUnitSprite 6/4 阴影边距
                boxX += 6; boxY += 4; boxW -= 12; boxH -= 8;
                if (boxW < 8) { boxX = drawX; boxW = body.tex.width; }
                if (boxH < 8) { boxY = drawY; boxH = body.tex.height; }
            }
            if (selected || e.hp < ud.hp)
                drawHealthBar(boxX, boxY - 6, std::max(28, boxW), (float)e.hp / std::max(1, ud.hp), selected);
            if (selected) {
                const float L = (float)std::min(10, std::min(std::max(boxW, 1), std::max(boxH, 1)) * 35 / 100);
                const float th = 2.f;
                Color sc{40, 255, 70, 230};
                fillRectWorld((float)boxX, (float)boxY, L, th, sc);
                fillRectWorld((float)boxX, (float)boxY, th, L, sc);
                fillRectWorld((float)boxX + boxW - L, (float)boxY, L, th, sc);
                fillRectWorld((float)boxX + boxW - th, (float)boxY, th, L, sc);
                fillRectWorld((float)boxX, (float)boxY + boxH - th, L, th, sc);
                fillRectWorld((float)boxX, (float)boxY + boxH - L, th, L, sc);
                fillRectWorld((float)boxX + boxW - L, (float)boxY + boxH - th, L, th, sc);
                fillRectWorld((float)boxX + boxW - th, (float)boxY + boxH - L, th, L, sc);
            }
            // 军衔标志（RA2：老兵银三角 / 精英金三角，右下角垂直向上累加）
            if (e.vetRank > 0) {
                Color rc = e.vetRank >= 2 ? Color{255, 200, 60, 255} : Color{220, 220, 220, 255};
                for (int i = 0; i < e.vetRank; i++) {
                    int vx = boxX + boxW - 8;
                    int vy = boxY + boxH - 2 - i * 5;
                    fillRectWorld((float)vx + 2, (float)vy, 2, 1, rc);
                    fillRectWorld((float)vx + 1, (float)vy + 1, 4, 1, rc);
                    fillRectWorld((float)vx, (float)vy + 2, 6, 2, rc);
                }
            }
            // 战机弹药指示
            if (selected && ud.ammo > 0) {
                for (int i = 0; i < ud.ammo; i++) {
                    Color ac = i < e.ammo ? Color{255, 200, 60, 255} : Color{70, 70, 74, 255};
                    fillRectWorld((float)(boxX + i * 7), (float)(boxY - 10), 5, 3, ac);
                }
            }
            // 铁幕无敌：暗化罩光
            if (e.invuln > 0) {
                int cx = boxX + boxW / 2, cy = boxY + boxH / 2;
                DrawEllipse(cx, cy, boxW / 2, boxH / 3, Color{30, 26, 30, 110});
                DrawEllipseLines(cx, cy, boxW / 2 + 1, boxH / 3 + 1,
                    ((world.tick / 6) % 2) ? Color{200, 60, 50, 160} : Color{120, 30, 26, 160});
            }
        } else {
            FogState fs = world.map.fogAt(localPlayer, (int)e.x, (int)e.y);
            if (e.player != localPlayer && fs == FOG_UNSEEN) continue;
            Vector2 p = bldScreenPos(e);
            // 占领后阵营染色；中立科技保留原色（cid=-2）
            int cid;
            Country bldCountry = Country::None;
            if (e.player >= 0) {
                cid = world.players[e.player].colorId;
                bldCountry = world.players[e.player].country;
            } else if (e.btype == BldType::OilDerrick || e.btype == BldType::Hospital
                     || e.btype == BldType::TechAirport)
                cid = -2;
            else
                cid = -1;
            // 建造/出售动画：mk 关键帧序列；出售时倒放
            const Sprite* sp;
            int mkf = g_sprites.bldMkFrames(e.btype);
            if (e.constructAnim > 0 && mkf > 1) {
                int total = mkf * 5;
                // 建造：f0 地基 → f(mkf-1) 成型；出售：反向同序列
                int frame = e.selling
                    ? (e.constructAnim - 1) / 5
                    : (total - e.constructAnim) / 5;
                if (frame < 0) frame = 0;
                if (frame >= mkf) frame = mkf - 1;
                sp = &g_sprites.buildingMk(e.btype, frame, cid, bldCountry);
            } else {
                sp = &g_sprites.building(e.btype, cid, false, bldCountry);
            }
            const Sprite& s = *sp;
            Color tint = (e.player != localPlayer && fs == FOG_SEEN) ? Color{110, 110, 110, 255} : WHITE;
            // 出售：仅倒放 Buildup(mk) 帧（RA2 原作），不加程序化闪烁
            // 破损民房：焦黑+橙红燃烧感
            if (e.btype == BldType::CivHouse && e.hp * 2 <= bldDef(e.btype).hp) {
                bool flicker = ((world.tick / 4 + (int)it.id) % 2) != 0;
                tint = flicker ? Color{200, 110, 70, 255} : Color{90, 70, 60, 255};
                if (e.player != localPlayer && fs == FOG_SEEN)
                    tint = Color{70, 55, 48, 255};
            }
            DrawTexture(s.tex, (int)p.x - s.ox, (int)p.y - s.oy, tint);
            // ActiveAnim：油井/医院泵机与灯火循环（CAOILD_A / CAHOSP_A）
            if (e.constructAnim <= 0 && !e.selling && (e.btype == BldType::OilDerrick || e.btype == BldType::Hospital)) {
                static Texture2D oilA[128]{}, hospA[8]{};
                static int oilAN = -1, hospAN = -1;
                if (oilAN < 0) {
                    oilAN = 0;
                    for (int fi = 0; fi < 128; fi++) {
                        const char* path = TextFormat("assets/sprites/bld_oilderrick_a_f%d.png", fi);
                        if (!FileExists(path)) break;
                        Image im = LoadImage(path);
                        if (!im.data) break;
                        oilA[fi] = LoadTextureFromImage(im);
                        SetTextureFilter(oilA[fi], TEXTURE_FILTER_POINT);
                        UnloadImage(im);
                        oilAN = fi + 1;
                    }
                }
                if (hospAN < 0) {
                    hospAN = 0;
                    for (int fi = 0; fi < 8; fi++) {
                        const char* path = TextFormat("assets/sprites/bld_hospital_a_f%d.png", fi);
                        if (!FileExists(path)) break;
                        Image im = LoadImage(path);
                        if (!im.data) break;
                        hospA[fi] = LoadTextureFromImage(im);
                        SetTextureFilter(hospA[fi], TEXTURE_FILTER_POINT);
                        UnloadImage(im);
                        hospAN = fi + 1;
                    }
                }
                Texture2D* anim = nullptr; int animN = 0; int rate = 2;
                if (e.btype == BldType::OilDerrick && oilAN > 0) { anim = oilA; animN = oilAN; rate = 2; }
                if (e.btype == BldType::Hospital && hospAN > 0) { anim = hospA; animN = hospAN; rate = 4; }
                if (anim && animN > 0) {
                    int fi = ((int)world.tick / rate + (int)it.id * 3) % animN;
                    if (anim[fi].id) {
                        // 主体经 finishBldSprite 加了 (6,4) 阴影边；ActiveAnim PNG 是内容画布，需对齐内容原点
                        int ax = (int)p.x - s.ox + 6;
                        int ay = (int)p.y - s.oy + 4;
                        DrawTexture(anim[fi], ax, ay, tint);
                    }
                }
            }
            const BldDef& d = bldDef(e.btype);
            const bool bldSelected = ((int)it.id == selBuilding);
            // 选中笼：底面/高度/偏移按贴图拟合（fit_bld_cages.py）；stem 与阵营贴图一致。
            const Sprite& cageS = g_sprites.building(e.btype, cid, false, bldCountry);
            auto dashLine = [](Vector2 a, Vector2 b, Color c, float thick = 1.75f) {
                float dx = b.x - a.x, dy = b.y - a.y;
                float len = sqrtf(dx * dx + dy * dy);
                if (len < 1.0f) return;
                if (len < 14.0f) {
                    DrawLineEx(a, b, thick, c);
                    return;
                }
                dx /= len; dy /= len;
                const float dash = 5.0f, gap = 3.0f;
                for (float t = 0; t < len; t += dash + gap) {
                    float t1 = t, t2 = std::min(len, t + dash);
                    DrawLineEx({a.x + dx * t1, a.y + dy * t1}, {a.x + dx * t2, a.y + dy * t2}, thick, c);
                }
            };
            auto drawIsoCuboid = [&](Vector2 bs, const BldCageParams& cage, Color edge) {
                float elev = std::clamp(cage.elev, 8.0f, 400.0f);
                Vector2 bn, be, bw;
                footCorners(bs, cage, bn, be, bw);
                Vector2 tn{bn.x, bn.y - elev}, te{be.x, be.y - elev};
                Vector2 ts{bs.x, bs.y - elev}, tw{bw.x, bw.y - elev};
                Color fill{edge.r, edge.g, edge.b, (unsigned char)22};
                auto quad = [&](Vector2 a, Vector2 b, Vector2 c, Vector2 d) {
                    DrawTriangle(a, b, c, fill);
                    DrawTriangle(a, c, d, fill);
                };
                quad(bw, bs, ts, tw);
                quad(bs, be, te, ts);
                dashLine(bn, be, edge); dashLine(be, bs, edge);
                dashLine(bs, bw, edge); dashLine(bw, bn, edge);
                dashLine(tn, te, edge); dashLine(te, ts, edge);
                dashLine(ts, tw, edge); dashLine(tw, tn, edge);
                const float vt = 2.25f;
                DrawLineEx(bn, tn, vt, edge);
                DrawLineEx(be, te, vt, edge);
                DrawLineEx(bs, ts, vt, edge);
                DrawLineEx(bw, tw, vt, edge);
                if (visualAuditMarkers) {
                    DrawCircleV(bs, 3.0f, Color{255, 0, 255, 255});
                    DrawCircleV(bn, 3.0f, Color{0, 255, 255, 255});
                    DrawCircleV(be, 3.0f, Color{0, 200, 255, 255});
                    DrawCircleV(ts, 3.0f, Color{0, 255, 0, 255});
                }
            };
            const char* cageStem = SpriteBank::bldSpriteStem(e.btype, bldCountry);
            BldCageParams cage = resolveBldCage(cageStem, d.w, d.h, cageS);
            Vector2 cageP{p.x + cage.offX, p.y + cage.offY};
            if (bldSelected)
                drawIsoCuboid(cageP, cage, Color{255, 240, 60, 245});
            // 血条贴在贴图不透明顶上方（不用 cage.elev：矮笼改完后条会沉进楼体）
            const int bldPips = 20;
            int barW = bldPips * 3 + (bldPips - 1) * 1;
            int visCx = (int)(p.x - (float)cageS.ox + 0.5f * (float)(cageS.visL + cageS.visR + 1));
            int barX = visCx - barW / 2;
            int barY = (int)(p.y - (float)cageS.visElev()) - 6;
            if (bldSelected || e.hp < d.hp) {
                drawHealthBar(barX, barY, barW, (float)e.hp / std::max(1, d.hp), bldSelected, bldPips);
            }
            // 持续维修：叠绘 RA2 cache.mix/wrench.shp（非程序黄块）
            if (e.repairing && e.player == localPlayer) {
                static Texture2D wrench[8]{};
                static int wrenchN = -1;
                if (wrenchN < 0) {
                    wrenchN = 0;
                    for (int fi = 0; fi < 8; fi++) {
                        const char* path = TextFormat("assets/gui/wrench/wrench_%02d.png", fi);
                        if (!FileExists(path)) break;
                        Image im = LoadImage(path);
                        if (!im.data) break;
                        wrench[fi] = LoadTextureFromImage(im);
                        SetTextureFilter(wrench[fi], TEXTURE_FILTER_POINT);
                        UnloadImage(im);
                        wrenchN = fi + 1;
                    }
                }
                if (wrenchN > 0) {
                    int fi = ((int)world.tick / 4) % wrenchN;
                    Texture2D& wt = wrench[fi];
                    if (wt.id) {
                        int wx = (int)p.x - wt.width / 2;
                        int wy = barY - wt.height - 2;
                        DrawTexture(wt, wx, wy, WHITE);
                    }
                }
            }
            // 集结点：生产建筑选中时画连线 + 旗杆/旗面（比三角更接近原作旗标）
            if ((int)it.id == selBuilding && isRallyBuilding(e.btype) && e.rallyX >= 0) {
                int rx, ry;
                tileToScreen(e.rallyX, e.rallyY, rx, ry);
                int rsx = rx - (int)camX, rsy = ry - (int)camY + TILE_H / 2;
                DrawLine((int)p.x, (int)p.y, rsx, rsy, Color{40, 220, 80, 160});
                DrawRectangle(rsx - 1, rsy - 16, 2, 16, Color{180, 190, 170, 255}); // 旗杆
                DrawTriangle({(float)rsx + 1, (float)(rsy - 16)}, {(float)(rsx + 12), (float)(rsy - 11)},
                             {(float)rsx + 1, (float)(rsy - 6)}, Color{40, 220, 80, 230});
                DrawCircle(rsx, rsy, 3, Color{0, 255, 0, 160});
            }
            // 驻军槽：选中可进驻建筑时显示空位框 + 占位兵种剪影（贴近原作 pip 条）
            if ((int)it.id == selBuilding && e.player == localPlayer) {
                const BldDef& gd = bldDef(e.btype);
                if (gd.garrisonCap > 0) {
                    int slots = gd.garrisonCap;
                    int filled = (int)e.garrison.size();
                    int iconW = 12, iconH = 14, gap = 2;
                    int rowW = slots * iconW + (slots - 1) * gap;
                    int pipX0 = (int)p.x - rowW / 2;
                    int pipY0 = (int)p.y + 6;
                    for (int i = 0; i < slots; i++) {
                        int ix = pipX0 + i * (iconW + gap);
                        Rectangle rr{(float)ix, (float)pipY0, (float)iconW, (float)iconH};
                        DrawRectangleRec(rr, Color{12, 14, 16, 210});
                        DrawRectangleLinesEx(rr, 1, Color{70, 120, 70, 220});
                        if (i < filled) {
                            // 占位：头盔+身躯剪影（非纯绿点）
                            DrawRectangle(ix + 3, pipY0 + 2, 6, 4, Color{200, 210, 180, 255});
                            DrawRectangle(ix + 2, pipY0 + 6, 8, 6, Color{90, 140, 70, 255});
                            DrawRectangle(ix + 4, pipY0 + 12, 4, 1, Color{60, 80, 50, 255});
                        }
                    }
                }
            }
            // 铁幕无敌建筑：暗化 + 红闪描边
            if (e.invuln > 0) {
                DrawRectangle((int)p.x - s.ox, (int)p.y - s.oy, s.tex.width, s.tex.height, Color{20, 16, 20, 90});
                DrawRectangleLines((int)p.x - s.ox, (int)p.y - s.oy, s.tex.width, s.tex.height,
                    ((world.tick / 6) % 2) ? Color{200, 60, 50, 150} : Color{110, 30, 26, 150});
            }
        }
    }

    // 弹道
    for (const Projectile& pr : world.projs) {
        // 瓦片浮点 → 世界像素
        float fx1 = (pr.x - pr.y) * (TILE_W / 2.0f);
        float fy1 = (pr.x + pr.y) * (TILE_H / 2.0f);
        int sx = (int)fx1 - (int)camX, sy = (int)fy1 - (int)camY - 6;
        if (pr.kind == ProjKind::Bullet) {
            DrawCircle(sx, sy, 2, Color{255, 230, 150, 255});
        } else if (pr.kind == ProjKind::Missile) {
            int dir = dirFromVec(pr.tx - pr.x, pr.ty - pr.y);
            const Sprite& s = g_sprites.projectile(1, dir);
            DrawTexture(s.tex, sx - s.ox, sy - s.oy, WHITE);
        } else if (pr.kind == ProjKind::Flak) {
            DrawCircle(sx, sy, 3, Color{160, 160, 170, 255});
        } else {
            const Sprite& s = g_sprites.projectile(0, 0);
            DrawTexture(s.tex, sx - s.ox, sy - s.oy, WHITE);
        }
    }

    // 选中单位移动路径连线（当前 A* 路径）+ 路径点队列（Z 模式）
    for (EID id : sel) {
        if (!world.valid(id)) continue;
        const World::Ent& e = world.ents[id];
        if (e.isBuilding) continue;
        Vector2 prev = unitScreenPos(e);
        // 普通移动：单位 → 剩余 path 格中心
        if (!e.path.empty() && e.pathIdx < (int)e.path.size()) {
            int drawn = 0;
            for (int pi = e.pathIdx; pi < (int)e.path.size() && drawn < 24; ++pi, ++drawn) {
                const Vec2i& cell = e.path[pi];
                float wx = (cell.x - cell.y) * (TILE_W / 2.0f) + TILE_W / 4.0f - camX;
                float wy = (cell.x + cell.y) * (TILE_H / 2.0f) + TILE_H / 4.0f - camY;
                DrawLine((int)prev.x, (int)prev.y, (int)wx, (int)wy, Color{80, 220, 255, 160});
                prev = {wx, wy};
            }
            DrawCircle((int)prev.x, (int)prev.y, 3.0f, Color{120, 240, 255, 200});
        }
        // Z 模式路径点：绿线串联 + 菱形节点
        if (e.wps.empty()) continue;
        prev = unitScreenPos(e);
        int i = 0;
        for (auto& w : e.wps) {
            float wx = (w.first - w.second) * (TILE_W / 2.0f) - camX;
            float wy = (w.first + w.second) * (TILE_H / 2.0f) - camY;
            DrawLine((int)prev.x, (int)prev.y, (int)wx, (int)wy, Color{80, 255, 80, 170});
            DrawTriangle({wx, wy - 5}, {wx + 5, wy}, {wx, wy + 5}, Color{60, 230, 60, 220});
            DrawTriangle({wx, wy - 5}, {wx - 5, wy}, {wx, wy + 5}, Color{60, 230, 60, 220});
            DrawCircle((int)wx, (int)wy, 1.5f, Color{220, 255, 220, 255});
            prev = {wx, wy};
            if (++i >= 12) break;
        }
    }

    // 核弹飞行：目标点准星 + 从天而降的弹体
    for (const Nuke& n : world.nukes) {
        if (!n.active) continue;
        float fx1 = (n.tx - n.ty) * (TILE_W / 2.0f);
        float fy1 = (n.tx + n.ty) * (TILE_H / 2.0f);
        int sx = (int)fx1 - (int)camX, sy = (int)fy1 - (int)camY;
        // 落点闪烁准星
        Color mc = (world.tick / 4) % 2 ? Color{255, 60, 50, 220} : Color{255, 200, 60, 220};
        DrawEllipseLines(sx, sy, 14, 7, mc);
        DrawLine(sx - 18, sy, sx - 6, sy, mc);
        DrawLine(sx + 6, sy, sx + 18, sy, mc);
        DrawLine(sx, sy - 10, sx, sy - 3, mc);
        DrawLine(sx, sy + 3, sx, sy + 10, mc);
        // 弹体：随时间从高空落下
        float prog = 1.0f - n.timer / 75.0f;
        int dropY = sy - (int)((1.0f - prog) * 420) - 24;
        // 尾焰
        DrawLine(sx, dropY - 16, sx, dropY - 40 - (int)(prog * 30), Color{255, 200, 90, 180});
        DrawLine(sx, dropY - 16, sx, dropY - 34 - (int)(prog * 26), Color{255, 240, 200, 220});
        // 弹体（白身红头）
        DrawLine(sx - 1, dropY - 16, sx - 1, dropY - 2, Color{210, 208, 200, 255});
        DrawLine(sx, dropY - 18, sx, dropY, Color{235, 232, 224, 255});
        DrawLine(sx + 1, dropY - 16, sx + 1, dropY - 2, Color{190, 188, 180, 255});
        DrawLine(sx, dropY, sx, dropY + 3, Color{220, 60, 50, 255});
    }

    // 疯狂伊文定时炸弹：闪烁红点 + 倒计时弧线
    for (const World::TimedBomb& b : world.timedBombs) {
        float fx = b.x, fy = b.y;
        if (world.valid(b.attachedTo)) {
            const World::Ent& t = world.ents[b.attachedTo];
            if (!t.alive) continue;
            fx = t.x; fy = t.y;
            if (t.isBuilding) { fx += bldDef(t.btype).w / 2.0f; fy += bldDef(t.btype).h / 2.0f; }
        }
        if (world.map.fogAt(localPlayer, (int)fx, (int)fy) != FOG_VISIBLE) continue;
        float wx = (fx - fy) * (TILE_W / 2.0f), wy = (fx + fy) * (TILE_H / 2.0f);
        int sx = (int)wx - (int)camX, sy = (int)wy - (int)camY - 14;
        DrawRectangle(sx - 3, sy - 3, 7, 7, Color{60, 40, 36, 255});
        DrawRectangleLines(sx - 3, sy - 3, 7, 7, Color{120, 60, 50, 255});
        if ((world.tick / 5) % 2) DrawCircle(sx, sy, 2, Color{255, 60, 50, 255});
    }
}

void Game::drawEffectsLayer() {
    for (const Effect& ef : world.effects) {
        auto toPx = [&](float tx, float ty, int& sx, int& sy) {
            float pxx = (tx - ty) * (TILE_W / 2.0f);
            float pyy = (tx + ty) * (TILE_H / 2.0f);
            sx = (int)pxx - (int)camX;
            sy = (int)pyy - (int)camY;
        };
        if (ef.kind == 0 || ef.kind == 4) {
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            int frame = ef.age * SpriteBank::EXPLOSION_FRAMES / ef.maxAge;
            const Sprite& s = g_sprites.explosion(frame);
            if (ef.kind == 4) {
                DrawTextureEx(s.tex, {(float)(sx - s.ox * 2), (float)(sy - s.oy * 2)}, 0, 2.0f, WHITE);
            } else {
                DrawTexture(s.tex, sx - s.ox, sy - s.oy - 8, WHITE);
            }
        } else if (ef.kind == 2) {
            // 磁暴电弧
            int sx, sy, tx2, ty2;
            toPx(ef.x, ef.y, sx, sy);
            toPx(ef.x2, ef.y2, tx2, ty2);
            sy -= 14; ty2 -= 10;
            float dx = (float)(tx2 - sx), dy = (float)(ty2 - sy);
            float len = sqrtf(dx * dx + dy * dy);
            int segs = (int)(len / 6) + 1;
            Vector2 prev{(float)sx, (float)sy};
            for (int i = 1; i <= segs; i++) {
                float t = (float)i / segs;
                float jx = 0, jy = 0;
                if (i < segs) {
                    jx = (float)((int)(ef.age * 37 + i * 91) % 11 - 5);
                    jy = (float)((int)(ef.age * 53 + i * 71) % 11 - 5);
                }
                Vector2 cur{sx + dx * t + jx, sy + dy * t + jy};
                DrawLineEx(prev, cur, 2.5f, Color{140, 180, 255, 220});
                DrawLineEx(prev, cur, 1.0f, Color{230, 245, 255, 255});
                prev = cur;
            }
        } else if (ef.kind == 3) {
            int sx, sy, tx2, ty2;
            toPx(ef.x, ef.y, sx, sy);
            toPx(ef.x2, ef.y2, tx2, ty2);
            sy -= 16; ty2 -= 10;
            float a = 1.0f - (float)ef.age / ef.maxAge;
            Color c1{255, 255, 255, (uint8_t)(255 * a)};
            Color c2{120, 220, 255, (uint8_t)(160 * a)};
            DrawLineEx({(float)sx, (float)sy}, {(float)tx2, (float)ty2}, 4.0f, c2);
            DrawLineEx({(float)sx, (float)sy}, {(float)tx2, (float)ty2}, 1.5f, c1);
        } else if (ef.kind == 1) {
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            const Sprite& s = g_sprites.smoke(ef.age * SpriteBank::SMOKE_FRAMES / ef.maxAge);
            DrawTexture(s.tex, sx - s.ox, sy - s.oy - ef.age / 2, WHITE);
        } else if (ef.kind == 5) {
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            const Sprite& s = g_sprites.muzzle();
            DrawTexture(s.tex, sx - s.ox, sy - s.oy - 12, WHITE);
        } else if (ef.kind == 6) {
            // 蘑菇云：闪光 → 火球 → 烟柱 + 环状冲击波
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            float t = (float)ef.age / ef.maxAge;
            if (ef.age < 6) {
                // 白闪全屏感（大圆）
                int r = 90 + ef.age * 30;
                DrawCircle(sx, sy - 20, (float)r, Color{255, 255, 240, (uint8_t)(220 - ef.age * 36)});
            }
            // 火球（膨胀后转暗）
            {
                int r = (int)(26 + t * 60);
                uint8_t ar = (uint8_t)(t < 0.5f ? 255 : 255 - (t - 0.5f) * 2 * 200);
                Color fire{255, (uint8_t)(200 - t * 160), (uint8_t)(80 - t * 70), ar};
                DrawEllipse(sx, sy - 14 - (int)(t * 40), r, r * 3 / 4, fire);
            }
            // 烟柱
            if (ef.age > 10) {
                float st = (float)(ef.age - 10) / (ef.maxAge - 10);
                int colH = (int)(st * 110);
                int colW = 16 + (int)(st * 26);
                DrawEllipse(sx, sy - 20 - colH / 2, colW, colH / 2 + 8, Color{90, 84, 80, (uint8_t)(230 - st * 160)});
                // 顶部蘑菇帽
                DrawEllipse(sx, sy - 24 - colH, colW + 22, 20, Color{120, 112, 106, (uint8_t)(240 - st * 170)});
            }
            // 冲击波环
            if (ef.age < 40) {
                float rt = (float)ef.age / 40;
                DrawEllipseLines(sx, sy - 4, 20 + rt * 220, 10 + rt * 100, Color{255, 230, 180, (uint8_t)(180 * (1 - rt))});
            }
        } else if (ef.kind == 7) {
            // 天降闪电：从高空到落点的锯齿折线 + 落点闪光
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            int topY = sy - 260;
            int segs = 7;
            Vector2 prev{(float)sx, (float)topY};
            for (int i = 1; i <= segs; i++) {
                float tt = (float)i / segs;
                int jx = i == segs ? 0 : (int)((ef.age * 71 + i * 131) % 17 - 8);
                Vector2 cur{sx + (float)jx, topY + (sy - topY) * tt};
                DrawLineEx(prev, cur, 3.0f, Color{180, 210, 255, 200});
                DrawLineEx(prev, cur, 1.2f, Color{240, 250, 255, 255});
                prev = cur;
            }
            DrawCircle(sx, sy, 8 + ef.age, Color{220, 240, 255, (uint8_t)(230 - ef.age * 24)});
        } else if (ef.kind == 8) {
            // 铁幕扩散：暗红能量环扩散
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            float t = (float)ef.age / ef.maxAge;
            float r = 10 + t * 110;
            DrawEllipseLines(sx, sy, r, r / 2, Color{220, 60, 50, (uint8_t)(230 * (1 - t))});
            DrawEllipseLines(sx, sy, r * 0.7f, r * 0.35f, Color{255, 120, 100, (uint8_t)(160 * (1 - t))});
        } else if (ef.kind == 10) {
            // 单位死亡动画（art.ini Die1 序列）：aux=utype aux2=dir aux3=colorId
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            const UnitAnimInfo& dai = g_sprites.animInfo((UnitType)ef.aux);
            if (dai.die > 0) {
                int phase = ef.age * dai.die / ef.maxAge;
                if (phase >= dai.die) phase = dai.die - 1;
                const Sprite& ds = g_sprites.unitAnim((UnitType)ef.aux, UAnim::Die, ef.aux2, phase, ef.aux3);
                DrawTexture(ds.tex, sx - ds.ox, sy - ds.oy, WHITE);
            }
        } else if (ef.kind == 11) {
            // 采矿尘土：褐色尘团上扬扩散
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            float t = (float)ef.age / ef.maxAge;
            int r = 2 + (int)(t * 6);
            DrawCircle(sx, sy - (int)(t * 10), (float)r, Color{150, 120, 80, (uint8_t)(140 * (1 - t))});
            DrawCircle(sx + 3, sy - 2 - (int)(t * 7), (float)(r * 2 / 3), Color{170, 140, 96, (uint8_t)(110 * (1 - t))});
        } else if (ef.kind == 12) {
            // 辐射辉光：绿色半透明圆斑闪烁
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            float t = (float)ef.age / ef.maxAge;
            float r = 14 + sinf(ef.age * 0.7f) * 4;
            DrawCircle(sx, sy, r, Color{80, 220, 60, (uint8_t)(70 * (1 - t))});
            DrawCircle(sx, sy, r * 0.55f, Color{140, 255, 100, (uint8_t)(90 * (1 - t))});
        }
    }
    // 心灵探测器（RA2 原作）：显示视野内敌方单位的攻击目标线
    if (world.hasBld(localPlayer, BldType::PsychicSensor) && (world.tick / 20) % 2) {
        for (const World::Ent& e : world.ents) {
            if (!e.alive || e.isBuilding || e.player < 0 || !world.isEnemy(e.player, localPlayer)) continue;
            if (e.target == INVALID_EID || !world.valid(e.target)) continue;
            if (world.map.fogAt(localPlayer, (int)e.x, (int)e.y) != FOG_VISIBLE) continue;
            const World::Ent& t = world.ents[e.target];
            Vector2 a = unitScreenPos(e);
            float txp = (t.x - t.y) * (TILE_W / 2.0f), typ = (t.x + t.y) * (TILE_H / 2.0f);
            Vector2 b{txp - camX, typ - camY};
            DrawLineEx({a.x, a.y - 10}, {b.x, b.y - 6}, 1.5f, Color{255, 80, 220, 150});
            DrawCircleV({b.x, b.y - 6}, 3.0f, Color{255, 80, 220, 180});
        }
    }
}

void Game::drawFogLayer() {
    int viewW = SCREEN_W - sidebarW;
    float visW = (float)viewW / camZoom;
    float visH = (float)SCREEN_H / camZoom;
    // 软迷雾遮罩：低分辨率 alpha 图 bilinear 放大（消除逐格菱形棋盘格；draw ~800 → 1）
    if (fogMaskTick != (int)(world.tick / 6)) { // 每 6 逻辑帧重烘（5 次/秒，跟随探索扩展）
        bakeFogMask();
        fogMaskTick = (int)(world.tick / 6);
    }
    if (!fogMaskTex.id) return;
    const float FS = 8.0f;
    Rectangle src{(camX + terrainOX) / FS, camY / FS, visW / FS, visH / FS};
    Rectangle dst{0, 0, visW, visH};
    if (src.x < 0) { dst.x -= src.x * FS; dst.width += src.x * FS; src.width += src.x; src.x = 0; }
    if (src.y < 0) { dst.y -= src.y * FS; dst.height += src.y * FS; src.height += src.y; src.y = 0; }
    float maxX = terrainW / FS + 2, maxY = terrainH / FS + 2;
    if (src.x + src.width > maxX) { float over = src.x + src.width - maxX; src.width -= over; dst.width -= over * FS; }
    if (src.y + src.height > maxY) { float over = src.y + src.height - maxY; src.height -= over; dst.height -= over * FS; }
    if (src.width > 0 && src.height > 0)
        DrawTexturePro(fogMaskTex, src, dst, {0, 0}, 0, WHITE);
    // 侧边栏黑块不得在此绘制：本函数处于 rlScalef(camZoom) 内，用屏幕坐标会在拉远时留下竖条
}

void Game::drawPlacement() {
    // 伞兵空降点选择：鼠标处画降落区预览圈
    if (targetingParadrop) {
        Vector2 m = mousePos();
        float wx, wy;
        screenToWorld((int)m.x, (int)m.y, wx, wy);
        int tx, ty;
        screenToTile(wx, wy, tx, ty);
        int px, py;
        tileToScreen(tx, ty, px, py);
        int sx = px - (int)camX, sy = py - (int)camY + TILE_H / 2;
        float ex = 2.5f * TILE_W / 2.0f, ey = 2.5f * TILE_H / 2.0f;
        Color cc{140, 220, 255, 220};
        if ((world.tick / 8) % 2) cc.a = 130;
        DrawEllipseLines(sx, sy, ex, ey, cc);
        DrawEllipse(sx, sy, ex, ey, Color{140, 220, 255, 30});
        DrawLine(sx - 10, sy, sx + 10, sy, cc);
        DrawLine(sx, sy - 6, sx, sy + 6, cc);
        return;
    }
    // 超武目标选择：鼠标处画范围预览圈
    if (targetingSW != SWType::COUNT) {
        Vector2 m = mousePos();
        float wx, wy;
        screenToWorld((int)m.x, (int)m.y, wx, wy);
        int tx, ty;
        screenToTile(wx, wy, tx, ty);
        int px, py;
        tileToScreen(tx, ty, px, py);
        int sx = px - (int)camX, sy = py - (int)camY + TILE_H / 2;
        float radius = targetingSW == SWType::Nuke ? 6.0f
                     : (targetingSW == SWType::Lightning ? 5.5f
                     : (targetingSW == SWType::ForceShield ? 4.0f : 3.0f));
        // 等距椭圆覆盖圈
        float ex = radius * TILE_W / 2.0f, ey = radius * TILE_H / 2.0f;
        Color cc = (targetingSW == SWType::IronCurtain || targetingSW == SWType::ForceShield)
            ? Color{80, 180, 255, 200} : Color{255, 220, 80, 220};
        if (targetingSW == SWType::IronCurtain) cc = Color{220, 60, 50, 200};
        if ((world.tick / 8) % 2) cc.a = 130;
        DrawEllipseLines(sx, sy, ex, ey, cc);
        DrawEllipse(sx, sy, ex, ey, targetingSW == SWType::IronCurtain ? Color{220, 60, 50, 36}
                    : (targetingSW == SWType::ForceShield ? Color{80, 180, 255, 36} : Color{255, 220, 80, 30}));
        // 中心准星
        DrawLine(sx - 10, sy, sx + 10, sy, cc);
        DrawLine(sx, sy - 6, sx, sy + 6, cc);
        return;
    }
    if (!placing) return;
    BldType t = world.players[localPlayer].placingBld;
    if (t == BldType::COUNT) return;
    const BldDef& d = bldDef(t);
    Vector2 m = mousePos();
    float wx, wy;
    screenToWorld((int)m.x, (int)m.y, wx, wy);
    int tx, ty;
    screenToTile(wx, wy, tx, ty);
    // 光标对准占地东南角格（与 place / updateHoverCursor 一致，避免中心格导致放置瞬间位移）
    int bx = tx - (d.w - 1), by = ty - (d.h - 1);
    // 脚印染色与 canPlace 一致（整座可放=绿，否则红），避免「格绿但放不下」
    bool canAll = world.canPlace(t, bx, by, localPlayer);
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int x = bx + dx, y = by + dy;
            int px, py;
            tileToScreen(x, y, px, py);
            if (world.map.inBounds(x, y))
                py -= heightScreenY(world.map.at(x, y).height);
            Color c = canAll ? Color{40, 220, 80, 55} : Color{255, 40, 40, 70};
            DrawTexture(fogBlack, px - TILE_W / 2 - (int)camX, py - (int)camY, c);
        }
    // 放置预览用无烘焙投影的幽灵（绿格已表示占地，避免双重阴影）
    Country placeCountry = world.players[localPlayer].country;
    const Sprite& s = g_sprites.buildingGhost(t, world.players[localPlayer].colorId, placeCountry);
    int px, py;
    tileToScreen(bx + d.w - 1, by + d.h - 1, px, py);
    if (world.map.inBounds(bx + d.w - 1, by + d.h - 1))
        py -= heightScreenY(world.map.at(bx + d.w - 1, by + d.h - 1).height);
    Color tint = canAll ? Color{255, 255, 255, 170} : Color{255, 90, 90, 160};
    Vector2 ghostP{(float)(px - (int)camX), (float)(py + TILE_H - (int)camY)};
    DrawTexture(s.tex, (int)ghostP.x - s.ox, (int)ghostP.y - s.oy, tint);
    // 放置预览笼：与选中态同一套贴图拟合参数
    {
        auto dashLine = [](Vector2 a, Vector2 b, Color c, float thick = 1.75f) {
            float dx = b.x - a.x, dy = b.y - a.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 1.0f) return;
            if (len < 14.0f) { DrawLineEx(a, b, thick, c); return; }
            dx /= len; dy /= len;
            const float dash = 5.0f, gap = 3.0f;
            for (float t = 0; t < len; t += dash + gap) {
                float t1 = t, t2 = std::min(len, t + dash);
                DrawLineEx({a.x + dx * t1, a.y + dy * t1}, {a.x + dx * t2, a.y + dy * t2}, thick, c);
            }
        };
        BldCageParams cage = resolveBldCage(SpriteBank::bldSpriteStem(t, placeCountry), d.w, d.h, s);
        Vector2 bs{ghostP.x + cage.offX, ghostP.y + cage.offY};
        float elev = cage.elev;
        Vector2 bn, be, bw;
        footCorners(bs, cage, bn, be, bw);
        Vector2 tn{bn.x, bn.y - elev}, te{be.x, be.y - elev};
        Vector2 ts{bs.x, bs.y - elev}, tw{bw.x, bw.y - elev};
        Color edge = canAll ? Color{255, 240, 60, 220} : Color{255, 90, 90, 220};
        Color fill{edge.r, edge.g, edge.b, (unsigned char)20};
        auto quad = [&](Vector2 a, Vector2 b, Vector2 c, Vector2 d) {
            DrawTriangle(a, b, c, fill);
            DrawTriangle(a, c, d, fill);
        };
        quad(bw, bs, ts, tw);
        quad(bs, be, te, ts);
        dashLine(bn, be, edge); dashLine(be, bs, edge);
        dashLine(bs, bw, edge); dashLine(bw, bn, edge);
        dashLine(tn, te, edge); dashLine(te, ts, edge);
        dashLine(ts, tw, edge); dashLine(tw, tn, edge);
        const float vt = 2.25f;
        DrawLineEx(bn, tn, vt, edge);
        DrawLineEx(be, te, vt, edge);
        DrawLineEx(bs, ts, vt, edge);
        DrawLineEx(bw, tw, vt, edge);
    }
}

void Game::drawHealthBar(int px, int py, int w, float frac, bool selected, int pipCount) {
    (void)selected;
    // RA2：血条为一格一格的 pip（非连续长条）；颜色阈值 ConditionYellow=50% / ConditionRed=25%
    frac = std::clamp(frac, 0.0f, 1.0f);
    const int pipW = 3, pipH = 4, gap = 1;
    int n = pipCount > 0 ? std::clamp(pipCount, 3, 30)
                         : std::clamp((w + gap) / (pipW + gap), 5, 25);
    int rowW = n * pipW + (n - 1) * gap;
    int x0 = px + (w - rowW) / 2;
    fillRectWorld((float)(x0 - 1), (float)(py - 1), (float)(rowW + 2), (float)(pipH + 2), Color{0, 0, 0, 180});
    Color fill = frac > 0.5f ? Color{60, 220, 60, 255}
               : (frac > 0.25f ? Color{230, 210, 40, 255} : Color{220, 40, 40, 255});
    const Color empty{40, 40, 36, 220};
    int filled = (int)std::lround(frac * n);
    if (frac > 0.0f && filled < 1) filled = 1;
    if (frac <= 0.0f) filled = 0;
    for (int i = 0; i < n; i++) {
        int x = x0 + i * (pipW + gap);
        fillRectWorld((float)x, (float)py, (float)pipW, (float)pipH, i < filled ? fill : empty);
    }
}

// ===================== 输入包装（高 DPI 修正 + 脚本注入） =====================
// ===================== 自动化完整游玩测试 =====================
// 脚本注入输入，真实窗口完整操作一遍：
// 主菜单→遭遇战设置→开局→点选MCV→D展开→侧边栏建电厂→放置→
// 框选坦克→右键移动→ESC菜单→返回主菜单→战役选择→战役开局→返回。
// 每步断言 PASS/FAIL 并截图 pt_XX_*.png，返回失败数（0 = 全部通过）。

