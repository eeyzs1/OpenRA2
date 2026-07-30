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

// ===================== RA2 风格金属 GUI 素材 =====================
// 调色：RA2 原作深棕铁灰金属 + 暖金色描边（偏暖色调，非蓝灰）
static const Color GUI_GOLD{196, 162, 74, 255};
static const Color GUI_GOLD_HI{255, 216, 120, 255};
static const Color GUI_EDGE_HI{100, 92, 78, 255};   // 暖灰高光
static const Color GUI_EDGE_LO{4, 4, 6, 255};

// 拉丝金属纹理（懒生成一次，96x96 平铺）—— RA2 风格：深棕铁灰
static Texture2D guiMetalTex() {
    static Texture2D t{};
    if (t.id == 0) {
        PixBuf p(96, 96);
        auto hsh = [](int x, int y) {
            uint32_t v = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u);
            v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15;
            return (float)(v % 1024) / 1024.0f;
        };
        for (int y = 0; y < 96; y++)
            for (int x = 0; x < 96; x++) {
                // 水平拉丝：行长条噪声 + 细颗粒
                float streak = hsh(x / 9, y) * 0.62f + hsh(x / 3, y + 40) * 0.38f;
                float v = 0.88f + (streak - 0.5f) * 0.34f + (hsh(x, y) - 0.5f) * 0.10f;
                v *= 1.10f - 0.20f * (y / 95.0f); // 上亮下暗
                // RA2 暖色金属：R > G > B（偏棕色调，非蓝灰）
                p.set(x, y, Color{(uint8_t)clampi((int)(38 * v), 0, 255),
                                  (uint8_t)clampi((int)(34 * v), 0, 255),
                                  (uint8_t)clampi((int)(28 * v), 0, 255), 255});
            }
        t = p.toTexture();
    }
    return t;
}

// 平铺金属底（剪刀裁剪到目标矩形）
static void guiMetalFill(int x, int y, int w, int h) {
    Texture2D t = guiMetalTex();
    BeginScissorMode(x, y, w, h);
    for (int ty = y; ty < y + h; ty += 96)
        for (int tx = x; tx < x + w; tx += 96)
            DrawTexture(t, tx, ty, WHITE);
    EndScissorMode();
}

// 铆钉（RA2 面板角饰）
static void guiRivet(int x, int y) {
    DrawCircle(x, y, 2.4f, Color{10, 11, 14, 255});
    DrawCircle(x, y, 1.5f, Color{106, 114, 128, 255});
    DrawPixel(x - 1, y - 1, Color{172, 182, 198, 255});
}

// 棱台斜面：sunken=false 凸起（按钮/面板），true 凹陷（信息槽）
static void guiBevel(Rectangle r, bool sunken) {
    Color hi = sunken ? GUI_EDGE_LO : GUI_EDGE_HI;
    Color lo = sunken ? GUI_EDGE_HI : GUI_EDGE_LO;
    int x = (int)r.x, y = (int)r.y, w = (int)r.width, h = (int)r.height;
    DrawLine(x, y, x + w - 1, y, hi);
    DrawLine(x, y, x, y + h - 1, hi);
    DrawLine(x, y + h - 1, x + w - 1, y + h - 1, lo);
    DrawLine(x + w - 1, y, x + w - 1, y + h - 1, lo);
}

// 凹陷信息槽（生产格/资金牌/超武格）
static void guiSlot(Rectangle r) {
    DrawRectangleRec(r, Color{15, 16, 20, 255});
    guiBevel(r, true);
}

// 金属面板：拉丝底 + 外凸棱 + 内金线 + 四角铆钉
static void guiPanel(int x, int y, int w, int h) {
    guiMetalFill(x, y, w, h);
    guiBevel({(float)x, (float)y, (float)w, (float)h}, false);
    DrawRectangleLinesEx({(float)x + 3, (float)y + 3, (float)w - 6, (float)h - 6}, 1, GUI_GOLD);
    guiRivet(x + 7, y + 7); guiRivet(x + w - 7, y + 7);
    guiRivet(x + 7, y + h - 7); guiRivet(x + w - 7, y + h - 7);
}

// 带黑色投影的文字（RA2 式）
static void drawTextS(Font f, const char* s, int x, int y, int size, Color c) {
    DrawTextEx(f, s, {(float)x + 1, (float)y + 1}, (float)size, 1, Color{0, 0, 0, 210});
    DrawTextEx(f, s, {(float)x, (float)y}, (float)size, 1, c);
}

// RA2 式金属按钮：竖向渐变面 + 棱台斜面 + 金框（悬停/激活加亮），按下斜面反相
bool Game::uiButton(Rectangle r, const char* text, bool enabled, bool active) {
    Vector2 m = mousePos();
    bool hover = CheckCollisionPointRec(m, r) && enabled;
    bool press = hover && mDown(MOUSE_LEFT_BUTTON);
    // RA2 暖色调：深棕铁灰 → 悬停加亮为暖棕金
    Color top = enabled ? (hover ? Color{72, 58, 40, 255} : Color{44, 40, 34, 255}) : Color{26, 24, 22, 255};
    Color bot = enabled ? (hover ? Color{44, 34, 24, 255} : Color{26, 24, 20, 255}) : Color{18, 16, 14, 255};
    if (active) { top = Color{68, 52, 30, 255}; bot = Color{38, 28, 16, 255}; }
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, top, bot);
    guiBevel(r, press);
    Color frame = !enabled ? Color{56, 52, 44, 255}
                : active ? GUI_GOLD_HI : (hover ? GUI_GOLD : Color{84, 74, 50, 255});
    DrawRectangleLinesEx(r, 1, frame);
    if (text && text[0]) {
        int tw = (int)MeasureTextEx(font, text, 14, 1).x;
        drawTextS(font, text, (int)(r.x + r.width / 2 - tw / 2), (int)(r.y + r.height / 2 - 7), 14,
                  enabled ? (active || hover ? Color{255, 228, 150, 255} : Color{226, 212, 170, 255})
                          : Color{110, 104, 96, 255});
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
            BldType::CloningVat, BldType::ServiceDepot, BldType::GapGenerator, BldType::SpySat, BldType::PsychicSensor,
            BldType::NukeSilo, BldType::WeatherDevice, BldType::IronCurtain, BldType::ChronoSphere,
        };
        for (BldType t : mainB)
            if (bldDef(t).factionMask & (1 << (int)f)) v.push_back(t);
    } else {
        // 防御建筑
        static const BldType defB[] = {
            BldType::Pillbox, BldType::SentryGun, BldType::FlakCannon,
            BldType::PrismTower, BldType::TeslaCoil, BldType::GrandCannon,
            BldType::PatriotMissile, BldType::Wall, BldType::BattleBunker, BldType::TankBunker,
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
        // 偷科技单位：未解锁前不显示在侧边栏（RA2 原作：渗透敌高科后才出现）
        int stBit = stolenTechBit((UnitType)i);
        if (stBit && !(world.players[localPlayer].stolenTech & stBit)) continue;
        bool nav = u.isNaval() || u.isAmphib();
        if (uiTab == 2 && u.isInfantry()) v.push_back((UnitType)i);          // 步兵
        if (uiTab == 3 && !u.isInfantry() && !nav) v.push_back((UnitType)i); // 车辆/空军
        if (uiTab == 4 && nav) v.push_back((UnitType)i);                     // 海军
    }
    return v;
}

void Game::drawHUD() {
    int sbX = SCREEN_W - sidebarW;
    // 侧边栏：拉丝金属 + 左侧金色分隔线（RA2 标志布局）
    guiMetalFill(sbX, 0, sidebarW, SCREEN_H);
    DrawLine(sbX, 0, sbX, SCREEN_H, GUI_EDGE_LO);
    DrawLine(sbX + 1, 0, sbX + 1, SCREEN_H, Color{150, 130, 80, 255});
    DrawLine(sbX + 2, 0, sbX + 2, SCREEN_H, Color{60, 52, 34, 255});

    Player& me = world.players[localPlayer];
    // 阵营色装饰条（RA2 原作侧边栏有玩家色镶边）
    Color facCol = HOUSE_COLORS[me.colorId];
    DrawRectangle(sbX + 3, 0, 2, SCREEN_H, facCol);

    // 资金牌：凹陷槽 + 金条堆图标 + 金色数字（RA2 顶部资金显示）
    {
        Rectangle mp{(float)sbX + 8, 4, (float)sidebarW - 14, 30};
        guiSlot(mp);
        DrawRectangleLinesEx(mp, 1, Color{74, 64, 42, 255});
        DrawRectangle(sbX + 14, 22, 15, 5, Color{140, 112, 44, 255}); // 金条堆
        DrawRectangle(sbX + 17, 16, 15, 5, GUI_GOLD);
        DrawRectangle(sbX + 20, 10, 15, 5, GUI_GOLD_HI);
        drawTextS(font, TextFormat("%d", me.money), sbX + 50, 9, 20,
                  me.money > 0 ? Color{255, 216, 90, 255} : Color{255, 92, 72, 255});
        // 游戏计时器（RA2 原作侧边栏顶部时间显示）
        int secs = (int)(world.tick / LOGIC_FPS);
        drawTextS(font, TextFormat("%d:%02d", secs / 60, secs % 60),
                  sbX + sidebarW - 42, 12, 14, Color{180, 180, 190, 255});
    }

    // 电力表：左侧竖条（RA2 标志仪表）——凹陷金属框 + 绿→黄→红渐变 + 分段刻度
    int pwrX = sbX + 8, pwrY = 42, pwrH = 186;
    DrawRectangle(pwrX - 2, pwrY - 2, 18, pwrH + 4, Color{15, 16, 20, 255});
    guiBevel({(float)pwrX - 2, (float)pwrY - 2, 18, (float)pwrH + 4}, true);
    DrawRectangle(pwrX, pwrY, 14, pwrH, Color{26, 26, 30, 255});
    float ratio = me.powerMade > 0 ? std::min(1.0f, (float)me.powerUsed / std::max(1, me.powerMade)) : 1.0f;
    int usedH = (int)(pwrH * ratio);
    if (me.lowPower() && (world.tick / 10) % 2) {
        DrawRectangle(pwrX + 1, pwrY + pwrH - usedH, 12, usedH, Color{220, 50, 36, 255}); // 低电红闪
    } else if (usedH > 0) {
        // 顶部红 → 中部黄 → 底部绿 三段渐变
        int fy = pwrY + pwrH - usedH;
        int half = usedH / 2;
        if (half > 0) DrawRectangleGradientV(pwrX + 1, fy, 12, half, Color{230, 70, 40, 255}, Color{230, 200, 60, 255});
        if (usedH - half > 0) DrawRectangleGradientV(pwrX + 1, fy + half, 12, usedH - half, Color{230, 200, 60, 255}, Color{70, 210, 80, 255});
    }
    for (int ty = pwrY + pwrH / 10; ty < pwrY + pwrH; ty += pwrH / 10)
        DrawLine(pwrX + 1, ty, pwrX + 12, ty, Color{0, 0, 0, 130});
    drawTextS(font, TR(S::Power), pwrX - 4, pwrY + pwrH + 6, 13, Color{210, 200, 170, 255});
    if (me.lowPower()) drawTextS(font, TR(S::LowPower), sbX + 34, pwrY + pwrH - 22, 13, Color{255, 90, 70, 255});

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
            // 凹陷槽 + 目标选择红框/就绪金色脉冲
            guiSlot(r);
            DrawRectangleLinesEx(r, 1, targeting ? Color{255, 120, 90, 255}
                                 : (ready && hasBld ? (((world.tick / 12) % 2) ? GUI_GOLD_HI : GUI_GOLD)
                                                    : Color{56, 60, 68, 255}));
            // 名称
            drawTextF(font, swName(t), (int)r.x + 4, (int)r.y + 4, 13,
                      hasBld ? (ready ? Color{180, 255, 150, 255} : WHITE) : Color{110, 110, 110, 255});
            if (!hasBld) {
                drawTextF(font, TR(S::NeedBld), (int)r.x + 16, (int)r.y + 40, 12, Color{120, 110, 100, 255});
            } else if (ready) {
                // 就绪：闪烁提示
                if ((world.tick / 15) % 2) drawTextF(font, TR(S::Ready), (int)r.x + 26, (int)r.y + 30, 16, Color{120, 255, 120, 255});
                drawTextF(font, TR(S::ClickTarget), (int)r.x + 6, (int)r.y + 54, 11, Color{200, 220, 180, 255});
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
                targetingParadrop = false;
                g_sfx.play(Sfx::Click, 0.6f);
                if (targetingSW != SWType::COUNT) message(TR(S::MsgSelectTargetSW));
            }
        }
        // ---- 伞兵支援按钮（RA2 原作：美国空指部/科技机场）----
        if (world.hasParadropSource(localPlayer)) {
            Rectangle r{(float)sbX + 6 + bi * 90, (float)swY, 86, 74};
            bi++;
            bool ready = me.paradropReady;
            guiSlot(r);
            DrawRectangleLinesEx(r, 1, targetingParadrop ? Color{255, 120, 90, 255}
                                 : (ready ? (((world.tick / 12) % 2) ? GUI_GOLD_HI : GUI_GOLD)
                                          : Color{56, 60, 68, 255}));
            drawTextF(font, TR(S::Paradrop), (int)r.x + 4, (int)r.y + 4, 13,
                      ready ? Color{180, 255, 150, 255} : WHITE);
            if (ready) {
                if ((world.tick / 15) % 2) drawTextF(font, TR(S::Ready), (int)r.x + 26, (int)r.y + 30, 16, Color{120, 255, 120, 255});
                drawTextF(font, TR(S::ClickTarget), (int)r.x + 6, (int)r.y + 54, 11, Color{200, 220, 180, 255});
            } else {
                float frac = (float)me.paradropCharge / World::PARADROP_TIME;
                DrawRectangle((int)r.x + 6, (int)r.y + 38, 74, 8, Color{40, 40, 44, 255});
                DrawRectangle((int)r.x + 7, (int)r.y + 39, (int)(72 * frac), 6, Color{220, 170, 60, 255});
                int secs = (World::PARADROP_TIME - me.paradropCharge) / LOGIC_FPS;
                drawTextF(font, TextFormat("%d:%02d", secs / 60, secs % 60), (int)r.x + 26, (int)r.y + 54, 13,
                          Color{200, 190, 150, 255});
            }
            if (ready && CheckCollisionPointRec(mousePos(), r) && mPressed(MOUSE_LEFT_BUTTON)) {
                targetingParadrop = !targetingParadrop;
                targetingSW = SWType::COUNT;
                g_sfx.play(Sfx::Click, 0.6f);
                if (targetingParadrop) message(TR(S::MsgParadropTarget));
            }
        }
    }

    // 选项卡（RA2 式：金属凸起小标签 + 文字）
    static const S tabIds[] = {S::TabBld, S::TabDef, S::TabInf, S::TabVeh, S::TabNavy};
    for (int i = 0; i < 5; i++) {
        Rectangle tr{(float)sbX + 6 + i * 37, 330, 35, 22};
        if (uiButton(tr, TR(tabIds[i]), true, uiTab == i)) uiTab = i;
    }
    // 阵营色装饰条在选项卡下方
    DrawLine(sbX + 6, 353, sbX + sidebarW - 6, 353, facCol);

    // 生产图标网格（行数封顶，避免画出屏幕；底部留给维修/出售/菜单按钮）
    int gx = sbX + 6, gy = 358, gw = 86, gh = 66, cols = 2;
    int maxRows = (SCREEN_H - 56 - gy) / (gh + 4);
    int idx = 0;
    // 悬停提示缓存（网格绘制完成后统一绘制，保证浮于所有槽位之上）
    std::string tipName, tipSub, tipReason;
    bool tipSet = false;
    auto drawItem = [&](bool isUnit, int typeIdx, const Sprite& icon, const char* name, int cost,
                        bool canBuild, ProdItem& prod, int queuedN, const char* reason) {
        int ix = gx + (idx % cols) * (gw + 4);
        int iy = gy + (idx / cols) * (gh + 4);
        idx++;
        if ((idx - 1) / cols >= maxRows) return; // 超出一页：截断
        Rectangle r{(float)ix, (float)iy, (float)gw, (float)gh};
        bool activeThis = prod.active && prod.typeIdx == typeIdx && prod.isUnit == isUnit;
        bool readyThis = activeThis && prod.ready;
        int time = isUnit ? unitDef((UnitType)typeIdx).buildTime : bldDef((BldType)typeIdx).buildTime;
        // 槽位：凹陷金属槽 + 就绪金色脉冲（RA2 式）
        guiSlot(r);
        DrawRectangleLinesEx(r, 1, readyThis ? (((world.tick / 12) % 2) ? GUI_GOLD_HI : GUI_GOLD)
                             : (activeThis ? Color{196, 180, 110, 255} : Color{56, 60, 68, 255}));
        DrawTexture(icon.tex, ix + (gw - icon.tex.width) / 2, iy + 2, canBuild ? WHITE : Color{90, 90, 90, 255});
        // 悬停高亮 + 记录提示内容
        bool hov = CheckCollisionPointRec(mousePos(), r);
        if (hov) {
            DrawRectangleLinesEx({r.x + 1, r.y + 1, r.width - 2, r.height - 2}, 1, Color{210, 190, 130, 130});
            if (!tipSet) {
                tipSet = true;
                tipName = name ? name : "?";
                tipSub = TextFormat(TR(S::TipCostTimeFmt), cost, time / LOGIC_FPS);
                if (reason && reason[0]) tipReason = reason;
            }
        }
        // 进度遮罩
        if (activeThis && !prod.ready) {
            float frac = (float)prod.progress / time;
            DrawRectangle(ix, iy + (int)(gh * (1 - frac)), gw, (int)(gh * frac), Color{0, 0, 0, 130});
            drawTextF(font, TextFormat("%d%%", (int)(frac * 100)), ix + 30, iy + 26, 14, WHITE);
        }
        if (readyThis) drawTextF(font, TR(S::Ready), ix + 27, iy + 24, 15, GREEN);
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
        if (hov) {
            if (mPressed(MOUSE_LEFT_BUTTON)) {
                // 就绪项优先：canBuild 为"能否开始新生产"（含队列空闲），不能阻塞就绪建筑的放置
                if (readyThis) {
                    // 建筑就绪 → 进入放置模式
                    me.placingBld = (BldType)typeIdx;
                    placing = true;
                    message(TR(S::MsgPlaceBld));
                } else if (!canBuild) { message(TR(S::MsgCannotBuild)); }
                else if (isUnit || !activeThis) {
                    // 单位允许重复点击排队（RA2 原作）；联机下命令延迟执行，本地预检队列容量
                    bool ok = isUnit ? (world.unitQueuedCount(localPlayer, unitDef((UnitType)typeIdx).prodCat()) < 8)
                                     : !me.bldProd.active;
                    if (!ok) { message(TR(S::MsgQueueBusy)); }
                    else {
                        World::Cmd c;
                        c.type = isUnit ? World::Cmd::StartUnitProd : World::Cmd::StartBldProd;
                        c.a = typeIdx;
                        issueCmd(c);
                    }
                }
            }
            if (mPressed(MOUSE_RIGHT_BUTTON)) {
                if (isUnit && totalN > 0) {
                    World::Cmd c; c.type = World::Cmd::CancelUnitProd; c.a = typeIdx;
                    issueCmd(c);
                    message(TR(S::MsgCanceledOne));
                } else if (!isUnit && activeThis) {
                    World::Cmd c; c.type = World::Cmd::CancelBldProd;
                    issueCmd(c);
                    message(TR(S::MsgCanceledProd));
                }
            }
        }
    };

    if (uiTab <= 1) {
        for (BldType t : tabBuildings()) {
            const BldDef& d = bldDef(t);
            bool hasCY = world.hasBld(localPlayer, BldType::ConYard);
            bool preOk = world.prereqMet(localPlayer, d);
            bool can = hasCY && preOk && me.money >= d.cost && !me.bldProd.active;
            // 悬停提示的缺失原因（优先级：建造厂 > 前置/国家 > 资金；队列忙为瞬态不提示）
            std::string reason;
            if (!hasCY) reason = TextFormat(TR(S::TipRequireFmt), bldName(BldType::ConYard));
            else if (!preOk) {
                if (d.prereq != BldType::COUNT && !world.hasBld(localPlayer, d.prereq))
                    reason = TextFormat(TR(S::TipRequireFmt), bldName(d.prereq));
                else if (d.countryReq != Country::None)
                    reason = TextFormat(TR(S::TipRequireFmt), countryName(d.countryReq));
            } else if (me.money < d.cost) reason = TR(S::TipNoMoney);
            drawItem(false, (int)t, g_sprites.iconBld(t, me.colorId), bldName(t), d.cost, can, me.bldProd, 0,
                     reason.c_str());
        }
    } else {
        for (UnitType t : tabUnits()) {
            const UnitDef& u = unitDef(t);
            int cat = u.prodCat();
            int qn = 0;
            for (int q : me.unitQueue[cat])
                if (q == (int)t) qn++;
            bool preOk = world.unitPrereqMet(localPlayer, u);
            bool facOk = world.hasFactoryFor(localPlayer, u);
            bool can = preOk && facOk && me.money >= u.cost;
            // 悬停提示的缺失原因（优先级：前置/国家 > 生产工厂 > 资金）
            std::string reason;
            if (!preOk) {
                if (u.prereq != BldType::COUNT && !world.hasBld(localPlayer, u.prereq))
                    reason = TextFormat(TR(S::TipRequireFmt), bldName(u.prereq));
                else if (u.countryReq != Country::None && me.country != u.countryReq
                         && me.secretLabUnlock != (int)u.countryReq)
                    reason = TextFormat(TR(S::TipRequireFmt), countryName(u.countryReq));
            } else if (!facOk) {
                for (int b = 0; b < (int)BldType::COUNT && reason.empty(); b++)
                    if (isFactoryFor((BldType)b, u))
                        reason = TextFormat(TR(S::TipRequireFmt), bldName((BldType)b));
            } else if (me.money < u.cost) reason = TR(S::TipNoMoney);
            drawItem(true, (int)t, g_sprites.iconUnit(t, me.colorId), unitName(t), u.cost, can, me.unitProd[cat], qn,
                     reason.c_str());
        }
    }

    // ---- 侧边栏底部：维修 / 出售 / 菜单（RA2 标志性按钮）----
    {
        int bw2 = 56, bh2 = 40, by2 = SCREEN_H - bh2 - 8;
        Rectangle repR{(float)sbX + 6, (float)by2, (float)bw2, (float)bh2};
        Rectangle selR{(float)sbX + 6 + bw2 + 5, (float)by2, (float)bw2, (float)bh2};
        Rectangle mnuR{(float)sbX + 6 + 2 * (bw2 + 5), (float)by2, (float)bw2, (float)bh2};
        if (uiButton(repR, TR(S::Repair), true, sideMode == 1)) {
            sideMode = sideMode == 1 ? 0 : 1;
            if (sideMode == 1) message(TR(S::MsgRepairMode));
        }
        if (uiButton(selR, TR(S::Sell), true, sideMode == 2)) {
            sideMode = sideMode == 2 ? 0 : 2;
            if (sideMode == 2) message(TR(S::MsgSellMode));
        }
        if (uiButton(mnuR, TR(S::Menu), true)) showMenu = true;
    }

    // 生产图标悬停提示框（RA2 式：黑底金框，名称 + 造价耗时 + 缺失条件；浮于侧边栏所有控件之上）
    if (tipSet) {
        Vector2 m = mousePos();
        int w = 0;
        w = std::max(w, (int)MeasureTextEx(font, tipName.c_str(), 14, 1).x);
        w = std::max(w, (int)MeasureTextEx(font, tipSub.c_str(), 12, 1).x);
        if (!tipReason.empty()) w = std::max(w, (int)MeasureTextEx(font, tipReason.c_str(), 12, 1).x);
        w += 16;
        int h = tipReason.empty() ? 44 : 60;
        int tx = (int)m.x - w - 12; // 侧边栏在最右，提示框向左弹出
        if (tx < 4) tx = 4;
        int ty = clampi((int)m.y - h / 2, 4, SCREEN_H - h - 4);
        Rectangle tr2{(float)tx, (float)ty, (float)w, (float)h};
        guiSlot(tr2);
        DrawRectangleLinesEx(tr2, 1, GUI_GOLD);
        drawTextS(font, tipName.c_str(), tx + 8, ty + 5, 14, Color{255, 226, 140, 255});
        drawTextS(font, tipSub.c_str(), tx + 8, ty + 24, 12, Color{200, 200, 210, 255});
        if (!tipReason.empty())
            drawTextS(font, tipReason.c_str(), tx + 8, ty + 40, 12, Color{255, 110, 90, 255});
    }

    // ---- 命令栏（RA2 标志性左下角命令按钮：选中单位/建筑时显示对应操作）----
    {
        int cbW = 44, cbH = 34, cbGap = 4;
        int cbY = SCREEN_H - cbH - 48;
        int cbX = 266; // 紧跟选中信息面板右侧
        auto cmdBtn = [&](int idx, const char* label, const char* key, bool enabled, auto&& action) {
            int bx = cbX + idx * (cbW + cbGap);
            Rectangle r{(float)bx, (float)cbY, (float)cbW, (float)cbH};
            bool hover = CheckCollisionPointRec(mousePos(), r) && enabled;
            // 槽位底
            Color top = enabled ? (hover ? Color{72, 52, 36, 255} : Color{44, 44, 50, 255}) : Color{24, 24, 28, 255};
            Color bot = enabled ? (hover ? Color{40, 28, 20, 255} : Color{26, 26, 30, 255}) : Color{18, 18, 22, 255};
            DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, top, bot);
            guiBevel(r, hover && mDown(MOUSE_LEFT_BUTTON));
            DrawRectangleLinesEx(r, 1, enabled ? (hover ? GUI_GOLD : Color{92, 84, 62, 255}) : Color{50, 50, 54, 255});
            // 标签
            int tw = (int)MeasureTextEx(font, label, 12, 1).x;
            drawTextS(font, label, bx + (cbW - tw) / 2, cbY + 4, 12,
                      enabled ? (hover ? Color{255, 228, 150, 255} : Color{216, 202, 160, 255})
                              : Color{90, 90, 90, 255});
            // 快捷键提示
            if (key && key[0]) {
                int kw = (int)MeasureTextEx(font, key, 9, 1).x;
                drawTextF(font, key, bx + (cbW - kw) / 2, cbY + 20, 9,
                          enabled ? Color{160, 150, 110, 255} : Color{70, 70, 70, 255});
            }
            // 点击执行
            if (hover && mPressed(MOUSE_LEFT_BUTTON)) { action(); g_sfx.play(Sfx::Click, 0.5f); }
        };
        bool hasSel = !sel.empty();
        bool hasBldSel = world.valid(selBuilding) && world.ents[selBuilding].isBuilding;
        // 选中单位：停止/部署/散布/警戒/同类/卸载
        if (hasSel) {
            cmdBtn(0, TR(S::KaStop), keyName(keyBind[KA_Stop]), true, [&]{
                World::Cmd c; c.type = World::Cmd::Stop; c.ids = sel; issueCmd(c);
            });
            cmdBtn(1, TR(S::KaDeploy), keyName(keyBind[KA_Deploy]), true, [&]{
                for (EID id : sel)
                    if (world.valid(id) && world.ents[id].utype == UnitType::MCV) {
                        World::Cmd c; c.type = World::Cmd::Deploy; c.ids.push_back(id); issueCmd(c);
                        sel.erase(std::remove(sel.begin(), sel.end(), id), sel.end());
                        message(TR(S::MsgDeployed));
                    }
                bool anyDep = false;
                for (EID id : sel)
                    if (world.valid(id) && !world.ents[id].isBuilding
                        && (world.ents[id].utype == UnitType::Desolator || world.ents[id].utype == UnitType::GuardianGI
                            || world.ents[id].utype == UnitType::GI || world.ents[id].utype == UnitType::SiegeChopper))
                        anyDep = true;
                if (anyDep) { World::Cmd c; c.type = World::Cmd::RadDeploy; c.ids = sel; issueCmd(c); message(TR(S::MsgDeployToggled)); }
            });
            cmdBtn(2, TR(S::KaScatter), keyName(keyBind[KA_Scatter]), true, [&]{
                World::Cmd c; c.type = World::Cmd::Scatter; c.ids = sel; issueCmd(c); message(TR(S::MsgScatter));
            });
            cmdBtn(3, TR(S::KaGuard), keyName(keyBind[KA_Guard]), true, [&]{
                World::Cmd c; c.type = World::Cmd::Guard; c.ids = sel; issueCmd(c); message(TR(S::MsgGuard));
            });
            cmdBtn(4, TR(S::KaSameType), keyName(keyBind[KA_SameType]), true, [&]{
                bool types[(int)UnitType::COUNT] = {};
                for (EID id : sel)
                    if (world.valid(id) && !world.ents[id].isBuilding) types[(int)world.ents[id].utype] = true;
                sel.clear();
                for (size_t i = 0; i < world.ents.size(); i++) {
                    const World::Ent& e = world.ents[i];
                    if (e.alive && !e.isBuilding && e.player == localPlayer && types[(int)e.utype])
                        sel.push_back((int)i);
                }
                message(TR(S::MsgSelSameType));
            });
            // 卸载：仅运输单位/驻军建筑可用
            bool canUnload = false;
            for (EID id : sel)
                if (world.valid(id) && unitDef(world.ents[id].utype).cargoCap > 0 && !world.ents[id].cargo.empty())
                    canUnload = true;
            cmdBtn(5, TR(S::KaUnload), keyName(keyBind[KA_Unload]), canUnload, [&]{
                World::Cmd c; c.type = World::Cmd::Unload; c.ids = sel; issueCmd(c);
            });
        }
        // 选中建筑：集结点 / 撤出驻军
        if (hasBldSel) {
            const World::Ent& b = world.ents[selBuilding];
            bool isFac = b.btype == BldType::WarFactory || b.btype == BldType::NavalYard
                         || b.btype == BldType::Barracks || b.btype == BldType::AirForceCmd;
            cmdBtn(0, TR(S::KaRally), keyName(keyBind[KA_Rally]), isFac && b.player == localPlayer, [&]{
                float wx, wy;
                screenToWorld((int)mousePos().x, (int)mousePos().y, wx, wy);
                int tx, ty;
                screenToTile(wx, wy, tx, ty);
                World::Cmd c; c.type = World::Cmd::SetRally; c.ids.push_back(selBuilding);
                c.x = (float)tx; c.y = (float)ty;
                issueCmd(c);
                message(TR(S::MsgRallySet));
            });
            cmdBtn(1, TR(S::KaUnload), keyName(keyBind[KA_Unload]), !b.garrison.empty(), [&]{
                World::Cmd c; c.type = World::Cmd::Ungarrison; c.ids.push_back(selBuilding); issueCmd(c);
                message(TR(S::MsgUngarrison));
            });
        }
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
        std::string obj = missionName(campaignMission);
        obj += " · ";
        if (!objectiveText.empty()) {
            obj += objectiveText; // P7 触发器脚本设定的目标文本（优先于默认波次/歼灭提示）
        } else if (md.objective == 1) {
            int remain = (md.objectiveTick - (int)world.tick) / LOGIC_FPS;
            if (remain < 0) remain = 0;
            obj += TextFormat(TR(S::ObjHoldFmt), remain / 60, remain % 60);
        } else if (nextWave < md.waves.size()) {
            obj += TextFormat(TR(S::ObjWaveFmt), (int)nextWave + 1, (int)md.waves.size());
        } else {
            obj += TR(S::ObjElimAll);
        }
        DrawRectangle(6, 30, (int)MeasureTextEx(font, obj.c_str(), 14, 1).x + 12, 22, Color{0, 0, 0, 140});
        drawTextF(font, obj.c_str(), 12, 34, 14, Color{230, 200, 130, 255});
    }

    // 选中信息面板（RA2 式左下角金属面板：单选=图标+名称+军衔+血条/载员，多选=构成统计）
    if (!sel.empty()) {
        int px = 8, pw = 254;
        if (sel.size() == 1 && world.valid(sel[0]) && !world.ents[sel[0]].isBuilding) {
            const World::Ent& e = world.ents[sel[0]];
            const UnitDef& ud = unitDef(e.utype);
            int ph = 52, py = SCREEN_H - ph - 48;
            guiPanel(px, py, pw, ph);
            // 单位图标（等比缩放进 40x40 凹槽）
            const Sprite& ic = g_sprites.iconUnit(e.utype, world.players[localPlayer].colorId);
            if (ic.valid()) {
                guiSlot({(float)px + 6, (float)py + 6, 40, 40});
                float sc = std::min(38.0f / ic.tex.width, 38.0f / ic.tex.height);
                float iw = ic.tex.width * sc, ih = ic.tex.height * sc;
                DrawTexturePro(ic.tex, {0, 0, (float)ic.tex.width, (float)ic.tex.height},
                               {(float)px + 7 + (38 - iw) / 2, (float)py + 7 + (38 - ih) / 2, iw, ih},
                               {0, 0}, 0, WHITE);
            }
            // 名称 + 军衔章（RA2：老兵 1 道 V 形，精英 3 道金色）
            const char* uname = unitName(e.utype);
            drawTextS(font, uname, px + 54, py + 6, 15, Color{240, 230, 200, 255});
            if (e.vetRank > 0) {
                bool elite = e.vetRank >= 2;
                Color rc = elite ? Color{255, 216, 90, 255} : Color{200, 220, 255, 255};
                int nx = px + 54 + (int)MeasureTextEx(font, uname, 15, 1).x + 8;
                for (int i = 0; i < (elite ? 3 : 1); i++)
                    DrawTriangle({(float)nx + i * 8, (float)py + 8}, {(float)nx + 6 + i * 8, (float)py + 8},
                                 {(float)nx + 3 + i * 8, (float)py + 15}, rc);
                drawTextS(font, elite ? TR(S::RankElite) : TR(S::RankVet), nx + (elite ? 28 : 12), py + 7, 12, rc);
            }
            // 血条 + 数值（军衔 HP 加成：老兵+50% 精英+100%）
            int mhp = (int)(ud.hp * (1.0f + 0.5f * e.vetRank));
            drawHealthBar(px + 54, py + 26, pw - 64, (float)e.hp / std::max(1, mhp), false);
            drawTextS(font, TextFormat(TR(S::HpFmt), e.hp, mhp), px + 54, py + 35, 12, Color{170, 200, 170, 255});
            // 运输载具：载员与卸载提示
            if (ud.cargoCap > 0)
                drawTextS(font, TextFormat(TR(S::CargoNFmt), (int)e.cargo.size(), ud.cargoCap,
                                           keyName(keyBind[KA_Unload])),
                          px + 150, py + 35, 12, Color{140, 200, 230, 255});
        } else {
            // 多选：总数 + 数量最多的前 3 类构成
            int cnt[256] = {};
            int n = 0;
            for (EID id : sel)
                if (world.valid(id) && !world.ents[id].isBuilding) { cnt[(int)world.ents[id].utype]++; n++; }
            int lines = 0;
            int top[3] = {-1, -1, -1};
            for (int i = 0; i < 256; i++)
                if (cnt[i] > 0) {
                    for (int k = 0; k < 3; k++)
                        if (top[k] < 0 || cnt[i] > cnt[top[k]]) {
                            for (int j = 2; j > k; j--) top[j] = top[j - 1];
                            top[k] = i;
                            break;
                        }
                }
            for (int k = 0; k < 3; k++) if (top[k] >= 0) lines++;
            int ph = 26 + lines * 15, py = SCREEN_H - ph - 48;
            guiPanel(px, py, pw, ph);
            drawTextS(font, TextFormat(TR(S::SelNFmt), n), px + 10, py + 6, 14, Color{180, 220, 180, 255});
            for (int k = 0; k < lines; k++)
                drawTextS(font, TextFormat("%s ×%d", unitName((UnitType)top[k]), cnt[top[k]]),
                          px + 10, py + 22 + k * 15, 12, Color{200, 200, 210, 255});
        }
    }
    // 操作提示
    drawTextF(font, TR(S::TipLine), 10, SCREEN_H - 44, 12, Color{130, 130, 140, 255});

    // 暂停/菜单/结算
    if (paused && !gameOver) {
        drawTextF(font, TR(S::Paused), SCREEN_W / 2 - 30, SCREEN_H / 2, 28, WHITE);
    }
    if (showMenu || gameOver) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 160});
        int mw = 320, mh = 426;
        int mx = SCREEN_W / 2 - mw / 2, my = SCREEN_H / 2 - mh / 2;
        guiPanel(mx, my, mw, mh);
        if (gameOver) {
            const char* t = victory ? TR(S::Victory) : TR(S::Defeat);
            drawTextF(font, t, mx + mw / 2 - 40, my + 24, 34, victory ? Color{120, 255, 120, 255} : RED);
        } else {
            drawTextF(font, TR(S::GameMenu), mx + mw / 2 - 40, my + 20, 22, WHITE);
        }
        // 重开当前局：战役回当前任务，遭遇战重随机一张（联机不支持单方重开）
        auto restart = [&]() {
            if (campaignMission >= 0) newCampaignGame(campaignMission);
            else newGame((uint64_t)time(nullptr));
            showMenu = false;
        };
        if (uiButton({(float)mx + 60, (float)my + 72, 200, 32}, gameOver ? TR(S::PlayAgain) : TR(S::Continue),
                     !(gameOver && netGame))) {
            if (gameOver) restart();
            else showMenu = false;
        }
        // 存读档：仅对局中可用（结算画面无意义）；联机禁用（lockstep 无法同步）；按钮标注当前绑定键位
        if (uiButton({(float)mx + 60, (float)my + 114, 200, 32},
                     TextFormat("%s (%s)", TR(S::SaveProgress), keyName(keyBind[KA_QuickSave])), !gameOver && !netGame)) {
            message(saveGameFile(QUICKSAVE_PATH) ? TR(S::MsgSaved) : TR(S::MsgSaveFail));
            showMenu = false;
        }
        if (uiButton({(float)mx + 60, (float)my + 156, 200, 32},
                     TextFormat("%s (%s)", TR(S::LoadProgress), keyName(keyBind[KA_QuickLoad])), !gameOver && !netGame)) {
            message(loadGameFile(QUICKSAVE_PATH) ? TR(S::MsgLoaded) : TR(S::MsgLoadFail));
            showMenu = false;
        }
        // 设置入口：局内菜单跳转设置页（返回时恢复菜单）
        if (uiButton({(float)mx + 60, (float)my + 198, 200, 32}, TR(S::Settings), true)) {
            settingsFromGame = true;
            showMenu = false;
            phase = Phase::Settings;
        }
        if (uiButton({(float)mx + 60, (float)my + 240, 200, 32}, TR(S::Restart), !netGame)) restart();
        if (uiButton({(float)mx + 60, (float)my + 282, 200, 32}, TR(S::BackToMain), true)) {
            if (netGame) netLeave(); // 联机：发 Bye 并复位联机状态
            else phase = Phase::MainMenu;
            showMenu = false;
        }
        if (uiButton({(float)mx + 60, (float)my + 324, 200, 32}, TR(S::ExitGame), true)) {
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
                case Terrain::Bridge: col = Color{160, 110, 60, 255}; break;
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

// RA2 原作：小地图（雷达）需雷达类建筑在线——雷达站/空指部/间谍卫星任一方块在线，
// 且电力充足（低电力时雷达掉线黑屏）。返回是否有可用雷达信号。
bool Game::radarOnline() const {
    const Player& me = world.players[localPlayer];
    if (me.lowPower()) return false;
    return world.hasBld(localPlayer, BldType::Radar)
        || world.hasBld(localPlayer, BldType::AirForceCmd)
        || world.hasBld(localPlayer, BldType::SpySat);
}

void Game::drawMinimap() {
    int sbX = SCREEN_W - sidebarW;
    // RA2 布局：电力条占左侧 28px，小地图居右，缩放适配（旧版按原生 256px 绘制会越界遮挡电力表）
    int mmSize = sidebarW - 34;
    int mmX = sbX + 28, mmY = 42;
    // 凹陷金属框 + 阵营色边框（RA2 雷达有阵营色镶边）
    DrawRectangle(mmX - 2, mmY - 2, mmSize + 4, mmSize + 4, Color{15, 16, 20, 255});
    guiBevel({(float)mmX - 2, (float)mmY - 2, (float)mmSize + 4, (float)mmSize + 4}, true);
    DrawRectangleLinesEx({(float)mmX - 2, (float)mmY - 2, (float)mmSize + 4, (float)mmSize + 4}, 1,
                         radarOnline() ? HOUSE_COLORS[world.players[localPlayer].colorId] : Color{74, 64, 42, 255});
    // 雷达标签（RA2 原作雷达框下方有阵营名/雷达字样）
    {
        Color fc = HOUSE_COLORS[world.players[localPlayer].colorId];
        const char* lbl = radarOnline() ? factionName(world.players[localPlayer].faction) : TR(S::RadarOffline);
        drawTextS(font, lbl, mmX, mmY + mmSize + 3, 10, radarOnline() ? fc : Color{120, 90, 70, 255});
    }
    // 雷达离线：黑屏 + 扫描噪点 + 红色闪烁提示（RA2 原作低电/无雷达表现）
    if (!radarOnline()) {
        DrawRectangle(mmX, mmY, mmSize, mmSize, Color{6, 8, 10, 255});
        // 稀疏噪点（随 tick 微动，模拟无信号雪花）
        for (int i = 0; i < mmSize; i += 4)
            for (int j = 0; j < mmSize; j += 4) {
                uint32_t v = ((uint32_t)(i * 31 + j * 17) ^ (uint32_t)(world.tick / 4)) * 0x5bd1e995u;
                if ((v >> 13) % 23 == 0)
                    DrawRectangle(mmX + i, mmY + j, 2, 2, Color{20, 30, 26, 255});
            }
        if ((world.tick / 16) % 2) {
            const char* t = TR(S::RadarOffline);
            int tw = (int)MeasureTextEx(font, t, 13, 1).x;
            drawTextS(font, t, mmX + mmSize / 2 - tw / 2, mmY + mmSize / 2 - 7, 13, Color{220, 70, 56, 255});
        }
        return;
    }
    // 绘制（256 渲染纹理 → mmSize 缩放）
    DrawTexturePro(minimap.texture,
                   {0, 0, (float)minimap.texture.width, -(float)minimap.texture.height},
                   {(float)mmX, (float)mmY, (float)mmSize, (float)mmSize}, {0, 0}, 0, WHITE);
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
