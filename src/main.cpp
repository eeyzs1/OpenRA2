#include "game/game.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

int main(int argc, char** argv) {
#ifdef _DEBUG
    // 无头/CI 环境：CRT 断言输出到 stderr 而非弹窗（弹窗会阻塞且变成断点）
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    // 离线素材生成：程序绘制/合成 → 落盘 PNG/WAV，不创建窗口与音频设备
    if (argc > 1 && strcmp(argv[1], "--gen-assets") == 0) {
        bool okS = g_sprites.genAssets("assets/sprites");
        bool okA = g_sfx.genSfxAssets("assets/sfx");
        return (okS && okA) ? 0 : 1;
    }
    // 元数据模板导出：rules.ini / 24 关战役 INI / 双语字符串 / 音乐播放列表，不创建窗口
    if (argc > 1 && strcmp(argv[1], "--export-assets") == 0) {
        exportRules("assets/rules/rules.ini");
        exportCampaigns("assets/campaigns");
        exportStrings("assets/strings");
        // 音乐播放列表（运行时 assets/music/ 全目录扫描，本文件仅定义轮换顺序）
        MakeDirectory("assets/music");
        if (FILE* f = fopen("assets/music/music.ini", "wb")) {
            fprintf(f, "; OpenRA2 music playlist - Track=<file in assets/music>, order = rotation order.\n");
            fprintf(f, "; Files not listed here are appended after the listed ones. Delete file to restore scan order.\n");
            fprintf(f, "[Playlist]\nTrack=industrial_march.wav\nTrack=grind_heavy.wav\nTrack=overdrive_fast.wav\n");
            fclose(f);
        }
        printf("export-assets: rules/campaigns/strings/music templates written to assets/\n");
        return 0;
    }
    bool windowed = false;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--windowed") == 0) windowed = true;
    // 测试/截图等非交互模式：隐藏窗口运行，不打扰用户桌面
    bool testMode = argc > 1 && strncmp(argv[1], "--", 2) == 0 && strcmp(argv[1], "--windowed") != 0;
    Game game;
    game.init(windowed, testMode);
    if (argc > 1 && strcmp(argv[1], "--smoke") == 0) {
        int frames = argc > 2 ? atoi(argv[2]) : 600;
        game.smokeTest(frames);
        game.shutdown();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--smoke-campaign") == 0) {
        int mission = argc > 2 ? atoi(argv[2]) : 0;
        int frames = argc > 3 ? atoi(argv[3]) : 600;
        game.campaignSmokeTest(mission, frames);
        game.shutdown();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--play-test") == 0) {
        int fails = game.playTest();
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--menu-shot") == 0) {
        bool setup = argc > 2 && strcmp(argv[2], "setup") == 0;
        game.debugMenuShot(setup ? "menu_setup.png" : "menu_main.png", setup);
        game.shutdown();
        return 0;
    }
    // P8 联机双进程自测：先起 --net-host，再起 --net-client；两端日志比对校验和
    if (argc > 1 && (strcmp(argv[1], "--net-host") == 0 || strcmp(argv[1], "--net-client") == 0)) {
        int frames = argc > 2 ? atoi(argv[2]) : 900;
        int rc = game.netSelfTestDriver(strcmp(argv[1], "--net-host") == 0 ? 0 : 1, frames);
        game.shutdown();
        return rc;
    }
    game.run();
    game.shutdown();
    return 0;
}
