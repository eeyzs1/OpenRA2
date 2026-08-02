// 设置页：语言 / 显示模式 / 分辨率 / 音量 / 按键重绑定（全部热更新 + settings.ini 持久化）
#include "game/game.h"
#include "sfx/sound.h"

// 重绑定按键捕获：真实键盘或自动化脚本注入
int Game::pollAnyKey() {
    if (sim.active) {
        if (!sim.keysPressed.empty()) return *sim.keysPressed.begin();
        return 0;
    }
    return GetKeyPressed();
}

void Game::drawSettings() {
    drawMenuBackdrop(font, TR(S::Settings));
    Vector2 m = mousePos();
    bool pr = mPressed(MOUSE_LEFT_BUTTON);

    // ---------- 左面板：显示与声音 ----------
    int px = 48, py = 92, pw = 620;
    guiPanel(px, py, pw, 400);
    drawTextS(font, TR(S::DisplaySection), px + 24, py + 14, 19, Color{200, 196, 170, 255});
    // 行：标签 + 值按钮（点击循环切换）
    auto optRow = [&](int y, const char* label, const char* value, bool enabled) {
        drawTextS(font, label, px + 24, y + 8, 18,
                  enabled ? Color{190, 194, 200, 255} : Color{110, 112, 116, 255});
        Rectangle r{(float)px + 292, (float)y, 300, 36};
        bool hover = CheckCollisionPointRec(m, r) && enabled;
        guiSlot(r); // 凹陷金属槽
        DrawRectangleLinesEx(r, 1, hover ? GUI_GOLD_HI : Color{80, 76, 56, 255});
        drawTextS(font, value, (int)r.x + 150 - textW(font, value, 17) / 2, y + 9, 17,
                  enabled ? Color{255, 224, 130, 255} : Color{120, 122, 126, 255});
        return hover && pr;
    };
    int ry = py + 48; // 140
    if (optRow(ry, TR(S::Language), g_lang ? "English" : "中文", true)) {
        cfgLang = g_lang ? 0 : 1;
        g_lang = cfgLang; // 热更新：界面文本即刻切换
        g_sfx.play(Sfx::Click, 0.5f);
        saveSettings();
    }
    if (optRow(ry + 50, TR(S::WindowMode),
               cfgWindowMode == 0 ? TR(S::WMFullscreen) : TR(S::WMWindowed), true)) {
        cfgWindowMode = cfgWindowMode ? 0 : 1;
        displayDirty = true; // 渲染帧首应用，避免帧中改窗口
        g_sfx.play(Sfx::Click, 0.5f);
        saveSettings();
    }
    // 分辨率：仅窗口模式可选（无边框全屏跟随桌面）
    if (optRow(ry + 100, TR(S::Resolution),
               cfgWindowMode ? TextFormat("%d × %d", RES_LIST[cfgResIdx][0], RES_LIST[cfgResIdx][1])
                             : TR(S::ResDesktop),
               cfgWindowMode != 0)) {
        cfgResIdx = (cfgResIdx + 1) % 8;
        displayDirty = true;
        g_sfx.play(Sfx::Click, 0.5f);
        saveSettings();
    }
    static const int vols[] = {0, 25, 50, 75, 100};
    if (optRow(ry + 150, TR(S::Volume), TextFormat("%d", vols[cfgVolume]), true)) {
        cfgVolume = (cfgVolume + 1) % 5;
        g_sfx.setMasterVol(vols[cfgVolume] / 100.0f); // 热更新：音效+音乐即刻生效
        g_sfx.play(Sfx::Click, 0.5f);
        saveSettings();
    }

    // ---------- 右面板：按键设置 ----------
    int kx = 692, ky = 92, kw = SCREEN_W - kx - 48;
    guiPanel(kx, ky, kw, 608);
    drawTextS(font, TR(S::KeysSection), kx + 24, ky + 14, 19, Color{200, 196, 170, 255});
    drawTextS(font, TR(S::KeysTip), kx + 200, ky + 16, 15, Color{110, 112, 120, 255});
    static const S names[KA_COUNT] = {
        S::KaStop, S::KaUnload, S::KaDeploy, S::KaScatter, S::KaGuard, S::KaSameType,
        S::KaMusic, S::KaViewBase, S::KaPause, S::KaRally, S::KaSell,
        S::KaQuickSave, S::KaQuickLoad, S::KaSpeedUp, S::KaSpeedDown, S::KaWaypoint,
    };
    for (int i = 0; i < KA_COUNT; i++) {
        int y = ky + 48 + i * 34;
        bool armed = rebinding == i;
        // 交替行底色 + 棱台（armed 时金色调）
        DrawRectangle(kx + 8, y - 2, kw - 16, 34, armed ? Color{60, 48, 32, 255} : (i % 2 ? Color{24, 26, 30, 255} : Color{30, 32, 38, 255}));
        guiBevel({(float)(kx + 8), (float)(y - 2), (float)(kw - 16), 34}, false);
        drawTextS(font, TR(names[i]), kx + 24, y + 6, 17,
                  armed ? Color{255, 226, 150, 255} : Color{200, 204, 210, 255});
        Rectangle kr{(float)kx + 458, (float)y, 200, 30};
        bool hover = CheckCollisionPointRec(m, kr);
        guiSlot(kr); // 凹陷金属槽
        DrawRectangleLinesEx(kr, 1, armed ? Color{255, 120, 90, 255} : (hover ? GUI_GOLD_HI : Color{80, 76, 56, 255}));
        const char* kn = armed ? "…" : keyName(keyBind[i]);
        drawTextS(font, kn, (int)kr.x + 100 - textW(font, kn, 16) / 2, y + 7, 16, Color{255, 224, 130, 255});
        if (hover && pr && rebinding < 0) { rebinding = i; g_sfx.play(Sfx::Click, 0.5f); }
    }
    // 恢复默认按键
    if (ra2Button(font, m, pr, {(float)kx + 24, (float)(ky + 568), 200, 36}, TR(S::ResetKeys), 16)) {
        resetKeyBinds();
        rebinding = -1;
        saveSettings();
    }

    // ---------- 底部：返回 ----------
    if (ra2Button(font, m, pr, {(float)(SCREEN_W / 2 - 100), 724, 200, 48}, TR(S::Back), 20)) {
        rebinding = -1;
        if (settingsFromGame) { phase = Phase::InGame; showMenu = true; }
        else phase = Phase::MainMenu;
    }

    // ---------- 重绑定捕获（屏蔽其他点击）----------
    if (rebinding >= 0) {
        int k = pollAnyKey();
        if (k == KEY_ESCAPE) rebinding = -1;
        else if (k > 0) {
            // 冲突处理：同键其他动作让位（避免一键两义）
            for (int i = 0; i < KA_COUNT; i++)
                if (i != rebinding && keyBind[i] == k) keyBind[i] = 0; // 0=未绑定
            keyBind[rebinding] = k;
            rebinding = -1;
            g_sfx.play(Sfx::Click, 0.6f);
            saveSettings();
        }
        // 蒙层提示（金属面板风格）
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 140});
        const char* t = TR(S::PressKey);
        int tw = textW(font, t, 26);
        int bx = SCREEN_W / 2 - tw / 2 - 24, by = SCREEN_H / 2 - 30;
        guiPanel(bx, by, tw + 48, 60);
        drawTextS(font, t, SCREEN_W / 2 - tw / 2, SCREEN_H / 2 - 13, 26, Color{255, 226, 150, 255});
    }
}
