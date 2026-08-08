// 设置页：原作壳层 — 左分区选项 / 右栏 键盘·网络·主选单（640 逻辑画布）
#include "game/game.h"
#include "sfx/sound.h"
#include <cmath>
#include <algorithm>

// 重绑定按键捕获：真实键盘或自动化脚本注入
int Game::pollAnyKey() {
    if (sim.active) {
        if (!sim.keysPressed.empty()) return *sim.keysPressed.begin();
        return 0;
    }
    return GetKeyPressed();
}

void Game::drawSettings() {
    drawRa2Shell(font, TR(S::OptionsMenu), 0, true);
    Rectangle content = menuShellContent();
    Rectangle side = menuShellSide();
    Vector2 m = menuUiFromCanvas(mousePos());
    bool pr = mPressed(MOUSE_LEFT_BUTTON);

    Rectangle mon = menuShellMonitor();
    if (mon.width > 8) {
        DrawRectangleRec(mon, Color{8, 10, 8, 255});
        float t = (float)GetTime();
        Color wave{255, 160, 40, 200};
        for (int i = 0; i < 3; i++) {
            float wavePh = t * (1.2f + i * 0.35f) + i * 1.7f;
            Vector2 prev{};
            bool has = false;
            for (int x = 0; x < (int)mon.width; x += 3) {
                float nx = (float)x / mon.width;
                float y = mon.height * 0.5f + sinf(nx * 6.28f * 2.0f + wavePh) * (mon.height * 0.28f)
                          * (0.55f + 0.45f * sinf(wavePh * 0.7f + i));
                Vector2 p{mon.x + x, mon.y + y};
                if (has) DrawLineEx(prev, p, 2.0f, wave);
                prev = p;
                has = true;
            }
        }
    }

    static int settingsPane = 0;

    float bw = side.width - 12;
    float bx = side.x + 6;
    const float bhSide = 42.f;
    if (ra2Button(font, m, pr, {bx, 210, bw, bhSide}, TR(S::Keyboard), 15)) {
        settingsPane = settingsPane == 1 ? 0 : 1;
        rebinding = -1;
        g_sfx.play(Sfx::Click, 0.5f);
    }
    if (ra2Button(font, m, pr, {bx, 210 + bhSide + 2, bw, bhSide}, TR(S::NetworkOpts), 15, !settingsFromGame)) {
        if (!settingsFromGame) {
            rebinding = -1;
            settingsPane = 0;
            phase = Phase::NetLobby;
            return;
        }
    }
    for (int i = 0; i < 3; i++) {
        Rectangle slat{bx, 210 + 2 * (bhSide + 2) + 8 + i * 28.0f, bw, 22};
        DrawRectangleRec(slat, Color{28, 30, 28, 180});
        guiBevel(slat, true);
    }
    if (ra2Button(font, m, pr, {bx, side.y + side.height - 50, bw, bhSide},
                  settingsFromGame ? TR(S::Back) : TR(S::BackToMain), 15)) {
        rebinding = -1;
        settingsPane = 0;
        if (settingsFromGame) { phase = Phase::InGame; showMenu = true; }
        else phase = Phase::MainMenu;
    }

    auto redValue = [&](int x, int y, int w, int h, const char* value, bool enabled) {
        Rectangle r{(float)x, (float)y, (float)w, (float)h};
        bool hover = CheckCollisionPointRec(m, r) && enabled;
        drawMenuOptSlot(r, hover && enabled);
        if (!enabled) DrawRectangleRec(r, Color{10, 12, 16, 180});
        drawTextS(font, value, x + 8, y + (h - 12) / 2, 12,
                  enabled ? Color{255, 230, 90, 255} : Color{110, 100, 90, 255});
        return hover && pr;
    };
    auto section = [&](int x, int y, const char* title) {
        drawTextS(font, title, x, y, 14, Color{255, 230, 90, 255});
        DrawRectangle(x, y + 18, 160, 1, Color{140, 50, 40, 180});
    };
    auto pip = [&](int x, int y, const char* label, bool& val) {
        Rectangle hit{(float)x, (float)y, (float)(22 + textW(font, label, 12)), 18};
        bool hover = CheckCollisionPointRec(m, hit);
        drawMenuPip((float)x, (float)y + 2, val);
        drawTextS(font, label, x + 22, y + 2, 12, Color{255, 230, 90, 255});
        if (hover && pr) { val = !val; g_sfx.play(Sfx::Click, 0.45f); }
    };

    int cx = (int)content.x + 24;
    int cy = 40;

    if (settingsPane == 0) {
        section(cx, cy, TR(S::DisplaySection));
        int y = cy + 28;
        drawTextS(font, TR(S::Language), cx, y + 4, 13, Color{255, 230, 90, 255});
        // play-test：值框中心约 (248+90, 72+12) → 与旧相对位置对齐到 UI
        if (redValue(cx + 140, y, 180, 24, g_lang ? "English" : "中文", true)) {
            cfgLang = g_lang ? 0 : 1;
            g_lang = cfgLang;
            g_sfx.play(Sfx::Click, 0.5f);
            saveSettings();
        }
        y += 32;
        drawTextS(font, TR(S::WindowMode), cx, y + 4, 13, Color{255, 230, 90, 255});
        if (redValue(cx + 140, y, 180, 24,
                     cfgWindowMode == 0 ? TR(S::WMFullscreen) : TR(S::WMWindowed), true)) {
            cfgWindowMode = cfgWindowMode ? 0 : 1;
            displayDirty = true;
            g_sfx.play(Sfx::Click, 0.5f);
            saveSettings();
        }
        y += 32;
        drawTextS(font, TR(S::Resolution), cx, y + 4, 13,
                  cfgWindowMode ? Color{255, 230, 90, 255} : Color{110, 100, 90, 255});
        if (redValue(cx + 140, y, 180, 24,
                     cfgWindowMode ? TextFormat("%d × %d", RES_LIST[cfgResIdx][0], RES_LIST[cfgResIdx][1])
                                   : TR(S::ResDesktop),
                     cfgWindowMode != 0)) {
            cfgResIdx = (cfgResIdx + 1) % 8;
            displayDirty = true;
            g_sfx.play(Sfx::Click, 0.5f);
            saveSettings();
        }

        y += 40;
        section(cx, y, TR(S::GameOptsSection));
        y += 28;
        const S speedNames[] = {S::SpeedSlow, S::SpeedNormal, S::SpeedFast};
        drawTextS(font, TR(S::GameSpeed), cx, y + 4, 13, Color{255, 230, 90, 255});
        if (ra2RedSlider(font, m, pr, cx + 140, y + 2, 180, gameSpeed, 3, TR(speedNames[gameSpeed])))
            saveSettings();

        y += 40;
        section(cx, y, TR(S::InterfaceSection));
        y += 28;
        pip(cx, y, TR(S::SharedVision), cfgSharedVision);

        y += 40;
        section(cx, y, TR(S::SoundSection));
        y += 28;
        static const int vols[] = {0, 25, 50, 75, 100};
        drawTextS(font, TR(S::Volume), cx, y + 4, 13, Color{255, 230, 90, 255});
        if (ra2RedSlider(font, m, pr, cx + 140, y + 2, 180, cfgVolume, 5,
                         TextFormat("%d", vols[cfgVolume]))) {
            g_sfx.setMasterVol(vols[cfgVolume] / 100.0f);
            saveSettings();
        }
    } else {
        section(cx, cy, TR(S::KeysSection));
        drawTextS(font, TR(S::KeysTip), cx + 120, cy + 2, 11, Color{160, 150, 120, 255});
        static const S names[KA_COUNT] = {
            S::KaStop, S::KaUnload, S::KaDeploy, S::KaScatter, S::KaGuard, S::KaSameType,
            S::KaMusic, S::KaViewBase, S::KaPause, S::KaRally, S::KaSell,
            S::KaQuickSave, S::KaQuickLoad, S::KaSpeedUp, S::KaSpeedDown, S::KaWaypoint,
        };
        int keyBoxW = 100;
        int listW = (int)content.width - 48;
        int keyBoxX = cx + listW - keyBoxW;
        int maxShow = std::min((int)KA_COUNT, 12);
        for (int i = 0; i < maxShow; i++) {
            int y = cy + 28 + i * 26;
            bool armed = rebinding == i;
            DrawRectangle(cx, y - 1, listW, 24, armed ? Color{48, 28, 20, 255} : (i % 2 ? Color{14, 12, 14, 200} : Color{20, 16, 16, 200}));
            drawTextS(font, TR(names[i]), cx + 6, y + 4, 12,
                      armed ? Color{255, 226, 150, 255} : Color{255, 230, 90, 255});
            Rectangle kr{(float)keyBoxX, (float)y, (float)keyBoxW, 22};
            bool hover = CheckCollisionPointRec(m, kr);
            DrawRectangleRec(kr, Color{10, 12, 16, 255});
            DrawRectangleLinesEx(kr, 1, armed || hover ? Color{255, 100, 70, 255} : Color{180, 50, 40, 220});
            const char* kn = armed ? "…" : keyName(keyBind[i]);
            drawTextS(font, kn, (int)kr.x + keyBoxW / 2 - textW(font, kn, 11) / 2, y + 4, 11, Color{255, 230, 90, 255});
            if (hover && pr && rebinding < 0) { rebinding = i; g_sfx.play(Sfx::Click, 0.5f); }
        }
        if (ra2Button(font, m, pr, {(float)cx, (float)(cy + 28 + maxShow * 26 + 4), 140, 26}, TR(S::ResetKeys), 12)) {
            resetKeyBinds();
            rebinding = -1;
            saveSettings();
        }
    }

    if (rebinding >= 0) {
        int k = pollAnyKey();
        if (k == KEY_ESCAPE) rebinding = -1;
        else if (k > 0) {
            for (int i = 0; i < KA_COUNT; i++)
                if (i != rebinding && keyBind[i] == k) keyBind[i] = 0;
            keyBind[rebinding] = k;
            rebinding = -1;
            g_sfx.play(Sfx::Click, 0.6f);
            saveSettings();
        }
    }
}
