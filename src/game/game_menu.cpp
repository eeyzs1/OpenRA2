// Game 的主菜单与遭遇战设置界面（RA2 风格复刻）
// 共享 UI 组件（drawTextM/textW/ra2Button/drawMenuBackdrop）供 game_settings.cpp 复用
#include "game/game.h"
#include "game/campaign.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <ctime>
#include <algorithm>

void drawTextM(Font f, const char* s, int x, int y, int size, Color c) {
    DrawTextEx(f, s, {(float)x, (float)y}, (float)size, 1, c);
}

int textW(Font f, const char* s, int size) {
    return (int)MeasureTextEx(f, s, (float)size, 1).x;
}

// RA2 式金属按钮：渐变底 + 顶部高光 + 金框，悬停加亮（冷钢灰+黄铜）
bool ra2Button(Font font, Vector2 m, bool pressed, Rectangle r, const char* text, int size,
               bool enabled, bool danger) {
    bool hover = CheckCollisionPointRec(m, r) && enabled;
    // RA2 冷钢灰 → 悬停加亮为暖金灰
    Color top = enabled ? (hover ? Color{84, 88, 98, 255} : Color{50, 54, 60, 255}) : Color{28, 30, 34, 255};
    Color bot = enabled ? (hover ? Color{52, 56, 62, 255} : Color{28, 30, 34, 255}) : Color{20, 22, 24, 255};
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, top, bot);
    DrawLine((int)r.x, (int)r.y, (int)(r.x + r.width), (int)r.y, Color{120, 128, 140, 200});
    Color frame = danger ? Color{200, 60, 40, 255} : (hover ? Color{240, 200, 90, 255} : Color{140, 116, 56, 255});
    if (!enabled) frame = Color{56, 58, 62, 255};
    DrawRectangleLinesEx(r, 2, frame);
    if (text && text[0]) {
        int tw = textW(font, text, size);
        drawTextM(font, text, (int)(r.x + r.width / 2 - tw / 2) + 1, (int)(r.y + r.height / 2 - size / 2) + 1, size,
                  Color{0, 0, 0, 255});
        drawTextM(font, text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - size / 2), size,
                  enabled ? (hover ? Color{255, 236, 160, 255} : Color{214, 218, 224, 255})
                          : Color{100, 102, 106, 255});
    }
    bool clicked = hover && pressed;
    if (clicked) g_sfx.play(Sfx::Click, 0.6f);
    return clicked;
}

// 菜单通用底板：深色 + 红色顶栏 + 网格暗纹（RA2 冷调）
void drawMenuBackdrop(Font font, const char* title) {
    ClearBackground(Color{10, 12, 16, 255});
    for (int i = 0; i < 30; i++)
        DrawLine(0, i * 30, SCREEN_W, i * 30 - 220, Color{18, 20, 26, 255});
    DrawRectangle(0, 0, SCREEN_W, 64, Color{20, 14, 12, 255});
    DrawRectangle(0, 62, SCREEN_W, 2, Color{176, 40, 32, 255});
    drawTextM(font, title, 42, 18, 30, Color{232, 210, 150, 255});
    drawTextM(font, "OPENRA2", SCREEN_W - 40 - textW(font, "OPENRA2", 20), 22, 20, Color{110, 64, 56, 255});
}

// ===================== 主菜单 =====================
void Game::drawMainMenu() {
    ClearBackground(Color{8, 10, 14, 255});
    // 背景斜纹 + 底部红色光带（RA2 冷调）
    for (int i = 0; i < 34; i++)
        DrawLine(0, i * 28, SCREEN_W, i * 28 - 300, Color{16, 18, 24, 255});
    DrawRectangleGradientV(0, SCREEN_H - 220, SCREEN_W, 220, Color{8, 10, 14, 0}, Color{70, 16, 12, 120});

    // 标题（RA2 式：黑色投影 + 红色主体 + 金色副标）
    const char* title = TR(S::GameTitle);
    int cx = SCREEN_W / 2;
    drawTextM(font, title, cx - textW(font, title, 84) / 2 + 5, 145, 84, Color{0, 0, 0, 255});
    drawTextM(font, title, cx - textW(font, title, 84) / 2, 140, 84, Color{216, 48, 40, 255});
    DrawRectangle(cx - 240, 258, 480, 3, Color{168, 40, 32, 255});
    const char* sub = TR(S::GameSub);
    drawTextM(font, sub, cx - textW(font, sub, 18) / 2, 278, 18, Color{196, 170, 110, 255});

    // 左侧竖排大按钮（RA2 主菜单布局）
    Vector2 m = mousePos();
    bool pr = mPressed(MOUSE_LEFT_BUTTON);
    int bx = 120, bw = 330, bh = 58, by = 360, gap = 18;
    if (ra2Button(font, m, pr, {(float)bx, (float)by, (float)bw, (float)bh}, TR(S::Skirmish), 24)) phase = Phase::Setup;
    if (ra2Button(font, m, pr, {(float)bx, (float)(by + (bh + gap)), (float)bw, (float)bh}, TR(S::LanGame), 24)) {
        lobbyState = 0; // 回角色选择（上次失败/退出残留）
        lobbyEditingIp = false;
        phase = Phase::NetLobby;
    }
    if (ra2Button(font, m, pr, {(float)bx, (float)(by + 2 * (bh + gap)), (float)bw, (float)bh}, TR(S::Campaign), 24))
        phase = Phase::MissionSelect;
    if (ra2Button(font, m, pr, {(float)bx, (float)(by + 3 * (bh + gap)), (float)bw, (float)bh}, TR(S::Settings), 24)) {
        settingsFromGame = false;
        phase = Phase::Settings;
    }
    if (ra2Button(font, m, pr, {(float)bx, (float)(by + 4 * (bh + gap)), (float)bw, (float)bh}, TR(S::ExitGame), 24,
                  true, true)) {
        CloseWindow();
        exit(0);
    }
    // 地图编辑器（紧凑按钮，置于退出下方）
    if (ra2Button(font, m, pr, {(float)bx, (float)(by + 5 * (bh + gap) - 10), (float)bw, 44}, TR(S::MapEditor), 20)) {
        editorNewMap();
        phase = Phase::MapEditor;
    }

    const char* tip = TR(S::MainTip);
    drawTextM(font, tip, cx - textW(font, tip, 14) / 2, SCREEN_H - 22, 14, Color{110, 112, 120, 255});
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

// 按像素宽度贪心换行绘制（中文按字、英文按词），返回行数
static int drawWrapped(Font f, const char* s, int x, int y, int maxW, int size, Color c, int maxLines) {
    int lines = 0;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty() && lines < maxLines) { drawTextM(f, cur.c_str(), x, y + lines * (size + 2), size, c); lines++; }
        cur.clear();
    };
    const char* p = s;
    while (*p && lines < maxLines) {
        std::string word;
        if ((unsigned char)*p < 0x80) { // ASCII 词（保留尾随空格）
            while (*p && (unsigned char)*p < 0x80 && *p != ' ') word += *p++;
            if (*p == ' ') word += *p++;
        } else { // 单个 UTF-8 多字节字符
            int n = (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 1;
            while (n-- > 0 && *p) word += *p++;
        }
        if (!cur.empty() && textW(f, (cur + word).c_str(), size) > maxW) flush();
        cur += word;
    }
    flush();
    return lines;
}

// ===================== 战役选择 =====================
void Game::drawMissionSelect() {
    drawMenuBackdrop(font, TR(S::Campaign));
    int cx = SCREEN_W / 2;
    Vector2 m = mousePos();

    // 阵营页签（32 关 = 中国/盟军/苏军/尤里 各 8 关，单页 2x4 网格避免超出屏幕）
    static int campTab = 0; // 0 中国 1 盟军 2 苏军 3 尤里（与任务表分段一致）
    const auto& tbl = missionTable();
    const int perCamp = 8;
    const Faction campFac[4] = {Faction::China, Faction::Allies, Faction::Soviet, Faction::Yuri};
    int tabW = 130, tabH = 40, tabGap = 10;
    int tabsX = cx - (4 * tabW + 3 * tabGap) / 2, tabsY = 108;
    for (int t = 0; t < 4; t++) {
        Rectangle r{(float)(tabsX + t * (tabW + tabGap)), (float)tabsY, (float)tabW, (float)tabH};
        bool sel = campTab == t;
        bool hover = CheckCollisionPointRec(m, r);
        DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                               sel ? Color{64, 56, 32, 255} : hover ? Color{42, 46, 52, 255} : Color{24, 26, 32, 255},
                               sel ? Color{38, 32, 18, 255} : Color{16, 18, 22, 255});
        DrawRectangleLinesEx(r, 2, sel ? Color{240, 200, 90, 255} : Color{104, 96, 56, 255});
        const char* fn = factName(campFac[t]);
        drawTextM(font, fn, (int)r.x + tabW / 2 - textW(font, fn, 19) / 2, (int)r.y + 10, 19,
                  sel ? Color{255, 226, 130, 255} : Color{190, 194, 200, 255});
        if (hover && mPressed(MOUSE_LEFT_BUTTON)) { g_sfx.play(Sfx::Click, 0.6f); campTab = t; }
    }

    // 任务卡片（4 列网格）：名称 + 简报 + 目标
    const int cols = 4;
    int cardW = 320, cardH = 168, gapX = 24, gapY = 20;
    int totalW = cols * cardW + (cols - 1) * gapX;
    int x0 = cx - totalW / 2, y0 = 180;
    int begin = campTab * perCamp, end = std::min(begin + perCamp, (int)tbl.size());
    for (int i = begin; i < end; i++) {
        const MissionDef& md = tbl[i];
        int j = i - begin;
        int gx = x0 + (j % cols) * (cardW + gapX), gy = y0 + (j / cols) * (cardH + gapY);
        Rectangle r{(float)gx, (float)gy, (float)cardW, (float)cardH};
        bool hover = CheckCollisionPointRec(m, r);
        DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                               hover ? Color{48, 52, 60, 255} : Color{28, 30, 36, 255},
                               hover ? Color{30, 32, 38, 255} : Color{18, 20, 24, 255});
        DrawRectangleLinesEx(r, 2, hover ? Color{240, 200, 90, 255} : Color{108, 96, 56, 255});
        int rx = (int)r.x, ry = (int)r.y;
        drawTextM(font, TextFormat(TR(S::MissionN), i + 1), rx + 14, ry + 10, 13, Color{150, 142, 130, 255});
        drawTextM(font, missionName(i), rx + 14, ry + 28, 22, Color{255, 210, 100, 255});
        DrawRectangle(rx + 14, ry + 58, cardW - 28, 1, Color{90, 70, 50, 255});
        int blines = drawWrapped(font, missionBrief(i), rx + 14, ry + 66, cardW - 28, 14, Color{196, 194, 200, 255}, 3);
        const char* objText = md.objective == 1 ? TextFormat(TR(S::ObjSurvive), md.objectiveTick / (30 * 60))
                              : md.objective == 2 ? TR(S::ObjTrigger) : TR(S::ObjEliminate);
        drawTextM(font, objText, rx + 14, ry + 68 + blines * 16, 14, Color{130, 200, 140, 255});
        if (hover) {
            drawTextM(font, TR(S::ClickEnter), rx + 14, ry + cardH - 28, 15, Color{255, 226, 150, 255});
            if (mPressed(MOUSE_LEFT_BUTTON)) {
                g_sfx.play(Sfx::Click, 0.6f);
                newCampaignGame(i);
                return;
            }
        }
    }

    int rows = 2;
    if (ra2Button(font, m, mPressed(MOUSE_LEFT_BUTTON), {(float)cx - 110, (float)(y0 + rows * (cardH + gapY) + 16), 220, 48}, TR(S::Back), 20))
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
    drawMenuBackdrop(font, TR(S::Skirmish));
    Vector2 m = mousePos();
    bool pr = mPressed(MOUSE_LEFT_BUTTON);
    if (previewDirty) refreshMapPreview();

    int maxAI = cfgMapSize <= 64 ? 3 : (cfgMapSize <= 96 ? 5 : 7);
    if (cfgAI > maxAI) cfgAI = maxAI;

    // ---------- 左面板：地图预览与地图参数 ----------
    int px = 48, py = 92, pw = 380;
    DrawRectangle(px, py, pw, 494, Color{18, 20, 24, 255});
    DrawRectangleLinesEx({(float)px, (float)py, (float)pw, 494}, 1, Color{74, 78, 86, 255});
    // 预览图 340x340
    int ix = px + 20, iy = py + 18;
    if (previewTex.id > 0) DrawTexture(previewTex, ix, iy, WHITE);
    DrawRectangleLinesEx({(float)ix, (float)iy, 340, 340}, 2, Color{150, 130, 80, 255});
    // 换一张
    if (ra2Button(font, m, pr, {(float)ix, (float)(iy + 342), 166, 36}, TR(S::ChangeMap), 18)) {
        previewSeed = (uint64_t)time(nullptr) * 2654435761u + 97;
        previewDirty = true;
    }
    // 地图尺寸 / 类型（点击循环切换）
    auto optRow = [&](int y, const char* label, const char* value) {
        drawTextM(font, label, ix, y + 8, 18, Color{190, 194, 200, 255});
        Rectangle r{(float)ix + 150, (float)y, 190, 36};
        bool hover = CheckCollisionPointRec(m, r);
        DrawRectangleRec(r, hover ? Color{50, 54, 62, 255} : Color{32, 34, 40, 255});
        DrawRectangleLinesEx(r, 1, hover ? Color{240, 200, 90, 255} : Color{100, 92, 56, 255});
        drawTextM(font, value, (int)r.x + 95 - textW(font, value, 17) / 2, y + 9, 17, Color{255, 224, 130, 255});
        return hover && pr;
    };
    static const int sizes[] = {64, 96, 128};
    const S sizeNames[] = {S::SizeS, S::SizeM, S::SizeL};
    int si = 0;
    while (si < 3 && sizes[si] != cfgMapSize) si++;
    if (optRow(iy + 386, TR(S::MapSize), TR(sizeNames[si]))) {
        cfgMapSize = sizes[(si + 1) % 3];
        if (cfgAI > maxAI) cfgAI = maxAI;
        previewDirty = true;
    }
    const S typeNames[] = {S::MapContinent, S::MapIslands, S::MapLake};
    if (optRow(iy + 428, TR(S::MapType), TR(typeNames[cfgMapType]))) {
        cfgMapType = (cfgMapType + 1) % 3;
        previewDirty = true;
    }

    // ---------- 右面板：玩家槽位 ----------
    int sx = 452, sy = 92, sw = SCREEN_W - sx - 48;
    DrawRectangle(sx, sy, sw, 494, Color{18, 20, 24, 255});
    DrawRectangleLinesEx({(float)sx, (float)sy, (float)sw, 494}, 1, Color{74, 78, 86, 255});
    // 表头
    int rowH = 48;
    int nameX = sx + 24, colorX = sx + 330, factX = sx + 520, delX = sx + sw - 96;
    drawTextM(font, TR(S::Player), nameX, sy + 12, 17, Color{150, 142, 130, 255});
    drawTextM(font, TR(S::Color), colorX, sy + 12, 17, Color{150, 142, 130, 255});
    drawTextM(font, TR(S::Country), factX, sy + 12, 17, Color{150, 142, 130, 255});
    int slotY = sy + 40;
    // 槽位行绘制：返回是否发生变更（需要刷新预览的出生点颜色）
    auto slotRow = [&](int idx, const char* name, int& color, int& country, int& diff, int& pers, bool isLocal) {
        int y = slotY + idx * rowH;
        bool even = idx % 2 == 0;
        DrawRectangle(sx + 8, y, sw - 16, rowH - 4, even ? Color{28, 30, 36, 255} : Color{22, 24, 28, 255});
        // 名字（本地玩家金色，AI 灰色）
        drawTextM(font, name, nameX, y + 12, 19, isLocal ? Color{255, 224, 130, 255} : Color{200, 204, 210, 255});
        // 颜色块按钮
        Rectangle cr{(float)colorX, (float)y + 6, 150, rowH - 16};
        bool chover = CheckCollisionPointRec(m, cr);
        DrawRectangleRec(cr, HOUSE_COLORS[color]);
        DrawRectangleLinesEx(cr, 2, chover ? WHITE : Color{56, 58, 64, 255});
        if (chover && pr) { color = (color + 1) % MAX_PLAYERS; g_sfx.play(Sfx::Click, 0.5f); }
        // 国家按钮（RA2 原作：选国家即定阵营；循环 10 国 + 随机）
        Rectangle fr{(float)factX, (float)y + 6, 170, rowH - 16};
        bool fhover = CheckCollisionPointRec(m, fr);
        DrawRectangleRec(fr, fhover ? Color{50, 54, 62, 255} : Color{34, 36, 42, 255});
        DrawRectangleLinesEx(fr, 1, fhover ? Color{240, 200, 90, 255} : Color{88, 84, 58, 255});
        const char* fn = country >= (int)Country::COUNT ? TR(S::Random) : countryName((Country)country);
        drawTextM(font, fn, (int)fr.x + 85 - textW(font, fn, 17) / 2, y + 13, 17, Color{224, 218, 178, 255});
        if (fhover && pr) {
            country = country >= (int)Country::COUNT ? 1 : country + 1; // 跳过 None(0)，COUNT=随机
            g_sfx.play(Sfx::Click, 0.5f);
        }
        // AI 难度 + 人格选择器（仅 AI 槽位显示）
        if (!isLocal) {
            int diffX = factX + 182;
            static const char* diffNames[] = {"简单", "普通", "困难", "残酷"};
            static const char* diffNamesEn[] = {"Easy", "Normal", "Hard", "Brutal"};
            Rectangle dr2{(float)diffX, (float)y + 6, 80, rowH - 16};
            bool dhover = CheckCollisionPointRec(m, dr2);
            DrawRectangleRec(dr2, dhover ? Color{50, 54, 62, 255} : Color{34, 36, 42, 255});
            DrawRectangleLinesEx(dr2, 1, dhover ? Color{240, 200, 90, 255} : Color{88, 84, 58, 255});
            const char* dn = g_lang ? diffNamesEn[diff] : diffNames[diff];
            drawTextM(font, dn, (int)dr2.x + 40 - textW(font, dn, 15) / 2, y + 13, 15,
                      diff >= 2 ? Color{255, 120, 90, 255} : diff == 0 ? Color{130, 200, 130, 255} : Color{220, 214, 180, 255});
            if (dhover && pr) { diff = (diff + 1) % 4; g_sfx.play(Sfx::Click, 0.5f); }
            // 人格选择器
            int persX = diffX + 88;
            static const char* persNames[] = {"均衡", "速攻", "龟缩", "轰压", "科技"};
            static const char* persNamesEn[] = {"Balanced", "Rusher", "Turtler", "Steamroller", "Tech"};
            Rectangle pr2{(float)persX, (float)y + 6, 100, rowH - 16};
            bool phover = CheckCollisionPointRec(m, pr2);
            DrawRectangleRec(pr2, phover ? Color{50, 54, 62, 255} : Color{34, 36, 42, 255});
            DrawRectangleLinesEx(pr2, 1, phover ? Color{240, 200, 90, 255} : Color{88, 84, 58, 255});
            const char* pn = g_lang ? persNamesEn[pers] : persNames[pers];
            drawTextM(font, pn, (int)pr2.x + 50 - textW(font, pn, 15) / 2, y + 13, 15, Color{196, 200, 220, 255});
            if (phover && pr) { pers = (pers + 1) % 5; g_sfx.play(Sfx::Click, 0.5f); }
        }
        // AI 移除按钮
        if (!isLocal) {
            Rectangle dr{(float)delX, (float)y + 8, 72, rowH - 20};
            if (ra2Button(font, m, pr, dr, TR(S::Remove), 15, true, true)) {
                for (int i = idx - 1; i < cfgAI - 1; i++) {
                    aiColor[i] = aiColor[i + 1]; aiCountry[i] = aiCountry[i + 1];
                    aiDiff[i] = aiDiff[i + 1]; aiPersonality[i] = aiPersonality[i + 1];
                }
                cfgAI--;
                previewDirty = true;
            }
        }
    };
    slotRow(0, TR(S::CommanderYou), cfgColor, cfgCountry, aiDiff[0], aiPersonality[0], true);
    for (int i = 0; i < cfgAI; i++)
        slotRow(i + 1, TextFormat(TR(S::ComputerN), i + 1), aiColor[i], aiCountry[i], aiDiff[i], aiPersonality[i], false);
    // 添加电脑
    if (cfgAI < maxAI) {
        int y = slotY + (cfgAI + 1) * rowH + 6;
        if (ra2Button(font, m, pr, {(float)nameX, (float)y, 200, 40}, TR(S::AddComputer), 18)) {
            aiColor[cfgAI] = (cfgAI + 1) % MAX_PLAYERS;
            aiCountry[cfgAI] = (int)Country::COUNT;
            aiDiff[cfgAI] = 1;       // 默认普通难度
            aiPersonality[cfgAI] = 0; // 默认均衡人格
            cfgAI++;
            previewDirty = true;
        }
    }

    // ---------- 底部选项条 ----------
    int oy = 600;
    DrawRectangle(48, oy, SCREEN_W - 96, 64, Color{18, 20, 24, 255});
    DrawRectangleLinesEx({48, (float)oy, (float)SCREEN_W - 96, 64}, 1, Color{74, 78, 86, 255});
    auto optBtn = [&](int x, const char* label, const char* value, int w) {
        drawTextM(font, label, x, oy + 20, 18, Color{190, 194, 200, 255});
        int lx = x + textW(font, label, 18) + 16;
        Rectangle r{(float)lx, (float)oy + 10, (float)w, 44};
        bool hover = CheckCollisionPointRec(m, r);
        DrawRectangleRec(r, hover ? Color{50, 54, 62, 255} : Color{32, 34, 40, 255});
        DrawRectangleLinesEx(r, 1, hover ? Color{240, 200, 90, 255} : Color{100, 92, 56, 255});
        drawTextM(font, value, lx + w / 2 - textW(font, value, 18) / 2, oy + 22, 18, Color{255, 224, 130, 255});
        return hover && pr;
    };
    static const int monies[] = {5000, 10000, 20000, 50000};
    if (optBtn(80, TR(S::StartMoney), TextFormat("%d", cfgMoney), 130)) {
        int i = 0;
        while (i < 4 && monies[i] != cfgMoney) i++;
        cfgMoney = monies[(i + 1) % 4];
        g_sfx.play(Sfx::Click, 0.5f);
    }
    const S speedNames[] = {S::SpeedSlow, S::SpeedNormal, S::SpeedFast};
    if (optBtn(420, TR(S::GameSpeed), TR(speedNames[gameSpeed]), 110)) {
        gameSpeed = (gameSpeed + 1) % 3;
        g_sfx.play(Sfx::Click, 0.5f);
    }
    // 音量：热更新立即生效（音效+音乐），无需重启
    static const int vols[] = {0, 25, 50, 75, 100};
    if (optBtn(660, TR(S::Volume), TextFormat("%d", vols[cfgVolume]), 70)) {
        cfgVolume = (cfgVolume + 1) % 5;
        g_sfx.setMasterVol(vols[cfgVolume] / 100.0f);
        g_sfx.play(Sfx::Click, 0.5f);
        saveSettings();
    }
    if (optBtn(830, TR(S::Crates), TR(cfgCrates ? S::On : S::Off), 70)) {
        cfgCrates = !cfgCrates;
        g_sfx.play(Sfx::Click, 0.5f);
    }
    if (optBtn(1000, TR(S::AIAlliance), TR(cfgAlliance ? S::On : S::Off), 70)) {
        cfgAlliance = !cfgAlliance;
        g_sfx.play(Sfx::Click, 0.5f);
    }

    // ---------- 底部：开始游戏 / 返回 ----------
    int by = 700;
    if (ra2Button(font, m, pr, {(float)(SCREEN_W / 2 - 330), (float)by, 320, 62}, TR(S::StartGame), 28))
        newGame(previewSeed); // 用预览的同一张图开局：所见即所玩
    if (ra2Button(font, m, pr, {(float)(SCREEN_W / 2 + 30), (float)by, 200, 62}, TR(S::Back), 24))
        phase = Phase::MainMenu;
}
