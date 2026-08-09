#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

void Game::updateHoverCursor(int mx, int my) {
    cursorKind = CursorKind::Arrow;
    // 框选拖拽中强制箭头（RA2：框选时不显示移动光标）
    if (dragging) return;
    if (phase != Phase::InGame || showMenu || gameOver
        || mx >= SCREEN_W - sidebarW || my >= SCREEN_H - BOTTOM_BAR_H) return;
    if (sideMode == 1) { cursorKind = CursorKind::Repair; return; }
    if (sideMode == 2) { cursorKind = CursorKind::Sell; return; }
    // 放置模式：可放→Arrow（保持默认不误导），不可放→NoMove
    if (placing) {
        BldType t = world.players[localPlayer].placingBld;
        if (t != BldType::COUNT) {
            float wx, wy;
            screenToWorld(mx, my, wx, wy);
            int tx, ty;
            screenToTile(wx, wy, tx, ty);
            const BldDef& d = bldDef(t);
            int bx = tx - (d.w - 1), by = ty - (d.h - 1);
            cursorKind = world.canPlace(t, bx, by, localPlayer) ? CursorKind::Arrow : CursorKind::NoMove;
        }
        return;
    }
    // 超武/伞兵瞄准：无专用帧时保持 Arrow，避免误用 Attack
    if (targetingSW != SWType::COUNT || targetingParadrop) return;

    float wx, wy;
    screenToWorld(mx, my, wx, wy);
    int tx, ty;
    screenToTile(wx, wy, tx, ty);
    EID eu = pickUnit(mx, my);
    EID eb = pickBuilding(mx, my);

    // 无选单位：己方驻军建筑 → 部署光标（点一下撤出）；
    // 建造厂 + MCV Repacks：悬停自身 → Deploy；悬停可走地 → Move（右键打包移动）
    if (sel.empty()) {
        if (eb != INVALID_EID && world.ents[eb].player == localPlayer
            && !world.ents[eb].garrison.empty()) {
            cursorKind = CursorKind::Deploy;
        } else if (world.valid(selBuilding) && world.ents[selBuilding].player == localPlayer
                   && world.ents[selBuilding].btype == BldType::ConYard && world.mcvRepacks) {
            if (eb == selBuilding)
                cursorKind = CursorKind::Deploy;
            else if (world.map.inBounds(tx, ty) && world.map.passable(tx, ty)
                     && (eu == INVALID_EID || world.ents[eu].player != localPlayer)
                     && (eb == INVALID_EID || world.ents[eb].player != localPlayer))
                cursorKind = CursorKind::Move;
            else
                cursorKind = CursorKind::Deploy;
        }
        return;
    }

    bool hasHarvester = false, hasEngineer = false, hasInf = false, hasMcv = false, hasDeployable = false, hasSpy = false;
    for (EID id : sel) {
        if (!world.valid(id) || world.ents[id].isBuilding) continue;
        UnitType ut = world.ents[id].utype;
        const UnitDef& ud = unitDef(ut);
        if (ud.canHarvet()) hasHarvester = true;
        if (ut == UnitType::Engineer) hasEngineer = true;
        if (ut == UnitType::Spy) hasSpy = true;
        if (ud.isInfantry()) hasInf = true;
        if (ut == UnitType::MCV) hasMcv = true;
        if (ut == UnitType::GI || ut == UnitType::GuardianGI || ut == UnitType::Desolator
            || ut == UnitType::SiegeChopper || ut == UnitType::SlaveMiner
            || ut == UnitType::V3Launcher)
            hasDeployable = true;
    }

    // 工程师占领：敌方或中立可占领建筑（优先于进驻/攻击光标）
    if (hasEngineer && eb != INVALID_EID) {
        const World::Ent& b = world.ents[eb];
        if (bldDef(b.btype).capturable && b.player != localPlayer
            && (b.player < 0 || world.isEnemy(localPlayer, b.player))) {
            cursorKind = CursorKind::Enter;
            return;
        }
    }
    // 间谍渗透敌方建筑 → 进入；点敌方步兵 → 伪装（攻击光标）
    if (hasSpy && eb != INVALID_EID) {
        const World::Ent& b = world.ents[eb];
        if (b.player >= 0 && world.isEnemy(localPlayer, b.player)) {
            cursorKind = CursorKind::Enter;
            return;
        }
    }
    if (hasSpy && eu != INVALID_EID) {
        const World::Ent& u = world.ents[eu];
        if (u.player >= 0 && world.isEnemy(localPlayer, u.player) && unitDef(u.utype).isInfantry()) {
            cursorKind = CursorKind::Attack; // 伪装指令（攻击目标兵种）
            return;
        }
    }

    // 敌方 → 攻击（工程师对可占领建筑 → 进入）；中立/盟友不当敌人
    EID enemy = INVALID_EID;
    if (eu != INVALID_EID && world.isEnemy(localPlayer, world.ents[eu].player)) enemy = eu;
    if (enemy == INVALID_EID && eb != INVALID_EID && world.isEnemy(localPlayer, world.ents[eb].player))
        enemy = eb;
    // 可进驻建筑（优先于攻击光标，避免中立民房显示攻击）
    if (eb != INVALID_EID && hasInf) {
        const World::Ent& b = world.ents[eb];
        const BldDef& bd = bldDef(b.btype);
        if (bd.garrisonCap > 0 && garrisonDomain(b.btype) == 1
            && (b.player == localPlayer || b.player < 0)) {
            bool anyFit = false;
            for (EID id : sel) {
                if (!world.valid(id) || world.ents[id].isBuilding) continue;
                if (!unitDef(world.ents[id].utype).isInfantry()) continue;
                if (b.btype == BldType::CivHouse && !canGarrisonCivHouse(world.ents[id].utype)) continue;
                anyFit = true; break;
            }
            if (anyFit) { cursorKind = CursorKind::Enter; return; }
        }
    }
    // 己方维修厂/机械商店/科技前哨：受损车辆 → 修理光标
    if (eb != INVALID_EID && world.ents[eb].player == localPlayer) {
        BldType bt = world.ents[eb].btype;
        if (bt == BldType::ServiceDepot || bt == BldType::MachineShop || bt == BldType::TechOutpost) {
            for (EID id : sel) {
                if (!world.valid(id) || world.ents[id].isBuilding) continue;
                const World::Ent& u = world.ents[id];
                const UnitDef& ud = unitDef(u.utype);
                if (ud.isInfantry() || ud.isAir() || ud.pathDomain() != 0 || ud.canHarvet()) continue;
                if (u.hp < ud.hp || u.parasite != INVALID_EID) {
                    cursorKind = CursorKind::Repair;
                    return;
                }
            }
        }
    }
    if (enemy != INVALID_EID) {
        // 空军目标：仅当选中单位有防空武器时显示攻击光标
        bool airT = !world.ents[enemy].isBuilding
            && unitDef(world.ents[enemy].utype).isAir()
            && world.ents[enemy].state != UState::Landed;
        if (airT) {
            bool canAA = false;
            for (EID id : sel) {
                if (!world.valid(id) || world.ents[id].isBuilding) continue;
                if (world.effWeapon(world.ents[id]).antiAir) { canAA = true; break; }
            }
            // 建筑武器：选中防御塔时
            if (!canAA && world.valid(selBuilding) && world.ents[selBuilding].player == localPlayer) {
                const BldDef& bd = bldDef(world.ents[selBuilding].btype);
                if (bd.weapon.antiAir) canAA = true;
            }
            if (!canAA) { cursorKind = CursorKind::NoMove; return; }
        }
        if (hasEngineer && world.ents[enemy].isBuilding && bldDef(world.ents[enemy].btype).capturable)
            cursorKind = CursorKind::Enter;
        else
            cursorKind = CursorKind::Attack;
        return;
    }

    // 运输载具
    if (eu != INVALID_EID && world.ents[eu].player == localPlayer
        && unitDef(world.ents[eu].utype).cargoCap > 0 && hasInf) {
        cursorKind = CursorKind::Enter;
        return;
    }
    // 矿车回己方精炼厂卸货 → Enter（进驻/停靠）
    if (hasHarvester && eb != INVALID_EID && world.ents[eb].player == localPlayer
        && world.ents[eb].btype == BldType::OreRefinery) {
        cursorKind = CursorKind::Enter;
        return;
    }
    // 矿脉 → AttackMove（Harvest）
    if (hasHarvester && world.map.inBounds(tx, ty) && world.map.at(tx, ty).ore > 0) {
        cursorKind = CursorKind::Harvest;
        return;
    }
    // MCV：可展开时显示 Deploy（含悬停自身；点选已不依赖光标种类）
    if (hasMcv) {
        for (EID id : sel) {
            if (!world.valid(id) || world.ents[id].utype != UnitType::MCV) continue;
            if (world.canDeployMcv(id)) { cursorKind = CursorKind::Deploy; return; }
        }
    }

    // 已选可部署单位且悬停自身 → Deploy
    if (hasDeployable && eu != INVALID_EID && world.ents[eu].player == localPlayer) {
        UnitType ut = world.ents[eu].utype;
        if (ut == UnitType::GI || ut == UnitType::GuardianGI || ut == UnitType::Desolator
            || ut == UnitType::SiegeChopper || ut == UnitType::SlaveMiner
            || ut == UnitType::V3Launcher) {
            cursorKind = CursorKind::Deploy;
            return;
        }
    }
    // 默认可走地 → 移动；按住 A = 攻击移动光标（AttackMove@404，与矿上 Harvest 同源）
    if (world.map.inBounds(tx, ty) && world.map.passable(tx, ty)) {
        if (!sel.empty() && kDown(KEY_A))
            cursorKind = CursorKind::Harvest; // AttackMove
        else
            cursorKind = CursorKind::Move;
    } else
        cursorKind = CursorKind::NoMove;
}

void Game::loadGameCursors() {
    unloadGameCursors();
    // 全量 mouse.shp：优先 cursor_XXX.png（≥404），兼容旧 cursor_XX.png；允许中间空洞
    cursorFrameN = 0;
    for (int i = 0; i < CURSOR_MAX_FRAMES; i++) {
        const char* path3 = TextFormat("assets/gui/cursors/cursor_%03d.png", i);
        const char* path2 = TextFormat("assets/gui/cursors/cursor_%02d.png", i);
        const char* path = FileExists(path3) ? path3 : (i < 100 && FileExists(path2) ? path2 : nullptr);
        if (!path) continue;
        Image img = LoadImage(path);
        if (!img.data) continue;
        cursorFrames[i] = LoadTextureFromImage(img);
        SetTextureFilter(cursorFrames[i], TEXTURE_FILTER_POINT);
        UnloadImage(img);
        if (i + 1 > cursorFrameN) cursorFrameN = i + 1;
    }
    // Ares MouseCursors.txt 默认表（RA2/YR 引擎隐式光标）
    // Harvest 动作 → AttackMove（404），非红准星 Attack(53)
    auto set = [&](CursorKind k, int start, int count, int interval, int hx, int hy) {
        cursorDefs[(int)k] = {start, std::max(1, count), std::max(0, interval), hx, hy};
    };
    set(CursorKind::Arrow,    0,  1, 0, 0, 0);          // Left,Top
    set(CursorKind::Move,    31, 10, 4, 27, 21);        // Center,Middle ≈ 55/2,43/2
    set(CursorKind::NoMove,  41,  1, 0, 27, 21);
    set(CursorKind::Attack,  53,  5, 4, 27, 21);
    set(CursorKind::Enter,   89, 10, 4, 27, 21);
    set(CursorKind::Deploy, 110,  9, 4, 27, 21);
    set(CursorKind::Sell,   129, 10, 4, 27, 21);
    set(CursorKind::Repair, 150, 20, 4, 27, 21);
    set(CursorKind::Harvest,404,  9, 4, 27, 21);        // AttackMove
    cursorsLoaded = cursorFrameN > 0;
    TraceLog(LOG_INFO, "RA2 cursors: loaded up to frame %d (Ares table)", cursorFrameN);
}

void Game::unloadGameCursors() {
    for (int i = 0; i < CURSOR_MAX_FRAMES; i++)
        if (cursorFrames[i].id) { UnloadTexture(cursorFrames[i]); cursorFrames[i] = {}; }
    cursorFrameN = 0;
    cursorsLoaded = false;
}

void Game::drawGameCursor(int mx, int my) {
    HideCursor();
    const CursorDef& def = cursorDefs[(int)cursorKind];
    int frame = def.start;
    if (def.count > 1 && def.interval > 0)
        // LOGIC_FPS=30 下 Ares Interval 按原作 ~15fps 逻辑帧计；×2 分频对齐观感
        frame = def.start + ((int)world.tick / (def.interval * 2)) % def.count;
    if (cursorsLoaded && frame >= 0 && frame < CURSOR_MAX_FRAMES && cursorFrames[frame].id) {
        // 原作 SHP 自带配色，禁止再 tint 冒充 Move/Harvest
        DrawTexture(cursorFrames[frame], mx - def.hx, my - def.hy, WHITE);
        return;
    }
    // 回退：无素材时用几何形
    Color c{255, 255, 255, 255};
    int x = mx, y = my;
    switch (cursorKind) {
        case CursorKind::Move:
            c = Color{80, 255, 120, 255};
            DrawTriangle({(float)x, (float)y}, {(float)(x + 14), (float)(y + 6)}, {(float)(x + 5), (float)(y + 14)}, c);
            break;
        case CursorKind::Attack:
            c = Color{255, 60, 40, 255};
            DrawCircleLines(x + 8, y + 8, 8, c);
            DrawLine(x + 8, y, x + 8, y + 16, c);
            DrawLine(x, y + 8, x + 16, y + 8, c);
            break;
        case CursorKind::Harvest:
            c = Color{255, 220, 60, 255};
            DrawCircleLines(x + 8, y + 8, 8, c);
            DrawLine(x + 3, y + 13, x + 13, y + 3, c);
            break;
        case CursorKind::Enter:
            c = Color{80, 200, 255, 255};
            DrawRectangleLines(x + 2, y + 2, 12, 12, c);
            break;
        case CursorKind::Deploy:
            c = Color{255, 200, 80, 255};
            DrawTriangle({(float)(x + 8), (float)y}, {(float)(x + 14), (float)(y + 10)}, {(float)(x + 2), (float)(y + 10)}, c);
            break;
        case CursorKind::Repair:
            DrawRectangle(x + 3, y + 6, 10, 3, Color{255, 220, 80, 255});
            DrawRectangle(x + 7, y + 2, 3, 12, Color{255, 220, 80, 255});
            break;
        case CursorKind::Sell:
            DrawText("$", x + 2, y + 2, 14, Color{255, 220, 60, 255});
            break;
        case CursorKind::NoMove:
            DrawLine(x, y, x + 14, y + 14, Color{255, 80, 80, 220});
            DrawLine(x + 14, y, x, y + 14, Color{255, 80, 80, 220});
            break;
        default:
            DrawTriangle({(float)x, (float)y}, {(float)(x + 12), (float)(y + 5)},
                         {(float)(x + 4), (float)(y + 14)}, WHITE);
            break;
    }
}

