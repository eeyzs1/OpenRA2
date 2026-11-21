// P8 LAN 联机：确定性 lockstep 集成（握手/命令帧同步/大厅 UI/双进程自测）
#include "game_regression.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "sfx/sound.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

int GameRegression::netSelfTestDriver(int role,int frames,RulesChoice choice,int country) {
    selectRulesChoice(choice);
    netResetHandshake();
    lobbyRole = role;
    netGame = false;
    netPeerLeft = netDesync = false;
    phase = Phase::NetLobby;
    const auto wait=[&](const auto& ready){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!ready() && lobbyState!=3 && !net.failed() && std::chrono::steady_clock::now()<deadline){
            netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(ready())return true;
        // Give the connected peer time to read a queued typed rejection.
        for(int i=0;i<200 && lobbyState==3 && net.connected();++i){net.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
        TraceLog(LOG_ERROR,"net-test: rejected reason=%u tick=%llu",(unsigned)lobbyError,(unsigned long long)netSimTick);
        net.close();return false;};
    if (lobbyRole == 0) {
        if (!net.host(NET_PORT)) { TraceLog(LOG_ERROR, "net-test: host failed"); return 1; }
        lobbyState = 1;
        if(!wait([&]{return lobbyState==2;}))return lobbyError==NetLink::RejectReason::None?1:2;
        cfgMapSize = 64; cfgMapType = 0; cfgMoney = 10000; cfgCrates = true;
        cfgCountry = country?country:cfgRules==RulesChoice::Fusion?(int)Country::China:(int)Country::America; cfgColor = 0;
        if(!netStartHandshake(20260724ull))return 2;
    } else {
        cfgCountry = country?country:(int)Country::Russia; cfgColor = 1;
        if (!net.connectTo("127.0.0.1", NET_PORT)) { TraceLog(LOG_ERROR, "net-test: connect failed"); return 1; }
        lobbyState = 1;
    }
    if(!wait([&]{return netGame;}))return lobbyError==NetLink::RejectReason::None?1:2;
    TraceLog(LOG_INFO,"net-test: game begin player=%d rules=%u source=%s countries=%u,%u",netPlayer,(unsigned)cfgRules,world.rulesProfileId.c_str(),
        (unsigned)world.players[0].country,(unsigned)world.players[1].country);
    // 脚本化命令：双方各自生产/移动，验证命令同步与确定性。
    // 循环条件为"推进到 frames tick"（非固定迭代数：空转迭代不确定，两端会以网络节奏互锁推进）
    bool peerDone = false;
    for (uint64_t f = 0; netSimTick < (uint64_t)frames && !netDesync; f++) {
        if (f > (uint64_t)frames * 8 + 600) { TraceLog(LOG_ERROR, "net-test: stall at tick %llu", (unsigned long long)netSimTick); return 1; }
        netHandleMsgs();
        if (netPeerLeft) { peerDone = true; netPeerLeft = false; } // 对方已完成（Bye）：不中断，己方仍可推进到 frames
        // 在 tick 30 部署基地车，tick 120 全体移动
        if (netSimTick + NET_DELAY == 30 || (netSimTick < NET_DELAY && f == 0)) {
            for (size_t id=0;id<world.ents.size();++id) {
                const auto& e=world.ents[id];
                if (e.alive && !e.isBuilding && e.player == localPlayer && e.utype == UnitType::MCV) {
                    World::Cmd c;
                    c.type = World::Cmd::Deploy;
                    c.ids.push_back((EID)id);
                    issueCmd(c);
                }
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
