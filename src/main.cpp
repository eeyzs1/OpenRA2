#include "game/game.h"
#include "gfx/sprites.h"
#include "gfx/bldmodels.h"
#include "sfx/sound.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

// 临时诊断：导出建筑模型四边形 CSV（离线几何验证用）
static int dumpQuads(BldType t) {
    M3Builder mb;
    if (!buildBldModel3D(t, mb)) { printf("no model\n"); return 1; }
    FILE* f = fopen("quads.csv", "w");
    fprintf(f, "x0,y0,z0,x1,y1,z1,x2,y2,z2,x3,y3,z3,r,g,b\n");
    for (const M3Quad& q : mb.quads)
        fprintf(f, "%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%g,%d,%d,%d\n",
                q.v[0][0], q.v[0][1], q.v[0][2], q.v[1][0], q.v[1][1], q.v[1][2],
                q.v[2][0], q.v[2][1], q.v[2][2], q.v[3][0], q.v[3][1], q.v[3][2],
                q.color.r, q.color.g, q.color.b);
    fclose(f);
    printf("dumped %zu quads\n", mb.quads.size());
    return 0;
}
#ifdef _DEBUG
#include <crtdbg.h>
#endif

// 资源目录自定位：双击 exe 时工作目录=exe 所在目录（如 build\Release\），
// 而 assets/ 在项目根——依次尝试 CWD、exe 旁、exe 上级目录链，找到后切换工作目录，
// 保证 rules/素材/音乐/存档在任何启动方式（双击/快捷方式/命令行）下都能加载。
static void locateAssetsDir() {
    const char* probe = "assets/rules/rules.ini";
    if (FileExists(probe)) return; // CWD 已正确（命令行从项目根启动）
    const char* appDir = GetApplicationDirectory(); // raylib：exe 所在目录（带尾部分隔符）
    char buf[512];
    const char* ups[] = { "", "../", "../../", "../../../" };
    for (const char* up : ups) {
        snprintf(buf, sizeof buf, "%s%s", appDir, up);
        if (ChangeDirectory(buf) && FileExists(probe)) {
            printf("ASSETS: relocated working dir near exe (%s)\n", buf);
            return;
        }
    }
    printf("ASSETS: WARNING assets/rules/rules.ini not found, keep CWD\n");
}

int main(int argc, char** argv) {
#ifdef _DEBUG
    // 无头/CI 环境：CRT 断言输出到 stderr 而非弹窗（弹窗会阻塞且变成断点）
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    locateAssetsDir(); // 资源根目录自定位（双击 exe 也能找到 assets/）
    // 临时诊断命令
    if (argc > 1 && strcmp(argv[1], "--dump-quads") == 0)
        return dumpQuads(argc > 2 && argv[2][0] == 'p' ? BldType::PowerPlant : BldType::ConYard);
    // 离线素材生成：程序绘制/合成 → 落盘 PNG/WAV，不创建窗口与音频设备
    if (argc > 1 && strcmp(argv[1], "--gen-assets") == 0) {
        bool okS = g_sprites.genAssets("assets/sprites");
        bool okA = g_sfx.genSfxAssets("assets/sfx");
        return (okS && okA) ? 0 : 1;
    }
    // 元数据模板导出：rules.ini / 32 关战役 INI（中/盟/苏/尤里 各 8）/ 双语字符串 / 音乐播放列表，不创建窗口
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
    // 临时诊断：战役真实渲染耗时（逻辑/渲染分离计时）
    if (argc > 1 && strcmp(argv[1], "--bench-campaign") == 0) {
        int mission = argc > 2 ? atoi(argv[2]) : 0;
        int warm = argc > 3 ? atoi(argv[3]) : 1800;
        int frames = argc > 4 ? atoi(argv[4]) : 300;
        game.benchCampaign(mission, warm, frames);
        game.shutdown();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--menu-shot") == 0) {
        bool setup = argc > 2 && strcmp(argv[2], "setup") == 0;
        game.debugMenuShot(setup ? "menu_setup.png" : "menu_main.png", setup);
        game.shutdown();
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--shot") == 0) {
        int warm = argc > 2 ? atoi(argv[2]) : 2400;
        game.debugShot(warm, "shot_game.png");
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
