#include "game_regression.h"
#include "game/campaign.h"
#include "gfx/sprites.h"
#include "rlgl.h"
#include <cstdio>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

void GameRegression::debugMenuShot(const char* file, bool setup) {
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

void GameRegression::debugGuiReview(const char* outDir) {
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
            else if (p == Phase::MissionBrief) drawMissionBrief();
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
    setSetupMapCustom(true);
    shotPhase(Phase::Setup, "02b_setup_mapcustom");
    setSetupMapCustom(false);
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

