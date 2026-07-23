// Game 的主菜单与遭遇战设置界面（RA2 风格复刻）
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

// RA2 式金属按钮：渐变底 + 顶部高光 + 金框，悬停泛红
static bool ra2Button(Font font, Vector2 m, bool pressed, Rectangle r, const char* text, int size = 20,
                      bool enabled = true, bool danger = false) {
    bool hover = CheckCollisionPointRec(m, r) && enabled;
    Color top = enabled ? (hover ? Color{96, 44, 40, 255} : Color{58, 56, 60, 255}) : Color{34, 34, 38, 255};
    Color bot = enabled ? (hover ? Color{64, 24, 22, 255} : Color{34, 32, 36, 255}) : Color{22, 22, 26, 255};
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, top, bot);
    DrawLine((int)r.x, (int)r.y, (int)(r.x + r.width), (int)r.y, Color{140, 138, 146, 200});
    Color frame = danger ? Color{200, 60, 40, 255} : (hover ? Color{255, 200, 90, 255} : Color{150, 130, 80, 255});
    if (!enabled) frame = Color{70, 70, 76, 255};
    DrawRectangleLinesEx(r, 2, frame);
    if (text && text[0]) {
        int tw = textW(font, text, size);
        drawTextM(font, text, (int)(r.x + r.width / 2 - tw / 2) + 1, (int)(r.y + r.height / 2 - size / 2) + 1, size,
                  Color{0, 0, 0, 255});
        drawTextM(font, text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - size / 2), size,
                  enabled ? (hover ? Color{255, 226, 150, 255} : Color{230, 216, 170, 255})
                          : Color{110, 110, 110, 255});
    }
    bool clicked = hover && pressed;
    if (clicked) g_sfx.play(Sfx::Click, 0.6f);
    return clicked;
}

// 菜单通用底板：深色 + 红色顶栏 + 网格暗纹
static void drawMenuBackdrop(Font font, const char* title) {
    ClearBackground(Color{12, 13, 17, 255});
    for (int i = 0; i < 30; i++)
        DrawLine(0, i * 30, SCREEN_W, i * 30 - 220, Color{18, 19, 25, 255});
    DrawRectangle(0, 0, SCREEN_W, 64, Color{26, 10, 10, 255});
    DrawRectangle(0, 62, SCREEN_W, 2, Color{168, 40, 32, 255});
    drawTextM(font, title, 42, 18, 30, Color{232, 206, 140, 255});
    drawTextM(font, "OPENRA2", SCREEN_W - 40 - textW(font, "OPENRA2", 20), 22, 20, Color{120, 60, 54, 255});
}

// ===================== 主菜单 =====================
void Game::drawMainMenu() {
    ClearBackground(Color{10, 11, 15, 255});
    // 背景斜纹 + 底部红色光带
    for (int i = 0; i < 34; i++)
        DrawLine(0, i * 28, SCREEN_W, i * 28 - 300, Color{17, 18, 24, 255});
    DrawRectangleGradientV(0, SCREEN_H - 220, SCREEN_W, 220, Color{10, 11, 15, 0}, Color{70, 16, 12, 120});

    // 标题（RA2 式：黑色投影 + 红色主体 + 金色副标）
    const char* title = "共和国之辉";
    int cx = SCREEN_W / 2;
    drawTextM(font, title, cx - textW(font, title, 84) / 2 + 5, 145, 84, Color{0, 0, 0, 255});
    drawTextM(font, title, cx - textW(font, title, 84) / 2, 140, 84, Color{216, 48, 40, 255});
    DrawRectangle(cx - 240, 258, 480, 3, Color{168, 40, 32, 255});
    const char* sub = "COMMAND & CONQUER · OPENRA2 像素复刻";
    drawTextM(font, sub, cx - textW(font, sub, 18) / 2, 278, 18, Color{196, 170, 110, 255});

    // 左侧竖排大按钮（RA2 主菜单布局）
    Vector2 m = mousePos();
    bool pr = mPressed(MOUSE_LEFT_BUTTON);
    int bx = 120, bw = 330, bh = 58, by = 360, gap = 18;
    if (ra2Button(font, m, pr, {(float)bx, (float)by, (float)bw, (float)bh}, "遭遇战", 24)) phase = Phase::Setup;
    if (ra2Button(font, m, pr, {(float)bx, (float)(by + (bh + gap)), (float)bw, (float)bh}, "战役模式", 24))
        phase = Phase::MissionSelect;
    if (ra2Button(font, m, pr, {(float)bx, (float)(by + 2 * (bh + gap)), (float)bw, (float)bh}, "退出游戏", 24,
                  true, true)) {
        CloseWindow();
        exit(0);
    }

    const char* tip = "程序生成像素素材 · 盟军 / 苏联 / 中国";
    drawTextM(font, tip, cx - textW(font, tip, 14) / 2, SCREEN_H - 46, 14, Color{110, 112, 120, 255});
}

void Game::debugMenuShot(const char* file, bool setup) {
    phase = setup ? Phase::Setup : Phase::MainMenu;
    if (setup) refreshMapPreview();
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

// ===================== 战役选择 =====================
void Game::drawMissionSelect() {
    drawMenuBackdrop(font, "战役模式");
    int cx = SCREEN_W / 2;
    Vector2 m = mousePos();

    // 任务卡片：名称 + 简报 + 目标
    const auto& tbl = missionTable();
    int cardW = 360, cardH = 200, gap = 30;
    int totalW = (int)tbl.size() * cardW + ((int)tbl.size() - 1) * gap;
    int x0 = cx - totalW / 2, y0 = 200;
    for (int i = 0; i < (int)tbl.size(); i++) {
        const MissionDef& md = tbl[i];
        Rectangle r{(float)(x0 + i * (cardW + gap)), (float)y0, (float)cardW, (float)cardH};
        bool hover = CheckCollisionPointRec(m, r);
        DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                               hover ? Color{52, 42, 34, 255} : Color{30, 30, 36, 255},
                               hover ? Color{34, 24, 20, 255} : Color{20, 20, 26, 255});
        DrawRectangleLinesEx(r, 2, hover ? Color{255, 200, 90, 255} : Color{120, 100, 60, 255});
        int rx = (int)r.x, ry = (int)r.y;
        drawTextM(font, TextFormat("任务 %d", i + 1), rx + 16, ry + 12, 14, Color{150, 142, 130, 255});
        drawTextM(font, md.name, rx + 16, ry + 34, 26, Color{255, 210, 100, 255});
        DrawRectangle(rx + 16, ry + 72, cardW - 32, 1, Color{90, 70, 50, 255});
        drawTextM(font, md.brief, rx + 16, ry + 84, 15, Color{196, 194, 200, 255});
        drawTextM(font, md.objective == 1 ? "目标：坚守十分钟" : "目标：歼灭所有敌军",
                  rx + 16, ry + 116, 15, Color{130, 200, 140, 255});
        if (hover) {
            drawTextM(font, "点击进入", rx + 16, ry + cardH - 34, 16, Color{255, 226, 150, 255});
            if (mPressed(MOUSE_LEFT_BUTTON)) {
                g_sfx.play(Sfx::Click, 0.6f);
                newCampaignGame(i);
                return;
            }
        }
    }

    if (ra2Button(font, m, mPressed(MOUSE_LEFT_BUTTON), {(float)cx - 110, (float)y0 + cardH + 70, 220, 48}, "返回", 20))
        phase = Phase::MainMenu;
}

// ===================== 地图预览 =====================
void Game::refreshMapPreview() {
    std::vector<Vec2i> spawns;
    previewMap.generate(cfgMapSize, cfgMapSize, previewSeed, cfgAI + 1, spawns, cfgMapType);
    const int P = 340;
    Image img = GenImageColor(P, P, BLACK);
    for (int y = 0; y < P; y++)
        for (int x = 0; x < P; x++) {
            const Cell& c = previewMap.at(x * cfgMapSize / P, y * cfgMapSize / P);
            Color col;
            switch (c.terrain) {
                case Terrain::Water: col = {18, 42, 96, 255}; break;
                case Terrain::Rough: col = {118, 96, 58, 255}; break;
                case Terrain::Ore:   col = {196, 164, 52, 255}; break;
                case Terrain::Gems:  col = {64, 190, 210, 255}; break;
                default:             col = {52, 96, 44, 255}; break;
            }
            if (c.overlay == Overlay::Tree1 || c.overlay == Overlay::Tree2 || c.overlay == Overlay::Tree3)
                col = {26, 60, 24, 255};
            if (c.overlay == Overlay::Rock1 || c.overlay == Overlay::Rock2)
                col = {92, 92, 98, 255};
            ImageDrawPixel(&img, x, y, col);
        }
    // 出生点标记：0=本地玩家色，其余白色
    for (size_t i = 0; i < spawns.size(); i++) {
        int px = spawns[i].x * P / cfgMapSize, py = spawns[i].y * P / cfgMapSize;
        Color sc = i == 0 ? HOUSE_COLORS[cfgColor] : Color{230, 230, 230, 255};
        ImageDrawRectangle(&img, px - 3, py - 3, 7, 7, Color{0, 0, 0, 255});
        ImageDrawRectangle(&img, px - 2, py - 2, 5, 5, sc);
    }
    if (previewTex.id > 0) UnloadTexture(previewTex);
    previewTex = LoadTextureFromImage(img);
    UnloadImage(img);
    previewDirty = false;
}

// ===================== 遭遇战设置（RA2 布局） =====================
void Game::drawSetup() {
    drawMenuBackdrop(font, "遭遇战");
    Vector2 m = mousePos();
    bool pr = mPressed(MOUSE_LEFT_BUTTON);
    if (previewDirty) refreshMapPreview();

    int maxAI = cfgMapSize <= 64 ? 3 : (cfgMapSize <= 96 ? 5 : 7);
    if (cfgAI > maxAI) cfgAI = maxAI;

    // ---------- 左面板：地图预览与地图参数 ----------
    int px = 48, py = 92, pw = 380;
    DrawRectangle(px, py, pw, 494, Color{20, 20, 26, 255});
    DrawRectangleLinesEx({(float)px, (float)py, (float)pw, 494}, 1, Color{80, 74, 66, 255});
    // 预览图 340x340
    int ix = px + 20, iy = py + 18;
    if (previewTex.id > 0) DrawTexture(previewTex, ix, iy, WHITE);
    DrawRectangleLinesEx({(float)ix, (float)iy, 340, 340}, 2, Color{150, 130, 80, 255});
    // 换一张
    if (ra2Button(font, m, pr, {(float)ix, (float)(iy + 342), 166, 36}, "更换一张", 18)) {
        previewSeed = (uint64_t)time(nullptr) * 2654435761u + 97;
        previewDirty = true;
    }
    // 地图尺寸 / 类型（点击循环切换）
    auto optRow = [&](int y, const char* label, const char* value) {
        drawTextM(font, label, ix, y + 8, 18, Color{190, 188, 196, 255});
        Rectangle r{(float)ix + 150, (float)y, 190, 36};
        bool hover = CheckCollisionPointRec(m, r);
        DrawRectangleRec(r, hover ? Color{56, 50, 44, 255} : Color{34, 32, 38, 255});
        DrawRectangleLinesEx(r, 1, hover ? Color{255, 200, 90, 255} : Color{120, 104, 66, 255});
        drawTextM(font, value, (int)r.x + 95 - textW(font, value, 17) / 2, y + 9, 17, Color{255, 220, 120, 255});
        return hover && pr;
    };
    static const int sizes[] = {64, 96, 128};
    static const char* sizeNames[] = {"小 64x64", "中 96x96", "大 128x128"};
    int si = 0;
    while (si < 3 && sizes[si] != cfgMapSize) si++;
    if (optRow(iy + 386, "地图尺寸", sizeNames[si])) {
        cfgMapSize = sizes[(si + 1) % 3];
        if (cfgAI > maxAI) cfgAI = maxAI;
        previewDirty = true;
    }
    static const char* typeNames[] = {"大陆", "岛屿", "湖泊"};
    if (optRow(iy + 428, "地图类型", typeNames[cfgMapType])) {
        cfgMapType = (cfgMapType + 1) % 3;
        previewDirty = true;
    }

    // ---------- 右面板：玩家槽位 ----------
    int sx = 452, sy = 92, sw = SCREEN_W - sx - 48;
    DrawRectangle(sx, sy, sw, 494, Color{20, 20, 26, 255});
    DrawRectangleLinesEx({(float)sx, (float)sy, (float)sw, 494}, 1, Color{80, 74, 66, 255});
    // 表头
    int rowH = 48;
    int nameX = sx + 24, colorX = sx + 330, factX = sx + 520, delX = sx + sw - 96;
    drawTextM(font, "玩家", nameX, sy + 12, 17, Color{150, 142, 130, 255});
    drawTextM(font, "颜色", colorX, sy + 12, 17, Color{150, 142, 130, 255});
    drawTextM(font, "阵营", factX, sy + 12, 17, Color{150, 142, 130, 255});
    int slotY = sy + 40;
    // 槽位行绘制：返回是否发生变更（需要刷新预览的出生点颜色）
    auto slotRow = [&](int idx, const char* name, int& color, int& faction, bool isLocal) {
        int y = slotY + idx * rowH;
        bool even = idx % 2 == 0;
        DrawRectangle(sx + 8, y, sw - 16, rowH - 4, even ? Color{30, 30, 38, 255} : Color{24, 24, 30, 255});
        // 名字（本地玩家金色，AI 灰色）
        drawTextM(font, name, nameX, y + 12, 19, isLocal ? Color{255, 220, 120, 255} : Color{200, 200, 210, 255});
        // 颜色块按钮
        Rectangle cr{(float)colorX, (float)y + 6, 150, rowH - 16};
        bool chover = CheckCollisionPointRec(m, cr);
        DrawRectangleRec(cr, HOUSE_COLORS[color]);
        DrawRectangleLinesEx(cr, 2, chover ? WHITE : Color{60, 58, 64, 255});
        if (chover && pr) { color = (color + 1) % MAX_PLAYERS; g_sfx.play(Sfx::Click, 0.5f); }
        // 阵营按钮
        static const char* fnames[] = {"盟军", "苏联", "中国", "随机"};
        Rectangle fr{(float)factX, (float)y + 6, 170, rowH - 16};
        bool fhover = CheckCollisionPointRec(m, fr);
        DrawRectangleRec(fr, fhover ? Color{56, 50, 44, 255} : Color{38, 36, 42, 255});
        DrawRectangleLinesEx(fr, 1, fhover ? Color{255, 200, 90, 255} : Color{96, 88, 70, 255});
        const char* fn = fnames[faction >= 3 ? 3 : faction];
        drawTextM(font, fn, (int)fr.x + 85 - textW(font, fn, 17) / 2, y + 13, 17, Color{230, 216, 170, 255});
        if (fhover && pr) { faction = (faction + 1) % 4; g_sfx.play(Sfx::Click, 0.5f); }
        // AI 移除按钮
        if (!isLocal) {
            Rectangle dr{(float)delX, (float)y + 8, 72, rowH - 20};
            if (ra2Button(font, m, pr, dr, "移除", 15, true, true)) {
                for (int i = idx - 1; i < cfgAI - 1; i++) { aiColor[i] = aiColor[i + 1]; aiFaction[i] = aiFaction[i + 1]; }
                cfgAI--;
                previewDirty = true;
            }
        }
    };
    slotRow(0, "指挥官（你）", cfgColor, cfgFaction, true);
    for (int i = 0; i < cfgAI; i++)
        slotRow(i + 1, TextFormat("电脑 %d", i + 1), aiColor[i], aiFaction[i], false);
    // 添加电脑
    if (cfgAI < maxAI) {
        int y = slotY + (cfgAI + 1) * rowH + 6;
        if (ra2Button(font, m, pr, {(float)nameX, (float)y, 200, 40}, "+ 添加电脑", 18)) {
            aiColor[cfgAI] = (cfgAI + 1) % MAX_PLAYERS;
            aiFaction[cfgAI] = 3;
            cfgAI++;
            previewDirty = true;
        }
    }

    // ---------- 底部选项条 ----------
    int oy = 600;
    DrawRectangle(48, oy, SCREEN_W - 96, 64, Color{20, 20, 26, 255});
    DrawRectangleLinesEx({48, (float)oy, (float)SCREEN_W - 96, 64}, 1, Color{80, 74, 66, 255});
    auto optBtn = [&](int x, const char* label, const char* value, int w) {
        drawTextM(font, label, x, oy + 20, 18, Color{190, 188, 196, 255});
        int lx = x + textW(font, label, 18) + 16;
        Rectangle r{(float)lx, (float)oy + 10, (float)w, 44};
        bool hover = CheckCollisionPointRec(m, r);
        DrawRectangleRec(r, hover ? Color{56, 50, 44, 255} : Color{34, 32, 38, 255});
        DrawRectangleLinesEx(r, 1, hover ? Color{255, 200, 90, 255} : Color{120, 104, 66, 255});
        drawTextM(font, value, lx + w / 2 - textW(font, value, 18) / 2, oy + 22, 18, Color{255, 220, 120, 255});
        return hover && pr;
    };
    static const int monies[] = {5000, 10000, 20000, 50000};
    if (optBtn(80, "初始资金", TextFormat("%d", cfgMoney), 130)) {
        int i = 0;
        while (i < 4 && monies[i] != cfgMoney) i++;
        cfgMoney = monies[(i + 1) % 4];
        g_sfx.play(Sfx::Click, 0.5f);
    }
    static const char* speedNames[] = {"慢", "普通", "快"};
    if (optBtn(420, "游戏速度", speedNames[gameSpeed], 110)) {
        gameSpeed = (gameSpeed + 1) % 3;
        g_sfx.play(Sfx::Click, 0.5f);
    }

    // ---------- 底部：开始游戏 / 返回 ----------
    int by = 700;
    if (ra2Button(font, m, pr, {(float)(SCREEN_W / 2 - 330), (float)by, 320, 62}, "开始游戏", 28))
        newGame(previewSeed); // 用预览的同一张图开局：所见即所玩
    if (ra2Button(font, m, pr, {(float)(SCREEN_W / 2 + 30), (float)by, 200, 62}, "返回", 24))
        phase = Phase::MainMenu;
}
