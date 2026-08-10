#include "game/game.h"
#include "game/data.h"
#include "gfx/sprites.h"
#include "gfx/assets.h"
#include <raylib.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

// 地图编辑器：游戏内可视化编辑，保存为 maps/*.txt（可被战役 MapFile= 引用或遭遇战加载）
// 工具：0 地形刷 1 装饰 2 单位 3 建筑 4 出生点 5 擦除

// 编辑器放置的实体（内存中，保存时写出）
struct EdEnt {
    bool isBld = false;
    int player = -1;
    int typeIdx = 0;
    int x = 0, y = 0;
    bool guard = false;
};
static std::vector<EdEnt> edEnts;
static std::vector<Vec2i> edSpawns;
static bool edMapInited = false;

static const char* terrainNames[] = {"clear", "rough", "water", "ore", "gems", "bridge"};
static const Terrain terrainTypes[] = {Terrain::Clear, Terrain::Rough, Terrain::Water, Terrain::Ore, Terrain::Gems, Terrain::Bridge};
static const char* overlayNames[] = {"tree1", "tree2", "tree3", "rock1", "rock2"};
static const Overlay overlayTypes[] = {Overlay::Tree1, Overlay::Tree2, Overlay::Tree3, Overlay::Rock1, Overlay::Rock2};

void Game::editorNewMap() {
    previewMap.cells.assign(edMapSize * edMapSize, Cell{});
    previewMap.w = edMapSize;
    previewMap.h = edMapSize;
    for (auto& c : previewMap.cells) c.terrain = Terrain::Clear;
    edEnts.clear();
    edSpawns.clear();
    edSpawns.push_back({edMapSize / 4, edMapSize / 4});
    edSpawns.push_back({edMapSize * 3 / 4, edMapSize * 3 / 4});
    edMapInited = true;
    camX = 0;
    camY = 0;
}

void Game::editorPlace(int mx, int my) {
    float wx, wy;
    screenToWorld(mx, my, wx, wy);
    int tx, ty;
    screenToTile(wx, wy, tx, ty);
    if (!previewMap.inBounds(tx, ty)) return;
    Cell& c = previewMap.at(tx, ty);
    switch (edTool) {
        case 0: // 地形刷
            c.terrain = terrainTypes[edTerrain];
            c.overlay = Overlay::None;
            c.height = (c.terrain == Terrain::Water || c.terrain == Terrain::Bridge)
                     ? 0 : (uint8_t)clampi(edHeight, 0, 3);
            if (c.terrain == Terrain::Ore) { c.ore = 1000; c.oreMax = 1000; }
            else if (c.terrain == Terrain::Gems) { c.ore = 2000; c.oreMax = 2000; }
            else { c.ore = 0; c.oreMax = 0; }
            break;
        case 1: // 装饰
            c.overlay = overlayTypes[edOverlay];
            break;
        case 2: { // 单位
            UnitType ut = (UnitType)edUnitIdx;
            if ((int)ut >= (int)UnitType::COUNT) break;
            edEnts.push_back({false, edPlayer, (int)ut, tx, ty, false});
            break;
        }
        case 3: { // 建筑
            BldType bt = (BldType)edBldIdx;
            if ((int)bt >= (int)BldType::COUNT) break;
            edEnts.push_back({true, edPlayer, (int)bt, tx, ty, false});
            break;
        }
        case 4: // 出生点
            if (edSpawnIdx < (int)edSpawns.size())
                edSpawns[edSpawnIdx] = {tx, ty};
            else
                edSpawns.push_back({tx, ty});
            break;
        case 5: // 擦除
            c.terrain = Terrain::Clear;
            c.overlay = Overlay::None;
            c.ore = 0;
            // 删除该位置的实体
            edEnts.erase(std::remove_if(edEnts.begin(), edEnts.end(),
                [tx, ty](const EdEnt& e) { return e.x == tx && e.y == ty; }), edEnts.end());
            break;
    }
}

void Game::editorSave() {
    char path[256];
    snprintf(path, sizeof(path), "maps/%s.txt", edMapName.c_str());
    FILE* f = fopen(path, "wb");
    if (!f) { message(TextFormat("保存失败: %s", path)); return; }
    fprintf(f, "# OpenRA2 custom map - %s\n", edMapName.c_str());
    fprintf(f, "size %d %d\n", previewMap.w, previewMap.h);
    fprintf(f, "fill clear\n");
    // 写出非 clear 地形（矩形扫描优化：逐格写出）
    for (int y = 0; y < previewMap.h; y++) {
        for (int x = 0; x < previewMap.w; x++) {
            const Cell& c = previewMap.at(x, y);
            if (c.terrain != Terrain::Clear) {
                fprintf(f, "rect %s %d %d 1 1\n", terrainNames[(int)c.terrain], x, y);
            }
            if (c.overlay != Overlay::None) {
                int oi = (int)c.overlay - 1;
                if (oi >= 0 && oi < 5)
                    fprintf(f, "deco %s %d %d 1 1 1\n", overlayNames[oi], x, y);
            }
        }
    }
    // 出生点
    for (size_t i = 0; i < edSpawns.size(); i++)
        fprintf(f, "spawn %d %d %d\n", (int)i, edSpawns[i].x, edSpawns[i].y);
    // 实体
    for (const EdEnt& e : edEnts) {
        if (e.isBld) {
            fprintf(f, "bld %d %s %d %d\n", e.player, bldTypeKey((BldType)e.typeIdx), e.x, e.y);
        } else {
            fprintf(f, "unit %d %s %d %d%s\n", e.player, unitTypeKey((UnitType)e.typeIdx), e.x, e.y, e.guard ? " guard" : "");
        }
    }
    fclose(f);
    message(TextFormat("地图已保存: %s", path));
}

void Game::drawMapEditor() {
    if (!edMapInited) editorNewMap();

    // 摄像机卷屏（方向键/WASD）
    float spd = camSpeed * 2.0f;
    if (kDown(KEY_LEFT) || kDown(KEY_A)) camX -= spd;
    if (kDown(KEY_RIGHT) || kDown(KEY_D)) camX += spd;
    if (kDown(KEY_UP) || kDown(KEY_W)) camY -= spd;
    if (kDown(KEY_DOWN) || kDown(KEY_S)) camY += spd;
    if (kPressed(KEY_ESCAPE)) phase = Phase::MainMenu;
    int panelW = 200;
    int viewW = SCREEN_W - panelW;
    float minX = -(float)(previewMap.h - 1) * (TILE_W / 2.0f);
    float maxX = (float)(previewMap.w - 1) * (TILE_W / 2.0f) + (float)TILE_W - (float)viewW;
    float maxY = (float)(previewMap.w + previewMap.h - 2) * (TILE_H / 2.0f) + (float)TILE_H - (float)SCREEN_H;
    if (maxX < minX) { float mid = (minX + maxX + (float)viewW) * 0.5f - (float)viewW * 0.5f; minX = maxX = mid; }
    if (maxY < 0) maxY = 0;
    camX = std::max(minX, std::min(maxX, camX));
    camY = std::max(0.0f, std::min(maxY, camY));

    // 背景
    ClearBackground(Color{18, 20, 26, 255});

    // 渲染 previewMap 地形瓦片
    for (int ty = 0; ty < previewMap.h; ty++) {
        for (int tx = 0; tx < previewMap.w; tx++) {
            const Cell& c = previewMap.at(tx, ty);
            int px, py;
            tileToScreen(tx, ty, px, py);
            py -= heightScreenY(c.height);
            int sx = px - (int)camX, sy = py - (int)camY;
            if (sx < -TILE_W || sx > viewW + TILE_W || sy < -TILE_H || sy > SCREEN_H + TILE_H) continue;
            const Sprite& s = g_sprites.tile(c.terrain, c.variant & 7);
            DrawTexture(s.tex, sx - TILE_W / 2, sy, WHITE);
        }
    }
    // 渲染装饰物（树/岩石）
    for (int ty = 0; ty < previewMap.h; ty++) {
        for (int tx = 0; tx < previewMap.w; tx++) {
            const Cell& c = previewMap.at(tx, ty);
            if (c.overlay == Overlay::None) continue;
            int px, py;
            tileToScreen(tx, ty, px, py);
            py -= heightScreenY(c.height);
            int sx = px - (int)camX, sy = py - (int)camY;
            if (sx < -64 || sx > viewW + 64 || sy < -96 || sy > SCREEN_H + 64) continue;
            const Sprite& s = g_sprites.overlaySpr(c.overlay);
            DrawTexture(s.tex, sx - s.ox, sy + TILE_H / 2 - s.oy, WHITE);
        }
    }

    // 左侧工具栏
    DrawRectangle(0, 0, panelW, SCREEN_H, Color{30, 30, 40, 230});
    DrawLine(panelW, 0, panelW, SCREEN_H, Color{80, 80, 100, 255});

    Font f = font;
    drawTextM(f, "地图编辑器", 10, 10, 18, GOLD);

    Vector2 m = mousePos();
    const char* toolNames[] = {"地形刷", "装饰", "单位", "建筑", "出生点", "擦除"};
    for (int i = 0; i < 6; i++) {
        Rectangle r = {10, (float)(40 + i * 32), panelW - 20, 28};
        bool active = edTool == i;
        if (ra2Button(f, m, mPressed(0), r, toolNames[i], 16, true, active)) {
            edTool = i;
        }
    }

    // 工具属性
    int y0 = 40 + 6 * 32 + 10;
    drawTextM(f, "--- 属性 ---", 10, y0, 14, GRAY);
    y0 += 20;
    switch (edTool) {
        case 0: // 地形
            for (int i = 0; i < 6; i++) {
                Rectangle r = {10, (float)(y0 + i * 24), panelW - 20, 22};
                if (ra2Button(f, m, mPressed(0), r, terrainNames[i], 14, true, edTerrain == i))
                    edTerrain = i;
            }
            y0 += 6 * 24 + 4;
            drawTextM(f, TextFormat("高度: %d  [ / ]", edHeight), 10, y0, 14, LIGHTGRAY);
            if (IsKeyPressed(KEY_LEFT_BRACKET)) edHeight = clampi(edHeight - 1, 0, 3);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) edHeight = clampi(edHeight + 1, 0, 3);
            y0 += 24;
            break;
        case 1: // 装饰
            for (int i = 0; i < 5; i++) {
                Rectangle r = {10, (float)(y0 + i * 24), panelW - 20, 22};
                if (ra2Button(f, m, mPressed(0), r, overlayNames[i], 14, true, edOverlay == i))
                    edOverlay = i;
            }
            y0 += 5 * 24 + 10;
            break;
        case 2: { // 单位
            drawTextM(f, "玩家:", 10, y0, 14, GRAY);
            Rectangle pr = {60, (float)y0, 60, 22};
            if (ra2Button(f, m, mPressed(0), pr, TextFormat("%d", edPlayer), 14)) edPlayer = (edPlayer + 2) % 8 - 1;
            y0 += 24;
            drawTextM(f, TextFormat("单位: %s", unitTypeKey((UnitType)edUnitIdx)), 10, y0, 14, WHITE);
            y0 += 20;
            Rectangle prev = {10, (float)y0, 80, 22};
            Rectangle next = {100, (float)y0, 80, 22};
            if (ra2Button(f, m, mPressed(0), prev, "< 上一个", 14)) edUnitIdx = (edUnitIdx + (int)UnitType::COUNT - 1) % (int)UnitType::COUNT;
            if (ra2Button(f, m, mPressed(0), next, "下一个 >", 14)) edUnitIdx = (edUnitIdx + 1) % (int)UnitType::COUNT;
            y0 += 28;
            break;
        }
        case 3: { // 建筑
            drawTextM(f, "玩家:", 10, y0, 14, GRAY);
            Rectangle pr = {60, (float)y0, 60, 22};
            if (ra2Button(f, m, mPressed(0), pr, TextFormat("%d", edPlayer), 14)) edPlayer = (edPlayer + 2) % 8 - 1;
            y0 += 24;
            drawTextM(f, TextFormat("建筑: %s", bldTypeKey((BldType)edBldIdx)), 10, y0, 14, WHITE);
            y0 += 20;
            Rectangle prev = {10, (float)y0, 80, 22};
            Rectangle next = {100, (float)y0, 80, 22};
            if (ra2Button(f, m, mPressed(0), prev, "< 上一个", 14)) edBldIdx = (edBldIdx + (int)BldType::COUNT - 1) % (int)BldType::COUNT;
            if (ra2Button(f, m, mPressed(0), next, "下一个 >", 14)) edBldIdx = (edBldIdx + 1) % (int)BldType::COUNT;
            y0 += 28;
            break;
        }
        case 4: // 出生点
            drawTextM(f, TextFormat("出生点 %d", edSpawnIdx), 10, y0, 14, WHITE);
            y0 += 20;
            Rectangle prev = {10, (float)y0, 80, 22};
            Rectangle next = {100, (float)y0, 80, 22};
            if (ra2Button(f, m, mPressed(0), prev, "< 上一个", 14)) edSpawnIdx = (edSpawnIdx + (int)edSpawns.size() - 1) % std::max(1, (int)edSpawns.size());
            if (ra2Button(f, m, mPressed(0), next, "下一个 >", 14)) edSpawnIdx = (edSpawnIdx + 1) % std::max(1, (int)edSpawns.size());
            y0 += 28;
            break;
    }

    // 地图名称输入 + 保存/新建/返回
    int yBot = SCREEN_H - 100;
    drawTextM(f, "地图名:", 10, yBot, 14, GRAY);
    DrawRectangle(70, yBot, panelW - 80, 22, Color{50, 50, 60, 255});
    drawTextM(f, edMapName.c_str(), 72, yBot + 2, 14, WHITE);
    if (mPressed(0) && m.x >= 70 && m.x < panelW && m.y >= yBot && m.y < yBot + 22) {
        static int nameIdx = 0;
        nameIdx++;
        edMapName = TextFormat("custom_%d", nameIdx + 1);
    }
    yBot += 28;
    Rectangle saveR = {10, (float)yBot, 80, 28};
    Rectangle newR = {100, (float)yBot, 80, 28};
    if (ra2Button(f, m, mPressed(0), saveR, "保存", 16)) editorSave();
    if (ra2Button(f, m, mPressed(0), newR, "新建", 16)) editorNewMap();
    yBot += 32;
    Rectangle backR = {10, (float)yBot, panelW - 20, 28};
    if (ra2Button(f, m, mPressed(0), backR, "返回主菜单", 16)) phase = Phase::MainMenu;

    // 鼠标放置
    if (m.x >= panelW && mPressed(0)) {
        editorPlace((int)m.x, (int)m.y);
    }
    // 右键删除
    if (m.x >= panelW && mPressed(1)) {
        edTool = 5;
        editorPlace((int)m.x, (int)m.y);
        edTool = 0;
    }

    // 绘制编辑器实体（用实际精灵 + 阵营色标记框）
    for (const EdEnt& e : edEnts) {
        int px, py;
        tileToScreen(e.x, e.y, px, py);
        int sx = px - (int)camX, sy = py - (int)camY;
        if (sx < panelW - 80 || sx > SCREEN_W + 80 || sy < -80 || sy > SCREEN_H + 80) continue;
        int colIdx = e.player < 0 ? -1 : e.player % MAX_PLAYERS;
        if (e.isBld) {
            const BldDef& d = bldDef((BldType)e.typeIdx);
            // 建筑锚点 = 占地东南角瓦片南角
            int bx, by;
            tileToScreen(e.x + d.w - 1, e.y + d.h - 1, bx, by);
            int bsx = bx - (int)camX, bsy = by + TILE_H - (int)camY;
            const Sprite& s = g_sprites.building((BldType)e.typeIdx, colIdx, false);
            DrawTexture(s.tex, bsx - s.ox, bsy - s.oy, WHITE);
            // 占地框
            Color col = colIdx < 0 ? GRAY : HOUSE_COLORS[colIdx];
            DrawRectangleLines(sx - TILE_W / 2 + 2, sy + 2, d.w * TILE_W / 2, d.h * TILE_H, col);
        } else {
            const Sprite& s = g_sprites.unitBody((UnitType)e.typeIdx, 0, 0, colIdx < 0 ? 0 : colIdx);
            // 编辑器预览：脚点按菱形中心（与 HEAD 一致；局内 unitScreenPos 仍为北尖）
            DrawTexture(s.tex, sx - s.ox, sy + TILE_H / 2 - s.oy, WHITE);
        }
        Color col = colIdx < 0 ? GRAY : HOUSE_COLORS[colIdx];
        DrawRectangleLines(sx - 10, sy - 2, 20, 4, col);
    }
    // 出生点
    for (size_t i = 0; i < edSpawns.size(); i++) {
        int sx, sy;
        tileToScreen(edSpawns[i].x, edSpawns[i].y, sx, sy);
        sx -= (int)camX;
        sy -= (int)camY;
        if (sx < panelW || sx > SCREEN_W || sy < 0 || sy > SCREEN_H) continue;
        DrawCircle(sx, sy, 10, Color{255, 255, 0, 120});
        DrawCircleLines(sx, sy, 10, YELLOW);
        drawTextM(f, TextFormat("P%d", (int)i), sx - 8, sy - 20, 12, YELLOW);
    }

    // 提示
    drawTextM(f, "左键放置/右键删除 | 方向键卷屏 | ESC返回", panelW + 10, 10, 14, GRAY);
    drawTextM(f, TextFormat("实体数: %d  出生点: %d", (int)edEnts.size(), (int)edSpawns.size()), panelW + 10, 28, 14, GRAY);

    // 消息（保存成功等）
    if (msgTimer > 0) {
        msgTimer -= GetFrameTime();
        drawTextM(f, msg.c_str(), panelW + 10, 50, 16, GOLD);
    }
}
