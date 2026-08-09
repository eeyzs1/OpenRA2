#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "gfx/bitfont.h"
#include "sfx/sound.h"
#include "rlgl.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

// ===================== 初始化 =====================
void Game::init(bool windowed, bool hidden) {
    // 不使用 FLAG_WINDOW_HIGHDPI：在高 DPI 缩放（如 150%）的屏幕上，HIGHDPI 会导致
    // 帧缓冲（物理像素）与窗口（逻辑像素）尺寸不匹配，letterbox 缩放计算出超出屏幕的 dst 矩形，
    // 表现为 GUI 右侧/底部被截断。改由 OS 负责 DPI 缩放，GetRenderWidth()=GetScreenWidth()=逻辑像素，
    // letterbox 代码一致工作。
    if (hidden) SetConfigFlags(FLAG_WINDOW_HIDDEN); // 测试模式：不弹窗不抢焦点
    loadSettings(); // settings.ini：语言/显示模式/分辨率/音量/键位（文件缺失则全默认）
    // 素材外置化：规则数值与字符串须在字体字模收集（appendAllFontText）与任何数据使用前加载；
    // 文件缺失/键缺失均回退内置默认（见 assets/README.txt）
    loadRules("assets/rules/rules.ini");
    loadStrings("assets/strings/zh.ini", 0);
    loadStrings("assets/strings/en.ini", 1);
    // 初始窗口用小尺寸创建（确保任何屏幕都能完整显示），随后 applyDisplay 切换到目标模式
    InitWindow(960, 600, "OpenRA2 - 共和国之辉 复刻");
    if (windowed) cfgWindowMode = 1; // 调试参数：强制窗口模式
    // 显示模式：无边框全屏（窗口=桌面分辨率，逻辑画布 letterbox 缩放，
    // 任何显示器含低分屏/高DPI缩放都不会出现按钮落在屏幕外）或指定分辨率窗口
    if (hidden) { /* 隐藏窗口不做显示模式切换 */ }
    else {
        // 首次启动若窗口分辨率大于显示器，自动选最大适配档位（避免窗口超出屏幕）
        if (cfgWindowMode != 0) {
            int mw = GetMonitorWidth(GetCurrentMonitor());
            int mh = GetMonitorHeight(GetCurrentMonitor());
            while (cfgResIdx > 0 && (RES_LIST[cfgResIdx][0] > mw - 40 || RES_LIST[cfgResIdx][1] > mh - 80))
                cfgResIdx--;
        }
        applyDisplay();
    }
    SetTargetFPS(60);
    loadFont();
    g_sprites.init();
    if (!g_sprites.assetsOk()) {
        assetsOk_ = false;
        TraceLog(LOG_ERROR, "ASSET-CHECK: %d sprite asset(s) missing — refuse start", g_sprites.missingCount());
    }
    // 侧栏/底栏铬面：无文件则程序化兜底已禁用，启动失败
    if (!FileExists("assets/gui/sidebar_allied.png") || !FileExists("assets/gui/bottombar.png")) {
        assetsOk_ = false;
        TraceLog(LOG_ERROR, "GUI-MISSING: assets/gui/sidebar_allied.png or bottombar.png");
        fprintf(stderr, "GUI-MISSING: assets/gui/sidebar_allied.png or bottombar.png\n");
    }
    if (!hidden) { // 隐藏窗口=无头测试：不初始化音频设备，避免提示音/BGM 打扰用户
        g_sfx.init();
        g_sfx.initBgm();
        if (!g_sfx.assetsOk() || !g_sfx.bgmReady()) {
            assetsOk_ = false;
            TraceLog(LOG_ERROR, "ASSET-CHECK: audio missing (sfx missing=%d bgmOk=%d) — refuse start",
                     g_sfx.missingCount(), (int)g_sfx.bgmReady());
        }
        static const int vols[] = {0, 25, 50, 75, 100};
        g_sfx.setMasterVol(vols[cfgVolume] / 100.0f); // 持久化音量生效
    }

    // 逻辑分辨率离屏缓冲：点采样放大，避免 LED/像素 UI 被双线性糊成一团
    canvas = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_POINT);
    loadGameCursors();

    // 迷雾贴图：黑菱形与半透菱形
    {
        PixBuf b(TILE_W, TILE_H);
        b.diamond(TILE_W / 2, TILE_H / 2, TILE_W / 2, TILE_H / 2, Color{0, 0, 0, 255});
        fogBlack = b.toTexture();
        PixBuf d(TILE_W, TILE_H);
        d.diamond(TILE_W / 2, TILE_H / 2, TILE_W / 2, TILE_H / 2, Color{0, 0, 0, 120});
        fogDim = d.toTexture();
    }
    minimap = LoadRenderTexture(256, 256);
    phase = Phase::MainMenu;

    // 脚本引擎回调注册（setObjective/win/lose 需要 Game 状态）
    scriptSetObjectiveCb([this](const std::string& t) { objectiveText = t; });
    scriptSetWinCb([this] { gameOver = true; victory = true; });
    scriptSetLoseCb([this] { gameOver = true; victory = false; });
}

void Game::newGame(uint64_t seed) {
    // 国家（RA2 原作：国家即定阵营与特色单位；随机槽位在全部 10 国中抽取，跳过 None）
    campaignMission = -1;
    nextWave = 0;
    missionTriggers.clear();
    objectiveText.clear();
    Rng frng(seed);
    auto pickCountry = [&](int c) {
        return c >= (int)Country::COUNT ? (Country)frng.range(1, (int)Country::COUNT - 1) : (Country)c;
    };
    std::vector<Country> countries;
    countries.push_back(pickCountry(cfgCountry));
    for (int i = 0; i < cfgAI; i++) countries.push_back(pickCountry(aiCountry[i]));
    std::vector<Faction> factions;
    for (Country c : countries) factions.push_back(countryFaction(c));
    SkirmishMode mode = cfgGameMode >= 0 && cfgGameMode < (int)SkirmishMode::COUNT
                      ? (SkirmishMode)cfgGameMode : SkirmishMode::Battle;
    int effectiveMapType = mode == SkirmishMode::NavalWar ? 1 : cfgMapType;
    world.init(cfgMapSize, cfgMapSize, seed, 1, cfgAI, factions, effectiveMapType);
    world.skirmishMode = mode;
    world.shortGame = cfgShortGame;
    world.mcvRepacks = cfgMcvRepacks;
    world.superweaponsEnabled = cfgSuperweapons;
    world.aiAlliance = mode == SkirmishMode::FreeForAll ? false : cfgAlliance;
    world.sharedVision = world.aiAlliance && cfgSharedVision;
    if (mode == SkirmishMode::Megawealth) world.ensureMegawealthOilDerricks(2);
    if (mode == SkirmishMode::LandRush) {
        world.cratesEnabled = true;
        for (int p = 0; p < world.numPlayers; ++p)
            for (int y = 0; y < world.map.h; ++y)
                for (int x = 0; x < world.map.w; ++x)
                    world.map.reveal(p, x, y, 0);
        // 出生点向地图中心集中（官方 Land Rush：抢中心地皮）
        float cx = world.map.w * 0.5f, cy = world.map.h * 0.5f;
        int slot = 0;
        for (auto& e : world.ents) {
            if (!e.alive || e.isBuilding || e.utype != UnitType::MCV) continue;
            float ang = slot * 0.9f;
            float r = 4.0f + (slot % 3) * 2.0f;
            e.x = cx + cosf(ang) * r;
            e.y = cy + sinf(ang) * r * 0.7f;
            e.path.clear();
            e.state = UState::Idle;
            slot++;
        }
    }
    if (mode == SkirmishMode::UnholyAlliance) {
        // 通用 MCV 在本引擎中按玩家科技权限展开；第二辆代表另一条官方阵营科技树的开局 MCV。
        for (int p = 0; p < world.numPlayers; ++p) {
            float mx = -1.0f, my = -1.0f;
            for (const auto& e : world.ents) {
                if (!e.alive || e.isBuilding || e.player != p || e.utype != UnitType::MCV) continue;
                mx = e.x; my = e.y;
                break;
            }
            if (mx >= 0.0f) world.spawnUnit(p, UnitType::MCV, mx + 2.0f, my);
        }
    }
    for (int i = 0; i < world.numPlayers; i++) world.players[i].country = countries[i];
    // 颜色：取槽位配置；冲突（与前面玩家同色）时顺延到下一个未用色
    bool used[MAX_PLAYERS] = {};
    world.players[0].colorId = cfgColor;
    used[cfgColor] = true;
    for (int i = 1; i < world.numPlayers; i++) {
        int c = aiColor[i - 1];
        while (used[c]) c = (c + 1) % MAX_PLAYERS;
        world.players[i].colorId = c;
        used[c] = true;
    }
    for (int i = 0; i < world.numPlayers; i++) world.players[i].money = cfgMoney;
    if (mode != SkirmishMode::LandRush) world.cratesEnabled = cfgCrates;
    ais.assign(cfgAI, SkirmishAI{});
    for (int i = 0; i < cfgAI; i++) {
        ais[i].reset(i + 1);
        ais[i].difficulty = (AIDiff)aiDiff[i];
        ais[i].personality = (AIPersonality)aiPersonality[i];
        ais[i].initPersonality();
        if (mode == SkirmishMode::NavalWar) {
            ais[i].hasWater = 1;
            ais[i].navalPlaceable = -1;
        }
    }
    sel.clear();
    selBuilding = INVALID_EID;
    dragging = false;
    dragPressSelected = false;
    placing = false;
    gameOver = victory = false;
    camZoom = 1.0f;
    // 摄像机对准本地基地车（优先 MCV；与 HEAD/unitScreenPos 北尖公式一致）
    {
        for (auto& e : world.ents) {
            if (!e.alive || e.isBuilding || e.player != localPlayer) continue;
            if (e.utype != UnitType::MCV) continue;
            int sx, sy;
            tileToScreen((int)e.x, (int)e.y, sx, sy);
            camX = (float)sx - (SCREEN_W - sidebarW) / 2.0f;
            camY = (float)sy - SCREEN_H / 2.0f;
            camZoom = 1.0f;
            break;
        }
    }
    {
        // 中文避免夹杂拉丁字母：game.fnt/字模图集对 D 等易出空心方框
        if (g_lang) {
            std::string tip = TR(S::MsgFindMCVFmt);
            const char* kn = keyName(keyBind[KA_Deploy]);
            size_t pos = tip.find("%s");
            if (pos != std::string::npos) tip.replace(pos, 2, kn ? kn : "?");
            message(tip);
        } else {
            message("找到基地车，按D键展开！");
        }
    }
    phase = Phase::InGame;
    bakeTerrain(); // 整图地表烘焙（连续噪声，无瓦片网格感）
    g_sprites.preloadMatch(localPlayer); // 开局预载本地玩家素材，消除游戏中懒加载掉帧
    // 脚本引擎：加载 assets/scripts/*.lua 并触发 OnGameStart
    // g_script.init(&world, 0, -1);
    // g_script.onGameStart();
}

void Game::loadFont() {
    // 界面字模：系统黑体（汉字/标点/拉丁一致）。先收集文案再对码点去重，避免 LoadFontEx 装入重复槽位。
    std::string all;
    appendAllFontText(all);
    for (int c = 32; c < 127; c++) all += (char)c;
    int count = 0;
    int* cps = LoadCodepoints(all.c_str(), &count);
    std::vector<int> uniq;
    uniq.reserve((size_t)std::max(count, 0) + 8);
    std::vector<uint8_t> seen(0x110000 / 8, 0); // bitset for BMP+plane
    auto add = [&](int cp) {
        if (cp <= 0 || cp >= 0x110000) return;
        size_t i = (size_t)cp;
        uint8_t bit = (uint8_t)(1u << (i & 7));
        size_t bi = i >> 3;
        if (seen[bi] & bit) return;
        seen[bi] |= bit;
        uniq.push_back(cp);
    };
    for (int i = 0; i < count; i++) add(cps[i]);
    UnloadCodepoints(cps);
    TraceLog(LOG_INFO, "RA2 font codepoints: raw=%d unique=%d", count, (int)uniq.size());

    fontOk = false;
    const char* paths[] = {
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
    };
    for (auto p : paths) {
        if (!FileExists(p)) continue;
        font = LoadFontEx(p, 36, uniq.data(), (int)uniq.size());
        TraceLog(LOG_INFO, "RA2 font TTF: %s baseSize=%d glyphs=%d",
                 p, font.baseSize, font.glyphCount);
        if (font.baseSize > 0 && font.glyphCount > 64) {
            // POINT：避免图集双线性采样把笔画顶缘糊成白块
            SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
            fontOk = true;
            break;
        }
        if (font.texture.id) UnloadFont(font);
        font = {};
    }
    if (!fontOk) {
        font = LoadFontFromRa2Fnt("assets/gui/menu/fonts/game.fnt", uniq.data(), (int)uniq.size());
        if (font.baseSize > 0 && font.glyphCount > 64) fontOk = true;
        else if (font.texture.id) { UnloadFont(font); font = {}; }
    }
    if (!fontOk) font = GetFontDefault();
}

void Game::shutdown() {
    g_script.shutdown();
    g_sfx.shutdown();
    unloadGameCursors();
    UnloadRenderTexture(canvas);
    UnloadRenderTexture(minimap);
    UnloadTexture(fogBlack);
    UnloadTexture(fogDim);
    if (terrainTex.id) UnloadTexture(terrainTex);
    if (fogMaskTex.id) UnloadTexture(fogMaskTex);
    if (previewTex.id > 0) UnloadTexture(previewTex);
    if (fontOk) UnloadFont(font);
    CloseWindow();
}

// ===================== 坐标 =====================
// ===================== 主循环 =====================
void Game::run() {
    while (!WindowShouldClose()) {
        if (displayDirty) { displayDirty = false; applyDisplay(); } // 显示模式/分辨率热切换
        g_sfx.updateBgm();
        if (phase == Phase::NetLobby) netHandleMsgs(); // 大厅：驱动握手状态机
        if (phase == Phase::InGame) {
            if (netGame) netHandleMsgs(); // 联机：渲染帧收包，命令帧尽早入队减少等待
            handleInput();
            if (!paused && !gameOver) {
                static const float muls[] = {0.5f, 1.0f, 2.0f}; // 慢/普通/快
                logicAcc += GetFrameTime() * muls[gameSpeed % 3];
                float step = 1.0f / LOGIC_FPS;
                int n = 0;
                while (logicAcc >= step && n < 4) {
                    logic();
                    logicAcc -= step;
                    n++;
                }
                if (n == 4) logicAcc = 0;
                interpAlpha = std::clamp((float)(logicAcc / step), 0.0f, 1.0f); // 渲染插值进度
            } else {
                interpAlpha = 1.0f; // 暂停/结算：定格在最新逻辑位置
            }
        }
        render();
    }
}


void Game::render() {
    // 菜单阶段：独立渲染路径
    if (phase != Phase::InGame) {
        // 壳层页先画 640×480 UI RT，再 blit 到 canvas（禁止嵌套 RenderTexture）
        if (menuShellPhase(phase)) {
            menuBeginUi();
            if (phase == Phase::MainMenu) drawMainMenu();
            else if (phase == Phase::MissionSelect) drawMissionSelect();
            else if (phase == Phase::Settings) drawSettings();
            else if (phase == Phase::NetLobby) drawNetLobby();
            else drawSetup();
            menuEndUi();
            BeginTextureMode(canvas);
            ClearBackground(BLACK);
            menuBlitUi();
            EndTextureMode();
        } else {
            BeginTextureMode(canvas);
            ClearBackground(BLACK);
            if (phase == Phase::MapEditor) drawMapEditor();
            else drawSetup();
            EndTextureMode();
        }
        if (!shotFile.empty()) {
            Image img = LoadImageFromTexture(canvas.texture);
            ImageFlipVertical(&img);
            ExportImage(img, shotFile.c_str());
            UnloadImage(img);
            shotFile.clear();
        }
        BeginDrawing();
        ClearBackground(Color{6, 8, 12, 255}); // letterbox 用深蓝灰填充，非纯黑（与 GUI 底色融合）
        Rectangle src{0, 0, (float)SCREEN_W, -(float)SCREEN_H};
        Rectangle dst = letterboxDst();
        static bool dbgOnce = false;
        if (!dbgOnce) {
            TraceLog(LOG_INFO, "LETTERBOX: screen=%dx%d render=%dx%d canvas=%dx%d dst={%.0f,%.0f,%.0fx%.0f}",
                     GetScreenWidth(), GetScreenHeight(), GetRenderWidth(), GetRenderHeight(),
                     SCREEN_W, SCREEN_H, dst.x, dst.y, dst.width, dst.height);
            dbgOnce = true;
        }
        DrawTexturePro(canvas.texture, src, dst, {0, 0}, 0, WHITE);
        ShowCursor();
        EndDrawing();
        return;
    }
    updateMinimap(); // 嵌套 RenderTexture 会破坏画布渲染状态，提前更新
    // ESC/结算壳同样先离屏到 UI RT，再在 canvas 上 blit
    if (showMenu || gameOver) {
        menuBeginUi();
        drawGameMenuOverlay();
        menuEndUi();
    }
    // 1. 逻辑分辨率渲染到离屏画布
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
        drawPlacement();
        rlPopMatrix();
        flushWorldOverlayRects();
        EndScissorMode();
    }
    // 框选矩形（屏幕空间，不受 zoom 矩阵影响）
    if (dragging && !showMenu) {
        Vector2 m = mousePos();
        float x0 = std::min(dragStart.x, m.x), y0 = std::min(dragStart.y, m.y);
        float w = fabsf(m.x - dragStart.x), h = fabsf(m.y - dragStart.y);
        if (w >= 2 || h >= 2) {
            DrawRectangleLinesEx({x0, y0, w, h}, 1.0f, Color{40, 220, 60, 230});
            DrawRectangleRec({x0, y0, w, h}, Color{40, 220, 60, 28});
        }
    }
    drawHUD();
    {
        Vector2 m = mousePos();
        drawGameCursor((int)m.x, (int)m.y);
    }
    EndTextureMode();
    // 截图优先直接从画布导出（逻辑分辨率，不依赖帧缓冲状态）
    if (!shotFile.empty()) {
        Image img = LoadImageFromTexture(canvas.texture);
        ImageFlipVertical(&img); // RenderTexture Y 轴翻转
        ExportImage(img, shotFile.c_str());
        UnloadImage(img);
        shotFile.clear();
    }
    // 2. 点对点放大到物理帧缓冲
    BeginDrawing();
    ClearBackground(Color{6, 8, 12, 255}); // letterbox 用深蓝灰填充，非纯黑
    Rectangle src{0, 0, (float)SCREEN_W, -(float)SCREEN_H};
    Rectangle dst = letterboxDst();
    DrawTexturePro(canvas.texture, src, dst, {0, 0}, 0, WHITE);
    HideCursor(); // 局内始终隐藏系统光标（自定义光标已画在 canvas）
    EndDrawing();
}
