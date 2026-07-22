// Game 的主菜单与遭遇战设置界面
#include "game/game.h"
#include "game/campaign.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <ctime>

static void drawTextM(Font f, const char* s, int x, int y, int size, Color c) {
    DrawTextEx(f, s, {(float)x, (float)y}, (float)size, 1, c);
}

static int textW(Font f, const char* s, int size) {
    return (int)MeasureTextEx(f, s, (float)size, 1).x;
}

void Game::drawMainMenu() {
    ClearBackground(Color{14, 16, 20, 255});
    // 背景装饰线
    for (int i = 0; i < 24; i++)
        DrawLine(0, i * 36, SCREEN_W, i * 36 - 160, Color{22, 24, 30, 255});

    int cx = SCREEN_W / 2;
    const char* title = "共和国之辉";
    drawTextM(font, title, cx - textW(font, title, 68) / 2 + 4, 154, 68, Color{0, 0, 0, 255});
    drawTextM(font, title, cx - textW(font, title, 68) / 2, 150, 68, Color{216, 48, 40, 255});
    const char* sub = "OPENRA2 像素复刻";
    drawTextM(font, sub, cx - textW(font, sub, 20) / 2, 240, 20, Color{150, 152, 160, 255});
    // 标题下红线
    DrawRectangle(cx - 200, 282, 400, 3, Color{160, 36, 30, 255});

    int bw = 260, bx = cx - bw / 2;
    if (uiButton({(float)bx, 360, (float)bw, 44}, "遭遇战", true)) phase = Phase::Setup;
    if (uiButton({(float)bx, 420, (float)bw, 44}, "战役模式", true)) phase = Phase::MissionSelect;
    if (uiButton({(float)bx, 480, (float)bw, 44}, "退出游戏", true)) { CloseWindow(); exit(0); }

    const char* tip = "程序生成像素素材 · 盟军 / 苏联 / 中国";
    drawTextM(font, tip, cx - textW(font, tip, 14) / 2, SCREEN_H - 60, 14, Color{110, 112, 120, 255});
}

void Game::debugMenuShot(const char* file, bool setup) {
    phase = setup ? Phase::Setup : Phase::MainMenu;
    BeginTextureMode(canvas);
    ClearBackground(BLACK);
    if (setup) drawSetup();
    else drawMainMenu();
    EndTextureMode();
    Image img = LoadImageFromTexture(canvas.texture);
    ImageFlipVertical(&img);
    ExportImage(img, file);
    UnloadImage(img);
}

void Game::drawMissionSelect() {
    ClearBackground(Color{14, 16, 20, 255});
    int cx = SCREEN_W / 2;
    const char* title = "战役模式";
    drawTextM(font, title, cx - textW(font, title, 34) / 2, 90, 34, WHITE);
    DrawRectangle(cx - 160, 142, 320, 2, Color{100, 106, 116, 255});

    // 任务卡片：名称 + 简报 + 目标
    const auto& tbl = missionTable();
    int cardW = 340, cardH = 118, gap = 26;
    int totalW = (int)tbl.size() * cardW + ((int)tbl.size() - 1) * gap;
    int x0 = cx - totalW / 2, y0 = 210;
    for (int i = 0; i < (int)tbl.size(); i++) {
        const MissionDef& md = tbl[i];
        Rectangle r{(float)(x0 + i * (cardW + gap)), (float)y0, (float)cardW, (float)cardH};
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRec(r, hover ? Color{52, 56, 66, 255} : Color{32, 34, 40, 255});
        DrawRectangleLinesEx(r, 1, hover ? Color{180, 150, 80, 255} : Color{80, 84, 92, 255});
        int rx = (int)r.x, ry = (int)r.y;
        drawTextM(font, TextFormat("任务 %d", i + 1), rx + 14, ry + 10, 13, Color{140, 142, 150, 255});
        drawTextM(font, md.name, rx + 14, ry + 28, 24, Color{255, 210, 100, 255});
        drawTextM(font, md.brief, rx + 14, ry + 62, 14, Color{190, 192, 200, 255});
        drawTextM(font, md.objective == 1 ? "目标：坚守十分钟" : "目标：歼灭所有敌军",
                  rx + 14, ry + 92, 14, Color{130, 200, 140, 255});
        if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            g_sfx.play(Sfx::Click, 0.6f);
            newCampaignGame(i);
            return;
        }
    }

    if (uiButton({(float)cx - 100, (float)y0 + cardH + 60, 200, 42}, "返回", true))
        phase = Phase::MainMenu;

    const char* tip = "战役地图为固定布局 · 玩家阵营：中国";
    drawTextM(font, tip, cx - textW(font, tip, 14) / 2, SCREEN_H - 60, 14, Color{110, 112, 120, 255});
}

void Game::drawSetup() {
    ClearBackground(Color{14, 16, 20, 255});
    int cx = SCREEN_W / 2;
    const char* title = "遭遇战设置";
    drawTextM(font, title, cx - textW(font, title, 34) / 2, 100, 34, WHITE);
    DrawRectangle(cx - 160, 152, 320, 2, Color{100, 106, 116, 255});

    // 地图尺寸决定 AI 上限
    int maxAI = cfgMapSize <= 64 ? 3 : (cfgMapSize <= 96 ? 5 : 7);
    if (cfgAI > maxAI) cfgAI = maxAI;

    int labelX = cx - 210, valX = cx + 10, rowY = 200, rowH = 56;
    auto row = [&](int idx, const char* label, const char* value) {
        int y = rowY + idx * rowH;
        drawTextM(font, label, labelX, y + 10, 18, Color{190, 192, 200, 255});
        Rectangle r{(float)valX, (float)y, 220, 40};
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRec(r, hover ? Color{56, 60, 68, 255} : Color{36, 38, 44, 255});
        DrawRectangleLinesEx(r, 1, Color{90, 94, 102, 255});
        drawTextM(font, value, valX + 110 - textW(font, value, 17) / 2, y + 11, 17, Color{255, 220, 120, 255});
        return hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    };

    // 玩家阵营
    if (row(0, "玩家阵营", factionName((Faction)cfgFaction)))
        cfgFaction = (cfgFaction + 1) % 3;

    // 选择颜色（色块组）
    {
        int y = rowY + 1 * rowH;
        drawTextM(font, "选择颜色", labelX, y + 10, 18, Color{190, 192, 200, 255});
        for (int i = 0; i < MAX_PLAYERS; i++) {
            Rectangle sw{(float)valX + i * 27, (float)y + 6, 23, 28};
            DrawRectangleRec(sw, HOUSE_COLORS[i]);
            DrawRectangleLinesEx(sw, i == cfgColor ? 2 : 1, i == cfgColor ? WHITE : Color{60, 62, 68, 255});
            if (CheckCollisionPointRec(GetMousePosition(), sw) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                cfgColor = i;
        }
    }

    // 电脑数量
    if (row(2, "电脑数量", TextFormat("%d", cfgAI)))
        cfgAI = cfgAI % maxAI + 1;

    // 初始资金
    static const int monies[] = {5000, 10000, 20000, 50000};
    if (row(3, "初始资金", TextFormat("%d", cfgMoney))) {
        int i = 0;
        while (i < 4 && monies[i] != cfgMoney) i++;
        cfgMoney = monies[(i + 1) % 4];
    }

    // 地图尺寸
    static const int sizes[] = {64, 96, 128};
    static const char* sizeNames[] = {"小 64x64", "中 96x96", "大 128x128"};
    int si = 0;
    while (si < 3 && sizes[si] != cfgMapSize) si++;
    if (row(4, "地图尺寸", sizeNames[si]))
        cfgMapSize = sizes[(si + 1) % 3];

    // 地图类型
    static const char* typeNames[] = {"大陆", "岛屿", "湖泊"};
    if (row(5, "地图类型", typeNames[cfgMapType]))
        cfgMapType = (cfgMapType + 1) % 3;

    // 按钮
    int bw = 200, by = rowY + 6 * rowH + 30;
    if (uiButton({(float)cx - bw - 16, (float)by, (float)bw, 46}, "开始游戏", true)) {
        newGame((uint64_t)time(nullptr));
    }
    if (uiButton({(float)cx + 16, (float)by, (float)bw, 46}, "返回", true)) {
        phase = Phase::MainMenu;
    }

    const char* tip = "点击数值切换选项 · AI 阵营随机";
    drawTextM(font, tip, cx - textW(font, tip, 14) / 2, by + 70, 14, Color{110, 112, 120, 255});
}
