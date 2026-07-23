// Game 的 HUD 部分实现（侧边栏/小地图/菜单）
#include "game/game.h"
#include "game/campaign.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cstring>
#include <ctime>

extern const int HUD_UNUSED; // 占位

static void drawTextF(Font f, const char* s, int x, int y, int size, Color c) {
    DrawTextEx(f, s, {(float)x, (float)y}, (float)size, 1, c);
}

bool Game::uiButton(Rectangle r, const char* text, bool enabled, bool active) {
    Vector2 m = mousePos();
    bool hover = CheckCollisionPointRec(m, r) && enabled;
    Color bg = active ? Color{70, 90, 60, 255} : (hover ? Color{60, 64, 72, 255} : Color{38, 40, 46, 255});
    if (!enabled) bg = Color{28, 28, 32, 255};
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, active ? Color{120, 220, 100, 255} : Color{80, 84, 92, 255});
    if (text && text[0]) {
        int tw = (int)MeasureTextEx(font, text, 14, 1).x;
        drawTextF(font, text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - 7), 14,
                  enabled ? WHITE : Color{110, 110, 110, 255});
    }
    bool clicked = hover && mPressed(MOUSE_LEFT_BUTTON);
    if (clicked) g_sfx.play(Sfx::Click, 0.6f);
    return clicked;
}

std::vector<BldType> Game::tabBuildings() const {
    std::vector<BldType> v;
    Faction f = world.players[localPlayer].faction;
    if (uiTab == 0) {
        // 主要建筑
        static const BldType mainB[] = {
            BldType::PowerPlant, BldType::TeslaReactor, BldType::OreRefinery, BldType::Barracks,
            BldType::WarFactory, BldType::Radar, BldType::AirForceCmd, BldType::NavalYard, BldType::BattleLab,
            BldType::NuclearReactor, BldType::OrePurifier, BldType::IndustrialPlant,
            BldType::NukeSilo, BldType::WeatherDevice, BldType::IronCurtain, BldType::ChronoSphere,
        };
        for (BldType t : mainB)
            if (bldDef(t).factionMask & (1 << (int)f)) v.push_back(t);
    } else {
        // 防御建筑
        static const BldType defB[] = {
            BldType::Pillbox, BldType::SentryGun, BldType::FlakCannon,
            BldType::PrismTower, BldType::TeslaCoil, BldType::GrandCannon,
            BldType::PatriotMissile, BldType::Wall,
        };
        for (BldType t : defB)
            if (bldDef(t).factionMask & (1 << (int)f)) v.push_back(t);
    }
    return v;
}

std::vector<UnitType> Game::tabUnits() const {
    std::vector<UnitType> v;
    Faction f = world.players[localPlayer].faction;
    for (int i = 0; i < (int)UnitType::COUNT; i++) {
        const UnitDef& u = unitDef((UnitType)i);
        if (!(u.factionMask & (1 << (int)f))) continue;
        bool nav = u.isNaval() || u.isAmphib();
        if (uiTab == 2 && u.isInfantry()) v.push_back((UnitType)i);          // 步兵
        if (uiTab == 3 && !u.isInfantry() && !nav) v.push_back((UnitType)i); // 车辆/空军
        if (uiTab == 4 && nav) v.push_back((UnitType)i);                     // 海军
    }
    return v;
}

void Game::drawHUD() {
    int sbX = SCREEN_W - sidebarW;
    // 侧边栏背景
    DrawRectangle(sbX, 0, sidebarW, SCREEN_H, Color{22, 23, 27, 255});
    DrawLine(sbX, 0, sbX, SCREEN_H, Color{90, 94, 102, 255});

    Player& me = world.players[localPlayer];

    // 资金
    drawTextF(font, TextFormat("%d", me.money), sbX + 46, 10, 22,
              me.money > 0 ? Color{255, 215, 80, 255} : RED);
    drawTextF(font, "资金", sbX + 8, 14, 14, Color{200, 200, 200, 255});

    // 电力条
    int pwrX = sbX + 10, pwrY = 40, pwrH = 200;
    DrawRectangle(pwrX, pwrY, 12, pwrH, Color{40, 40, 44, 255});
    float ratio = me.powerMade > 0 ? std::min(1.0f, (float)me.powerUsed / std::max(1, me.powerMade)) : 1.0f;
    int usedH = (int)(pwrH * ratio);
    Color pc = me.lowPower() ? RED : (ratio > 0.8f ? YELLOW : Color{80, 220, 80, 255});
    DrawRectangle(pwrX + 1, pwrY + pwrH - usedH, 10, usedH, pc);
    drawTextF(font, "电", pwrX - 2, pwrY + pwrH + 4, 13, Color{200, 200, 200, 255});
    if (me.lowPower()) drawTextF(font, "低电力!", sbX + 30, pwrY + pwrH - 20, 13, RED);

    // 小地图
    drawMinimap();

    // ---- 超武区（小地图下方）：可用超武按钮 + 充能进度 ----
    {
        int swY = 248;
        int bi = 0;
        for (int i = 0; i < (int)SWType::COUNT; i++) {
            SWType t = (SWType)i;
            const SWDef& sd = swDef(t);
            // 仅显示本阵营可建的超武
            if (!(bldDef(sd.fromBld).factionMask & (1 << (int)me.faction))) continue;
            bool hasBld = world.hasBld(localPlayer, sd.fromBld);
            Rectangle r{(float)sbX + 6 + bi * 90, (float)swY, 86, 74};
            bi++;
            bool ready = me.swReady[i];
            bool targeting = targetingSW == t;
            // 背景与边框
            DrawRectangleRec(r, targeting ? Color{70, 48, 40, 255} : (ready ? Color{52, 60, 44, 255} : Color{30, 32, 38, 255}));
            DrawRectangleLinesEx(r, 1, targeting ? Color{255, 120, 90, 255} : (ready ? GREEN : Color{70, 74, 82, 255}));
            // 名称
            drawTextF(font, sd.name, (int)r.x + 4, (int)r.y + 4, 13,
                      hasBld ? (ready ? Color{180, 255, 150, 255} : WHITE) : Color{110, 110, 110, 255});
            if (!hasBld) {
                drawTextF(font, "需建筑", (int)r.x + 16, (int)r.y + 40, 12, Color{120, 110, 100, 255});
            } else if (ready) {
                // 就绪：闪烁提示
                if ((world.tick / 15) % 2) drawTextF(font, "就绪", (int)r.x + 26, (int)r.y + 30, 16, Color{120, 255, 120, 255});
                drawTextF(font, "点击选目标", (int)r.x + 6, (int)r.y + 54, 11, Color{200, 220, 180, 255});
            } else {
                // 充能进度条 + 倒计时
                float frac = (float)me.swCharge[i] / sd.chargeTime;
                DrawRectangle((int)r.x + 6, (int)r.y + 38, 74, 8, Color{40, 40, 44, 255});
                DrawRectangle((int)r.x + 7, (int)r.y + 39, (int)(72 * frac), 6, Color{220, 170, 60, 255});
                int secs = (sd.chargeTime - me.swCharge[i]) / LOGIC_FPS;
                drawTextF(font, TextFormat("%d:%02d", secs / 60, secs % 60), (int)r.x + 26, (int)r.y + 54, 13,
                          Color{200, 190, 150, 255});
            }
            // 点击：就绪 → 进入目标选择
            if (ready && CheckCollisionPointRec(mousePos(), r) && mPressed(MOUSE_LEFT_BUTTON)) {
                targetingSW = targeting ? SWType::COUNT : t;
                g_sfx.play(Sfx::Click, 0.6f);
                if (targetingSW != SWType::COUNT) message("选择目标位置（右键取消）");
            }
        }
    }

    // 选项卡
    static const char* tabs[] = {"建筑", "防御", "步兵", "车辆", "海军"};
    for (int i = 0; i < 5; i++) {
        Rectangle tr{(float)sbX + 6 + i * 37, 330, 35, 22};
        if (uiButton(tr, tabs[i], true, uiTab == i)) uiTab = i;
    }

    // 生产图标网格（行数封顶，避免画出屏幕；底部留给维修/出售/菜单按钮）
    int gx = sbX + 6, gy = 358, gw = 86, gh = 66, cols = 2;
    int maxRows = (SCREEN_H - 56 - gy) / (gh + 4);
    int idx = 0;
    auto drawItem = [&](bool isUnit, int typeIdx, const Sprite& icon, const char* name, int cost,
                        bool canBuild, ProdItem& prod, int queuedN) {
        int ix = gx + (idx % cols) * (gw + 4);
        int iy = gy + (idx / cols) * (gh + 4);
        idx++;
        if ((idx - 1) / cols >= maxRows) return; // 超出一页：截断
        Rectangle r{(float)ix, (float)iy, (float)gw, (float)gh};
        bool activeThis = prod.active && prod.typeIdx == typeIdx && prod.isUnit == isUnit;
        bool readyThis = activeThis && prod.ready;
        // 背景与图标
        DrawRectangleRec(r, activeThis ? Color{50, 58, 46, 255} : Color{30, 32, 38, 255});
        DrawRectangleLinesEx(r, 1, readyThis ? GREEN : (activeThis ? Color{180, 200, 120, 255} : Color{70, 74, 82, 255}));
        DrawTexture(icon.tex, ix + (gw - icon.tex.width) / 2, iy + 2, canBuild ? WHITE : Color{90, 90, 90, 255});
        // 进度遮罩
        if (activeThis && !prod.ready) {
            int time = isUnit ? unitDef((UnitType)typeIdx).buildTime : bldDef((BldType)typeIdx).buildTime;
            float frac = (float)prod.progress / time;
            DrawRectangle(ix, iy + (int)(gh * (1 - frac)), gw, (int)(gh * frac), Color{0, 0, 0, 130});
            drawTextF(font, TextFormat("%d%%", (int)(frac * 100)), ix + 30, iy + 26, 14, WHITE);
        }
        if (readyThis) drawTextF(font, "就绪", ix + 27, iy + 24, 15, GREEN);
        // 排队数量角标（RA2 原作：含进行中项）
        int totalN = queuedN + (activeThis ? 1 : 0);
        if (isUnit && totalN > 0) {
            DrawRectangle(ix + gw - 20, iy + 2, 18, 16, Color{0, 0, 0, 160});
            drawTextF(font, TextFormat("%d", totalN), ix + gw - 15, iy + 3, 13, Color{255, 220, 100, 255});
        }
        // 名称与造价
        drawTextF(font, name, ix + 2, iy + gh - 24, 11, canBuild ? WHITE : Color{120, 120, 120, 255});
        drawTextF(font, TextFormat("%d", cost), ix + 2, iy + gh - 12, 11,
                  me.money >= cost ? Color{255, 215, 80, 255} : RED);
        // 点击
        if (CheckCollisionPointRec(mousePos(), r)) {
            if (mPressed(MOUSE_LEFT_BUTTON)) {
                // 就绪项优先：canBuild 为"能否开始新生产"（含队列空闲），不能阻塞就绪建筑的放置
                if (readyThis) {
                    // 建筑就绪 → 进入放置模式
                    me.placingBld = (BldType)typeIdx;
                    placing = true;
                    message("选择放置位置（右键取消）");
                } else if (!canBuild) { message("无法建造：缺前置建筑或资金不足"); }
                else if (isUnit || !activeThis) {
                    // 单位允许重复点击排队（RA2 原作）
                    bool ok = isUnit ? world.startUnitProd(localPlayer, (UnitType)typeIdx)
                                     : world.startBldProd(localPlayer, (BldType)typeIdx);
                    if (!ok) message("生产队列忙或条件不足");
                }
            }
            if (mPressed(MOUSE_RIGHT_BUTTON)) {
                if (isUnit && totalN > 0) {
                    world.cancelUnitProd(localPlayer, (UnitType)typeIdx);
                    message("已取消一个");
                } else if (!isUnit && activeThis) {
                    world.cancelProd(localPlayer, false);
                    message("已取消生产");
                }
            }
        }
    };

    if (uiTab <= 1) {
        for (BldType t : tabBuildings()) {
            const BldDef& d = bldDef(t);
            bool can = world.hasBld(localPlayer, BldType::ConYard) && world.prereqMet(localPlayer, d)
                       && me.money >= d.cost && !me.bldProd.active;
            drawItem(false, (int)t, g_sprites.iconBld(t, me.colorId), d.name, d.cost, can, me.bldProd, 0);
        }
    } else {
        for (UnitType t : tabUnits()) {
            const UnitDef& u = unitDef(t);
            int cat = u.prodCat();
            int qn = 0;
            for (int q : me.unitQueue[cat])
                if (q == (int)t) qn++;
            bool can = world.unitPrereqMet(localPlayer, u) && world.hasFactoryFor(localPlayer, u)
                       && me.money >= u.cost;
            drawItem(true, (int)t, g_sprites.iconUnit(t, me.colorId), u.name, u.cost, can, me.unitProd[cat], qn);
        }
    }

    // ---- 侧边栏底部：维修 / 出售 / 菜单（RA2 标志性按钮）----
    {
        int bw2 = 56, bh2 = 40, by2 = SCREEN_H - bh2 - 8;
        Rectangle repR{(float)sbX + 6, (float)by2, (float)bw2, (float)bh2};
        Rectangle selR{(float)sbX + 6 + bw2 + 5, (float)by2, (float)bw2, (float)bh2};
        Rectangle mnuR{(float)sbX + 6 + 2 * (bw2 + 5), (float)by2, (float)bw2, (float)bh2};
        if (uiButton(repR, "维修", true, sideMode == 1)) {
            sideMode = sideMode == 1 ? 0 : 1;
            if (sideMode == 1) message("维修模式：点击己方受损建筑（右键取消）");
        }
        if (uiButton(selR, "出售", true, sideMode == 2)) {
            sideMode = sideMode == 2 ? 0 : 2;
            if (sideMode == 2) message("出售模式：点击己方建筑（右键取消）");
        }
        if (uiButton(mnuR, "菜单", true)) showMenu = true;
    }

    // 提示消息
    if (msgTimer > 0) {
        int tw = (int)MeasureTextEx(font, msg.c_str(), 16, 1).x;
        DrawRectangle(SCREEN_W / 2 - tw / 2 - 10, 8, tw + 20, 26, Color{0, 0, 0, 160});
        drawTextF(font, msg.c_str(), SCREEN_W / 2 - tw / 2, 12, 16, Color{255, 230, 140, 255});
    }

    // 战役目标状态（左上角）
    if (campaignMission >= 0 && !gameOver) {
        const MissionDef& md = missionTable()[campaignMission];
        std::string obj = md.name;
        obj += " · ";
        if (md.objective == 1) {
            int remain = (md.objectiveTick - (int)world.tick) / LOGIC_FPS;
            if (remain < 0) remain = 0;
            obj += TextFormat("坚守 %d:%02d", remain / 60, remain % 60);
        } else if (nextWave < md.waves.size()) {
            obj += TextFormat("敌军增援将至（第%d/%d波）", (int)nextWave + 1, (int)md.waves.size());
        } else {
            obj += "歼灭所有敌军";
        }
        DrawRectangle(6, 30, (int)MeasureTextEx(font, obj.c_str(), 14, 1).x + 12, 22, Color{0, 0, 0, 140});
        drawTextF(font, obj.c_str(), 12, 34, 14, Color{230, 200, 130, 255});
    }

    // 选择信息
    if (!sel.empty()) {
        drawTextF(font, TextFormat("已选 %d 单位", (int)sel.size()), 10, SCREEN_H - 24, 14, Color{180, 220, 180, 255});
        // 单个运输载具选中：显示载员与卸载提示
        if (sel.size() == 1 && world.valid(sel[0])) {
            const World::Ent& e = world.ents[sel[0]];
            if (!e.isBuilding && unitDef(e.utype).cargoCap > 0)
                drawTextF(font, TextFormat("载员 %d/%d  U键卸载", (int)e.cargo.size(), unitDef(e.utype).cargoCap),
                          130, SCREEN_H - 24, 14, Color{140, 200, 230, 255});
        }
    }
    // 操作提示
    drawTextF(font, "左键选择/框选 右键移动/攻击(点己方运输船=登船) A+右键攻击移动 D展开 S停止 H回基地 X卖建筑 R设集结点 U卸载 ESC菜单",
              10, SCREEN_H - 44, 12, Color{130, 130, 140, 255});

    // 暂停/菜单/结算
    if (paused && !gameOver) {
        drawTextF(font, "已暂停", SCREEN_W / 2 - 30, SCREEN_H / 2, 28, WHITE);
    }
    if (showMenu || gameOver) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 160});
        int mw = 320, mh = 300;
        int mx = SCREEN_W / 2 - mw / 2, my = SCREEN_H / 2 - mh / 2;
        DrawRectangle(mx, my, mw, mh, Color{30, 32, 38, 255});
        DrawRectangleLinesEx({(float)mx, (float)my, (float)mw, (float)mh}, 2, Color{100, 106, 116, 255});
        if (gameOver) {
            const char* t = victory ? "胜 利" : "失 败";
            drawTextF(font, t, mx + mw / 2 - 40, my + 24, 34, victory ? Color{120, 255, 120, 255} : RED);
        } else {
            drawTextF(font, "游戏菜单", mx + mw / 2 - 40, my + 20, 22, WHITE);
        }
        // 重开当前局：战役回当前任务，遭遇战重随机一张
        auto restart = [&]() {
            if (campaignMission >= 0) newCampaignGame(campaignMission);
            else newGame((uint64_t)time(nullptr));
            showMenu = false;
        };
        if (uiButton({(float)mx + 60, (float)my + 80, 200, 32}, gameOver ? "再来一局" : "继续游戏", true)) {
            if (gameOver) restart();
            else showMenu = false;
        }
        if (uiButton({(float)mx + 60, (float)my + 122, 200, 32}, "重新开始", true)) restart();
        if (uiButton({(float)mx + 60, (float)my + 164, 200, 32}, "返回主菜单", true)) {
            phase = Phase::MainMenu;
            showMenu = false;
        }
        if (uiButton({(float)mx + 60, (float)my + 206, 200, 32}, "退出游戏", true)) {
            CloseWindow();
            exit(0);
        }
    }
}

void Game::updateMinimap() {
    // 定时重绘小地图纹理（独立渲染通道，禁止嵌套）
    if (--minimapTimer > 0) return;
    minimapTimer = 6;
    BeginTextureMode(minimap);
    ClearBackground(BLACK);
    int w = world.map.w, h = world.map.h;
    float sc = 256.0f / std::max(w, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            FogState fs = world.map.fogAt(localPlayer, x, y);
            if (fs == FOG_UNSEEN) continue;
            const Cell& c = world.map.at(x, y);
            Color col;
            switch (c.terrain) {
                case Terrain::Water: col = Color{30, 60, 140, 255}; break;
                case Terrain::Rough: col = Color{110, 96, 70, 255}; break;
                case Terrain::Ore:   col = Color{200, 160, 40, 255}; break;
                case Terrain::Gems:  col = Color{60, 210, 110, 255}; break;
                default:             col = Color{70, 105, 55, 255}; break;
            }
            if (fs == FOG_SEEN) { col.r /= 2; col.g /= 2; col.b /= 2; }
            DrawRectangle((int)(x * sc), (int)(y * sc), (int)sc + 1, (int)sc + 1, col);
        }
    // 实体
    for (const World::Ent& e : world.ents) {
        if (!e.alive || e.player < 0) continue;
        FogState fs = world.map.fogAt(localPlayer, (int)e.x, (int)e.y);
        if (e.player != localPlayer && fs != FOG_VISIBLE) continue;
        Color col = HOUSE_COLORS[world.players[e.player].colorId];
        if (e.isBuilding) DrawRectangle((int)(e.x * sc) - 1, (int)(e.y * sc) - 1, 4, 4, col);
        else DrawRectangle((int)(e.x * sc), (int)(e.y * sc), 2, 2, col);
    }
    EndTextureMode();
}

void Game::drawMinimap() {
    int sbX = SCREEN_W - sidebarW;
    int mmSize = 178;
    int mmX = sbX + 6, mmY = 64;
    // 绘制
    DrawTextureRec(minimap.texture, {0, 0, (float)minimap.texture.width, -(float)minimap.texture.height},
                   {(float)mmX, (float)mmY}, WHITE);
    DrawRectangleLinesEx({(float)mmX, (float)mmY, (float)mmSize * 256.0f / 256, (float)mmSize}, 1, Color{90, 94, 102, 255});
    // 摄像机视野框
    int w = world.map.w, h = world.map.h;
    float sc = mmSize / (float)std::max(w, h);
    int viewW = SCREEN_W - sidebarW;
    int t0x, t0y, t1x, t1y;
    screenToTile(camX, camY, t0x, t0y);
    screenToTile(camX + viewW, camY + SCREEN_H, t1x, t1y);
    // 等距视野近似为四边形 → 用包围盒
    int t2x, t2y, t3x, t3y;
    screenToTile(camX + viewW, camY, t2x, t2y);
    screenToTile(camX, camY + SCREEN_H, t3x, t3y);
    int minX = std::min({t0x, t1x, t2x, t3x}), maxX = std::max({t0x, t1x, t2x, t3x});
    int minY = std::min({t0y, t1y, t2y, t3y}), maxY = std::max({t0y, t1y, t2y, t3y});
    DrawRectangleLines(mmX + (int)(minX * sc), mmY + (int)(minY * sc),
                       (int)((maxX - minX) * sc), (int)((maxY - minY) * sc), WHITE);
    // 小地图点击跳转
    if (CheckCollisionPointRec(mousePos(), {(float)mmX, (float)mmY, (float)mmSize, (float)mmSize})) {
        if (mDown(MOUSE_LEFT_BUTTON)) {
            float tx = (mousePos().x - mmX) / sc;
            float ty = (mousePos().y - mmY) / sc;
            int px, py;
            tileToScreen((int)tx, (int)ty, px, py);
            camX = (float)px - viewW / 2.0f;
            camY = (float)py - SCREEN_H / 2.0f;
        }
    }
}
