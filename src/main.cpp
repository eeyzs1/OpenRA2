#include "game/game.h"
#include "gfx/sprites.h"
#include "gfx/bldmodels.h"
#include "gfx/vxl.h"
#include "gfx/pixel.h"
#include "sfx/sound.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <exception>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define CloseWindow CloseWindowWin32
#  define ShowCursor ShowCursorWin32
#  include <windows.h>
#  undef CloseWindow
#  undef ShowCursor
#endif

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

#ifdef _WIN32
// bat/`start` 关闭父控制台时会向子进程广播 CTRL_CLOSE；忽略以免秒退。
static BOOL WINAPI ra2ConsoleCtrlHandler(DWORD type) {
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleCtrlHandler(ra2ConsoleCtrlHandler, TRUE);
#endif
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
    // 运行时 VXL 样张：不进主循环，直接渲几辆车到 tools/ra2pack/out/
    if (argc > 1 && strcmp(argv[1], "--dump-vxl") == 0) {
        VxlRt::init();
        MakeDirectory("tools/ra2pack/out");
        struct { UnitType t; const char* name; bool tur; } samples[] = {
            {UnitType::Grizzly, "grizzly", true},
            {UnitType::Rhino, "rhino", true},
            {UnitType::Apocalypse, "apocalypse", true},
            {UnitType::Harvester, "harvester", false},
            {UnitType::IFV, "ifv", true},
            {UnitType::TeslaTank, "teslatank", true},
        };
        int ok = 0;
        for (auto& s : samples) {
            PixBuf body;
            if (!VxlRt::renderBody(s.t, 2, 0, body)) {
                printf("dump-vxl: FAIL body %s\n", s.name);
                continue;
            }
            if (s.tur) {
                PixBuf tur;
                if (VxlRt::renderTurret(s.t, 2, tur) && tur.w == body.w && tur.h == body.h)
                    body.blit(tur, 0, 0);
            }
            char path[256];
            snprintf(path, sizeof(path), "tools/ra2pack/out/rt_%s_d2.png", s.name);
            if (body.saveToFile(path)) {
                printf("dump-vxl: wrote %s (%dx%d)\n", path, body.w, body.h);
                ok++;
            }
        }
        printf("dump-vxl: %d/%d ok\n", ok, (int)(sizeof(samples) / sizeof(samples[0])));
        return ok > 0 ? 0 : 1;
    }
    // 已禁用：禁止程序生成 PNG 写入 assets（一律从 RA2 MIX 提取）
    if (argc > 1 && strcmp(argv[1], "--gen-assets") == 0) {
        fprintf(stderr, "--gen-assets disabled: use tools/ra2pack/gen_assets.py (MIX extract only)\n");
        return 1;
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
        int fails = 1;
        try {
            fails = game.smokeTest(frames);
        } catch (const std::exception& e) {
            TraceLog(LOG_ERROR, "SMOKE EXCEPTION: %s", e.what());
        } catch (...) {
            TraceLog(LOG_ERROR, "SMOKE EXCEPTION: unknown exception");
        }
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--smoke-campaign") == 0) {
        int mission = argc > 2 ? atoi(argv[2]) : 0;
        int frames = argc > 3 ? atoi(argv[3]) : 600;
        int fails = 1;
        try {
            fails = game.campaignSmokeTest(mission, frames);
        } catch (const std::exception& e) {
            TraceLog(LOG_ERROR, "CAMPAIGN SMOKE EXCEPTION: %s", e.what());
        } catch (...) {
            TraceLog(LOG_ERROR, "CAMPAIGN SMOKE EXCEPTION: unknown exception");
        }
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--campaign-matrix") == 0) {
        int frames = argc > 2 ? atoi(argv[2]) : 60;
        int fails = 1;
        try {
            fails = game.campaignMatrixTest(frames);
        } catch (const std::exception& e) {
            TraceLog(LOG_ERROR, "CAMPAIGN MATRIX EXCEPTION: %s", e.what());
        } catch (...) {
            TraceLog(LOG_ERROR, "CAMPAIGN MATRIX EXCEPTION: unknown exception");
        }
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--play-test") == 0) {
        int fails = game.playTest();
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    if (argc > 1 && strcmp(argv[1], "--visual-audit") == 0) {
        int fails = game.visualAudit();
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
    if (argc > 1 && strcmp(argv[1], "--state-test") == 0) {
        int failures = game.statePersistenceTest();
        game.shutdown();
        return failures == 0 ? 0 : 1;
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
