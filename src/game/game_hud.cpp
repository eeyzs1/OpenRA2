// Game 的 HUD 部分实现（侧边栏/小地图/菜单）—— RA2 原作 1:1 复刻
// 原作侧边栏结构（对照 1366x768 实测截图等比放大到 1440x810）：
//   顶部银雕（资金黑槽绿 LED + 蓝铬图标带）→ 银框雷达（黑底小地图）→
//   维修/出售蓝铬药丸（扳手|$）→ 4 蓝铬页签（兵种剪影）→ cameo 双列网格 →
//   电力条（左缘竖条，绿顶红底，填充=剩余电力）→ 右缘银蔓雕花 →
//   底部蓝铬穹顶+阵营徽 → 底部状态栏（左功能图标，右超武计时）
// 阵营化：盟军银框蓝铬 / 苏军(含中国)金框银铬红符 / 尤里紫银框紫铬
#include "game/game.h"
#include "game/campaign.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <ctime>

extern const int HUD_UNUSED; // 占位

static void drawTextF(Font f, const char* s, int x, int y, int size, Color c) {
    DrawTextEx(f, s, {(float)x, (float)y}, (float)size, 1, c);
}

// ===================== 菜单共享 RA2 金属 GUI 素材 =====================
const Color GUI_GOLD{184, 152, 64, 255};
const Color GUI_GOLD_HI{240, 200, 96, 255};
const Color GUI_EDGE_HI{108, 116, 128, 255};   // 冷灰高光
const Color GUI_EDGE_LO{6, 8, 12, 255};

// 拉丝金属纹理（菜单/设置页共享）
static Texture2D genMetalTex(Color base0, Color base1) {
    PixBuf p(96, 96);
    auto hsh = [](int x, int y) {
        uint32_t v = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u);
        v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15;
        return (float)(v % 1024) / 1024.0f;
    };
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 96; x++) {
            float streak = hsh(x / 9, y) * 0.62f + hsh(x / 3, y + 40) * 0.38f;
            float v = 0.88f + (streak - 0.5f) * 0.34f + (hsh(x, y) - 0.5f) * 0.10f;
            v *= 1.10f - 0.20f * (y / 95.0f);
            auto L = [&](uint8_t a, uint8_t b) { return (uint8_t)clampi((int)((a + (b - a) * v)), 0, 255); };
            p.set(x, y, Color{L(base0.r, base1.r), L(base0.g, base1.g), L(base0.b, base1.b), 255});
        }
    return p.toTexture();
}

Texture2D guiMetalTex() {
    static Texture2D t = genMetalTex(Color{36, 40, 44, 255}, Color{50, 54, 58, 255});
    return t;
}

static void guiTileTex(Texture2D t, int x, int y, int w, int h) {
    BeginScissorMode(x, y, w, h);
    for (int ty = y; ty < y + h; ty += 96)
        for (int tx = x; tx < x + w; tx += 96)
            DrawTexture(t, tx, ty, WHITE);
    EndScissorMode();
}
void guiMetalFill(int x, int y, int w, int h) { guiTileTex(guiMetalTex(), x, y, w, h); }

void guiRivet(int x, int y) {
    DrawCircle(x, y, 2.4f, Color{10, 11, 14, 255});
    DrawCircle(x, y, 1.5f, Color{106, 114, 128, 255});
    DrawPixel(x - 1, y - 1, Color{172, 182, 198, 255});
}

static void guiBevelC(Rectangle r, bool sunken, Color hi, Color lo) {
    Color a = sunken ? lo : hi, b = sunken ? hi : lo;
    int x = (int)r.x, y = (int)r.y, w = (int)r.width, h = (int)r.height;
    DrawLine(x, y, x + w - 1, y, a);
    DrawLine(x, y, x, y + h - 1, a);
    DrawLine(x, y + h - 1, x + w - 1, y + h - 1, b);
    DrawLine(x + w - 1, y, x + w - 1, y + h - 1, b);
}
void guiBevel(Rectangle r, bool sunken) { guiBevelC(r, sunken, GUI_EDGE_HI, GUI_EDGE_LO); }

void guiSlot(Rectangle r) {
    DrawRectangleRec(r, Color{15, 16, 20, 255});
    guiBevel(r, true);
}

void guiPanel(int x, int y, int w, int h) {
    guiMetalFill(x, y, w, h);
    guiBevel({(float)x, (float)y, (float)w, (float)h}, false);
    DrawRectangleLinesEx({(float)x + 3, (float)y + 3, (float)w - 6, (float)h - 6}, 1, GUI_GOLD);
    guiRivet(x + 7, y + 7); guiRivet(x + w - 7, y + 7);
    guiRivet(x + 7, y + h - 7); guiRivet(x + w - 7, y + h - 7);
}

void drawTextS(Font f, const char* s, int x, int y, int size, Color c) {
    DrawTextEx(f, s, {(float)x + 1, (float)y + 1}, (float)size, 1, Color{0, 0, 0, 210});
    DrawTextEx(f, s, {(float)x, (float)y}, (float)size, 1, c);
}

// ===================== 阵营化 GUI 调色板（RA2 原作实测取色） =====================
struct GuiPal {
    Color metal0, metal1;     // 雕饰框 亮/暗（盟军银 / 苏军金 / 尤里紫银）
    Color trim, trimHi;       // 框暗线 / 高光
    Color chrome0, chrome1;   // 铬面交互件 亮/暗
    Color chromeHi;           // 铬面顶缘高光
    Color glyph;              // 铬面符号色（盟军深海军蓝 / 苏军红珐琅 / 尤里深紫）
    Color led;                // 资金 LED（原作全阵营统一青白色）
    Color bgDark;             // 侧边栏底（近黑）
    Color dome0, dome1;       // 底部穹顶 亮/暗
    Color emblem;             // 阵营徽色
};
static const GuiPal GUI_PAL[3] = {
    // 盟军：银框 + 蓝铬 + 深蓝符号 + 蓝穹顶 + 银鹰徽
    {{198, 206, 220, 255}, {84, 90, 104, 255}, {28, 32, 40, 255}, {236, 244, 252, 255},
     {88, 142, 255, 255}, {14, 38, 116, 255}, {172, 206, 255, 255}, {12, 28, 72, 255},
     {164, 216, 244, 255}, {6, 8, 14, 255}, {88, 142, 255, 255}, {14, 38, 116, 255}, {226, 236, 248, 255}},
    // 苏军/中国：金框 + 银灰铬 + 红珐琅符号 + 红穹顶 + 红星徽
    {{218, 188, 112, 255}, {106, 80, 32, 255}, {32, 24, 10, 255}, {255, 238, 178, 255},
     {188, 182, 170, 255}, {100, 94, 84, 255}, {244, 240, 232, 255}, {178, 36, 24, 255},
     {164, 216, 244, 255}, {12, 10, 7, 255}, {168, 52, 40, 255}, {78, 20, 14, 255}, {216, 48, 40, 255}},
    // 尤里：紫银框 + 紫铬 + 深紫符号 + 紫穹顶 + Ψ 徽
    {{188, 158, 206, 255}, {74, 56, 92, 255}, {26, 18, 34, 255}, {246, 226, 255, 255},
     {158, 96, 210, 255}, {52, 24, 86, 255}, {226, 182, 255, 255}, {30, 10, 52, 255},
     {164, 216, 244, 255}, {10, 7, 14, 255}, {158, 96, 210, 255}, {52, 24, 86, 255}, {200, 120, 230, 255}},
};
static int guiStyleOf(Faction f) { return f == Faction::Allies ? 0 : f == Faction::Yuri ? 2 : 1; }

// ===================== 铬面件预渲染（PixBuf CPU 绘制，一次生成） =====================
static uint32_t guiHash(int x, int y, uint32_t seed) {
    uint32_t v = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ seed;
    v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15;
    return v;
}
static float guiNoise(int x, int y, uint32_t seed) { return (float)(guiHash(x, y, seed) & 1023) / 1023.0f; }

static bool inRoundRect(int x, int y, int w, int h, int r) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    int cx = x < r ? r : (x >= w - r ? w - r - 1 : x);
    int cy = y < r ? r : (y >= h - r ? h - r - 1 : y);
    int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}
static void pbFillMask(PixBuf& p, int x, int y, int w, int h, int r, Color c) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            if (inRoundRect(xx, yy, w, h, r)) p.set(x + xx, y + yy, c);
}
static void pbRingMask(PixBuf& p, int x, int y, int w, int h, int r, int th, Color c) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            if (inRoundRect(xx, yy, w, h, r) &&
                !inRoundRect(xx - th, yy - th, w - 2 * th, h - 2 * th, r > th ? r - th : 0))
                p.set(x + xx, y + yy, c);
}
static void pbClearMask(PixBuf& p, int x, int y, int w, int h, int r) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            if (inRoundRect(xx, yy, w, h, r)) p.set(x + xx, y + yy, Color{0, 0, 0, 0});
}
// 铬面渐变填充（圆角）：未按下=顶亮底暗+顶部光泽带；按下=顶暗底亮（凹陷感）
static void pbChrome(PixBuf& p, int x, int y, int w, int h, int r, Color c0, Color c1,
                     bool pressed, uint32_t seed) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            if (!inRoundRect(xx, yy, w, h, r)) continue;
            float t = (float)yy / (h > 1 ? h - 1 : 1);
            float v = !pressed ? (t < 0.40f ? 1.02f - t * 0.55f : 0.80f - (t - 0.40f) * 0.95f)
                               : 0.30f + t * 0.55f;
            v += (guiNoise(xx, yy, seed) - 0.5f) * 0.05f;
            auto L = [&](uint8_t a, uint8_t b) { return (uint8_t)clampi((int)(a + (b - a) * v), 0, 255); };
            p.set(x + xx, y + yy, Color{L(c1.r, c0.r), L(c1.g, c0.g), L(c1.b, c0.b), 255});
        }
}
// 拉丝银填充（圆角）：竖向渐变（顶亮底暗）+ 横向刷痕 + 细噪
static void pbMetal(PixBuf& p, int x, int y, int w, int h, int r, Color m0, Color m1, uint32_t seed) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            if (!inRoundRect(xx, yy, w, h, r)) continue;
            float v = 1.0f - (float)yy / h;
            float n = guiNoise(xx >> 3, yy, seed) * 0.6f + guiNoise(xx, yy, seed + 7) * 0.4f;
            v = v * 0.70f + 0.15f + (n - 0.5f) * 0.22f;
            auto L = [&](uint8_t a, uint8_t b) { return (uint8_t)clampi((int)(a + (b - a) * v), 0, 255); };
            p.set(x + xx, y + yy, Color{L(m1.r, m0.r), L(m1.g, m0.g), L(m1.b, m0.b), 255});
        }
}
static void pbTri(PixBuf& p, float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
    int minX = (int)floorf(std::min({x0, x1, x2})), maxX = (int)ceilf(std::max({x0, x1, x2}));
    int minY = (int)floorf(std::min({y0, y1, y2})), maxY = (int)ceilf(std::max({y0, y1, y2}));
    auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
        return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
    };
    for (int y = minY; y <= maxY; y++)
        for (int x = minX; x <= maxX; x++) {
            float w0 = edge(x1, y1, x2, y2, x + 0.5f, y + 0.5f);
            float w1 = edge(x2, y2, x0, y0, x + 0.5f, y + 0.5f);
            float w2 = edge(x0, y0, x1, y1, x + 0.5f, y + 0.5f);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) p.set(x, y, c);
        }
}
static void pbStar(PixBuf& p, float cx, float cy, float rO, float rI, Color c) {
    for (int i = 0; i < 5; i++) {
        float a0 = -(float)PI / 2 + i * ((float)PI * 2 / 5), a1 = a0 + (float)PI / 5, a2 = a0 + (float)PI * 2 / 5;
        pbTri(p, cx, cy, cx + cosf(a0) * rO, cy + sinf(a0) * rO, cx + cosf(a1) * rI, cy + sinf(a1) * rI, c);
        pbTri(p, cx, cy, cx + cosf(a1) * rI, cy + sinf(a1) * rI, cx + cosf(a2) * rO, cy + sinf(a2) * rO, c);
    }
}

// 铬面件组（每阵营风格一套，懒生成）
struct ChromeSet {
    Texture2D header;  // 184x56  顶部银雕（资金黑槽 + 铬面图标带）
    Texture2D bandDome;// 184x26  资金下玻璃穹带（阵营琉璃+徽标水印）
    Texture2D radar;   // 154x112 雷达银框（横矩形，中心镂空 140x96）
    Texture2D bandGlow;// 184x24  雷达下阵营徽发光带
    Texture2D pill[3]; // 152x32  维修/出售拱形穹带（0 常态 1 悬停 2 激活）
    Texture2D tab[3];  // 37x28   页签（0 常态 1 悬停 2 激活）
    Texture2D cradle;  // 184x70  中部雕饰托（药丸/页签两侧银翼+圆钮）
    Texture2D cap;     // 184x44  底部穹顶 + 阵营徽
    Texture2D edge;    // 14x48   右缘银蔓（竖向平铺）
};
static ChromeSet g_chrome[3];
static bool g_chromeOk[3]{};

static void genChrome(int style) {
    const GuiPal& P = GUI_PAL[style];
    ChromeSet& C = g_chrome[style];
    auto dim = [](Color c, float f) {
        return Color{(uint8_t)(c.r * f), (uint8_t)(c.g * f), (uint8_t)(c.b * f), 255};
    };

    { // ---- 顶部银雕（原作：LED 黑槽下即阵营琉璃穹，穹内嵌图标带） ----
        PixBuf p(184, 56);
        pbMetal(p, 0, 0, 184, 46, 10, P.metal0, P.metal1, 11);   // 主银体
        pbMetal(p, 0, 34, 16, 22, 7, P.metal0, P.metal1, 12);    // 左翼（下垂接雷达框）
        pbMetal(p, 168, 34, 16, 22, 7, P.metal0, P.metal1, 13);  // 右翼
        pbRingMask(p, 0, 0, 184, 46, 10, 1, P.trim);
        for (int x = 12; x < 172; x++) p.set(x, 1, P.trimHi);    // 顶缘高光
        // 资金黑槽（圆角深槽，LED 数字动态绘制）
        pbFillMask(p, 10, 5, 164, 19, 9, Color{3, 4, 7, 255});
        pbRingMask(p, 10, 5, 164, 19, 9, 1, dim(P.metal1, 0.7f));
        // 琉璃穹带（阵营色玻璃弧，圆心在下方；原作标志观感）
        pbFillMask(p, 10, 27, 164, 23, 10, Color{4, 5, 9, 255}); // 穹带黑槽
        for (int yy = 28; yy < 50; yy++)
            for (int xx = 12; xx < 172; xx++) {
                if (!inRoundRect(xx - 12, yy - 27, 160, 23, 10)) continue;
                float ex = (xx - 92) / 80.0f, ey = (yy - 58) / 30.0f;
                if (ex * ex + ey * ey > 1.0f) continue;          // 穹弧（中部饱满）
                float v = 1.08f - (float)(yy - 28) / 22.0f * 0.72f;
                v += (guiNoise(xx, yy, 36) - 0.5f) * 0.06f;
                auto L = [&](uint8_t a, uint8_t b) { return (uint8_t)clampi((int)(a + (b - a) * v), 0, 255); };
                p.set(xx, yy, Color{L(P.dome1.r, P.dome0.r), L(P.dome1.g, P.dome0.g), L(P.dome1.b, P.dome0.b), 255});
            }
        // 两组凹陷符号区（压入穹面）
        Color rec = dim(P.dome1, 0.55f);
        pbFillMask(p, 20, 33, 58, 11, 3, rec);
        pbFillMask(p, 106, 33, 58, 11, 3, rec);
        // 带内符号剪影（暗调压印）：左组 波纹+方块；右组 圆点+双方块（原作布局）
        Color gb = dim(P.dome0, 0.9f);
        for (int i = 0; i < 12; i++) {
            int yy = 36 + (i % 4 < 2 ? 0 : 2);
            p.set(26 + i, yy, gb); p.set(26 + i, yy + 3, gb);
        }
        p.fillRect(44, 35, 9, 7, gb);
        p.fillEllipse(114, 38, 3, 3, gb);
        p.fillRect(124, 35, 9, 7, gb);
        p.fillRect(140, 35, 13, 7, gb);
        // 穹顶高光弧（玻璃感）
        for (int xx = 30; xx < 154; xx++) {
            float t = (xx - 92) / 62.0f;
            int hy = 29 + (int)(t * t * 6.0f);
            p.set(xx, hy, P.chromeHi);
        }
        C.header = p.toTexture();
        SetTextureFilter(C.header, TEXTURE_FILTER_BILINEAR);
    }
    { // ---- 雷达银框（横矩形 154x112，中心镂空 140x96；原作雷达为 1.39:1 横屏） ----
        PixBuf p(154, 112);
        pbMetal(p, 0, 0, 154, 112, 8, P.metal0, P.metal1, 31);
        pbClearMask(p, 7, 8, 140, 96, 2);
        pbRingMask(p, 0, 0, 154, 112, 8, 1, P.trim);
        pbRingMask(p, 6, 7, 142, 98, 3, 1, Color{8, 9, 13, 255}); // 内缘暗线
        for (int x = 12; x < 142; x++) p.set(x, 1, P.trimHi);      // 顶缘高光
        C.radar = p.toTexture();
        SetTextureFilter(C.radar, TEXTURE_FILTER_BILINEAR);
    }
    { // ---- 资金下玻璃穹带（原作标志：阵营琉璃弧 + 徽标水印） ----
        PixBuf p(184, 26);
        pbMetal(p, 0, 0, 184, 26, 6, P.metal0, P.metal1, 35);       // 银底
        pbFillMask(p, 4, 2, 176, 24, 8, Color{6, 8, 14, 255});      // 黑槽
        for (int yy = 3; yy < 26; yy++)                             // 琉璃穹（圆心在下方）
            for (int xx = 6; xx < 178; xx++) {
                float ex = (xx - 92) / 86.0f, ey = (yy - 34) / 30.0f;
                if (ex * ex + ey * ey > 1.0f) continue;
                float v = 1.05f - (float)yy / 26.0f * 0.72f;
                v += (guiNoise(xx, yy, 36) - 0.5f) * 0.06f;
                auto L = [&](uint8_t a, uint8_t b) { return (uint8_t)clampi((int)(a + (b - a) * v), 0, 255); };
                p.set(xx, yy, Color{L(P.dome1.r, P.dome0.r), L(P.dome1.g, P.dome0.g), L(P.dome1.b, P.dome0.b), 255});
            }
        // 徽标水印（暗调压印）：盟军鹰/苏军星/尤里Ψ
        Color wm{(uint8_t)(P.dome1.r * 0.5f), (uint8_t)(P.dome1.g * 0.5f), (uint8_t)(P.dome1.b * 0.5f), 255};
        if (style == 0) {
            pbTri(p, 92, 6, 74, 18, 88, 19, wm); pbTri(p, 92, 6, 110, 18, 96, 19, wm);
            pbTri(p, 88, 15, 96, 15, 92, 24, wm);
        } else if (style == 1) {
            pbStar(p, 92, 15, 9, 3.6f, wm);
        } else {
            pbTri(p, 85, 7, 89, 7, 87, 17, wm); pbTri(p, 99, 7, 95, 7, 97, 17, wm);
            p.fillRect(90, 7, 4, 17, wm);
        }
        // 顶部高光弧（玻璃感）
        for (int xx = 30; xx < 154; xx++) {
            float t = (xx - 92) / 62.0f;
            int hy = 5 + (int)(t * t * 7.0f);
            p.set(xx, hy, P.chromeHi);
            if (xx % 2 == 0) p.set(xx, hy + 1, dim(P.dome0, 1.25f));
        }
        C.bandDome = p.toTexture();
        SetTextureFilter(C.bandDome, TEXTURE_FILTER_BILINEAR);
    }
    { // ---- 雷达下阵营徽发光带 ----
        PixBuf p(184, 24);
        pbFillMask(p, 0, 0, 184, 24, 4, Color{5, 6, 10, 255});
        // 中心辉光（徽后大软斑）
        for (int yy = 2; yy < 22; yy++)
            for (int xx = 40; xx < 144; xx++) {
                float ex = (xx - 92) / 52.0f, ey = (yy - 12) / 11.0f;
                float d2 = ex * ex + ey * ey;
                if (d2 > 1.0f) continue;
                float v = (1.0f - d2) * 0.55f;
                Color g = p.get(xx, yy);
                p.set(xx, yy, Color{(uint8_t)std::min(255, (int)(g.r + P.dome0.r * v)),
                                    (uint8_t)std::min(255, (int)(g.g + P.dome0.g * v)),
                                    (uint8_t)std::min(255, (int)(g.b + P.dome0.b * v)), 255});
            }
        // 徽标（亮）
        if (style == 0) {
            pbTri(p, 92, 4, 76, 15, 89, 16, P.emblem); pbTri(p, 92, 4, 108, 15, 95, 16, P.emblem);
            pbTri(p, 89, 13, 95, 13, 92, 21, P.emblem);
        } else if (style == 1) {
            pbStar(p, 92, 12, 8, 3.2f, P.emblem);
        } else {
            pbTri(p, 86, 5, 89, 5, 87, 14, P.emblem); pbTri(p, 98, 5, 95, 5, 97, 14, P.emblem);
            p.fillRect(90, 5, 4, 15, P.emblem);
        }
        // 两侧装饰小钉
        p.fillEllipse(20, 12, 4, 4, P.metal1); p.set(19, 11, P.trimHi);
        p.fillEllipse(164, 12, 4, 4, P.metal1); p.set(163, 11, P.trimHi);
        // 上下铬线
        for (int xx = 30; xx < 154; xx++) { p.set(xx, 1, P.trim); p.set(xx, 22, P.trim); }
        C.bandGlow = p.toTexture();
        SetTextureFilter(C.bandGlow, TEXTURE_FILTER_BILINEAR);
    }
    for (int s = 0; s < 3; s++) { // ---- 维修/出售拱形穹带（原作：顶缘中部上拱的亮铬穹带） ----
        PixBuf p(152, 32);
        // 拱形外轮廓：顶缘 y = 4 - 3*cos 形（中高边低），底缘平直
        auto inBand = [](int x, int y) {
            if (x < 0 || x >= 152 || y < 0 || y >= 32) return false;
            float t = (x - 76) / 76.0f;
            int top = 4 + (int)(t * t * 5.0f);   // 中高 4，边 9
            int bot = 29 - (int)(t * t * 2.0f);  // 底微拱
            if (y < top || y > bot) return false;
            // 端部圆角
            if (x < 8 || x >= 144) {
                int cx = x < 8 ? 8 : 143;
                float cy = (top + bot) / 2.0f, ry = (bot - top) / 2.0f;
                float dx = (x - cx) / 8.0f, dy = (y - cy) / ry;
                return dx * dx + dy * dy <= 1.0f;
            }
            return true;
        };
        for (int yy = 0; yy < 32; yy++)
            for (int xx = 0; xx < 152; xx++) {
                if (!inBand(xx, yy)) continue;
                float t = (xx - 76) / 76.0f;
                int top = 4 + (int)(t * t * 5.0f), bot = 29 - (int)(t * t * 2.0f);
                float v = (float)(yy - top) / (bot - top);
                float l = s == 2 ? 0.35f + v * 0.55f                 // 按下：顶暗底亮
                          : v < 0.38f ? 1.06f - v * 0.5f             // 常态：顶高光带
                                      : 0.88f - (v - 0.38f) * 0.95f;
                l += (guiNoise(xx, yy, 41 + s) - 0.5f) * 0.05f;
                if (s == 1) l *= 1.14f;                              // 悬停提亮
                auto L = [&](uint8_t a, uint8_t b) { return (uint8_t)clampi((int)(a + (b - a) * l), 0, 255); };
                p.set(xx, yy, Color{L(P.chrome1.r, P.chrome0.r), L(P.chrome1.g, P.chrome0.g), L(P.chrome1.b, P.chrome0.b), 255});
            }
        // 拱缘描边 + 顶高光弧
        for (int yy = 0; yy < 32; yy++)
            for (int xx = 0; xx < 152; xx++)
                if (inBand(xx, yy) && (!inBand(xx - 1, yy) || !inBand(xx + 1, yy) || !inBand(xx, yy - 1) || !inBand(xx, yy + 1)))
                    p.set(xx, yy, s == 2 ? P.trimHi : P.trim);
        if (s != 2)
            for (int xx = 20; xx < 132; xx++) {
                float t = (xx - 76) / 56.0f;
                int hy = 5 + (int)(t * t * 4.0f);
                if (inBand(xx, hy)) p.set(xx, hy, P.chromeHi);
            }
        for (int y = 8; y < 26; y++) { p.set(75, y, dim(P.chrome1, 0.5f)); p.set(76, y, P.chromeHi); } // 中缝
        C.pill[s] = p.toTexture();
        SetTextureFilter(C.pill[s], TEXTURE_FILTER_BILINEAR);
    }
    for (int s = 0; s < 3; s++) { // ---- 页签（原作：圆角铬面 + 边缘辉光，激活态强发光） ----
        PixBuf p(37, 28);
        // 底晕（激活态：向外发光的底色）
        if (s == 2)
            for (int yy = 0; yy < 28; yy++)
                for (int xx = 0; xx < 37; xx++) {
                    float ex = (xx - 18.5f) / 18.5f, ey = (yy - 14) / 14.0f;
                    float d = ex * ex + ey * ey;
                    if (d < 1.3f) {
                        float g = (1.3f - d) * 0.35f;
                        p.set(xx, yy, Color{(uint8_t)(P.chrome0.r * g), (uint8_t)(P.chrome0.g * g), (uint8_t)(P.chrome0.b * g), 255});
                    }
                }
        pbChrome(p, 0, 0, 37, 28, 5, P.chrome0, P.chrome1, false, 51 + s);
        if (s == 1) p.shade(1.18f);
        if (s == 2) p.shade(1.55f); // 激活页签明显发亮（原作特征）
        // 内缘辉光带（原作页签的青/金亮边）
        for (int yy = 1; yy < 27; yy++)
            for (int xx = 1; xx < 36; xx++)
                if (inRoundRect(xx, yy, 37, 28, 5) &&
                    (!inRoundRect(xx - 1, yy, 37, 28, 5) || !inRoundRect(xx, yy - 1, 37, 28, 5))) {
                    Color cur = p.get(xx, yy);
                    p.set(xx, yy, Color{(uint8_t)std::min(255, cur.r + P.chromeHi.r / 2),
                                        (uint8_t)std::min(255, cur.g + P.chromeHi.g / 2),
                                        (uint8_t)std::min(255, cur.b + P.chromeHi.b / 2), 255});
                }
        pbRingMask(p, 0, 0, 37, 28, 5, 1, s == 2 ? P.trimHi : P.trim);
        C.tab[s] = p.toTexture();
        SetTextureFilter(C.tab[s], TEXTURE_FILTER_BILINEAR);
    }
    { // ---- 中部雕饰托 ----
        PixBuf p(184, 70);
        pbMetal(p, 0, 0, 15, 70, 6, P.metal0, P.metal1, 61);    // 左翼
        pbMetal(p, 169, 0, 15, 70, 6, P.metal0, P.metal1, 62);  // 右翼
        p.fillEllipse(8, 52, 6, 6, P.metal1);                   // 页签排左圆钮
        p.fillEllipse(8, 52, 4, 4, P.metal0);
        p.set(6, 50, P.trimHi);
        p.fillEllipse(176, 52, 6, 6, P.metal1);                 // 页签排右圆钮
        p.fillEllipse(176, 52, 4, 4, P.metal0);
        p.set(174, 50, P.trimHi);
        C.cradle = p.toTexture();
        SetTextureFilter(C.cradle, TEXTURE_FILTER_BILINEAR);
    }
    { // ---- 底部穹顶 + 阵营徽 ----
        PixBuf p(184, 44);
        pbFillMask(p, 0, 4, 184, 40, 8, P.bgDark);              // 藏青雕座
        pbRingMask(p, 0, 4, 184, 40, 8, 1, P.trim);
        p.fillEllipse(92, 27, 81, 17, P.metal1);                // 银月牙托
        for (int yy = 8; yy <= 30; yy++)                        // 铬面穹顶（半椭圆）
            for (int xx = 20; xx <= 164; xx++) {
                float ex = (xx - 92) / 72.0f, ey = (yy - 30) / 22.0f;
                if (ex * ex + ey * ey > 1.0f) continue;
                float v = 1.0f - (float)(yy - 8) / 22.0f * 0.75f;
                v += (guiNoise(xx, yy, 71) - 0.5f) * 0.05f;
                auto L = [&](uint8_t a, uint8_t b) { return (uint8_t)clampi((int)(a + (b - a) * v), 0, 255); };
                p.set(xx, yy, Color{L(P.dome1.r, P.dome0.r), L(P.dome1.g, P.dome0.g), L(P.dome1.b, P.dome0.b), 255});
            }
        for (int yy = 10; yy < 30; yy++) { p.set(91, yy, dim(P.dome1, 0.5f)); p.set(92, yy, P.chromeHi); } // 穹顶中缝
        if (style == 0) {          // 盟军：银鹰
            pbTri(p, 166, 12, 154, 20, 163, 21, P.emblem);
            pbTri(p, 166, 12, 178, 20, 169, 21, P.emblem);
            pbTri(p, 163, 18, 169, 18, 166, 27, P.emblem);
        } else if (style == 1) {   // 苏军：红五星
            pbStar(p, 166, 19, 8, 3.2f, P.emblem);
        } else {                   // 尤里：Ψ
            pbTri(p, 160, 12, 163, 12, 162, 20, P.emblem);
            pbTri(p, 169, 12, 172, 12, 170, 20, P.emblem);
            p.fillRect(165, 12, 2, 15, P.emblem);
        }
        C.cap = p.toTexture();
        SetTextureFilter(C.cap, TEXTURE_FILTER_BILINEAR);
    }
    { // ---- 右缘银蔓 ----
        PixBuf p(14, 48);
        pbMetal(p, 9, 0, 5, 48, 2, P.metal0, P.metal1, 81);
        p.fillEllipse(7, 9, 6, 7, P.metal1);
        p.fillEllipse(7, 9, 4, 5, P.metal0);
        p.set(5, 7, P.trimHi);
        p.fillEllipse(7, 39, 6, 7, P.metal1);
        p.fillEllipse(7, 39, 4, 5, P.metal0);
        p.set(5, 37, P.trimHi);
        C.edge = p.toTexture();
        SetTextureFilter(C.edge, TEXTURE_FILTER_BILINEAR);
    }
    g_chromeOk[style] = true;
}

// 底部状态栏（阵营通用：黑底 + 银药丸菜单钮 + 蓝宝珠 + 6 蓝图标）
static Texture2D g_bbar{};
static void genBottomBar() {
    const GuiPal& P = GUI_PAL[0];
    PixBuf p(1256, 26);
    for (int y = 0; y < 26; y++)
        for (int x = 0; x < 1256; x++) {
            float t = (float)y / 25.0f;
            p.set(x, y, Color{(uint8_t)(6 + t * 16), (uint8_t)(7 + t * 4), (uint8_t)(9 + t * 4), 255});
        }
    p.hline(0, 1255, 0, Color{72, 84, 106, 255});
    p.hline(0, 1255, 1, Color{16, 19, 26, 255});
    // 银药丸菜单钮（3 道暗槽）
    pbMetal(p, 6, 5, 52, 16, 8, P.metal0, P.metal1, 91);
    pbRingMask(p, 6, 5, 52, 16, 8, 1, P.trim);
    for (int i = 0; i < 3; i++) p.fillRect(14, 9 + i * 4, 36, 2, Color{24, 28, 36, 255});
    // 蓝宝珠
    p.fillEllipse(72, 13, 7, 7, Color{16, 40, 120, 255});
    p.fillEllipse(70, 11, 4, 4, Color{60, 120, 255, 255});
    p.set(68, 9, Color{200, 230, 255, 255});
    // 6 蓝图标（暗蓝光晕 + 亮蓝芯）
    Color halo{18, 46, 100, 255}, core{72, 166, 255, 255}, hi{190, 226, 255, 255};
    p.ellipse(104, 13, 6, 6, halo); p.ellipse(104, 13, 5, 5, core);       // ⓘ 信息
    p.fillRect(103, 9, 2, 2, hi); p.fillRect(103, 12, 2, 5, hi);
    p.ellipse(140, 13, 6, 6, halo); p.ellipse(140, 13, 5, 5, core);       // ⏸ 暂停
    p.fillRect(137, 10, 2, 7, hi); p.fillRect(141, 10, 2, 7, hi);
    pbTri(p, 168, 13, 174, 8, 174, 18, core); p.fillRect(174, 9, 3, 8, core); // 喇叭+声波
    for (int i = 0; i < 3; i++) { p.set(180 + i * 4, 10, halo); p.set(180 + i * 4, 13, core); p.set(180 + i * 4, 16, halo); }
    for (int i = 0; i < 11; i++) { p.set(210 + i, 8 + i, core); p.set(220 - i, 8 + i, core); } // 交叉工具
    p.set(210, 8, hi); p.set(220, 8, hi); p.set(210, 18, hi); p.set(220, 18, hi);
    p.fillEllipse(254, 11, 6, 5, core); pbTri(p, 248, 12, 260, 12, 254, 20, core); // 盾
    p.fillEllipse(254, 11, 4, 3, halo);
    pbTri(p, 288, 8, 296, 13, 288, 18, core); pbTri(p, 296, 8, 304, 13, 296, 18, core); // 速度 »
    g_bbar = p.toTexture();
    SetTextureFilter(g_bbar, TEXTURE_FILTER_BILINEAR);
}

// ===================== 原作贴图 GUI（assets/gui 提取自原作截图 = 观感 1:1） =====================
// 侧边栏源图 171x768（1366x768 原作实测 x1195..1366），动态区已清理：
//   资金数字槽/雷达内腔/cameo 槽/电力填充腔由游戏运行时绘制；其余铬面雕饰即原作原图。
// 阵营化：盟军=原图蓝铬；苏军/尤里对蓝主导像素做色相旋转（红珐琅/紫铬），雕饰银框不变。
struct OrigGui {
    Texture2D sidebar[3]{}; // 171x768 × 3 阵营风格
    Texture2D bottombar{};  // 1366x33
    bool tried = false, ok = false;
};
static OrigGui g_orig;

static void hueRotateChrome(PixBuf& p, int style) {
    for (auto& c : p.px) {
        if (c.b > c.r + 24 && c.b > c.g + 8 && c.b > 60) { // 蓝铬/琉璃像素
            uint8_t b = c.b, r = c.r, g = c.g;
            if (style == 1) { c.r = b; c.g = (uint8_t)(g * 0.45f); c.b = (uint8_t)(r * 0.45f); } // 苏军红
            else            { c.r = (uint8_t)(b * 0.74f); c.g = (uint8_t)(g * 0.40f); c.b = b; }  // 尤里紫
        }
    }
}

static void loadOrigGui() {
    if (g_orig.tried) return;
    g_orig.tried = true;
    PixBuf sb;
    if (!sb.loadFromFile("assets/gui/sidebar_allied.png")) return;
    if (sb.w != 171 || sb.h != 768) return;
    g_orig.sidebar[0] = sb.toTexture();
    SetTextureFilter(g_orig.sidebar[0], TEXTURE_FILTER_POINT); // 点采样：像素锐利（RA2 原作观感）
    for (int st = 1; st <= 2; st++) {
        PixBuf v = sb;
        hueRotateChrome(v, st);
        g_orig.sidebar[st] = v.toTexture();
        SetTextureFilter(g_orig.sidebar[st], TEXTURE_FILTER_POINT);
    }
    PixBuf bb;
    if (bb.loadFromFile("assets/gui/bottombar.png") && bb.w == 1366) {
        g_orig.bottombar = bb.toTexture();
        SetTextureFilter(g_orig.bottombar, TEXTURE_FILTER_POINT);
    }
    g_orig.ok = true;
}
// 原作贴图坐标映射：源图 1366x768（侧边栏 x1195..1366）→ 本游 1440x810（侧边栏 184）
struct OrigMap {
    int sbX;
    float sx, sy; // sx=184/171, sy=810/768
    int MX(float x) const { return sbX + (int)((x - 1195.0f) * sx); }
    int MY(float y) const { return (int)(y * sy); }
    int MW(float w) const { return (int)(w * sx); }
    int MH(float h) const { return (int)(h * sy); }
};

// ===================== 7 段 LED 数码管（RA2 资金牌标志） =====================
static void ledDigit(int d, int x, int y, int h, Color c) {
    static const uint8_t SEGS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    int t = std::max(2, h / 8);
    int w = h * 11 / 20, hh = h / 2;
    Color off{(uint8_t)(c.r / 9), (uint8_t)(c.g / 9), (uint8_t)(c.b / 9), 255}; // 灭段残影
    auto seg = [&](int i, int sx, int sy, int sw, int sh) {
        DrawRectangle(sx, sy, sw, sh, (SEGS[d] & (1 << i)) ? c : off);
    };
    seg(0, x + t, y, w - 2 * t, t);            // a 上横
    seg(1, x + w - t, y + t, t, hh - t);       // b 右上
    seg(2, x + w - t, y + hh, t, hh - t);      // c 右下
    seg(3, x + t, y + h - t, w - 2 * t, t);    // d 下横
    seg(4, x, y + hh, t, hh - t);              // e 左下
    seg(5, x, y + t, t, hh - t);               // f 左上
    seg(6, x + t, y + hh - t / 2, w - 2 * t, t); // g 中横
}
// 居中 LED 数字（原作资金牌：数字在黑槽内居中）
static void ledNumberCenter(int value, int cx, int y, int h, Color c) {
    char buf[16];
    snprintf(buf, sizeof buf, "%d", std::max(0, value));
    int dw = h * 11 / 20 + h / 6;
    int x = cx - (int)strlen(buf) * dw / 2;
    for (const char* p = buf; *p; p++, x += dw) ledDigit(*p - '0', x, y, h, c);
}

// ===================== 页签通用符号（RA2 原作：兵种剪影） =====================
static void tabSymbol(int i, Rectangle r, Color c) {
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    switch (i) {
        case 0: // 建筑：房屋剪影
            DrawTriangle({cx - 11, cy}, {cx + 11, cy}, {cx, cy - 10}, c);
            DrawRectangle((int)cx - 8, (int)cy, 16, 9, c);
            DrawRectangle((int)cx - 2, (int)cy + 3, 4, 6, Color{0, 0, 0, 60});
            break;
        case 1: // 防御：盾牌剪影
            DrawCircleSector({cx, cy - 2}, 8, 180, 360, 12, c);
            DrawTriangle({cx - 8, cy - 3}, {cx + 8, cy - 3}, {cx, cy + 10}, c);
            break;
        case 2: // 步兵：士兵剪影
            DrawCircle((int)cx - 1, (int)cy - 5, 3.0f, c);
            DrawTriangle({cx - 7, cy + 8}, {cx + 5, cy + 8}, {cx - 1, cy - 1}, c);
            DrawLineEx({cx + 2, cy - 7}, {cx + 8, cy + 5}, 2, c);
            break;
        default: // 载具：坦克剪影
            DrawRectangle((int)cx - 10, (int)cy - 1, 20, 5, c);
            DrawRectangle((int)cx - 4, (int)cy - 6, 8, 5, c);
            DrawLineEx({cx + 3, cy - 4}, {cx + 12, cy - 4}, 2, c);
            DrawCircle((int)cx - 6, (int)cy + 5, 2, c);
            DrawCircle((int)cx, (int)cy + 5, 2, c);
            DrawCircle((int)cx + 6, (int)cy + 5, 2, c);
            break;
    }
}

// ===================== 超武/伞兵 cameo 符号 =====================
static void swGlyph(int swIdx, Rectangle r, Color tint) {
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2 - 4;
    Color dim{(uint8_t)(tint.r / 2), (uint8_t)(tint.g / 2), (uint8_t)(tint.b / 2), 255};
    switch ((SWType)swIdx) {
        case SWType::Nuke: { // 蘑菇云
            DrawCircle((int)cx - 8, (int)cy - 6, 6, tint);
            DrawCircle((int)cx, (int)cy - 9, 7, tint);
            DrawCircle((int)cx + 8, (int)cy - 6, 6, tint);
            DrawTriangle({cx - 5, cy - 4}, {cx + 5, cy - 4}, {cx + 2, cy + 10}, tint);
            DrawTriangle({cx - 5, cy - 4}, {cx + 5, cy - 4}, {cx - 2, cy + 10}, tint);
            DrawEllipse((int)cx, (int)cy + 11, 9, 3, dim);
            break;
        }
        case SWType::Lightning: { // 风暴云 + 闪电
            DrawCircle((int)cx - 7, (int)cy - 4, 6, tint);
            DrawCircle((int)cx + 2, (int)cy - 7, 7, tint);
            DrawCircle((int)cx + 9, (int)cy - 3, 5, tint);
            DrawRectangle((int)cx - 10, (int)cy - 4, 24, 5, tint);
            DrawTriangle({cx + 1, cy + 1}, {cx + 7, cy + 1}, {cx + 1, cy + 9}, Color{255, 226, 80, 255});
            DrawTriangle({cx + 1, cy + 7}, {cx + 6, cy + 7}, {cx - 3, cy + 16}, Color{255, 226, 80, 255});
            break;
        }
        case SWType::IronCurtain: { // 铁幕：暗球 + 红弧
            DrawCircle((int)cx, (int)cy, 10, Color{30, 30, 36, 255});
            DrawCircleLines((int)cx, (int)cy, 10, tint);
            DrawRing({cx, cy}, 12, 14, 200, 340, 16, tint);
            DrawCircle((int)cx - 3, (int)cy - 3, 3, Color{80, 80, 92, 255});
            break;
        }
        case SWType::ChronoShift: { // 超时空：青蓝旋涡
            DrawRing({cx, cy}, 4, 6, 0, 300, 16, tint);
            DrawRing({cx, cy}, 8, 10, 60, 330, 16, tint);
            DrawRing({cx, cy}, 12, 13, 140, 360, 16, dim);
            DrawCircle((int)cx, (int)cy, 2, tint);
            break;
        }
        case SWType::GeneticMutator: { // 基因突变：绿色双螺旋
            for (int k = -8; k <= 8; k += 4) {
                DrawCircle((int)(cx - 5 - k * 0.3f), (int)(cy + k), 2, tint);
                DrawCircle((int)(cx + 5 + k * 0.3f), (int)(cy + k), 2, tint);
            }
            DrawLineEx({cx - 5, cy - 9}, {cx + 5, cy + 9}, 1.5f, dim);
            DrawLineEx({cx + 5, cy - 9}, {cx - 5, cy + 9}, 1.5f, dim);
            break;
        }
        default: { // 心灵控制：紫同心弧 + 瞳
            DrawRing({cx, cy}, 4, 6, 0, 360, 16, tint);
            DrawRing({cx, cy}, 8, 9, 0, 360, 16, tint);
            DrawRing({cx, cy}, 12, 13, 0, 360, 16, dim);
            DrawCircle((int)cx, (int)cy, 2.5f, Color{255, 120, 240, 255});
            break;
        }
    }
}
static void paradropGlyph(Rectangle r) {
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2 - 4;
    DrawCircleSector({cx, cy - 2}, 11, 180, 360, 14, Color{232, 220, 190, 255}); // 伞盖
    DrawLineEx({cx - 10, cy - 2}, {cx, cy + 10}, 1.2f, Color{200, 200, 200, 255});
    DrawLineEx({cx, cy - 2}, {cx, cy + 10}, 1.2f, Color{200, 200, 200, 255});
    DrawLineEx({cx + 10, cy - 2}, {cx, cy + 10}, 1.2f, Color{200, 200, 200, 255});
    DrawCircle((int)cx, (int)cy + 12, 3, Color{140, 200, 120, 255}); // 伞兵
}

// RA2 式金属按钮（菜单/局内菜单用）：竖向渐变面 + 棱台斜面 + 金框
bool Game::uiButton(Rectangle r, const char* text, bool enabled, bool active) {
    Vector2 m = mousePos();
    bool hover = CheckCollisionPointRec(m, r) && enabled;
    bool press = hover && mDown(MOUSE_LEFT_BUTTON);
    Color top = enabled ? (hover ? Color{78, 82, 92, 255} : Color{52, 56, 62, 255}) : Color{28, 30, 34, 255};
    Color bot = enabled ? (hover ? Color{48, 52, 58, 255} : Color{30, 32, 36, 255}) : Color{20, 22, 24, 255};
    if (active) { top = Color{92, 76, 40, 255}; bot = Color{52, 42, 22, 255}; }
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, top, bot);
    guiBevel(r, press);
    Color frame = !enabled ? Color{50, 52, 56, 255}
                : active ? GUI_GOLD_HI : (hover ? GUI_GOLD : Color{80, 76, 56, 255});
    DrawRectangleLinesEx(r, 1, frame);
    if (text && text[0]) {
        int tw = (int)MeasureTextEx(font, text, 14, 1).x;
        drawTextS(font, text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - 7), 14,
                  enabled ? (active || hover ? Color{255, 236, 160, 255} : Color{214, 218, 224, 255})
                          : Color{96, 98, 102, 255});
    }
    bool clicked = hover && mPressed(MOUSE_LEFT_BUTTON);
    if (clicked) g_sfx.play(Sfx::Click, 0.6f);
    return clicked;
}

std::vector<BldType> Game::tabBuildings() const {
    std::vector<BldType> v;
    Faction f = world.players[localPlayer].faction;
    if (uiTab == 0) {
        static const BldType mainB[] = {
            BldType::PowerPlant, BldType::TeslaReactor, BldType::BioReactor, BldType::OreRefinery, BldType::Barracks,
            BldType::WarFactory, BldType::Radar, BldType::AirForceCmd, BldType::NavalYard, BldType::BattleLab,
            BldType::NuclearReactor, BldType::OrePurifier, BldType::IndustrialPlant,
            BldType::CloningVat, BldType::ServiceDepot, BldType::Grinder, BldType::GapGenerator, BldType::SpySat, BldType::PsychicSensor,
            BldType::NukeSilo, BldType::WeatherDevice, BldType::IronCurtain, BldType::ChronoSphere,
            BldType::GeneticMutator, BldType::PsychicDominator,
        };
        for (BldType t : mainB)
            if (bldDef(t).factionMask & (1 << (int)f)) v.push_back(t);
    } else {
        static const BldType defB[] = {
            BldType::Pillbox, BldType::SentryGun, BldType::FlakCannon, BldType::GatlingCannon,
            BldType::PrismTower, BldType::TeslaCoil, BldType::PsychicTower, BldType::GrandCannon,
            BldType::PatriotMissile, BldType::Wall, BldType::BattleBunker, BldType::TankBunker,
        };
        for (BldType t : defB)
            if (bldDef(t).factionMask & (1 << (int)f)) v.push_back(t);
    }
    return v;
}

std::vector<UnitType> Game::tabUnits() const {
    std::vector<UnitType> v;
    Faction f = world.players[localPlayer].faction;
    for (int i = 0; i < (int)UnitType::COUNT; i++) {
        const UnitDef& u = unitDef((UnitType)i);
        if (!(u.factionMask & (1 << (int)f))) continue;
        int stBit = stolenTechBit((UnitType)i);
        if (stBit && !(world.players[localPlayer].stolenTech & stBit)) continue;
        if (uiTab == 2 && u.isInfantry()) v.push_back((UnitType)i);
        if (uiTab == 3 && !u.isInfantry()) v.push_back((UnitType)i);
    }
    return v;
}

void Game::drawHUD() {
    int sbX = SCREEN_W - sidebarW;
    Player& me = world.players[localPlayer];
    const int style = guiStyleOf(me.faction);
    const GuiPal& P = GUI_PAL[style];
    if (!g_chromeOk[style]) genChrome(style);
    if (!g_bbar.id) genBottomBar();
    const ChromeSet& C = g_chrome[style];
    loadOrigGui();
    const bool orig = g_orig.ok; // 原作贴图可用 → 1:1 布局；否则程序化兜底
    const OrigMap OM{sbX, sidebarW / 171.0f, SCREEN_H / 768.0f};
    auto lerpC = [](Color a, Color b, float t) {
        return Color{(uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t),
                     (uint8_t)(a.b + (b.b - a.b) * t), 255};
    };

    // ---- 布局度量（orig=原作 171x768 贴图映射；否则程序化度量） ----
    int CX, CW, mmX, mmY, mmW, mmH, Y_MODE, H_MODE, Y_TABS, H_TABS, TAB_W;
    int gridY, slotW, slotH, gap, gridBottom, visRows;
    const int cols = 2;
    if (orig) { // 源图实测坐标（tools/sbprobe.py）
        CX = OM.MX(1221); CW = OM.MW(62) * 2;            // 网格左槽列 x，内容宽
        mmX = OM.MX(1209); mmY = OM.MY(37); mmW = OM.MW(147); mmH = OM.MH(118); // 雷达内腔
        Y_MODE = OM.MY(162); H_MODE = OM.MH(32);         // 维修/出售穹带
        Y_TABS = OM.MY(194); H_TABS = OM.MH(29); TAB_W = OM.MW(34.5f);
        gridY = OM.MY(228); slotW = OM.MW(60); slotH = OM.MH(48); gap = 2;
        gridBottom = OM.MY(727);
        visRows = 10;                                    // 原作一屏 10 行
    } else {
        CX = sbX + 15; CW = 154;
        mmX = CX + 8; mmY = 68; mmW = 138; mmH = 138;
        Y_MODE = 226; H_MODE = 26;
        Y_TABS = 258; H_TABS = 28; TAB_W = 37;
        gridY = 292; slotW = 75; slotH = 72; gap = 4;
        gridBottom = 744;
        visRows = (gridBottom - gridY + gap) / (slotH + gap);
    }

    // ===================== 侧栏铬面（orig=原作贴图整条 1:1；否则程序化拼件兜底） =====================
    if (orig) {
        // 底部状态栏先画（原作：全宽黑条，侧边栏底盖覆盖其右端）
        if (g_orig.bottombar.id)
            DrawTexturePro(g_orig.bottombar, {0, 0, 1366, 33},
                           {0, (float)(SCREEN_H - BOTTOM_BAR_H), (float)SCREEN_W, (float)BOTTOM_BAR_H},
                           {0, 0}, 0, WHITE);
        else
            DrawRectangle(0, SCREEN_H - BOTTOM_BAR_H, SCREEN_W, BOTTOM_BAR_H, Color{8, 9, 12, 255});
        // 侧边栏整条原作贴图（资金槽/雷达框/琉璃穹带/页签/槽框/电力轨道/底盖全部即原作原图）
        DrawTexturePro(g_orig.sidebar[style], {0, 0, 171, 768},
                       {(float)sbX, 0, (float)sidebarW, (float)SCREEN_H}, {0, 0}, 0, WHITE);
        // 资金 LED（黑槽内居中，青白；原作数字位置）
        ledNumberCenter(me.money, OM.MX(1272), OM.MY(4), OM.MH(11),
                        me.money > 0 ? P.led : Color{255, 80, 60, 255});
        // 雷达上琉璃带左图标组：点击打开菜单（原作：选项按钮）
        if (CheckCollisionPointRec(mousePos(), {(float)OM.MX(1206), (float)OM.MY(18),
                                                (float)OM.MW(62), (float)OM.MH(15)}) &&
            mPressed(MOUSE_LEFT_BUTTON)) {
            showMenu = true;
            g_sfx.play(Sfx::Click, 0.6f);
        }
        // 雷达小地图（贴图框内腔）
        drawMinimap(mmX, mmY, mmW, mmH);

        // ---- 维修/出售穹带（贴图已含扳手|$；仅热点 + 悬停/激活叠加） ----
        {
            Rectangle band{(float)OM.MX(1206), (float)Y_MODE, (float)OM.MW(140), (float)H_MODE};
            bool hovL = CheckCollisionPointRec(mousePos(), {band.x, band.y, band.width / 2, band.height});
            bool hovR = CheckCollisionPointRec(mousePos(), {band.x + band.width / 2, band.y, band.width / 2, band.height});
            if (hovL || hovR)
                DrawRectangleRec({band.x + (hovR ? band.width / 2 : 0), band.y, band.width / 2, band.height},
                                 Color{255, 255, 255, 26});
            if (sideMode == 1) DrawRectangleRec({band.x, band.y, band.width / 2, band.height}, Color{120, 200, 255, 62});
            if (sideMode == 2) DrawRectangleRec({band.x + band.width / 2, band.y, band.width / 2, band.height}, Color{255, 170, 80, 62});
            if (hovL && mPressed(MOUSE_LEFT_BUTTON)) {
                sideMode = sideMode == 1 ? 0 : 1;
                if (sideMode == 1) message(TR(S::MsgRepairMode));
                g_sfx.play(Sfx::Click, 0.6f);
            }
            if (hovR && mPressed(MOUSE_LEFT_BUTTON)) {
                sideMode = sideMode == 2 ? 0 : 2;
                if (sideMode == 2) message(TR(S::MsgSellMode));
                g_sfx.play(Sfx::Click, 0.6f);
            }
        }
    } else {
        // ===================== 侧栏底 + 左右雕饰缘 =====================
        DrawRectangle(sbX, 0, sidebarW, SCREEN_H, P.bgDark);
        DrawRectangleGradientV(sbX, 0, 3, SCREEN_H, P.metal0, P.metal1); // 左缘银线
        DrawLine(sbX + 3, 0, sbX + 3, SCREEN_H, P.trim);
        BeginScissorMode(sbX + 170, 0, 14, SCREEN_H);                   // 右缘银蔓
        for (int ty = 0; ty < SCREEN_H; ty += 48) DrawTexture(C.edge, sbX + 170, ty, WHITE);
        EndScissorMode();
        // cameo 区近黑底
        DrawRectangle(CX - 4, gridY - 4, CW + 8, gridBottom - gridY + 8, Color{5, 6, 9, 255});

        // ===================== 顶部银雕 + 资金 LED =====================
        DrawTexture(C.header, sbX, 0, WHITE);
        ledNumberCenter(me.money, sbX + sidebarW / 2, 8, 13,
                        me.money > 0 ? P.led : Color{255, 80, 60, 255});
        // 图标带左组：点击打开菜单（原作：选项按钮）
        if (CheckCollisionPointRec(mousePos(), {(float)sbX + 20, 33, 58, 11}) && mPressed(MOUSE_LEFT_BUTTON)) {
            showMenu = true;
            g_sfx.play(Sfx::Click, 0.6f);
        }

        // ===================== 雷达 =====================
        DrawTexture(C.radar, CX, 60, WHITE);
        drawMinimap(mmX, mmY, mmW, mmH);
        DrawCircle(CX + 6, 218, 2, Color{90, 230, 140, 255}); // 框下指示 LED（装饰）
        DrawCircle(CX + 12, 218, 2, Color{60, 160, 220, 255});
        DrawCircle(CX + 18, 218, 2, Color{90, 230, 140, 255});

        // ===================== 中部雕饰托 =====================
        DrawTexture(C.cradle, sbX, 220, WHITE);

        // ===================== 维修/出售药丸 =====================
        {
            float px = (float)CX + 1, py = (float)Y_MODE;
            bool hovL = CheckCollisionPointRec(mousePos(), {px, py, 76, 26});
            bool hovR = CheckCollisionPointRec(mousePos(), {px + 76, py, 76, 26});
            int st = (sideMode != 0) ? 2 : ((hovL || hovR) ? 1 : 0);
            DrawTexture(C.pill[st], (int)px, (int)py, WHITE);
            Color sym = P.glyph;
            float cy = py + 13;
            float cx = px + 38; // 扳手（左半）
            DrawLineEx({cx - 8, cy + 8}, {cx + 5, cy - 5}, 4.5f, sym);
            DrawRing({cx + 7, cy - 7}, 3.0f, 5.5f, 100, 320, 10, sym);
            DrawCircle((int)cx - 8, (int)cy + 8, 3, sym);
            drawTextS(font, "$", (int)px + 107, (int)cy - 11, 24, sym); // $（右半）
            if (hovL && mPressed(MOUSE_LEFT_BUTTON)) {
                sideMode = sideMode == 1 ? 0 : 1;
                if (sideMode == 1) message(TR(S::MsgRepairMode));
                g_sfx.play(Sfx::Click, 0.6f);
            }
            if (hovR && mPressed(MOUSE_LEFT_BUTTON)) {
                sideMode = sideMode == 2 ? 0 : 2;
                if (sideMode == 2) message(TR(S::MsgSellMode));
                g_sfx.play(Sfx::Click, 0.6f);
            }
        }
    }

    // 悬停提示缓存
    std::string tipName, tipSub, tipReason;
    bool tipSet = false;
    Vector2 tipPos{0, 0};

    // ===================== 4 页签（orig=贴图页签+辉光叠加；否则铬面页签+剪影） =====================
    static const S tabIds[] = {S::TabBld, S::TabDef, S::TabInf, S::TabVeh};
    const int tabX0 = orig ? OM.MX(1209) : CX;
    const int tabPitch = orig ? TAB_W : TAB_W + 2;
    for (int i = 0; i < 4; i++) {
        Rectangle tr{(float)tabX0 + i * tabPitch, (float)Y_TABS, (float)TAB_W, (float)H_TABS};
        bool active = uiTab == i;
        bool hov = CheckCollisionPointRec(mousePos(), tr);
        if (orig) { // 贴图页签：激活=辉光增亮，悬停=微亮
            if (active) {
                DrawRectangleRec({tr.x + 2, tr.y + 2, tr.width - 4, tr.height - 4}, Color{150, 200, 255, 66});
                DrawRectangleLinesEx({tr.x + 1, tr.y + 1, tr.width - 2, tr.height - 2}, 1, Color{200, 230, 255, 200});
            } else if (hov) {
                DrawRectangleRec({tr.x + 2, tr.y + 2, tr.width - 4, tr.height - 4}, Color{255, 255, 255, 24});
            }
        } else {
            DrawTexture(C.tab[active ? 2 : (hov ? 1 : 0)], (int)tr.x, (int)tr.y, WHITE);
            tabSymbol(i, tr, P.glyph);
        }
        if (hov) {
            if (!tipSet) { tipSet = true; tipName = TR(tabIds[i]); tipPos = mousePos(); }
            if (mPressed(MOUSE_LEFT_BUTTON)) { uiTab = i; uiScroll = 0; g_sfx.play(Sfx::Click, 0.6f); }
        }
    }

    // ===================== 电力条（绿顶红底位置色，自底向上填充=剩余电力） =====================
    {
        int pbX, pbW, pbY0, pbY1;
        // 原作轨道凹槽实测（assets/gui/sidebar_allied.png 扫描）：内腔 x1200-1204, y214-757
        if (orig) { pbX = OM.MX(1200); pbW = OM.MW(5); pbY0 = OM.MY(214); pbY1 = OM.MY(757); }
        else { pbX = sbX + 6; pbW = 8; pbY0 = gridY - 2; pbY1 = gridBottom; }
        int ix = pbX + 1, iw = pbW - 2, iy0 = pbY0 + 1, ih = pbY1 - pbY0 - 2;
        // 内腔铺深色底：orig 模式覆盖贴图残留色条，非 orig 画整槽
        if (orig) DrawRectangle(ix, iy0, iw, ih, Color{8, 9, 12, 255});
        else {
            Rectangle ch{(float)pbX, (float)pbY0, (float)pbW, (float)(pbY1 - pbY0)};
            DrawRectangleRec(ch, Color{8, 9, 12, 255});
        }
        float spare = me.powerMade > 0 ? 1.0f - (float)me.powerUsed / me.powerMade : 0.0f;
        spare = std::clamp(spare, 0.0f, 1.0f);
        bool over = me.lowPower();
        int fillH = (int)(ih * spare);
        int fy = iy0 + ih - fillH;
        for (int y = fy; y < iy0 + ih; y += 3) { // 3px 段 + 1px 暗缝（原作细密分段）
            float t = (float)(y - iy0) / ih;     // 0=顶 1=底（位置定色：顶绿→中黄→底红）
            Color c = t < 0.50f ? lerpC(Color{70, 220, 80, 255}, Color{230, 220, 70, 255}, t * 2)
                      : t < 0.62f ? lerpC(Color{230, 220, 70, 255}, Color{235, 150, 50, 255}, (t - 0.50f) / 0.12f)
                                  : lerpC(Color{235, 150, 50, 255}, Color{230, 70, 40, 255}, (t - 0.62f) / 0.38f);
            DrawRectangle(ix, y, iw, 2, over && (world.tick / 8) % 2 ? Color{255, 60, 40, 255} : c);
        }
        if (!orig) {
            Rectangle ch{(float)pbX, (float)pbY0, (float)pbW, (float)(pbY1 - pbY0)};
            DrawRectangleLinesEx(ch, 1, over && (world.tick / 8) % 2 ? Color{255, 90, 70, 255} : P.trim);
        }
    }

    // ===================== 生产 cameo 网格 =====================
    struct GItem { uint8_t kind; int idx; };
    GItem items[128];
    int nItems = 0;
    if (uiTab == 1) {
        for (int i = 0; i < (int)SWType::COUNT; i++) {
            const SWDef& sd = swDef((SWType)i);
            if (!(bldDef(sd.fromBld).factionMask & (1 << (int)me.faction))) continue;
            if (!world.hasBld(localPlayer, sd.fromBld)) continue;
            items[nItems++] = {2, i};
        }
        if (world.hasParadropSource(localPlayer)) items[nItems++] = {3, 0};
    }
    if (uiTab <= 1) {
        for (BldType t : tabBuildings()) items[nItems++] = {0, (int)t};
    } else {
        for (UnitType t : tabUnits()) items[nItems++] = {1, (int)t};
    }

    int startRow = uiScroll;
    int idx = startRow * cols;
    int drawn = 0;
    auto slotShell = [&](Rectangle r, bool readyPulse, bool activeThis, bool targeting) {
        if (orig) { // 槽框已烘焙进贴图：仅叠加状态框
            if (targeting) DrawRectangleLinesEx({r.x + 1, r.y + 1, r.width - 2, r.height - 2}, 2, Color{255, 120, 90, 255});
            else if (readyPulse) DrawRectangleLinesEx({r.x + 1, r.y + 1, r.width - 2, r.height - 2}, 2,
                                                      ((world.tick / 12) % 2) ? Color{240, 220, 120, 255} : Color{150, 130, 60, 255});
            else if (activeThis) DrawRectangleLinesEx({r.x + 1, r.y + 1, r.width - 2, r.height - 2}, 1, Color{160, 150, 100, 255});
            return;
        }
        DrawRectangleRec(r, Color{5, 6, 9, 255});
        Color frame = targeting ? Color{255, 120, 90, 255}
                    : readyPulse ? (((world.tick / 12) % 2) ? Color{240, 220, 120, 255} : Color{150, 130, 60, 255})
                    : activeThis ? Color{160, 150, 100, 255}
                                 : Color{34, 38, 48, 255};
        DrawRectangleLinesEx(r, 1, frame);
    };
    auto veil = [&](Rectangle r, float frac) { // 建造/充能进度遮罩（自下而上退去）
        if (frac >= 1.0f) return;
        int vh = (int)(r.height * (1.0f - frac));
        DrawRectangle((int)r.x + 1, (int)r.y + 1, (int)r.width - 2, vh, Color{0, 0, 0, 150});
    };
    auto nameStrip = [&](Rectangle r, const char* nm, bool bright) { // 名称（叠于图标底部，白字黑影）
        int nw = (int)MeasureTextEx(font, nm, 11, 1).x;
        drawTextS(font, nm, (int)(r.x + r.width / 2 - nw / 2), (int)(r.y + r.height - 13), 11,
                  bright ? Color{240, 242, 248, 255} : Color{120, 124, 132, 255});
    };

    for (int row = 0; row < visRows; row++) {
        for (int col = 0; col < cols; col++, drawn++) {
            int ix = CX + col * (slotW + gap);
            int iy = gridY + row * (slotH + gap);
            Rectangle r{(float)ix, (float)iy, (float)slotW, (float)slotH};
            if (drawn + idx >= nItems) { // 空槽（orig=贴图已含空槽；否则近黑）
                if (!orig) {
                    DrawRectangleRec(r, Color{5, 6, 9, 255});
                    DrawRectangleLinesEx(r, 1, Color{22, 25, 32, 255});
                }
                continue;
            }
            GItem it = items[drawn + idx];
            bool hov = CheckCollisionPointRec(mousePos(), r);

            if (it.kind == 2 || it.kind == 3) { // ---- 超武 / 伞兵 cameo ----
                bool isPara = it.kind == 3;
                bool ready = isPara ? me.paradropReady : me.swReady[it.idx];
                bool targeting = isPara ? targetingParadrop : (targetingSW == (SWType)it.idx);
                slotShell(r, ready, false, targeting);
                if (!orig) // 贴图槽已含深色底；程序化槽需补底
                    DrawRectangle((int)r.x + 2, (int)r.y + 2, (int)r.width - 4, (int)r.height - 16,
                                  Color{22, 26, 34, 255});
                if (isPara) paradropGlyph(r);
                else swGlyph(it.idx, r, ready ? Color{255, 120, 90, 255} : Color{150, 90, 80, 255});
                int chargeT = isPara ? World::PARADROP_TIME : swDef((SWType)it.idx).chargeTime;
                int charge = isPara ? me.paradropCharge : me.swCharge[it.idx];
                if (!ready) {
                    veil(r, (float)charge / chargeT);
                    int secs = (chargeT - charge) / LOGIC_FPS;
                    const char* ct = TextFormat("%d:%02d", secs / 60, secs % 60);
                    drawTextS(font, ct, (int)(r.x + r.width / 2 - MeasureTextEx(font, ct, 14, 1).x / 2),
                              (int)(r.y + r.height / 2 - 12), 14, WHITE);
                } else if ((world.tick / 15) % 2) {
                    int rw = (int)MeasureTextEx(font, TR(S::Ready), 12, 1).x;
                    drawTextS(font, TR(S::Ready), (int)(r.x + r.width / 2 - rw / 2), (int)r.y + 3, 12,
                              Color{160, 255, 160, 255});
                }
                const char* nm = isPara ? TR(S::Paradrop) : swName((SWType)it.idx);
                nameStrip(r, nm, ready);
                if (hov) {
                    if (!tipSet) {
                        tipSet = true; tipName = nm; tipPos = mousePos();
                        if (ready) tipSub = TR(S::ClickTarget);
                        else tipSub = TextFormat("%d:%02d", (chargeT - charge) / LOGIC_FPS / 60,
                                                 ((chargeT - charge) / LOGIC_FPS) % 60);
                    }
                    if (ready && mPressed(MOUSE_LEFT_BUTTON)) {
                        if (isPara) {
                            targetingParadrop = !targetingParadrop;
                            targetingSW = SWType::COUNT;
                            if (targetingParadrop) message(TR(S::MsgParadropTarget));
                        } else {
                            targetingSW = targeting ? SWType::COUNT : (SWType)it.idx;
                            targetingParadrop = false;
                            if (targetingSW != SWType::COUNT) message(TR(S::MsgSelectTargetSW));
                        }
                        g_sfx.play(Sfx::Click, 0.6f);
                    }
                }
                continue;
            }

            // ---- 建筑/单位 cameo ----
            bool isUnit = it.kind == 1;
            int typeIdx = it.idx;
            const Sprite& icon = isUnit ? g_sprites.iconUnit((UnitType)typeIdx, me.colorId)
                                        : g_sprites.iconBld((BldType)typeIdx, me.colorId);
            const char* name = isUnit ? unitName((UnitType)typeIdx) : bldName((BldType)typeIdx);
            int cost = isUnit ? unitDef((UnitType)typeIdx).cost : bldDef((BldType)typeIdx).cost;
            int time = isUnit ? unitDef((UnitType)typeIdx).buildTime : bldDef((BldType)typeIdx).buildTime;
            ProdItem& prod = isUnit ? me.unitProd[unitDef((UnitType)typeIdx).prodCat()] : me.bldProd;
            bool canBuild = false, readyThis = false, activeThis = false;
            int queuedN = 0;
            std::string reason;
            if (!isUnit) {
                const BldDef& d = bldDef((BldType)typeIdx);
                bool hasCY = world.hasBld(localPlayer, BldType::ConYard);
                bool preOk = world.prereqMet(localPlayer, d);
                canBuild = hasCY && preOk && me.money >= d.cost && !me.bldProd.active;
                if (!hasCY) reason = TextFormat(TR(S::TipRequireFmt), bldName(BldType::ConYard));
                else if (!preOk) {
                    if (d.prereq != BldType::COUNT && !world.hasBld(localPlayer, d.prereq))
                        reason = TextFormat(TR(S::TipRequireFmt), bldName(d.prereq));
                    else if (d.countryReq != Country::None)
                        reason = TextFormat(TR(S::TipRequireFmt), countryName(d.countryReq));
                } else if (me.money < d.cost) reason = TR(S::TipNoMoney);
            } else {
                const UnitDef& u = unitDef((UnitType)typeIdx);
                int cat = u.prodCat();
                for (int q : me.unitQueue[cat])
                    if (q == typeIdx) queuedN++;
                bool preOk = world.unitPrereqMet(localPlayer, u);
                bool facOk = world.hasFactoryFor(localPlayer, u);
                canBuild = preOk && facOk && me.money >= u.cost;
                if (!preOk) {
                    if (u.prereq != BldType::COUNT && !world.hasBld(localPlayer, u.prereq))
                        reason = TextFormat(TR(S::TipRequireFmt), bldName(u.prereq));
                    else if (u.countryReq != Country::None && me.country != u.countryReq
                             && me.secretLabUnlock != (int)u.countryReq)
                        reason = TextFormat(TR(S::TipRequireFmt), countryName(u.countryReq));
                } else if (!facOk) {
                    for (int b = 0; b < (int)BldType::COUNT && reason.empty(); b++)
                        if (isFactoryFor((BldType)b, u))
                            reason = TextFormat(TR(S::TipRequireFmt), bldName((BldType)b));
                } else if (me.money < u.cost) reason = TR(S::TipNoMoney);
            }
            activeThis = prod.active && prod.typeIdx == typeIdx && prod.isUnit == isUnit;
            readyThis = activeThis && prod.ready;
            slotShell(r, readyThis, activeThis, false);
            // 图标铺入（orig=精确填槽内腔，原作满幅 cameo；否则等比居中，名称叠于底部）
            if (orig) {
                DrawTexturePro(icon.tex, {0, 0, (float)icon.tex.width, (float)icon.tex.height},
                               {r.x + 3, r.y + 3, r.width - 6, r.height - 6}, {0, 0}, 0,
                               canBuild || activeThis ? WHITE : Color{88, 88, 92, 255});
            } else {
                float availW = r.width - 4, availH = r.height - 4;
                float isc = std::min(availW / icon.tex.width, availH / icon.tex.height);
                float dw = icon.tex.width * isc, dh = icon.tex.height * isc;
                DrawTexturePro(icon.tex, {0, 0, (float)icon.tex.width, (float)icon.tex.height},
                               {r.x + (r.width - dw) / 2, r.y + 2 + (availH - dh) / 2, dw, dh}, {0, 0}, 0,
                               canBuild || activeThis ? WHITE : Color{88, 88, 92, 255});
            }
            if (activeThis && !prod.ready) veil(r, (float)prod.progress / time);
            if (readyThis && (world.tick / 15) % 2) { // Ready 叠于图标顶部（原作位置）
                int rw = (int)MeasureTextEx(font, TR(S::Ready), 12, 1).x;
                drawTextS(font, TR(S::Ready), (int)(r.x + r.width / 2 - rw / 2), (int)r.y + 3, 12,
                          Color{160, 255, 160, 255});
            }
            // 排队数量角标（右上角）
            int totalN = queuedN + (activeThis && isUnit ? 1 : 0);
            if (isUnit && totalN > 0) {
                DrawRectangle((int)r.x + (int)r.width - 18, (int)r.y + 2, 16, 13, Color{0, 0, 0, 170});
                drawTextS(font, TextFormat("%d", totalN), (int)r.x + (int)r.width - 13, (int)r.y + 3, 11,
                          Color{255, 220, 100, 255});
            }
            nameStrip(r, name, canBuild || activeThis);
            if (hov) {
                DrawRectangleLinesEx({r.x + 1, r.y + 1, r.width - 2, r.height - 2}, 1,
                                     Color{220, 200, 140, 140});
                if (!tipSet) {
                    tipSet = true; tipName = name; tipPos = mousePos();
                    tipSub = TextFormat(TR(S::TipCostTimeFmt), cost, time / LOGIC_FPS);
                    if (!reason.empty()) tipReason = reason;
                }
                if (mPressed(MOUSE_LEFT_BUTTON)) {
                    if (readyThis) {
                        me.placingBld = (BldType)typeIdx;
                        placing = true;
                        message(TR(S::MsgPlaceBld));
                    } else if (!canBuild) { message(TR(S::MsgCannotBuild)); }
                    else if (isUnit || !activeThis) {
                        bool ok = isUnit ? (world.unitQueuedCount(localPlayer, unitDef((UnitType)typeIdx).prodCat()) < 8)
                                         : !me.bldProd.active;
                        if (!ok) { message(TR(S::MsgQueueBusy)); }
                        else {
                            World::Cmd c;
                            c.type = isUnit ? World::Cmd::StartUnitProd : World::Cmd::StartBldProd;
                            c.a = typeIdx;
                            issueCmd(c);
                        }
                    }
                }
                if (mPressed(MOUSE_RIGHT_BUTTON)) {
                    if (isUnit && totalN > 0) {
                        World::Cmd c; c.type = World::Cmd::CancelUnitProd; c.a = typeIdx;
                        issueCmd(c);
                        message(TR(S::MsgCanceledOne));
                    } else if (!isUnit && activeThis) {
                        World::Cmd c; c.type = World::Cmd::CancelBldProd;
                        issueCmd(c);
                        message(TR(S::MsgCanceledProd));
                    }
                }
            }
        }
    }

    // ---- 滚动箭头（超出一页时；orig=网格右缘装饰区竖排，避免压底栏；否则网格底右端 ▲▼） ----
    {
        int totalRows = (nItems + cols - 1) / cols;
        int maxScroll = std::max(0, totalRows - visRows);
        if (uiScroll > maxScroll) uiScroll = maxScroll;
        if (maxScroll > 0) {
            auto arrow = [&](int i, bool up) {
                Rectangle r = orig
                    ? Rectangle{(float)CX + CW + 3, (float)(gridBottom - 44 + i * 22), 16, 20}
                    : Rectangle{(float)CX + CW - 52 + i * 28, (float)gridBottom + 3, 24, 16};
                bool en = up ? uiScroll > 0 : uiScroll < maxScroll;
                bool hov = CheckCollisionPointRec(mousePos(), r) && en;
                Color c = !en ? Color{50, 54, 60, 255} : (hov ? P.chromeHi : P.chrome0);
                DrawRectangleRec(r, Color{10, 12, 16, 230});
                DrawRectangleLinesEx(r, 1, P.trim);
                float cx = r.x + r.width / 2, cy = r.y + r.height / 2;
                if (up) DrawTriangle({cx - 5, cy + 3}, {cx + 5, cy + 3}, {cx, cy - 4}, c);
                else DrawTriangle({cx - 5, cy - 3}, {cx + 5, cy - 3}, {cx, cy + 4}, c);
                if (hov && mPressed(MOUSE_LEFT_BUTTON)) { uiScroll += up ? -1 : 1; g_sfx.play(Sfx::Click, 0.5f); }
            };
            arrow(0, true);
            arrow(1, false);
        }
    }

    // ===================== 底部穹顶（orig=贴图已含） =====================
    if (!orig) DrawTexture(C.cap, sbX, SCREEN_H - 44, WHITE);

    // ===================== 底部状态栏（左功能图标，右超武计时） =====================
    {
        int by = SCREEN_H - BOTTOM_BAR_H;
        const float BB = SCREEN_W / 1366.0f; // 原作底栏源图 → 本游宽度
        if (!orig) DrawTexture(g_bbar, 0, by, WHITE); // orig 底栏已在侧栏贴图前绘制
        Vector2 mp = mousePos();
        auto barHov = [&](int x, int w) {
            return CheckCollisionPointRec(mp, {(float)x, (float)by, (float)w, (float)BOTTOM_BAR_H});
        };
        const char* tip = nullptr;
        // ---- RA2 底栏：左侧菜单药丸 + 单位指令按钮组（停止/部署/散开/警戒/路径点） ----
        // 热点（orig=原作底栏图标实测位 ×BB；否则程序化底栏位）
        int hxMenu = orig ? (int)(4 * BB) : 6, hwMenu = orig ? (int)(58 * BB) : 52;
        if (barHov(hxMenu, hwMenu)) { // 银药丸：菜单
            tip = TR(S::GameMenu);
            if (mPressed(MOUSE_LEFT_BUTTON)) { showMenu = true; g_sfx.play(Sfx::Click, 0.6f); }
        }
        // 铬面按钮（RA2 风格：金属渐变 + 顶高光/底暗影 + 斜角，填满条高）
        auto chromeBtn = [&](Rectangle r, bool hov, bool press, bool active, bool en) {
            if (!en) { // 禁用：暗色压平
                DrawRectangleRec(r, Color{22, 24, 28, 220});
                DrawRectangleLinesEx(r, 1, Color{60, 62, 68, 160});
                return;
            }
            Color top = press ? Color{58, 62, 70, 255} : hov ? Color{172, 178, 190, 255} : Color{132, 138, 150, 255};
            Color bot = press ? Color{96, 102, 112, 255} : hov ? Color{88, 92, 102, 255} : Color{58, 62, 70, 255};
            DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, top, bot);
            // 斜角：四角 1px 削掉（暗色）
            Color cc{10, 11, 14, 255};
            DrawPixel((int)r.x, (int)r.y, cc); DrawPixel((int)(r.x + r.width - 1), (int)r.y, cc);
            DrawPixel((int)r.x, (int)(r.y + r.height - 1), cc); DrawPixel((int)(r.x + r.width - 1), (int)(r.y + r.height - 1), cc);
            if (press) { // 内凹：顶/左暗、底/右亮
                DrawLine((int)r.x, (int)r.y, (int)(r.x + r.width), (int)r.y, Color{30, 32, 38, 255});
                DrawLine((int)r.x, (int)r.y, (int)r.x, (int)(r.y + r.height), Color{30, 32, 38, 255});
                DrawLine((int)r.x, (int)(r.y + r.height - 1), (int)(r.x + r.width), (int)(r.y + r.height - 1), Color{150, 156, 168, 255});
            } else {     // 外凸：顶/左亮、底/右暗
                DrawLine((int)r.x, (int)r.y, (int)(r.x + r.width), (int)r.y, Color{225, 230, 240, 255});
                DrawLine((int)r.x, (int)r.y, (int)r.x, (int)(r.y + r.height), Color{200, 205, 215, 255});
                DrawLine((int)r.x, (int)(r.y + r.height - 1), (int)(r.x + r.width), (int)(r.y + r.height - 1), Color{26, 28, 34, 255});
                DrawLine((int)(r.x + r.width - 1), (int)r.y, (int)(r.x + r.width - 1), (int)(r.y + r.height), Color{26, 28, 34, 255});
            }
            if (active) DrawRectangleLinesEx({r.x + 1, r.y + 1, r.width - 2, r.height - 2}, 1, GUI_GOLD);
        };
        // 单位指令按钮（未选单位时禁用置灰）；orig 模式槽位对齐贴图自带图标中心
        struct CmdBtn { int x, w; S name; int key; };
        const CmdBtn btns[] = {
            {orig ? (int)(52 * BB) : 132, orig ? (int)(30 * BB) : 30, S::KaStop, keyBind[KA_Stop]},
            {orig ? (int)(88 * BB) : 164, orig ? (int)(30 * BB) : 30, S::KaDeploy, keyBind[KA_Deploy]},
            {orig ? (int)(140 * BB) : 196, orig ? (int)(30 * BB) : 30, S::KaScatter, keyBind[KA_Scatter]},
            {orig ? (int)(191 * BB) : 228, orig ? (int)(30 * BB) : 30, S::KaGuard, keyBind[KA_Guard]},
            {orig ? (int)(244 * BB) : 260, orig ? (int)(30 * BB) : 30, S::KaWaypoint, keyBind[KA_Waypoint]},
        };
        bool hasSel = !sel.empty();
        for (int i = 0; i < 5; i++) {
            const CmdBtn& b = btns[i];
            Rectangle r{(float)b.x, (float)by + 2, (float)b.w, (float)BOTTOM_BAR_H - 4};
            bool en = hasSel;
            bool hov = en && CheckCollisionPointRec(mp, r);
            bool press = hov && mDown(MOUSE_LEFT_BUTTON);
            bool active = (i == 4 && waypointLatch); // 路径点模式锁存高亮
            // 铬面底（orig：先铺贴图底色盖住自带蓝图标，保证按钮视觉统一）
            if (orig) DrawRectangleRec({r.x - 1, r.y, r.width + 2, r.height}, Color{14, 15, 19, 255});
            chromeBtn(r, hov, press, active, en);
            // 图标（矢量象形，RA2 指令语义；大号居中）
            Color ic = en ? (press ? Color{255, 240, 200, 255} : Color{240, 236, 220, 255}) : Color{110, 110, 105, 140};
            int cx = (int)(r.x + r.width / 2), cy = (int)(r.y + r.height / 2) - 1;
            switch (i) {
                case 0: // 停止：实心方块
                    DrawRectangle(cx - 6, cy - 6, 12, 12, ic);
                    DrawRectangleLinesEx({(float)cx - 6, (float)cy - 6, 12, 12}, 1, Color{20, 20, 20, 200});
                    break;
                case 1: // 部署：上下展开双箭头
                    DrawTriangle({(float)cx - 7, (float)cy - 1}, {(float)cx + 7, (float)cy - 1}, {(float)cx, (float)cy + 7}, ic);
                    DrawTriangle({(float)cx - 7, (float)cy + 1}, {(float)cx + 7, (float)cy + 1}, {(float)cx, (float)cy - 7}, ic);
                    break;
                case 2: // 散开：四角外指箭头
                    for (int s1 = -1; s1 <= 1; s1 += 2)
                        for (int s2 = -1; s2 <= 1; s2 += 2) {
                            int ax = cx + s1 * 8, ay = cy + s2 * 7;
                            DrawTriangle({(float)ax, (float)ay}, {(float)(cx + s1 * 2), (float)(cy + s2 * 6)},
                                         {(float)(cx + s1 * 6), (float)(cy + s2 * 2)}, ic);
                        }
                    DrawCircle(cx, cy, 1.8f, ic);
                    break;
                case 3: // 警戒：盾形（圆 + 中竖）
                    DrawCircleLines(cx, cy, 7.5f, ic);
                    DrawCircleLines(cx, cy, 6.5f, ic);
                    DrawLine(cx, cy - 7, cx, cy + 7, ic);
                    break;
                case 4: // 路径点：旗帜
                    DrawLine(cx - 5, cy - 7, cx - 5, cy + 8, ic);
                    DrawTriangle({(float)cx - 4, (float)cy - 7}, {(float)cx - 4, (float)cy - 1},
                                 {(float)cx + 7, (float)cy - 4}, active ? Color{255, 220, 90, 255} : ic);
                    break;
            }
            // 热键字母（右下角小字）
            if (b.key > 0) drawTextS(font, keyName(b.key), (int)(r.x + r.width - 10), (int)(r.y + r.height - 11), 9, ic);
            // 交互
            if (hov) {
                tip = TR(b.name);
                if (mPressed(MOUSE_LEFT_BUTTON)) {
                    World::Cmd c;
                    c.ids = sel;
                    switch (i) {
                        case 0: c.type = World::Cmd::Stop; issueCmd(c); break;
                        case 1: cmdDeploySel(); break;
                        case 2: c.type = World::Cmd::Scatter; issueCmd(c); message(TR(S::MsgScatter)); break;
                        case 3: c.type = World::Cmd::Guard; issueCmd(c); message(TR(S::MsgGuard)); break;
                        case 4:
                            waypointLatch = !waypointLatch;
                            message(waypointLatch ? TR(S::MsgWaypointOn) : TR(S::MsgWaypointOff));
                            break;
                    }
                    g_sfx.play(Sfx::Click, 0.6f);
                }
            }
        }
        if (tip && !tipSet) { tipSet = true; tipName = tip; tipPos = mp; }
        // 超武计时（右侧红字；就绪绿闪）
        for (int i = 0; i < (int)SWType::COUNT; i++) {
            const SWDef& sd = swDef((SWType)i);
            if (!(bldDef(sd.fromBld).factionMask & (1 << (int)me.faction))) continue;
            if (!world.hasBld(localPlayer, sd.fromBld)) continue;
            const char* nm = swName((SWType)i);
            char tb[64];
            const char* txt;
            Color tc;
            if (me.swReady[i]) {
                snprintf(tb, sizeof tb, "%s %s", nm, TR(S::Ready));
                txt = tb;
                tc = (world.tick / 15) % 2 ? Color{140, 255, 140, 255} : Color{60, 160, 60, 255};
            } else {
                int secs = (sd.chargeTime - me.swCharge[i]) / LOGIC_FPS;
                snprintf(tb, sizeof tb, "%s %d:%02d", nm, secs / 60, secs % 60);
                txt = tb;
                tc = Color{255, 70, 56, 255};
            }
            int tw = (int)MeasureTextEx(font, txt, 13, 1).x;
            drawTextS(font, txt, sbX - tw - 12, by + (orig ? 10 : 7), 13, tc);
            break; // 只显示第一个
        }
    }

    // ---- 悬停提示（光标旁浮动文字，RA2 风格深色底+金边，避免压在图标上看不清） ----
    if (tipSet) {
        int tx = (int)tipPos.x + 16, ty = (int)tipPos.y + 6;
        int wname = (int)MeasureTextEx(font, tipName.c_str(), 15, 1).x;
        if (tx + wname > sbX - 4) tx = (int)tipPos.x - wname - 14;
        int wsub = tipSub.empty() ? 0 : (int)MeasureTextEx(font, tipSub.c_str(), 12, 1).x;
        int wreas = tipReason.empty() ? 0 : (int)MeasureTextEx(font, tipReason.c_str(), 12, 1).x;
        int bw = wname;
        if (wsub > bw) bw = wsub;
        if (wreas > bw) bw = wreas;
        bw += 14;
        int bh = 24 + (tipSub.empty() ? 0 : 15) + (tipReason.empty() ? 0 : 15);
        if (tx + bw > SCREEN_W) tx = SCREEN_W - bw - 2;
        if (tx < 2) tx = 2;
        DrawRectangle(tx - 7, ty - 5, bw, bh, Color{10, 12, 16, 224});
        DrawRectangleLinesEx({(float)(tx - 7), (float)(ty - 5), (float)bw, (float)bh}, 1, GUI_GOLD);
        drawTextS(font, tipName.c_str(), tx, ty, 15, Color{255, 240, 200, 255});
        int ly = ty + 17;
        if (!tipSub.empty()) { drawTextS(font, tipSub.c_str(), tx, ly, 12, Color{200, 205, 215, 255}); ly += 14; }
        if (!tipReason.empty()) drawTextS(font, tipReason.c_str(), tx, ly, 12, Color{255, 110, 90, 255});
    }

    // ===================== 消息与战役目标（左上角） =====================
    if (msgTimer > 0)
        drawTextS(font, msg.c_str(), 8, 8, 15, Color{240, 236, 220, 255});

    if (campaignMission >= 0 && !gameOver) {
        const MissionDef& md = missionTable()[campaignMission];
        std::string obj = missionName(campaignMission);
        obj += " · ";
        if (!objectiveText.empty()) {
            obj += objectiveText;
        } else if (md.objective == 1) {
            int remain = (md.objectiveTick - (int)world.tick) / LOGIC_FPS;
            if (remain < 0) remain = 0;
            obj += TextFormat(TR(S::ObjHoldFmt), remain / 60, remain % 60);
        } else if (nextWave < md.waves.size()) {
            obj += TextFormat(TR(S::ObjWaveFmt), (int)nextWave + 1, (int)md.waves.size());
        } else {
            obj += TR(S::ObjElimAll);
        }
        drawTextS(font, obj.c_str(), 8, 28, 13, Color{230, 200, 130, 255});
    }

    // ---- F3 帧率显示 ----
    if (showFps) {
        int alive = 0;
        for (auto& e : world.ents) if (e.alive) alive++;
        drawTextS(font, TextFormat("%d FPS  %.1f ms  ents %d  tick %llu", GetFPS(), GetFrameTime() * 1000.0,
                                   alive, (unsigned long long)world.tick),
                  8, 48, 13, Color{140, 230, 255, 255});
    }

    // 暂停/菜单/结算
    if (paused && !gameOver) {
        drawTextF(font, TR(S::Paused), SCREEN_W / 2 - 30, SCREEN_H / 2, 28, WHITE);
    }
    if (showMenu || gameOver) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 160});
        int mw = 320, mh = 426;
        int mx = SCREEN_W / 2 - mw / 2, my = SCREEN_H / 2 - mh / 2;
        guiPanel(mx, my, mw, mh);
        if (gameOver) {
            const char* t = victory ? TR(S::Victory) : TR(S::Defeat);
            drawTextF(font, t, mx + mw / 2 - 40, my + 24, 34, victory ? Color{120, 255, 120, 255} : RED);
        } else {
            drawTextF(font, TR(S::GameMenu), mx + mw / 2 - 40, my + 20, 22, WHITE);
        }
        auto restart = [&]() {
            if (campaignMission >= 0) newCampaignGame(campaignMission);
            else newGame((uint64_t)time(nullptr));
            showMenu = false;
        };
        if (uiButton({(float)mx + 60, (float)my + 72, 200, 32}, gameOver ? TR(S::PlayAgain) : TR(S::Continue),
                     !(gameOver && netGame))) {
            if (gameOver) restart();
            else showMenu = false;
        }
        if (uiButton({(float)mx + 60, (float)my + 114, 200, 32},
                     TextFormat("%s (%s)", TR(S::SaveProgress), keyName(keyBind[KA_QuickSave])), !gameOver && !netGame)) {
            message(saveGameFile(QUICKSAVE_PATH) ? TR(S::MsgSaved) : TR(S::MsgSaveFail));
            showMenu = false;
        }
        if (uiButton({(float)mx + 60, (float)my + 156, 200, 32},
                     TextFormat("%s (%s)", TR(S::LoadProgress), keyName(keyBind[KA_QuickLoad])), !gameOver && !netGame)) {
            message(loadGameFile(QUICKSAVE_PATH) ? TR(S::MsgLoaded) : TR(S::MsgLoadFail));
            showMenu = false;
        }
        if (uiButton({(float)mx + 60, (float)my + 198, 200, 32}, TR(S::Settings), true)) {
            settingsFromGame = true;
            showMenu = false;
            phase = Phase::Settings;
        }
        if (uiButton({(float)mx + 60, (float)my + 240, 200, 32}, TR(S::Restart), !netGame)) restart();
        if (uiButton({(float)mx + 60, (float)my + 282, 200, 32}, TR(S::BackToMain), true)) {
            if (netGame) netLeave();
            else phase = Phase::MainMenu;
            showMenu = false;
        }
        if (uiButton({(float)mx + 60, (float)my + 324, 200, 32}, TR(S::ExitGame), true)) {
            CloseWindow();
            exit(0);
        }
    }
}

void Game::updateMinimap() {
    if (--minimapTimer > 0) return;
    minimapTimer = 6;
    BeginTextureMode(minimap);
    ClearBackground(BLACK);
    int w = world.map.w, h = world.map.h;
    float sc = 256.0f / std::max(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            FogState fs = world.map.fogAt(localPlayer, x, y);
            if (fs == FOG_UNSEEN) continue;
            const Cell& c = world.map.at(x, y);
            Color col;
            switch (c.terrain) {
                case Terrain::Water: col = Color{30, 60, 140, 255}; break;
                case Terrain::Rough: col = Color{110, 96, 70, 255}; break;
                case Terrain::Ore:   col = Color{200, 160, 40, 255}; break;
                case Terrain::Gems:  col = Color{60, 210, 110, 255}; break;
                case Terrain::Bridge: col = Color{160, 110, 60, 255}; break;
                default:             col = Color{70, 105, 55, 255}; break;
            }
            if (fs == FOG_SEEN) { col.r /= 2; col.g /= 2; col.b /= 2; }
            DrawRectangle((int)(x * sc), (int)(y * sc), (int)sc + 1, (int)sc + 1, col);
        }
    for (const World::Ent& e : world.ents) {
        if (!e.alive || e.player < 0) continue;
        FogState fs = world.map.fogAt(localPlayer, (int)e.x, (int)e.y);
        if (e.player != localPlayer && fs != FOG_VISIBLE) continue;
        Color col = HOUSE_COLORS[world.players[e.player].colorId];
        if (e.isBuilding) DrawRectangle((int)(e.x * sc) - 1, (int)(e.y * sc) - 1, 4, 4, col);
        else DrawRectangle((int)(e.x * sc), (int)(e.y * sc), 2, 2, col);
    }
    EndTextureMode();
}

// RA2 原作：雷达需雷达类建筑在线且电力充足
bool Game::radarOnline() const {
    const Player& me = world.players[localPlayer];
    if (me.lowPower()) return false;
    return world.hasBld(localPlayer, BldType::Radar)
        || world.hasBld(localPlayer, BldType::AirForceCmd)
        || world.hasBld(localPlayer, BldType::SpySat);
}

void Game::drawMinimap(int mmX, int mmY, int mmW, int mmH) {
    int w = world.map.w, h = world.map.h;
    float scTex = 256.0f / std::max(w, h); // 与 updateMinimap 烘焙一致
    float srcW = w * scTex, srcH = h * scTex;
    if (!radarOnline()) {
        DrawRectangle(mmX, mmY, mmW, mmH, Color{6, 8, 10, 255});
        for (int i = 0; i < mmW; i += 4)
            for (int j = 0; j < mmH; j += 4) {
                uint32_t v = ((uint32_t)(i * 31 + j * 17) ^ (uint32_t)(world.tick / 4)) * 0x5bd1e995u;
                if ((v >> 13) % 23 == 0)
                    DrawRectangle(mmX + i, mmY + j, 2, 2, Color{20, 30, 26, 255});
            }
        if ((world.tick / 16) % 2) {
            const char* t = TR(S::RadarOffline);
            int tw = (int)MeasureTextEx(font, t, 13, 1).x;
            drawTextS(font, t, mmX + mmW / 2 - tw / 2, mmY + mmH / 2 - 7, 13, Color{220, 70, 56, 255});
        }
        return;
    }
    // 地图区拉伸铺满雷达内腔（原作雷达非等比）
    DrawTexturePro(minimap.texture,
                   {0, 0, srcW, -srcH},
                   {(float)mmX, (float)mmY, (float)mmW, (float)mmH}, {0, 0}, 0, WHITE);
    float scX = mmW / (float)w, scY = mmH / (float)h;
    // 摄像机视野框
    int viewW = SCREEN_W - sidebarW;
    int t0x, t0y, t1x, t1y;
    screenToTile(camX, camY, t0x, t0y);
    screenToTile(camX + viewW, camY + SCREEN_H, t1x, t1y);
    int t2x, t2y, t3x, t3y;
    screenToTile(camX + viewW, camY, t2x, t2y);
    screenToTile(camX, camY + SCREEN_H, t3x, t3y);
    int minX = std::min({t0x, t1x, t2x, t3x}), maxX = std::max({t0x, t1x, t2x, t3x});
    int minY = std::min({t0y, t1y, t2y, t3y}), maxY = std::max({t0y, t1y, t2y, t3y});
    DrawRectangleLines(mmX + (int)(minX * scX), mmY + (int)(minY * scY),
                       (int)((maxX - minX) * scX), (int)((maxY - minY) * scY), WHITE);
    // 小地图点击跳转
    if (CheckCollisionPointRec(mousePos(), {(float)mmX, (float)mmY, (float)mmW, (float)mmH})) {
        if (mDown(MOUSE_LEFT_BUTTON)) {
            float tx = (mousePos().x - mmX) / scX;
            float ty = (mousePos().y - mmY) / scY;
            int px, py;
            tileToScreen((int)tx, (int)ty, px, py);
            camX = (float)px - viewW / 2.0f;
            camY = (float)py - SCREEN_H / 2.0f;
        }
    }
}
