#include "game_regression.h"
#include <cstdio>
#include <chrono>
#include <thread>
#include <filesystem>

int GameRegression::netProtocolRegression() {
    int failures=0;
    auto check=[&](bool ok,const char* message) {
        std::printf("NET_PROTOCOL %s %s\n",ok?"PASS":"FAIL",message);
        failures+=!ok;
    };
    Game host,client;
    host.world.init(64,64,20261113,2,0,{Faction::Soviet,Faction::Soviet});
    for(size_t id=0;id<host.world.ents.size();++id)
        if(host.world.ents[id].alive)host.world.kill((EID)id,false);
    for(auto& cell:host.world.map.cells)cell=Cell{};
    host.world.cratesEnabled=false;
    host.world.spawnBuilding(0,BldType::NukeSilo,4,4,true);
    host.world.spawnBuilding(1,BldType::NukeSilo,44,44,true);
    for(auto& player:host.world.players){player.defeated=false;player.swReady[(int)SWType::Nuke]=true;}
    client.world=host.world;client.world.map.bldOccRef=&client.world.bldOcc;
    host.localPlayer=0;client.localPlayer=1;
    host.netSimTick=client.netSimTick=host.world.tick=client.world.tick=NET_DELAY;
    World::Cmd first,second;first.type=second.type=World::Cmd::LaunchSW;
    first.a=second.a=(int)SWType::Nuke;first.x=first.y=45;second.x=second.y=5;
    host.localCmds[NET_DELAY]={first};host.remoteCmds[NET_DELAY]={second};
    client.localCmds[NET_DELAY]={second};client.remoteCmds[NET_DELAY]={first};
    host.netAdvance();client.netAdvance();
    std::printf("NET_PROTOCOL_NUKES host_count=%zu client_count=%zu host_first=%d client_first=%d host_hash=%08x client_hash=%08x\n",
        host.world.nukes.size(),client.world.nukes.size(),host.world.nukes.empty()?-1:host.world.nukes[0].player,
        client.world.nukes.empty()?-1:client.world.nukes[0].player,host.world.checksum(),client.world.checksum());
    check(host.world.nukes.size()==2 && client.world.nukes.size()==2
        && host.world.nukes[0].player==0 && client.world.nukes[0].player==0
        && host.world.checksum()==client.world.checksum(),
        "simultaneous superweapons use identical house order on both peers");
    bool equal=true;
    for(int frame=0;frame<300;++frame) {
        host.remoteCmds[host.netSimTick]={};client.remoteCmds[client.netSimTick]={};
        host.netAdvance();client.netAdvance();
        equal &= host.world.checksum()==client.world.checksum();
    }
    check(equal,"both nuclear launches and their damage replay identically for 300 ticks");

    Game sums;sums.selectRulesChoice(RulesChoice::Fusion);
    sums.netRecordChecksum(90,0x12345678,true);
    sums.netRecordChecksum(90,0x12345678,false);
    check(!sums.netDesync,"a checksum received before the local tick is retained and compared");
    sums.netRecordChecksum(180,0x12345678,true);
    sums.netRecordChecksum(180,0x87654321,false);
    check(sums.netDesync,"an early mismatching checksum is detected when the local tick arrives");
    sums.netDesync=false;
    sums.netRecordChecksum(270,1,false);sums.netRecordChecksum(270,2,true);
    check(sums.netDesync,"a late mismatching checksum is detected too");
    for(int sample=4;sample<100;++sample) {
        sums.netRecordChecksum(sample*90,1,false);sums.netRecordChecksum(sample*90,1,true);
    }
    check(sums.localSums.size()==32 && sums.remoteSums.size()==32,"checksum history remains bounded");

    int testPort=NET_PORT+64;
    const auto link=[&](Game& lobby,NetLink& remote){bool listening=false;
        for(int i=0;i<100 && !listening;++i)listening=remote.host(++testPort);
        if(!listening || !lobby.net.connectTo("127.0.0.1",testPort))return false;
        for(int i=0;i<1000 && (!remote.connected() || !lobby.net.connected());++i){remote.poll();lobby.net.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        return remote.connected() && lobby.net.connected();};
    const auto rules=simulationRulesFingerprint();check(rules.has_value(),"installed rules have a valid compatibility digest");if(!rules)return failures;
    for(int size:{48,64,96,128,160,200,256}) {
        Game lobby;lobby.selectRulesChoice(RulesChoice::Fusion);NetLink remote;lobby.lobbyRole=1;lobby.lobbyState=1;lobby.lobbyHelloSent=true;
        const bool linked=link(lobby,remote);
        NetLink::Writer packet;
        packet.w(NetLink::SIMULATION_VERSION);
        packet.w((uint8_t)RulesChoice::Fusion);
        packet.w(uint64_t{20261113});packet.w((uint16_t)size);packet.w(uint8_t{0});
        packet.w(int32_t{10000});packet.w(uint8_t{1});packet.w((uint8_t)Country::COUNT);packet.w(uint8_t{0});
        packet.w(*rules);
        lobby.net.inbox.push_back({NetLink::MsgWelcome,packet.b});lobby.netHandleMsgs();
        for(int i=0;i<50 && remote.inbox.empty();++i){remote.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        bool acknowledged=false;if(!remote.inbox.empty()){const auto& packetMessage=remote.inbox.front();NetLink::Reader r(packetMessage.body);
            const auto version=r.r<uint32_t>();const auto choice=r.r<uint8_t>();const auto seed=r.r<uint64_t>();const auto digest=r.r<Sha256Digest>();
            acknowledged=packetMessage.type==NetLink::MsgReady && r.done() && choice==0 && version==NetLink::SIMULATION_VERSION && seed==20261113 && digest==*rules;}
        check(linked && acknowledged && !lobby.netGame && lobby.lobbyState==2 && lobby.cfgMapSize==size && lobby.peerCountry==(int)Country::COUNT,
            "every selectable map size and a random country survive the real welcome parser and send a bound Ready over TCP before starting");
    }
    sums.netSeed=20261113;sums.netPlayer=0;sums.cfgCountry=(int)Country::COUNT;
    sums.peerCountry=(int)Country::COUNT;sums.peerColor=1;sums.cfgMapSize=64;
    sums.world.shortGame=true;sums.world.superweaponsEnabled=false;sums.world.mcvRepacks=true;
    sums.world.skirmishMode=SkirmishMode::MeatGrinder;sums.world.campaignTechGate=true;
    sums.world.campaignAllowedUnits={UnitType::Engineer};
    sums.netBeginGame();
    check(sums.localSums.empty() && sums.remoteSums.empty() && !sums.netDesync
        && sums.world.players[0].country>Country::None && sums.world.players[0].country<Country::COUNT
        && sums.world.players[1].country>Country::None && sums.world.players[1].country<Country::COUNT,
        "a new network match clears old checksums and resolves both random countries");
    check(!sums.world.shortGame && sums.world.superweaponsEnabled && !sums.world.mcvRepacks
        && sums.world.skirmishMode==SkirmishMode::Battle && !sums.world.campaignTechGate
        && sums.world.campaignAllowedUnits.empty(),"previous skirmish and campaign rules cannot leak into a network match");
    // Check every truncated prefix against a live, pre-existing world. Rejecting
    // a packet must not install even some of its map/money/country fields.
    const auto packet=[&](int type){NetLink::Writer w;w.w(NetLink::SIMULATION_VERSION);w.w((uint8_t)RulesChoice::Fusion);
        if(type==NetLink::MsgHello){w.w((uint8_t)Country::America);w.w(uint8_t{1});}
        else {w.w(uint64_t{20261113});if(type==NetLink::MsgWelcome){w.w(uint16_t{64});w.w(uint8_t{0});w.w(int32_t{10000});w.w(uint8_t{1});w.w((uint8_t)Country::America);w.w(uint8_t{1});}}
        w.w(*rules);return w.b;};
    const auto rejected=[&](int type,std::vector<uint8_t> data,NetLink::RejectReason reason,bool wrongPhase=false){
        Game g;g.selectRulesChoice(RulesChoice::Fusion);g.world=sums.world;g.world.map.bldOccRef=&g.world.bldOcc;
        g.lobbyRole=type==NetLink::MsgHello || type==NetLink::MsgReady?0:1;
        g.lobbyState=wrongPhase?0:type==NetLink::MsgReady?5:type==NetLink::MsgStart?2:1;
        g.lobbyHelloSent=true;g.lobbyPeerRules=g.lobbyAgreedRules=*rules;g.peerCountry=(int)Country::Russia;g.peerColor=0;
        g.cfgMapSize=96;g.cfgMoney=12345;g.cfgCrates=false;g.netSeed=type==NetLink::MsgWelcome?110:20261113;
        const auto seed=g.netSeed;const auto before=g.world.checksum();
        g.net.inbox.push_back({(uint8_t)type,std::move(data)});g.netHandleMsgs();
        return !g.netGame && g.netSimTick==0 && g.lobbyState==3 && g.lobbyError==reason && g.world.checksum()==before
            && g.cfgMapSize==96 && g.cfgMoney==12345 && !g.cfgCrates && g.netSeed==seed && g.peerCountry==(int)Country::Russia && g.peerColor==0;};
    for(int type:{NetLink::MsgHello,NetLink::MsgWelcome,NetLink::MsgReady,NetLink::MsgStart}){
        const auto full=packet(type);bool bounded=true;
        for(size_t size=0;size<full.size();++size)bounded &= rejected(type,{full.begin(),full.begin()+size},NetLink::RejectReason::Protocol);
        auto changed=full;changed.push_back(0);bounded &= rejected(type,changed,NetLink::RejectReason::Protocol);
        check(bounded,"every truncated prefix and trailing byte in handshake messages is rejected without partial world/configuration mutation");
        changed=full;uint32_t oldVersion=26;std::memcpy(changed.data(),&oldVersion,4);
        check(rejected(type,changed,NetLink::RejectReason::Version),"old protocol packets stop before creating a network world");
        changed=full;changed[4]=(uint8_t)RulesChoice::RA2;
        check(rejected(type,changed,NetLink::RejectReason::Profile),"a different rule selection is refused at every handshake stage before changing the world");
        changed=full;changed.back()^=0x80;
        check(rejected(type,changed,NetLink::RejectReason::Rules),"a mismatching rules digest at every handshake stage leaves the current world and options intact");
        check(rejected(type,full,NetLink::RejectReason::Protocol,true),"a valid packet in the wrong phase cannot start or reconfigure a match");
    }
    {
        Game g;g.selectRulesChoice(RulesChoice::Fusion);g.world=sums.world;g.world.map.bldOccRef=&g.world.bldOcc;g.lobbyRole=0;g.lobbyState=2;g.lobbyPeerRules=*rules;
        const auto before=g.world.checksum();const auto original=g_gameRules;++g_gameRules.oreRegrowRate;
        const bool started=g.netStartHandshake(20261113);g_gameRules=original;
        check(!started && !g.netGame && g.lobbyError==NetLink::RejectReason::Rules && g.world.checksum()==before,
            "a local rules change after Hello is rejected before the host sends Welcome or enters the game");
    }
    for(int type:{NetLink::MsgReady,NetLink::MsgStart}){
        const auto original=g_gameRules;++g_gameRules.oreRegrowRate;
        const bool stopped=rejected(type,packet(type),NetLink::RejectReason::Rules);g_gameRules=original;
        check(stopped,"rules changed while awaiting confirmation are rechecked before either start transition");
    }
    check(rejected(NetLink::MsgReject,{2},NetLink::RejectReason::Rules)
        && rejected(NetLink::MsgReject,{5},NetLink::RejectReason::Profile)
        && rejected(NetLink::MsgReject,{},NetLink::RejectReason::Protocol)
        && rejected(NetLink::MsgReject,{0},NetLink::RejectReason::Protocol)
        && rejected(NetLink::MsgReject,{255},NetLink::RejectReason::Protocol)
        && rejected(NetLink::MsgReject,{2,0},NetLink::RejectReason::Protocol),
        "bounded rejection reasons are delivered without mutating the world; invalid reasons and trailing bytes fail");
    {
        Game server,joiner;server.selectRulesChoice(RulesChoice::Fusion);joiner.selectRulesChoice(RulesChoice::Fusion);
        server.world=joiner.world=sums.world;
        server.world.map.bldOccRef=&server.world.bldOcc;joiner.world.map.bldOccRef=&joiner.world.bldOcc;
        server.lobbyRole=0;joiner.lobbyRole=1;server.phase=joiner.phase=Phase::NetLobby;server.lobbyState=joiner.lobbyState=1;
        server.cfgCountry=(int)Country::China;server.cfgColor=0;joiner.cfgCountry=(int)Country::Russia;joiner.cfgColor=1;
        const auto before=server.world.checksum();bool linked=link(joiner,server.net);
        for(int i=0;i<1000 && linked && server.lobbyState!=2;++i){joiner.netHandleMsgs();server.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        const bool hello=linked && joiner.lobbyHelloSent && server.lobbyState==2;
        const bool offered=hello && server.netStartHandshake(20261113);
        check(offered && server.lobbyState==5 && !server.netGame && !joiner.netGame && server.world.checksum()==before,
            "the actual lobby emits Hello automatically and the host preserves its current world while waiting for a real Ready");
        for(int i=0;i<1000 && offered && (!server.netGame || !joiner.netGame);++i){joiner.netHandleMsgs();server.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(server.netGame && joiner.netGame && server.netPlayer==0 && joiner.netPlayer==1
            && server.world.tick==0 && joiner.world.tick==0 && server.world.checksum()==joiner.world.checksum(),
            "a real Hello/Welcome/Ready/Start exchange creates identical fresh worlds through the same methods used by the UI");
    }
    // Render the actual error state through the normal lobby UI. Keep the
    // localized prompt readable at both supported screenshot sizes.
    const auto output=(std::filesystem::path(GetApplicationDirectory())/".."/"net-protocol-audit").lexically_normal();
    std::filesystem::create_directories(output);const int language=g_lang;const auto previousPhase=phase;
    world=sums.world;world.map.bldOccRef=&world.bldOcc;phase=Phase::NetLobby;lobbyRole=0;lobbyState=3;
    lobbyError=NetLink::RejectReason::Rules;sim=SimInput{};sim.active=true;sim.pos={-100,-100};
    for(int locale:{0,1})for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){g_lang=locale;SetWindowSize(size.x,size.y);
        const auto file=(output/(std::string("rules-mismatch-")+(locale?"en-":"zh-")+std::to_string(size.x)+".png")).string();
        const auto before=world.checksum();shotAtDisplaySize=true;shotFile=file;render();Image frame=LoadImage(file.c_str());
        check(frame.data && frame.width==size.x && frame.height==size.y && world.checksum()==before,
            "the actual localized mismatch lobby renders at the requested size without advancing or mutating the world");UnloadImage(frame);}
    g_lang=language;phase=previousPhase;netResetHandshake();lobbyState=0;
    return failures;
}
