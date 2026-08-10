// P8 LAN 联机：确定性 lockstep 集成（握手/命令帧同步/大厅 UI/双进程自测）
#include "game/game.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

// ===================== 命令入队 =====================
void Game::issueCmd(const World::Cmd& c) {
    if (netGame) {
        if (pendingCmds.size() >= 256 || c.ids.size() > 4096) {
            TraceLog(LOG_WARNING, "NET local command exceeded protocol limits");
            netDesync = true;
            return;
        }
        pendingCmds.push_back(c);
        return;
    }
    world.applyCmd(localPlayer, c);
}

// ===================== 握手与开局 =====================
// 主机在 Welcome 中权威下发：种子/地图/资金/双方国家颜色 → 双方同配置 newGame，保证确定性起点
void Game::netBeginGame() {
    campaignMission = -1;
    nextWave = 0;
    missionTriggers.clear();
    objectiveText.clear();
    Rng frng(netSeed);
    auto pickCountry = [&](int c) {
        return c >= (int)Country::COUNT ? (Country)frng.range(1, (int)Country::COUNT - 1) : (Country)c;
    };
    // 固定顺序：先主机后客户端（双方计算一致）
    int hostC = (int)pickCountry(netPlayer == 0 ? cfgCountry : peerCountry);
    int clientC = (int)pickCountry(netPlayer == 0 ? peerCountry : cfgCountry);
    std::vector<Faction> factions{countryFaction((Country)hostC), countryFaction((Country)clientC)};
    world.init(cfgMapSize, cfgMapSize, netSeed, 2, 0, factions, cfgMapType);
    world.players[0].country = (Country)hostC;
    world.players[1].country = (Country)clientC;
    int hostColor = netPlayer == 0 ? cfgColor : peerColor;
    int clientColor = netPlayer == 0 ? peerColor : cfgColor;
    world.players[0].colorId = hostColor;
    world.players[1].colorId = clientColor != hostColor ? clientColor : (hostColor + 1) % MAX_PLAYERS;
    for (int i = 0; i < 2; i++) world.players[i].money = cfgMoney;
    world.cratesEnabled = cfgCrates;
    world.aiAlliance = false;
    ais.clear();
    localPlayer = netPlayer;
    netGame = true;
    netDesync = netPeerLeft = false;
    netSimTick = 0;
    localCmds.clear();
    remoteCmds.clear();
    pendingCmds.clear();
    sel.clear();
    selBuilding = INVALID_EID;
    placing = false;
    targetingSW = SWType::COUNT;
    chronoSourceSel.clear();
    targetingParadrop = false;
    gameOver = victory = false;
    gameSpeed = 1; // 联机锁定 1x（lockstep 步进对齐）
    // 摄像机对准本端出生点
    for (auto& e : world.ents)
        if (e.alive && !e.isBuilding && e.player == localPlayer) {
            int sx, sy;
            tileToScreen((int)e.x, (int)e.y, sx, sy);
            camX = (float)sx - (SCREEN_W - sidebarW) / 2.0f;
            camY = (float)sy - SCREEN_H / 2.0f;
            break;
        }
    message(TextFormat(TR(S::YourSide), countryName(world.players[localPlayer].country)));
    phase = Phase::InGame;
}

void Game::netLeave() {
    if (net.connected() || net.listening()) net.send(NetLink::MsgBye);
    net.close();
    netGame = false;
    netDesync = netPeerLeft = false;
    lobbyState = 0;
    peerCountry = peerColor = -1;
    localPlayer = 0;
    phase = Phase::MainMenu;
}

// ===================== 消息处理 =====================
void Game::netHandleMsgs() {
    net.poll();
    if (net.failed()) {
        if (netGame) netPeerLeft = true;
        else if (lobbyState == 1 || lobbyState == 2) lobbyState = 3;
        return;
    }
    auto reject = [&](const char* reason) {
        TraceLog(LOG_WARNING, "NET protocol error: %s", reason);
        if (netGame) netPeerLeft = true;
        else lobbyState = 3;
        net.close();
    };
    auto validCountry = [](int value) {
        return value > (int)Country::None && value < (int)Country::COUNT;
    };
    auto validMapSize = [](int value) {
        static const int k[] = {48, 64, 96, 128, 160, 200, 256};
        for (int s : k) if (s == value) return true;
        return false;
    };
    auto validTargetEid = [&](int value) {
        return value >= 0 && value < (int)world.ents.size();
    };
    while (!net.inbox.empty()) {
        NetLink::Msg m = std::move(net.inbox.front());
        net.inbox.pop_front();
        NetLink::Reader r(m.body);
        switch (m.type) {
            case NetLink::MsgHello: { // C→S：version u32, country u8, color u8
                if (netGame || lobbyRole != 0 || lobbyState != 1) { reject("unexpected hello"); return; }
                uint32_t ver = r.r<uint32_t>();
                int country = r.r<uint8_t>();
                int color = r.r<uint8_t>();
                if (!r.done() || ver != 2 || !validCountry(country) || color < 0 || color >= MAX_PLAYERS) {
                    reject("invalid hello");
                    return;
                }
                peerCountry = country;
                peerColor = color;
                lobbyState = 2; // 已连接待开局
                break;
            }
            case NetLink::MsgWelcome: { // S→C：seed u64, mapSize u8, mapType u8, money i32, crates u8, hostCountry u8, hostColor u8
                if (netGame || lobbyRole != 1 || lobbyState != 1) { reject("unexpected welcome"); return; }
                uint64_t seed = r.r<uint64_t>();
                int mapSize = r.r<uint8_t>();
                int mapType = r.r<uint8_t>();
                int money = r.r<int32_t>();
                int crates = r.r<uint8_t>();
                int country = r.r<uint8_t>();
                int color = r.r<uint8_t>();
                if (!r.done() || seed == 0 || !validMapSize(mapSize) || mapType < 0 || mapType > 6
                    || money < 0 || money > 1000000 || crates > 1
                    || !validCountry(country) || color < 0 || color >= MAX_PLAYERS) {
                    reject("invalid welcome");
                    return;
                }
                netSeed = seed;
                cfgMapSize = mapSize;
                cfgMapType = mapType;
                cfgMoney = money;
                cfgCrates = crates != 0;
                peerCountry = country;
                peerColor = color;
                lobbyState = 2;
                break;
            }
            case NetLink::MsgStart: {
                if (!r.done() || netGame || lobbyRole != 1 || lobbyState != 2
                    || netSeed == 0 || !validCountry(peerCountry)) {
                    reject("unexpected start");
                    return;
                }
                netPlayer = 1;
                netBeginGame();
                break;
            }
            case NetLink::MsgCmdFrame: {
                if (!netGame) { reject("command outside game"); return; }
                uint32_t t = r.r<uint32_t>();
                uint16_t n = r.r<uint16_t>();
                if (!r.ok || n > 256 || t < netSimTick || t > netSimTick + NET_DELAY + 16
                    || remoteCmds.count(t)) {
                    reject("invalid command frame header");
                    return;
                }
                std::vector<World::Cmd> cmds;
                cmds.reserve(n);
                for (int i = 0; i < n && r.ok; i++) {
                    World::Cmd c;
                    int type = r.r<uint8_t>();
                    c.type = (World::Cmd::Type)type;
                    uint16_t ni = r.r<uint16_t>();
                    if (type <= (int)World::Cmd::None || type > (int)World::Cmd::LaunchSW || ni > 4096) {
                        r.ok = false;
                        break;
                    }
                    for (int j = 0; j < ni && r.ok; j++) c.ids.push_back(r.r<int32_t>());
                    c.x = r.r<float>();
                    c.y = r.r<float>();
                    c.a = r.r<int32_t>();
                    c.b = r.r<int32_t>();
                    uint8_t attackMove = r.r<uint8_t>();
                    c.attackMove = attackMove != 0;
                    bool valid = r.ok && attackMove <= 1 && std::isfinite(c.x) && std::isfinite(c.y)
                              && c.x >= -1.0f && c.y >= -1.0f
                              && c.x <= world.map.w + 1.0f && c.y <= world.map.h + 1.0f;
                    for (EID id : c.ids)
                        valid = valid && id >= 0 && id < (int)world.ents.size();
                    switch (c.type) {
                        case World::Cmd::Attack: case World::Cmd::Capture: case World::Cmd::Repair:
                        case World::Cmd::Board: case World::Cmd::Garrison: case World::Cmd::Service:
                            valid = valid && validTargetEid(c.a);
                            break;
                        case World::Cmd::StartUnitProd: case World::Cmd::CancelUnitProd:
                        case World::Cmd::HoldUnitProd:
                            valid = valid && c.a >= 0 && c.a < (int)UnitType::COUNT;
                            break;
                        case World::Cmd::StartBldProd: case World::Cmd::PlaceBuilding:
                        case World::Cmd::HoldBldProd: case World::Cmd::CancelBldProd:
                            valid = valid && c.a >= 0 && c.a < (int)BldType::COUNT;
                            break;
                        case World::Cmd::LaunchSW:
                            valid = valid && c.a >= 0 && c.a < (int)SWType::COUNT;
                            break;
                        case World::Cmd::Harvest:
                            valid = valid && world.map.inBounds(c.a, c.b);
                            break;
                        default: break;
                    }
                    if (!valid) { r.ok = false; break; }
                    cmds.push_back(std::move(c));
                }
                if (!r.done()) { reject("malformed command frame"); return; }
                remoteCmds[t] = std::move(cmds);
                break;
            }
            case NetLink::MsgChecksum: {
                if (!netGame) { reject("checksum outside game"); return; }
                uint32_t t = r.r<uint32_t>();
                uint32_t sum = r.r<uint32_t>();
                if (!r.done() || t == 0 || t % 90 != 0 || t > netSimTick + 90) {
                    reject("invalid checksum");
                    return;
                }
                // 本端已过该 tick 才能比对（双方发送时机相同，正常都已过）
                if (t <= netSimTick) {
                    // 重算该 tick 不可行——在推进到 t 时记录历史校验和比对
                    auto it = localSums.find(t);
                    if (it != localSums.end() && it->second != sum) {
                        netDesync = true;
                        TraceLog(LOG_WARNING, "NET desync at tick %u: local=%08x remote=%08x", t, it->second, sum);
                    }
                }
                break;
            }
            case NetLink::MsgBye:
                if (!r.done()) { reject("invalid bye"); return; }
                if (netGame) netPeerLeft = true;
                else lobbyState = 3;
                net.close();
                return;
            default:
                reject("unknown message");
                return;
        }
    }
}

// ===================== lockstep 推进 =====================
// 每逻辑帧调用一次：本帧命令分配到 netSimTick+NET_DELAY 发送；远端命令就绪则推进 1 tick
void Game::netAdvance() {
    uint64_t target = netSimTick + NET_DELAY;
    if (!localCmds.count(target)) { // 卡顿期间 target 不变，新命令保留到推进后分配
        localCmds[target] = pendingCmds;
        pendingCmds.clear();
        NetLink::Writer w;
        w.w((uint32_t)target);
        w.w((uint16_t)localCmds[target].size());
        for (const World::Cmd& c : localCmds[target]) {
            w.w((uint8_t)c.type);
            w.w((uint16_t)c.ids.size());
            for (EID id : c.ids) w.w((int32_t)id);
            w.w(c.x);
            w.w(c.y);
            w.w((int32_t)c.a);
            w.w((int32_t)c.b);
            w.w((uint8_t)(c.attackMove ? 1 : 0));
        }
        net.send(NetLink::MsgCmdFrame, w.b);
    }
    bool haveRemote = netSimTick < NET_DELAY || remoteCmds.count(netSimTick) > 0;
    if (!haveRemote) return; // 等待对手（LAN 下罕见；画面静止即网络等待）
    for (const World::Cmd& c : localCmds[netSimTick]) world.applyCmd(localPlayer, c);
    int peer = 1 - localPlayer;
    for (const World::Cmd& c : remoteCmds[netSimTick]) world.applyCmd(peer, c);
    localCmds.erase(netSimTick);
    remoteCmds.erase(netSimTick);
    world.update();
    netSimTick++;
    // 周期校验和：推进到 90 的倍数时记录并发送（3 秒间隔）
    if (netSimTick % 90 == 0) {
        uint32_t sum = world.checksum();
        localSums[netSimTick] = sum;
        while (localSums.size() > 32) localSums.erase(localSums.begin());
        NetLink::Writer cw;
        cw.w((uint32_t)netSimTick);
        cw.w(sum);
        net.send(NetLink::MsgChecksum, cw.b);
    }
}

// ===================== 大厅 UI（左内容 + 右侧栏） =====================
void Game::drawNetLobby() {
    drawRa2Shell(font, TR(S::LanGame), 2);
    Rectangle content = menuShellContent();
    Rectangle side = menuShellSide();
    int cx = (int)(content.x + content.width / 2);
    Vector2 m = menuUiFromCanvas(mousePos());
    bool pressed = mPressed(MOUSE_LEFT_BUTTON);

    auto sideBtn = [&](float y, const char* text, int size = 14) {
        return ra2Button(font, m, pressed, {side.x + 8, y, side.width - 16, 40}, text, size);
    };

    // 角色选择：左内容展示，动作在右侧栏（钮高 40、行距 52 → 缝 12）
    if (lobbyState == 0) {
        const char* tip = g_lang ? "Choose host or join" : "选择创建主机或加入对局";
        drawTextM(font, tip, cx - textW(font, tip, 16) / 2, 180, 16, Color{220, 216, 206, 255});
        if (sideBtn(210, TR(S::HostGame), 14)) {
            lobbyRole = 0;
            if (net.host(NET_PORT)) lobbyState = 1;
            else lobbyState = 3;
        }
        if (sideBtn(262, TR(S::JoinGame), 14)) {
            lobbyRole = 1;
            lobbyEditingIp = true;
            lobbyState = 4;
        }
    } else if (lobbyRole == 0) {
        const char* st = lobbyState == 1 ? TR(S::WaitPeer) : lobbyState == 2 ? TR(S::PeerJoined) : TR(S::ConnectFail);
        drawTextM(font, st, cx - textW(font, st, 16) / 2, 180, 16, Color{220, 216, 206, 255});
        if (lobbyState == 2) {
            std::string info = std::string(countryName((Country)peerCountry));
            drawTextM(font, info.c_str(), cx - textW(font, info.c_str(), 14) / 2, 210, 14, Color{255, 210, 100, 255});
            if (sideBtn(210, TR(S::StartBattle), 14)) {
                netSeed = (uint64_t)GetTime() * 2654435761u + 881;
                NetLink::Writer w;
                w.w((uint64_t)netSeed);
                w.w((uint8_t)cfgMapSize);
                w.w((uint8_t)cfgMapType);
                w.w((int32_t)cfgMoney);
                w.w((uint8_t)(cfgCrates ? 1 : 0));
                w.w((uint8_t)cfgCountry);
                w.w((uint8_t)cfgColor);
                net.send(NetLink::MsgWelcome, w.b);
                net.send(NetLink::MsgStart);
                netPlayer = 0;
                netBeginGame();
                return;
            }
        }
    } else {
        if (lobbyState == 4) {
            drawTextM(font, TR(S::IpLabel), cx - 160, 160, 14, Color{200, 196, 188, 255});
            Rectangle box{(float)cx - 160, 186, 320, 32};
            drawMenuOptSlot(box, lobbyEditingIp, false);
            drawTextM(font, lobbyIp.c_str(), (int)box.x + 8, (int)box.y + 8, 14, Color{255, 220, 120, 255});
            for (int k = KEY_ZERO; k <= KEY_NINE; k++)
                if (kPressed(k) && lobbyIp.size() < 15) lobbyIp.push_back((char)('0' + k - KEY_ZERO));
            if (kPressed(KEY_PERIOD) && lobbyIp.size() < 15) lobbyIp.push_back('.');
            if (kPressed(KEY_BACKSPACE) && !lobbyIp.empty()) lobbyIp.pop_back();
            if (sideBtn(210, TR(S::JoinGame), 14)) {
                if (net.connectTo(lobbyIp.c_str(), NET_PORT)) lobbyState = 1;
                else lobbyState = 3;
            }
        } else {
            const char* st = lobbyState == 1 ? TR(S::WaitPeer) : lobbyState == 2 ? TR(S::WaitHostStart) : TR(S::ConnectFail);
            drawTextM(font, st, cx - textW(font, st, 16) / 2, 180, 16, Color{220, 216, 206, 255});
        }
    }
    if (sideBtn(side.y + side.height - 56, TR(S::Back), 14)) netLeave();
}

// ===================== 双进程自测 =====================
// host 进程：开局后自动跑 frames tick；client 进程：连接 127.0.0.1 加入。
// 两端各自在每个 90 tick 打印校验和，由外部比对日志判定一致性。
int Game::netSelfTestDriver(int role, int frames) {
    lobbyRole = role;
    netGame = false;
    netPeerLeft = netDesync = false;
    phase = Phase::NetLobby;
    if (lobbyRole == 0) {
        if (!net.host(NET_PORT)) { TraceLog(LOG_ERROR, "net-test: host failed"); return 1; }
        lobbyState = 1;
        // 等客户端握手
        while (lobbyState != 2) {
            netHandleMsgs();
            if (net.failed()) { TraceLog(LOG_ERROR, "net-test: accept failed"); return 1; }
        }
        netSeed = 20260724ull;
        cfgMapSize = 64; cfgMapType = 0; cfgMoney = 10000; cfgCrates = true;
        cfgCountry = (int)Country::China; cfgColor = 0;
        NetLink::Writer w;
        w.w((uint64_t)netSeed);
        w.w((uint8_t)cfgMapSize);
        w.w((uint8_t)cfgMapType);
        w.w((int32_t)cfgMoney);
        w.w((uint8_t)1);
        w.w((uint8_t)cfgCountry);
        w.w((uint8_t)cfgColor);
        net.send(NetLink::MsgWelcome, w.b);
        net.send(NetLink::MsgStart);
        netPlayer = 0;
    } else {
        if (!net.connectTo("127.0.0.1", NET_PORT)) { TraceLog(LOG_ERROR, "net-test: connect failed"); return 1; }
        cfgCountry = (int)Country::Russia; cfgColor = 1;
        // 等待连接建立后发送 Hello
        for (int i = 0; i < 600 && !net.connected(); i++) {
            net.poll();
            if (net.failed()) { TraceLog(LOG_ERROR, "net-test: connect refused"); return 1; }
        }
        if (!net.connected()) { TraceLog(LOG_ERROR, "net-test: connect timeout"); return 1; }
        NetLink::Writer w;
        w.w((uint32_t)2);
        w.w((uint8_t)cfgCountry);
        w.w((uint8_t)cfgColor);
        net.send(NetLink::MsgHello, w.b);
        // 等 Start
        lobbyState = 1;
        while (!netGame) {
            netHandleMsgs();
            if (net.failed()) { TraceLog(LOG_ERROR, "net-test: lost before start"); return 1; }
        }
    }
    if (lobbyRole == 0) netBeginGame();
    TraceLog(LOG_INFO, "net-test: game begin player=%d", netPlayer);
    // 脚本化命令：双方各自生产/移动，验证命令同步与确定性。
    // 循环条件为"推进到 frames tick"（非固定迭代数：空转迭代不确定，两端会以网络节奏互锁推进）
    bool peerDone = false;
    for (uint64_t f = 0; netSimTick < (uint64_t)frames && !netDesync; f++) {
        if (f > (uint64_t)frames * 8 + 600) { TraceLog(LOG_ERROR, "net-test: stall at tick %llu", (unsigned long long)netSimTick); return 1; }
        netHandleMsgs();
        if (netPeerLeft) { peerDone = true; netPeerLeft = false; } // 对方已完成（Bye）：不中断，己方仍可推进到 frames
        // 在 tick 30 部署基地车，tick 120 全体移动
        if (netSimTick + NET_DELAY == 30 || (netSimTick < NET_DELAY && f == 0)) {
            for (auto& e : world.ents)
                if (e.alive && !e.isBuilding && e.player == localPlayer && e.utype == UnitType::MCV) {
                    World::Cmd c;
                    c.type = World::Cmd::Deploy;
                    c.ids.push_back((EID)(&e - world.ents.data()));
                    issueCmd(c);
                }
        }
        if (netSimTick + NET_DELAY == 120) {
            World::Cmd c;
            c.type = World::Cmd::Move;
            for (size_t i = 0; i < world.ents.size(); i++)
                if (world.ents[i].alive && !world.ents[i].isBuilding && world.ents[i].player == localPlayer)
                    c.ids.push_back((EID)i);
            c.x = (float)world.map.w / 2.0f;
            c.y = (float)world.map.h / 2.0f;
            issueCmd(c);
        }
        netAdvance();
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // 让出调度：单机双进程靠消息互锁，空转只会烧 CPU
    }
    // 完成通知 + 等对方收尾：确保 close 不会打断对方最后的推进
    net.send(NetLink::MsgBye);
    for (int i = 0; i < 2000 && !peerDone && !net.failed(); i++) {
        netHandleMsgs();
        if (netPeerLeft) { peerDone = true; netPeerLeft = false; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    TraceLog(LOG_INFO, "net-test: done tick=%llu checksum=%08x desync=%d",
             (unsigned long long)netSimTick, world.checksum(), (int)netDesync);
    int rc = (netDesync || netSimTick < (uint64_t)frames) ? 1 : 0;
    net.close();
    return rc;
}
