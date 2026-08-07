#include "game/game.h"
#include "game/campaign.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_set>

#include <cstdio>
#include <filesystem>

int Game::smokeTest(int frames) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char* name) {
        TraceLog(ok ? LOG_INFO : LOG_ERROR, "SMOKE %s: %s", ok ? "PASS" : "FAIL", name);
        ok ? ++passed : ++failed;
    };
    newGame(20260722);
    check(world.numPlayers > 1, "skirmish creates multiple players");
    check(world.map.w > 0 && world.map.h > 0 && !world.map.cells.empty(), "skirmish creates a map");
    for (int i = 0; i < frames; i++) logic();
    render(); // 预热帧，确保帧缓冲有效
    int savedLocal = localPlayer;
    gameOver = victory = false; // 截图不叠结算画面
    showMenu = false;
    // 每个玩家基地截图（使用该玩家的迷雾视角）
    for (int p = 0; p < world.numPlayers; p++) {
        // 摄像机目标：建造厂 > 任意建筑 > 任意单位
        const World::Ent* target = nullptr;
        for (auto& e : world.ents)
            if (e.alive && e.player == p && e.isBuilding && e.btype == BldType::ConYard) { target = &e; break; }
        if (!target)
            for (auto& e : world.ents)
                if (e.alive && e.player == p && e.isBuilding) { target = &e; break; }
        if (!target)
            for (auto& e : world.ents)
                if (e.alive && e.player == p) { target = &e; break; }
        if (target) {
            float wx, wy;
            if (target->isBuilding) {
                int px, py;
                const BldDef& d = bldDef(target->btype);
                tileToScreen((int)target->x + d.w / 2, (int)target->y + d.h / 2, px, py);
                wx = (float)px; wy = (float)py;
            } else {
                wx = (target->x - target->y) * (TILE_W / 2.0f);
                wy = (target->x + target->y) * (TILE_H / 2.0f);
            }
            camX = wx - (SCREEN_W - sidebarW) / 2.0f;
            camY = wy - SCREEN_H / 2.0f;
        }
        localPlayer = p;
        minimapTimer = 0; // 强制重绘小地图
        shotFile = TextFormat("shot_p%d.png", p);
        render();
    }
    localPlayer = savedLocal;
    for (int p = 0; p < world.numPlayers; p++) {
        int blds = 0, units = 0;
        for (auto& e : world.ents)
            if (e.alive && e.player == p) { if (e.isBuilding) blds++; else units++; }
        TraceLog(LOG_INFO, "player %d: money=%d blds=%d units=%d power=%d/%d defeated=%d",
                 p, world.players[p].money, blds, units, world.players[p].powerMade, world.players[p].powerUsed,
                 (int)world.players[p].defeated);
        // 诊断：单位明细与采矿车状态
        int cnt[(int)UnitType::COUNT] = {0};
        int harvState[12] = {0};
        for (auto& e : world.ents) {
            if (!e.alive || e.isBuilding || e.player != p) continue;
            cnt[(int)e.utype]++;
            if (unitDef(e.utype).canHarvet() && (int)e.state < 12) harvState[(int)e.state]++;
        }
        std::string det;
        for (int i = 0; i < (int)UnitType::COUNT; i++)
            if (cnt[i]) { det += unitDef((UnitType)i).name; det += "=" + std::to_string(cnt[i]) + " "; }
        TraceLog(LOG_INFO, "  units: %s", det.c_str());
        // 建筑明细
        int bcnt[(int)BldType::COUNT] = {0};
        for (auto& e : world.ents)
            if (e.alive && e.isBuilding && e.player == p) bcnt[(int)e.btype]++;
        std::string bdet;
        for (int i = 0; i < (int)BldType::COUNT; i++)
            if (bcnt[i]) { bdet += bldDef((BldType)i).name; bdet += "=" + std::to_string(bcnt[i]) + " "; }
        TraceLog(LOG_INFO, "  blds: %s", bdet.c_str());
        // 超武状态
        std::string swdet;
        for (int i = 0; i < (int)SWType::COUNT; i++) {
            swdet += swDef((SWType)i).name;
            swdet += world.players[p].swReady[i] ? "=就绪 "
                     : ("=" + std::to_string(world.players[p].swCharge[i] * 100 / swDef((SWType)i).chargeTime) + "% ");
        }
        TraceLog(LOG_INFO, "  superweapons: %s", swdet.c_str());
        TraceLog(LOG_INFO, "  harvester states: idle=%d moving=%d atkmove=%d chase=%d atk=%d hgo=%d dig=%d hret=%d hunload=%d",
                 harvState[0], harvState[1], harvState[2], harvState[3], harvState[4],
                 harvState[5], harvState[6], harvState[7], harvState[8]);
    }
    // 全图剩余矿量
    {
        long oreTotal = 0;
        for (int y = 0; y < world.map.h; y++)
            for (int x = 0; x < world.map.w; x++) oreTotal += world.map.at(x, y).ore;
        TraceLog(LOG_INFO, "  ore remaining on map: %ld", oreTotal);
    }
    // ---- 超武释放路径强制验证（不依赖 AI 建造进度）----
    if (world.numPlayers > 1 && !world.players[1].defeated) {
        // 在地图中部找空地
        int cx = world.map.w / 2, cy = world.map.h / 2;
        while (!world.map.passable(cx, cy) && cx < world.map.w - 8) cx++;
        // 给玩家 1 强制放置三座超武建筑（测试专用，绕过 canPlace）
        auto forceBld = [&](BldType t, int bx, int by) {
            const BldDef& d = bldDef(t);
            for (int dy = 0; dy < d.h; dy++)
                for (int dx = 0; dx < d.w; dx++)
                    if (world.bldAt(bx + dx, by + dy) != INVALID_EID) return;
            world.spawnBuilding(1, t, bx, by, true);
        };
        forceBld(BldType::NukeSilo, cx - 6, cy - 3);
        forceBld(BldType::WeatherDevice, cx - 6, cy + 1);
        forceBld(BldType::IronCurtain, cx + 5, cy - 3);
        // 在空地刷靶子（玩家 0 的坦克群）
        std::vector<EID> targets;
        for (int k = 0; k < 4; k++)
            targets.push_back(world.spawnUnit(0, UnitType::Rhino, cx + (k % 2) + 0.5f, cy + (k / 2) + 0.5f));
        int hpBefore = 0;
        for (EID t : targets) hpBefore += world.ents[t].hp;
        // 1) 核弹：强制就绪并发射
        world.players[1].swReady[(int)SWType::Nuke] = true;
        bool okNuke = world.launchSW(1, SWType::Nuke, cx + 0.5f, cy + 0.5f);
        // 2) 闪电风暴：强制就绪并启动（目标点挪远些，避免与核爆重叠干扰统计）
        world.players[1].swReady[(int)SWType::Lightning] = true;
        bool okStorm = world.launchSW(1, SWType::Lightning, cx + 0.5f, cy + 0.5f);
        // 3) 铁幕：套在玩家 1 自己的坦克上验证无敌
        EID ownTank = world.spawnUnit(1, UnitType::Rhino, cx + 3.5f, cy + 3.5f);
        world.players[1].swReady[(int)SWType::IronCurtain] = true;
        bool okIC = world.launchSW(1, SWType::IronCurtain, cx + 3.5f, cy + 3.5f);
        bool icInvuln = world.ents[ownTank].invuln > 0;
        // 跑 300 tick 让核弹落地爆炸 + 闪电落雷
        for (int i = 0; i < 300; i++) world.update();
        int hpAfter = 0, alive = 0;
        for (EID t : targets)
            if (world.ents[t].alive) { hpAfter += world.ents[t].hp; alive++; }
        TraceLog(LOG_INFO, "sw verify: nuke=%d storm=%d ic=%d icInvuln=%d | targets hp %d -> %d, alive %d/4",
                 (int)okNuke, (int)okStorm, (int)okIC, (int)icInvuln, hpBefore, hpAfter, alive);
        check(okNuke, "nuclear missile launches");
        check(okStorm, "lightning storm launches");
        check(okIC, "iron curtain launches");
        check(icInvuln, "iron curtain applies invulnerability");
        check(hpAfter < hpBefore, "offensive superweapons damage targets");
        // ---- 碾压验证：重甲坦克碾过敌方无甲步兵 ----
        {
            // 长时 smoke 中玩家 0 可能已先战败；测试专用单位应恢复可接收 EVA 的边界状态。
            world.players[0].defeated = false;
            world.players[0].evaUnitCd = 0;
            int cy2 = cy - 8;
            EID tank = world.spawnUnit(1, UnitType::Rhino, cx - 3.5f, cy2 + 0.5f);
            EID inf = world.spawnUnit(0, UnitType::Conscript, cx + 2.5f, cy2 + 0.5f);
            world.orderMove({tank}, cx + 4.5f, cy2 + 0.5f, false);
            for (int i = 0; i < 400 && world.valid(inf); i++) world.update();
            bool sawLost = false;
            for (const auto& ev : world.evaQueue)
                if (ev.player == 0 && ev.text.find(TR(S::EvaUnitLost)) != std::string::npos) sawLost = true;
            TraceLog(LOG_INFO, "crush verify: infantry alive=%d (expect 0), eva unitLost=%d (expect 1)",
                     (int)world.valid(inf), (int)sawLost);
            check(!world.valid(inf), "tank crushes infantry");
            check(sawLost, "crush emits unit-lost EVA");
        }
        // ---- 警戒/散布指令冒烟 ----
        {
            EID g1 = world.spawnUnit(1, UnitType::Rhino, cx - 5.5f, cy + 6.5f);
            EID g2 = world.spawnUnit(1, UnitType::Rhino, cx - 4.5f, cy + 6.5f);
            world.orderGuard({g1, g2});
            bool guardOk = world.ents[g1].guard && world.ents[g2].guard;
            world.orderScatter({g1, g2});
            bool scatterOk = !world.ents[g1].guard && world.ents[g1].state == UState::Moving;
            TraceLog(LOG_INFO, "guard/scatter verify: guard=%d scatter=%d (expect 1/1)",
                     (int)guardOk, (int)scatterOk);
            check(guardOk, "guard order is applied");
            check(scatterOk, "scatter order starts movement");
        }
        // ---- 海军验证：船厂水面放置 + 舰艇水域寻路 + 鱼雷限制 + 运输装卸 ----
        {
            // 找一块 8x8 水域（容纳电厂 2x2 + 船厂 3x3 + 舰艇活动空间）
            int wx0 = -1, wy0 = -1;
            for (int y = 0; y + 8 <= world.map.h && wx0 < 0; y++)
                for (int x = 0; x + 8 <= world.map.w && wx0 < 0; x++) {
                    bool allW = true;
                    for (int dy = 0; dy < 8 && allW; dy++)
                        for (int dx = 0; dx < 8 && allW; dx++)
                            if (world.map.at(x + dx, y + dy).terrain != Terrain::Water) allW = false;
                    if (allW) { wx0 = x; wy0 = y; }
                }
            if (wx0 < 0) {
                TraceLog(LOG_ERROR, "naval verify: no 8x8 water on map");
                check(false, "naval scenario has an 8x8 water test area");
            } else {
                // 0) 水中放一座电厂建立建造半径（测试专用，spawnBuilding 绕过地形检查）
                world.spawnBuilding(1, BldType::PowerPlant, wx0, wy0, true);
                // 1) 船厂可建于水面（含建造半径约束）
                bool yardWater = world.canPlace(BldType::NavalYard, wx0 + 2, wy0, 1);
                world.spawnBuilding(1, BldType::NavalYard, wx0 + 2, wy0, true);
                // 1b) 同一建造半径内的纯陆地 3x3 必须拒绝船厂（地形校验）
                bool yardLandRejected = true;
                for (int dy = -3; dy <= 1; dy++)
                    for (int dx = -3; dx <= 1; dx++) {
                        int nx = wx0 + dx, ny = wy0 + dy;
                        bool ok = true;
                        for (int ddy = 0; ddy < 3 && ok; ddy++)
                            for (int ddx = 0; ddx < 3 && ok; ddx++)
                                if (!world.map.inBounds(nx + ddx, ny + ddy) || !world.map.passable(nx + ddx, ny + ddy)
                                    || world.bldAt(nx + ddx, ny + ddy) != INVALID_EID) ok = false;
                        if (ok && world.canPlace(BldType::NavalYard, nx, ny, 1)) { yardLandRejected = false; dy = 99; break; }
                    }
                // 2) 驱逐舰水域寻路：8x8 水域内选可达远格
                float sx0 = wx0 + 6.5f, sy0 = wy0 + 5.5f;
                EID ship = world.spawnUnit(1, UnitType::Destroyer, sx0, sy0);
                int gx = -1, gy = -1;
                std::vector<Vec2i> npath;
                for (int tcell = 0; tcell < 64 && gx < 0; tcell++) {
                    int nx = wx0 + (tcell % 8), ny = wy0 + (tcell / 8);
                    if (world.bldAt(nx, ny) != INVALID_EID) continue;
                    if (world.map.findPath((int)sx0, (int)sy0, nx, ny, npath, 20000, 1) && npath.size() >= 4) {
                        gx = nx; gy = ny;
                    }
                }
                bool moveOk = false;
                if (gx >= 0) {
                    world.orderMove({ship}, gx + 0.5f, gy + 0.5f, false);
                    for (int i = 0; i < 900 && world.ents[ship].state == UState::Moving; i++) world.update();
                    float moved = distf(world.ents[ship].x, world.ents[ship].y, sx0, sy0);
                    bool onWater = world.map.at((int)world.ents[ship].x, (int)world.ents[ship].y).terrain == Terrain::Water;
                    moveOk = moved > 3.0f && onWater;
                }
                if (world.valid(ship)) world.ents[ship].alive = false; // 清理：避免干扰鱼雷测试
                // 3) 鱼雷：可攻击水上目标（同水域内 2 格距离）
                EID sub = world.spawnUnit(1, UnitType::Typhoon, wx0 + 5.5f, wy0 + 1.5f);
                EID foeShip = world.spawnUnit(0, UnitType::Destroyer, wx0 + 5.5f, wy0 + 3.5f);
                int foeHp0 = world.ents[foeShip].hp;
                for (int i = 0; i < 300 && world.valid(sub); i++) world.update();
                bool torpOk = !world.valid(foeShip) || world.ents[foeShip].hp < foeHp0;
                // 4) 岸边：鱼雷不得攻击陆地目标 + 运输装卸
                int lx = -1, ly = -1;
                for (int r = 3; r <= 8 && lx < 0; r++)
                    for (int dy = -r; dy <= r && lx < 0; dy++)
                        for (int dx = -r; dx <= r && lx < 0; dx++) {
                            int nx = wx0 + dx, ny = wy0 + dy;
                            if (world.map.inBounds(nx, ny) && world.map.passable(nx, ny)
                                && world.unitAtCell(nx, ny) == INVALID_EID && !world.bldBlocked(nx, ny)) { lx = nx; ly = ny; }
                        }
                bool navalOnlyOk = true, boardOk = true, unloadOk = true;
                if (lx >= 0) {
                    // 陆地靶子：鱼雷不得选为攻击目标（NavalOnly）
                    if (world.valid(sub)) {
                        EID landTank = world.spawnUnit(0, UnitType::Rhino, lx + 0.5f, ly + 0.5f);
                        world.ents[landTank].invuln = 1000; // 排除流弹干扰，只验索敌
                        world.ents[sub].target = INVALID_EID;
                        world.ents[sub].state = UState::Idle;
                        for (int i = 0; i < 200 && world.valid(landTank); i++) {
                            world.update();
                            if (world.valid(sub) && world.ents[sub].target == landTank) navalOnlyOk = false;
                        }
                        navalOnlyOk = navalOnlyOk && unitDef(UnitType::Typhoon).weapon.navalOnly;
                        if (world.valid(landTank)) world.ents[landTank].alive = false; // 清理
                    }
                    // 运输装卸：两栖运输船在陆地格装载 2 名步兵后卸载
                    std::vector<Vec2i> frees;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            if (!dx && !dy) continue;
                            int nx = lx + dx, ny = ly + dy;
                            if (world.map.passable(nx, ny) && !world.bldBlocked(nx, ny)
                                && world.unitAtCell(nx, ny) == INVALID_EID) frees.push_back({nx, ny});
                        }
                    if ((int)frees.size() >= 2) {
                        EID tr = world.spawnUnit(1, UnitType::AmphTransport, lx + 0.5f, ly + 0.5f);
                        EID i1 = world.spawnUnit(1, UnitType::Conscript, frees[0].x + 0.5f, frees[0].y + 0.5f);
                        EID i2 = world.spawnUnit(1, UnitType::Conscript, frees[1].x + 0.5f, frees[1].y + 0.5f);
                        world.orderBoard({i1, i2}, tr);
                        for (int i = 0; i < 600 && (int)world.ents[tr].cargo.size() < 2; i++) world.update();
                        boardOk = (int)world.ents[tr].cargo.size() == 2;
                        world.orderUnload({tr});
                        unloadOk = world.ents[tr].cargo.empty();
                    }
                }
                TraceLog(LOG_INFO, "naval verify: yardWater=%d yardLandRejected=%d move=%d torpedo=%d navalOnly=%d board=%d unload=%d (expect 1/1/1/1/1/1/1)",
                         (int)yardWater, (int)yardLandRejected, (int)moveOk, (int)torpOk, (int)navalOnlyOk, (int)boardOk, (int)unloadOk);
                check(yardWater, "naval yard can be placed on water");
                check(yardLandRejected, "naval yard is rejected on land");
                check(moveOk, "naval unit follows a water path");
                check(torpOk, "torpedo damages a naval target");
                check(navalOnlyOk, "torpedo rejects a land target");
                check(boardOk, "transport boards infantry");
                check(unloadOk, "transport unloads infantry");
            }
        }
        // ---- 特殊单位机制验证：辐射部署 / 超时空传送 / 幻影伪装 / V3溅射 ----
        {
            int bx = cx, by = cy - 14; // 与上方测试区错开
            // 1) 辐射工兵：部署后范围持续伤害（用同阵营兵避免反击走位，辐射不分敌我）
            EID deso = world.spawnUnit(1, UnitType::Desolator, bx + 0.5f, by + 0.5f);
            world.orderRadDeploy({deso});
            bool deployOk = world.ents[deso].radDeployed;
            EID victim = world.spawnUnit(1, UnitType::Conscript, bx + 1.5f, by + 0.5f);
            int vhp0 = world.ents[victim].hp;
            for (int i = 0; i < 150 && world.valid(victim); i++) world.update();
            bool radOk = !world.valid(victim) || world.ents[victim].hp < vhp0;
            if (world.valid(victim)) world.ents[victim].alive = false;
            if (world.valid(deso)) world.ents[deso].alive = false; // 清理：避免辐射干扰后续
            // 2) 超时空军团兵：传送后瞬时位移 + 相位不适
            EID chr = world.spawnUnit(1, UnitType::Chrono, bx + 4.5f, by + 0.5f);
            world.orderMove({chr}, bx + 10.5f, by + 6.5f, false);
            bool tpOk = distf(world.ents[chr].x, world.ents[chr].y, bx + 4.5f, by + 0.5f) > 3.0f
                        && world.ents[chr].tpSick > 0;
            if (world.valid(chr)) world.ents[chr].alive = false; // 清理：避免干扰伪装/溅射
            // 3) 幻影坦克：静止进入伪装（无敌+清目标，避免挨打反击打断 Idle）
            EID mir = world.spawnUnit(0, UnitType::MirageTank, bx + 6.5f, by + 2.5f);
            world.ents[mir].invuln = 100000;
            world.ents[mir].guard = false;
            world.ents[mir].camoTick = 0;
            for (int i = 0; i < 100; i++) {
                if (world.valid(mir)) {
                    World::Ent& m = world.ents[mir];
                    m.state = UState::Idle;
                    m.target = INVALID_EID;
                    m.path.clear();
                    m.wps.clear();
                }
                world.update();
            }
            bool camoOk = world.valid(mir) && world.ents[mir].camouflaged;
            // 4) V3 溅射：命中点相邻目标一同掉血（清理幻影避免其击杀 t1 干扰；t1 只能死于 V3 导弹）
            if (world.valid(mir)) world.ents[mir].alive = false;
            EID v3 = world.spawnUnit(1, UnitType::V3Launcher, bx - 6.5f, by + 0.5f);
            EID t1 = world.spawnUnit(0, UnitType::Conscript, bx + 6.5f, by + 4.5f); // 距 V3 13 格，在射程 14 内
            EID t2 = world.spawnUnit(0, UnitType::Conscript, bx + 7.5f, by + 4.5f); // t1 相邻，处于溅射圈
            world.orderAttack({v3}, t1);
            int t2hp0 = world.ents[t2].hp;
            // t1 死后仍继续模拟，让飞行中的导弹落地（溅射与直接命中同帧结算）
            for (int i = 0; i < 900 && world.valid(v3) && (world.valid(t1) || world.valid(t2)); i++) world.update();
            // t2 掉血或阵亡只能来自溅射（V3 未以其为目标）
            bool splashOk = !world.valid(t2) || world.ents[t2].hp < t2hp0;
            TraceLog(LOG_INFO, "special verify: radDeploy=%d radDmg=%d chronoTp=%d mirageCamo=%d v3Splash=%d (expect 1/1/1/1/1)",
                     (int)deployOk, (int)radOk, (int)tpOk, (int)camoOk, (int)splashOk);
            check(deployOk, "desolator deploys");
            check(radOk, "deployed desolator deals radiation damage");
            check(tpOk, "chrono legionnaire teleports");
            check(camoOk, "mirage tank camouflages");
            check(splashOk, "V3 missile deals splash damage");
        }
        // ---- 尤复补全验证：鲍里斯空袭 / 攻城直升机部署炮击 / 混乱无人机毒气 ----
        {
            int bx = cx, by = cy + 14; // 与上方测试区错开
            // 1) 鲍里斯：对建筑呼叫米格空袭（airstrikeCd 被设置 + 建筑掉血）
            EID boris = world.spawnUnit(1, UnitType::Boris, bx + 0.5f, by + 0.5f);
            EID bld = world.spawnBuilding(0, BldType::PowerPlant, bx + 4, by, true);
            int bhp0 = world.ents[bld].hp;
            world.orderAttack({boris}, bld);
            for (int i = 0; i < 150 && world.valid(bld); i++) world.update();
            bool borisAir = world.valid(boris) && world.ents[boris].airstrikeCd > 0;
            bool borisDmg = !world.valid(bld) || world.ents[bld].hp < bhp0;
            if (world.valid(boris)) world.ents[boris].alive = false;
            if (world.valid(bld)) world.ents[bld].alive = false;
            // 2) 攻城直升机：部署后转为固定炮台，射程内敌军掉血
            EID sc = world.spawnUnit(1, UnitType::SiegeChopper, bx + 8.5f, by + 0.5f);
            world.orderRadDeploy({sc});
            bool scDeploy = world.valid(sc) && world.ents[sc].deployed;
            EID scTgt = world.spawnUnit(0, UnitType::Conscript, bx + 14.5f, by + 0.5f); // 6 格外，在部署射程 12 内
            int scTgtHp0 = world.ents[scTgt].hp;
            for (int i = 0; i < 240 && world.valid(scTgt); i++) world.update();
            bool scFire = !world.valid(scTgt) || world.ents[scTgt].hp < scTgtHp0;
            if (world.valid(sc)) world.ents[sc].alive = false;
            if (world.valid(scTgt)) world.ents[scTgt].alive = false;
            // 3) 混乱无人机：毒气命中后敌军陷入混乱（confused > 0）
            EID cd = world.spawnUnit(1, UnitType::ChaosDrone, bx + 20.5f, by + 0.5f);
            EID cdTgt = world.spawnUnit(0, UnitType::Conscript, bx + 23.5f, by + 0.5f); // 3 格外，在射程 5 内
            world.orderAttack({cd}, cdTgt);
            bool chaosHit = false;
            for (int i = 0; i < 180 && world.valid(cdTgt); i++) {
                world.update();
                if (world.valid(cdTgt) && world.ents[cdTgt].confused > 0) { chaosHit = true; break; }
            }
            if (world.valid(cd)) world.ents[cd].alive = false;
            if (world.valid(cdTgt)) world.ents[cdTgt].alive = false;
            TraceLog(LOG_INFO, "yr verify: borisAir=%d borisDmg=%d siegeDeploy=%d siegeFire=%d chaosHit=%d (expect 1/1/1/1/1)",
                     (int)borisAir, (int)borisDmg, (int)scDeploy, (int)scFire, (int)chaosHit);
            check(borisAir, "Boris starts an airstrike");
            check(borisDmg, "Boris airstrike damages a building");
            check(scDeploy, "siege chopper deploys");
            check(scFire, "deployed siege chopper fires");
            check(chaosHit, "chaos drone applies confusion");
        }
        // ---- 中立科技建筑：生成 + 工程师占领 + 油井收益 ----
        {
            int neutralCnt = 0;
            EID oil = INVALID_EID;
            for (size_t i = 0; i < world.ents.size(); i++) {
                const World::Ent& e = world.ents[i];
                if (!e.alive || !e.isBuilding || e.player != -1) continue;
                neutralCnt++;
                if (e.btype == BldType::OilDerrick && oil == INVALID_EID) oil = (int)i;
            }
            bool captureOk = false, incomeOk = false;
            if (oil != INVALID_EID) {
                const World::Ent& ob = world.ents[oil];
                // 在油井旁找一格空地刷工程师（步行一两步即达）
                int ex = -1, ey = -1;
                for (int r = 3; r < 8 && ex < 0; r++)
                    for (int dy = -r; dy <= r && ex < 0; dy++)
                        for (int dx = -r; dx <= r && ex < 0; dx++) {
                            int nx = (int)ob.x + dx, ny = (int)ob.y + dy;
                            if (world.map.passable(nx, ny) && !world.bldBlocked(nx, ny)
                                && world.unitAtCell(nx, ny) == INVALID_EID) { ex = nx; ey = ny; }
                        }
                if (ex >= 0) {
                    EID eng = world.spawnUnit(0, UnitType::Engineer, ex + 0.5f, ey + 0.5f);
                    world.orderCapture({eng}, oil);
                    int money0 = world.players[0].money;
                    for (int i = 0; i < 1200 && world.valid(eng) && world.ents[oil].player != 0; i++) world.update();
                    captureOk = world.valid(oil) && world.ents[oil].player == 0;
                    for (int i = 0; i < 220; i++) world.update(); // 跑过两个油井结算周期
                    incomeOk = world.players[0].money > money0;
                }
            }
            TraceLog(LOG_INFO, "neutral verify: count=%d capture=%d oilIncome=%d (expect >0/1/1)",
                     neutralCnt, (int)captureOk, (int)incomeOk);
            check(neutralCnt > 0, "neutral tech buildings are generated");
            check(captureOk, "engineer captures an oil derrick");
            check(incomeOk, "captured oil derrick grants income");
        }
    } else {
        check(false, "superweapon scenario has a live opponent");
    }
    // ---- playable-core：规则接线、经济、生产边界与 lockstep 指令所有权 ----
    {
        World core;
        core.init(64, 64, 0xC0DEF00D, 2, 0, {Faction::Allies, Faction::Yuri});
        auto findOpen = [&](int rw, int rh) {
            for (int y = 5; y + rh < core.map.h - 5; y++)
                for (int x = 5; x + rw < core.map.w - 5; x++) {
                    bool ok = true;
                    for (int dy = 0; dy < rh && ok; dy++)
                        for (int dx = 0; dx < rw && ok; dx++)
                            if (!core.map.passable(x + dx, y + dy) || core.bldBlocked(x + dx, y + dy)
                                || core.unitAtCell(x + dx, y + dy) != INVALID_EID) ok = false;
                    if (ok) return Vec2i{x, y};
                }
            return Vec2i{-1, -1};
        };

        Vec2i base = findOpen(12, 12);
        bool areaOk = base.x >= 0;
        check(areaOk, "playable-core scenario has an open test area");
        if (areaOk) {
            core.spawnBuilding(0, BldType::WarFactory, base.x, base.y, true);
            core.spawnBuilding(0, BldType::IndustrialPlant, base.x + 5, base.y, true);
            int discounted = core.unitProductionCost(0, UnitType::Grizzly);
            bool discountOk = discounted == unitDef(UnitType::Grizzly).cost * 75 / 100
                           && core.unitProductionCost(0, UnitType::Intruder) == unitDef(UnitType::Intruder).cost;

            // 配置化低电倍率：0.25 下跑过一半基础建造时间仍不应完成，取消精确返还实扣金额。
            float oldLow = g_gameRules.lowPowerSpeedFactor;
            g_gameRules.lowPowerSpeedFactor = 0.25f;
            core.players[0].money = 20000;
            int money0 = core.players[0].money;
            core.players[0].powerSabotage = 100000;
            bool started = core.startUnitProd(0, UnitType::Grizzly);
            for (int i = 0; i < unitDef(UnitType::Grizzly).buildTime * 2 + 5; i++) core.update();
            bool lowPowerConfigured = core.players[0].unitProd[1].active;
            core.cancelUnitProd(0, UnitType::Grizzly);
            bool refundExact = core.players[0].money == money0;
            core.players[0].powerSabotage = 0;
            g_gameRules.lowPowerSpeedFactor = oldLow;

            // 所有出口堵塞时完成项必须保留；清出一个出口后才真正产出。
            core.players[0].powerMade = 10000;
            core.players[0].powerUsed = 0;
            core.players[0].money = 20000;
            bool startedBlocked = core.startUnitProd(0, UnitType::Grizzly);
            ProdItem& blocked = core.players[0].unitProd[1];
            blocked.progress = unitDef(UnitType::Grizzly).buildTime - 1;
            blocked.paid = blocked.totalCost - 1;
            std::vector<EID> blockers;
            const BldDef& wf = bldDef(BldType::WarFactory);
            for (int r = 1; r < 8; r++) {
                int sx = base.x + wf.w / 2, sy = base.y + wf.h + r - 1;
                EID b = core.spawnUnit(0, UnitType::GI, sx + 0.5f, sy + 0.5f);
                core.ents[b].tpSick = 10000;
                blockers.push_back(b);
            }
            int tanks0 = core.countUnits(0, UnitType::Grizzly);
            core.update();
            bool blockedHeld = core.players[0].unitProd[1].active
                            && core.countUnits(0, UnitType::Grizzly) == tanks0;
            core.kill(blockers.front());
            core.update();
            bool exitReleased = !core.players[0].unitProd[1].active
                             && core.countUnits(0, UnitType::Grizzly) == tanks0 + 1;

            // 宝石卸载与矿石精炼器收益：70 × 1.25 = 87（整数规则）。
            Vec2i eco = findOpen(8, 6);
            core.spawnBuilding(0, BldType::OreRefinery, eco.x, eco.y, true);
            core.spawnBuilding(0, BldType::OrePurifier, eco.x + 4, eco.y, true);
            EID miner = core.spawnUnit(0, UnitType::ChronoMiner, eco.x + 1.5f, eco.y + 4.5f);
            core.ents[miner].state = UState::HarvestUnload;
            core.ents[miner].oreLoad = 1;
            core.ents[miner].gemLoad = 1;
            core.ents[miner].digTimer = 29;
            core.players[0].money = 0;
            core.update();
            bool gemPurifier = core.players[0].money == 87;

            // RA2 UI 保真：采矿容量 HARV=40 / CMIN=20；HOLD 暂停进度；缺钱停扣可续产
            bool harvCap = World::harvesterCapacity(UnitType::WarMiner) == 40
                        && World::harvesterCapacity(UnitType::ChronoMiner) == 20;
            core.players[0].money = 50000;
            bool holdStart = core.startUnitProd(0, UnitType::Grizzly);
            int holdProg0 = core.players[0].unitProd[1].progress;
            for (int i = 0; i < 10; i++) core.update();
            int holdProg1 = core.players[0].unitProd[1].progress;
            core.holdUnitProd(0, UnitType::Grizzly, true);
            for (int i = 0; i < 10; i++) core.update();
            int holdProg2 = core.players[0].unitProd[1].progress;
            bool holdFreezes = holdStart && holdProg1 > holdProg0 && holdProg2 == holdProg1
                            && core.players[0].unitProd[1].held;
            core.holdUnitProd(0, UnitType::Grizzly, false);
            core.cancelUnitProd(0, UnitType::Grizzly);
            core.players[0].money = 0;
            bool brokeStart = core.startUnitProd(0, UnitType::Grizzly);
            int brokeProg0 = core.players[0].unitProd[1].progress;
            int brokePaid0 = core.players[0].unitProd[1].paid;
            for (int i = 0; i < 20; i++) core.update();
            bool brokePaused = brokeStart && core.players[0].unitProd[1].progress == brokeProg0
                            && core.players[0].unitProd[1].paid == brokePaid0
                            && core.players[0].unitProd[1].active;
            core.players[0].money = 5000;
            for (int i = 0; i < 20; i++) core.update();
            bool brokeResumes = brokePaused && (core.players[0].unitProd[1].progress > brokeProg0
                            || core.players[0].unitProd[1].paid > brokePaid0);
            core.cancelUnitProd(0, UnitType::Grizzly);

            // 人类超时空命令携带稳定实体 ID；外玩家伪造 ID 被 applyCmd 过滤。
            Vec2i chrono = findOpen(8, 8);
            core.spawnBuilding(0, BldType::ChronoSphere, chrono.x, chrono.y, true);
            EID ownTank = core.spawnUnit(0, UnitType::Grizzly, chrono.x + 3.5f, chrono.y + 4.5f);
            EID foreignTank = core.spawnUnit(1, UnitType::LasherTank, chrono.x + 4.5f, chrono.y + 4.5f);
            float foreignX = core.ents[foreignTank].x, ownX = core.ents[ownTank].x;
            core.players[0].swReady[(int)SWType::ChronoShift] = true;
            World::Cmd chronoCmd; chronoCmd.type = World::Cmd::LaunchSW; chronoCmd.a = (int)SWType::ChronoShift;
            chronoCmd.ids = {ownTank, foreignTank}; chronoCmd.x = chrono.x + 7.5f; chronoCmd.y = chrono.y + 7.5f;
            core.applyCmd(0, chronoCmd);
            bool chronoDeterministic = core.ents[ownTank].x != ownX
                                    && core.ents[foreignTank].x == foreignX
                                    && !core.players[0].swReady[(int)SWType::ChronoShift];
            World::Cmd forged; forged.type = World::Cmd::Move; forged.ids = {foreignTank};
            forged.x = 1.5f; forged.y = 1.5f;
            UState foreignState = core.ents[foreignTank].state;
            core.applyCmd(0, forged);
            bool ownershipGuard = core.ents[foreignTank].state == foreignState;

            // 两座尤里超武建筑均可被间谍识别，且只重置被渗透的那一种。
            Vec2i spyArea = findOpen(10, 6);
            EID mut = core.spawnBuilding(1, BldType::GeneticMutator, spyArea.x, spyArea.y, true);
            EID dom = core.spawnBuilding(1, BldType::PsychicDominator, spyArea.x + 5, spyArea.y, true);
            core.players[1].swReady[(int)SWType::GeneticMutator] = true;
            core.players[1].swReady[(int)SWType::PsychicDominator] = true;
            EID spy1 = core.spawnUnit(0, UnitType::Spy, spyArea.x + 1.5f, spyArea.y + 2.5f);
            core.ents[spy1].invuln = 100;
            core.orderAttack({spy1}, mut);
            core.update();
            bool mutReset = !core.players[1].swReady[(int)SWType::GeneticMutator]
                         && core.players[1].swReady[(int)SWType::PsychicDominator];
            EID spy2 = core.spawnUnit(0, UnitType::Spy, spyArea.x + 6.5f, spyArea.y + 2.5f);
            core.ents[spy2].invuln = 100;
            core.orderAttack({spy2}, dom);
            core.update();
            bool domReset = !core.players[1].swReady[(int)SWType::PsychicDominator];
            TraceLog(LOG_INFO, "spy Yuri SW verify: mutReset=%d domStillReady=%d domReset=%d",
                     (int)!core.players[1].swReady[(int)SWType::GeneticMutator],
                     (int)core.players[1].swReady[(int)SWType::PsychicDominator], (int)domReset);

            // 规则化补给箱间隔、矿再生量和军衔伤害倍率。
            int oldCrate = g_gameRules.crateInterval, oldRegrow = g_gameRules.oreRegrowRate;
            float oldVet = g_gameRules.veteranismDmgBonus[1];
            g_gameRules.crateInterval = 2;
            core.crates.clear(); core.tick = 1; core.update();
            bool crateConfigured = !core.crates.empty();
            g_gameRules.oreRegrowRate = 3;
            for (Cell& c : core.map.cells) { c.terrain = Terrain::Rough; c.ore = 0; c.oreMax = 10; }
            core.tick = 119; core.update();
            bool regrowConfigured = false;
            for (const Cell& c : core.map.cells) if (c.ore == 3) { regrowConfigured = true; break; }
            g_gameRules.veteranismDmgBonus[1] = 2.0f;
            EID vet = core.spawnUnit(0, UnitType::GI, base.x + 10.5f, base.y + 10.5f);
            core.ents[vet].vetRank = 1;
            bool vetConfigured = core.effWeapon(core.ents[vet]).damage == unitDef(UnitType::GI).weapon.damage * 2;
            g_gameRules.crateInterval = oldCrate;
            g_gameRules.oreRegrowRate = oldRegrow;
            g_gameRules.veteranismDmgBonus[1] = oldVet;

            check(discountOk, "industrial plant discounts vehicle cost without affecting aircraft");
            check(started && lowPowerConfigured, "configured low-power factor slows production");
            check(refundExact, "production cancellation refunds exactly the amount paid");
            check(startedBlocked && blockedHeld, "blocked factory exit retains the completed unit");
            check(exitReleased, "factory produces after an exit becomes available");
            check(gemPurifier, "gems and ore purifier apply their combined income");
            check(harvCap, "harvester capacity is HARV40/CMIN20");
            check(holdFreezes, "production HOLD freezes progress");
            check(brokeStart && brokePaused && brokeResumes, "broke production pauses and resumes with funds");
            check(chronoDeterministic, "chrono shift uses deterministic selected source units");
            check(ownershipGuard, "commands cannot control another player's entities");
            check(mutReset && domReset, "spies reset both Yuri superweapon types independently");
            check(crateConfigured, "crate interval follows GameRules");
            check(regrowConfigured, "ore regrowth rate follows GameRules");
            check(vetConfigured, "veteran damage bonus follows GameRules");
        }
    }
    // ---- official-mechanics：军衔、YR 标志单位/建筑与规则矩阵 ----
    {
        World mech;
        mech.init(64, 64, 0x1006'1001, 2, 0, {Faction::Allies, Faction::Yuri});
        for (Cell& c : mech.map.cells) {
            if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
        }
        auto open = [&](int rw, int rh) {
            for (int y = 6; y + rh < mech.map.h - 6; ++y)
                for (int x = 6; x + rw < mech.map.w - 6; ++x) {
                    bool ok = true;
                    for (int dy = 0; dy < rh && ok; ++dy)
                        for (int dx = 0; dx < rw && ok; ++dx)
                            if (!mech.map.passable(x + dx, y + dy) || mech.bldBlocked(x + dx, y + dy)
                                || mech.unitAtCell(x + dx, y + dy) != INVALID_EID) ok = false;
                    if (ok) return Vec2i{x, y};
                }
            return Vec2i{-1, -1};
        };

        Vec2i a = open(12, 12);
        bool mechanicsArea = a.x >= 0;
        check(mechanicsArea, "official mechanics scenario has an open test area");
        if (mechanicsArea) {
            // 价值经验：700 价值足以让 200 价值 GI 达到 3× 门槛，而不是依赖击杀数。
            EID veteran = mech.spawnUnit(0, UnitType::GI, a.x + .5f, a.y + .5f);
            EID valuable = mech.spawnUnit(1, UnitType::Grizzly, a.x + 2.5f, a.y + .5f);
            mech.damage(valuable, 100000, 0, veteran);
            bool valueVeteran = mech.valid(veteran) && mech.ents[veteran].kills == 1
                              && mech.ents[veteran].veterancyValue == unitDef(UnitType::Grizzly).cost
                              && mech.ents[veteran].vetRank == 1;
            EID secondValue = mech.spawnUnit(1, UnitType::Rhino, a.x + 3.5f, a.y + .5f);
            mech.damage(secondValue, 100000, 0, veteran);
            bool valueElite = mech.ents[veteran].vetRank == 2;
            int hp0 = mech.ents[veteran].hp;
            mech.damage(veteran, 100, 1);
            bool eliteArmor = hp0 - mech.ents[veteran].hp
                            == (int)(100 * g_gameRules.veteranArmorBonus[2]);
            bool rankBonuses = mech.effWeapon(mech.ents[veteran]).cooldown
                             <= unitDef(UnitType::GI).weapon.cooldown * g_gameRules.veteranRofBonus[2] + 1
                             && g_gameRules.veteranSpeedBonus[2] > 1.0f
                             && g_gameRules.veteranSelfHeal[2] > 0;

            // 英雄唯一：在场英雄阻止生产；YR 英雄免疫普通和超级心控。
            Vec2i heroBase = open(8, 8);
            mech.spawnBuilding(0, BldType::Barracks, heroBase.x, heroBase.y, true);
            mech.spawnBuilding(0, BldType::BattleLab, heroBase.x + 3, heroBase.y, true);
            EID tanya = mech.spawnUnit(0, UnitType::Tanya, heroBase.x + .5f, heroBase.y + 4.5f);
            bool heroUnique = !mech.startUnitProd(0, UnitType::Tanya);
            EID dominator = mech.spawnBuilding(1, BldType::PsychicDominator, heroBase.x + 7, heroBase.y, true);
            (void)dominator;
            int tanyaOwner = mech.ents[tanya].player;
            mech.players[1].swReady[(int)SWType::PsychicDominator] = true;
            mech.launchSW(1, SWType::PsychicDominator, mech.ents[tanya].x, mech.ents[tanya].y);
            bool heroImmune = mech.valid(tanya) && mech.ents[tanya].player == tanyaOwner;

            // 生化反应堆驻军增电；撤出时保存生命与军衔。
            Vec2i support = open(10, 8);
            EID bio = mech.spawnBuilding(1, BldType::BioReactor, support.x, support.y, true);
            int basePower = mech.players[1].powerMade;
            mech.ents[bio].garrison.push_back({UnitType::Initiate, 61, 2, 700, 1});
            mech.update();
            bool bioPower = mech.players[1].powerMade >= basePower + g_gameRules.bioReactorPowerPerOccupant;
            mech.orderUngarrison({bio});
            bool garrisonState = false;
            for (const World::Ent& e : mech.ents)
                if (e.alive && !e.isBuilding && e.player == 1 && e.utype == UnitType::Initiate
                    && e.hp == 61 && e.vetRank == 1 && e.veterancyValue == 700) garrisonState = true;

            // 回收炉：己方非英雄进入后按规则返还价值。
            EID grinder = mech.spawnBuilding(1, BldType::Grinder, support.x + 4, support.y, true);
            EID recycle = mech.spawnUnit(1, UnitType::LasherTank, support.x + 4.5f, support.y + 3.5f);
            int moneyBefore = mech.players[1].money;
            mech.orderGarrison({recycle}, grinder);
            for (int i = 0; i < 120 && mech.valid(recycle); ++i) mech.update();
            bool grinderRefund = !mech.valid(recycle)
                              && mech.players[1].money >= moneyBefore
                                  + (int)(unitDef(UnitType::LasherTank).cost * g_gameRules.grinderRefund);

            // 磁电举升/断束坠落。
            Vec2i special = open(14, 10);
            EID magnet = mech.spawnUnit(1, UnitType::Magnetron, special.x + .5f, special.y + .5f);
            EID lifted = mech.spawnUnit(0, UnitType::Grizzly, special.x + 2.5f, special.y + .5f);
            mech.ents[magnet].target = lifted; mech.ents[magnet].state = UState::Attacking; mech.ents[magnet].atkCd = 0;
            mech.update(); mech.update(); // free-list ID 顺序可能令目标先于施术者更新，第二帧进入悬空
            bool magnetLift = mech.valid(lifted) && mech.ents[lifted].magneticBy == magnet
                           && mech.ents[lifted].magneticHeight > 0;
            int liftedHp = mech.ents[lifted].hp;
            mech.ents[magnet].target = INVALID_EID;
            mech.ents[magnet].tpSick = 10;
            mech.update();
            bool magnetDrop = mech.valid(lifted) && mech.ents[lifted].magneticBy == INVALID_EID
                           && mech.ents[lifted].hp < liftedHp;

            // 主脑前三目标稳定，第四目标触发过载。
            EID master = mech.spawnUnit(1, UnitType::MasterMind, special.x + .5f, special.y + 3.5f);
            std::vector<EID> controlled;
            for (int i = 0; i < 4; ++i) {
                EID victim = mech.spawnUnit(0, UnitType::Conscript, special.x + 2.5f + i, special.y + 3.5f);
                mech.ents[master].target = victim; mech.ents[master].state = UState::Attacking; mech.ents[master].atkCd = 0;
                mech.update();
                controlled.push_back(victim);
            }
            bool multiControl = mech.ents[master].mindTargets.size() == 4;
            int masterHp = mech.ents[master].hp;
            mech.tick = (uint64_t)((master % 30 + 29) % 30);
            mech.update();
            bool overload = mech.valid(master) && mech.ents[master].hp < masterHp;

            // 飞碟对电厂吸电、对工厂停产。
            Vec2i drain = open(12, 8);
            EID disc = mech.spawnUnit(1, UnitType::FloatingDisc, drain.x + 2.5f, drain.y + 2.5f);
            EID plant = mech.spawnBuilding(0, BldType::PowerPlant, drain.x, drain.y, true);
            int powered = mech.players[0].powerMade;
            mech.ents[disc].target = plant; mech.ents[disc].state = UState::Attacking;
            mech.update(); mech.update();
            bool discDrain = mech.ents[plant].drainedBy == disc && mech.players[0].powerMade < powered;
            EID drainedFactory = mech.spawnBuilding(0, BldType::WarFactory, drain.x + 5, drain.y, true);
            mech.players[0].money = 10000;
            bool drainProdStarted = mech.startUnitProd(0, UnitType::Grizzly);
            mech.ents[disc].target = drainedFactory; mech.ents[disc].state = UState::Attacking;
            mech.update(); // 飞碟先建立吸取链接，下一帧生产队列应暂停
            int drainProgress = mech.players[0].unitProd[1].progress;
            mech.update(); mech.update();
            bool discStopsFactory = drainProdStarted && mech.ents[drainedFactory].drainedBy == disc
                                  && mech.players[0].unitProd[1].progress == drainProgress;
            // 飞碟悬停精炼厂偷钱
            EID refSteal = mech.spawnBuilding(0, BldType::OreRefinery, drain.x, drain.y + 5, true);
            mech.players[0].money = 5000;
            mech.players[1].money = 0;
            mech.ents[disc].target = refSteal; mech.ents[disc].state = UState::Attacking;
            for (int i = 0; i < 40; ++i) mech.update();
            bool discSteal = mech.players[0].money < 5000 && mech.players[1].money > 0;

            // 尤里奴隶矿车部署产奴 + 可采矿
            World yuriEco;
            yuriEco.init(48, 48, 77, 1, 0, {Faction::Yuri}, 0);
            yuriEco.players[0].faction = Faction::Yuri;
            yuriEco.players[0].money = 20000;
            for (Cell& c : yuriEco.map.cells)
                if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
            auto openY = [&](int rw, int rh) {
                for (int y = 6; y + rh < yuriEco.map.h - 6; ++y)
                    for (int x = 6; x + rw < yuriEco.map.w - 6; ++x) {
                        bool ok = true;
                        for (int dy = 0; dy < rh && ok; ++dy)
                            for (int dx = 0; dx < rw && ok; ++dx)
                                if (!yuriEco.map.passable(x + dx, y + dy) || yuriEco.bldBlocked(x + dx, y + dy)
                                    || yuriEco.unitAtCell(x + dx, y + dy) != INVALID_EID) ok = false;
                        if (ok) return Vec2i{x, y};
                    }
                return Vec2i{-1, -1};
            };
            Vec2i ye = openY(8, 6);
            EID sm = yuriEco.spawnUnit(0, UnitType::SlaveMiner, ye.x + 0.5f, ye.y + 0.5f);
            yuriEco.orderDeploy(sm);
            yuriEco.spawnBuilding(0, BldType::OreRefinery, ye.x + 3, ye.y, true);
            for (int i = 0; i < 200; ++i) yuriEco.update();
            int slaveN = 0;
            for (const auto& e : yuriEco.ents)
                if (e.alive && e.utype == UnitType::Slave) slaveN++;
            bool slaveEconomy = yuriEco.valid(sm) && yuriEco.ents[sm].deployed && slaveN > 0
                             && harvesterType(Faction::Yuri) == UnitType::SlaveMiner;

            // MCV Repacks：建造厂可打包回 MCV
            World pack;
            pack.init(40, 40, 88, 1, 0, {Faction::Allies}, 0);
            pack.mcvRepacks = true;
            for (Cell& c : pack.map.cells)
                if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
            auto openP = [&](int rw, int rh) {
                for (int y = 6; y + rh < pack.map.h - 6; ++y)
                    for (int x = 6; x + rw < pack.map.w - 6; ++x) {
                        bool ok = true;
                        for (int dy = 0; dy < rh && ok; ++dy)
                            for (int dx = 0; dx < rw && ok; ++dx)
                                if (!pack.map.passable(x + dx, y + dy) || pack.bldBlocked(x + dx, y + dy)
                                    || pack.unitAtCell(x + dx, y + dy) != INVALID_EID) ok = false;
                        if (ok) return Vec2i{x, y};
                    }
                return Vec2i{-1, -1};
            };
            Vec2i pb = openP(4, 4);
            EID cy = pack.spawnBuilding(0, BldType::ConYard, pb.x, pb.y, true);
            pack.orderDeploy(cy);
            // spawnUnit may reuse the freed ConYard slot — accept either recycled MCV or a new one
            int mcvN = 0;
            bool stillConYard = false;
            for (const auto& e : pack.ents) {
                if (!e.alive) continue;
                if (e.utype == UnitType::MCV && !e.isBuilding) mcvN++;
                if (e.isBuilding && e.btype == BldType::ConYard) stillConYard = true;
            }
            bool mcvPack = mcvN >= 1 && !stillConYard;

            // 基洛夫空艇：自身炸弹为地面爆炸，不应误伤空中单位
            World airDmg;
            airDmg.init(32, 32, 91, 2, 0, {Faction::Soviet, Faction::Allies}, 0);
            for (Cell& c : airDmg.map.cells)
                if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
            EID kirov = airDmg.spawnUnit(0, UnitType::Kirov, 10.5f, 10.5f);
            EID ground = airDmg.spawnUnit(1, UnitType::GI, 10.5f, 10.5f);
            int kirovHp0 = airDmg.ents[kirov].hp;
            int groundHp0 = airDmg.ents[ground].hp;
            World::TimedBomb bomb;
            bomb.x = 10.5f; bomb.y = 10.5f; bomb.player = 0;
            bomb.dmg = 300; bomb.radius = 2.0f; bomb.timer = 1; bomb.rockVehicles = true;
            airDmg.timedBombs.push_back(bomb);
            airDmg.update();
            bool kirovSafeBomb = airDmg.valid(kirov) && airDmg.ents[kirov].hp == kirovHp0
                              && (!airDmg.valid(ground) || airDmg.ents[ground].hp < groundHp0);

            bool rifleWarhead = unitDef(UnitType::GI).weapon.warhead == WeaponDef::Warhead::SmallArms
                             && unitDef(UnitType::Grizzly).weapon.warhead == WeaponDef::Warhead::AP;
            EID gat = mech.spawnUnit(1, UnitType::GatlingTank, drain.x + 5.5f, drain.y + 4.5f);
            EID gatTarget = mech.spawnUnit(0, UnitType::Apocalypse, drain.x + 7.5f, drain.y + 4.5f);
            mech.ents[gat].invuln = 1000;
            mech.ents[gat].target = gatTarget; mech.ents[gat].state = UState::Attacking;
            for (int i = 0; i < 240 && mech.valid(gatTarget); ++i) mech.update();
            int hotStage = mech.ents[gat].gatlingStage;
            mech.ents[gat].target = INVALID_EID; mech.ents[gat].state = UState::Idle;
            mech.ents[gat].tpSick = 200;
            for (int i = 0; i < 100; ++i) mech.update();
            bool gatlingStages = hotStage > 0 && mech.ents[gat].gatlingStage == 0;

            // IFV 乘员切换与光棱坦克邻近折射。
            EID ifv = mech.spawnUnit(0, UnitType::IFV, drain.x + 9.5f, drain.y + 6.5f);
            mech.ents[ifv].cargo.push_back({UnitType::Sniper, unitDef(UnitType::Sniper).hp, 0, 0, 0});
            bool ifvPassenger = mech.effWeapon(mech.ents[ifv]).range > unitDef(UnitType::IFV).weapon.range;
            EID prism = mech.spawnUnit(0, UnitType::PrismTank, drain.x + 9.5f, drain.y + 3.5f);
            EID prismMain = mech.spawnUnit(1, UnitType::LasherTank, drain.x + 11.5f, drain.y + 3.5f);
            EID prismChain = mech.spawnUnit(1, UnitType::LasherTank, drain.x + 11.5f, drain.y + 4.5f);
            int chainHp = mech.ents[prismChain].hp;
            mech.ents[prism].invuln = 100;
            mech.ents[prism].target = prismMain; mech.ents[prism].state = UState::Attacking; mech.ents[prism].atkCd = 0;
            for (int i = 0; i < 20 && mech.ents[prismChain].hp == chainHp; ++i) mech.update();
            bool prismRefraction = mech.valid(prismChain) && mech.ents[prismChain].hp < chainHp;

            // 炮塔坦克攻击移动：射程内边走边打（车体沿路径，炮塔可独立朝向）
            Vec2i drive = open(16, 6);
            bool turretMoveFire = false;
            if (drive.x >= 0) {
                EID shooter = mech.spawnUnit(0, UnitType::Grizzly, drive.x + .5f, drive.y + 2.5f);
                EID victim = mech.spawnUnit(1, UnitType::Conscript, drive.x + 5.5f, drive.y + 2.5f);
                int vicHp = mech.ents[victim].hp;
                float startX = mech.ents[shooter].x;
                mech.ents[shooter].atkCd = 0;
                mech.orderMove({shooter}, drive.x + 14.5f, drive.y + 2.5f, true);
                for (int i = 0; i < 240 && mech.valid(shooter) && mech.valid(victim); ++i) {
                    mech.update();
                    const World::Ent& s = mech.ents[shooter];
                    if (s.x > startX + 0.4f && mech.ents[victim].hp < vicHp) {
                        turretMoveFire = true;
                        break;
                    }
                }
            }

            WeaponDef matrixWeapon;
            matrixWeapon.warhead = WeaponDef::Warhead::AP;
            bool armorMatrix = weaponVsArmor(matrixWeapon, Armor::Heavy, false, false)
                             == g_gameRules.warheadVerses[(int)WeaponDef::Warhead::AP][(int)Armor::Heavy]
                             && weaponVsArmor(WeaponDef{}, Armor::Heavy, false, false) == 1.0f;

            // ---- M2 YR 补洞：Force Shield / RCC / 克隆 / 心灵塔帽 / 限一 / 油田奖金 ----
            Vec2i m2 = open(18, 8);
            bool forceShieldOk = false, rccOk = false, cloningYuriOk = false;
            bool psychicCapOk = false, uniqueBldOk = false, oilBonusOk = false;
            if (m2.x >= 0) {
                // Force Shield：友方建筑无敌，单位不受；发动后断电
                EID lab = mech.spawnBuilding(0, BldType::BattleLab, m2.x, m2.y, true);
                EID shieldBld = mech.spawnBuilding(0, BldType::PowerPlant, m2.x + 3, m2.y, true);
                EID shieldUnit = mech.spawnUnit(0, UnitType::Grizzly, m2.x + 3.5f, m2.y + 3.5f);
                (void)lab;
                mech.players[0].swReady[(int)SWType::ForceShield] = true;
                bool fsLaunch = mech.launchSW(0, SWType::ForceShield, m2.x + 3.5f, m2.y + 1.0f);
                forceShieldOk = fsLaunch && mech.ents[shieldBld].invuln >= 1000
                             && mech.ents[shieldUnit].invuln == 0
                             && mech.players[0].powerSabotage >= 2000;
                mech.players[0].powerSabotage = 0;

                // Robot Control：无 RCC 不可造遥控坦克；有 RCC 且通电可动，断电停摆
                mech.spawnBuilding(0, BldType::PowerPlant, m2.x + 6, m2.y + 3, true);
                mech.spawnBuilding(0, BldType::PowerPlant, m2.x + 9, m2.y + 3, true);
                mech.spawnBuilding(0, BldType::WarFactory, m2.x + 6, m2.y, true);
                mech.players[0].money = 20000;
                mech.players[0].powerSabotage = 0;
                bool noRccBlocked = !mech.startUnitProd(0, UnitType::RobotTank);
                EID rcc = mech.spawnBuilding(0, BldType::RobotControl, m2.x + 10, m2.y, true);
                (void)rcc;
                mech.update(); // 重算电力
                bool rccUnlock = mech.unitPrereqMet(0, unitDef(UnitType::RobotTank)) && !mech.players[0].lowPower();
                EID robot = mech.spawnUnit(0, UnitType::RobotTank, m2.x + 12.5f, m2.y + 4.5f);
                mech.orderMove({robot}, m2.x + 16.5f, m2.y + 4.5f, false);
                for (int i = 0; i < 5; ++i) mech.update();
                bool movingOnline = mech.valid(robot)
                    && (mech.ents[robot].state == UState::Moving || !mech.ents[robot].path.empty());
                mech.players[0].powerSabotage = 100000;
                mech.update();
                bool frozenOffline = mech.valid(robot)
                    && mech.ents[robot].state == UState::Idle && mech.ents[robot].path.empty();
                mech.players[0].powerSabotage = 0;
                rccOk = noRccBlocked && rccUnlock && movingOnline && frozenOffline;

                // CloningVat 归尤里：苏军不可造，尤里可造
                World cv;
                cv.init(48, 48, 88, 2, 0, {Faction::Soviet, Faction::Yuri}, 0);
                for (Cell& c : cv.map.cells)
                    if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                Vec2i cvb{-1, -1};
                for (int y = 6; y + 6 < cv.map.h - 6 && cvb.x < 0; ++y)
                    for (int x = 6; x + 6 < cv.map.w - 6 && cvb.x < 0; ++x) {
                        bool ok = true;
                        for (int dy = 0; dy < 6 && ok; ++dy)
                            for (int dx = 0; dx < 6 && ok; ++dx)
                                if (!cv.map.passable(x + dx, y + dy) || cv.bldBlocked(x + dx, y + dy)) ok = false;
                        if (ok) cvb = {x, y};
                    }
                if (cvb.x >= 0) {
                    cv.spawnBuilding(0, BldType::ConYard, cvb.x, cvb.y, true);
                    cv.spawnBuilding(0, BldType::Radar, cvb.x + 4, cvb.y, true);
                    cv.spawnBuilding(0, BldType::BattleLab, cvb.x + 4, cvb.y + 3, true);
                    cv.spawnBuilding(1, BldType::ConYard, cvb.x, cvb.y + 6, true);
                    cv.spawnBuilding(1, BldType::Radar, cvb.x + 4, cvb.y + 6, true);
                    cv.spawnBuilding(1, BldType::BattleLab, cvb.x + 4, cvb.y + 9, true);
                    cv.players[0].money = cv.players[1].money = 20000;
                    cloningYuriOk = !cv.startBldProd(0, BldType::CloningVat)
                                 && cv.startBldProd(1, BldType::CloningVat);
                }

                // Psychic Tower 硬顶 3
                mech.spawnBuilding(1, BldType::BioReactor, m2.x - 3, m2.y + 6, true);
                mech.spawnBuilding(1, BldType::BioReactor, m2.x - 3, m2.y + 9, true);
                EID tower = mech.spawnBuilding(1, BldType::PsychicTower, m2.x, m2.y + 6, true);
                for (int i = 0; i < 4; ++i) {
                    EID victim = mech.spawnUnit(0, UnitType::Conscript, m2.x + 2.5f + (float)i, m2.y + 6.5f);
                    mech.ents[tower].target = victim;
                    mech.ents[tower].atkCd = 0;
                    mech.update();
                }
                psychicCapOk = mech.ents[tower].mindTargets.size() == 3;

                // 限一：已有精炼器时不可再开工
                mech.spawnBuilding(0, BldType::ConYard, m2.x + 6, m2.y + 6, true);
                mech.spawnBuilding(0, BldType::OrePurifier, m2.x + 10, m2.y + 6, true);
                mech.players[0].money = 20000;
                uniqueBldOk = !mech.startBldProd(0, BldType::OrePurifier);

                // 油田占领瞬间 +1000
                EID oil = mech.spawnBuilding(-1, BldType::OilDerrick, m2.x, m2.y + 10, true);
                EID eng = mech.spawnUnit(0, UnitType::Engineer, m2.x + 2.5f, m2.y + 10.5f);
                int oilMoney0 = mech.players[0].money;
                mech.orderCapture({eng}, oil);
                for (int i = 0; i < 800 && mech.valid(oil) && mech.ents[oil].player != 0; ++i) mech.update();
                oilBonusOk = mech.valid(oil) && mech.ents[oil].player == 0
                          && mech.players[0].money >= oilMoney0 + 1000;
            }

            // ---- B5/B6/B7：YuriPrime / Genetic Mutator 矩阵 / Spy Plane·Psychic Reveal ----
            bool yuriPrimeOk = false, mutatorMatrixOk = false, spyRevealOk = false;
            {
                World yr;
                yr.init(48, 48, 0xB5B6B7u, 2, 0, {Faction::Yuri, Faction::Soviet}, 0);
                for (Cell& c : yr.map.cells)
                    if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                Vec2i b{-1, -1};
                for (int y = 6; y + 8 < yr.map.h - 6 && b.x < 0; ++y)
                    for (int x = 6; x + 10 < yr.map.w - 6 && b.x < 0; ++x) {
                        bool ok = true;
                        for (int dy = 0; dy < 8 && ok; ++dy)
                            for (int dx = 0; dx < 10 && ok; ++dx)
                                if (!yr.map.passable(x + dx, y + dy) || yr.bldBlocked(x + dx, y + dy)) ok = false;
                        if (ok) b = {x, y};
                    }
                if (b.x >= 0) {
                    yr.spawnBuilding(0, BldType::Barracks, b.x, b.y, true);
                    yr.spawnBuilding(0, BldType::BattleLab, b.x + 3, b.y, true);
                    yr.spawnBuilding(0, BldType::GeneticMutator, b.x + 6, b.y, true);
                    yr.spawnBuilding(0, BldType::PsychicSensor, b.x, b.y + 3, true);
                    yr.spawnBuilding(1, BldType::PowerPlant, b.x + 3, b.y + 3, true);
                    yr.spawnBuilding(1, BldType::Radar, b.x + 6, b.y + 3, true);
                    yr.players[0].money = yr.players[1].money = 20000;

                    // B5：尤里首脑唯一 + 可控敌方建筑
                    EID prime = yr.spawnUnit(0, UnitType::YuriPrime, b.x + 0.5f, b.y + 6.5f);
                    bool primeUnique = !yr.startUnitProd(0, UnitType::YuriPrime);
                    EID enemyBld = INVALID_EID;
                    for (size_t i = 0; i < yr.ents.size(); ++i)
                        if (yr.ents[i].alive && yr.ents[i].isBuilding && yr.ents[i].player == 1
                            && yr.ents[i].btype == BldType::PowerPlant) { enemyBld = (int)i; break; }
                    yr.ents[prime].atkCd = 0;
                    yr.orderAttack({prime}, enemyBld);
                    for (int i = 0; i < 400 && yr.valid(enemyBld) && yr.ents[enemyBld].player == 1; ++i) yr.update();
                    yuriPrimeOk = primeUnique && yr.valid(enemyBld) && yr.ents[enemyBld].player == 0;

                    // B6：基因突变器——英雄杀、普通步兵变狂兽人
                    EID dog = yr.spawnUnit(1, UnitType::AttackDog, b.x + 1.5f, b.y + 7.5f);
                    EID gi = yr.spawnUnit(1, UnitType::GI, b.x + 2.5f, b.y + 7.5f);
                    EID hero = yr.spawnUnit(1, UnitType::Tanya, b.x + 3.5f, b.y + 7.5f);
                    float mx = b.x + 2.5f, my = b.y + 7.5f;
                    yr.players[0].swReady[(int)SWType::GeneticMutator] = true;
                    bool mutLaunched = yr.launchSW(0, SWType::GeneticMutator, mx, my);
                    // kill() 会把 EID 回收进 freeList，spawnUnit 可能复用旧 ID，故不能只看 valid(eid)
                    bool dogAlive = false, heroAlive = false, giAlive = false, bruteSpawned = false;
                    for (const auto& e : yr.ents) {
                        if (!e.alive || e.isBuilding) continue;
                        if (e.utype == UnitType::AttackDog && e.player == 1) dogAlive = true;
                        if (e.utype == UnitType::Tanya && e.player == 1) heroAlive = true;
                        if (e.utype == UnitType::GI && e.player == 1) giAlive = true;
                        if (e.utype == UnitType::Brute && e.player == 0) bruteSpawned = true;
                    }
                    (void)dog; (void)gi; (void)hero;
                    mutatorMatrixOk = mutLaunched && !dogAlive && !heroAlive && !giAlive && bruteSpawned;

                    // B7：尤里心灵揭示；苏军侦察机（共用充能槽）
                    yr.players[0].paradropReady = true;
                    yr.players[0].paradropCharge = World::PARADROP_TIME;
                    bool revealSrc = yr.hasPsychicRevealSource(0);
                    int fogX = b.x + 20, fogY = b.y + 1;
                    if (fogX >= yr.map.w - 2) fogX = yr.map.w / 2;
                    if (!yr.map.fog.empty() && fogX < yr.map.w && fogY < yr.map.h)
                        yr.map.fog[0][(size_t)fogY * yr.map.w + fogX] = FOG_UNSEEN;
                    yr.orderPsychicReveal(0, fogX + 0.5f, fogY + 0.5f);
                    bool revealed = revealSrc && !yr.players[0].paradropReady
                                 && yr.map.fogAt(0, fogX, fogY) != FOG_UNSEEN;

                    yr.players[1].paradropReady = true;
                    yr.players[1].paradropCharge = World::PARADROP_TIME;
                    bool spySrc = yr.hasSpyPlaneSource(1);
                    int stripY = b.y + 4;
                    int stripX = std::min(b.x + 12, yr.map.w - 3);
                    if (!yr.map.fog.empty() && stripX < yr.map.w && stripY < yr.map.h)
                        yr.map.fog[1][(size_t)stripY * yr.map.w + stripX] = FOG_UNSEEN;
                    yr.orderSpyPlane(1, stripX + 0.5f, stripY + 0.5f);
                    bool spied = spySrc && !yr.players[1].paradropReady
                              && yr.map.fogAt(1, stripX, stripY) != FOG_UNSEEN;
                    spyRevealOk = revealed && spied;
                }
            }

            // ---- M3/B8：国家科技树 / 心控 MCV / 核电熔毁 / 超武冷却 ----
            bool countryTechOk = false, permaMcvOk = false, meltdownOk = false, swChargeOk = false;
            bool paradropCountOk = false, cloningDupOk = false, v3DeployOk = false, yuriFactionOk = false;
            bool spyDisguiseOk = false;
            {
                World ct;
                ct.init(48, 48, 0x00330001u, 2, 0, {Faction::Allies, Faction::Soviet}, 0);
                for (Cell& c : ct.map.cells)
                    if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                Vec2i b{-1, -1};
                for (int y = 6; y + 8 < ct.map.h - 6 && b.x < 0; ++y)
                    for (int x = 6; x + 10 < ct.map.w - 6 && b.x < 0; ++x) {
                        bool ok = true;
                        for (int dy = 0; dy < 8 && ok; ++dy)
                            for (int dx = 0; dx < 10 && ok; ++dx)
                                if (!ct.map.passable(x + dx, y + dy) || ct.bldBlocked(x + dx, y + dy)) ok = false;
                        if (ok) b = {x, y};
                    }
                if (b.x >= 0) {
                    ct.spawnBuilding(0, BldType::Barracks, b.x, b.y, true);
                    ct.spawnBuilding(0, BldType::AirForceCmd, b.x + 3, b.y, true);
                    ct.spawnBuilding(0, BldType::BattleLab, b.x + 6, b.y, true);
                    ct.spawnBuilding(0, BldType::WarFactory, b.x, b.y + 3, true);
                    ct.players[0].money = 50000;
                    ct.players[0].country = Country::UK;
                    bool ukSniper = ct.unitPrereqMet(0, unitDef(UnitType::Sniper));
                    ct.players[0].country = Country::America;
                    bool usNoSniper = !ct.unitPrereqMet(0, unitDef(UnitType::Sniper));
                    bool usIntruder = ct.unitPrereqMet(0, unitDef(UnitType::Intruder));
                    ct.players[0].country = Country::Korea;
                    bool krNoHarrier = !ct.unitPrereqMet(0, unitDef(UnitType::Intruder));
                    bool krEagle = ct.unitPrereqMet(0, unitDef(UnitType::BlackEagle));
                    ct.players[0].country = Country::France;
                    bool frCannon = ct.prereqMet(0, bldDef(BldType::GrandCannon));
                    // 共和国之辉中国：空指部下可造入侵者+黑鹰
                    World cn;
                    cn.init(48, 48, 0x00330021u, 2, 0, {Faction::China, Faction::Allies}, 0);
                    for (Cell& c : cn.map.cells)
                        if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                    Vec2i cb{-1, -1};
                    for (int y = 6; y + 8 < cn.map.h - 6 && cb.x < 0; ++y)
                        for (int x = 6; x + 10 < cn.map.w - 6 && cb.x < 0; ++x) {
                            bool ok = true;
                            for (int dy = 0; dy < 8 && ok; ++dy)
                                for (int dx = 0; dx < 10 && ok; ++dx)
                                    if (!cn.map.passable(x + dx, y + dy) || cn.bldBlocked(x + dx, y + dy)) ok = false;
                            if (ok) cb = {x, y};
                        }
                    bool chinaAir = false;
                    if (cb.x >= 0) {
                        cn.spawnBuilding(0, BldType::AirForceCmd, cb.x, cb.y, true);
                        chinaAir = cn.unitPrereqMet(0, unitDef(UnitType::Intruder))
                                && cn.unitPrereqMet(0, unitDef(UnitType::BlackEagle))
                                && cn.hasFactoryFor(0, unitDef(UnitType::Intruder));
                    }
                    // 尤里可造心灵探测器；苏军不可（看 factionMask + prereqMet）
                    bool yuriSensor = (bldDef(BldType::PsychicSensor).factionMask & (1 << (int)Faction::Yuri)) != 0
                                   && (bldDef(BldType::PsychicSensor).factionMask & (1 << (int)Faction::Soviet)) == 0;
                    bool radarSovietOnly = (bldDef(BldType::Radar).factionMask & (1 << (int)Faction::Soviet)) != 0
                                        && (bldDef(BldType::Radar).factionMask & (1 << (int)Faction::Allies)) == 0;
                    countryTechOk = ukSniper && usNoSniper && usIntruder && krNoHarrier && krEagle && frCannon
                                 && yuriSensor && radarSovietOnly && chinaAir;

                    // 永久心控 MCV 不可展开
                    EID mcv = ct.spawnUnit(0, UnitType::MCV, b.x + 8.5f, b.y + 5.5f);
                    ct.ents[mcv].permaControlled = true;
                    ct.orderDeploy(mcv);
                    permaMcvOk = ct.valid(mcv) && !ct.ents[mcv].isBuilding;

                    // 核电熔毁：用坦克承伤
                    EID nuke = ct.spawnBuilding(0, BldType::NuclearReactor, b.x + 3, b.y + 6, true);
                    EID vic = ct.spawnUnit(1, UnitType::Rhino, b.x + 5.5f, b.y + 9.5f);
                    bool nukeOk = ct.valid(nuke) && ct.ents[nuke].btype == BldType::NuclearReactor;
                    int meltHp0 = nukeOk && ct.valid(vic) ? ct.ents[vic].hp : -1;
                    if (nukeOk) ct.kill(nuke);
                    meltdownOk = nukeOk && meltHp0 > 0 && (!ct.valid(vic) || ct.ents[vic].hp < meltHp0);

                    swChargeOk = swDef(SWType::Nuke).chargeTime == 30 * 60 * 10
                              && swDef(SWType::IronCurtain).chargeTime == 30 * 60 * 5
                              && swDef(SWType::ChronoShift).chargeTime == 30 * 60 * 7
                              && swDef(SWType::ForceShield).chargeTime == 30 * 60 * 5;

                    // 伞兵编制：美国空指 8；科技机场盟军 6 / 苏军 9
                    auto countInf = [](World& w, int pl, UnitType t) {
                        int n = 0;
                        for (size_t i = 0; i < w.ents.size(); ++i) {
                            const World::Ent& e = w.ents[i];
                            if (e.alive && !e.isBuilding && e.player == pl && e.utype == t) n++;
                        }
                        return n;
                    };
                    {
                        World pa;
                        pa.init(48, 48, 0x00330011u, 2, 0, {Faction::Allies, Faction::Soviet}, 0);
                        for (Cell& c : pa.map.cells)
                            if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                        pa.spawnBuilding(0, BldType::AirForceCmd, 10, 10, true);
                        pa.players[0].country = Country::America;
                        pa.players[0].paradropReady = true;
                        pa.players[0].paradropCharge = World::PARADROP_TIME;
                        int before = countInf(pa, 0, UnitType::GI);
                        pa.orderParadrop(0, 20.5f, 20.5f);
                        bool us8 = countInf(pa, 0, UnitType::GI) - before == 8;

                        World ps;
                        ps.init(48, 48, 0x00330012u, 2, 0, {Faction::Soviet, Faction::Allies}, 0);
                        for (Cell& c : ps.map.cells)
                            if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                        ps.spawnBuilding(0, BldType::TechAirport, 10, 10, true);
                        ps.players[0].paradropReady = true;
                        ps.players[0].paradropCharge = World::PARADROP_TIME;
                        before = countInf(ps, 0, UnitType::Conscript);
                        ps.orderParadrop(0, 20.5f, 20.5f);
                        bool sov9 = countInf(ps, 0, UnitType::Conscript) - before == 9;

                        World pf;
                        pf.init(48, 48, 0x00330013u, 2, 0, {Faction::Allies, Faction::Soviet}, 0);
                        for (Cell& c : pf.map.cells)
                            if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                        pf.spawnBuilding(0, BldType::TechAirport, 10, 10, true);
                        pf.players[0].country = Country::France;
                        pf.players[0].paradropReady = true;
                        pf.players[0].paradropCharge = World::PARADROP_TIME;
                        before = countInf(pf, 0, UnitType::GI);
                        pf.orderParadrop(0, 20.5f, 20.5f);
                        bool fr6 = countInf(pf, 0, UnitType::GI) - before == 6;
                        paradropCountOk = us8 && sov9 && fr6;
                    }

                    // 复制中心：兵营造步兵时免费复制一只
                    {
                        World cl;
                        cl.init(48, 48, 0x00330014u, 2, 0, {Faction::Yuri, Faction::Allies}, 0);
                        for (Cell& c : cl.map.cells)
                            if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                        cl.spawnBuilding(0, BldType::Barracks, 8, 8, true);
                        cl.spawnBuilding(0, BldType::CloningVat, 12, 8, true);
                        cl.players[0].money = 50000;
                        int before = countInf(cl, 0, UnitType::Initiate);
                        bool started = cl.startUnitProd(0, UnitType::Initiate);
                        for (int t = 0; t < 4000 && started; ++t) cl.update();
                        cloningDupOk = started && countInf(cl, 0, UnitType::Initiate) - before >= 2;
                    }

                    // V3：部署切换；射程内自动升起；最小射程拒射贴脸目标
                    {
                        World v3;
                        v3.init(48, 48, 0x00330015u, 2, 0, {Faction::Soviet, Faction::Allies}, 0);
                        for (Cell& c : v3.map.cells)
                            if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                        EID launcher = v3.spawnUnit(0, UnitType::V3Launcher, 10.5f, 10.5f);
                        v3.orderRadDeploy({launcher});
                        bool depToggle = v3.valid(launcher) && v3.ents[launcher].deployed;
                        if (v3.valid(launcher)) {
                            v3.ents[launcher].deployed = false;
                            EID tgtFar = v3.spawnBuilding(1, BldType::Pillbox, 20, 10, true);
                            v3.ents[launcher].target = tgtFar;
                            v3.ents[launcher].state = UState::Attacking;
                            v3.ents[launcher].path.clear();
                        }
                        for (int t = 0; t < 8; ++t) v3.update();
                        bool autoDep = v3.valid(launcher) && v3.ents[launcher].deployed;
                        // 清场后只留贴脸目标，验证最小射程
                        for (size_t i = 0; i < v3.ents.size(); ++i) {
                            if ((int)i == launcher) continue;
                            if (v3.ents[i].alive) v3.kill((int)i);
                        }
                        EID tgtNear = v3.spawnUnit(1, UnitType::GI, 12.5f, 10.5f);
                        if (v3.valid(launcher)) {
                            v3.ents[launcher].deployed = true;
                            v3.ents[launcher].atkCd = 0;
                            v3.ents[launcher].target = tgtNear;
                            v3.ents[launcher].state = UState::Idle;
                            v3.ents[launcher].path.clear();
                            v3.projs.clear();
                        }
                        int nearHp0 = v3.valid(tgtNear) ? v3.ents[tgtNear].hp : -1;
                        for (int t = 0; t < 40; ++t) v3.update();
                        bool minRange = nearHp0 > 0 && v3.valid(tgtNear) && v3.ents[tgtNear].hp == nearHp0
                                     && v3.projs.empty();
                        v3DeployOk = depToggle && autoDep && minRange;
                    }

                    yuriFactionOk = (unitDef(UnitType::Yuri).factionMask & (1 << (int)Faction::Yuri)) != 0
                                 && (unitDef(UnitType::Yuri).factionMask & (1 << (int)Faction::Soviet)) == 0;

                    // 间谍伪装：攻击敌方步兵立刻变成该兵种形象
                    {
                        World sd;
                        sd.init(48, 48, 0x00330016u, 2, 0, {Faction::Allies, Faction::Soviet}, 0);
                        for (Cell& c : sd.map.cells)
                            if (c.terrain != Terrain::Water) c.terrain = Terrain::Clear;
                        EID spy = sd.spawnUnit(0, UnitType::Spy, 10.5f, 10.5f);
                        EID cons = sd.spawnUnit(1, UnitType::Conscript, 14.5f, 10.5f);
                        sd.orderAttack({spy}, cons);
                        spyDisguiseOk = sd.valid(spy) && sd.ents[spy].camouflaged
                                     && (sd.ents[spy].camoTick & 0xFFFF) == (int)UnitType::Conscript;
                    }
                }
            }

            check(valueVeteran && valueElite, "veterancy accumulates destroyed target value at 3x thresholds");
            check(eliteArmor && rankBonuses, "veteran and elite armor, speed, ROF and self-heal bonuses are active");
            check(heroUnique && heroImmune, "heroes are unique in production and immune to Yuri control");
            check(bioPower, "bio reactor occupants increase generated power");
            check(garrisonState, "garrison unload preserves rank, health and experience");
            check(grinderRefund, "grinder recycles an eligible owned unit for configured value");
            check(magnetLift && magnetDrop, "magnetron lifts vehicles and dropping causes damage");
            check(multiControl && overload, "master mind controls multiple targets and overloads above capacity");
            check(discDrain && discStopsFactory, "floating disc drains power and pauses a targeted factory");
            check(discSteal, "floating disc steals funds from an ore refinery");
            check(slaveEconomy, "yuri slave miner deploys and spawns slaves");
            check(mcvPack, "mcv repacks packs construction yard into an MCV");
            check(kirovSafeBomb, "kirov bombs do not damage airborne units");
            check(rifleWarhead, "primary weapons use official warhead classes");
            check(gatlingStages, "gatling fire advances and decays through stages");
            check(ifvPassenger && prismRefraction, "IFV passenger weapons and prism refraction are active");
            check(turretMoveFire, "turreted tanks fire while attack-moving");
            check(armorMatrix, "configurable warhead armor matrix preserves legacy fallback");
            check(forceShieldOk, "force shield invulns ally buildings and blackouts the base");
            check(rccOk, "robot control center gates and freezes robot tanks");
            check(cloningYuriOk, "cloning vats belong to Yuri not Soviet");
            check(psychicCapOk, "psychic tower hard-caps mind control at 3");
            check(uniqueBldOk, "unique buildings cannot be started twice");
            check(oilBonusOk, "oil derrick capture grants +1000");
            check(yuriPrimeOk, "yuri prime is unique and can mind-control enemy buildings");
            check(mutatorMatrixOk, "genetic mutator kills heroes/dogs and mutates infantry to brutes");
            check(spyRevealOk, "spy plane and psychic reveal support powers clear fog");
            check(countryTechOk, "country tech gates and psychic sensor faction are correct");
            check(permaMcvOk, "permanently mind-controlled MCV cannot deploy");
            check(meltdownOk, "nuclear reactor meltdown damages nearby units");
            check(swChargeOk, "superweapon charge times match official minutes at 30fps");
            check(paradropCountOk, "paradrop drops America-8 airport-Soviet-9 airport-Allies-6");
            check(cloningDupOk, "cloning vat duplicates infantry on barracks completion");
            check(v3DeployOk, "v3 auto-deploys to fire and respects min range");
            check(yuriFactionOk, "yuri infantry is Yuri-faction not Soviet");
            check(spyDisguiseOk, "spy disguise copies target infantry on attack order");
        }
    }
    // ---- 地图类型验证：岛屿水域占比应显著高于大陆，湖泊存在成片水域 ----
    {
        std::vector<Vec2i> sp;
        Map m0, m1, m2;
        auto waterFrac = [](Map& mm) {
            int n = 0;
            for (auto& c : mm.cells) if (c.terrain == Terrain::Water) n++;
            return (float)n / (float)mm.cells.size();
        };
        m0.generate(96, 96, 4242, 4, sp, 0);
        float f0 = waterFrac(m0);
        m1.generate(96, 96, 4242, 4, sp, 1);
        float f1 = waterFrac(m1);
        m2.generate(96, 96, 4242, 4, sp, 2);
        float f2 = waterFrac(m2);
        bool layoutOk = f1 > f0 + 0.15f && f2 > 0.08f;
        TraceLog(LOG_INFO, "maptype verify: continent=%.2f islands=%.2f lake=%.2f layout=%d (expect islands>>continent, 1)",
                 (double)f0, (double)f1, (double)f2, (int)layoutOk);
        check(layoutOk, "map types produce distinct water layouts");
    }
    // ---- 官方模式规则 + 固定种子 AI 验证 ----
    {
        std::vector<Faction> facs = {Faction::Allies, Faction::Soviet};
        World modes;
        modes.init(64, 64, 77123, 0, 2, facs, 0);
        modes.skirmishMode = SkirmishMode::FreeForAll;
        modes.aiAlliance = false;
        bool ffaEnemies = modes.isEnemy(0, 1);
        modes.skirmishMode = SkirmishMode::UnholyAlliance;
        bool unholyTech = modes.modeAllowsUnit(1, UnitType::GI)
                       && modes.modeAllowsBuilding(1, BldType::PowerPlant);
        modes.skirmishMode = SkirmishMode::Megawealth;
        modes.ensureMegawealthOilDerricks(2);
        int oil = 0;
        for (const auto& e : modes.ents)
            if (e.alive && e.isBuilding && e.btype == BldType::OilDerrick) oil++;
        bool megaRules = !modes.modeAllowsUnit(0, UnitType::ChronoMiner)
                      && !modes.modeAllowsBuilding(0, BldType::OreRefinery)
                      && oil >= modes.numPlayers * 2;
        modes.skirmishMode = SkirmishMode::MeatGrinder;
        bool meatRules = modes.modeAllowsUnit(0, UnitType::Grizzly)
                      && !modes.modeAllowsUnit(0, UnitType::Intruder)
                      && !modes.modeAllowsBuilding(0, BldType::NavalYard);
        modes.superweaponsEnabled = false;
        bool swRules = !modes.modeAllowsBuilding(0, BldType::WeatherDevice)
                    && !modes.launchSW(0, SWType::Lightning, 10, 10);
        modes.skirmishMode = SkirmishMode::Battle;
        modes.aiAlliance = true;
        modes.sharedVision = true;
        modes.update();
        int p0x = 0, p0y = 0;
        for (const auto& e : modes.ents)
            if (e.alive && e.player == 0) { p0x = (int)e.x; p0y = (int)e.y; break; }
        bool visionRules = modes.map.fogAt(1, p0x, p0y) == FOG_VISIBLE;

        World shortWorld;
        shortWorld.init(64, 64, 77124, 1, 0, {Faction::Allies}, 0);
        shortWorld.shortGame = true;
        EID shortMcv = INVALID_EID;
        for (size_t i = 0; i < shortWorld.ents.size(); ++i)
            if (shortWorld.ents[i].alive && !shortWorld.ents[i].isBuilding
                && shortWorld.ents[i].utype == UnitType::MCV) { shortMcv = (int)i; break; }
        if (shortMcv != INVALID_EID) shortWorld.kill(shortMcv);
        bool shortRules = shortWorld.players[0].defeated;
        check(ffaEnemies && unholyTech, "FFA hostility and Unholy Alliance cross-tech rules are active");
        check(megaRules && meatRules && swRules, "Megawealth, Meat Grinder and superweapon restrictions are active");
        check(visionRules && shortRules, "allied shared vision and Short Game defeat are active");

        bool deterministic = true, economies = true, legal = true;
        const Faction aiFacs[] = {Faction::Allies, Faction::Soviet, Faction::Yuri, Faction::China};
        for (int fi = 0; fi < 4; ++fi) {
            std::vector<Faction> pair = {Faction::Allies, aiFacs[fi]};
            // Run A and B sequentially (not interleaved) so shared globals
            // (sfx throttle clocks, sprite caches, etc.) cannot falsely desync twins.
            World a, b;
            a.init(64, 64, 88000 + fi, 1, 1, pair, 0);
            b.init(64, 64, 88000 + fi, 1, 1, pair, 0);
            SkirmishAI aa, ab;
            aa.reset(1); ab.reset(1);
            aa.difficulty = ab.difficulty = AIDiff::Normal;
            for (int tick = 0; tick < 3000; ++tick) {
                a.update(); aa.update(a);
                b.update(); ab.update(b);
            }
            uint32_t ca = a.checksum(), cb = b.checksum();
            deterministic = deterministic && (ca == cb);
            int blds = 0;
            for (const auto& e : a.ents) {
                if (!e.alive || e.player != 1 || !e.isBuilding) continue;
                blds++;
                // factionMask=0 是工程师合法占领的中立科技建筑，不属于 AI 建造序列。
                if (bldDef(e.btype).factionMask != 0)
                    legal = legal && a.modeAllowsBuilding(1, e.btype);
            }
            economies = economies && a.players[1].money >= 0 && blds >= 3
                      && (a.hasBld(1, BldType::OreRefinery) || a.players[1].defeated);
            TraceLog(LOG_INFO, "AI fixed-seed faction=%d checksum=%u blds=%d money=%d refinery=%d defeated=%d",
                     fi, a.checksum(), blds, a.players[1].money,
                     (int)a.hasBld(1, BldType::OreRefinery), (int)a.players[1].defeated);
            if (a.players[1].bldProd.active)
                legal = legal && a.modeAllowsBuilding(1, (BldType)a.players[1].bldProd.typeIdx);
            if (a.players[1].defProd.active)
                legal = legal && a.modeAllowsBuilding(1, (BldType)a.players[1].defProd.typeIdx);
            for (int cat = 0; cat < PROD_CAT_N; ++cat)
                if (a.players[1].unitProd[cat].active)
                    legal = legal && a.modeAllowsUnit(1, (UnitType)a.players[1].unitProd[cat].typeIdx);
        }
        check(deterministic, "fixed-seed AI simulations produce identical checksums");
        check(economies && legal, "all four faction AIs establish legal economies without negative funds");
    }
    // ---- 战役验证：任务表 + 首波增援刷出 ----
    {
        bool tblOk = missionTable().size() >= 32;
        int officialN = 0;
        for (const auto& md : missionTable()) if (md.track == 1) officialN++;
        bool officialOk = officialN >= 3;
        newCampaignGame(0);
        int before = 0;
        for (auto& e : world.ents) if (e.alive && e.player == 1 && !e.isBuilding) before++;
        for (int i = 0; i < 2800; i++) logic(); // 跑过首波（2700 tick）
        int after = 0;
        for (auto& e : world.ents) if (e.alive && e.player == 1 && !e.isBuilding) after++;
        bool waveOk = nextWave >= 1 && after >= before + 2; // 首波 4 单位（途中可能战损，放宽）
        TraceLog(LOG_INFO, "campaign verify: table=%zu official=%d waveSpawn=%d (p1 units %d -> %d, nextWave=%zu)",
                 missionTable().size(), officialN, (int)waveOk, before, after, nextWave);
        check(tblOk, "campaign table contains fusion missions");
        check(officialOk, "official campaign track has Lone Guardian class missions");
        check(waveOk, "campaign first reinforcement wave spawns");
    }
    TraceLog(failed == 0 ? LOG_INFO : LOG_ERROR,
             "SMOKE SUMMARY: %s passed=%d failed=%d frames=%d ents=%zu tick=%llu",
             failed == 0 ? "PASS" : "FAIL", passed, failed, frames, world.ents.size(),
             (unsigned long long)world.tick);
    return failed;
}

// 战役冒烟测试：开局跑 N 帧，输出实体/触发器/目标状态（校验手工地图加载与触发器运行）
int Game::campaignSmokeTest(int mission, int frames) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char* name) {
        TraceLog(ok ? LOG_INFO : LOG_ERROR, "CAMPAIGN SMOKE %s: %s", ok ? "PASS" : "FAIL", name);
        ok ? ++passed : ++failed;
    };
    if (mission < 0 || mission >= (int)missionTable().size()) {
        TraceLog(LOG_ERROR, "CAMPAIGN SMOKE SUMMARY: FAIL invalid mission=%d (valid range 0..%d)",
                 mission, (int)missionTable().size() - 1);
        return 1;
    }
    newCampaignGame(mission);
    const MissionDef& md = missionTable()[mission];
    check(world.map.w > 0 && world.map.h > 0 && !world.map.cells.empty(), "mission creates a map");
    check(world.numPlayers == 1 + (int)md.aiFactions.size(), "mission creates declared players");
    check(!objectiveText.empty() || !md.triggers.empty() || !md.waves.empty(), "mission has validation content");
    TraceLog(LOG_INFO, "campaign smoke: mission=%d map=%s size=%dx%d players=%d triggers=%zu",
             mission, md.mapFile.empty() ? "(generated)" : md.mapFile.c_str(), world.map.w, world.map.h,
             world.numPlayers, missionTriggers.size());
    // 开局实体快照
    for (int p = 0; p < world.numPlayers; p++) {
        int blds = 0, units = 0;
        for (auto& e : world.ents)
            if (e.alive && e.player == p) { if (e.isBuilding) blds++; else units++; }
        TraceLog(LOG_INFO, "  start player %d: blds=%d units=%d money=%d", p, blds, units, world.players[p].money);
    }
    {
        int neutral = 0;
        for (auto& e : world.ents)
            if (e.alive && e.player == -1) neutral++;
        TraceLog(LOG_INFO, "  start neutral ents: %d", neutral);
    }
    for (int i = 0; i < frames && !gameOver; i++) logic();
    // 目检用：揭示全图、相机移到地图中心（覆盖水域/矿区等各类地形，出生点看不到全地貌）
    world.map.reveal(localPlayer, world.map.w / 2, world.map.h / 2, world.map.w);
    {
        int csx, csy;
        tileToScreen(world.map.w / 2, world.map.h / 2, csx, csy);
        camX = (float)csx - (SCREEN_W - sidebarW) / 2.0f;
        camY = (float)csy - SCREEN_H / 2.0f;
    }
    shotFile = TextFormat("smoke_campaign_%d.png", mission); // 截图供素材/地图目检（无头模式落盘 PNG）
    render();
    int firedN = 0;
    for (const Trigger& t : missionTriggers)
        if (t.fired) firedN++;
    TraceLog(LOG_INFO, "  after %d frames: tick=%llu gameOver=%d victory=%d triggersFired=%d/%zu objective='%s'",
             frames, (unsigned long long)world.tick, (int)gameOver, (int)victory, firedN,
             missionTriggers.size(), objectiveText.c_str());
    for (int p = 0; p < world.numPlayers; p++) {
        int blds = 0, units = 0;
        for (auto& e : world.ents)
            if (e.alive && e.player == p) { if (e.isBuilding) blds++; else units++; }
        TraceLog(LOG_INFO, "  end player %d: blds=%d units=%d money=%d defeated=%d",
                 p, blds, units, world.players[p].money, (int)world.players[p].defeated);
    }
    check(world.tick > 0 || frames == 0, "mission simulation advances");
    TraceLog(failed == 0 ? LOG_INFO : LOG_ERROR,
             "CAMPAIGN SMOKE SUMMARY: %s passed=%d failed=%d mission=%d frames=%d",
             failed == 0 ? "PASS" : "FAIL", passed, failed, mission, frames);
    return failed;
}

int Game::campaignMatrixTest(int frames) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, int mission, const char* name) {
        TraceLog(ok ? LOG_INFO : LOG_ERROR, "CAMPAIGN MATRIX %s m=%02d: %s",
                 ok ? "PASS" : "FAIL", mission + 1, name);
        ok ? ++passed : ++failed;
    };
    const auto& table = missionTable();
    check(table.size() >= 32, 0, "mission table contains fusion + optional official tracks");
    int officialN = 0;
    for (const auto& md : table) if (md.track == 1) officialN++;
    check(officialN >= 3, 0, "official track lists at least 3 Allied prototype missions");
    for (int mission = 0; mission < (int)table.size(); ++mission) {
        const MissionDef& md = table[mission];
        newCampaignGame(mission, false);
        bool hasPlayerForce = false;
        for (const auto& e : world.ents)
            if (e.alive && e.player == 0) { hasPlayerForce = true; break; }
        check(world.map.w == md.mapSize && world.map.h == md.mapSize, mission, "declared map starts");
        check(world.numPlayers == 1 + (int)md.aiFactions.size(), mission, "declared player matrix starts");
        check(hasPlayerForce, mission, "player has a valid starting force");

        bool refsOk = true, hasWin = false, hasLose = false;
        for (const Trigger& t : md.triggers) {
            if ((t.cond == TrigCond::PlayerBldLost || t.cond == TrigCond::PlayerAllDead
                 || t.cond == TrigCond::UnitInRect || t.cond == TrigCond::MoneyBelow)
                && (t.c[0] < 0 || t.c[0] >= world.numPlayers)) refsOk = false;
            if (t.cond == TrigCond::PlayerBldLost
                && (t.c[1] < 0 || t.c[1] >= (int)BldType::COUNT)) refsOk = false;
            if ((t.act == TrigAct::SpawnAt || t.act == TrigAct::GiveMoney || t.act == TrigAct::RevealMap)
                && (t.a[0] < 0 || t.a[0] >= world.numPlayers)) refsOk = false;
            hasWin = hasWin || t.act == TrigAct::Win;
            hasLose = hasLose || t.act == TrigAct::Lose;
        }
        check(refsOk, mission, "trigger references are in range");
        bool objectiveOk = (md.objective == 0)
                        || (md.objective == 1 && md.objectiveTick > 0)
                        || (md.objective == 2 && hasWin && hasLose);
        check(objectiveOk, mission, "objective has complete win/loss semantics");

        for (int i = 0; i < frames && !gameOver; ++i) logic();
        check(world.tick > 0 || frames == 0, mission, "startup simulation advances");
        bool alwaysFired = true;
        for (const Trigger& t : missionTriggers)
            if (t.cond == TrigCond::Always && t.once && !t.fired) alwaysFired = false;
        check(alwaysFired, mission, "startup triggers fire");

        // 失败矩阵：任何任务在玩家被判负后都必须结束为失败。
        newCampaignGame(mission, false);
        world.players[0].defeated = true;
        logic();
        check(gameOver && !victory, mission, "player defeat resolves as loss");

        // 胜利矩阵：分别驱动默认歼灭、坚守计时或显式 Win 触发器。
        newCampaignGame(mission, false);
        if (md.objective == 0) {
            nextWave = md.waves.size();
            for (int p = 1; p < world.numPlayers; ++p) world.players[p].defeated = true;
            logic();
        } else if (md.objective == 1) {
            world.tick = (uint64_t)std::max(0, md.objectiveTick - 1);
            logic();
        } else {
            for (Trigger& t : missionTriggers) {
                if (t.act != TrigAct::Win) continue;
                switch (t.cond) {
                    case TrigCond::Always: break;
                    case TrigCond::Time: world.tick = (uint64_t)std::max(0, t.c[0]); break;
                    case TrigCond::PlayerAllDead: world.players[t.c[0]].defeated = true; break;
                    case TrigCond::MoneyBelow: world.players[t.c[0]].money = t.c[1] - 1; break;
                    case TrigCond::PlayerBldLost:
                        t.armed = true;
                        for (size_t i = 0; i < world.ents.size(); ++i)
                            if (world.ents[i].alive && world.ents[i].isBuilding
                                && world.ents[i].player == t.c[0]
                                && world.ents[i].btype == (BldType)t.c[1]) world.kill((int)i);
                        break;
                    case TrigCond::UnitInRect:
                        world.spawnUnit(t.c[0], UnitType::Engineer, t.c[1] + 0.5f, t.c[2] + 0.5f);
                        break;
                    case TrigCond::Script: break;
                }
                break;
            }
            updateTriggers();
        }
        check(gameOver && victory, mission, "declared victory path resolves");
    }
    TraceLog(failed == 0 ? LOG_INFO : LOG_ERROR,
             "CAMPAIGN MATRIX SUMMARY: %s missions=%d passed=%d failed=%d frames=%d",
             failed == 0 ? "PASS" : "FAIL", (int)table.size(), passed, failed, frames);
    return failed;
}

// 遭遇战截图：固定种子开局 + 预热（AI 与本地玩家发展出建筑/电力/部队），相机对准本地基地后拍全屏
void Game::debugShot(int warmTicks, const char* file) {
    newGame(20260801);
    // 本地玩家也由 AI 代打（截图需发展出完整基地/电力/部队）
    ais.insert(ais.begin(), SkirmishAI{});
    ais[0].reset(0);
    ais[0].difficulty = AIDiff::Normal;
    for (int i = 0; i < warmTicks && !gameOver; i++) logic();
    // 相机对准本地玩家建造场（含建成的电厂/兵营等，验证 GUI 电力条与建筑贴图）
    bool found = false;
    for (auto& e : world.ents) {
        if (e.alive && e.isBuilding && e.player == localPlayer && e.btype == BldType::ConYard) {
            int sx, sy;
            tileToScreen((int)e.x + 1, (int)e.y + 1, sx, sy);
            camX = (float)sx - (SCREEN_W - sidebarW) / 2.0f;
            camY = (float)sy - SCREEN_H / 2.0f;
            found = true;
            break;
        }
    }
    if (!found) TraceLog(LOG_WARNING, "debugShot: local ConYard not found");
    int blds = 0, units = 0;
    for (auto& e : world.ents)
        if (e.alive && e.player == localPlayer) { if (e.isBuilding) blds++; else units++; }
    TraceLog(LOG_INFO, "debugShot: warm=%d local blds=%d units=%d power=%d/%d", warmTicks, blds, units,
             world.players[localPlayer].powerMade, world.players[localPlayer].powerUsed);
    shotFile = file;
    render();
    // 再拍一张对准中立油田（验证预置科技建筑贴图）
    for (auto& e : world.ents) {
        if (!e.alive || !e.isBuilding || e.btype != BldType::OilDerrick) continue;
        // 揭开迷雾：否则预置中立建筑在未探索区全黑，无法验收贴图
        world.map.reveal(localPlayer, (int)e.x + 1, (int)e.y + 1, 12);
        fogMaskTick = -1; // 强制重烘迷雾遮罩（否则仍按旧 tick 跳过 bake）
        int sx, sy;
        tileToScreen((int)e.x + 1, (int)e.y + 1, sx, sy);
        camX = (float)sx - (SCREEN_W - sidebarW) / 2.0f;
        camY = (float)sy - SCREEN_H / 2.0f;
        cursorKind = CursorKind::Arrow;
        shotFile = "shot_oil.png";
        render();
        TraceLog(LOG_INFO, "debugShot: oil at (%d,%d) sprite=%s shot_oil.png",
                 (int)e.x, (int)e.y, bldDef(e.btype).name);
        break;
    }
    // 医院
    for (auto& e : world.ents) {
        if (!e.alive || !e.isBuilding || e.btype != BldType::Hospital) continue;
        const BldDef& hd = bldDef(e.btype);
        world.map.reveal(localPlayer, (int)e.x + hd.w / 2, (int)e.y + hd.h / 2, 14);
        fogMaskTick = -1;
        int sx, sy;
        tileToScreen((int)e.x + hd.w / 2, (int)e.y + hd.h / 2, sx, sy);
        camX = (float)sx - (SCREEN_W - sidebarW) / 2.0f;
        camY = (float)sy - SCREEN_H / 2.0f;
        shotFile = "shot_hospital.png";
        render();
        TraceLog(LOG_INFO, "debugShot: hospital at (%d,%d) shot_hospital.png", (int)e.x, (int)e.y);
        break;
    }
}

// 临时诊断：战役真实渲染耗时（真实窗口+解除帧率上限，分离逻辑/渲染耗时定位"战役卡"）
void Game::benchCampaign(int mission, int warmTicks, int frames) {
    newCampaignGame(mission);
    for (int i = 0; i < warmTicks && !gameOver; i++) logic(); // 预热：让 AI 发展出规模兵力
    int alive = 0;
    for (auto& e : world.ents) if (e.alive) alive++;
    SetTargetFPS(0); // 解除 60fps 上限
    double t0 = GetTime();
    for (int i = 0; i < frames; i++) logic();
    double t1 = GetTime();
    for (int i = 0; i < frames; i++) render();
    double t2 = GetTime();
    printf("BENCH mission=%d warm=%d ents=%d: logic=%.2fms render=%.2fms total=%.2fms (%.0f fps)\n",
           mission, warmTicks, alive,
           (t1 - t0) * 1000.0 / frames, (t2 - t1) * 1000.0 / frames,
           (t2 - t0) * 1000.0 / frames, frames * 2.0 / (t2 - t0));
}

int Game::playTest() {
    sim.active = true;
    int fails = 0, stepNo = 0;
    auto check = [&](bool ok, const char* name) {
        stepNo++;
        TraceLog(LOG_INFO, "PLAYTEST [%02d] %-30s %s", stepNo, name, ok ? "PASS" : "FAIL");
        if (!ok) fails++;
    };
    // 单帧推进：逻辑 + 输入处理 + 渲染，帧末清除输入边沿
    auto frame = [&](int n = 1) {
        for (int i = 0; i < n; i++) {
            if (phase == Phase::InGame && !paused && !gameOver) logic();
            if (phase == Phase::InGame) handleInput();
            render();
            sim.pressedL = sim.pressedR = sim.releasedL = sim.releasedR = false;
            sim.keysPressed.clear();
        }
    };
    auto clickL = [&](float x, float y) {
        sim.pos = {x, y}; frame();
        sim.pressedL = true; sim.downL = true; frame();
        sim.releasedL = true; sim.downL = false; frame();
    };
    auto clickR = [&](float x, float y) {
        sim.pos = {x, y}; frame();
        sim.pressedR = true; sim.downR = true; frame();
        sim.releasedR = true; sim.downR = false; frame();
    };
    auto dragL = [&](float x1, float y1, float x2, float y2) {
        sim.pos = {x1, y1}; frame();
        sim.pressedL = true; sim.downL = true; frame();
        sim.pos = {x2, y2}; frame(2);
        sim.releasedL = true; sim.downL = false; frame();
    };
    auto key = [&](int k) {
        sim.keysDown.insert(k); sim.keysPressed.insert(k);
        frame();
        sim.keysDown.erase(k);
    };
    auto shot = [&](const char* f) { shotFile = f; render(); };
    auto findUnit = [&](UnitType t) -> EID {
        for (size_t i = 0; i < world.ents.size(); i++)
            if (world.ents[i].alive && !world.ents[i].isBuilding && world.ents[i].player == 0 && world.ents[i].utype == t)
                return (int)i;
        return INVALID_EID;
    };

    // ---- 1 主菜单 ----
    frame(3);
    check(phase == Phase::MainMenu, "启动进入主菜单");
    shot("pt_01_mainmenu.png");

    // ---- 1b 设置页：语言热切换 / 显示模式 / 按键重绑 / 持久化 ----
    clickL(720, 514); // “设置”按钮 {570,490,300,48}（主菜单第4按钮：遭遇战/战役/局域网/设置/编辑器/退出）
    check(phase == Phase::Settings, "点击[设置]进设置页");
    frame(2);
    shot("pt_01b_settings.png");
    clickL(490, 158); // 语言行值按钮 {340,140,300,36}
    check(g_lang == 1 && cfgLang == 1, "语言热切换为英文");
    frame(2);
    shot("pt_01c_settings_en.png");
    clickL(490, 158);
    check(g_lang == 0 && cfgLang == 0, "语言切回中文");
    // 显示模式：窗口 ⇄ 无边框全屏（还原默认值，避免干扰后续截图）
    clickL(490, 208); // 显示模式行 {340,190,300,36}
    check(cfgWindowMode == 1, "切换为窗口模式");
    clickL(490, 208);
    check(cfgWindowMode == 0, "切回无边框全屏");
    // 按键重绑：第一行“停止”键位框 {1150,140,200,30}，注入按键 B
    clickL(1250, 155);
    check(rebinding == KA_Stop, "点击键位框进入重绑");
    key(KEY_B);
    check(rebinding < 0 && keyBind[KA_Stop] == KEY_B, "注入按键完成重绑");
    // 持久化验证：settings.ini 已写入，重载后键位保留
    check(FileExists("settings.ini"), "settings.ini 已落盘");
    {
        int saved = keyBind[KA_Stop];
        loadSettings();
        check(keyBind[KA_Stop] == saved, "重载配置键位保持");
        keyBind[KA_Stop] = KEY_S; // 还原默认，避免影响后续流程
        saveSettings();
    }
    clickL(720, 748); // “返回” {620,724,200,48}
    check(phase == Phase::MainMenu, "设置页返回主菜单");

    // ---- 2 遭遇战设置 ----
    clickL(720, 334); // “遭遇战”按钮 {570,310,300,48}（主菜单第1按钮）
    check(phase == Phase::Setup, "点击[遭遇战]进设置");
    frame(2); // 让地图预览生成
    shot("pt_02_setup.png");
    clickL(230, 620); // 游戏模式值框
    check(cfgGameMode == (int)SkirmishMode::FreeForAll, "Setup可明确选择游戏模式");
    for (int i = 0; i < (int)SkirmishMode::COUNT - 1; ++i) clickL(230, 620);
    check(cfgGameMode == (int)SkirmishMode::Battle, "模式选择可循环回标准作战");

    // ---- 3 开始游戏 ----
    clickL(550, 731); // “开始游戏” {390,700,320,62}
    check(phase == Phase::InGame && campaignMission < 0, "点击[开始游戏]进遭遇战");
    frame(5);

    // ---- 4 点选基地车 ----
    EID mcv = findUnit(UnitType::MCV);
    check(mcv != INVALID_EID, "找到出生基地车");
    if (mcv != INVALID_EID) {
        // 点贴图中心（脚底锚点在南触点，直接点 unitScreenPos 易落空）
        Rectangle ur = unitScreenRect(world.ents[mcv]);
        clickL(ur.x + ur.width * 0.5f, ur.y + ur.height * 0.5f);
        check(sel.size() == 1 && sel[0] == mcv, "左键点选基地车");
        frame(2);
        shot("pt_03b_selpanel.png"); // 单选信息面板（图标/名称/血条）
    }

    // ---- 5 D 展开建造厂 ----
    key(KEY_D);
    frame(3);
    check(world.hasBld(0, BldType::ConYard), "按D展开建造厂");
    shot("pt_03_deployed.png");

    // ---- 6 侧边栏生产电厂（建筑页签第1个图标：orig 首槽 {1283,240,64x50}）----
    clickL(1315, 265);
    check(world.players[0].bldProd.active, "点击电厂图标开始生产");
    frame(2);
    shot("pt_03c_tooltip.png"); // 悬停生产图标：名称/造价/耗时提示框
    for (int i = 0; i < 6000 && !world.players[0].bldProd.ready; i++) logic(); // 快进至就绪
    check(world.players[0].bldProd.ready, "电厂生产就绪");

    // ---- 7 放置建筑 ----
    clickL(1315, 265); // 就绪后再点图标进入放置模式
    check(placing, "再次点击进入放置模式");
    BldType pt = world.players[0].placingBld;
    const BldDef& pd = bldDef(pt);
    int abx = 10, aby = 10;
    for (auto& e : world.ents)
        if (e.alive && e.isBuilding && e.player == 0 && e.btype == BldType::ConYard) { abx = (int)e.x; aby = (int)e.y; break; }
    int pbx = -1, pby = -1;
    for (int r = 1; r < 12 && pbx < 0; r++)
        for (int dy = -r; dy <= r && pbx < 0; dy++)
            for (int dx = -r; dx <= r && pbx < 0; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                if (world.canPlace(pt, abx + dx, aby + dy, 0)) { pbx = abx + dx; pby = aby + dy; }
            }
    check(pbx >= 0, "扫描到可放置位置");
    if (pbx >= 0) {
        int px, py;
        tileToScreen(pbx + pd.w / 2, pby + pd.h / 2, px, py); // 点击使 bx=tx-w/2 还原到扫描点
        clickL((float)(px - (int)camX), (float)(py - (int)camY));
    }
    check(world.countBlds(0, pt) >= 1 && !placing, "左键放置建筑成功");
    shot("pt_04_placed.png");

    // ---- 8 框选坦克 ----
    EID tank = findUnit(UnitType::Type99);
    if (tank == INVALID_EID) tank = findUnit(UnitType::Rhino);
    if (tank == INVALID_EID) tank = findUnit(UnitType::Grizzly);
    check(tank != INVALID_EID, "找到护卫坦克");
    if (tank != INVALID_EID) {
        Vector2 tp = unitScreenPos(world.ents[tank]);
        dragL(tp.x - 60, tp.y - 60, tp.x + 60, tp.y + 60);
        check(!sel.empty(), "拖拽框选选中单位");

        // ---- 9 右键移动 ----
        // 目标锚点：要求 3x2 区域全空（orderMove 编队偏移 x∈{-1,0,1}, y∈{0,1}，避免偏移落点不可走）
        int mtx = -1, mty = -1;
        auto blockClear = [&](int cx, int cy) {
            for (int oy = 0; oy <= 1; oy++)
                for (int ox = -1; ox <= 1; ox++) {
                    int nx = cx + ox, ny = cy + oy;
                    if (!world.passableFor(nx, ny, 0) || world.bldBlocked(nx, ny) || world.unitAtCell(nx, ny) != INVALID_EID)
                        return false;
                }
            return true;
        };
        for (int r = 3; r < 14 && mtx < 0; r++)
            for (int dx = r; dx >= -r && mtx < 0; dx--)
                for (int dy = -r; dy <= r; dy++) {
                    int nx = (int)world.ents[tank].x + dx, ny = (int)world.ents[tank].y + dy;
                    if (blockClear(nx, ny)) { mtx = nx; mty = ny; break; }
                }
        if (mtx >= 0) {
            int px, py;
            tileToScreen(mtx, mty, px, py);
            // 记录所选单位起点
            std::vector<std::pair<float, float>> from;
            for (EID id : sel) if (world.valid(id)) from.push_back({world.ents[id].x, world.ents[id].y});
            clickR((float)(px - (int)camX), (float)(py - (int)camY));
            frame(180); // 6 秒逻辑
            float bestMoved = 0;
            for (size_t i = 0; i < sel.size() && i < from.size(); i++)
                if (world.valid(sel[i]))
                    bestMoved = std::max(bestMoved, distf(from[i].first, from[i].second, world.ents[sel[i]].x, world.ents[sel[i]].y));
            check(bestMoved > 1.5f, "右键移动单位");
            // 编队设定/召回与攻击移动状态（同一输入路径，避免仅测 World 直调）。
            sim.keysDown.insert(KEY_LEFT_CONTROL);
            sim.keysPressed.insert(KEY_ONE);
            frame();
            sim.keysDown.erase(KEY_LEFT_CONTROL);
            bool groupStored = !groups[1].empty();
            sel.clear();
            key(KEY_ONE);
            check(groupStored && !sel.empty(), "Ctrl+1设定并用1召回编队");
            if (!sel.empty() && world.valid(sel.front())) {
                EID aid = sel.front();
                world.orderMove({aid}, world.ents[aid].x + 3.0f, world.ents[aid].y, true);
                check(world.ents[aid].state == UState::AttackMoving, "攻击移动进入主动索敌状态");
            } else {
                check(false, "攻击移动进入主动索敌状态");
            }
        } else {
            check(false, "右键移动单位（无可走目标）");
        }
        shot("pt_05_move.png");
        // ---- 9.1 大框选：多选构成统计面板 ----
        dragL(100, 100, (float)(SCREEN_W - 260), (float)(SCREEN_H - 100));
        check(sel.size() > 1, "大框选选中多单位");
        frame(2);
        shot("pt_05b_multisel.png");
    }

    // ---- 9.5 侧边栏出售模式（RA2 按钮）----
    {
        int bcnt = world.countBlds(0, pt);
        EID pbld = INVALID_EID;
        for (size_t i = 0; i < world.ents.size(); i++)
            if (world.ents[i].alive && world.ents[i].isBuilding && world.ents[i].player == 0
                && world.ents[i].btype == pt) { pbld = (int)i; break; }
        if (pbld != INVALID_EID) {
            Vector2 bp = bldScreenPos(world.ents[pbld]);
            const Sprite& ps = g_sprites.building(pt, world.players[0].colorId, false);
            world.ents[pbld].hp /= 2;
            world.players[0].money += bldDef(pt).cost;
            clickL(1300, 186); // “维修”按钮：穹带左半
            check(sideMode == 1, "点击[维修]进入维修模式");
            clickL(bp.x - ps.ox + ps.tex.width / 2.0f, bp.y - ps.oy + ps.tex.height / 2.0f);
            // RA2：扳手模式是持续维修开关，非一帧回满；推进若干 tick 后应回满并结束
            check(world.ents[pbld].repairing || world.ents[pbld].hp > bldDef(pt).hp / 2,
                  "维修模式已开启持续维修");
            for (int t = 0; t < 600 && world.ents[pbld].alive
                           && world.ents[pbld].hp < bldDef(pt).hp; t++)
                logic();
            check(world.ents[pbld].hp == bldDef(pt).hp && !world.ents[pbld].repairing,
                  "维修模式恢复建筑生命");
            sideMode = 0; // 显式清零：下面单独测出售模式保持
            int money0 = world.players[0].money;
            clickL(1380, 186); // “出售”按钮：orig 穹带右半中心 {1267,170,150x33}（1:1 布局 Y_MODE≈170）
            check(sideMode == 2, "点击[出售]进入出售模式");
            clickL(bp.x - ps.ox + ps.tex.width / 2.0f, bp.y - ps.oy + ps.tex.height / 2.0f); // 电厂贴图中心
            check(sideMode == 2, "出售后模式保持（右键才取消）");
            check(world.ents[pbld].selling || !world.ents[pbld].alive, "出售开始倒放动画或已拆除");
            for (int t = 0; t < 200 && world.ents[pbld].alive; t++) logic();
            check(world.countBlds(0, pt) == bcnt - 1 && world.players[0].money > money0,
                  "出售模式点击建筑卖出");
            clickR(bp.x, bp.y); // 右键退出出售模式
            check(sideMode == 0, "右键取消出售模式");
        } else check(false, "出售模式点击建筑卖出");
        sel.clear(); // 出售后可能残留选中
    }

    // ---- 10 ESC 菜单 → 保存进度 → F9 读档 → 返回主菜单 ----
    // 局内菜单布局与 drawHUD 一致（mw=320, mh=426）：按钮 x=mx+60=620 宽200 中心x=720，行距42
    auto menuBtn = [&](int offY) { clickL(720, (float)(SCREEN_H / 2 - 426 / 2 + offY + 16)); };
    key(KEY_ESCAPE); // 第一次：清除选择
    key(KEY_ESCAPE); // 第二次：打开菜单
    check(showMenu, "ESC打开游戏菜单");
    shot("pt_06_escmenu.png");
    menuBtn(114); // “保存进度” {620,my+114,200,32}
    check(FileExists(QUICKSAVE_PATH) && !showMenu, "菜单点击[保存进度]");
    uint64_t tick0 = world.tick;
    int money0 = world.players[0].money;
    key(KEY_F9); // 快速读档：状态应还原到保存时刻（误差=点击后推进的几帧）
    check(world.tick + 5 >= tick0 && world.tick <= tick0 + 5 && world.players[0].money == money0
          && world.hasBld(0, BldType::ConYard), "F9读档状态还原");
    uint64_t tickL = world.tick;
    frame(30);
    check(world.tick == tickL + 30, "读档后模拟继续推进");
    key(KEY_ESCAPE);
    check(showMenu, "再次ESC打开菜单");
    // 局内菜单 → 设置页 → 返回（settingsFromGame 恢复菜单）
    menuBtn(198); // “设置” {620,my+198,200,32}
    check(phase == Phase::Settings && settingsFromGame, "菜单点击[设置]进设置页");
    clickL(720, 748); // 设置页“返回” {620,724,200,48}
    check(phase == Phase::InGame && showMenu, "设置页返回恢复局内菜单");
    menuBtn(282); // “返回主菜单” {620,my+282,200,32}
    check(phase == Phase::MainMenu && !showMenu, "点击[返回主菜单]");

    // ---- 11 战役模式 ----
    clickL(720, 394); // “战役模式” {570,370,300,48}（主菜单第2按钮）
    check(phase == Phase::MissionSelect, "点击[战役模式]");
    shot("pt_07_missions.png");
    clickL(330, 300); // 第一张任务卡 {44,180,320,168}
    check(phase == Phase::InGame && campaignMission == 0, "点击任务1进入战役");
    frame(10);
    shot("pt_08_campaign.png");

    // ---- 12 战役内 ESC → 返回主菜单 ----
    key(KEY_ESCAPE);
    check(showMenu, "战役ESC打开菜单");
    menuBtn(282); // “返回主菜单”
    check(phase == Phase::MainMenu, "战役返回主菜单");

    frame(2);
    TraceLog(LOG_INFO, "PLAYTEST DONE: %d checks, %d failed", stepNo, fails);
    sim.active = false;
    return fails;
}

// 目检截图：虚线笼 / 建造·出售 mk / 地形。输出 tools/visual_audit/out/*.png
// 用法: ra2.exe --visual-audit（须在仓库根目录运行，以便落盘路径正确）
int Game::visualAudit() {
    namespace fs = std::filesystem;
    const fs::path outDir = fs::path("tools") / "visual_audit" / "out";
    fs::create_directories(outDir);
    int fails = 0;
    auto check = [&](bool ok, const char* name) {
        TraceLog(ok ? LOG_INFO : LOG_ERROR, "VISUAL %s: %s", ok ? "PASS" : "FAIL", name);
        if (!ok) fails++;
    };

    newGame(20260805);
    phase = Phase::InGame;
    paused = false;
    showMenu = false;
    placing = false;
    visualAuditMarkers = false; // 正式目检截图不叠调试十字/青框
    for (Cell& c : world.map.cells) c.height = 0;
    bakeTerrain();
    world.map.reveal(localPlayer, world.map.w / 2, world.map.h / 2, world.map.w);
    fogMaskTick = -1;

    // 开局只有 MCV：展开为建造厂，再围绕它摆测试建筑（与 play-test 一致）
    Vec2i base{-1, -1};
    EID cyId = INVALID_EID;
    for (size_t i = 0; i < world.ents.size(); i++) {
        auto& e = world.ents[i];
        if (!e.alive || e.isBuilding || e.player != localPlayer || e.utype != UnitType::MCV) continue;
        int bx = (int)e.x, by = (int)e.y;
        e.alive = false;
        cyId = world.spawnBuilding(localPlayer, BldType::ConYard, bx, by, true);
        base = {bx, by};
        break;
    }
    for (size_t i = 0; i < world.ents.size() && cyId == INVALID_EID; i++) {
        auto& e = world.ents[i];
        if (e.alive && e.isBuilding && e.player == localPlayer && e.btype == BldType::ConYard) {
            base = {(int)e.x, (int)e.y};
            cyId = (EID)i;
        }
    }
    check(base.x >= 0 && world.valid(cyId), "has local ConYard");
    if (base.x < 0 || !world.valid(cyId)) return fails + 1;

    auto centerOn = [&](EID id, float zoom) {
        if (!world.valid(id)) return;
        const World::Ent& e = world.ents[id];
        const BldDef& d = bldDef(e.btype);
        camZoom = zoom;
        int sx = 0, sy = 0;
        tileToScreen((int)e.x + d.w / 2, (int)e.y + d.h / 2, sx, sy);
        float visW = (float)(SCREEN_W - sidebarW) / camZoom;
        float visH = (float)SCREEN_H / camZoom;
        camX = (float)sx - visW / 2.0f;
        camY = (float)sy - visH / 2.0f;
        world.map.reveal(localPlayer, (int)e.x + d.w / 2, (int)e.y + d.h / 2, 14);
        fogMaskTick = -1;
    };
    auto shoot = [&](const char* name) {
        fs::path path = outDir / (std::string(name) + ".png");
        shotFile = path.string();
        render();
        bool ok = fs::exists(path) && fs::file_size(path) > 1000;
        check(ok, name);
        TraceLog(LOG_INFO, "VISUAL wrote %s (%lld bytes)", path.string().c_str(),
                 ok ? (long long)fs::file_size(path) : 0LL);
    };
    auto findPlace = [&](BldType t, int preferX, int preferY) -> Vec2i {
        const BldDef& d = bldDef(t);
        auto prep = [&](int x, int y) {
            for (int dy = 0; dy < d.h; dy++)
                for (int dx = 0; dx < d.w; dx++) {
                    int cx = x + dx, cy = y + dy;
                    if (!world.map.inBounds(cx, cy)) continue;
                    Cell& c = world.map.at(cx, cy);
                    c.terrain = Terrain::Clear;
                    c.overlay = Overlay::None;
                    c.height = 0;
                    c.ore = 0;
                }
            for (auto& e : world.ents) {
                if (!e.alive || e.isBuilding) continue;
                if ((int)e.x >= x && (int)e.x < x + d.w && (int)e.y >= y && (int)e.y < y + d.h)
                    e.alive = false;
            }
        };
        for (int r = 0; r < 22; r++)
            for (int dy = -r; dy <= r; dy++)
                for (int dx = -r; dx <= r; dx++) {
                    int x = preferX + dx, y = preferY + dy;
                    if (!world.map.inBounds(x, y)) continue;
                    prep(x, y);
                    if (world.canPlace(t, x, y, localPlayer)) return {x, y};
                }
        return {preferX, preferY};
    };

    // 全景：确认平坦 TMP（无随机丘陵抬升）
    sel.clear();
    selBuilding = cyId;
    centerOn(cyId, 1.0f);
    shoot("00_terrain_base");

    struct Case { BldType t; const char* tag; };
    const Case cases[] = {
        {BldType::PowerPlant, "powerplant"},
        {BldType::TeslaCoil, "teslacoil"},
        {BldType::WarFactory, "warfactory"},
        {BldType::Barracks, "barracks"},
        {BldType::Pillbox, "pillbox"},
    };

    // 逐建筑孤立拍摄：藏起其它建筑，避免基地杂讯干扰 bare/cage 对比
    auto hideOthers = [&](EID keep) {
        for (size_t i = 0; i < world.ents.size(); i++) {
            if ((EID)i == keep) continue;
            auto& e = world.ents[i];
            if (!e.alive || !e.isBuilding) continue;
            world.kill((EID)i, false);
        }
    };

    for (int slot = 0; slot < (int)(sizeof(cases) / sizeof(cases[0])); slot++) {
        const Case& cs = cases[slot];
        char label[64];
        // 远离开局基地，空地单栋拍摄
        int preferX = base.x + 18 + (slot % 3) * 10;
        int preferY = base.y + 14 + (slot / 3) * 10;
        Vec2i pos = findPlace(cs.t, preferX, preferY);
        world.players[localPlayer].money += 20000;
        EID id = world.spawnBuilding(localPlayer, cs.t, pos.x, pos.y, true);
        snprintf(label, sizeof(label), "spawn %s", cs.tag);
        check(world.valid(id), label);
        if (!world.valid(id)) continue;

        hideOthers(id);
        World::Ent& e = world.ents[id];
        int mkf = g_sprites.bldMkFrames(cs.t);
        snprintf(label, sizeof(label), "%s has mk frames", cs.tag);
        check(mkf > 1, label);

        centerOn(id, 1.6f);

        // 建造中段
        sel.clear();
        selBuilding = id;
        e.constructAnim = std::max(1, mkf * 5 / 2);
        e.selling = false;
        snprintf(label, sizeof(label), "01_%s_mk_mid", cs.tag);
        shoot(label);

        // 建造末段（接近成型）
        e.constructAnim = 3;
        snprintf(label, sizeof(label), "02_%s_mk_late", cs.tag);
        shoot(label);

        // 成品：先无框，再同机位有虚线笼——成对对比用
        e.constructAnim = 0;
        e.selling = false;
        {
            int cid = world.players[localPlayer].colorId;
            const Sprite& cageS = g_sprites.building(cs.t, cid, false);
            const BldDef& bd = bldDef(cs.t);
            Vector2 p = bldScreenPos(e);
            int npx = 0, npy = 0, spx = 0, spy = 0;
            tileToScreen((int)e.x, (int)e.y, npx, npy);
            tileToScreen((int)e.x + bd.w - 1, (int)e.y + bd.h - 1, spx, spy);
            float footDepth = (float)(spy + TILE_H - npy);
            float footHalfW = (float)(bd.w + bd.h) * (TILE_W / 4.0f);
            float artHalfW = (float)cageS.visW() * 0.5f;
            float cageHalfW = std::max(footHalfW, artHalfW);
            float visElev = (float)cageS.visElev();
            float halfD = std::clamp(std::min(footHalfW * 0.5f, visElev * 0.15f), 8.0f, 28.0f);
            float cageElev = std::clamp(visElev - 2.f * halfD, 8.0f, 240.0f);
            TraceLog(LOG_INFO,
                     "VISUAL METRICS %s: foot=%dx%d visL=%d visT=%d visR=%d visB=%d "
                     "visW=%d visElev=%d ox=%d oy=%d "
                     "anchor=(%.1f,%.1f) footDepth≈%.0f cageHalfW=%.1f halfD=%.1f cageElev=%.1f",
                     cs.tag, bd.w, bd.h, cageS.visL, cageS.visT, cageS.visR, cageS.visB,
                     cageS.visW(), cageS.visElev(), cageS.ox, cageS.oy,
                     p.x, p.y, footDepth, cageHalfW, halfD, cageElev);
        }
        sel.clear();
        selBuilding = INVALID_EID;
        snprintf(label, sizeof(label), "03_%s_bare", cs.tag);
        shoot(label);
        selBuilding = id;
        snprintf(label, sizeof(label), "03_%s_cage", cs.tag);
        shoot(label);

        // 出售中段（倒放）
        e.selling = true;
        e.constructAnim = mkf * 5 / 2;
        snprintf(label, sizeof(label), "04_%s_sell_mid", cs.tag);
        shoot(label);

        e.selling = false;
        e.constructAnim = 0;
        world.kill(id, false); // 拍完拆除并清占地
        selBuilding = INVALID_EID;
    }

    TraceLog(LOG_INFO, "VISUAL AUDIT DONE: fails=%d out=%s", fails, outDir.string().c_str());
    return fails;
}

