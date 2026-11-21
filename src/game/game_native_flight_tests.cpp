#include "game_regression.h"
#include "game/script.h"
#include "world_save_sections.h"
#include "fixtures/native_flight_reference.h"
#include "core/ini.h"
#include <bit>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <thread>
#include <algorithm>

int GameRegression::nativeFlightRegression(const char* outputDir) {
    namespace Ref=NativeFlightReference;
    int failures=0;std::filesystem::create_directories(outputDir);g_script.shutdown();
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_FLIGHT %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    check(NativeFlightMath::tableChecksum()==Ref::tableChecksum,"all 30,721 generated math table values match the local original executable");
    {const auto values=NativeFlightMath::tableValues();std::ofstream out(std::filesystem::path(outputDir)/"math-tables.bin",std::ios::binary);
        out.write((const char*)values.data(),values.size()*sizeof(uint32_t));}
    bool directions=true;
    for(const auto& d:Ref::directions)directions &= NativeFlightMath::direction(d.x,d.y)==d.heading;
    check(directions,"49 signed and axis directions match actual original atan2 and heading conversion calls");
    bool rotations=true;
    for(const auto& r:Ref::rotations){Ini ini;ini.sections.push_back({"Type.0",{{"OriginalLocomotor","{4A582746-9839-11D1-B709-00A024DDAFD1}"},
        {"FlightVersion","1"},{"OriginalSpeed","14"},{"FlightROT",std::to_string(r.raw)},{"FlightSlowdownDistance","500"}}});
        NativeFlightProfile p;rotations &= parseNativeFlightProfile(ini,0,p) && p.rotation==r.rate;}
    check(rotations,"source ROT parsing matches seven original FacingClass setter calls, including the 127 clamp");
    {Ini ini;ini.sections.push_back({"Type.0",{{"OriginalLocomotor","{4A582746-9839-11D1-B709-00A024DDAFD1}"}}});
        NativeFlightProfile p;bool strict=!parseNativeFlightProfile(ini,0,p);
        ini.sections[0].kv={{"OriginalLocomotor","unknown"},{"FlightVersion","1"}};
        strict &= !parseNativeFlightProfile(ini,0,p);check(strict,"incomplete or mismatched flight metadata is rejected without silently selecting prototype flight");}
    int number=0;
    for(const auto& c:Ref::cases) {
        NativeFlightProfile profile{c.speed,c.rotation*256,500,true};NativeFlightState s;
        s.throttle=c.throttle;s.facing=s.startFacing=s.desiredFacing=(uint16_t)c.heading;
        if(c.turning)NativeFlightMath::turn(s,(uint16_t)c.target,profile.rotation);
        int x=25600,y=25600;bool same=true;
        for(int i=0;i<c.count;++i) {
            s.frame=i+1;
            if(c.retarget && s.frame==7)NativeFlightMath::turn(s,49151,profile.rotation);
            NativeFlightMath::advance(profile,s,x,y,25600,25600-c.distance,true);x+=s.dx;y+=s.dy;
            const auto& expected=c.frames[i];
            const bool row=x==expected.x && y==expected.y && s.facing==expected.heading && std::bit_cast<uint64_t>(s.throttle)==expected.throttle;
            if(same && !row)std::printf("NATIVE_FLIGHT TRACE case=%d frame=%d xy=%d,%d expected=%d,%d heading=%d expected=%d throttle=%016llx expected=%016llx\n",
                number,i+1,x,y,expected.x,expected.y,s.facing,expected.heading,(unsigned long long)std::bit_cast<uint64_t>(s.throttle),(unsigned long long)expected.throttle);
            same &= row;
        }
        check(same,("original locomotor trajectory "+std::to_string(number++)).c_str());
    }
    World sample;bool loaded=sample.init(64,64,881,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,"assets/rules/profiles/ra2.ini");
    check(loaded,"the current source profile initializes the normal world");if(!loaded)return failures;
    for(int p=0;p<sample.numPlayers;++p)sample.players[p].isAI=false;
    const EID plane=sample.spawnUnit(0,UnitType::Intruder,20.5f,20.5f);
    bool profile=sample.valid(plane) && sample.ents[plane].nativeIdentity.definition->id=="ORCA";
    if(profile){const auto& p=sample.ents[plane].nativeIdentity.definition->flight;profile=p.available && p.maxSpeed==35 && p.rotation==768;}
    check(profile,"the original ORCA uses source Speed=14 and ROT=3, instead of the prototype movement delay");if(!profile)return failures;
    sample.ents[plane].dir=7;sample.orderMove({plane},20.5f,5.5f,false);
    World legacy=sample;legacy.nativeFlightEnabled=false;legacy.supportDeliveryEnabled=false;
    for(int i=0;i<20;++i){sample.update();legacy.update();}
    const auto& expected=Ref::cases[1].frames[9];
    check(sample.ents[plane].nativeFlight.frame==10 && sample.ents[plane].x==20.5f
        && sample.ents[plane].y==20.5f+(expected.y-25600)/256.f,
        "20 real 30-Hz world ticks reproduce ten original 15-Hz acceleration frames exactly");
    check(20.5f-sample.ents[plane].y<2 && 20.5f-legacy.ents[plane].y>5,
        "the retained legacy control reproduces the excessive original prototype flight speed");
    sample.orderMove({plane},42.5f,20.5f,false);sample.update();
    check(sample.ents[plane].nativeFlight.half==1 && sample.ents[plane].dir!=1,
        "a real right-angle Move command starts a gradual turn during an unfinished 15-Hz frame");
    const auto path=(std::filesystem::path(outputDir)/"flight-world56.sav").string();
    FILE* file=std::fopen(path.c_str(),"wb");bool saved=file && sample.saveGame(file);if(file)std::fclose(file);
    World copy;file=std::fopen(path.c_str(),"rb");saved &= file && copy.loadGame(file);if(file)std::fclose(file);
    saved &= copy.checksum()==sample.checksum();
    for(int i=0;i<240 && saved;++i){sample.update();copy.update();saved &= sample.checksum()==copy.checksum();}
    check(saved,"World55 saves an unfinished flight frame and turn, then continues for 240 matching ticks");
    {
        std::ifstream input(path,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        const auto section=locateWorldSaveSection(bytes,WorldSaveSection::NativeFlight);bool rejected=section && section->length>=9+4+8;
        if(rejected){const double invalid=std::numeric_limits<double>::quiet_NaN();std::memcpy(bytes.data()+section->offset+9+4,&invalid,8);
            file=std::tmpfile();rejected=file && std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size() && !std::fseek(file,0,SEEK_SET);
            World invalidWorld;if(rejected)rejected=!invalidWorld.loadGame(file);if(file)std::fclose(file);}
        check(rejected,"a nonfinite flight throttle is rejected when reading a real serialized world");
    }
    const auto app=(std::filesystem::path(outputDir)/"flight-app11.sav").string();
    Game application;application.world=sample;application.cfgAI=1;application.cfgMapSize=64;application.cfgCountry=(int)Country::America;
    application.cfgRules=RulesChoice::RA2;application.localPlayer=0;application.phase=Phase::InGame;
    bool roundtrip=application.saveGameFile(app.c_str());Game resumed;roundtrip &= resumed.loadGameFile(app.c_str());
    roundtrip &= resumed.world.nativeFlightEnabled && resumed.world.checksum()==application.world.checksum();
    for(int i=0;i<120 && roundtrip;++i){application.world.update();resumed.world.update();roundtrip &= application.world.checksum()==resumed.world.checksum();}
    check(roundtrip,"the real App11 archive retains source flight state through 120 resumed ticks");
    {
        std::ifstream input(app,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        const auto found=embeddedWorldSaveOffset(bytes);
        const size_t worldOffset=found.value_or(bytes.size());
        const auto section=found?locateWorldSaveSection(std::span<const uint8_t>(bytes).subspan(worldOffset),WorldSaveSection::NativeFlight):std::nullopt;
        bool refused=section && section->length>=21;const auto before=resumed.world.checksum();
        if(refused){
            const double invalid=std::numeric_limits<double>::infinity();std::memcpy(bytes.data()+worldOffset+section->offset+13,&invalid,8);
            const auto bad=(std::filesystem::path(outputDir)/"invalid-flight-app.sav").string();
            {std::ofstream out(bad,std::ios::binary);out.write((const char*)bytes.data(),bytes.size());}
            refused=!resumed.loadGameFile(bad.c_str()) && resumed.world.checksum()==before && resumed.cfgRules==RulesChoice::RA2;}
        check(refused,"an invalid nested flight state rejects App11 loading without replacing the live match");
    }
    Game old;bool previous=old.loadGameFile("review_out/project-review-20261115/legacy-world51-app11-scene20.sav");
    check(previous && old.world.usesRulesProfile() && !old.world.nativeFlightEnabled,"an actual previous App11/World51/Scene20 snapshot keeps its original flight semantics");
    if(previous){const bool disabled=old.world.nativeFlightEnabled;old.restartCurrentMatch();check(old.world.nativeFlightEnabled==disabled,
        "restarting a legacy source match also preserves its embedded older flight model");}
    int port=NET_PORT+650;
    for(auto choice:{RulesChoice::RA2,RulesChoice::YR}) {
        Game host,client;host.selectRulesChoice(choice);client.selectRulesChoice(choice);
        host.cfgCountry=client.cfgCountry=(int)Country::America;host.cfgMapSize=64;host.cfgCrates=false;host.cfgColor=0;client.cfgColor=1;
        host.lobbyRole=0;client.lobbyRole=1;host.phase=client.phase=Phase::NetLobby;host.lobbyState=client.lobbyState=1;
        bool connected=false;for(int i=0;i<100 && !connected;++i)connected=host.net.host(++port);
        connected &= client.net.connectTo("127.0.0.1",port);
        for(int i=0;i<1000 && connected && (!host.net.connected() || !client.net.connected());++i){host.net.poll();client.net.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        for(int i=0;i<1000 && connected && host.lobbyState!=2;++i){host.netHandleMsgs();client.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        connected &= host.lobbyState==2 && host.netStartHandshake(20261117);
        for(int i=0;i<1000 && connected && (!host.netGame || !client.netGame);++i){host.netHandleMsgs();client.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        bool synchronized=connected && host.netGame && client.netGame;
        EID aircraft[2]={-1,-1};
        if(synchronized)for(Game* peer:{&host,&client})for(int p=0;p<2;++p){const EID id=peer->world.spawnUnit(p,UnitType::Intruder,20.5f+p*16,30.5f);
            if(peer==&host)aircraft[p]=id;else synchronized &= aircraft[p]==id;}
        for(int stage=0;stage<2 && synchronized;++stage) {
            for(Game* peer:{&host,&client}){World::Cmd move{};move.type=World::Cmd::Move;move.ids={aircraft[peer->localPlayer]};
                move.x=stage?45.5f:20.5f+peer->localPlayer*16;move.y=stage?30.5f:8.5f;peer->issueCmd(move);}
            const int until=(stage+1)*120;
            for(int i=0;i<4000 && (host.netSimTick<until || client.netSimTick<until) && !host.netDesync && !client.netDesync;++i){
                host.netHandleMsgs();client.netHandleMsgs();if(host.netSimTick<until)host.netAdvance();if(client.netSimTick<until)client.netAdvance();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            synchronized &= host.netSimTick==until && client.netSimTick==until && !host.netDesync && !client.netDesync
                && host.world.checksum()==client.world.checksum();
        }
        check(synchronized && host.world.valid(aircraft[0]) && host.world.ents[aircraft[0]].nativeFlight.started,
            "real TCP movement and mid-flight turn commands remain synchronized for 240 ticks under the selected source edition");
    }
    g_script.shutdown();std::printf("NATIVE_FLIGHT failures=%d\n",failures);return failures;
}
