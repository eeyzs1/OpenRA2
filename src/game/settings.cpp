// 应用设置：settings.ini 读写 + 显示模式/分辨率热切换 + 默认键位
#include "game/game.h"
#include "sfx/sound.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// 可选窗口分辨率（16:9；无边框全屏时忽略，跟随桌面）
const int Game::RES_LIST[4][2] = {{1280, 720}, {1440, 810}, {1600, 900}, {1920, 1080}};

static constexpr const char* SETTINGS_PATH = "settings.ini";

// RA2 原作默认键位
static const int DEF_KEYS[KA_COUNT] = {
    KEY_S,      // KA_Stop
    KEY_U,      // KA_Unload
    KEY_D,      // KA_Deploy
    KEY_X,      // KA_Scatter
    KEY_G,      // KA_Guard
    KEY_T,      // KA_SameType
    KEY_M,      // KA_Music
    KEY_H,      // KA_ViewBase
    KEY_P,      // KA_Pause
    KEY_R,      // KA_Rally
    KEY_DELETE, // KA_Sell
    KEY_F5,     // KA_QuickSave
    KEY_F9,     // KA_QuickLoad
    KEY_EQUAL,  // KA_SpeedUp
    KEY_MINUS,  // KA_SpeedDown
};

void Game::resetKeyBinds() {
    for (int i = 0; i < KA_COUNT; i++) keyBind[i] = DEF_KEYS[i];
}

void Game::loadSettings() {
    resetKeyBinds(); // 先填默认，文件缺失项保持默认
    FILE* f = fopen(SETTINGS_PATH, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        int v = atoi(eq + 1);
        if (strcmp(line, "lang") == 0) cfgLang = v ? 1 : 0;
        else if (strcmp(line, "window_mode") == 0) cfgWindowMode = v ? 1 : 0;
        else if (strcmp(line, "resolution") == 0) { if (v >= 0 && v < 4) cfgResIdx = v; }
        else if (strcmp(line, "volume") == 0) { if (v >= 0 && v <= 4) cfgVolume = v; }
        else if (strncmp(line, "key_", 4) == 0) {
            int a = atoi(line + 4);
            if (a >= 0 && a < KA_COUNT && v > 0) keyBind[a] = v;
        }
    }
    fclose(f);
    g_lang = cfgLang; // 语言全局生效
}

void Game::saveSettings() const {
    FILE* f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    fprintf(f, "lang=%d\n", cfgLang);
    fprintf(f, "window_mode=%d\n", cfgWindowMode);
    fprintf(f, "resolution=%d\n", cfgResIdx);
    fprintf(f, "volume=%d\n", cfgVolume);
    for (int i = 0; i < KA_COUNT; i++) fprintf(f, "key_%d=%d\n", i, keyBind[i]);
    fclose(f);
}

// 显示模式与分辨率热切换：无边框全屏 ⇄ 窗口（letterbox 渲染自动适配任意尺寸）
void Game::applyDisplay() {
    if (cfgWindowMode == 0) {
        if (!borderlessActive) {
            ToggleBorderlessWindowed();
            borderlessActive = true;
        }
    } else {
        if (borderlessActive) {
            ToggleBorderlessWindowed();
            borderlessActive = false;
        }
        SetWindowSize(RES_LIST[cfgResIdx][0], RES_LIST[cfgResIdx][1]);
    }
}
