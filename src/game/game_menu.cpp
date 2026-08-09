// Game 的主菜单与遭遇战设置界面（RA2 风格复刻）
// 共享 UI 组件（drawTextM/textW/ra2Button/ra2TextButton/drawRa2Shell）供 settings/net 复用
#include "game/game.h"
#include "game/campaign.h"
#include "gfx/sprites.h"
#include "gfx/pixel.h"
#include "sfx/sound.h"
#include "rlgl.h"
#include <ctime>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

// load.pcx 640×480 UV — 冻结见 docs/ra2-reference/menu-screens.md
constexpr int LOAD_SIDE_X = 472;
constexpr int LOAD_SIDE_W = 168;
constexpr int LOAD_MON_X = 486;
constexpr int LOAD_MON_Y = 48;
constexpr int LOAD_MON_W = 140;
constexpr int LOAD_MON_H = 122;

// 等比最终像素 RT：避免 640→各向拉伸发糊/比例错。高度贴满 canvas，左右 letterbox。
constexpr float UI_DRAW_SCALE = (float)SCREEN_H / (float)UI_H; // 1.6875
constexpr int UI_RT_W = (int)((float)UI_W * UI_DRAW_SCALE + 0.5f); // 1080
constexpr int UI_RT_H = SCREEN_H; // 810

void drawTextM(Font f, const char* s, int x, int y, int size, Color c) {
    DrawTextEx(f, s, {(float)x, (float)y}, (float)size, 0, c);
}

int textW(Font f, const char* s, int size) {
    return (int)MeasureTextEx(f, s, (float)size, 0).x;
}

// 菜单黄字（原作偏纯黄高对比，非金棕）
static constexpr Color MENU_YELLOW = {255, 236, 64, 255};
static constexpr Color MENU_YELLOW_HI = {255, 255, 170, 255};

// ===================== MIX 菜单 chrome（assets/gui/menu） =====================
struct MenuGui {
    Texture2D titlelg{};
    Texture2D loadShell{};
    Texture2D multiShell{};
    Texture2D fsbkg{};
    Texture2D fsbkgSm{};
    Texture2D bkgdlg{};
    Texture2D bkgdMd{};
    Texture2D bkgdSm{};
    Texture2D pudlg{};
    Texture2D pudlgS{};
    Texture2D optbtn{};
    Texture2D optbtnHi{};
    Texture2D diplo{};
    Texture2D diploHi{};
    Texture2D dropdown{};
    Texture2D dropdownHi{};
    Texture2D pipOn{};
    Texture2D pipOff{};
    Texture2D contentMap{}; // 左内容区暗红战术地图底（load.pcx 洞内近黑）
    Texture2D countryFlag[(int)Country::COUNT]{}; // America..Yuri；None 不用
    Texture2D factionIcon[5]{}; // Allies,Soviet,China,Yuri,Random
    Texture2D sdmp[8]{};
    int sdmpN = 0;
    Texture2D sdbtnBkgd{};
    Texture2D sdbtnAnm[20]{};
    int sdbtnAnmN = 0;
    int shellTheme = 0; // 与 drawRa2Shell theme 同步，供按钮盟军蓝光
    int bikN = 0;
    int bikFps = 15;
    Texture2D bikTex{};
    int bikLast = -1;
    int bikForce = -1;
    bool tried = false;
    bool ok = false;
    RenderTexture2D uiRT{};
    bool uiActive = false;
    bool uiMatrixPushed = false;
    float uiScale = 1.f;
    float uiSX = 1.f, uiSY = 1.f; // canvas↔640：等比
    float uiOX = 0.f, uiOY = 0.f;
};
static MenuGui g_menu;
static Rectangle g_shellContent{};
static Rectangle g_shellSide{};
static Rectangle g_shellMonitor{};

static Texture2D menuLoadTex(const char* path) {
    if (!FileExists(path)) return Texture2D{};
    Image img = LoadImage(path);
    if (!img.data) return Texture2D{};
    Texture2D t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    UnloadImage(img);
    return t;
}

static void menuDrawTex(Texture2D t, Rectangle dst, Color tint = WHITE) {
    if (!t.id) return;
    DrawTexturePro(t, {0, 0, (float)t.width, (float)t.height}, dst, {0, 0}, 0, tint);
}

// 等比贴高：640×480 → 1080×810 RT（字/控件在最终像素绘制），再 1:1 居中到 canvas
static void menuComputeUiScale(float& sx, float& sy, float& ox, float& oy) {
    sx = UI_DRAW_SCALE;
    sy = UI_DRAW_SCALE;
    ox = (float)(SCREEN_W - UI_RT_W) * 0.5f;
    oy = 0.f;
}

float menuUiScale() {
    return UI_DRAW_SCALE;
}

bool menuShellPhase(Phase p) {
    return p == Phase::MainMenu || p == Phase::Setup || p == Phase::Settings
        || p == Phase::MissionSelect || p == Phase::NetLobby;
}

void menuBeginUi() {
    ensureMenuGui();
    if (g_menu.uiRT.id && (g_menu.uiRT.texture.width != UI_RT_W || g_menu.uiRT.texture.height != UI_RT_H)) {
        UnloadRenderTexture(g_menu.uiRT);
        g_menu.uiRT = {};
    }
    if (!g_menu.uiRT.id) {
        g_menu.uiRT = LoadRenderTexture(UI_RT_W, UI_RT_H);
        SetTextureFilter(g_menu.uiRT.texture, TEXTURE_FILTER_POINT);
    }
    menuComputeUiScale(g_menu.uiSX, g_menu.uiSY, g_menu.uiOX, g_menu.uiOY);
    g_menu.uiScale = g_menu.uiSY;
    g_menu.uiActive = true;
    BeginTextureMode(g_menu.uiRT);
    ClearBackground(BLACK);
    // 在最终分辨率 RT 内用 640 逻辑坐标绘制（矩阵放大），避免低分辨率字再被拉伸发糊
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_MODELVIEW);
    rlPushMatrix();
    rlLoadIdentity();
    rlScalef(UI_DRAW_SCALE, UI_DRAW_SCALE, 1.f);
    g_menu.uiMatrixPushed = true;
}

void menuEndUi() {
    if (g_menu.uiMatrixPushed) {
        rlDrawRenderBatchActive();
        rlMatrixMode(RL_MODELVIEW);
        rlPopMatrix();
        g_menu.uiMatrixPushed = false;
    }
    EndTextureMode();
    g_menu.uiActive = false;
}

// BeginScissorMode 用 FBO 像素，不受 rlScalef 影响；在 menuBeginUi 缩放矩阵下必须乘 UI_DRAW_SCALE
static void menuScissorUi(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (g_menu.uiActive) {
        BeginScissorMode((int)(x * UI_DRAW_SCALE + 0.5f), (int)(y * UI_DRAW_SCALE + 0.5f),
                         (int)(w * UI_DRAW_SCALE + 0.5f), (int)(h * UI_DRAW_SCALE + 0.5f));
    } else {
        BeginScissorMode(x, y, w, h);
    }
}

void menuBlitUi() {
    if (!g_menu.uiRT.id) return;
    menuComputeUiScale(g_menu.uiSX, g_menu.uiSY, g_menu.uiOX, g_menu.uiOY);
    g_menu.uiScale = g_menu.uiSY;
    SetTextureFilter(g_menu.uiRT.texture, TEXTURE_FILTER_POINT);
    Rectangle src{0, 0, (float)UI_RT_W, -(float)UI_RT_H};
    Rectangle dst{g_menu.uiOX, g_menu.uiOY, (float)UI_RT_W, (float)UI_RT_H};
    DrawTexturePro(g_menu.uiRT.texture, src, dst, {0, 0}, 0, WHITE);
}

// 勾选：原作遭遇战是橙/红圆形指示灯（非蓝菱）
void drawMenuPip(float x, float y, bool on) {
    ensureMenuGui();
    // 优先用 pips 小图，但多数帧不是菜单灯；菜单勾选改画原作风格圆灯
    float cx = x + 7.f, cy = y + 7.f;
    DrawCircle((int)cx, (int)cy, 7, Color{40, 12, 10, 255});
    DrawCircleLines((int)cx, (int)cy, 7, Color{180, 40, 30, 255});
    if (on) {
        DrawCircle((int)cx, (int)cy, 5, Color{255, 120, 30, 255});
        DrawCircle((int)cx, (int)cy, 3, Color{255, 200, 80, 255});
        DrawCircle((int)cx - 1, (int)cy - 1, 1, Color{255, 240, 180, 220});
    } else {
        DrawCircle((int)cx, (int)cy, 4, Color{50, 20, 16, 255});
    }
}

Vector2 menuUiFromCanvas(Vector2 canvasPos) {
    float sx = g_menu.uiSX, sy = g_menu.uiSY, ox = g_menu.uiOX, oy = g_menu.uiOY;
    if (!g_menu.uiActive || sx <= 0.f || sy <= 0.f)
        menuComputeUiScale(sx, sy, ox, oy);
    return {(canvasPos.x - ox) / sx, (canvasPos.y - oy) / sy};
}

Vector2 menuCanvasFromUi(float uiX, float uiY) {
    float sx, sy, ox, oy;
    menuComputeUiScale(sx, sy, ox, oy);
    return {ox + uiX * sx, oy + uiY * sy};
}

void menuSetBikForceFrame(int frame) { ensureMenuGui(); g_menu.bikForce = frame; }
int menuBikFrameCount() { ensureMenuGui(); return g_menu.bikN; }

static int menuCurrentBikFrame() {
    if (g_menu.bikN <= 0) return 0;
    if (g_menu.bikForce >= 0) return g_menu.bikForce % g_menu.bikN;
    return ((int)(GetTime() * g_menu.bikFps) % g_menu.bikN + g_menu.bikN) % g_menu.bikN;
}

static void menuEnsureBikTex(int fi) {
    if (g_menu.bikN <= 0) return;
    if (fi < 0) fi = 0;
    if (fi >= g_menu.bikN) fi = g_menu.bikN - 1;
    if (fi == g_menu.bikLast && g_menu.bikTex.id) return;
    const char* path = TextFormat("assets/gui/menu/ra2ts_l/f%04d.jpg", fi + 1);
    Image img = LoadImage(path);
    if (!img.data) return;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (!g_menu.bikTex.id) {
        g_menu.bikTex = LoadTextureFromImage(img);
        SetTextureFilter(g_menu.bikTex, TEXTURE_FILTER_POINT);
    } else if (g_menu.bikTex.width == img.width && g_menu.bikTex.height == img.height) {
        UpdateTexture(g_menu.bikTex, img.data);
    } else {
        UnloadTexture(g_menu.bikTex);
        g_menu.bikTex = LoadTextureFromImage(img);
        SetTextureFilter(g_menu.bikTex, TEXTURE_FILTER_POINT);
    }
    UnloadImage(img);
    g_menu.bikLast = fi;
}

// 主菜单：640×480 逻辑画布铺 BIK/title（再 letterbox），与壳层同倍率
static void menuDrawBikOrTitleUi() {
    ensureMenuGui();
    Rectangle dst{0, 0, (float)UI_W, (float)UI_H};
    if (g_menu.bikN > 0) {
        menuEnsureBikTex(menuCurrentBikFrame());
        if (g_menu.bikTex.id) {
            menuDrawTex(g_menu.bikTex, dst);
            return;
        }
    }
    if (g_menu.titlelg.id)
        menuDrawTex(g_menu.titlelg, dst);
}

void ensureMenuGui() {
    if (g_menu.tried) return;
    g_menu.tried = true;
    g_menu.titlelg = menuLoadTex("assets/gui/menu/titlelg.png");
    g_menu.loadShell = menuLoadTex("assets/gui/menu/load.png");
    g_menu.multiShell = menuLoadTex("assets/gui/menu/multi.png");
    g_menu.fsbkg = menuLoadTex("assets/gui/menu/fsbkgdlg_00.png");
    g_menu.fsbkgSm = menuLoadTex("assets/gui/menu/fsbkgdsm_00.png");
    g_menu.bkgdlg = menuLoadTex("assets/gui/menu/bkgdlg_00.png");
    if (!g_menu.bkgdlg.id) g_menu.bkgdlg = menuLoadTex("assets/gui/menu/bkgdlg.png");
    g_menu.bkgdMd = menuLoadTex("assets/gui/menu/bkgdmd_00.png");
    if (!g_menu.bkgdMd.id) g_menu.bkgdMd = menuLoadTex("assets/gui/menu/bkgdmd.png");
    g_menu.bkgdSm = menuLoadTex("assets/gui/menu/bkgdsm_00.png");
    if (!g_menu.bkgdSm.id) g_menu.bkgdSm = menuLoadTex("assets/gui/menu/bkgdsm.png");
    g_menu.pudlg = menuLoadTex("assets/gui/menu/pudlgbga_00.png");
    g_menu.pudlgS = menuLoadTex("assets/gui/menu/pudlgbgs_00.png");
    g_menu.optbtn = menuLoadTex("assets/gui/menu/optbtn_00.png");
    g_menu.optbtnHi = menuLoadTex("assets/gui/menu/optbtn_01.png");
    g_menu.diplo = menuLoadTex("assets/gui/menu/diplobtn_00.png");
    g_menu.diploHi = menuLoadTex("assets/gui/menu/diplobtn_01.png");
    g_menu.dropdown = menuLoadTex("assets/gui/menu/dropdown_00.png");
    g_menu.dropdownHi = menuLoadTex("assets/gui/menu/dropdown_01.png");
    if (!g_menu.dropdownHi.id) g_menu.dropdownHi = menuLoadTex("assets/gui/menu/dropdown_02.png");
    g_menu.pipOn = menuLoadTex("assets/gui/menu/pips_01.png");
    g_menu.pipOff = menuLoadTex("assets/gui/menu/pips_00.png");
    g_menu.contentMap = menuLoadTex("assets/gui/menu/content_map.png");
    static const char* kFlagStem[] = {
        nullptr, // None
        "america", "korea", "france", "germany", "uk",
        "russia", "cuba", "libya", "iraq",
        "china", "yuri",
    };
    for (int i = 1; i < (int)Country::COUNT; i++) {
        if (!kFlagStem[i]) continue;
        g_menu.countryFlag[i] = menuLoadTex(TextFormat("assets/gui/menu/flags/%s.png", kFlagStem[i]));
    }
    static const char* kFacStem[] = {"allies", "soviet", "china", "yuri", "random"};
    for (int i = 0; i < 5; i++)
        g_menu.factionIcon[i] = menuLoadTex(TextFormat("assets/gui/menu/factions/%s.png", kFacStem[i]));
    g_menu.sdmpN = 0;
    for (int i = 0; i < 8; i++) {
        Texture2D t = menuLoadTex(TextFormat("assets/gui/menu/sdmpbtn_%02d.png", i));
        if (!t.id) break;
        g_menu.sdmp[g_menu.sdmpN++] = t;
    }
    g_menu.sdbtnBkgd = menuLoadTex("assets/gui/menu/sdbtnbkgd_00.png");
    g_menu.sdbtnAnmN = 0;
    for (int i = 0; i < 20; i++) {
        Texture2D t = menuLoadTex(TextFormat("assets/gui/menu/sdbtnanm_%02d.png", i));
        if (!t.id) break;
        g_menu.sdbtnAnm[g_menu.sdbtnAnmN++] = t;
    }
    g_menu.bikN = 0;
    g_menu.bikFps = 15;
    if (FileExists("assets/gui/menu/ra2ts_l/meta.ini")) {
        FILE* f = fopen("assets/gui/menu/ra2ts_l/meta.ini", "rb");
        if (f) {
            char line[128];
            while (fgets(line, sizeof line, f)) {
                if (strncmp(line, "frames=", 7) == 0) g_menu.bikN = atoi(line + 7);
                if (strncmp(line, "fps=", 4) == 0) g_menu.bikFps = atoi(line + 4);
            }
            fclose(f);
        }
    } else {
        for (int i = 1; i <= 512; i++) {
            if (!FileExists(TextFormat("assets/gui/menu/ra2ts_l/f%04d.jpg", i))) break;
            g_menu.bikN = i;
        }
    }
    if (g_menu.bikFps <= 0) g_menu.bikFps = 15;
    g_menu.ok = g_menu.titlelg.id || g_menu.loadShell.id || g_menu.multiShell.id
        || g_menu.bkgdlg.id || g_menu.sdmpN > 0 || g_menu.sdbtnAnmN > 0 || g_menu.bikN > 0;
}

bool drawMenuPanelChrome(int x, int y, int w, int h) {
    ensureMenuGui();
    Texture2D t = g_menu.pudlg.id ? g_menu.pudlg : (g_menu.bkgdlg.id ? g_menu.bkgdlg : g_menu.fsbkg);
    if (!t.id) return false;
    DrawRectangle(x + 6, y + 6, w - 12, h - 12, Color{8, 10, 14, 210});
    menuDrawTex(t, {(float)x, (float)y, (float)w, (float)h}, Color{255, 255, 255, 230});
    DrawRectangleLinesEx({(float)x, (float)y, (float)w, (float)h}, 1, GUI_GOLD);
    return true;
}

void drawMenuOptSlot(Rectangle r, bool hover, bool showArrow) {
    ensureMenuGui();
    // value slot: near-black + red border; yellow chevron if dropdown (not gray SHP tile)
    DrawRectangleRec(r, Color{10, 8, 10, 255});
    DrawRectangleLinesEx(r, 1, hover ? Color{255, 96, 48, 255} : Color{176, 40, 32, 255});
    if (showArrow) {
        float cx = r.x + r.width - 9.f, cy = r.y + r.height * 0.5f;
        Color ac = hover ? MENU_YELLOW_HI : MENU_YELLOW;
        DrawTriangle({cx - 3.5f, cy - 2.5f}, {cx + 3.5f, cy - 2.5f}, {cx, cy + 3.5f}, ac);
    }
}
void drawMenuOptSlot(Rectangle r, bool hover) { drawMenuOptSlot(r, hover, true); }

bool ra2TextButton(Font font, Vector2 m, bool pressed, Rectangle r, const char* text, int size) {
    bool hover = CheckCollisionPointRec(m, r);
    Color gold{232, 196, 96, 255};
    Color hi{255, 236, 150, 255};
    Color c = hover ? hi : gold;
    if (text && text[0]) {
        int tw = textW(font, text, size);
        int tx = (int)(r.x + r.width / 2 - tw / 2);
        int ty = (int)(r.y + r.height / 2 - size / 2);
        drawTextS(font, text, tx, ty, size, c);
        if (hover) {
            int uy = ty + size + 2;
            DrawLine(tx, uy, tx + tw, uy, hi);
        }
    }
    bool clicked = hover && pressed;
    if (clicked) g_sfx.play(Sfx::Click, 0.6f);
    return clicked;
}

// 原作按钮：水平宽带红/蓝内光（中亮上下暗）；亮心约占面高 ~40%
static void menuDrawButtonGlow(Rectangle r, bool hot, bool allied) {
    if (r.width < 4.f || r.height < 4.f) return;
    int x = (int)r.x, y = (int)r.y, w = (int)r.width, h = (int)r.height;
    int mid = h / 2;
    unsigned char aLo = (unsigned char)(hot ? 220 : 180);
    unsigned char aHi = (unsigned char)(hot ? 255 : 235);
    Color edge = allied ? Color{6, 14, 40, aLo} : Color{12, 2, 2, aLo};
    Color core = allied ? Color{50, 120, 230, aHi} : Color{200, 28, 18, aHi};
    Color peak = allied ? Color{140, 200, 255, aHi} : Color{255, 56, 32, aHi};
    DrawRectangle(x, y, w, h, edge);
    if (mid > 0) {
        DrawRectangleGradientV(x, y, w, mid, edge, core);
        DrawRectangleGradientV(x, y + mid, w, h - mid, core, edge);
    }
    int band = std::max(6, (h * 2) / 5);
    DrawRectangle(x, y + (h - band) / 2, w, band, peak);
}

static void menuDrawButtonChrome(Rectangle r, bool press) {
    // 内凹金属槽：上/左亮、下/右暗
    Color hi = press ? Color{40, 42, 48, 220} : Color{170, 175, 185, 220};
    Color lo = press ? Color{90, 92, 100, 220} : Color{30, 28, 28, 230};
    DrawRectangleLinesEx(r, 1, Color{12, 12, 14, 255});
    DrawLine((int)r.x + 1, (int)r.y + 1, (int)(r.x + r.width - 2), (int)r.y + 1, hi);
    DrawLine((int)r.x + 1, (int)r.y + 1, (int)r.x + 1, (int)(r.y + r.height - 2), hi);
    DrawLine((int)r.x + 1, (int)(r.y + r.height - 2), (int)(r.x + r.width - 2), (int)(r.y + r.height - 2), lo);
    DrawLine((int)(r.x + r.width - 2), (int)r.y + 1, (int)(r.x + r.width - 2), (int)(r.y + r.height - 2), lo);
}

// load.pcx 洞内近黑；原作有暗红战术地图 — 贴 content_map 或程序回退
static void menuDrawContentMapBg(Rectangle content) {
    if (g_menu.contentMap.id) {
        menuDrawTex(g_menu.contentMap, content, WHITE);
        DrawRectangleRec(content, Color{0, 0, 0, 70});
        return;
    }
    DrawRectangleRec(content, Color{10, 4, 4, 255});
    for (int i = 0; i < 16; i++) {
        float y0 = content.y + 24.f + i * (content.height - 48.f) / 15.f;
        Vector2 prev{content.x + 12.f, y0};
        for (int x = 12; x < (int)content.width - 12; x += 10) {
            float y = y0 + sinf(x * 0.04f + i * 0.7f) * 10.f + sinf(x * 0.01f + i) * 6.f;
            Vector2 p{content.x + (float)x, y};
            DrawLineEx(prev, p, 1.2f, Color{(unsigned char)(55 + i * 2), 14, 12, 90});
            prev = p;
        }
    }
    for (int k = 0; k < 6; k++) {
        int cx = (int)(content.x + 70 + k * 65);
        int cy = (int)(content.y + 80 + (k % 3) * 90);
        DrawCircleLines(cx, cy, 26 + k * 5, Color{80, 20, 16, 70});
    }
    DrawRectangleRec(content, Color{0, 0, 0, 80});
}

bool ra2Button(Font font, Vector2 m, bool pressed, Rectangle r, const char* text, int size,
               bool enabled, bool danger) {
    ensureMenuGui();
    bool hover = CheckCollisionPointRec(m, r) && enabled;
    bool press = hover && pressed;
    const bool allied = g_menu.shellTheme == 1;
    if (enabled && g_menu.sdmpN > 0 && g_menu.sdmp[0].id) {
        Texture2D face = g_menu.sdmp[0];
        if (press && g_menu.sdmpN > 2) face = g_menu.sdmp[2];
        else if (hover && g_menu.sdmpN > 1) face = g_menu.sdmp[1];
        // 拉满槽位（原作侧栏钮占满分隔格，勿按 SHP 比例缩成窄条）
        Color tint = danger ? Color{255, 200, 190, 255}
                   : (allied ? Color{190, 210, 255, 255} : WHITE);
        menuDrawTex(face, r, tint);
        if (!danger) {
            Rectangle inset{r.x + 6.f, r.y + r.height * 0.22f, r.width - 12.f, r.height * 0.52f};
            menuDrawButtonGlow(inset, hover || press, allied);
        }
    } else {
        DrawRectangleRec(r, enabled ? Color{12, 12, 14, 255} : Color{24, 24, 28, 255});
        menuDrawButtonChrome(r, press);
        if (enabled && !danger) menuDrawButtonGlow(
            {r.x + 3.f, r.y + 4.f, r.width - 6.f, r.height - 8.f}, hover || press, allied);
    }
    if (text && text[0]) {
        Color idle = MENU_YELLOW;
        Color hiC = MENU_YELLOW_HI;
        int tw = textW(font, text, size);
        drawTextS(font, text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - size / 2), size,
                  enabled ? (hover ? hiC : idle) : Color{96, 98, 102, 255});
    }
    bool clicked = hover && pressed;
    if (clicked) g_sfx.play(Sfx::Click, 0.6f);
    return clicked;
}

// 主菜单：金属槽 + 水平红亮心；悬停才扫 sdbtnanm（idle 禁止溅射红斑）
static bool ra2TitleButton(Font font, Vector2 m, bool pressed, Rectangle r, const char* text, int size) {
    ensureMenuGui();
    bool hover = CheckCollisionPointRec(m, r);
    bool press = hover && pressed;
    DrawRectangleRec(r, Color{8, 8, 10, 255});
    menuDrawButtonChrome(r, press);
    Rectangle inset{r.x + 3.f, r.y + 4.f, r.width - 6.f, r.height - 8.f};
    menuDrawButtonGlow(inset, hover || press, false);
    if (hover && g_menu.sdbtnAnmN > 0) {
        int fi = ((int)(GetTime() * 12.0) % g_menu.sdbtnAnmN + g_menu.sdbtnAnmN) % g_menu.sdbtnAnmN;
        Texture2D face = g_menu.sdbtnAnm[fi];
        if (face.id) menuDrawTex(face, r, Color{255, 255, 255, 90});
    } else if (g_menu.sdbtnBkgd.id) {
        menuDrawTex(g_menu.sdbtnBkgd, r, Color{255, 255, 255, 40});
    }
    if (text && text[0]) {
        int tw = textW(font, text, size);
        drawTextS(font, text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - size / 2), size,
                  hover ? MENU_YELLOW_HI : MENU_YELLOW);
    }
    bool clicked = hover && pressed;
    if (clicked) g_sfx.play(Sfx::Click, 0.6f);
    return clicked;
}

bool ra2RedSlider(Font font, Vector2 m, bool pressed, int x, int y, int trackW,
                  int& step, int nSteps, const char* valueText) {
    if (nSteps < 2) nSteps = 2;
    if (step < 0) step = 0;
    if (step >= nSteps) step = nSteps - 1;
    // 原作：暗红底轨 + 细红框 + 竖条红指示针；右侧红框数值
    Rectangle frame{(float)x, (float)y + 2, (float)trackW, 12};
    DrawRectangleRec(frame, Color{28, 10, 10, 255});
    DrawRectangleLinesEx(frame, 1, Color{180, 48, 36, 255});
    float t = (float)step / (float)(nSteps - 1);
    int hx = (int)(frame.x + 2 + t * (frame.width - 4));
    DrawRectangle(hx - 1, (int)frame.y + 1, 3, (int)frame.height - 2, Color{230, 56, 40, 255});
    if (valueText && valueText[0]) {
        int vw = textW(font, valueText, 12) + 10;
        if (vw < 28) vw = 28;
        Rectangle vb{(float)(x + trackW + 8), (float)y, (float)vw, 16};
        DrawRectangleRec(vb, Color{8, 10, 12, 255});
        DrawRectangleLinesEx(vb, 1, Color{180, 48, 36, 255});
        drawTextS(font, valueText, (int)vb.x + (vw - textW(font, valueText, 12)) / 2, y + 2, 12,
                  Color{255, 230, 90, 255});
    }
    Rectangle hit{frame.x - 2, frame.y - 4, frame.width + 4, frame.height + 8};
    if (CheckCollisionPointRec(m, hit) && pressed) {
        float nx = (m.x - (frame.x + 2)) / std::max(1.0f, frame.width - 4);
        if (nx < 0) nx = 0;
        if (nx > 1) nx = 1;
        int ns = (int)(nx * (nSteps - 1) + 0.5f);
        if (ns == step) ns = (step + 1) % nSteps;
        step = ns;
        g_sfx.play(Sfx::Click, 0.4f);
        return true;
    }
    return false;
}

Rectangle menuShellContent() { return g_shellContent; }
Rectangle menuShellSide() { return g_shellSide; }
Rectangle menuShellMonitor() { return g_shellMonitor; }

void drawRa2Shell(Font font, const char* sideTitle, int theme, bool drawEmptyMonitor) {
    ensureMenuGui();
    const bool allied = theme == 1;
    const bool useMulti = theme == 2;
    g_menu.shellTheme = theme;
    ClearBackground(Color{0, 0, 0, 255});

    // 逻辑 640×480 槽位（UV 冻结见 menu-screens.md）
    g_shellContent = {0, 0, (float)LOAD_SIDE_X, (float)UI_H};
    g_shellSide = {(float)LOAD_SIDE_X, 0, (float)LOAD_SIDE_W, (float)UI_H};
    g_shellMonitor = {(float)LOAD_MON_X, (float)LOAD_MON_Y, (float)LOAD_MON_W, (float)LOAD_MON_H};

    Texture2D shell = useMulti && g_menu.multiShell.id ? g_menu.multiShell : g_menu.loadShell;

    if (allied) {
        Texture2D eagle = g_menu.bkgdMd.id ? g_menu.bkgdMd : (g_menu.pudlg.id ? g_menu.pudlg : g_menu.bkgdlg);
        DrawRectangle(0, 0, LOAD_SIDE_X, UI_H, Color{4, 10, 24, 255});
        if (eagle.id) {
            float scale = std::min((float)LOAD_SIDE_X / (float)eagle.width, (float)UI_H / (float)eagle.height);
            float dw = eagle.width * scale, dh = eagle.height * scale;
            menuDrawTex(eagle, {(LOAD_SIDE_X - dw) * 0.5f, (UI_H - dh) * 0.5f, dw, dh}, WHITE);
        }
        if (g_menu.loadShell.id) {
            DrawTexturePro(g_menu.loadShell,
                           {(float)LOAD_SIDE_X, 0, (float)LOAD_SIDE_W, (float)UI_H},
                           g_shellSide, {0, 0}, 0, Color{200, 220, 255, 255});
        } else {
            guiMetalFill(LOAD_SIDE_X, 0, LOAD_SIDE_W, UI_H);
            DrawRectangle(LOAD_SIDE_X, 0, LOAD_SIDE_W, UI_H, Color{40, 70, 130, 70});
        }
    } else if (shell.id) {
        // 1:1 贴 load/multi；洞内补暗红战术地图（原作非纯黑）
        menuDrawTex(shell, {0, 0, (float)UI_W, (float)UI_H}, WHITE);
        menuDrawContentMapBg(g_shellContent);
    } else {
        DrawRectangle(0, 0, LOAD_SIDE_X, UI_H, Color{12, 14, 18, 255});
        menuDrawContentMapBg(g_shellContent);
        guiMetalFill(LOAD_SIDE_X, 0, LOAD_SIDE_W, UI_H);
    }

    Rectangle titlePlate{(float)LOAD_SIDE_X + 8, 10, (float)LOAD_SIDE_W - 16, 28};
    if (g_menu.diplo.id) menuDrawTex(g_menu.diplo, titlePlate, WHITE);
    else {
        DrawRectangleRec(titlePlate, Color{12, 14, 12, 230});
        guiBevel(titlePlate, true);
    }
    if (sideTitle && sideTitle[0]) {
        int tw = textW(font, sideTitle, 14);
        drawTextS(font, sideTitle, (int)(titlePlate.x + titlePlate.width / 2 - tw / 2),
                  (int)(titlePlate.y + 6), 14, Color{255, 230, 90, 255});
    }

    if (drawEmptyMonitor) {
        DrawRectangleRec(g_shellMonitor, Color{6, 10, 10, 255});
        if (allied && g_menu.bkgdSm.id) {
            float pad = 4;
            menuDrawTex(g_menu.bkgdSm, {g_shellMonitor.x + pad, g_shellMonitor.y + pad,
                                        g_shellMonitor.width - pad * 2, g_shellMonitor.height - pad * 2}, WHITE);
        } else {
            for (int y = 0; y < (int)g_shellMonitor.height; y += 3)
                DrawLine((int)g_shellMonitor.x, (int)g_shellMonitor.y + y,
                         (int)(g_shellMonitor.x + g_shellMonitor.width), (int)g_shellMonitor.y + y,
                         Color{30, 40, 28, 140});
        }
    }
}

void drawMenuBackdrop(Font font, const char* title) {
    drawRa2Shell(font, title);
}

// 主菜单：BIK 只画进左内容洞（等比），右栏用 load 侧栏——禁止把 BIK 拉满再盖侧栏（会裁掉 CRT/自由女神等）
void Game::drawMainMenu() {
    ensureMenuGui();
    g_menu.shellTheme = 0;
    ClearBackground(Color{4, 6, 10, 255});
    g_shellContent = {0, 0, (float)LOAD_SIDE_X, (float)UI_H};
    g_shellSide = {(float)LOAD_SIDE_X, 0, (float)LOAD_SIDE_W, (float)UI_H};
    g_shellMonitor = {(float)LOAD_MON_X, (float)LOAD_MON_Y, (float)LOAD_MON_W, (float)LOAD_MON_H};

    // 底：整张 load 壳（左洞近黑 + 右 PCB）
    if (g_menu.loadShell.id)
        menuDrawTex(g_menu.loadShell, {0, 0, (float)UI_W, (float)UI_H}, WHITE);
    else
        DrawRectangle(0, 0, LOAD_SIDE_X, UI_H, Color{4, 6, 10, 255});

    // BIK/title：等比放入左侧洞，完整 CRT 外框可见，不被侧栏裁切
    if (g_menu.bikN > 0 || g_menu.titlelg.id) {
        Texture2D art{};
        if (g_menu.bikN > 0) {
            menuEnsureBikTex(menuCurrentBikFrame());
            art = g_menu.bikTex;
        }
        if (!art.id) art = g_menu.titlelg;
        if (art.id) {
            Rectangle hole = g_shellContent;
            float aw = (float)std::max(1, art.width), ah = (float)std::max(1, art.height);
            float scale = std::min(hole.width / aw, hole.height / ah);
            float dw = aw * scale, dh = ah * scale;
            Rectangle dst{
                hole.x + (hole.width - dw) * 0.5f,
                hole.y + (hole.height - dh) * 0.5f,
                dw, dh};
            menuDrawTex(art, dst, WHITE);
        }
    } else {
        int cx = (int)(g_shellContent.width * 0.5f), cy = UI_H / 2 - 20;
        for (int r = 40; r <= 160; r += 40)
            DrawCircleLines(cx, cy, (float)r, Color{80, 20, 16, 255});
        const char* title = TR(S::GameTitle);
        drawTextS(font, title, cx - textW(font, title, 28) / 2, 80, 28, Color{216, 48, 40, 255});
    }

    // 右栏再贴一次，确保 PCB 盖住任何溢出
    if (g_menu.loadShell.id) {
        DrawTexturePro(g_menu.loadShell,
                       {(float)LOAD_SIDE_X, 0, (float)LOAD_SIDE_W, (float)UI_H},
                       g_shellSide, {0, 0}, 0, WHITE);
    }

    Rectangle titlePlate{(float)LOAD_SIDE_X + 8, 10, (float)LOAD_SIDE_W - 16, 28};
    if (g_menu.diplo.id) menuDrawTex(g_menu.diplo, titlePlate, WHITE);
    else {
        DrawRectangleRec(titlePlate, Color{12, 14, 12, 230});
        guiBevel(titlePlate, true);
    }
    const char* mmTitle = g_lang ? "Main Menu" : "主菜单";
    drawTextS(font, mmTitle,
              (int)(titlePlate.x + titlePlate.width / 2 - textW(font, mmTitle, 14) / 2),
              (int)(titlePlate.y + 6), 14, MENU_YELLOW);

    // 监视器槽：示波/警告
    DrawRectangleRec(g_shellMonitor, Color{6, 10, 10, 255});
    {
        float t = (float)GetTime();
        DrawRectangle((int)g_shellMonitor.x + 6, (int)g_shellMonitor.y + 6, 52, 12, Color{160, 30, 24, 220});
        drawTextS(font, "WARNING", (int)g_shellMonitor.x + 8, (int)g_shellMonitor.y + 6, 10, Color{255, 220, 80, 255});
        Color wave{255, 170, 40, 210};
        for (int i = 0; i < 2; i++) {
            float ph = t * (1.3f + i * 0.4f) + i;
            Vector2 prev{};
            bool has = false;
            for (int x = 0; x < (int)g_shellMonitor.width; x += 2) {
                float nx = (float)x / g_shellMonitor.width;
                float y = g_shellMonitor.height * 0.58f
                    + sinf(nx * 6.28f * 2.2f + ph) * (g_shellMonitor.height * 0.22f);
                Vector2 p{g_shellMonitor.x + x, g_shellMonitor.y + y};
                if (has) DrawLineEx(prev, p, 1.5f, wave);
                prev = p;
                has = true;
            }
        }
    }

    Vector2 m = menuUiFromCanvas(mousePos());
    bool pr = mPressed(MOUSE_LEFT_BUTTON);
    const float bw = 148.f, bh = 42.f; // 略窄于侧栏，露出 PCB 银框
    const float bx = (float)LOAD_SIDE_X + ((float)LOAD_SIDE_W - bw) * 0.5f;
    float by = 202.f;
    const float gap = 1.f;
    const int fontSz = 14;

    if (ra2TitleButton(font, m, pr, {bx, by, bw, bh}, TR(S::Skirmish), fontSz))
        phase = Phase::Setup;
    by += bh + gap;
    if (ra2TitleButton(font, m, pr, {bx, by, bw, bh}, TR(S::Campaign), fontSz))
        phase = Phase::MissionSelect;
    by += bh + gap;
    if (ra2TitleButton(font, m, pr, {bx, by, bw, bh}, TR(S::LanGame), fontSz)) {
        lobbyState = 0;
        lobbyEditingIp = false;
        phase = Phase::NetLobby;
    }
    by += bh + gap;
    if (ra2TitleButton(font, m, pr, {bx, by, bw, bh}, TR(S::Settings), fontSz)) {
        settingsFromGame = false;
        phase = Phase::Settings;
    }
    by += bh + gap;
    if (ra2TitleButton(font, m, pr, {bx, by, bw, bh}, TR(S::MapEditor), fontSz)) {
        editorNewMap();
        phase = Phase::MapEditor;
    }
    by += bh + gap;
    if (ra2TitleButton(font, m, pr, {bx, by, bw, bh}, TR(S::ExitGame), fontSz)) {
        CloseWindow();
        exit(0);
    }

    drawTextS(font, "Version 1.0", (int)(g_shellSide.x + 10), UI_H - 16, 11, MENU_YELLOW);
}

void Game::debugMenuShot(const char* file, bool setup) {
    phase = setup ? Phase::Setup : Phase::MainMenu;
    if (setup) refreshMapPreview();
    menuBeginUi();
    if (setup) drawSetup();
    else drawMainMenu();
    menuEndUi();
    BeginTextureMode(canvas);
    ClearBackground(BLACK);
    menuBlitUi();
    EndTextureMode();
    Image img = LoadImageFromTexture(canvas.texture);
    ImageFlipVertical(&img);
    ExportImage(img, file);
    UnloadImage(img);
}

void Game::debugGuiReview(const char* outDir) {
    ensureMenuGui();
    if (outDir && outDir[0]) {
#ifdef _WIN32
        _mkdir(outDir);
#else
        mkdir(outDir, 0755);
#endif
    }
    const char* dir = (outDir && outDir[0]) ? outDir : "gui_review";
    auto saveCanvas = [&](const char* name) {
        Image img = LoadImageFromTexture(canvas.texture);
        ImageFlipVertical(&img);
        ExportImage(img, TextFormat("%s/%s.png", dir, name));
        UnloadImage(img);
        TraceLog(LOG_INFO, "gui_review: %s/%s.png", dir, name);
    };
    auto shotPhase = [&](Phase p, const char* name) {
        phase = p;
        if (p == Phase::Setup) refreshMapPreview();
        if (p == Phase::MapEditor) editorNewMap();
        if (menuShellPhase(p)) {
            menuBeginUi();
            if (p == Phase::MainMenu) drawMainMenu();
            else if (p == Phase::MissionSelect) drawMissionSelect();
            else if (p == Phase::Settings) drawSettings();
            else if (p == Phase::NetLobby) drawNetLobby();
            else drawSetup();
            menuEndUi();
            BeginTextureMode(canvas);
            ClearBackground(BLACK);
            menuBlitUi();
            EndTextureMode();
        } else {
            BeginTextureMode(canvas);
            ClearBackground(BLACK);
            if (p == Phase::MapEditor) drawMapEditor();
            else drawSetup();
            EndTextureMode();
        }
        saveCanvas(name);
    };

    int bn = menuBikFrameCount();
    menuSetBikForceFrame(0);
    shotPhase(Phase::MainMenu, "01_mainmenu_bik0");
    if (bn > 1) {
        menuSetBikForceFrame(bn / 2);
        shotPhase(Phase::MainMenu, "01b_mainmenu_bik_mid");
        menuSetBikForceFrame(bn - 1);
        shotPhase(Phase::MainMenu, "01c_mainmenu_bik_end");
    }
    menuSetBikForceFrame(-1);

    shotPhase(Phase::Setup, "02_setup");
    shotPhase(Phase::MissionSelect, "03_campaign");
    shotPhase(Phase::Settings, "04_settings");
    shotPhase(Phase::NetLobby, "05_netlobby");
    shotPhase(Phase::MapEditor, "06_mapeditor");

    newGame(42);
    phase = Phase::InGame;
    showMenu = true;
    paused = true;
    updateMinimap();
    menuBeginUi();
    drawGameMenuOverlay();
    menuEndUi();
    BeginTextureMode(canvas);
    ClearBackground(BLACK);
    {
        int viewW = SCREEN_W - sidebarW;
        BeginScissorMode(0, 0, viewW, SCREEN_H);
        rlPushMatrix();
        rlLoadIdentity();
        rlScalef(camZoom, camZoom, 1.0f);
        drawWorld();
        drawEntities();
        drawEffectsLayer();
        drawFogLayer();
        rlPopMatrix();
        flushWorldOverlayRects();
        EndScissorMode();
    }
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 170});
    menuBlitUi();
    EndTextureMode();
    saveCanvas("07_esc_menu");
    showMenu = false;
    paused = false;
    phase = Phase::MainMenu;
    TraceLog(LOG_INFO, "gui_review: done -> %s (bik_frames=%d)", dir, bn);
}

static int drawWrapped(Font f, const char* s, int x, int y, int maxW, int size, Color c, int maxLines) {
    int lines = 0;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty() && lines < maxLines) { drawTextM(f, cur.c_str(), x, y + lines * (size + 2), size, c); lines++; }
        cur.clear();
    };
    const char* p = s;
    while (*p && lines < maxLines) {
        std::string word;
        if ((unsigned char)*p < 0x80) {
            while (*p && (unsigned char)*p < 0x80 && *p != ' ') word += *p++;
            if (*p == ' ') word += *p++;
        } else {
            int n = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 1;
            while (n-- > 0 && *p) word += *p++;
        }
        if (!cur.empty() && textW(f, (cur + word).c_str(), size) > maxW) flush();
        cur += word;
    }
    flush();
    return lines;
}

// ===================== 战役选择 =====================
void Game::drawMissionSelect() {
    drawRa2Shell(font, TR(S::Campaign), 0);
    Rectangle content = menuShellContent();
    Rectangle side = menuShellSide();
    Vector2 m = menuUiFromCanvas(mousePos());

    // 阵营徽仅放监视器槽（勿当侧栏）
    Rectangle mon = menuShellMonitor();
    if (g_menu.fsbkgSm.id) {
        float pad = 4;
        float aw = (float)g_menu.fsbkgSm.width, ah = (float)g_menu.fsbkgSm.height;
        float iw = mon.width - pad * 2, ih = mon.height - pad * 2;
        float sc = std::min(iw / aw, ih / ah);
        float dw = aw * sc, dh = ah * sc;
        DrawRectangleRec(mon, Color{6, 10, 10, 255});
        menuDrawTex(g_menu.fsbkgSm,
                    {mon.x + (mon.width - dw) * 0.5f, mon.y + (mon.height - dh) * 0.5f, dw, dh}, WHITE);
    }

    static int campTab = 0;
    const auto& tbl = missionTable();
    const int perCamp = 8;
    const Faction campFac[4] = {Faction::China, Faction::Allies, Faction::Soviet, Faction::Yuri};
    int tabW = 80, tabH = 28, tabGap = 4;
    int tabsX = (int)content.x + 12, tabsY = 40;
    for (int t = 0; t < 5; t++) {
        Rectangle r{(float)(tabsX + t * (tabW + tabGap)), (float)tabsY, (float)tabW, (float)tabH};
        bool sel = campTab == t;
        bool hover = CheckCollisionPointRec(m, r);
        drawMenuOptSlot(r, sel || hover);
        if (sel) DrawRectangleLinesEx(r, 1, Color{255, 90, 50, 255});
        const char* fn = t < 4 ? factName(campFac[t]) : (g_lang ? "Official" : "官方");
        drawTextS(font, fn, (int)r.x + tabW / 2 - textW(font, fn, 12) / 2, (int)r.y + 7, 12,
                  sel ? Color{255, 230, 90, 255} : Color{200, 180, 120, 255});
        if (hover && mPressed(MOUSE_LEFT_BUTTON)) { g_sfx.play(Sfx::Click, 0.6f); campTab = t; }
    }

    // 640 内容区仅 ~472 宽：2 列任务卡
    const int cols = 2;
    int cardW = 210, cardH = 110, gapX = 10, gapY = 8;
    int totalW = cols * cardW + (cols - 1) * gapX;
    int x0 = (int)content.x + ((int)content.width - totalW) / 2;
    if (x0 < (int)content.x + 10) x0 = (int)content.x + 10;
    int y0 = 78;
    std::vector<int> indices;
    if (campTab < 4) {
        int begin = campTab * perCamp, end = std::min(begin + perCamp, (int)tbl.size());
        for (int i = begin; i < end; i++)
            if (tbl[i].track == 0) indices.push_back(i);
    } else {
        for (int i = 0; i < (int)tbl.size(); i++)
            if (tbl[i].track == 1) indices.push_back(i);
    }
    for (int j = 0; j < (int)indices.size() && j < 8; j++) {
        int i = indices[j];
        const MissionDef& md = tbl[i];
        int gx = x0 + (j % cols) * (cardW + gapX), gy = y0 + (j / cols) * (cardH + gapY);
        Rectangle r{(float)gx, (float)gy, (float)cardW, (float)cardH};
        bool hover = CheckCollisionPointRec(m, r);
        // 原作 load 内容区：深底 + 暗红地图感 + 红描边黄字
        DrawRectangleRec(r, hover ? Color{28, 16, 14, 255} : Color{14, 10, 12, 255});
        DrawRectangle(gx + 2, gy + 2, cardW - 4, cardH - 4, Color{40, 12, 10, 40});
        DrawRectangleLinesEx(r, 1, hover ? Color{255, 90, 50, 255} : Color{160, 40, 32, 230});
        int rx = (int)r.x, ry = (int)r.y;
        drawTextS(font, TextFormat(TR(S::MissionN), i + 1), rx + 8, ry + 6, 11, Color{200, 150, 80, 255});
        drawTextS(font, missionName(i), rx + 8, ry + 20, 15, Color{255, 220, 90, 255});
        DrawRectangle(rx + 8, ry + 40, cardW - 16, 1, Color{140, 40, 32, 200});
        int blines = drawWrapped(font, missionBrief(i), rx + 8, ry + 44, cardW - 16, 11, Color{220, 200, 150, 255}, 2);
        const char* objText = md.objective == 1 ? TextFormat(TR(S::ObjSurvive), md.objectiveTick / (30 * 60))
                              : md.objective == 2 ? TR(S::ObjTrigger) : TR(S::ObjEliminate);
        drawTextS(font, objText, rx + 8, ry + 46 + blines * 13, 11, Color{255, 180, 70, 255});
        if (hover) {
            drawTextS(font, TR(S::ClickEnter), rx + 8, ry + cardH - 18, 12, Color{255, 236, 150, 255});
            if (mPressed(MOUSE_LEFT_BUTTON)) {
                g_sfx.play(Sfx::Click, 0.6f);
                newCampaignGame(i);
                return;
            }
        }
    }

    float bx = side.x + 6, bw = side.width - 12;
    if (ra2Button(font, m, mPressed(MOUSE_LEFT_BUTTON),
                  {bx, side.y + side.height - 56, bw, 44}, TR(S::Back), 16))
        phase = Phase::MainMenu;
}

// ===================== 地图预览（与 bakeTerrain 同源 TMP 采样缩略） =====================
static uint32_t previewTileHash(int x, int y, uint64_t s) {
    uint32_t n = (uint32_t)(x * 374761393u + y * 668265263u) ^ (uint32_t)s;
    n = (n ^ (n >> 13)) * 1274126177u;
    return n ^ (n >> 16);
}

void Game::refreshMapPreview() {
    std::vector<Vec2i> spawns;
    previewMap.generate(cfgMapSize, cfgMapSize, previewSeed, cfgAI + 1, spawns, cfgMapType);
    const int w = cfgMapSize, h = cfgMapSize;
    const uint64_t seed = 20260723; // 与 bakeTerrain 同种子

    PixBuf tilePx[6][8];
    bool tileOk[6][8] = {};
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
    PixBuf shorePx[16][2];
    bool shoreOk[16][2] = {};
    for (int m = 1; m < 16; m++)
        for (int v = 0; v < 2; v++) {
            char path[192];
            snprintf(path, sizeof(path), "assets/sprites/tile_shore_m%d_%d.png", m, v);
            shoreOk[m][v] = shorePx[m][v].loadFromFile(path)
                         && shorePx[m][v].w == TILE_W && shorePx[m][v].h == TILE_H;
        }
    std::vector<uint8_t> shoreMask((size_t)w * h, 0);
    for (int ty = 0; ty < h; ty++)
        for (int tx = 0; tx < w; tx++) {
            Terrain tt = previewMap.at(tx, ty).terrain;
            if (tt == Terrain::Water || tt == Terrain::Bridge) continue;
            uint8_t m = 0;
            if (previewMap.inBounds(tx + 1, ty) && previewMap.at(tx + 1, ty).terrain == Terrain::Water) m |= 1;
            if (previewMap.inBounds(tx, ty + 1) && previewMap.at(tx, ty + 1).terrain == Terrain::Water) m |= 2;
            if (previewMap.inBounds(tx - 1, ty) && previewMap.at(tx - 1, ty).terrain == Terrain::Water) m |= 4;
            if (previewMap.inBounds(tx, ty - 1) && previewMap.at(tx, ty - 1).terrain == Terrain::Water) m |= 8;
            shoreMask[(size_t)ty * w + tx] = m;
        }

    float terrainOX = (float)(h - 1) * (TILE_W / 2);
    float terrainW = (float)((w + h - 2) * (TILE_W / 2) + TILE_W);
    float terrainH = (float)((w + h - 2) * (TILE_H / 2) + TILE_H);
    const int Pw = 280, Ph = 200;
    float sc = std::min((float)Pw / terrainW, (float)Ph / terrainH);
    int bw = (int)(terrainW * sc) + 1, bh = (int)(terrainH * sc) + 1;
    PixBuf pb(bw, bh);
    pb.clear(Color{22, 24, 28, 255});

    auto fallbackCol = [](Terrain t) -> Color {
        switch (t) {
            case Terrain::Water: return Color{28, 55, 110, 255};
            case Terrain::Rough: return Color{130, 108, 70, 255};
            case Terrain::Ore:   return Color{200, 170, 55, 255};
            case Terrain::Gems:  return Color{70, 200, 210, 255};
            case Terrain::Bridge: return Color{160, 110, 60, 255};
            default: return Color{58, 110, 48, 255};
        }
    };

    for (int by = 0; by < bh; by++) {
        float sy = by / sc;
        for (int bx = 0; bx < bw; bx++) {
            float sx = bx / sc - terrainOX;
            float fx = sx / (TILE_W / 2.0f), fy = sy / (TILE_H / 2.0f);
            int tx = (int)floorf((fx + fy) / 2.0f), ty = (int)floorf((fy - fx) / 2.0f);
            if (!previewMap.inBounds(tx, ty)) continue;
            Terrain t = previewMap.at(tx, ty).terrain;
            Color c = fallbackCol(t);
            int ti = (int)t;
            uint32_t vh = previewTileHash(tx / 5, ty / 5, seed + 13);
            int tv = (vh % 100 < 94) ? 0 : 1 + (int)(vh % 7);
            bool fromTile = false;
            int ppx = 0, ppy = 0;
            auto sampleTileAt = [&](int txi, int tyi, const PixBuf& src) -> bool {
                tileToScreen(txi, tyi, ppx, ppy);
                int ix = (int)floorf(sx - ppx) + TILE_W / 2, iy = (int)floorf(sy - ppy);
                if ((unsigned)ix >= (unsigned)TILE_W || (unsigned)iy >= (unsigned)TILE_H) return false;
                Color tc = src.px[(size_t)iy * TILE_W + ix];
                if (tc.a < 16) return false;
                c = tc; return true;
            };
            uint8_t sm = shoreMask[(size_t)ty * w + tx];
            if (sm) {
                int sv = (vh % 100 < 88) ? 0 : (int)((previewTileHash(tx / 5, ty / 5, seed + 29) >> 4) & 1);
                if (shoreOk[sm][sv]) fromTile = sampleTileAt(tx, ty, shorePx[sm][sv]);
            }
            if (!fromTile && ti != (int)Terrain::Ore && ti != (int)Terrain::Gems && tileOk[ti][tv])
                fromTile = sampleTileAt(tx, ty, tilePx[ti][tv]);
            if (!fromTile && (t == Terrain::Ore || t == Terrain::Gems))
                c = Color{96, 76, 46, 255};
            pb.px[(size_t)by * bw + bx] = c;
        }
    }
    for (size_t i = 0; i < spawns.size(); i++) {
        int sx, sy;
        tileToScreen(spawns[i].x, spawns[i].y, sx, sy);
        int px = (int)((sx + terrainOX) * sc);
        int py = (int)(sy * sc);
        Color scc = i == 0 ? HOUSE_COLORS[cfgColor] : Color{230, 230, 230, 255};
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
                pb.set(px + dx, py + dy, (dx == 0 || dy == 0) ? scc : Color{0, 0, 0, 255});
    }

    if (previewTex.id > 0) UnloadTexture(previewTex);
    previewTex = pb.toTexture();
    SetTextureFilter(previewTex, TEXTURE_FILTER_POINT);
    previewDirty = false;
}

// ===================== 遭遇战下拉（真正弹出列表，不再点击轮转） =====================
enum class SetupDrop : int { None = 0, Country, Color, Diff, Mode, MapSize, MapType };
struct SetupDropState {
    SetupDrop kind = SetupDrop::None;
    int slot = -1; // 玩家行（Country/Color/Diff）；全局项为 -1
    Rectangle anchor{};
};
static SetupDropState g_sdrop;

static void setupDropClose() {
    g_sdrop.kind = SetupDrop::None;
    g_sdrop.slot = -1;
    g_sdrop.anchor = {};
}

static void setupDropToggle(SetupDrop k, int slot, Rectangle anchor) {
    if (g_sdrop.kind == k && g_sdrop.slot == slot) setupDropClose();
    else {
        g_sdrop.kind = k;
        g_sdrop.slot = slot;
        g_sdrop.anchor = anchor;
    }
}

static const char* setupDiffLabel(int d) {
    static const char* zh[] = {"简单", "普通", "困难", "残酷"};
    static const char* en[] = {"Easy", "Normal", "Hard", "Brutal"};
    if (d < 0) d = 0;
    if (d > 3) d = 3;
    return g_lang ? en[d] : zh[d];
}

static Rectangle setupDropListRect(int nItems, float rowH) {
    float w = std::max(g_sdrop.anchor.width, 100.f);
    float h = (float)nItems * rowH + 4.f;
    float x = g_sdrop.anchor.x;
    float y = g_sdrop.anchor.y + g_sdrop.anchor.height;
    if (y + h > (float)UI_H - 4.f) y = g_sdrop.anchor.y - h; // 向上展开
    if (x + w > (float)UI_W - 4.f) x = (float)UI_W - 4.f - w;
    if (x < 4.f) x = 4.f;
    if (y < 4.f) y = 4.f;
    return {x, y, w, h};
}

// 处理已打开下拉的点击；返回 true 表示吞掉本次点击。outSel=选中项索引，-1=未选中
static bool setupDropConsumeClick(Vector2 m, bool pr, int nItems, float rowH, int& outSel) {
    outSel = -1;
    if (g_sdrop.kind == SetupDrop::None || nItems <= 0) return false;
    Rectangle list = setupDropListRect(nItems, rowH);
    if (!pr) return false;
    if (CheckCollisionPointRec(m, list)) {
        int idx = (int)((m.y - list.y - 2.f) / rowH);
        if (idx < 0) idx = 0;
        if (idx >= nItems) idx = nItems - 1;
        outSel = idx;
        return true;
    }
    if (CheckCollisionPointRec(m, g_sdrop.anchor)) {
        setupDropClose();
        return true;
    }
    setupDropClose();
    return true; // 点空白关闭，不点穿
}

static void setupDropDrawPanel(int nItems, float rowH) {
    if (g_sdrop.kind == SetupDrop::None || nItems <= 0) return;
    Rectangle list = setupDropListRect(nItems, rowH);
    DrawRectangleRec(list, Color{8, 8, 10, 250});
    DrawRectangleLinesEx(list, 1, Color{200, 48, 36, 255});
}

// ===================== 遭遇战设置（640 逻辑：左玩家/规则 + 右地图轨） =====================
void Game::drawSetup() {
    drawRa2Shell(font, TR(S::Skirmish), 0, false);
    Rectangle content = menuShellContent();
    Rectangle side = menuShellSide();
    Vector2 m = menuUiFromCanvas(mousePos());
    bool pr = mPressed(MOUSE_LEFT_BUTTON);
    if (previewDirty) refreshMapPreview();

    int maxAI = cfgMapSize <= 64 ? 3 : (cfgMapSize <= 96 ? 5 : (cfgMapSize <= 160 ? 7 : 7));
    if (cfgAI > maxAI) cfgAI = maxAI;

    // ---------- 右栏：监视器 + 信息 + sdmp 按钮 ----------
    Rectangle mon = menuShellMonitor();
    DrawRectangleRec(mon, Color{22, 24, 28, 255});
    if (previewTex.id > 0) {
        float pad = 4;
        float aw = (float)previewTex.width, ah = (float)previewTex.height;
        float iw = mon.width - pad * 2, ih = mon.height - pad * 2;
        float scale = std::min(iw / aw, ih / ah);
        float dw = aw * scale, dh = ah * scale;
        Rectangle dst{mon.x + (mon.width - dw) * 0.5f, mon.y + (mon.height - dh) * 0.5f, dw, dh};
        DrawTexturePro(previewTex, {0, 0, aw, ah}, dst, {0, 0}, 0, WHITE);
    }
    Rectangle info = {mon.x, mon.y + mon.height + 4, mon.width, 36};
    DrawRectangleRec(info, Color{8, 10, 10, 230});
    static const S modeNames[] = {
        S::ModeBattle, S::ModeFFA, S::ModeUnholy, S::ModeMegawealth,
        S::ModeLandRush, S::ModeMeatGrinder, S::ModeNavalWar,
    };
    const S typeNames[] = {
        S::MapContinent, S::MapIslands, S::MapLake,
        S::MapArchipelago, S::MapCoast, S::MapRiver, S::MapMountain,
    };
    static const int mapSizes[] = {48, 64, 96, 128, 160, 200, 256};
    const S sizeNames[] = {
        S::SizeXS, S::SizeS, S::SizeM, S::SizeL, S::SizeXL, S::SizeHuge, S::SizeEpic,
    };
    static constexpr int kMapSizeN = 7;
    static constexpr int kMapTypeN = 7;
    const float dropRowH = 18.f;
    auto dropItemCount = [&]() -> int {
        switch (g_sdrop.kind) {
            case SetupDrop::Country: return (int)Country::COUNT; // 11 国 + 随机
            case SetupDrop::Color: return MAX_PLAYERS;
            case SetupDrop::Diff: return 4;
            case SetupDrop::Mode: return (int)SkirmishMode::COUNT;
            case SetupDrop::MapSize: return kMapSizeN;
            case SetupDrop::MapType: return kMapTypeN;
            default: return 0;
        }
    };
    int dropSel = -1;
    bool dropAte = false;
    if (g_sdrop.kind != SetupDrop::None)
        dropAte = setupDropConsumeClick(m, pr, dropItemCount(), dropRowH, dropSel);
    if (dropSel >= 0) {
        switch (g_sdrop.kind) {
            case SetupDrop::Country: {
                int v = (dropSel < (int)Country::COUNT - 1) ? (dropSel + 1) : (int)Country::COUNT;
                if (g_sdrop.slot <= 0) cfgCountry = v;
                else if (g_sdrop.slot - 1 < cfgAI) aiCountry[g_sdrop.slot - 1] = v;
                break;
            }
            case SetupDrop::Color: {
                int v = dropSel % MAX_PLAYERS;
                if (g_sdrop.slot <= 0) cfgColor = v;
                else if (g_sdrop.slot - 1 < cfgAI) aiColor[g_sdrop.slot - 1] = v;
                break;
            }
            case SetupDrop::Diff:
                if (g_sdrop.slot >= 1 && g_sdrop.slot - 1 < cfgAI) aiDiff[g_sdrop.slot - 1] = dropSel;
                break;
            case SetupDrop::Mode:
                cfgGameMode = dropSel;
                if (cfgGameMode == (int)SkirmishMode::FreeForAll) cfgAlliance = false;
                if (cfgGameMode == (int)SkirmishMode::LandRush) cfgCrates = true;
                if (cfgGameMode == (int)SkirmishMode::NavalWar && cfgMapType != 1) {
                    cfgMapType = 1;
                    previewDirty = true;
                }
                break;
            case SetupDrop::MapSize:
                cfgMapSize = mapSizes[dropSel];
                if (cfgAI > maxAI) cfgAI = maxAI;
                previewDirty = true;
                break;
            case SetupDrop::MapType:
                cfgMapType = dropSel;
                if (cfgGameMode == (int)SkirmishMode::NavalWar) cfgMapType = 1;
                previewDirty = true;
                break;
            default: break;
        }
        g_sfx.play(Sfx::Click, 0.5f);
        setupDropClose();
    }
    bool prUi = pr && !dropAte;

    drawTextS(font, TR(modeNames[cfgGameMode]), (int)info.x + 4, (int)info.y + 4, 12, MENU_YELLOW);
    int infoType = cfgMapType;
    if (infoType < 0 || infoType >= kMapTypeN) infoType = 0;
    drawTextS(font, TextFormat("%s (%d)", TR(typeNames[infoType]), cfgAI + 1),
              (int)info.x + 4, (int)info.y + 20, 11, MENU_YELLOW);

    // 侧栏：sdmpbtn 原作约 156×83；槽宽贴满、高度按素材比（勿压成 42 扁条）
    float bw = side.width - 10;
    float bx = side.x + 5;
    const float bhSide = std::min(64.f, bw * 83.f / 156.f); // ~54–64
    float byBtn = 212.f;
    if (ra2Button(font, m, prUi, {bx, byBtn, bw, bhSide}, TR(S::StartGame), 13))
        newGame(previewSeed);
    if (ra2Button(font, m, prUi, {bx, byBtn + bhSide + 3, bw, bhSide}, TR(S::CustomizeBattle), 11)) {
        // 重新生成地图（地形类型用下方下拉改）
        previewSeed = (uint64_t)time(nullptr) * 2654435761u + 97;
        previewDirty = true;
        g_sfx.play(Sfx::Click, 0.55f);
    }
    if (ra2Button(font, m, prUi, {bx, 415.f, bw, bhSide}, TR(S::Back), 13)) {
        setupDropClose();
        phase = Phase::MainMenu;
    }

    // ---------- 左：玩家槽 [名][阵营徽][旗+国家][颜色][难度▾] ----------
    // 比例贴近原作遭遇战：名列略宽、国家槽为主、颜色方块、难度窄下拉
    int sx = (int)content.x + 8, sy = 18;
    int sw = (int)content.width - 16;
    int rowH = 22;
    int nameX = sx;
    int nameW = 90;
    int facIconX = nameX + nameW + 4;
    int factX = facIconX + 24;
    int factW = 150;
    int colorX = factX + factW + 6;
    int colorW = 28;
    int extraX = colorX + colorW + 6;
    int extraW = std::max(64, sx + sw - extraX - 44);
    int delX = sx + sw - 40;
    drawTextM(font, TR(S::Player), nameX, sy, 10, MENU_YELLOW);
    drawTextM(font, TR(S::Country), factX, sy, 10, MENU_YELLOW);
    drawTextM(font, TR(S::Color), colorX, sy, 10, MENU_YELLOW);
    drawTextM(font, g_lang ? "Diff" : "难度", extraX, sy, 10, MENU_YELLOW);
    int slotY = sy + 16;
    auto factionIconFor = [&](int country) -> Texture2D {
        if (country >= (int)Country::COUNT) return g_menu.factionIcon[4]; // random
        if (country <= 0) return Texture2D{};
        Faction f = countryFaction((Country)country);
        int fi = (int)f;
        if (fi < 0 || fi > 3) fi = 0;
        return g_menu.factionIcon[fi];
    };
    auto slotRow = [&](int idx, const char* name, int& color, int& country, int& diff, int& pers, bool isLocal) {
        (void)pers;
        int y = slotY + idx * rowH;
        DrawRectangle(sx, y, sw, rowH - 2, idx % 2 ? Color{16, 14, 16, 160} : Color{22, 16, 16, 160});
        Rectangle nr{(float)nameX, (float)y + 2, (float)nameW, (float)(rowH - 6)};
        drawMenuOptSlot(nr, CheckCollisionPointRec(m, nr), false);
        {
            int nfs = 11;
            if (textW(font, name, nfs) > (int)nr.width - 6) nfs = 10;
            menuScissorUi((int)nr.x + 2, y + 1, (int)nr.width - 4, rowH - 4);
            drawTextS(font, name, nameX + 3, y + 4, nfs, isLocal ? MENU_YELLOW : Color{220, 200, 160, 255});
            EndScissorMode();
        }

        Texture2D fi = factionIconFor(country);
        if (fi.id) {
            float ih = (float)(rowH - 8);
            float iw = ih * (float)fi.width / (float)std::max(1, fi.height);
            if (iw > 20.f) { iw = 20.f; ih = iw * (float)fi.height / (float)fi.width; }
            Rectangle fd{(float)facIconX + (20.f - iw) * 0.5f, (float)y + (rowH - ih) * 0.5f, iw, ih};
            DrawTexturePro(fi, {0, 0, (float)fi.width, (float)fi.height}, fd, {0, 0}, 0, WHITE);
        }

        Rectangle fr{(float)factX, (float)y + 2, (float)factW, (float)(rowH - 6)};
        bool fhover = CheckCollisionPointRec(m, fr);
        bool fopen = g_sdrop.kind == SetupDrop::Country && g_sdrop.slot == idx;
        drawMenuOptSlot(fr, fhover || fopen);
        int textX = (int)fr.x + 4;
        {
            Texture2D fl{};
            if (country >= (int)Country::COUNT) fl = g_menu.factionIcon[4]; // 随机：暗底菱形，勿用 ???
            else if (country > 0 && country < (int)Country::COUNT) fl = g_menu.countryFlag[country];
            if (fl.id) {
                float ih = (float)(rowH - 10);
                float iw = ih * (float)fl.width / (float)std::max(1, fl.height);
                if (iw > 20.f) { iw = 20.f; ih = iw * (float)fl.height / (float)fl.width; }
                Rectangle fd{fr.x + 3.f, fr.y + (fr.height - ih) * 0.5f, iw, ih};
                DrawTexturePro(fl, {0, 0, (float)fl.width, (float)fl.height}, fd, {0, 0}, 0, WHITE);
                textX = (int)(fd.x + fd.width + 4.f);
            }
        }
        const char* fn = country >= (int)Country::COUNT ? TR(S::Random) : countryName((Country)country);
        {
            int fs = 11;
            int maxTw = (int)fr.x + (int)fr.width - textX - 10;
            if (maxTw > 8 && textW(font, fn, fs) > maxTw) fs = 10;
            menuScissorUi(textX, y + 1, std::max(8, maxTw), rowH - 4);
            drawTextS(font, fn, textX, y + 4, fs, MENU_YELLOW);
            EndScissorMode();
        }
        if (fhover && prUi) {
            setupDropToggle(SetupDrop::Country, idx, fr);
            g_sfx.play(Sfx::Click, 0.45f);
        }

        Rectangle cr{(float)colorX, (float)y + 2, (float)colorW, (float)(rowH - 6)};
        bool chover = CheckCollisionPointRec(m, cr);
        bool copen = g_sdrop.kind == SetupDrop::Color && g_sdrop.slot == idx;
        DrawRectangleRec(cr, HOUSE_COLORS[color]);
        DrawRectangleLinesEx(cr, 1, (chover || copen) ? Color{255, 96, 48, 255} : Color{176, 40, 32, 255});
        {
            float cx = cr.x + cr.width - 7.f, cy = cr.y + cr.height * 0.5f;
            Color ac = (chover || copen) ? MENU_YELLOW_HI : MENU_YELLOW;
            DrawTriangle({cx - 3, cy - 2}, {cx + 3, cy - 2}, {cx, cy + 3}, ac);
        }
        if (chover && prUi) {
            setupDropToggle(SetupDrop::Color, idx, cr);
            g_sfx.play(Sfx::Click, 0.45f);
        }
        if (!isLocal) {
            Rectangle dr2{(float)extraX, (float)y + 2, (float)std::min(extraW, 72), (float)(rowH - 6)};
            bool dhover = CheckCollisionPointRec(m, dr2);
            bool dopen = g_sdrop.kind == SetupDrop::Diff && g_sdrop.slot == idx;
            drawMenuOptSlot(dr2, dhover || dopen);
            const char* dn = setupDiffLabel(diff);
            drawTextS(font, dn, (int)dr2.x + 2, y + 4, 10,
                      diff >= 2 ? Color{255, 120, 90, 255} : MENU_YELLOW);
            if (dhover && prUi) {
                setupDropToggle(SetupDrop::Diff, idx, dr2);
                g_sfx.play(Sfx::Click, 0.45f);
            }
            Rectangle dr{(float)delX, (float)y + 2, 36, (float)(rowH - 6)};
            if (ra2Button(font, m, prUi, dr, TR(S::Remove), 9, true, true)) {
                for (int i = idx - 1; i < cfgAI - 1; i++) {
                    aiColor[i] = aiColor[i + 1]; aiCountry[i] = aiCountry[i + 1];
                    aiDiff[i] = aiDiff[i + 1]; aiPersonality[i] = aiPersonality[i + 1];
                }
                cfgAI--;
                setupDropClose();
                previewDirty = true;
            }
        }
    };
    slotRow(0, TR(S::CommanderYou), cfgColor, cfgCountry, aiDiff[0], aiPersonality[0], true);
    for (int i = 0; i < cfgAI; i++)
        slotRow(i + 1, TextFormat(TR(S::ComputerN), i + 1), aiColor[i], aiCountry[i], aiDiff[i], aiPersonality[i], false);
    if (cfgAI < maxAI) {
        int y = slotY + (cfgAI + 1) * rowH + 2;
        if (ra2Button(font, m, prUi, {(float)nameX, (float)y, 88, 22}, TR(S::AddComputer), 10)) {
            aiColor[cfgAI] = (cfgAI + 1) % MAX_PLAYERS;
            aiCountry[cfgAI] = (int)Country::COUNT;
            aiDiff[cfgAI] = 1;
            aiPersonality[cfgAI] = 0;
            cfgAI++;
            previewDirty = true;
        }
    }

    auto toggle = [&](int x, int y, const char* label, bool& val) {
        ensureMenuGui();
        const int fs = 11;
        Rectangle hit{(float)x, (float)y, (float)(18 + textW(font, label, fs)), 16};
        bool hover = CheckCollisionPointRec(m, hit);
        drawMenuPip((float)x, (float)y + 2, val);
        drawTextS(font, label, x + 20, y + 1, fs, MENU_YELLOW);
        if (hover && prUi) { val = !val; g_sfx.play(Sfx::Click, 0.45f); return true; }
        return false;
    };
    auto dropField = [&](int x, int y, const char* label, const char* value, int vw, SetupDrop kind) {
        const int fs = 11;
        drawTextS(font, label, x, y + 3, fs, MENU_YELLOW);
        int lx = x + textW(font, label, fs) + 6;
        Rectangle r{(float)lx, (float)y, (float)vw, 18};
        bool hover = CheckCollisionPointRec(m, r);
        bool open = g_sdrop.kind == kind;
        drawMenuOptSlot(r, hover || open);
        menuScissorUi((int)r.x + 2, (int)r.y, (int)r.width - 4, (int)r.height);
        drawTextS(font, value, lx + 4, y + 2, fs, MENU_YELLOW);
        EndScissorMode();
        if (hover && prUi) {
            setupDropToggle(kind, -1, r);
            g_sfx.play(Sfx::Click, 0.45f);
        }
    };

    // 选项：双列勾选 + 右侧滑条；行距贴近原作 pip 条（约 16–18）
    int ty = std::max(228, slotY + (cfgAI + 2) * rowH + 8);
    if (ty > 255) ty = 228;
    const int optGap = 17;
    if (toggle(12, ty, TR(S::ShortGame), cfgShortGame)) {}
    if (toggle(12, ty + optGap, TR(S::McvRepacks), cfgMcvRepacks)) {}
    if (toggle(12, ty + optGap * 2, TR(S::Crates), cfgCrates)) {}
    if (toggle(168, ty, TR(S::AIAlliance), cfgAlliance)) {
        if (cfgAlliance) cfgGameMode = (int)SkirmishMode::Battle;
    }
    if (toggle(168, ty + optGap, TR(S::SharedVision), cfgSharedVision)) {}
    if (toggle(168, ty + optGap * 2, TR(S::Superweapons), cfgSuperweapons)) {}

    const S speedNames[] = {S::SpeedSlow, S::SpeedNormal, S::SpeedFast};
    drawTextS(font, TR(S::GameSpeed), 310, ty, 11, MENU_YELLOW);
    if (ra2RedSlider(font, m, prUi, 310, ty + 14, 110, gameSpeed, 3, TR(speedNames[gameSpeed]))) {}
    static const int monies[] = {5000, 10000, 20000, 50000};
    int moneyStep = 0;
    while (moneyStep < 4 && monies[moneyStep] != cfgMoney) moneyStep++;
    if (moneyStep > 3) moneyStep = 1;
    drawTextS(font, TR(S::StartMoney), 310, ty + 38, 11, MENU_YELLOW);
    if (ra2RedSlider(font, m, prUi, 310, ty + 52, 110, moneyStep, 4, TextFormat("%d", monies[moneyStep])))
        cfgMoney = monies[moneyStep];

    // 底栏模式/地图：值槽高度跟 optbtn（18），宽度按标签分栏
    int oy = 424;
    dropField(10, oy, TR(S::GameMode), TR(modeNames[cfgGameMode]), 108, SetupDrop::Mode);
    int si = 0;
    while (si < kMapSizeN && mapSizes[si] != cfgMapSize) si++;
    if (si >= kMapSizeN) si = 2; // default Medium
    dropField(210, oy, TR(S::MapSize), TR(sizeNames[si]), 88, SetupDrop::MapSize);
    int ti = cfgMapType;
    if (ti < 0 || ti >= kMapTypeN) ti = 0;
    dropField(330, oy, TR(S::MapType), TR(typeNames[ti]), 88, SetupDrop::MapType);

    // 最上层画下拉列表
    int nDrop = dropItemCount();
    if (nDrop > 0) {
        setupDropDrawPanel(nDrop, dropRowH);
        Rectangle list = setupDropListRect(nDrop, dropRowH);
        for (int i = 0; i < nDrop; i++) {
            Rectangle row{list.x + 1, list.y + 2 + i * dropRowH, list.width - 2, dropRowH};
            bool hover = CheckCollisionPointRec(m, row);
            if (hover) DrawRectangleRec(row, Color{48, 20, 16, 255});
            switch (g_sdrop.kind) {
                case SetupDrop::Country: {
                    if (i < (int)Country::COUNT - 1) {
                        int c = i + 1;
                        int tx = (int)row.x + 4;
                        Texture2D fl = g_menu.countryFlag[c];
                        if (fl.id) {
                            float ih = dropRowH - 4.f;
                            float iw = ih * (float)fl.width / (float)std::max(1, fl.height);
                            menuDrawTex(fl, {row.x + 3, row.y + 2, iw, ih}, WHITE);
                            tx = (int)(row.x + 3 + iw + 4);
                        }
                        drawTextS(font, countryName((Country)c), tx, (int)row.y + 2, 11, MENU_YELLOW);
                    } else {
                        int tx = (int)row.x + 6;
                        if (g_menu.factionIcon[4].id) {
                            float ih = dropRowH - 4.f;
                            float iw = ih * (float)g_menu.factionIcon[4].width
                                     / (float)std::max(1, g_menu.factionIcon[4].height);
                            menuDrawTex(g_menu.factionIcon[4], {row.x + 3, row.y + 2, iw, ih}, WHITE);
                            tx = (int)(row.x + 3 + iw + 4);
                        }
                        drawTextS(font, TR(S::Random), tx, (int)row.y + 2, 11, MENU_YELLOW);
                    }
                    break;
                }
                case SetupDrop::Color: {
                    DrawRectangle((int)row.x + 4, (int)row.y + 3, 22, (int)dropRowH - 6, HOUSE_COLORS[i]);
                    drawTextS(font, TextFormat("%d", i + 1), (int)row.x + 30, (int)row.y + 2, 11, MENU_YELLOW);
                    break;
                }
                case SetupDrop::Diff:
                    drawTextS(font, setupDiffLabel(i), (int)row.x + 6, (int)row.y + 2, 11,
                              i >= 2 ? Color{255, 120, 90, 255} : MENU_YELLOW);
                    break;
                case SetupDrop::Mode:
                    drawTextS(font, TR(modeNames[i]), (int)row.x + 6, (int)row.y + 2, 11, MENU_YELLOW);
                    break;
                case SetupDrop::MapSize:
                    drawTextS(font, TR(sizeNames[i]), (int)row.x + 6, (int)row.y + 2, 11, MENU_YELLOW);
                    break;
                case SetupDrop::MapType:
                    drawTextS(font, TR(typeNames[i]), (int)row.x + 6, (int)row.y + 2, 11, MENU_YELLOW);
                    break;
                default: break;
            }
        }
    }
}
