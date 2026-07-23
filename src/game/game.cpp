#include "game/game.h"
#include "game/campaign.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <cmath>
#include <cstring>
#include <algorithm>

// ===================== 初始化 =====================
void Game::init(bool windowed) {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI); // DPI 感知：帧缓冲=物理像素，输入仍为逻辑坐标
    InitWindow(SCREEN_W, SCREEN_H, "OpenRA2 - 共和国之辉 复刻");
    if (!windowed) {
        // 无边框全屏：窗口=桌面分辨率，逻辑画布 letterbox 缩放。
        // 任何显示器（含低分屏/高DPI缩放）都不会出现按钮落在屏幕外的问题。
        ToggleBorderlessWindowed();
    }
    SetTargetFPS(60);
    loadFont();
    g_sprites.init();
    g_sfx.init();
    g_sfx.initBgm();

    // 逻辑分辨率离屏缓冲：像素风点对点放大，高分屏不模糊
    canvas = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_POINT);

    // 迷雾贴图：黑菱形与半透菱形
    {
        PixBuf b(TILE_W, TILE_H);
        b.diamond(TILE_W / 2, TILE_H / 2, TILE_W / 2, TILE_H / 2, Color{0, 0, 0, 255});
        fogBlack = b.toTexture();
        PixBuf d(TILE_W, TILE_H);
        d.diamond(TILE_W / 2, TILE_H / 2, TILE_W / 2, TILE_H / 2, Color{0, 0, 0, 120});
        fogDim = d.toTexture();
    }
    minimap = LoadRenderTexture(256, 256);
    phase = Phase::MainMenu;
}

void Game::newGame(uint64_t seed) {
    // 阵营：本地玩家与每个 AI 均来自遭遇战设置界面的槽位配置（3=随机）
    campaignMission = -1;
    nextWave = 0;
    std::vector<Faction> factions;
    factions.push_back((Faction)cfgFaction);
    Rng frng(seed);
    for (int i = 0; i < cfgAI; i++)
        factions.push_back(aiFaction[i] >= 3 ? (Faction)frng.range(0, 2) : (Faction)aiFaction[i]);
    world.init(cfgMapSize, cfgMapSize, seed, 1, cfgAI, factions, cfgMapType);
    // 颜色：取槽位配置；冲突（与前面玩家同色）时顺延到下一个未用色
    bool used[MAX_PLAYERS] = {};
    world.players[0].colorId = cfgColor;
    used[cfgColor] = true;
    for (int i = 1; i < world.numPlayers; i++) {
        int c = aiColor[i - 1];
        while (used[c]) c = (c + 1) % MAX_PLAYERS;
        world.players[i].colorId = c;
        used[c] = true;
    }
    for (int i = 0; i < world.numPlayers; i++) world.players[i].money = cfgMoney;
    world.cratesEnabled = cfgCrates;
    world.aiAlliance = cfgAlliance;
    ais.assign(cfgAI, SkirmishAI{});
    for (int i = 0; i < cfgAI; i++) ais[i].reset(i + 1);
    sel.clear();
    selBuilding = INVALID_EID;
    placing = false;
    gameOver = victory = false;
    // 摄像机对准出生点
    for (auto& e : world.ents)
        if (e.alive && !e.isBuilding && e.player == 0) {
            int sx, sy;
            tileToScreen((int)e.x, (int)e.y, sx, sy);
            camX = (float)sx - (SCREEN_W - sidebarW) / 2.0f;
            camY = (float)sy - SCREEN_H / 2.0f;
            break;
        }
    message("找到基地车，双击或按 D 展开！");
    phase = Phase::InGame;
}

// ===================== 战役 =====================
void Game::newCampaignGame(int mission) {
    const MissionDef& md = missionTable()[mission];
    campaignMission = mission;
    nextWave = 0;
    std::vector<Faction> factions;
    factions.push_back(md.playerFaction);
    for (Faction f : md.aiFactions) factions.push_back(f);
    // 固定种子：战役地图可复现
    world.init(md.mapSize, md.mapSize, 20260723ull + mission * 977, 1, (int)md.aiFactions.size(), factions, md.mapType);
    int pool[MAX_PLAYERS], pn = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (i != cfgColor) pool[pn++] = i;
    world.players[0].colorId = cfgColor;
    for (int i = 1; i < world.numPlayers; i++) world.players[i].colorId = pool[(i - 1) % pn];
    for (int i = 0; i < world.numPlayers; i++) world.players[i].money = md.money;
    world.cratesEnabled = cfgCrates;
    world.aiAlliance = cfgAlliance;
    ais.assign((int)md.aiFactions.size(), SkirmishAI{});
    for (int i = 0; i < (int)ais.size(); i++) ais[i].reset(i + 1);
    sel.clear();
    selBuilding = INVALID_EID;
    placing = false;
    gameOver = victory = false;
    for (auto& e : world.ents)
        if (e.alive && !e.isBuilding && e.player == 0) {
            int sx, sy;
            tileToScreen((int)e.x, (int)e.y, sx, sy);
            camX = (float)sx - (SCREEN_W - sidebarW) / 2.0f;
            camY = (float)sy - SCREEN_H / 2.0f;
            break;
        }
    message(md.brief);
    phase = Phase::InGame;
}

// 战役波次：为敌方玩家1刷出脚本部队，攻击移动至玩家基地
void Game::spawnCampaignWave() {
    const MissionDef& md = missionTable()[campaignMission];
    const MissionWave& w = md.waves[nextWave];
    // 锚点：敌方玩家1的建造厂 > 任意建筑 > 任意单位
    float ax = -1, ay = -1;
    for (auto& e : world.ents)
        if (e.alive && e.player == 1 && e.isBuilding && e.btype == BldType::ConYard) { ax = e.x; ay = e.y; break; }
    if (ax < 0)
        for (auto& e : world.ents)
            if (e.alive && e.player == 1 && e.isBuilding) { ax = e.x; ay = e.y; break; }
    if (ax < 0)
        for (auto& e : world.ents)
            if (e.alive && e.player == 1) { ax = e.x; ay = e.y; break; }
    if (ax < 0) return; // 敌方已无依托，不刷
    // 目标：玩家任意建筑
    float px = ax, py = ay;
    for (auto& e : world.ents)
        if (e.alive && e.player == 0 && e.isBuilding) { px = e.x; py = e.y; break; }
    std::vector<EID> spawned;
    for (UnitType t : w.units) {
        const UnitDef& ud = unitDef(t);
        int dom = ud.pathDomain();
        int bx = -1, by = -1;
        for (int r = 3; r < 16 && bx < 0; r++)
            for (int dy = -r; dy <= r && bx < 0; dy++)
                for (int dx = -r; dx <= r && bx < 0; dx++) {
                    if (std::max(abs(dx), abs(dy)) != r) continue;
                    int nx = (int)ax + dx, ny = (int)ay + dy;
                    if (world.passableFor(nx, ny, dom) && !world.bldBlocked(nx, ny)
                        && world.unitAtCell(nx, ny) == INVALID_EID) { bx = nx; by = ny; }
                }
        if (bx < 0) continue;
        spawned.push_back(world.spawnUnit(1, t, bx + 0.5f, by + 0.5f));
    }
    if (!spawned.empty()) {
        world.orderMove(spawned, px, py, true);
        world.eva(0, "警告：敌军增援抵达战场");
    }
}

// ===================== 存档/读档 =====================
// 文件格式：8 字节 Game 魔数 + Game 头（战役状态/镜头/速度/AI 状态）+ World 全量状态
bool Game::saveGameFile(const char* path) {
    MakeDirectory("saves");
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite("RA2GAME1", 1, 8, f) == 8;
    auto w = [&](const auto& v) { if (ok && fwrite(&v, sizeof(v), 1, f) != 1) ok = false; };
    w(campaignMission);
    uint64_t nw = (uint64_t)nextWave;
    w(nw);
    w(localPlayer);
    w(camX); w(camY);
    w(gameSpeed);
    // AI 状态（思考计时/进攻波次/海军检测缓存，避免读档后行为跳变）
    uint32_t an = (uint32_t)ais.size();
    w(an);
    for (const SkirmishAI& a : ais) {
        w(a.player); w(a.thinkTimer); w(a.attackWave); w(a.attackTimer); w(a.difficulty);
        w(a.hasWater); w(a.navalPlaceable); w(a.navalCheckCd); w(a.navalFail);
    }
    if (ok) ok = world.saveGame(f);
    fclose(f);
    return ok;
}

bool Game::loadGameFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    bool ok = true;
    char magic[8];
    ok = fread(magic, 1, 8, f) == 8 && memcmp(magic, "RA2GAME1", 8) == 0;
    auto r = [&](auto& v) { if (ok && fread(&v, sizeof(v), 1, f) != 1) ok = false; };
    r(campaignMission);
    uint64_t nw = 0;
    r(nw); nextWave = (size_t)nw;
    r(localPlayer);
    r(camX); r(camY);
    r(gameSpeed);
    uint32_t an = 0;
    r(an);
    if (an > MAX_PLAYERS) ok = false;
    if (ok) {
        ais.assign(an, SkirmishAI{});
        for (SkirmishAI& a : ais) {
            r(a.player); r(a.thinkTimer); r(a.attackWave); r(a.attackTimer); r(a.difficulty);
            r(a.hasWater); r(a.navalPlaceable); r(a.navalCheckCd); r(a.navalFail);
        }
        ok = world.loadGame(f);
    }
    fclose(f);
    if (!ok) return false;
    // 界面状态复位（与开局一致）
    sel.clear();
    selBuilding = INVALID_EID;
    placing = false;
    targetingSW = SWType::COUNT;
    sideMode = 0;
    paused = false;
    showMenu = false;
    gameOver = victory = false;
    evaLines.clear();
    phase = Phase::InGame;
    return true;
}


void Game::loadFont() {
    // 自动收集全部界面字符：数据表名称 + 界面文本 + ASCII，避免漏字显示为 '?'
    std::string all;
    for (int i = 0; i < (int)UnitType::COUNT; i++) all += unitDef((UnitType)i).name;
    for (int i = 0; i < (int)BldType::COUNT; i++) all += bldDef((BldType)i).name;
    for (int i = 0; i < (int)SWType::COUNT; i++) all += swDef((SWType)i).name;
    for (int i = 0; i < 3; i++) all += factionName((Faction)i);
    for (int c = 32; c < 127; c++) all += (char)c;
    all += "建筑防御步兵车辆资金电低电力就绪暂停胜利失败游戏菜单再来一局继续游戏重新开始退出游戏"
           "左键选择框选右键移动攻击展开停止回基地卖设集结点已出售无法在此放置工程师前往占领建造厂已部署"
           "找到基地车双击或按提示无法建造缺前置建筑或生产队列忙条件取消已选单位新目标警告维修"
           "主要选项遭遇战颜色金钱速度小地图无足够位置确认返回合计摧毁敌军所取得指挥中心被占领等待剩余秒正在加载中"
           "共和国之辉像素复刻电脑数量初始地图尺寸选择开始中小大指挥官主随机"
           "程序生成素材玩家阵值·切换需充点击已发射"
           "侦测到敌方核弹闪电风暴接近中遭受攻击单位损失被摧毁占领电力减缓"
           "警戒模式按视野索敌散布同类编队设定音乐开关"
           "载员登船卸载完成靠岸地点键"
           "更换一张添加移除你慢普通快进入（）"
           "保存读取进度音量补给箱互相结盟档"
           "战役任务简报边境冲突近海防御决胜时刻坚守十分钟歼灭所增援抵达战场类型陆岛屿湖泊第波";
    // 战役任务表文本与 HUD 动态文本：全部字模必须收录，否则显示 '?'
    for (const MissionDef& md : missionTable()) { all += md.name; all += md.brief; }
    all += "目标：歼灭所有敌军坚守·敌军增援将至（第/波）%：（），！为固";
    int count = 0;
    int* cps = LoadCodepoints(all.c_str(), &count);
    const char* paths[] = {
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
    };
    for (auto p : paths) {
        if (FileExists(p)) {
            font = LoadFontEx(p, 18, cps, count);
            TraceLog(LOG_INFO, "RA2 font: %s baseSize=%d glyphs=%d", p, font.baseSize, font.glyphCount);
            if (font.baseSize > 0 && font.glyphCount > count / 2) { fontOk = true; break; }
        }
    }
    UnloadCodepoints(cps);
    if (!fontOk) font = GetFontDefault();
}

void Game::shutdown() {
    g_sfx.shutdown();
    UnloadRenderTexture(canvas);
    UnloadRenderTexture(minimap);
    UnloadTexture(fogBlack);
    UnloadTexture(fogDim);
    if (previewTex.id > 0) UnloadTexture(previewTex);
    if (fontOk) UnloadFont(font);
    CloseWindow();
}

// ===================== 坐标 =====================
void Game::worldToScreen(float wx, float wy, int& sx, int& sy) const {
    sx = (int)(wx - camX);
    sy = (int)(wy - camY);
}
void Game::screenToWorld(int sx, int sy, float& wx, float& wy) const {
    wx = sx + camX;
    wy = sy + camY;
}

Vector2 Game::unitScreenPos(const World::Ent& e) const {
    // 瓦片浮点坐标 → 世界像素：sx=(x-y)*32, sy=(x+y)*16
    float wx = (e.x - e.y) * (TILE_W / 2.0f);
    float wy = (e.x + e.y) * (TILE_H / 2.0f);
    return {wx - camX, wy - camY};
}

Vector2 Game::bldScreenPos(const World::Ent& e) const {
    const BldDef& d = bldDef(e.btype);
    // 锚点 = 建筑占地东南角瓦片的南角点
    int px, py;
    tileToScreen((int)e.x + d.w - 1, (int)e.y + d.h - 1, px, py);
    return {(float)px - camX, (float)py + TILE_H - camY};
}

// ===================== 主循环 =====================
void Game::run() {
    while (!WindowShouldClose()) {
        g_sfx.updateBgm();
        if (phase == Phase::InGame) {
            handleInput();
            if (!paused && !gameOver) {
                static const float muls[] = {0.5f, 1.0f, 2.0f}; // 慢/普通/快
                logicAcc += GetFrameTime() * muls[gameSpeed % 3];
                float step = 1.0f / LOGIC_FPS;
                int n = 0;
                while (logicAcc >= step && n < 4) {
                    logic();
                    logicAcc -= step;
                    n++;
                }
                if (n == 4) logicAcc = 0;
            }
        }
        render();
    }
}

void Game::smokeTest(int frames) {
    newGame(20260722);
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
            if (e.utype == UnitType::Harvester && (int)e.state < 12) harvState[(int)e.state]++;
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
        // ---- 碾压验证：重甲坦克碾过敌方无甲步兵 ----
        {
            int cy2 = cy - 8;
            EID tank = world.spawnUnit(1, UnitType::Rhino, cx - 3.5f, cy2 + 0.5f);
            EID inf = world.spawnUnit(0, UnitType::Conscript, cx + 2.5f, cy2 + 0.5f);
            world.orderMove({tank}, cx + 4.5f, cy2 + 0.5f, false);
            for (int i = 0; i < 400 && world.valid(inf); i++) world.update();
            bool sawLost = false;
            for (const auto& ev : world.evaQueue)
                if (ev.player == 0 && ev.text.find("单位损失") != std::string::npos) sawLost = true;
            TraceLog(LOG_INFO, "crush verify: infantry alive=%d (expect 0), eva unitLost=%d (expect 1)",
                     (int)world.valid(inf), (int)sawLost);
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
                TraceLog(LOG_INFO, "naval verify: no 8x8 water on map, skipped");
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
                    // 陆地靶子：鱼雷不得攻击（HP 不变）
                    if (world.valid(sub)) {
                        EID landTank = world.spawnUnit(0, UnitType::Rhino, lx + 0.5f, ly + 0.5f);
                        int ltHp0 = world.ents[landTank].hp;
                        for (int i = 0; i < 200 && world.valid(landTank); i++) world.update();
                        navalOnlyOk = world.valid(landTank) && world.ents[landTank].hp == ltHp0;
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
            // 3) 幻影坦克：静止进入伪装（场内无敌军，不会开火解除）
            EID mir = world.spawnUnit(1, UnitType::MirageTank, bx + 6.5f, by + 2.5f);
            for (int i = 0; i < 120; i++) world.update();
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
    }
    // ---- 战役验证：任务表 + 首波增援刷出 ----
    {
        bool tblOk = missionTable().size() == 3;
        newCampaignGame(0);
        int before = 0;
        for (auto& e : world.ents) if (e.alive && e.player == 1 && !e.isBuilding) before++;
        for (int i = 0; i < 2800; i++) logic(); // 跑过首波（2700 tick）
        int after = 0;
        for (auto& e : world.ents) if (e.alive && e.player == 1 && !e.isBuilding) after++;
        bool waveOk = nextWave >= 1 && after >= before + 2; // 首波 4 单位（途中可能战损，放宽）
        TraceLog(LOG_INFO, "campaign verify: table=%d waveSpawn=%d (p1 units %d -> %d, nextWave=%zu)",
                 (int)tblOk, (int)waveOk, before, after, nextWave);
    }
    TraceLog(LOG_INFO, "smoke test done: %d frames, ents=%zu tick=%llu", frames, world.ents.size(),
             (unsigned long long)world.tick);
}

void Game::logic() {
    world.update();
    for (auto& ai : ais) ai.update(world);

    // 听者位置 = 视野中心瓦片
    {
        int ltx, lty;
        screenToTile(camX + (SCREEN_W - sidebarW) / 2.0f, camY + SCREEN_H / 2.0f, ltx, lty);
        g_sfx.setListener((float)ltx, (float)lty);
    }
    // 低电力警报（上升沿触发）
    {
        bool low = world.players[localPlayer].lowPower();
        if (low && !wasLowPower) {
            g_sfx.play(Sfx::Alarm, 0.8f);
            message("电力不足，生产减缓");
        }
        wasLowPower = low;
    }

    // EVA 播报：消费世界事件 → 字幕队列（仅本地玩家）
    while (!world.evaQueue.empty()) {
        const World::EvaEvent& ev = world.evaQueue.front();
        if (ev.player == localPlayer && evaLines.size() < 4) {
            evaLines.push_back(ev.text);
            g_sfx.play(Sfx::Eva, 0.7f);
        }
        world.evaQueue.pop_front();
    }
    // 逐条显示（不覆盖当前消息）
    if (msgTimer <= 0 && !evaLines.empty()) {
        msg = evaLines.front();
        msgTimer = 2.6f;
        evaLines.pop_front();
    }

    // 胜负判定
    if (!gameOver) {
        bool meDead = world.players[0].defeated;
        if (campaignMission >= 0) {
            // 战役：先刷波次再判定
            const MissionDef& md = missionTable()[campaignMission];
            if (nextWave < md.waves.size() && world.tick >= (uint64_t)md.waves[nextWave].atTick) {
                spawnCampaignWave();
                nextWave++;
            }
            if (meDead) { gameOver = true; victory = false; }
            else if (md.objective == 1) {
                // 存活目标：坚守到指定帧数
                if (world.tick >= (uint64_t)md.objectiveTick) { gameOver = true; victory = true; }
            } else {
                // 歼灭目标：敌军全灭且脚本波次刷完
                bool allAIDead = true;
                for (int i = 1; i < world.numPlayers; i++)
                    if (!world.players[i].defeated) allAIDead = false;
                if (allAIDead && nextWave >= md.waves.size()) { gameOver = true; victory = true; }
            }
        } else {
            bool allAIDead = true;
            for (int i = 1; i < world.numPlayers; i++)
                if (!world.players[i].defeated) allAIDead = false;
            if (meDead) { gameOver = true; victory = false; }
            else if (allAIDead) { gameOver = true; victory = true; }
        }
    }
    if (msgTimer > 0) msgTimer -= 1.0f / LOGIC_FPS;
}

// ===================== 输入 =====================
EID Game::pickUnit(int mx, int my) const {
    EID best = INVALID_EID;
    float bd = 18;
    for (size_t i = 0; i < world.ents.size(); i++) {
        const World::Ent& e = world.ents[i];
        if (!e.alive || e.isBuilding) continue;
        if (e.player != localPlayer && world.map.fogAt(localPlayer, (int)e.x, (int)e.y) != FOG_VISIBLE) continue;
        Vector2 p = unitScreenPos(e);
        // 飞行单位点其空中机身位置
        if (unitDef(e.utype).isAir() && e.state != UState::Landed) p.y -= AIR_ALT;
        float d = distf(p.x, p.y, (float)mx, (float)my);
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}

EID Game::pickBuilding(int mx, int my) const {
    // 倒序遍历：后建的建筑渲染在上层，优先命中（符合点击直觉）
    for (int i = (int)world.ents.size() - 1; i >= 0; i--) {
        const World::Ent& e = world.ents[i];
        if (!e.alive || !e.isBuilding) continue;
        if (e.player != localPlayer && world.map.fogAt(localPlayer, (int)e.x, (int)e.y) != FOG_VISIBLE) continue;
        Vector2 p = bldScreenPos(e);
        const Sprite& s = g_sprites.building(e.btype, world.players[e.player].colorId, false);
        if (mx >= p.x - s.ox && mx <= p.x - s.ox + s.tex.width &&
            my >= p.y - s.oy && my <= p.y - s.oy + s.tex.height)
            return (int)i;
    }
    return INVALID_EID;
}

void Game::doSelect(int mx, int my, bool additive) {
    if (!additive) { sel.clear(); selBuilding = INVALID_EID; }
    EID u = pickUnit(mx, my);
    if (u != INVALID_EID) {
        if (world.ents[u].player == localPlayer) sel.push_back(u);
        return;
    }
    EID b = pickBuilding(mx, my);
    if (b != INVALID_EID && world.ents[b].player == localPlayer) {
        selBuilding = b;
        return;
    }
}

void Game::doBoxSelect(Rectangle r, bool additive) {
    if (!additive) { sel.clear(); selBuilding = INVALID_EID; }
    for (size_t i = 0; i < world.ents.size(); i++) {
        const World::Ent& e = world.ents[i];
        if (!e.alive || e.isBuilding || e.player != localPlayer) continue;
        Vector2 p = unitScreenPos(e);
        // 飞行单位以空中机身位置框选
        if (unitDef(e.utype).isAir() && e.state != UState::Landed) p.y -= AIR_ALT;
        if (p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height)
            sel.push_back((int)i);
    }
}

void Game::issueSmartOrder(int mx, int my) {
    float wx, wy;
    screenToWorld(mx, my, wx, wy);
    int tx, ty;
    screenToTile(wx, wy, tx, ty);
    if (!world.map.inBounds(tx, ty)) return;

    // 目标：敌人 → 攻击；矿 → 采矿；否则移动
    EID eu = pickUnit(mx, my);
    EID eb = pickBuilding(mx, my);
    EID enemy = INVALID_EID;
    if (eu != INVALID_EID && world.ents[eu].player != localPlayer) enemy = eu;
    if (enemy == INVALID_EID && eb != INVALID_EID && world.ents[eb].player != localPlayer) enemy = eb;

    bool hasEngineer = false, hasHarvester = false;
    for (EID id : sel) {
        if (!world.valid(id)) continue;
        if (world.ents[id].utype == UnitType::Engineer) hasEngineer = true;
        if (world.ents[id].utype == UnitType::Harvester) hasHarvester = true;
    }

    if (enemy != INVALID_EID) {
        if (hasEngineer && world.ents[enemy].isBuilding && bldDef(world.ents[enemy].btype).capturable) {
            std::vector<EID> engs;
            for (EID id : sel) if (world.valid(id) && world.ents[id].utype == UnitType::Engineer) engs.push_back(id);
            world.orderCapture(engs, enemy);
            message("工程师：前往占领");
        } else {
            world.orderAttack(sel, enemy);
        }
        return;
    }
    // 右键己方运输载具 → 步兵登船
    if (eu != INVALID_EID && world.ents[eu].player == localPlayer && !world.ents[eu].isBuilding
        && unitDef(world.ents[eu].utype).cargoCap > 0) {
        std::vector<EID> inf;
        for (EID id : sel)
            if (world.valid(id) && !world.ents[id].isBuilding && unitDef(world.ents[id].utype).isInfantry())
                inf.push_back(id);
        if (!inf.empty()) {
            world.orderBoard(inf, eu);
            message("步兵登船中");
            return;
        }
    }
    // 友军建筑：工程师右键受损建筑 → 进入修复
    if (eb != INVALID_EID && world.ents[eb].player == localPlayer) {
        if (hasEngineer && world.ents[eb].hp < bldDef(world.ents[eb].btype).hp) {
            std::vector<EID> engs;
            for (EID id : sel) if (world.valid(id) && world.ents[id].utype == UnitType::Engineer) engs.push_back(id);
            world.orderRepair(engs, eb);
            message("工程师：前往修复");
        }
        return;
    }
    const Cell& c = world.map.at(tx, ty);
    if (hasHarvester && c.ore > 0) {
        std::vector<EID> harv;
        std::vector<EID> rest;
        for (EID id : sel) {
            if (!world.valid(id)) continue;
            if (world.ents[id].utype == UnitType::Harvester) harv.push_back(id);
            else rest.push_back(id);
        }
        world.orderHarvest(harv, tx, ty);
        if (!rest.empty()) world.orderMove(rest, (float)tx, (float)ty, kDown(KEY_A));
        return;
    }
    world.orderMove(sel, (float)tx, (float)ty, kDown(KEY_A));
}

void Game::message(const std::string& m) {
    msg = m;
    msgTimer = 4;
}

void Game::handleInput() {
    updateCamera();
    Vector2 mouse = mousePos();
    bool overUI = mouse.x > SCREEN_W - sidebarW;
    if (showMenu || gameOver) {
        // 菜单点击在 render 中处理（立即模式）
        return;
    }

    // 超武目标选择模式
    if (targetingSW != SWType::COUNT) {
        if (mPressed(MOUSE_RIGHT_BUTTON) || kPressed(KEY_ESCAPE)) {
            targetingSW = SWType::COUNT;
            return;
        }
        if (mPressed(MOUSE_LEFT_BUTTON) && !overUI) {
            float wx, wy;
            screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
            int tx, ty;
            screenToTile(wx, wy, tx, ty);
            if (world.map.inBounds(tx, ty)) {
                SWType t = targetingSW;
                if (world.launchSW(localPlayer, t, tx + 0.5f, ty + 0.5f)) {
                    message(std::string(swDef(t).name) + "已发射");
                }
                targetingSW = SWType::COUNT;
            }
        }
        return;
    }

    // 维修/出售点击模式（侧边栏 RA2 标志性按钮；执行一次后自动退出）
    if (sideMode != 0) {
        if (mPressed(MOUSE_RIGHT_BUTTON) || kPressed(KEY_ESCAPE)) { sideMode = 0; return; }
        if (mPressed(MOUSE_LEFT_BUTTON) && !overUI) {
            EID b = pickBuilding((int)mouse.x, (int)mouse.y);
            if (b != INVALID_EID && world.ents[b].player == localPlayer) {
                if (sideMode == 2) {
                    if (world.ents[b].btype != BldType::ConYard) { world.sellBuilding(b); message("已出售建筑"); }
                    else message("建造厂不可出售");
                } else {
                    if (world.repairBuilding(b)) message("建筑已修复");
                    else message("无需维修或资金不足");
                }
            }
            sideMode = 0;
        }
        return; // 该模式下屏蔽选择/框选
    }

    // 放置建筑模式
    if (placing) {
        if (mPressed(MOUSE_RIGHT_BUTTON) || kPressed(KEY_ESCAPE)) {
            placing = false;
            world.players[localPlayer].placingBld = BldType::COUNT;
            return;
        }
        if (mPressed(MOUSE_LEFT_BUTTON) && !overUI) {
            float wx, wy;
            screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
            int tx, ty;
            screenToTile(wx, wy, tx, ty);
            BldType t = world.players[localPlayer].placingBld;
            const BldDef& d = bldDef(t);
            int bx = tx - d.w / 2, by = ty - d.h / 2;
            if (world.placeBuilding(localPlayer, t, bx, by)) {
                placing = false;
                if (!kDown(KEY_LEFT_SHIFT)) world.players[localPlayer].placingBld = BldType::COUNT;
                else { world.players[localPlayer].placingBld = t; placing = true; }
            } else {
                message("无法在此放置");
            }
        }
        return;
    }

    if (!overUI) {
        // 左键选择
        if (mPressed(MOUSE_LEFT_BUTTON)) {
            dragging = true;
            dragStart = mouse;
        }
        if (dragging && mReleased(MOUSE_LEFT_BUTTON)) {
            Rectangle r{
                std::min(dragStart.x, mouse.x), std::min(dragStart.y, mouse.y),
                fabsf(mouse.x - dragStart.x), fabsf(mouse.y - dragStart.y)};
            bool add = kDown(KEY_LEFT_SHIFT);
            if (r.width < 6 && r.height < 6) doSelect((int)mouse.x, (int)mouse.y, add);
            else doBoxSelect(r, add);
            dragging = false;
        }
        // 右键指令
        if (mPressed(MOUSE_RIGHT_BUTTON) && !dragging) {
            if (!sel.empty()) issueSmartOrder((int)mouse.x, (int)mouse.y);
            else { selBuilding = INVALID_EID; }
        }
    }

    // 快捷键
    if (kPressed(KEY_S)) world.orderStop(sel);
    if (kPressed(KEY_U) && !sel.empty()) world.orderUnload(sel); // 运输船卸载
    if (kPressed(KEY_D)) {
        for (EID id : sel)
            if (world.valid(id) && world.ents[id].utype == UnitType::MCV) {
                world.orderDeploy(id);
                sel.erase(std::remove(sel.begin(), sel.end(), id), sel.end());
                message("建造厂已部署");
            }
        // 辐射工兵/重装大兵：部署/收起（RA2 原作同键）
        bool anyDeploy = false;
        for (EID id : sel)
            if (world.valid(id) && !world.ents[id].isBuilding
                && (world.ents[id].utype == UnitType::Desolator || world.ents[id].utype == UnitType::GuardianGI))
                anyDeploy = true;
        if (anyDeploy) {
            world.orderRadDeploy(sel);
            message("已切换部署状态");
        }
    }
    // X 散布（RA2 原作键位）
    if (kPressed(KEY_X) && !sel.empty()) {
        world.orderScatter(sel);
        message("单位散布");
    }
    // G 警戒（RA2 原作键位）
    if (kPressed(KEY_G) && !sel.empty()) {
        world.orderGuard(sel);
        message("警戒模式：按视野索敌");
    }
    // T 选择同类（RA2 原作键位）
    if (kPressed(KEY_T) && !sel.empty()) {
        bool types[(int)UnitType::COUNT] = {};
        for (EID id : sel)
            if (world.valid(id) && !world.ents[id].isBuilding) types[(int)world.ents[id].utype] = true;
        sel.clear();
        for (size_t i = 0; i < world.ents.size(); i++) {
            const World::Ent& e = world.ents[i];
            if (e.alive && !e.isBuilding && e.player == localPlayer && types[(int)e.utype])
                sel.push_back((int)i);
        }
        message("选择同类单位");
        g_sfx.play(Sfx::Click, 0.6f);
    }
    // 编队：Ctrl+数字设定 / 数字召回 / 双击数字跳转视角
    {
        bool ctrl = kDown(KEY_LEFT_CONTROL) || kDown(KEY_RIGHT_CONTROL);
        for (int n = 0; n <= 9; n++) {
            if (!kPressed(KEY_ZERO + n)) continue;
            if (ctrl) {
                groups[n] = sel;
                groups[n].erase(std::remove_if(groups[n].begin(), groups[n].end(), [&](EID id) {
                    return !world.valid(id) || world.ents[id].isBuilding;
                }), groups[n].end());
                message(TextFormat("编队 %d 已设定（%d 单位）", n, (int)groups[n].size()));
                g_sfx.play(Sfx::Click, 0.6f);
            } else {
                auto& g = groups[n];
                g.erase(std::remove_if(g.begin(), g.end(), [&](EID id) {
                    return !world.valid(id) || world.ents[id].isBuilding;
                }), g.end());
                if (g.empty()) continue;
                sel = g;
                selBuilding = INVALID_EID;
                double now = GetTime();
                if (lastGroupKey == n && now - lastGroupTap < 0.5) {
                    // 双击：视角跳到编队重心
                    float cx = 0, cy = 0;
                    for (EID id : g) {
                        Vector2 p = unitScreenPos(world.ents[id]);
                        cx += p.x + camX; cy += p.y + camY;
                    }
                    cx /= (float)g.size(); cy /= (float)g.size();
                    camX = cx - (SCREEN_W - sidebarW) / 2.0f;
                    camY = cy - SCREEN_H / 2.0f;
                }
                lastGroupKey = n;
                lastGroupTap = now;
            }
        }
    }
    // M 音乐开关
    if (kPressed(KEY_M)) {
        g_sfx.toggleBgm();
        message(g_sfx.bgmEnabled() ? "音乐：开" : "音乐：关");
    }
    // F5 快速存档 / F9 快速读档
    if (kPressed(KEY_F5)) message(saveGameFile(QUICKSAVE_PATH) ? "进度已保存" : "保存失败");
    if (kPressed(KEY_F9)) message(loadGameFile(QUICKSAVE_PATH) ? "进度已读取" : "读取失败（无存档）");
    // Delete 出售选中建筑（X 已让位于散布）
    if (kPressed(KEY_DELETE) && world.valid(selBuilding) && world.ents[selBuilding].player == localPlayer
        && world.ents[selBuilding].btype != BldType::ConYard) {
        world.sellBuilding(selBuilding);
        selBuilding = INVALID_EID;
        message("已出售建筑");
    }
    if (kPressed(KEY_H)) {
        for (auto& e : world.ents)
            if (e.alive && e.isBuilding && e.player == localPlayer && e.btype == BldType::ConYard) {
                Vector2 p = bldScreenPos(e);
                camX += p.x - (SCREEN_W - sidebarW) / 2.0f;
                camY += p.y - SCREEN_H / 2.0f;
                break;
            }
    }
    if (kPressed(KEY_P)) paused = !paused;
    if (kPressed(KEY_EQUAL) || kPressed(KEY_KP_ADD)) gameSpeed = std::min(2, gameSpeed + 1);
    if (kPressed(KEY_MINUS) || kPressed(KEY_KP_SUBTRACT)) gameSpeed = std::max(0, gameSpeed - 1);
    if (kPressed(KEY_ESCAPE)) {
        if (!sel.empty() || world.valid(selBuilding)) { sel.clear(); selBuilding = INVALID_EID; }
        else showMenu = true;
    }
    // 设置集结点
    if (kPressed(KEY_R) && world.valid(selBuilding)) {
        float wx, wy;
        screenToWorld((int)mouse.x, (int)mouse.y, wx, wy);
        int tx, ty;
        screenToTile(wx, wy, tx, ty);
        world.setRally(selBuilding, tx, ty);
        message("集结点已设置");
    }

    // 清理失效选择
    sel.erase(std::remove_if(sel.begin(), sel.end(), [&](EID id) { return !world.valid(id); }), sel.end());
    if (!world.valid(selBuilding)) selBuilding = INVALID_EID;
}

void Game::updateCamera() {
    int viewW = SCREEN_W - sidebarW;
    float sp = camSpeed * gameSpeed;
    if (kDown(KEY_LEFT)) camX -= sp;
    if (kDown(KEY_RIGHT)) camX += sp;
    if (kDown(KEY_UP)) camY -= sp;
    if (kDown(KEY_DOWN)) camY += sp;
    Vector2 m = mousePos();
    if (m.x < 4) camX -= sp;
    if (m.x > viewW - 4 && m.x < viewW + 2) camX += sp;
    if (m.y < 4) camY -= sp;
    if (m.y > SCREEN_H - 4) camY += sp;
    // 边界
    float minX = -(float)world.map.h * TILE_W / 2 - 200;
    float maxX = (float)world.map.w * TILE_W / 2 + 200 - viewW;
    float maxY = (float)(world.map.w + world.map.h) * TILE_H / 2 + 100 - SCREEN_H;
    camX = std::clamp(camX, minX, maxX);
    camY = std::clamp(camY, -100.0f, maxY);
}

// ===================== 渲染 =====================
void Game::render() {
    // 菜单阶段：独立渲染路径
    if (phase != Phase::InGame) {
        BeginTextureMode(canvas);
        ClearBackground(BLACK);
        if (phase == Phase::MainMenu) drawMainMenu();
        else if (phase == Phase::MissionSelect) drawMissionSelect();
        else drawSetup();
        EndTextureMode();
        if (!shotFile.empty()) {
            Image img = LoadImageFromTexture(canvas.texture);
            ImageFlipVertical(&img);
            ExportImage(img, shotFile.c_str());
            UnloadImage(img);
            shotFile.clear();
        }
        BeginDrawing();
        ClearBackground(BLACK);
        float rw = (float)GetRenderWidth(), rh = (float)GetRenderHeight();
        Rectangle src{0, 0, (float)SCREEN_W, -(float)SCREEN_H};
        Rectangle dst{(rw - rh * SCREEN_W / SCREEN_H) / 2, 0, rh * SCREEN_W / SCREEN_H, rh};
        if (rw / SCREEN_W < rh / SCREEN_H) dst = Rectangle{0, (rh - rw * SCREEN_H / SCREEN_W) / 2, rw, rw * SCREEN_H / SCREEN_W};
        DrawTexturePro(canvas.texture, src, dst, {0, 0}, 0, WHITE);
        EndDrawing();
        return;
    }
    updateMinimap(); // 嵌套 RenderTexture 会破坏画布渲染状态，提前更新
    // 1. 逻辑分辨率渲染到离屏画布
    BeginTextureMode(canvas);
    ClearBackground(BLACK);
    drawWorld();
    drawEntities();
    drawEffectsLayer();
    drawFogLayer();
    drawPlacement();
    drawHUD();
    EndTextureMode();
    // 截图优先直接从画布导出（逻辑分辨率，不依赖帧缓冲状态）
    if (!shotFile.empty()) {
        Image img = LoadImageFromTexture(canvas.texture);
        ImageFlipVertical(&img); // RenderTexture Y 轴翻转
        ExportImage(img, shotFile.c_str());
        UnloadImage(img);
        shotFile.clear();
    }
    // 2. 点对点放大到物理帧缓冲
    BeginDrawing();
    ClearBackground(BLACK);
    float rw = (float)GetRenderWidth(), rh = (float)GetRenderHeight();
    Rectangle src{0, 0, (float)SCREEN_W, -(float)SCREEN_H};
    Rectangle dst{(rw - rh * SCREEN_W / SCREEN_H) / 2, 0, rh * SCREEN_W / SCREEN_H, rh};
    if (rw / SCREEN_W < rh / SCREEN_H) dst = Rectangle{0, (rh - rw * SCREEN_H / SCREEN_W) / 2, rw, rw * SCREEN_H / SCREEN_W};
    DrawTexturePro(canvas.texture, src, dst, {0, 0}, 0, WHITE);
    EndDrawing();
}

void Game::drawWorld() {
    int viewW = SCREEN_W - sidebarW;
    // 计算可见瓦片范围
    int x0, y0, x1, y1;
    screenToTile(camX, camY - 64, x0, y0);
    screenToTile(camX + viewW + 64, camY + SCREEN_H + 64, x1, y1);
    // 等距地图需要扩大范围
    int x2, y2, x3, y3;
    screenToTile(camX + viewW + 64, camY - 128, x2, y2);
    screenToTile(camX - 64, camY + SCREEN_H + 128, x3, y3);
    int minTX = std::min({x0, x1, x2, x3}) - 1, maxTX = std::max({x0, x1, x2, x3}) + 1;
    int minTY = std::min({y0, y1, y2, y3}) - 1, maxTY = std::max({y0, y1, y2, y3}) + 1;
    minTX = std::max(0, minTX); minTY = std::max(0, minTY);
    maxTX = std::min(world.map.w - 1, maxTX); maxTY = std::min(world.map.h - 1, maxTY);

    for (int ty = minTY; ty <= maxTY; ty++)
        for (int tx = minTX; tx <= maxTX; tx++) {
            const Cell& c = world.map.at(tx, ty);
            int px, py;
            tileToScreen(tx, ty, px, py);
            int sx = px - camX, sy = py - camY;
            if (sx < -TILE_W || sx > viewW + TILE_W || sy < -TILE_H || sy > SCREEN_H + TILE_H) continue;
            const Sprite& s = g_sprites.tile(c.terrain, c.variant & 3);
            DrawTexture(s.tex, sx - TILE_W / 2, sy, WHITE);
            // 装饰物在实体层按深度画
        }
}

void Game::drawEntities() {
    int viewW = SCREEN_W - sidebarW;
    // 深度排序绘制项
    struct Item { float depth; int kind; int id; }; // kind 0 单位 1 建筑 2 树
    std::vector<Item> items;
    for (size_t i = 0; i < world.ents.size(); i++) {
        const World::Ent& e = world.ents[i];
        if (!e.alive) continue;
        if (e.isBuilding) {
            const BldDef& d = bldDef(e.btype);
            items.push_back({e.x + e.y + d.w + d.h, 1, (int)i});
        } else {
            // 飞行单位始终最后绘制（浮于空中，压过地面一切）
            bool flying = unitDef(e.utype).isAir() && e.state != UState::Landed;
            items.push_back({e.x + e.y + (flying ? 1000.0f : 0.0f), 0, (int)i});
        }
    }
    // 树木
    int minTX = std::max(0, (int)((camY) / TILE_H) - world.map.h), maxX2 = world.map.w;
    for (int ty = 0; ty < world.map.h; ty++)
        for (int tx = 0; tx < world.map.w; tx++) {
            const Cell& c = world.map.at(tx, ty);
            if (c.overlay >= Overlay::Tree1 && c.overlay <= Overlay::Tree3) {
                int px, py;
                tileToScreen(tx, ty, px, py);
                int sx = px - camX, sy = py - camY;
                if (sx < -64 || sx > viewW + 64 || sy < -96 || sy > SCREEN_H + 64) continue;
                items.push_back({(float)(tx + ty) + 0.9f, 2, ty * world.map.w + tx});
            }
        }
    // 补给箱（地面道具，参与深度排序）
    for (size_t i = 0; i < world.crates.size(); i++) {
        const World::Crate& c = world.crates[i];
        if (!c.alive) continue;
        if (world.map.fogAt(localPlayer, c.x, c.y) != FOG_VISIBLE) continue;
        items.push_back({(float)(c.x + c.y) + 0.5f, 3, (int)i});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.depth < b.depth; });

    for (const Item& it : items) {
        if (it.kind == 3) {
            // 补给箱：木箱 + 顶盖高亮（闪烁提示可拾取）
            const World::Crate& c = world.crates[it.id];
            int px, py;
            tileToScreen(c.x, c.y, px, py);
            int sx = px - (int)camX, sy = py - (int)camY + TILE_H / 2;
            Color box{146, 108, 62, 255}, lid{176, 134, 80, 255};
            DrawRectangle(sx - 7, sy - 10, 14, 10, box);
            DrawRectangle(sx - 7, sy - 13, 14, 4, lid);
            DrawRectangleLines(sx - 7, sy - 13, 14, 13, Color{80, 58, 32, 255});
            if ((world.tick / 12) % 2) // 闪烁顶灯
                DrawCircle(sx, sy - 15, 2, c.kind == 0 ? Color{255, 220, 80, 255} : c.kind == 1 ? Color{120, 255, 120, 255} : Color{255, 120, 220, 255});
            continue;
        }
        if (it.kind == 2) {
            int tx = it.id % world.map.w, ty = it.id / world.map.w;
            const Cell& c = world.map.at(tx, ty);
            FogState fs = world.map.fogAt(localPlayer, tx, ty);
            if (fs == FOG_UNSEEN) continue;
            int px, py;
            tileToScreen(tx, ty, px, py);
            const Sprite& s = g_sprites.overlaySpr(c.overlay);
            Color tint = fs == FOG_SEEN ? Color{140, 140, 140, 255} : WHITE;
            DrawTexture(s.tex, px - (int)camX - s.ox, py + TILE_H / 2 - (int)camY - s.oy, tint);
            continue;
        }
        const World::Ent& e = world.ents[it.id];
        if (!e.isBuilding) {
            const UnitDef& ud = unitDef(e.utype);
            bool flying = ud.isAir() && e.state != UState::Landed;
            FogState fs = world.map.fogAt(localPlayer, (int)e.x, (int)e.y);
            if (e.player != localPlayer && fs != FOG_VISIBLE) continue;
            Vector2 p = unitScreenPos(e);
            if (p.x < -80 || p.x > viewW + 80 || p.y < -80 || p.y > SCREEN_H + 80) continue;
            int cid = world.players[e.player].colorId;
            // 阴影（地面投影，飞行中缩小变远）
            DrawEllipse((int)p.x, (int)p.y + 4, flying ? 8 : 14, flying ? 3 : 5,
                        Color{0, 0, 0, (unsigned char)(flying ? 50 : 70)});
            if (flying) p.y -= AIR_ALT;
            const Sprite& body = g_sprites.unitBody(e.utype, e.dir,
                e.utype == UnitType::Harvester ? (e.oreLoad > 10 ? 1 : 0) : e.walkFrame, cid);
            Color tint = (e.player != localPlayer && fs == FOG_SEEN) ? Color{120, 120, 120, 255} : WHITE;
            DrawTexture(body.tex, (int)p.x - body.ox, (int)p.y - body.oy, tint);
            if (g_sprites.hasTurret(e.utype)) {
                const Sprite& tur = g_sprites.unitTurret(e.utype, e.turretDir, cid);
                DrawTexture(tur.tex, (int)p.x - tur.ox, (int)p.y - tur.oy, tint);
            }
            // 血条与选择框
            bool selected = std::find(sel.begin(), sel.end(), it.id) != sel.end();
            if (selected) {
                DrawEllipseLines((int)p.x, (int)p.y + 4, 16, 6, GREEN);
            }
            if (selected || e.hp < ud.hp)
                drawHealthBar((int)p.x - 14, (int)p.y - 26, 28, (float)e.hp / ud.hp, selected);
            // 军衔标志（RA2 原作：老兵 1 杠，精英 2 杠金色）
            if (e.vetRank > 0) {
                Color rc = e.vetRank >= 2 ? Color{255, 200, 60, 255} : Color{220, 220, 220, 255};
                for (int i = 0; i < e.vetRank; i++) {
                    int vx = (int)p.x - 8 + i * 9, vy = (int)p.y + 8;
                    DrawTriangle({(float)vx, (float)vy + 4}, {(float)vx + 3, (float)vy}, {(float)vx + 6, (float)vy + 4}, rc);
                }
            }
            // 战机弹药指示
            if (selected && ud.ammo > 0) {
                for (int i = 0; i < ud.ammo; i++) {
                    Color ac = i < e.ammo ? Color{255, 200, 60, 255} : Color{70, 70, 74, 255};
                    DrawRectangle((int)p.x - 14 + i * 7, (int)p.y - 21, 5, 3, ac);
                }
            }
            // 铁幕无敌：暗化罩光
            if (e.invuln > 0) {
                DrawEllipse((int)p.x, (int)p.y - 8, 15, 11, Color{30, 26, 30, 110});
                DrawEllipseLines((int)p.x, (int)p.y - 8, 16, 12,
                    ((world.tick / 6) % 2) ? Color{200, 60, 50, 160} : Color{120, 30, 26, 160});
            }
        } else {
            FogState fs = world.map.fogAt(localPlayer, (int)e.x, (int)e.y);
            if (e.player != localPlayer && fs == FOG_UNSEEN) continue;
            Vector2 p = bldScreenPos(e);
            int cid = world.players[e.player].colorId;
            const Sprite& s = g_sprites.building(e.btype, cid, false);
            Color tint = (e.player != localPlayer && fs == FOG_SEEN) ? Color{110, 110, 110, 255} : WHITE;
            DrawTexture(s.tex, (int)p.x - s.ox, (int)p.y - s.oy, tint);
            const BldDef& d = bldDef(e.btype);
            if ((int)it.id == selBuilding) {
                // 选中框：沿占地外接菱形包围盒
                int x0, y0;
                tileToScreen((int)e.x, (int)e.y, x0, y0);
                int minx = x0 - (d.h - 1) * TILE_W / 2 - TILE_W / 2;
                int maxx = x0 + (d.w - 1) * TILE_W / 2 + TILE_W / 2;
                int maxy = y0 + (d.w + d.h - 2) * TILE_H / 2 + TILE_H;
                DrawRectangleLines(minx - (int)camX - 3, y0 - (int)camY - 3,
                                   maxx - minx + 6, maxy - y0 + 3, GREEN);
            }
            if ((int)it.id == selBuilding || e.hp < d.hp) {
                drawHealthBar((int)p.x - 20, (int)p.y - s.oy - 6, 40, (float)e.hp / d.hp, (int)it.id == selBuilding);
            }
            // 集结点标记
            if ((int)it.id == selBuilding && (e.btype == BldType::Barracks || e.btype == BldType::WarFactory)) {
                if (e.rallyX >= 0) {
                    int rx, ry;
                    tileToScreen(e.rallyX, e.rallyY, rx, ry);
                    DrawCircle(rx - (int)camX, ry - (int)camY + TILE_H / 2, 5, Color{0, 255, 0, 120});
                }
            }
            // 铁幕无敌建筑：暗化 + 红闪描边
            if (e.invuln > 0) {
                DrawRectangle((int)p.x - s.ox, (int)p.y - s.oy, s.tex.width, s.tex.height, Color{20, 16, 20, 90});
                DrawRectangleLines((int)p.x - s.ox, (int)p.y - s.oy, s.tex.width, s.tex.height,
                    ((world.tick / 6) % 2) ? Color{200, 60, 50, 150} : Color{110, 30, 26, 150});
            }
        }
    }

    // 弹道
    for (const Projectile& pr : world.projs) {
        // 瓦片浮点 → 世界像素
        float fx1 = (pr.x - pr.y) * (TILE_W / 2.0f);
        float fy1 = (pr.x + pr.y) * (TILE_H / 2.0f);
        int sx = (int)fx1 - (int)camX, sy = (int)fy1 - (int)camY - 6;
        if (pr.kind == ProjKind::Bullet) {
            DrawCircle(sx, sy, 2, Color{255, 230, 150, 255});
        } else if (pr.kind == ProjKind::Missile) {
            int dir = dirFromVec(pr.tx - pr.x, pr.ty - pr.y);
            const Sprite& s = g_sprites.projectile(1, dir);
            DrawTexture(s.tex, sx - s.ox, sy - s.oy, WHITE);
        } else if (pr.kind == ProjKind::Flak) {
            DrawCircle(sx, sy, 3, Color{160, 160, 170, 255});
        } else {
            const Sprite& s = g_sprites.projectile(0, 0);
            DrawTexture(s.tex, sx - s.ox, sy - s.oy, WHITE);
        }
    }

    // 核弹飞行：目标点准星 + 从天而降的弹体
    for (const Nuke& n : world.nukes) {
        if (!n.active) continue;
        float fx1 = (n.tx - n.ty) * (TILE_W / 2.0f);
        float fy1 = (n.tx + n.ty) * (TILE_H / 2.0f);
        int sx = (int)fx1 - (int)camX, sy = (int)fy1 - (int)camY;
        // 落点闪烁准星
        Color mc = (world.tick / 4) % 2 ? Color{255, 60, 50, 220} : Color{255, 200, 60, 220};
        DrawEllipseLines(sx, sy, 14, 7, mc);
        DrawLine(sx - 18, sy, sx - 6, sy, mc);
        DrawLine(sx + 6, sy, sx + 18, sy, mc);
        DrawLine(sx, sy - 10, sx, sy - 3, mc);
        DrawLine(sx, sy + 3, sx, sy + 10, mc);
        // 弹体：随时间从高空落下
        float prog = 1.0f - n.timer / 75.0f;
        int dropY = sy - (int)((1.0f - prog) * 420) - 24;
        // 尾焰
        DrawLine(sx, dropY - 16, sx, dropY - 40 - (int)(prog * 30), Color{255, 200, 90, 180});
        DrawLine(sx, dropY - 16, sx, dropY - 34 - (int)(prog * 26), Color{255, 240, 200, 220});
        // 弹体（白身红头）
        DrawLine(sx - 1, dropY - 16, sx - 1, dropY - 2, Color{210, 208, 200, 255});
        DrawLine(sx, dropY - 18, sx, dropY, Color{235, 232, 224, 255});
        DrawLine(sx + 1, dropY - 16, sx + 1, dropY - 2, Color{190, 188, 180, 255});
        DrawLine(sx, dropY, sx, dropY + 3, Color{220, 60, 50, 255});
    }

    // 疯狂伊文定时炸弹：闪烁红点 + 倒计时弧线
    for (const World::TimedBomb& b : world.timedBombs) {
        float fx = b.x, fy = b.y;
        if (world.valid(b.attachedTo)) {
            const World::Ent& t = world.ents[b.attachedTo];
            if (!t.alive) continue;
            fx = t.x; fy = t.y;
            if (t.isBuilding) { fx += bldDef(t.btype).w / 2.0f; fy += bldDef(t.btype).h / 2.0f; }
        }
        if (world.map.fogAt(localPlayer, (int)fx, (int)fy) != FOG_VISIBLE) continue;
        float wx = (fx - fy) * (TILE_W / 2.0f), wy = (fx + fy) * (TILE_H / 2.0f);
        int sx = (int)wx - (int)camX, sy = (int)wy - (int)camY - 14;
        DrawRectangle(sx - 3, sy - 3, 7, 7, Color{60, 40, 36, 255});
        DrawRectangleLines(sx - 3, sy - 3, 7, 7, Color{120, 60, 50, 255});
        if ((world.tick / 5) % 2) DrawCircle(sx, sy, 2, Color{255, 60, 50, 255});
    }
}

void Game::drawEffectsLayer() {
    for (const Effect& ef : world.effects) {
        auto toPx = [&](float tx, float ty, int& sx, int& sy) {
            float pxx = (tx - ty) * (TILE_W / 2.0f);
            float pyy = (tx + ty) * (TILE_H / 2.0f);
            sx = (int)pxx - (int)camX;
            sy = (int)pyy - (int)camY;
        };
        if (ef.kind == 0 || ef.kind == 4) {
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            int frame = ef.age * SpriteBank::EXPLOSION_FRAMES / ef.maxAge;
            const Sprite& s = g_sprites.explosion(frame);
            if (ef.kind == 4) {
                DrawTextureEx(s.tex, {(float)(sx - s.ox * 2), (float)(sy - s.oy * 2)}, 0, 2.0f, WHITE);
            } else {
                DrawTexture(s.tex, sx - s.ox, sy - s.oy - 8, WHITE);
            }
        } else if (ef.kind == 2) {
            // 磁暴电弧
            int sx, sy, tx2, ty2;
            toPx(ef.x, ef.y, sx, sy);
            toPx(ef.x2, ef.y2, tx2, ty2);
            sy -= 14; ty2 -= 10;
            float dx = (float)(tx2 - sx), dy = (float)(ty2 - sy);
            float len = sqrtf(dx * dx + dy * dy);
            int segs = (int)(len / 6) + 1;
            Vector2 prev{(float)sx, (float)sy};
            for (int i = 1; i <= segs; i++) {
                float t = (float)i / segs;
                float jx = 0, jy = 0;
                if (i < segs) {
                    jx = (float)((int)(ef.age * 37 + i * 91) % 11 - 5);
                    jy = (float)((int)(ef.age * 53 + i * 71) % 11 - 5);
                }
                Vector2 cur{sx + dx * t + jx, sy + dy * t + jy};
                DrawLineEx(prev, cur, 2.5f, Color{140, 180, 255, 220});
                DrawLineEx(prev, cur, 1.0f, Color{230, 245, 255, 255});
                prev = cur;
            }
        } else if (ef.kind == 3) {
            int sx, sy, tx2, ty2;
            toPx(ef.x, ef.y, sx, sy);
            toPx(ef.x2, ef.y2, tx2, ty2);
            sy -= 16; ty2 -= 10;
            float a = 1.0f - (float)ef.age / ef.maxAge;
            Color c1{255, 255, 255, (uint8_t)(255 * a)};
            Color c2{120, 220, 255, (uint8_t)(160 * a)};
            DrawLineEx({(float)sx, (float)sy}, {(float)tx2, (float)ty2}, 4.0f, c2);
            DrawLineEx({(float)sx, (float)sy}, {(float)tx2, (float)ty2}, 1.5f, c1);
        } else if (ef.kind == 1) {
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            const Sprite& s = g_sprites.smoke(ef.age * SpriteBank::SMOKE_FRAMES / ef.maxAge);
            DrawTexture(s.tex, sx - s.ox, sy - s.oy - ef.age / 2, WHITE);
        } else if (ef.kind == 5) {
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            const Sprite& s = g_sprites.muzzle();
            DrawTexture(s.tex, sx - s.ox, sy - s.oy - 12, WHITE);
        } else if (ef.kind == 6) {
            // 蘑菇云：闪光 → 火球 → 烟柱 + 环状冲击波
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            float t = (float)ef.age / ef.maxAge;
            if (ef.age < 6) {
                // 白闪全屏感（大圆）
                int r = 90 + ef.age * 30;
                DrawCircle(sx, sy - 20, (float)r, Color{255, 255, 240, (uint8_t)(220 - ef.age * 36)});
            }
            // 火球（膨胀后转暗）
            {
                int r = (int)(26 + t * 60);
                uint8_t ar = (uint8_t)(t < 0.5f ? 255 : 255 - (t - 0.5f) * 2 * 200);
                Color fire{255, (uint8_t)(200 - t * 160), (uint8_t)(80 - t * 70), ar};
                DrawEllipse(sx, sy - 14 - (int)(t * 40), r, r * 3 / 4, fire);
            }
            // 烟柱
            if (ef.age > 10) {
                float st = (float)(ef.age - 10) / (ef.maxAge - 10);
                int colH = (int)(st * 110);
                int colW = 16 + (int)(st * 26);
                DrawEllipse(sx, sy - 20 - colH / 2, colW, colH / 2 + 8, Color{90, 84, 80, (uint8_t)(230 - st * 160)});
                // 顶部蘑菇帽
                DrawEllipse(sx, sy - 24 - colH, colW + 22, 20, Color{120, 112, 106, (uint8_t)(240 - st * 170)});
            }
            // 冲击波环
            if (ef.age < 40) {
                float rt = (float)ef.age / 40;
                DrawEllipseLines(sx, sy - 4, 20 + rt * 220, 10 + rt * 100, Color{255, 230, 180, (uint8_t)(180 * (1 - rt))});
            }
        } else if (ef.kind == 7) {
            // 天降闪电：从高空到落点的锯齿折线 + 落点闪光
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            int topY = sy - 260;
            int segs = 7;
            Vector2 prev{(float)sx, (float)topY};
            for (int i = 1; i <= segs; i++) {
                float tt = (float)i / segs;
                int jx = i == segs ? 0 : (int)((ef.age * 71 + i * 131) % 17 - 8);
                Vector2 cur{sx + (float)jx, topY + (sy - topY) * tt};
                DrawLineEx(prev, cur, 3.0f, Color{180, 210, 255, 200});
                DrawLineEx(prev, cur, 1.2f, Color{240, 250, 255, 255});
                prev = cur;
            }
            DrawCircle(sx, sy, 8 + ef.age, Color{220, 240, 255, (uint8_t)(230 - ef.age * 24)});
        } else if (ef.kind == 8) {
            // 铁幕扩散：暗红能量环扩散
            int sx, sy;
            toPx(ef.x, ef.y, sx, sy);
            float t = (float)ef.age / ef.maxAge;
            float r = 10 + t * 110;
            DrawEllipseLines(sx, sy, r, r / 2, Color{220, 60, 50, (uint8_t)(230 * (1 - t))});
            DrawEllipseLines(sx, sy, r * 0.7f, r * 0.35f, Color{255, 120, 100, (uint8_t)(160 * (1 - t))});
        }
    }
}

void Game::drawFogLayer() {
    int viewW = SCREEN_W - sidebarW;
    int x0, y0, x1, y1, x2, y2, x3, y3;
    screenToTile(camX, camY - 64, x0, y0);
    screenToTile(camX + viewW + 64, camY + SCREEN_H + 64, x1, y1);
    screenToTile(camX + viewW + 64, camY - 128, x2, y2);
    screenToTile(camX - 64, camY + SCREEN_H + 128, x3, y3);
    int minTX = std::max(0, std::min({x0, x1, x2, x3}) - 1), maxTX = std::min(world.map.w - 1, std::max({x0, x1, x2, x3}) + 1);
    int minTY = std::max(0, std::min({y0, y1, y2, y3}) - 1), maxTY = std::min(world.map.h - 1, std::max({y0, y1, y2, y3}) + 1);
    for (int ty = minTY; ty <= maxTY; ty++)
        for (int tx = minTX; tx <= maxTX; tx++) {
            FogState fs = world.map.fogAt(localPlayer, tx, ty);
            if (fs == FOG_VISIBLE) continue;
            int px, py;
            tileToScreen(tx, ty, px, py);
            int sx = px - (int)camX, sy = py - (int)camY;
            if (fs == FOG_UNSEEN) DrawTexture(fogBlack, sx - TILE_W / 2, sy, WHITE);
            else DrawTexture(fogDim, sx - TILE_W / 2, sy, WHITE);
        }
}

void Game::drawPlacement() {
    // 超武目标选择：鼠标处画范围预览圈
    if (targetingSW != SWType::COUNT) {
        Vector2 m = mousePos();
        float wx, wy;
        screenToWorld((int)m.x, (int)m.y, wx, wy);
        int tx, ty;
        screenToTile(wx, wy, tx, ty);
        int px, py;
        tileToScreen(tx, ty, px, py);
        int sx = px - (int)camX, sy = py - (int)camY + TILE_H / 2;
        float radius = targetingSW == SWType::Nuke ? 6.0f : (targetingSW == SWType::Lightning ? 5.5f : 3.0f);
        // 等距椭圆覆盖圈
        float ex = radius * TILE_W / 2.0f, ey = radius * TILE_H / 2.0f;
        Color cc = targetingSW == SWType::IronCurtain ? Color{220, 60, 50, 200} : Color{255, 220, 80, 220};
        if ((world.tick / 8) % 2) cc.a = 130;
        DrawEllipseLines(sx, sy, ex, ey, cc);
        DrawEllipse(sx, sy, ex, ey, targetingSW == SWType::IronCurtain ? Color{220, 60, 50, 36} : Color{255, 220, 80, 30});
        // 中心准星
        DrawLine(sx - 10, sy, sx + 10, sy, cc);
        DrawLine(sx, sy - 6, sx, sy + 6, cc);
        return;
    }
    if (!placing) return;
    BldType t = world.players[localPlayer].placingBld;
    if (t == BldType::COUNT) return;
    const BldDef& d = bldDef(t);
    Vector2 m = mousePos();
    float wx, wy;
    screenToWorld((int)m.x, (int)m.y, wx, wy);
    int tx, ty;
    screenToTile(wx, wy, tx, ty);
    int bx = tx - d.w / 2, by = ty - d.h / 2;
    // 逐格绘制可行性
    for (int dy = 0; dy < d.h; dy++)
        for (int dx = 0; dx < d.w; dx++) {
            int x = bx + dx, y = by + dy;
            int px, py;
            tileToScreen(x, y, px, py);
            bool ok = world.map.passable(x, y) && !world.bldBlocked(x, y);
            Color c = ok ? Color{0, 255, 0, 90} : Color{255, 0, 0, 110};
            DrawTexture(fogBlack, px - TILE_W / 2 - (int)camX, py - (int)camY, c);
        }
    bool canAll = world.canPlace(t, bx, by, localPlayer);
    const Sprite& s = g_sprites.building(t, world.players[localPlayer].colorId, !canAll);
    int px, py;
    tileToScreen(bx + d.w - 1, by + d.h - 1, px, py);
    DrawTexture(s.tex, px - (int)camX - s.ox, py + TILE_H - (int)camY - s.oy, Color{255, 255, 255, 170});
}

void Game::drawHealthBar(int px, int py, int w, float frac, bool selected) {
    (void)selected;
    DrawRectangle(px - 1, py - 1, w + 2, 5, Color{0, 0, 0, 180});
    Color c = frac > 0.5f ? Color{60, 220, 60, 255} : (frac > 0.25f ? Color{230, 210, 40, 255} : Color{220, 40, 40, 255});
    DrawRectangle(px, py, (int)(w * frac), 3, c);
}

// ===================== 输入包装（高 DPI 修正 + 脚本注入） =====================
Vector2 Game::mousePos() const {
    if (sim.active) return sim.pos;
    // 物理像素 → 逻辑画布坐标：与 render() 的 letterbox 目标矩形一一对应。
    // 高 DPI 显示器（如 150% 缩放）下 raylib 返回物理像素坐标，必须归一化，
    // 否则所有按钮命中测试整体偏移，表现为"点任何按钮都没反应"。
    float rw = (float)GetRenderWidth(), rh = (float)GetRenderHeight();
    Vector2 m = GetMousePosition();
    if (rw <= 0 || rh <= 0) return m;
    Rectangle dst{(rw - rh * SCREEN_W / SCREEN_H) / 2, 0, rh * SCREEN_W / SCREEN_H, rh};
    if (rw / SCREEN_W < rh / SCREEN_H) dst = Rectangle{0, (rh - rw * SCREEN_H / SCREEN_W) / 2, rw, rw * SCREEN_H / SCREEN_W};
    return {(m.x - dst.x) * SCREEN_W / dst.width, (m.y - dst.y) * SCREEN_H / dst.height};
}

bool Game::mPressed(int b) const {
    if (!sim.active) return IsMouseButtonPressed(b);
    return b == MOUSE_RIGHT_BUTTON ? sim.pressedR : sim.pressedL;
}
bool Game::mDown(int b) const {
    if (!sim.active) return IsMouseButtonDown(b);
    return b == MOUSE_RIGHT_BUTTON ? sim.downR : sim.downL;
}
bool Game::mReleased(int b) const {
    if (!sim.active) return IsMouseButtonReleased(b);
    return b == MOUSE_RIGHT_BUTTON ? sim.releasedR : sim.releasedL;
}
bool Game::kPressed(int k) const {
    return sim.active ? sim.keysPressed.count(k) > 0 : IsKeyPressed(k);
}
bool Game::kDown(int k) const {
    return sim.active ? sim.keysDown.count(k) > 0 : IsKeyDown(k);
}

// ===================== 自动化完整游玩测试 =====================
// 脚本注入输入，真实窗口完整操作一遍：
// 主菜单→遭遇战设置→开局→点选MCV→D展开→侧边栏建电厂→放置→
// 框选坦克→右键移动→ESC菜单→返回主菜单→战役选择→战役开局→返回。
// 每步断言 PASS/FAIL 并截图 pt_XX_*.png，返回失败数（0 = 全部通过）。
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

    // ---- 2 遭遇战设置 ----
    clickL(285, 389); // “遭遇战”按钮 {120,360,330,58}
    check(phase == Phase::Setup, "点击[遭遇战]进设置");
    frame(2); // 让地图预览生成
    shot("pt_02_setup.png");

    // ---- 3 开始游戏 ----
    clickL(550, 731); // “开始游戏” {390,700,320,62}
    check(phase == Phase::InGame && campaignMission < 0, "点击[开始游戏]进遭遇战");
    frame(5);

    // ---- 4 点选基地车 ----
    EID mcv = findUnit(UnitType::MCV);
    check(mcv != INVALID_EID, "找到出生基地车");
    if (mcv != INVALID_EID) {
        Vector2 mp = unitScreenPos(world.ents[mcv]);
        clickL(mp.x, mp.y);
        check(sel.size() == 1 && sel[0] == mcv, "左键点选基地车");
    }

    // ---- 5 D 展开建造厂 ----
    key(KEY_D);
    frame(3);
    check(world.hasBld(0, BldType::ConYard), "按D展开建造厂");
    shot("pt_03_deployed.png");

    // ---- 6 侧边栏生产电厂（建筑页签第1个图标 {1256,358,86,66}）----
    clickL(1299, 391);
    check(world.players[0].bldProd.active, "点击电厂图标开始生产");
    for (int i = 0; i < 6000 && !world.players[0].bldProd.ready; i++) logic(); // 快进至就绪
    check(world.players[0].bldProd.ready, "电厂生产就绪");

    // ---- 7 放置建筑 ----
    clickL(1299, 391); // 就绪后再点图标进入放置模式
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
        } else {
            check(false, "右键移动单位（无可走目标）");
        }
        shot("pt_05_move.png");
    }

    // ---- 9.5 侧边栏出售模式（RA2 按钮）----
    {
        int bcnt = world.countBlds(0, pt);
        int money0 = world.players[0].money;
        clickL(1345, 782); // “出售”按钮 {1317,762,56,40}
        check(sideMode == 2, "点击[出售]进入出售模式");
        EID pbld = INVALID_EID;
        for (size_t i = 0; i < world.ents.size(); i++)
            if (world.ents[i].alive && world.ents[i].isBuilding && world.ents[i].player == 0
                && world.ents[i].btype == pt) { pbld = (int)i; break; }
        if (pbld != INVALID_EID) {
            Vector2 bp = bldScreenPos(world.ents[pbld]);
            const Sprite& ps = g_sprites.building(pt, world.players[0].colorId, false);
            clickL(bp.x - ps.ox + ps.tex.width / 2.0f, bp.y - ps.oy + ps.tex.height / 2.0f); // 电厂贴图中心
            check(world.countBlds(0, pt) == bcnt - 1 && world.players[0].money > money0 && sideMode == 0,
                  "出售模式点击建筑卖出");
        } else check(false, "出售模式点击建筑卖出");
        sel.clear(); // 出售后可能残留选中
    }

    // ---- 10 ESC 菜单 → 保存进度 → F9 读档 → 返回主菜单 ----
    key(KEY_ESCAPE); // 第一次：清除选择
    key(KEY_ESCAPE); // 第二次：打开菜单
    check(showMenu, "ESC打开游戏菜单");
    shot("pt_06_escmenu.png");
    clickL(720, 343); // “保存进度 (F5)” {620,327,200,32}
    check(FileExists(QUICKSAVE_PATH) && !showMenu, "菜单点击[保存进度]");
    uint64_t tick0 = world.tick;
    int money0 = world.players[0].money;
    key(KEY_F9); // 快速读档：状态应还原到保存时刻（误差=点击后推进的几帧）
    check(world.tick <= tick0 && world.tick + 5 >= tick0 && world.players[0].money == money0
          && world.hasBld(0, BldType::ConYard), "F9读档状态还原");
    uint64_t tickL = world.tick;
    frame(30);
    check(world.tick == tickL + 30, "读档后模拟继续推进");
    key(KEY_ESCAPE);
    check(showMenu, "再次ESC打开菜单");
    clickL(720, 469); // “返回主菜单” {620,453,200,32}
    check(phase == Phase::MainMenu && !showMenu, "点击[返回主菜单]");

    // ---- 11 战役模式 ----
    clickL(285, 465); // “战役模式” {120,436,330,58}
    check(phase == Phase::MissionSelect, "点击[战役模式]");
    shot("pt_07_missions.png");
    clickL(330, 300); // 第一张任务卡 {150,200,360,200}
    check(phase == Phase::InGame && campaignMission == 0, "点击任务1进入战役");
    frame(10);
    shot("pt_08_campaign.png");

    // ---- 12 战役内 ESC → 返回主菜单 ----
    key(KEY_ESCAPE);
    check(showMenu, "战役ESC打开菜单");
    clickL(720, 469);
    check(phase == Phase::MainMenu, "战役返回主菜单");

    frame(2);
    TraceLog(LOG_INFO, "PLAYTEST DONE: %d checks, %d failed", stepNo, fails);
    sim.active = false;
    return fails;
}
