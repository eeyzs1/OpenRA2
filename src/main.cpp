#include "game/game.h"
#include <cstring>
#include <cstdlib>
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
    bool windowed = false;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--windowed") == 0) windowed = true;
    Game game;
    game.init(windowed);
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
    game.run();
    game.shutdown();
    return 0;
}
