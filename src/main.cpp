#include "game/game.h"
#ifdef RA2_TESTING
#include "game_regression.h"
using GameApplication=GameRegression;
#else
using GameApplication=Game;
#endif
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
        const int outputFlag=findFlag("--dump-dir");
        const std::string output=outputFlag>=0?flagStr(outputFlag,1,"tools/ra2pack/out"):"tools/ra2pack/out";
        MakeDirectory(output.c_str());
        struct { UnitType t; const char* name; bool tur; } samples[] = {
            {UnitType::Grizzly, "grizzly", true},
            {UnitType::Rhino, "rhino", true},
            {UnitType::Apocalypse, "apocalypse", true},
            {UnitType::Harvester, "harvester", false},
            {UnitType::IFV, "ifv", true},
            {UnitType::TeslaTank, "teslatank", true},
        };
        int ok = 0;
        if(findFlag("--all-facings")>=0) {
            // Native source layers for the documented fusion asset pipeline.
            for(int direction=0;direction<8;++direction) {
                PixBuf body,turret;
                if(!VxlRt::renderBody(UnitType::Rhino,direction,0,body)
                    || !VxlRt::renderTurret(UnitType::Rhino,direction,turret)) return 1;
                if(!body.saveToFile(TextFormat("%s/native_rhino_body_d%d.png",output.c_str(),direction))
                    || !turret.saveToFile(TextFormat("%s/native_rhino_turret_d%d.png",output.c_str(),direction))) return 1;
            }
            FILE* anchors=fopen((output+"/native_siege_anchors.csv").c_str(),"wb");
            if(!anchors)return 1;
            std::fprintf(anchors,"direction,pose,frame,x,y\n");
            for(int direction=0;direction<8;++direction) {
                for(int frame=0;frame<2;++frame) {
                    PixBuf flight;Vec2i anchor;
                    if(!VxlRt::renderBody(UnitType::SiegeChopper,direction,frame,flight,nullptr,&anchor)
                        || !flight.saveToFile(TextFormat("%s/native_siege_flight_d%d_f%d.png",output.c_str(),direction,frame))){fclose(anchors);return 1;}
                    std::fprintf(anchors,"%d,flight,%d,%d,%d\n",direction,frame,anchor.x,anchor.y);
                }
                PixBuf ground;Vec2i anchor;
                if(!VxlRt::renderDeployed(UnitType::SiegeChopper,direction,direction,ground,anchor)
                    || !ground.saveToFile(TextFormat("%s/native_siege_ground_d%d.png",output.c_str(),direction))){fclose(anchors);return 1;}
                std::fprintf(anchors,"%d,ground,0,%d,%d\n",direction,anchor.x,anchor.y);
            }
            if(fclose(anchors)!=0)return 1;
            printf("dump-vxl: native rhino source layers 8/8\n");
        }
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
            snprintf(path, sizeof(path), "%s/rt_%s_d2.png",output.c_str(),s.name);
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
        exportStrings("assets/strings");
        // 音乐播放列表（运行时 assets/music/ 全目录扫描，本文件仅定义轮换顺序）
        MakeDirectory("assets/music");
        if (FILE* f = fopen("assets/music/music.ini", "wb")) {
            fprintf(f, "; OpenRA2 music playlist - Track=<file in assets/music>, order = rotation order.\n");
            fprintf(f, "; Files not listed here are appended after the listed ones. Delete file to restore scan order.\n");
            fprintf(f, "[Playlist]\nTrack=industrial_march.wav\nTrack=grind_heavy.wav\nTrack=overdrive_fast.wav\n");
            fclose(f);
        }
        printf("export-assets: rules/strings/music templates written to assets/\n");
        return 0;
    }
    bool windowed = findFlag("--windowed") >= 0;
    bool liveVerify = findFlag("--live-verify") >= 0;
    if (liveVerify) windowed = true;
    // 测试/截图等非交互模式：隐藏窗口；live-verify 必须可见才能打真实鼠标
    static const char* kTestFlags[] = {
        "--ai-construction-regression",
        "--native-script-control-regression",
        "--native-production-regression",
        "--native-ai-regression",
        "--native-global-ai-regression",
        "--native-global-app-regression",
        "--native-house-leader-regression",
        "--native-nearby-regression",
        "--native-nearby-runtime-regression",
        "--native-source-coordinate-regression",
        "--native-crate-regression",
        "--smoke", "--smoke-campaign", "--campaign-matrix", "--live-verify", "--ai-economy-regression", "--ai-transport-regression", "--ai-air-transport-regression", "--ai-factory-exit-regression", "--ai-air-defense-regression", "--moving-yield-regression", "--junction-regression", "--combat-pressure-regression", "--apocalypse-regression", "--burst-regression", "--splash-regression",
        "--play-test", "--visual-audit", "--bench-campaign", "--menu-shot",
        "--generated-match", "--generated-map-structure", "--generated-map-health", "--factory-state-regression", "--chrono-visual-regression", "--superweapon-regression", "--fog-feedback-regression", "--fog-knowledge-regression", "--terrain-visual-regression", "--refund-regression", "--production-feedback-regression", "--placement-regression", "--reference-map-regression", "--gui-review", "--shot", "--state-test", "--app-state-test", "--visual-regression", "--animation-regression", "--attack-regression", "--engineer-regression", "--lifecycle-regression", "--entry-regression", "--release-regression", "--instant-combat-regression", "--service-regression", "--hud-regression", "--skirmish-regression", "--movement-regression", "--traffic-regression", "--blocker-regression", "--idle-yield-regression", "--chopper-ground-regression", "--immobilized-regression", "--deployment-feedback-regression", "--rally-regression", "--rally-visual-regression", "--refinery-recovery-regression", "--feedback-visual-regression", "--magnetic-landing-regression", "--ai-attack-regression", "--magnetron-regression", "--economy-regression", "--presentation-replay", "--net-host", "--net-client",
        "--siege-pose-regression",
        "--voxel-animation-regression",
        "--chopper-transition-regression",
        "--elevation-feedback-regression",
        "--content-check",
        "--net-protocol-regression",
        "--net-production-regression",
        "--modal-input-regression",
        "--classic-input-regression",
        "--original-skirmish-regression", "--original-skirmish-natural",
        "--ai-scouting-regression", "--gap-height-regression", "--gap-slope-regression", "--source-transport-regression", "--cargo-ownership-regression", "--ground-unload-regression", "--ground-control-regression", "--ground-travel-regression", "--crushing-regression", "--drive-blockage-regression",
        "--entity-identity-regression",
        "--transport-exit-regression",
        "--allied-boarding-regression",
        "--transport-boarding-regression", "--building-repair-regression", "--building-sale-regression", "--building-survivors-regression", "--player-scatter-regression", "--infantry-travel-regression",
        "--rules-profile-regression", "--source-match-regression", "--source-mode-regression", "--source-economy-regression", "--source-support-regression", "--support-delivery-regression", "--source-crate-regression", "--native-gatling-regression", "--rules-consistency-regression",
        "--rules-selection-regression",
        "--rules-spawner-regression",
        "--aircraft-spawner-regression",
        "--asw-spawner-regression",
        "--group-order-regression",
        "--campaign-save-regression",
        "--unit-voice-regression",
        "--native-audio-regression", "--native-condition-regression", "--native-playthrough-regression", "--native-motion-regression", "--native-movement-regression", "--native-flight-regression", "--native-jumpjet-regression", "--air-ground-regression", "--air-production-regression", "--attack-move-regression",
        "--native-map-regression",
        "--native-scene-regression",
        "--native-bridge-regression",
        "--native-bridge-repair-regression",
        "--native-gate-regression",
        "--native-event-regression",
        "--native-team-regression",
        "--native-team-orders-regression",
        "--native-team-attack-regression",
        "--native-weapon-regression", "--native-weapon-actions-regression", "--native-shrapnel-regression", "--native-ifv-regression", "--native-temporal-regression", "--native-crew-regression",
        "--native-rocket-regression",
        "--native-v3-regression",
        "--native-mission-regression",
        "--native-campaign-regression",
        "--native-demolition-regression",
    };
    bool testMode = false;
    if (!liveVerify) {
        for (const char* f : kTestFlags)
            if (findFlag(f) >= 0) { testMode = true; break; }
    }
#ifndef RA2_TESTING
    if(testMode || liveVerify) {
        fprintf(stderr,"Regression and diagnostic commands require ra2_regression.\n");
        return 2;
    }
#endif
    GameApplication game;
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
#ifdef RA2_TESTING
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
        return fails==77?77:fails==0?0:1;
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
        if(mission<0 || mission>=(int)missionTable().size()) {
            TraceLog(LOG_ERROR,"No installed campaign at index %d",mission);
            game.shutdown();
            return 1;
        }
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
    if (int fi=findFlag("--presentation-replay");fi>=0) {
        int failures=game.presentationReplay(flagInt(fi,1,0)!=0);
        game.shutdown(); return failures==0?0:1;
    }
    if (findFlag("--economy-regression") >= 0) {
        int failures = game.economyRegression();
        game.shutdown();
        return failures == 0 ? 0 : 1;
    }
    if (findFlag("--movement-regression") >= 0) {
        int failures = game.movementRegression();
        game.shutdown();
        return failures == 0 ? 0 : 1;
    }
    if (int fi = findFlag("--skirmish-regression"); fi >= 0) {
        int failures = game.skirmishRegression((std::max)(1, flagInt(fi, 1, 36000)), flagInt(fi,2,-1), (uint64_t)flagInt(fi,3,0),findFlag("--require-outcomes")>=0);
        game.shutdown();
        return failures == 0 ? 0 : 1;
    }
    if (int fi=findFlag("--visual-regression");fi>=0) {
        int failures=game.visualRegression(flagStr(fi,1,"review_out/visual"));
        game.shutdown();return failures==0?0:1;
    }
    if (int fi=findFlag("--siege-pose-regression");fi>=0) {
        int failures=game.siegePoseRegression(flagStr(fi,1,"review_out/siege-pose"));
        game.shutdown();return failures==0?0:1;
    }
    if (int fi=findFlag("--voxel-animation-regression");fi>=0) {
        int failures=game.voxelAnimationRegression(flagStr(fi,1,"review_out/voxel-animation"));
        game.shutdown();return failures==0?0:1;
    }
    if (int fi=findFlag("--chopper-transition-regression");fi>=0) {
        int failures=game.chopperTransitionRegression(flagStr(fi,1,"review_out/chopper-transition"));
        game.shutdown();return failures==0?0:1;
    }
    if (int fi=findFlag("--elevation-feedback-regression");fi>=0) {
        int failures=game.elevationFeedbackRegression(flagStr(fi,1,"review_out/elevation-feedback"));
        game.shutdown();return failures==0?0:1;
    }
    if (int fi=findFlag("--animation-regression");fi>=0) {
        int failures=game.animationRegression(flagStr(fi,1,"review_out/animation"));
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--attack-regression")>=0) {
        int failures=game.attackRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--engineer-regression")>=0) {
        int failures=game.engineerRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--lifecycle-regression")>=0) {
        int failures=game.lifecycleRegression();game.shutdown();return failures==0?0:1;
    }
    if(int fi=findFlag("--generated-match");fi>=0) {
        int failures=game.skirmishRegression((std::max)(1,flagInt(fi,1,72000)),flagInt(fi,2,-1),(uint64_t)flagInt(fi,3,0),true,(std::clamp)(flagInt(fi,4,1),0,6));
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--generated-map-structure")>=0 || findFlag("--generated-map-health")>=0) {
        int index=findFlag("--generated-map-structure");
        int failures=game.generatedMapRegression(findFlag("--generated-map-health")>=0,index>=0 && index+1<argc?argv[index+1]:"review_out/generated-map");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--factory-state-regression")>=0) {
        int i=findFlag("--factory-state-regression");
        int failures=game.factoryStateRegression(i+1<argc?argv[i+1]:"review_out/factory-state");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--chrono-visual-regression")>=0) {
        int i=findFlag("--chrono-visual-regression");
        int failures=game.chronoVisualRegression(i+1<argc?argv[i+1]:"review_out/chrono");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--superweapon-regression")>=0) {
        int i=findFlag("--superweapon-regression");
        int failures=game.superweaponRegression(i+1<argc?argv[i+1]:"review_out/superweapon");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--fog-feedback-regression")>=0) {
        int i=findFlag("--fog-feedback-regression");
        int failures=game.fogFeedbackRegression(i+1<argc?argv[i+1]:"review_out/fog-feedback");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--fog-knowledge-regression")>=0) {
        int i=findFlag("--fog-knowledge-regression");
        int failures=game.fogKnowledgeRegression(i+1<argc?argv[i+1]:"review_out/fog-knowledge");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--terrain-visual-regression")>=0) {
        int i=findFlag("--terrain-visual-regression");
        int failures=game.terrainVisualRegression(i+1<argc?argv[i+1]:"review_out/terrain");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--refund-regression")>=0) {
        int i=findFlag("--refund-regression");
        int failures=game.refundRegression(i+1<argc?argv[i+1]:"review_out/refund");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--production-feedback-regression")>=0) {
        int i=findFlag("--production-feedback-regression");
        int failures=game.productionFeedbackRegression(i+1<argc?argv[i+1]:"review_out/production-feedback");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--placement-regression")>=0) {
        int i=findFlag("--placement-regression");
        int failures=game.placementRegression(i+1<argc?argv[i+1]:"review_out/placement");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--reference-map-regression")>=0) {
        int i=findFlag("--reference-map-regression");
        int failures=game.referenceMapRegression(i+1<argc?argv[i+1]:"review_out/reference-map");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--native-map-regression")>=0) {
        const int i=findFlag("--native-map-regression");
        const int failures=game.nativeMapRegression(i+1<argc?argv[i+1]:"review_out/native-map");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-scene-regression")>=0) {
        const int i=findFlag("--native-scene-regression");
        const int failures=game.nativeSceneRegression(i+1<argc?argv[i+1]:"review_out/native-scene");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-bridge-regression")>=0) {
        const int i=findFlag("--native-bridge-regression");
        const int failures=game.nativeBridgeRegression(i+1<argc?argv[i+1]:"review_out/native-bridge");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-event-regression")>=0) {
        const int i=findFlag("--native-event-regression");
        const int failures=game.nativeEventRegression(i+1<argc?argv[i+1]:"review_out/native-events");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-team-regression")>=0) {
        const int i=findFlag("--native-team-regression");
        const int failures=game.nativeTeamRegression(i+1<argc?argv[i+1]:"review_out/native-teams");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-team-orders-regression")>=0) {
        const int i=findFlag("--native-team-orders-regression");
        const int failures=game.nativeTeamOrdersRegression(i+1<argc?argv[i+1]:"review_out/native-team-orders");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-team-attack-regression")>=0) {
        const int i=findFlag("--native-team-attack-regression");
        const int failures=game.nativeTeamAttackRegression(i+1<argc?argv[i+1]:"review_out/native-team-attack");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(int at=findFlag("--native-weapon-actions-regression");at>=0) {
        const int failures=game.nativeWeaponActionsRegression(at+1<argc?argv[at+1]:"review_out/native-weapon-actions");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--native-shrapnel-regression");at>=0) {
        const int failures=game.nativeShrapnelRegression(at+1<argc?argv[at+1]:"review_out/native-shrapnel");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--native-ifv-regression");at>=0) {
        const int failures=game.nativeIFVRegression(at+1<argc?argv[at+1]:"review_out/native-ifv");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--native-crew-regression");at>=0) {
        const int failures=game.nativeCrewRegression(at+1<argc?argv[at+1]:"review_out/native-crew");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--native-temporal-regression");at>=0) {
        const int failures=game.nativeTemporalRegression(at+1<argc?argv[at+1]:"review_out/native-temporal");
        game.shutdown();return failures?1:0;
    }
    if(findFlag("--native-weapon-regression")>=0) {
        const int i=findFlag("--native-weapon-regression");
        const int failures=game.nativeWeaponRegression(i+1<argc?argv[i+1]:"review_out/native-weapons");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-rocket-regression")>=0) {
        const int i=findFlag("--native-rocket-regression");
        const int failures=game.nativeRocketRegression(i+1<argc?argv[i+1]:"review_out/native-rockets");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-v3-regression")>=0) {
        const int i=findFlag("--native-v3-regression");
        const int failures=game.nativeV3Regression(i+1<argc?argv[i+1]:"review_out/native-v3");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-campaign-regression")>=0) {
        const int i=findFlag("--native-campaign-regression");
        const int failures=game.nativeCampaignRegression(i+1<argc?argv[i+1]:"review_out/native-campaign");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-mission-regression")>=0) {
        const int i=findFlag("--native-mission-regression");
        const int failures=game.nativeMissionRegression(i+1<argc?argv[i+1]:"review_out/native-mission");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-demolition-regression")>=0) {
        const int i=findFlag("--native-demolition-regression");
        const int failures=game.nativeDemolitionRegression(i+1<argc?argv[i+1]:"review_out/native-demolition");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-script-control-regression")>=0) {
        const int i=findFlag("--native-script-control-regression");
        const int failures=game.nativeScriptControlRegression(i+1<argc?argv[i+1]:"review_out/native-script-control");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-source-coordinate-regression")>=0) {
        const int i=findFlag("--native-source-coordinate-regression");
        const int failures=game.nativeSourceCoordinateRegression(i+1<argc?argv[i+1]:"review_out/native-global-ai");
        game.shutdown();return failures==77?77:failures?1:0;
    }
    if(findFlag("--native-nearby-runtime-regression")>=0) {
        const int i=findFlag("--native-nearby-runtime-regression");
        const int failures=game.nativeNearbyRuntimeRegression(i+1<argc?argv[i+1]:"review_out/native-global-ai");
        game.shutdown();return failures==77?77:failures?1:0;
    }
    if(findFlag("--native-nearby-regression")>=0) {
        const int failures=game.nativeNearbyRegression();
        game.shutdown();return failures?1:0;
    }
    if(findFlag("--native-house-leader-regression")>=0) {
        const int i=findFlag("--native-house-leader-regression");
        const int failures=game.nativeHouseLeaderRegression(i+1<argc?argv[i+1]:"review_out/native-global-ai");
        game.shutdown();return failures==77?77:failures?1:0;
    }
    if(findFlag("--native-global-app-regression")>=0) {
        const int i=findFlag("--native-global-app-regression");
        const int failures=game.nativeGlobalAppRegression(i+1<argc?argv[i+1]:"review_out/native-global-ai");
        game.shutdown();return failures==77?77:failures?1:0;
    }
    if(findFlag("--native-global-ai-regression")>=0) {
        const int i=findFlag("--native-global-ai-regression");
        const int failures=game.nativeGlobalAIRegression(i+1<argc?argv[i+1]:"review_out/native-global-ai");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-ai-regression")>=0) {
        const int i=findFlag("--native-ai-regression");
        const int failures=game.nativeAIRegression(i+1<argc?argv[i+1]:"review_out/native-ai");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-crate-regression")>=0) {
        const int i=findFlag("--native-crate-regression");
        const int failures=game.nativeCrateRegression(i+1<argc?argv[i+1]:"review_out/native-crates");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-condition-regression")>=0) {
        const int i=findFlag("--native-condition-regression");
        const int failures=game.nativeConditionRegression(i+1<argc?argv[i+1]:"review_out/native-conditions");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-playthrough-regression")>=0) {
        const int i=findFlag("--native-playthrough-regression");
        const int failures=game.nativePlaythroughRegression(i+1<argc?argv[i+1]:"review_out/native-playthrough");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-motion-regression")>=0) {
        const int i=findFlag("--native-motion-regression");
        const int failures=game.nativeMotionRegression(i+1<argc?argv[i+1]:"review_out/native-motion");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-movement-regression")>=0) {
        const int i=findFlag("--native-movement-regression");
        const int failures=game.nativeMovementRegression(i+1<argc?argv[i+1]:"review_out/native-movement");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-flight-regression")>=0) {
        const int i=findFlag("--native-flight-regression");
        const int failures=game.nativeFlightRegression(i+1<argc?argv[i+1]:"review_out/native-flight");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--native-jumpjet-regression")>=0) {
        const int i=findFlag("--native-jumpjet-regression");
        const int failures=game.nativeJumpjetRegression(i+1<argc?argv[i+1]:"review_out/native-jumpjet");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--air-ground-regression")>=0) {
        const int i=findFlag("--air-ground-regression");
        const int failures=game.airGroundRegression(i+1<argc?argv[i+1]:"review_out/air-ground");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--air-production-regression")>=0) {
        const int i=findFlag("--air-production-regression");
        const int failures=game.airProductionRegression(i+1<argc?argv[i+1]:"review_out/air-production");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--attack-move-regression")>=0) {
        const int i=findFlag("--attack-move-regression");
        const int failures=game.attackMoveRegression(i+1<argc?argv[i+1]:"review_out/attack-move");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--native-production-regression")>=0) {
        const int i=findFlag("--native-production-regression");
        const int failures=game.nativeProductionRegression(i+1<argc?argv[i+1]:"review_out/native-production");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-gate-regression")>=0) {
        const int i=findFlag("--native-gate-regression");
        const int failures=game.nativeGateRegression(i+1<argc?argv[i+1]:"review_out/native-gates");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--native-bridge-repair-regression")>=0) {
        const int i=findFlag("--native-bridge-repair-regression");
        const int failures=game.nativeBridgeRepairRegression(i+1<argc?argv[i+1]:"review_out/native-bridge-repair");
        game.shutdown();return failures==0?0:failures==77?77:1;
    }
    if(findFlag("--entry-regression")>=0) {
        int failures=game.entryRegression();game.shutdown();return failures==0?0:1;
    }
    if(int fi=findFlag("--hud-regression");fi>=0) {
        int failures=game.hudRegression(fi+1<argc?argv[fi+1]:"review_out/hud");game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--moving-yield-regression")>=0) {
        int failures=game.movingYieldRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--ai-transport-regression")>=0) {
        int failures=game.aiTransportRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--ai-air-transport-regression")>=0) {
        const int i=findFlag("--ai-air-transport-regression");
        int failures=game.aiAirTransportRegression(i+1<argc?argv[i+1]:"review_out/ai-air-transport");game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--ai-factory-exit-regression")>=0) {
        int failures=game.aiFactoryExitRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--ai-economy-regression")>=0) {
        int failures=game.aiEconomyRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--ai-construction-regression")>=0) {
        const int i=findFlag("--ai-construction-regression");
        int failures=game.aiConstructionRegression(i+1<argc?argv[i+1]:"review_out/ai-construction");game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--ai-air-defense-regression")>=0) {
        int failures=game.aiAirDefenseRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--ai-attack-regression")>=0) {
        int failures=game.aiAttackRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--blocker-regression")>=0) {
        int failures=game.blockerRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--idle-yield-regression")>=0) {
        int failures=game.idleYieldRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--chopper-ground-regression")>=0) {
        int failures=game.chopperGroundRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--immobilized-regression")>=0) {
        int failures=game.immobilizedRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--deployment-feedback-regression")>=0) {
        int failures=game.deploymentFeedbackRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--rally-regression")>=0) {
        int failures=game.rallyRegression();game.shutdown();return failures==0?0:1;
    }
    if(int fi=findFlag("--rally-visual-regression");fi>=0) {
        int failures=game.rallyVisualRegression(fi+1<argc?argv[fi+1]:"review_out/rally-visual");game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--refinery-recovery-regression")>=0) {
        int failures=game.refineryRecoveryRegression();game.shutdown();return failures==0?0:1;
    }
    if(int fi=findFlag("--feedback-visual-regression");fi>=0) {
        int failures=game.feedbackVisualRegression(fi+1<argc?argv[fi+1]:"review_out/feedback-visual");game.shutdown();return failures==0?0:1;
    }
    if(int fi=findFlag("--magnetic-landing-regression");fi>=0) {
        int failures=game.magneticLandingRegression(fi+1<argc?argv[fi+1]:"review_out/magnetic-landing");game.shutdown();return failures==0?0:1;
    }
    if(int fi=findFlag("--magnetron-regression");fi>=0) {
        int failures=game.magnetronRegression(fi+1<argc?argv[fi+1]:"review_out/magnetron");game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--splash-regression")>=0) {
        int failures=game.splashRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--burst-regression")>=0) {
        int failures=game.burstRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--apocalypse-regression")>=0) {
        int failures=game.apocalypseRegression();game.shutdown();return failures==0?0:1;
    }
    if(int fi=findFlag("--combat-pressure-regression");fi>=0) {
        int failures=game.combatPressureRegression(flagInt(fi,1,-1),fi+2<argc?argv[fi+2]:"review_out/combat-pressure");game.shutdown();return failures==0?0:1;
    }
    if(int fi=findFlag("--junction-regression");fi>=0) {
        int failures=game.junctionRegression(flagInt(fi,1,-1));game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--traffic-regression")>=0) {
        const int cf=findFlag("--traffic-case");
        int failures=game.trafficRegression(cf>=0 && cf+1<argc?atoi(argv[cf+1]):-1);
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--service-regression")>=0) {
        int failures=game.serviceRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--instant-combat-regression")>=0) {
        int failures=game.instantCombatRegression();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--release-regression")>=0) {
        int failures=game.releaseRegression();game.shutdown();return failures==0?0:1;
    }
    if (findFlag("--app-state-test") >= 0) {
        int failures=game.appPersistenceTest();game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--campaign-save-regression")>=0) {
        int failures=game.campaignSaveRegression();game.shutdown();return failures?1:0;
    }
    if(findFlag("--unit-voice-regression")>=0) {
        int failures=game.unitVoiceRegression();game.shutdown();return failures?1:0;
    }
    if(findFlag("--native-audio-regression")>=0) {
        int failures=game.nativeAudioRegression();game.shutdown();return failures?1:0;
    }
    if(findFlag("--net-protocol-regression")>=0) {
        int result=game.netProtocolRegression();game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--net-production-regression");at>=0) {
        int result=game.netProductionRegression(flagStr(at,1,"review_out/net-production"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--modal-input-regression");at>=0) {
        int result=game.modalInputRegression(flagStr(at,1,"review_out/modal-input"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--classic-input-regression");at>=0) {
        int result=game.classicInputRegression(flagStr(at,1,"review_out/classic-input"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--original-skirmish-regression");at>=0) {
        int result=game.originalSkirmishRegression(flagStr(at,1,"review_out/original-skirmish"),flagStr(at,2,nullptr));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--original-skirmish-natural");at>=0) {
        int result=game.originalSkirmishNaturalRegression(flagStr(at,1,"review_out/original-natural"),flagInt(at,2,0));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--ai-scouting-regression");at>=0) {
        int result=game.aiScoutingRegression(flagStr(at,1,"review_out/ai-gap-audit"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--gap-height-regression");at>=0) {
        int result=game.gapHeightRegression(flagStr(at,1,"review_out/gap-height-audit"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--gap-slope-regression");at>=0) {
        int result=game.gapSlopeRegression(flagStr(at,1,"review_out/gap-slope-audit"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--source-transport-regression");at>=0) {
        int result=game.sourceTransportRegression(flagStr(at,1,"review_out/source-transport-audit"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--cargo-ownership-regression");at>=0) {
        int result=game.cargoOwnershipRegression(flagStr(at,1,"review_out/cargo-ownership-audit"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--ground-unload-regression");at>=0) {
        int result=game.groundUnloadRegression(flagStr(at,1,"review_out/ground-unload-audit"));game.shutdown();return result?1:0;
    }
    if(findFlag("--entity-identity-regression")>=0) {
        int result=game.entityIdentityRegression();game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--transport-exit-regression");at>=0) {
        int result=game.transportExitRegression(flagStr(at,1,"review_out/transport-exit-audit"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--allied-boarding-regression");at>=0) {
        int result=game.alliedBoardingRegression(flagStr(at,1,"review_out/allied-boarding-audit"));game.shutdown();return result?1:0;
    }
    if(int at=findFlag("--transport-boarding-regression");at>=0) {
        int result=game.transportBoardingRegression(flagStr(at,1,"review_out/transport-boarding-audit"));game.shutdown();return result?1:0;
    }
    if(findFlag("--aircraft-spawner-regression")>=0) {
        const int at=findFlag("--aircraft-spawner-regression");
        int result=game.aircraftSpawnerRegression(at+1<argc?argv[at+1]:"review_out/aircraft-spawners");game.shutdown();return result==77?77:result?1:0;
    }
    if(findFlag("--asw-spawner-regression")>=0) {
        const int at=findFlag("--asw-spawner-regression");
        int result=game.aswSpawnerRegression(at+1<argc?argv[at+1]:"review_out/asw-spawners");game.shutdown();return result==77?77:result?1:0;
    }
    if(findFlag("--rules-spawner-regression")>=0) {
        const int at=findFlag("--rules-spawner-regression");
        int result=game.rulesSpawnerRegression(at+1<argc?argv[at+1]:"review_out/rules-spawners");game.shutdown();return result==77?77:result?1:0;
    }
    if(int at=findFlag("--rules-consistency-regression");at>=0) {
        int result=game.rulesConsistencyRegression(at+1<argc?argv[at+1]:"review_out/rules-consistency");game.shutdown();return result?1:0;
    }
    if(findFlag("--rules-profile-regression")>=0) {
        const int at=findFlag("--rules-profile-regression");
        int result=game.rulesProfileRegression(at+1<argc?argv[at+1]:"review_out/rules-profile");game.shutdown();return result==77?77:result?1:0;
    }
    if(int at=findFlag("--source-match-regression");at>=0) {
        int failures=game.sourceMatchRegression(at+1<argc?argv[at+1]:"review_out/source-match",
            (std::max)(1,flagInt(at,2,36000)),flagInt(at,3,-1),flagInt(at,4,-1));
        game.shutdown();return failures==0?0:1;
    }
    if(int at=findFlag("--source-mode-regression");at>=0) {
        int failures=game.sourceModeRegression(at+1<argc?argv[at+1]:"review_out/source-modes");
        game.shutdown();return failures==0?0:1;
    }
    if(int at=findFlag("--ground-travel-regression");at>=0) {
        const int failures=game.groundTravelRegression(at+1<argc?argv[at+1]:"review_out/ground-travel");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--crushing-regression");at>=0) {
        const int failures=game.crushingRegression(at+1<argc?argv[at+1]:"review_out/crushing");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--ground-control-regression");at>=0) {
        const int failures=game.groundControlRegression(at+1<argc?argv[at+1]:"review_out/ground-control");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--source-economy-regression");at>=0) {
        int failures=game.sourceEconomyRegression(at+1<argc?argv[at+1]:"review_out/source-economy");
        game.shutdown();return failures==0?0:1;
    }
    if(int at=findFlag("--building-repair-regression");at>=0) {
        const int failures=game.buildingRepairRegression(at+1<argc?argv[at+1]:"review_out/building-repair");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--infantry-travel-regression");at>=0) {
        const int failures=game.infantryTravelRegression(at+1<argc?argv[at+1]:"review_out/infantry-travel");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--player-scatter-regression");at>=0) {
        const int failures=game.playerScatterRegression(at+1<argc?argv[at+1]:"review_out/player-scatter");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--building-survivors-regression");at>=0) {
        const int failures=game.buildingSurvivorsRegression(at+1<argc?argv[at+1]:"review_out/building-survivors");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--building-sale-regression");at>=0) {
        const int failures=game.buildingSaleRegression(at+1<argc?argv[at+1]:"review_out/building-sale");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--drive-blockage-regression");at>=0) {
        const int failures=game.driveBlockageRegression(at+1<argc?argv[at+1]:"review_out/drive-blockage");
        game.shutdown();return failures==0?0:1;
    }
    if(int at=findFlag("--support-delivery-regression");at>=0) {
        int failures=game.supportDeliveryRegression(at+1<argc?argv[at+1]:"review_out/support-delivery");
        game.shutdown();return failures?1:0;
    }
    if(int at=findFlag("--source-support-regression");at>=0) {
        int failures=game.sourceSupportRegression(at+1<argc?argv[at+1]:"review_out/source-support");
        game.shutdown();return failures==0?0:1;
    }
    if(int at=findFlag("--source-crate-regression");at>=0) {
        int failures=game.sourceCrateRegression(at+1<argc?argv[at+1]:"review_out/source-crates");
        game.shutdown();return failures==0?0:1;
    }
    if(int at=findFlag("--native-gatling-regression");at>=0) {
        int failures=game.nativeGatlingRegression(at+1<argc?argv[at+1]:"review_out/native-gatling");
        game.shutdown();return failures==0?0:1;
    }
    if(findFlag("--rules-selection-regression")>=0) {
        const int at=findFlag("--rules-selection-regression");int result=game.rulesSelectionRegression(at+1<argc?argv[at+1]:"review_out/rules-selection");game.shutdown();return result==77?77:result?1:0;
    }
    if(findFlag("--group-order-regression")>=0) {
        int result=game.groupOrderRegression();game.shutdown();return result?1:0;
    }
    if (findFlag("--state-test") >= 0) {
        int failures = game.statePersistenceTest();
        game.shutdown();
        return failures == 0 ? 0 : 1;
    }
    // P8 联机双进程自测：先起 --net-host，再起 --net-client；两端日志比对校验和
    const auto netRules=[&](){const int at=findFlag("--ruleset");if(at<0)return RulesChoice::Fusion;
        if(at+1>=argc)return RulesChoice::COUNT;const std::string value=argv[at+1];
        return value=="fusion"?RulesChoice::Fusion:value=="ra2"?RulesChoice::RA2:value=="yr"?RulesChoice::YR:RulesChoice::COUNT;};
    const auto netCountry=[&](){const int at=findFlag("--net-country");return at<0?0:flagInt(at,1,-1);};
    if (int fi = findFlag("--net-host"); fi >= 0) {
        int frames = flagInt(fi, 1, 900);
        const auto choice=netRules();const int country=netCountry();int rc=choice==RulesChoice::COUNT || country<0 || country>(int)Country::COUNT?2:game.netSelfTestDriver(0,frames,choice,country);
        game.shutdown();
        return rc;
    }
    if (int fi = findFlag("--net-client"); fi >= 0) {
        int frames = flagInt(fi, 1, 900);
        const auto choice=netRules();const int country=netCountry();int rc=choice==RulesChoice::COUNT || country<0 || country>(int)Country::COUNT?2:game.netSelfTestDriver(1,frames,choice,country);
        game.shutdown();
        return rc;
    }
#endif
    game.run();
    game.shutdown();
    return 0;
}
