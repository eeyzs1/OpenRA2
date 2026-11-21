// P8 LAN 联机：确定性 lockstep 集成（握手/命令帧同步/大厅 UI/双进程自测）
#include "game/game.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

// ===================== 命令入队 =====================
void Game::issueCmd(const World::Cmd& input) {
    captureInteractionReferences();
    auto c=input;world.bindCommand(c);
    if(c.generations.size()!=c.ids.size())return;
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
    consumePresentationEvents();
}

// ===================== 握手与开局 =====================
// 主机在 Welcome 中权威下发：种子/地图/资金/双方国家颜色 → 双方同配置 newGame，保证确定性起点
void Game::netBeginGame() {
    if(!refreshRulesChoice(cfgRules)){netRejectLobby(NetLink::RejectReason::InvalidRules);return;}
    const auto options=selectableCountries();
    if(options.size()<2){netRejectLobby(NetLink::RejectReason::InvalidRules);return;}
    Rng frng(netSeed);
    auto pickCountry = [&](int c) {
        return c==(int)Country::COUNT?(Country)options[frng.range(0,(int)options.size()-2)]:(Country)c;
    };
    // 固定顺序：先主机后客户端（双方计算一致）
    int hostC = (int)pickCountry(netPlayer == 0 ? cfgCountry : peerCountry);
    int clientC = (int)pickCountry(netPlayer == 0 ? peerCountry : cfgCountry);
    std::vector<Faction> factions{countryFaction((Country)hostC), countryFaction((Country)clientC)};
    if(std::find(options.begin(),options.end(),hostC)==options.end() || std::find(options.begin(),options.end(),clientC)==options.end()){
        netRejectLobby(NetLink::RejectReason::Protocol);return;
    }
    // A lobby can be entered after a custom mission or a different skirmish
    // mode. Both peers start from the same default rules, not their old World.
    World match;
    const World::RulesSnapshot snapshot{rulesInfo[(int)cfgRules].source,true,true};
    if(!match.init(cfgMapSize,cfgMapSize,netSeed,2,0,factions,cfgMapType,nullptr,false,false,nullptr,
        {(Country)hostC,(Country)clientC},cfgRules==RulesChoice::Fusion?nullptr:&snapshot)){
        netRejectLobby(NetLink::RejectReason::InvalidRules);return;
    }
    g_script.shutdown();resetMatchInteraction();campaignMission=-1;nextWave=0;missionTriggers.clear();objectiveText.clear();matchSetup.reset();
    world=std::move(match);world.map.bldOccRef=&world.bldOcc;
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
    resetNotifications();
    netGame = true;
    netDesync = netPeerLeft = false;
    netSimTick = 0;
    localCmds.clear();
    remoteCmds.clear();
    pendingCmds.clear();
    localSums.clear();
    remoteSums.clear();
    sel.clear();
    selBuilding = INVALID_EID;
    clearPlacement();
    targetingSW = SWType::COUNT;
    chronoSourceSel.clear();
    targetingParadrop = false;
    targetingSupport = SupportType::COUNT;
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
    bakeTerrain();
    g_sprites.preloadMatch(localPlayer);
    clampCameraToMap();
}

void Game::netLeave() {
    if (net.connected() || net.listening()) net.send(NetLink::MsgBye);
    net.close();
    netGame = false;
    netDesync = netPeerLeft = false;
    lobbyState = 0;
    peerCountry = peerColor = -1;
    netResetHandshake();
    localCmds.clear();remoteCmds.clear();pendingCmds.clear();
    localSums.clear();remoteSums.clear();
    localPlayer = 0;
    phase = Phase::MainMenu;
}

// ===================== 消息处理 =====================
void Game::netHandleMsgs() {
    net.poll();
    if (net.failed()) {
        if (netGame) netPeerLeft = true;
        else if (lobbyState == 1 || lobbyState == 2 || lobbyState == 5) lobbyState = 3;
        return;
    }
    // The real lobby and the two-process test share this connection path.
    if(phase==Phase::NetLobby && lobbyRole==1 && lobbyState==1 && net.connected() && !lobbyHelloSent && !netSendHello())return;
    auto reject = [&](const char* reason) {
        TraceLog(LOG_WARNING, "NET protocol error: %s", reason);
        if (netGame) netPeerLeft = true;
        else lobbyState = 3;
        net.close();
    };
    auto validTargetEid = [&](int value) {
        // A peer can be a few input ticks ahead. Existence and incarnation
        // belong to execution-time validation, not this earlier receive tick.
        return value >= 0 && value < 65536;
    };
    while (!net.inbox.empty()) {
        NetLink::Msg m = std::move(net.inbox.front());
        net.inbox.pop_front();
        if(netHandleHandshake(m)){if(lobbyState==3 || netPeerLeft)return;continue;}
        NetLink::Reader r(m.body);
        switch (m.type) {
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
                    for (int j = 0; j < ni && r.ok; j++) {
                        c.ids.push_back(r.r<int32_t>());c.generations.push_back(r.r<uint64_t>());
                    }
                    c.x = r.r<float>();
                    c.y = r.r<float>();
                    c.a = r.r<int32_t>();
                    c.b = r.r<int32_t>();
                    c.targetGeneration=r.r<uint64_t>();c.identitiesBound=true;
                    uint8_t attackMove = r.r<uint8_t>();
                    c.attackMove = attackMove != 0;
                    bool valid = r.ok && attackMove <= 1 && std::isfinite(c.x) && std::isfinite(c.y)
                              && c.x >= -1.0f && c.y >= -1.0f
                              && c.x <= world.map.w + 1.0f && c.y <= world.map.h + 1.0f;
                    for (EID id : c.ids)
                        valid = valid && validTargetEid(id);
                    switch (c.type) {
                        case World::Cmd::Attack: case World::Cmd::Capture: case World::Cmd::Repair:
                        case World::Cmd::Board: case World::Cmd::Garrison: case World::Cmd::Service:
                            valid = valid && validTargetEid(c.a);
                            break;
                        case World::Cmd::StartUnitProd: case World::Cmd::CancelUnitProd:
                        case World::Cmd::HoldUnitProd:
                            valid = valid && world.validProductionType(true,c.a);
                            break;
                        case World::Cmd::StartBldProd: case World::Cmd::PlaceBuilding:
                        case World::Cmd::HoldBldProd: case World::Cmd::CancelBldProd:
                            valid = valid && world.validProductionType(false,c.a);
                            break;
                        case World::Cmd::LaunchSW:
                            valid = valid && c.a >= 0 && c.a < (int)SWType::COUNT;
                            break;
                        case World::Cmd::Paradrop:
                            valid = valid && c.a>=0 && c.a<=SUPPORT_COUNT;
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
                netRecordChecksum(t,sum,true);
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
void Game::netRecordChecksum(uint64_t at,uint32_t sum,bool remote) {
    auto& own=remote?remoteSums:localSums;
    auto& other=remote?localSums:remoteSums;
    if(auto it=own.find(at);it!=own.end() && it->second!=sum) netDesync=true;
    own[at]=sum;
    if(auto it=other.find(at);it!=other.end() && it->second!=sum) {
        netDesync=true;
        TraceLog(LOG_WARNING,"NET desync at tick %llu: local=%08x remote=%08x",
            (unsigned long long)at,remote?it->second:sum,remote?sum:it->second);
    }
    while(own.size()>32)own.erase(own.begin());
}

// 每逻辑帧调用一次：本帧命令分配到 netSimTick+NET_DELAY 发送；远端命令就绪则推进 1 tick
void Game::netAdvance() {
    uint64_t target = netSimTick + NET_DELAY;
    if (!localCmds.count(target)) { // 卡顿期间 target 不变，新命令保留到推进后分配
        auto& frame=localCmds[target];size_t bytes=sizeof(uint32_t)+sizeof(uint16_t),taken=0;
        while(taken<pendingCmds.size() && frame.size()<256) {
            const auto& command=pendingCmds[taken];
            if(command.ids.size()>4096 || command.generations.size()!=command.ids.size()) {netDesync=true;return;}
            constexpr size_t fixed=sizeof(uint8_t)+sizeof(uint16_t)+2*sizeof(float)+2*sizeof(int32_t)+sizeof(uint64_t)+sizeof(uint8_t);
            const size_t encoded=fixed+command.ids.size()*(sizeof(int32_t)+sizeof(uint64_t));
            if(bytes+encoded>NetLink::MAX_BODY_BYTES)break;
            bytes+=encoded;frame.push_back(std::move(pendingCmds[taken++]));
        }
        // Preserve captured identities and FIFO order when a valid burst needs
        // more than one bounded transport frame, including 4096-unit orders.
        pendingCmds.erase(pendingCmds.begin(),pendingCmds.begin()+taken);
        NetLink::Writer w;
        w.w((uint32_t)target);
        w.w((uint16_t)localCmds[target].size());
        for (const World::Cmd& c : localCmds[target]) {
            w.w((uint8_t)c.type);
            w.w((uint16_t)c.ids.size());
            for(size_t index=0;index<c.ids.size();++index) {
                w.w((int32_t)c.ids[index]);w.w(c.generations[index]);
            }
            w.w(c.x);
            w.w(c.y);
            w.w((int32_t)c.a);
            w.w((int32_t)c.b);
            w.w(c.targetGeneration);
            w.w((uint8_t)(c.attackMove ? 1 : 0));
        }
        net.send(NetLink::MsgCmdFrame, w.b);
    }
    bool haveRemote = netSimTick < NET_DELAY || remoteCmds.count(netSimTick) > 0;
    if (!haveRemote) return; // 等待对手（LAN 下罕见；画面静止即网络等待）
    // House order must be identical on both computers, including simultaneous
    // commands that append entities, projectiles or superweapons to the world.
    for(int player=0;player<2;++player) {
        const auto& commands=player==localPlayer?localCmds[netSimTick]:remoteCmds[netSimTick];
        for(const World::Cmd& c:commands)world.applyCmd(player,c);
    }
    localCmds.erase(netSimTick);
    remoteCmds.erase(netSimTick);
    world.update();
    netSimTick++;
    // 周期校验和：推进到 90 的倍数时记录并发送（3 秒间隔）
    if (netSimTick % 90 == 0) {
        uint32_t sum = world.checksum();
        netRecordChecksum(netSimTick,sum,false);
        NetLink::Writer cw;
        cw.w((uint32_t)netSimTick);
        cw.w(sum);
        net.send(NetLink::MsgChecksum, cw.b);
    }
}

// ===================== 大厅 UI（左内容 + 右侧栏） =====================
void Game::drawNetLobby() {
    if(!cfgOriginalMap.empty())clearOriginalMap();
    cfgReferenceMap=false;
    drawRa2Shell(font, TR(S::LanGame), 2);
    Rectangle content = menuShellContent();
    Rectangle side = menuShellSide();
    int cx = (int)(content.x + content.width / 2);
    Vector2 m = menuUiFromCanvas(mousePos());
    bool pressed = mPressed(MOUSE_LEFT_BUTTON);
    const bool available=refreshRulesChoice(cfgRules);
    const bool editable=lobbyState==0 || lobbyState==4;
    const int left=(int)content.x+24;
    if(menuLabeledValue(font,m,pressed,left,42,TR(S::RulesSet),left+130,180,24,rulesChoiceName(cfgRules),editable))
        selectRulesChoice((RulesChoice)(((int)cfgRules+1)%(int)RulesChoice::COUNT));
    if(menuLabeledValue(font,m,pressed,left,80,TR(S::Country),left+130,180,24,
        cfgCountry==(int)Country::COUNT?TR(S::Random):countryName((Country)cfgCountry),editable)){
        const auto countries=selectableCountries();const auto found=std::find(countries.begin(),countries.end(),cfgCountry);
        cfgCountry=found==countries.end() || found+1==countries.end()?countries.front():*(found+1);
    }
    if(menuLabeledValue(font,m,pressed,left,118,TR(S::Color),left+130,180,24,TextFormat("%d",cfgColor+1),editable))cfgColor=(cfgColor+1)%MAX_PLAYERS;
    DrawRectangle(left+286,122,18,16,HOUSE_COLORS[std::clamp(cfgColor,0,MAX_PLAYERS-1)]);
    if(!available)drawWrapped(font,TR(S::RulesUnavailable),left,151,(int)content.width-48,11,Color{255,160,128,255},3);

    auto sideBtn = [&](float y, const char* text, int size = 14,bool enabled=true) {
        return ra2Button(font, m, pressed, {side.x + 8, y, side.width - 16, 40}, text, size,enabled);
    };

    // 角色选择：左内容展示，动作在右侧栏（钮高 40、行距 52 → 缝 12）
    if (lobbyState == 0) {
        const char* tip = g_lang ? "Choose host or join" : "选择创建主机或加入对局";
        drawTextM(font, tip, cx - textW(font, tip, 16) / 2, 180, 16, Color{220, 216, 206, 255});
        if (sideBtn(210, TR(S::HostGame), 14,available)) {
            netResetHandshake();
            lobbyRole = 0;
            if (net.host(NET_PORT)) lobbyState = 1;
            else lobbyState = 3;
        }
        if (sideBtn(262, TR(S::JoinGame), 14,available)) {
            lobbyRole = 1;
            lobbyEditingIp = true;
            lobbyState = 4;
        }
    } else if (lobbyRole == 0) {
        const char* st = netLobbyStatus();
        drawWrapped(font,st,(int)content.x+24,180,(int)content.width-48,16,Color{220,216,206,255},6,true);
        if (lobbyState == 2) {
            std::string info = peerCountry==(int)Country::COUNT?TR(S::Random):countryName((Country)peerCountry);
            drawTextM(font, info.c_str(), cx - textW(font, info.c_str(), 14) / 2, 210, 14, Color{255, 210, 100, 255});
            if (sideBtn(210, TR(S::StartBattle), 14)) {
                netStartHandshake((uint64_t)GetTime()*2654435761u+881);
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
            if (sideBtn(210, TR(S::JoinGame), 14,available)) {
                netResetHandshake();
                if (net.connectTo(lobbyIp.c_str(), NET_PORT)) lobbyState = 1;
                else lobbyState = 3;
            }
        } else {
            const char* st = netLobbyStatus();
            drawWrapped(font,st,(int)content.x+24,180,(int)content.width-48,16,Color{220,216,206,255},6,true);
        }
    }
    if (sideBtn(side.y + side.height - 56, TR(S::Back), 14)) netLeave();
}

// ===================== 双进程自测 =====================
// host 进程：开局后自动跑 frames tick；client 进程：连接 127.0.0.1 加入。
// 两端各自在每个 90 tick 打印校验和，由外部比对日志判定一致性。
