#include "game/game.h"
#include "game/campaign.h"
#include "game/data.h"
#include "gfx/sprites.h"
#include "gfx/bldmodels.h"
#include "gfx/vxl.h"
#include "gfx/pixel.h"
#include "sfx/sound.h"
#include "core/content.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>
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

    // 内容根 / Mod：--assets <dir> 切换游戏根；--mod <path> 可重复叠加载；自动扫描 mods/
    std::vector<std::string> cliMods;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--assets") && i + 1 < argc) {
            if (ChangeDirectory(argv[++i]))
                printf("ASSETS: --assets working dir -> %s\n", argv[i]);
            else
                fprintf(stderr, "ASSETS: --assets failed to chdir %s\n", argv[i]);
        } else if (!strcmp(argv[i], "--mod") && i + 1 < argc) {
            cliMods.push_back(argv[++i]);
        }
    }
    contentInit(cliMods);

    auto findFlag = [&](const char* flag) -> int {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], flag)) return i;
            // 跳过 --mod/--assets 的路径参数，避免把路径当成其它 flag
            if ((!strcmp(argv[i], "--mod") || !strcmp(argv[i], "--assets")) && i + 1 < argc) i++;
        }
        return -1;
    };
    auto flagInt = [&](int flagIdx, int offset, int def) -> int {
        int i = flagIdx + offset;
        if (i >= argc || !argv[i] || argv[i][0] == '-') return def;
        return atoi(argv[i]);
    };
    auto flagStr = [&](int flagIdx, int offset, const char* def) -> const char* {
        int i = flagIdx + offset;
        if (i >= argc || !argv[i] || argv[i][0] == '-') return def;
        return argv[i];
    };

    // 临时诊断命令（可与 --mod/--assets 任意顺序）
    if (findFlag("--dump-quads") >= 0)
        return dumpQuads(flagStr(findFlag("--dump-quads"), 1, "")[0] == 'p' ? BldType::PowerPlant : BldType::ConYard);
    // 运行时 VXL 样张：不进主循环，直接渲几辆车到 tools/ra2pack/out/
    if (findFlag("--dump-vxl") >= 0) {
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
    if (findFlag("--gen-assets") >= 0) {
        fprintf(stderr, "--gen-assets disabled: use tools/ra2pack/gen_assets.py (MIX extract only)\n");
        return 1;
    }
    // 元数据模板导出：rules.ini / 32 关战役 INI（中/盟/苏/尤里 各 8）/ 双语字符串 / 音乐播放列表，不创建窗口
    if (findFlag("--export-assets") >= 0) {
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
    bool windowed = findFlag("--windowed") >= 0;
    bool liveVerify = findFlag("--live-verify") >= 0;
    if (liveVerify) windowed = true;
    // 测试/截图等非交互模式：隐藏窗口；live-verify 必须可见才能打真实鼠标
    static const char* kTestFlags[] = {
        "--smoke", "--smoke-campaign", "--campaign-matrix", "--live-verify",
        "--play-test", "--visual-audit", "--bench-campaign", "--menu-shot",
        "--gui-review", "--shot", "--state-test", "--net-host", "--net-client",
        "--content-check",
    };
    bool testMode = false;
    if (!liveVerify) {
        for (const char* f : kTestFlags)
            if (findFlag(f) >= 0) { testMode = true; break; }
    }
    Game game;
    game.init(windowed, testMode);
    if (!game.assetsOk()) {
        fprintf(stderr, "FATAL: required assets missing — game will not start. See SPRITE-MISSING/SFX-MISSING/GUI-MISSING above.\n");
#ifdef _WIN32
        if (!testMode) {
            MessageBoxA(nullptr,
                "Required game assets are missing.\nExtract RA2 MIX assets first (tools/ra2pack).\nSee console/log for SPRITE-MISSING / SFX-MISSING.",
                "OpenRA2 — missing assets", MB_OK | MB_ICONERROR);
        }
#endif
        game.shutdown();
        return 1;
    }
    // 用户内容自检：打印 mods / 任务数 / 关键单位数值，便于确认 userdata 与 CSV 是否生效
    if (findFlag("--content-check") >= 0) {
        int fails = 0;
        printf("CONTENT-CHECK: mods=%d\n", (int)contentMods().size());
        for (const ContentMod& m : contentMods()) {
            printf("CONTENT-CHECK: mod id=%s enabled=%d order=%d path=%s\n",
                   m.id.c_str(), (int)m.enabled, m.loadOrder, m.path.c_str());
        }
        bool userOn = false;
        for (const ContentMod& m : contentMods())
            if ((m.path == "userdata/content" || m.id == "content") && m.enabled) userOn = true;
        if (!userOn) { printf("CONTENT-CHECK FAIL: userdata/content not enabled\n"); fails++; }
        else printf("CONTENT-CHECK PASS: userdata/content enabled\n");

        const auto& missions = missionTable();
        printf("CONTENT-CHECK: missions=%d variants=%d bldVariants=%d\n",
               (int)missions.size(), (int)g_variants.size(), (int)g_bldVariants.size());
        const UnitDef& g = unitDef(UnitType::Grizzly);
        printf("CONTENT-CHECK: Grizzly cost=%d hp=%d name=%s\n", g.cost, g.hp, g.name ? g.name : "?");
        const BldDef& cy = bldDef(BldType::ConYard);
        printf("CONTENT-CHECK: ConYard cost=%d hp=%d name=%s\n", cy.cost, cy.hp, cy.name ? cy.name : "?");

        std::string mapDemo = contentResolve("maps/example_demo.txt");
        printf("CONTENT-CHECK: maps/example_demo.txt -> %s\n",
               mapDemo.empty() ? "(missing)" : mapDemo.c_str());
        std::string unitsCsv = contentResolve("assets/rules/units.csv");
        printf("CONTENT-CHECK: assets/rules/units.csv -> %s\n",
               unitsCsv.empty() ? "(missing)" : unitsCsv.c_str());

        // 可选期望（兼容 cmd 把 a=b 拆成两个参数）：
        //   expect-grizzly-cost=888  或  expect-grizzly-cost 888
        auto matchExpect = [&](const char* key, int& i, int& wantOut) -> bool {
            size_t n = strlen(key);
            if (!strncmp(argv[i], key, n) && argv[i][n] == '=') {
                wantOut = atoi(argv[i] + n + 1);
                return true;
            }
            if (!strcmp(argv[i], key) && i + 1 < argc) {
                wantOut = atoi(argv[++i]);
                return true;
            }
            return false;
        };
        auto matchExpectStr = [&](const char* key, int& i, const char*& out) -> bool {
            size_t n = strlen(key);
            if (!strncmp(argv[i], key, n) && argv[i][n] == '=') {
                out = argv[i] + n + 1;
                return out && out[0];
            }
            if (!strcmp(argv[i], key) && i + 1 < argc) {
                out = argv[++i];
                return out && out[0];
            }
            return false;
        };

        for (int i = 1; i < argc; i++) {
            int want = 0;
            const char* sval = nullptr;
            if (matchExpect("expect-grizzly-cost", i, want)) {
                if (g.cost != want) {
                    printf("CONTENT-CHECK FAIL: Grizzly cost=%d want=%d\n", g.cost, want);
                    fails++;
                } else printf("CONTENT-CHECK PASS: Grizzly cost=%d\n", g.cost);
            } else if (matchExpect("expect-grizzly-hp", i, want)) {
                if (g.hp != want) {
                    printf("CONTENT-CHECK FAIL: Grizzly hp=%d want=%d\n", g.hp, want);
                    fails++;
                } else printf("CONTENT-CHECK PASS: Grizzly hp=%d\n", g.hp);
            } else if (matchExpect("expect-missions", i, want)) {
                if ((int)missions.size() != want) {
                    printf("CONTENT-CHECK FAIL: missions=%d want=%d\n", (int)missions.size(), want);
                    fails++;
                } else printf("CONTENT-CHECK PASS: missions=%d\n", (int)missions.size());
            } else if (matchExpect("expect-missions-min", i, want)) {
                if ((int)missions.size() < want) {
                    printf("CONTENT-CHECK FAIL: missions=%d want>=%d\n", (int)missions.size(), want);
                    fails++;
                } else printf("CONTENT-CHECK PASS: missions=%d (>=%d)\n", (int)missions.size(), want);
            } else if (matchExpectStr("expect-map", i, sval)) {
                std::string got = contentResolve(sval);
                if (got.empty()) {
                    printf("CONTENT-CHECK FAIL: map missing %s\n", sval);
                    fails++;
                } else printf("CONTENT-CHECK PASS: map %s -> %s\n", sval, got.c_str());
            } else if (matchExpectStr("expect-variant", i, sval)) {
                if (!findVariant(sval)) {
                    printf("CONTENT-CHECK FAIL: variant missing %s\n", sval);
                    fails++;
                } else printf("CONTENT-CHECK PASS: variant %s\n", sval);
            }
        }
        printf("CONTENT-CHECK SUMMARY: %s fails=%d\n", fails ? "FAIL" : "PASS", fails);
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    if (int fi = findFlag("--smoke"); fi >= 0) {
        int frames = flagInt(fi, 1, 600);
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
    if (int fi = findFlag("--smoke-campaign"); fi >= 0) {
        int mission = flagInt(fi, 1, 0);
        int frames = flagInt(fi, 2, 600);
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
    if (int fi = findFlag("--campaign-matrix"); fi >= 0) {
        int frames = flagInt(fi, 1, 60);
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
    if (liveVerify) {
        int fails = game.liveVerify();
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    if (findFlag("--play-test") >= 0) {
        int fails = game.playTest();
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    if (findFlag("--visual-audit") >= 0) {
        int fails = game.visualAudit();
        game.shutdown();
        return fails == 0 ? 0 : 1;
    }
    // 临时诊断：战役真实渲染耗时（逻辑/渲染分离计时）
    if (int fi = findFlag("--bench-campaign"); fi >= 0) {
        int mission = flagInt(fi, 1, 0);
        int warm = flagInt(fi, 2, 1800);
        int frames = flagInt(fi, 3, 300);
        game.benchCampaign(mission, warm, frames);
        game.shutdown();
        return 0;
    }
    if (int fi = findFlag("--menu-shot"); fi >= 0) {
        bool setup = flagStr(fi, 1, "") && !strcmp(flagStr(fi, 1, ""), "setup");
        game.debugMenuShot(setup ? "menu_setup.png" : "menu_main.png", setup);
        game.shutdown();
        return 0;
    }
    if (int fi = findFlag("--gui-review"); fi >= 0) {
        const char* dir = flagStr(fi, 1, "gui_review");
        game.debugGuiReview(dir);
        game.shutdown();
        return 0;
    }
    if (int fi = findFlag("--shot"); fi >= 0) {
        int warm = flagInt(fi, 1, 2400);
        game.debugShot(warm, "shot_game.png");
        game.shutdown();
        return 0;
    }
    if (findFlag("--state-test") >= 0) {
        int failures = game.statePersistenceTest();
        game.shutdown();
        return failures == 0 ? 0 : 1;
    }
    // P8 联机双进程自测：先起 --net-host，再起 --net-client；两端日志比对校验和
    if (int fi = findFlag("--net-host"); fi >= 0) {
        int frames = flagInt(fi, 1, 900);
        int rc = game.netSelfTestDriver(0, frames);
        game.shutdown();
        return rc;
    }
    if (int fi = findFlag("--net-client"); fi >= 0) {
        int frames = flagInt(fi, 1, 900);
        int rc = game.netSelfTestDriver(1, frames);
        game.shutdown();
        return rc;
    }
    game.run();
    game.shutdown();
    return 0;
}
