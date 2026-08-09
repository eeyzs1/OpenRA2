#include "game/game.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#  define NOMINMAX
#  define NOGDI              // 避免 GDI Rectangle() 与 raylib Rectangle 冲突
#  define WIN32_LEAN_AND_MEAN
#  define CloseWindow CloseWindowWin32
#  define ShowCursor ShowCursorWin32
#  include <windows.h>
#  undef CloseWindow
#  undef ShowCursor
#endif

int Game::liveVerify() {
#ifdef _WIN32
    sim.active = false;
    int fails = 0, step = 0;
    auto check = [&](bool ok, const char* name) {
        step++;
        TraceLog(LOG_INFO, "LIVE [%02d] %-36s %s", step, name, ok ? "PASS" : "FAIL");
        if (!ok) fails++;
    };
    auto shot = [&](const char* f) {
        std::filesystem::create_directories("live_review");
        shotFile = TextFormat("live_review/%s", f);
        render();
    };
    auto frame = [&](int n = 1) {
        for (int i = 0; i < n; i++) {
            if (phase == Phase::InGame && !showMenu) handleInput();
            if (phase == Phase::InGame && !paused && !gameOver) logic();
            render();
        }
    };
    auto canvasToScreen = [&](float cx, float cy, POINT* out) -> bool {
        HWND hwnd = (HWND)GetWindowHandle();
        if (!hwnd || !out) return false;
        Rectangle dst = letterboxDst();
        float lx = dst.x + cx * dst.width / (float)SCREEN_W;
        float ly = dst.y + cy * dst.height / (float)SCREEN_H;
        POINT pt{ (LONG)(lx + 0.5f), (LONG)(ly + 0.5f) };
        ClientToScreen(hwnd, &pt);
        *out = pt;
        return true;
    };
    auto osMouse = [&](float cx, float cy, bool down) {
        HWND hwnd = (HWND)GetWindowHandle();
        if (hwnd) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        POINT pt{};
        if (!canvasToScreen(cx, cy, &pt)) return;
        SetCursorPos(pt.x, pt.y);
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        SendInput(1, &in, sizeof(INPUT));
    };
    auto realClick = [&](float cx, float cy) {
        osMouse(cx, cy, true);
        frame(2);
        osMouse(cx, cy, false);
        frame(2);
    };
    auto realClickUi = [&](float ux, float uy) {
        Vector2 c = menuCanvasFromUi(ux, uy);
        realClick(c.x, c.y);
    };

    cfgWindowMode = 1;
    applyDisplay();
    frame(5);
    check(phase == Phase::MainMenu, "visible main menu");
    shot("live_01_mainmenu.png");

    realClick(1118, 376);
    frame(3);
    check(phase == Phase::Setup, "OS-click into Setup");
    shot("live_02_setup.png");

    realClickUi(556, 266);
    frame(8);
    check(phase == Phase::InGame, "OS-click StartGame");

    EID mcv = INVALID_EID;
    for (size_t i = 0; i < world.ents.size(); i++) {
        const auto& e = world.ents[i];
        if (e.alive && !e.isBuilding && e.player == localPlayer && e.utype == UnitType::MCV) {
            mcv = (int)i;
            break;
        }
    }
    check(mcv != INVALID_EID, "spawn MCV");
    if (mcv != INVALID_EID) {
        ::Rectangle ur = unitScreenRect(world.ents[mcv]);
        float cx = ur.x + ur.width * 0.5f, cy = ur.y + ur.height * 0.5f;
        TraceLog(LOG_INFO, "LIVE tip='%s' ur=(%.0f,%.0f,%.0fx%.0f)",
                 msg.c_str(), ur.x, ur.y, ur.width, ur.height);

        POINT pt{};
        canvasToScreen(cx, cy, &pt);
        SetCursorPos(pt.x, pt.y);
        frame(2);
        Vector2 back = mousePos();
        float err = distf(back.x, back.y, cx, cy);
        TraceLog(LOG_INFO, "LIVE roundtrip target=(%.1f,%.1f) got=(%.1f,%.1f) err=%.2f",
                 cx, cy, back.x, back.y, err);
        check(err < 8.f, "letterbox mouse roundtrip ok");

        sel.clear();
        selBuilding = INVALID_EID;
        realClick(cx, cy);
        frame(2);
        check(sel.size() == 1 && sel[0] == mcv, "OS-click select MCV");
        shot("live_03_selected.png");
        {
            // 框选全体：验证选中框贴合单位（画布像素，不依赖 OS）
            float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
            int nOwn = 0;
            for (size_t i = 0; i < world.ents.size(); i++) {
                const auto& e = world.ents[i];
                if (!e.alive || e.isBuilding || e.player != localPlayer) continue;
                ::Rectangle r = unitScreenRect(e);
                minX = std::min(minX, r.x); minY = std::min(minY, r.y);
                maxX = std::max(maxX, r.x + r.width); maxY = std::max(maxY, r.y + r.height);
                nOwn++;
            }
            if (nOwn > 0) {
                osMouse(minX - 20.f, minY - 20.f, true);
                frame(1);
                POINT pt{};
                canvasToScreen(maxX + 20.f, maxY + 20.f, &pt);
                SetCursorPos(pt.x, pt.y);
                frame(2);
                osMouse(maxX + 20.f, maxY + 20.f, false);
                frame(2);
                check((int)sel.size() >= std::min(nOwn, 2), "OS box-select owns");
                shot("live_03b_boxsel.png");
                TraceLog(LOG_INFO, "LIVE boxsel n=%d tip='%s'", (int)sel.size(), msg.c_str());
            }
        }

        float ox = cx + 160.f, oy = cy + 120.f;
        if (ox > (float)(SCREEN_W - sidebarW - 20)) ox = cx - 160.f;
        float mx0 = world.ents[mcv].x, my0 = world.ents[mcv].y;
        realClick(ox, oy);
        frame(5);
        bool moved = distf(world.ents[mcv].x, world.ents[mcv].y, mx0, my0) > 0.05f;
        check(!moved || world.hasBld(localPlayer, BldType::ConYard),
              "LMB empty does not Move MCV");
        shot("live_04_after_empty_click.png");
    }

    TraceLog(LOG_INFO, "LIVE VERIFY DONE: %d checks, %d failed", step, fails);
    return fails;
#else
    TraceLog(LOG_WARNING, "liveVerify: Windows only");
    return 0;
#endif
}
