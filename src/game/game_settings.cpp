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
    // 空监视器由 shell 画工业 CRT；本页不再叠玩具示波
    drawRa2Shell(font, TR(S::OptionsMenu), 0, true);
    Rectangle content = menuShellContent();
    Rectangle side = menuShellSide();
    Vector2 m = menuUiFromCanvas(mousePos());
    bool pr = mPressed(MOUSE_LEFT_BUTTON);

    static int settingsPane = 0;

    float bw = std::min(side.width - 14.f, 148.f);
    float bx = side.x + (side.width - bw) * 0.5f;
    const float bhSide = bw * (83.f / 156.f);
    const float sideGap = 10.f;
    const float by0 = 206.f;
    if (ra2Button(font, m, pr, {bx, by0, bw, bhSide}, TR(S::Keyboard), 14)) {
        settingsPane = settingsPane == 1 ? 0 : 1;
        rebinding = -1;
        g_sfx.play(Sfx::Click, 0.5f);
    }
    if (ra2Button(font, m, pr, {bx, by0 + bhSide + sideGap, bw, bhSide}, TR(S::NetworkOpts), 14, !settingsFromGame)) {
        if (!settingsFromGame) {
            rebinding = -1;
            settingsPane = 0;
            phase = Phase::NetLobby;
            return;
        }
    }
    if (ra2Button(font, m, pr, {bx, side.y + side.height - 12.f - bhSide, bw, bhSide},
                  settingsFromGame ? TR(S::Back) : TR(S::BackToMain), 14)) {
        rebinding = -1;
        settingsPane = 0;
        if (settingsFromGame) { phase = Phase::InGame; showMenu = true; }
        else phase = Phase::MainMenu;
    }

    int cx = (int)content.x + 24;
    int cy = 36;

    if (settingsPane == 0) {
        menuSectionHeader(font, cx, cy, TR(S::DisplaySection));
        int y = cy + 26;
        if (menuLabeledValue(font, m, pr, cx, y, TR(S::Language), cx + 140, 180, 22,
                             g_lang ? "English" : "中文", true)) {
            cfgLang = g_lang ? 0 : 1;
            g_lang = cfgLang;
            g_sfx.play(Sfx::Click, 0.5f);
            saveSettings();
        }
        y += 34;
        if (menuLabeledValue(font, m, pr, cx, y, TR(S::WindowMode), cx + 140, 180, 22,
                             cfgWindowMode == 0 ? TR(S::WMFullscreen) : TR(S::WMWindowed), true)) {
            cfgWindowMode = cfgWindowMode ? 0 : 1;
            displayDirty = true;
            g_sfx.play(Sfx::Click, 0.5f);
            saveSettings();
        }
        y += 34;
        if (menuLabeledValue(font, m, pr, cx, y, TR(S::Resolution), cx + 140, 180, 22,
                             cfgWindowMode ? TextFormat("%d × %d", RES_LIST[cfgResIdx][0], RES_LIST[cfgResIdx][1])
                                           : TR(S::ResDesktop),
                             cfgWindowMode != 0)) {
            cfgResIdx = (cfgResIdx + 1) % 8;
            displayDirty = true;
            g_sfx.play(Sfx::Click, 0.5f);
            saveSettings();
        }

        y += 44;
        menuSectionHeader(font, cx, y, TR(S::GameOptsSection));
        y += 26;
        const S speedNames[] = {S::SpeedSlow, S::SpeedNormal, S::SpeedFast};
        drawTextS(font, TR(S::GameSpeed), cx, y + 4, 13, Color{200, 190, 150, 255});
        if (ra2RedSlider(font, m, pr, cx + 140, y + 2, 180, gameSpeed, 3, TR(speedNames[gameSpeed])))
            saveSettings();

        y += 44;
        menuSectionHeader(font, cx, y, TR(S::InterfaceSection));
        y += 26;
        if (menuPipToggle(font, m, pr, cx, y, TR(S::SharedVision), cfgSharedVision)) {}
        y += 28;
        if (menuPipToggle(font, m, pr, cx, y, TR(S::UiTips), cfgUiTips))
            saveSettings();

        y += 40;
        menuSectionHeader(font, cx, y, TR(S::SoundSection));
        y += 26;
        static const int vols[] = {0, 25, 50, 75, 100};
        drawTextS(font, TR(S::Volume), cx, y + 4, 13, Color{200, 190, 150, 255});
        if (ra2RedSlider(font, m, pr, cx + 140, y + 2, 180, cfgVolume, 5,
                         TextFormat("%d", vols[cfgVolume]))) {
            g_sfx.setMasterVol(vols[cfgVolume] / 100.0f);
            saveSettings();
        }
    } else {
        menuSectionHeader(font, cx, cy, TR(S::KeysSection));
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
            int y = cy + 28 + i * 28;
            bool armed = rebinding == i;
            DrawRectangle(cx, y - 1, listW, 26, armed ? Color{48, 28, 20, 255} : (i % 2 ? Color{14, 12, 14, 200} : Color{20, 16, 16, 200}));
            drawTextS(font, TR(names[i]), cx + 6, y + 5, 12,
                      armed ? Color{255, 226, 150, 255} : Color{255, 230, 90, 255});
            Rectangle kr{(float)keyBoxX, (float)y, (float)keyBoxW, 22};
            const char* kn = armed ? "…" : keyName(keyBind[i]);
            if (menuValueSlot(font, m, pr && rebinding < 0, kr, kn, true, false, true) && rebinding < 0) {
                rebinding = i;
                g_sfx.play(Sfx::Click, 0.5f);
            }
            if (armed)
                DrawRectangleLinesEx(kr, 1, Color{255, 200, 80, 255});
        }
        if (ra2Button(font, m, pr, {(float)cx, (float)(cy + 28 + maxShow * 28 + 6), 140, 28}, TR(S::ResetKeys), 12)) {
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
