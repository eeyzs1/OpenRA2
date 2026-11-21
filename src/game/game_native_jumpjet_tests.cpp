#include "game_regression.h"
#include "game/script.h"
#include "world_save_sections.h"
#include "fixtures/native_jumpjet_reference.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

int GameRegression::nativeJumpjetRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);g_script.shutdown();int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_JUMPJET %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto fresh=[&](World& w,const char* profile){bool ready=w.init(64,64,1181,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,profile);
        if(!ready)return false;w.ents.clear();w.freeList.clear();std::fill(w.bldOcc.begin(),w.bldOcc.end(),0);
        for(auto& cell:w.map.cells){cell.height=0;cell.terrain=Terrain::Clear;cell.ore=0;cell.blocked=false;cell.overlay=Overlay::None;}
        for(auto& p:w.players)p.isAI=false;w.rebuildUnitOcc();w.rebuildAirOcc();return true;};
    const auto spawn=[&](World& w,int player,UnitType type,float x,float y){const EID id=w.spawnUnit(player,type,x,y);
        if(w.valid(id)){auto& e=w.ents[id];e.dir=e.turretDir=7;e.nativeJumpjet={};w.initializeNativeJumpjet(e);w.unitOccSync(id);w.airOccSync(id);}return id;};
    const auto roundtrip=[&](World& sample,int ticks){
        FILE* file=std::tmpfile();World replay;bool ok=file && sample.saveGame(file);
        if(ok){std::rewind(file);ok=replay.loadGame(file);}if(file)std::fclose(file);
        ok &= sample.checksum()==replay.checksum();
        for(int i=0;i<ticks && ok;++i){sample.update();replay.update();ok=sample.checksum()==replay.checksum();}return ok;
    };
    World w;bool ready=fresh(w,"assets/rules/profiles/ra2.ini");
    check(ready,"the source RA2 world initializes");if(!ready)return failures;
    int profiles=0;for(const auto& d:w.nativeTypes)profiles+=d->jumpjet.available;
    check(profiles==3,"RA2 retains all three original Jumpjet parameter sets, including flying infantry");if(profiles!=3)return failures;
    const EID zep=spawn(w,1,UnitType::Kirov,20.5f,20.5f);
    check(w.valid(zep) && w.ents[zep].nativeIdentity.definition->jumpjet.speed==5
        && w.ents[zep].nativeIdentity.definition->jumpjet.climb==6,"ZEP binds raw JumpjetSpeed=5 and JumpjetClimb=6");
    w.orderMove({zep},20.5f,8.5f,false);World legacy=w;legacy.nativeJumpjetEnabled=false;
    legacy.ents[zep].nativeJumpjet={};legacy.ents[zep].nativeAltitude=legacy.ents[zep].previousNativeAltitude=0;
    for(int i=0;i<20;++i){w.update();legacy.update();}
    const auto& rise=NativeJumpjetReference::cases[3];
    check(w.ents[zep].nativeAltitude==rise.frames[9].z && w.ents[zep].x==20.5f && w.ents[zep].y==20.5f,
        "20 real world ticks match ten original ascent frames and prevent premature horizontal movement");
    check(20.5f-legacy.ents[zep].y>.5f,"the paired legacy control reproduces excessive prototype Kirov movement");
    w.update();check(w.ents[zep].nativeJumpjet.motion.half==1,"a real ascent can be saved during an unfinished 15-Hz half-frame");
    const auto save=(std::filesystem::path(outputDir)/"jumpjet-world56.sav").string();
    FILE* f=std::fopen(save.c_str(),"wb");bool saved=f && w.saveGame(f);if(f)std::fclose(f);World copy;
    f=std::fopen(save.c_str(),"rb");saved &= f && copy.loadGame(f);if(f)std::fclose(f);saved &= copy.checksum()==w.checksum();
    for(int i=0;i<300 && saved;++i){w.update();copy.update();saved &= w.checksum()==copy.checksum();}
    check(saved,"World55 preserves height, acceleration, facing, phase and half-frame through 300 resumed ticks");
    {
        std::ifstream input(save,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        const auto block=locateWorldSaveSection(bytes,WorldSaveSection::NativeJumpjet);bool rejected=block && block->length>=64;
        if(rejected){const double nan=std::numeric_limits<double>::quiet_NaN();std::memcpy(bytes.data()+block->offset+56,&nan,8);
            f=std::tmpfile();rejected=f && std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size() && !std::fseek(f,0,SEEK_SET);
            World bad;if(rejected)rejected=!bad.loadGame(f);if(f)std::fclose(f);}
        check(rejected,"nonfinite serialized Jumpjet speed is rejected");
    }
    {
        World flight;bool ok=fresh(flight,"assets/rules/profiles/ra2.ini");const EID actor=spawn(flight,1,UnitType::Kirov,20.5f,20.5f);
        flight.orderMove({actor},20.5f,8.5f,false);for(int i=0;i<260 && ok;++i)flight.update();
        const auto& expected=rise.frames[129];ok &= flight.ents[actor].nativeAltitude==expected.z && flight.ents[actor].x==20.5f
            && flight.ents[actor].y==20.5f+(expected.y-25600)/256.f;
        check(ok,"260 real ticks reproduce the original Kirov rise and travel coordinates exactly");
    }
    {
        World idle;bool ok=fresh(idle,"assets/rules/profiles/ra2.ini");const EID actor=spawn(idle,1,UnitType::Kirov,20.5f,20.5f);
        for(int i=0;i<300 && ok;++i)idle.update();ok &= idle.ents[actor].x==20.5f && idle.ents[actor].y==20.5f && idle.ents[actor].nativeAltitude==750;
        check(ok,"a balloon unit rises and hovers in place without an unintended movement command");
    }
    {
        World transport;bool ok=fresh(transport,"assets/rules/profiles/ra2.ini");const EID actor=spawn(transport,0,UnitType::Nighthawk,20.5f,20.5f);
        ok &= !transport.ents[actor].airborne();transport.orderMove({actor},20.5f,16.5f,false);
        bool airborne=false;for(int i=0;i<600 && ok;++i){transport.update();airborne |= transport.ents[actor].airborne();}
        ok &= airborne && !transport.ents[actor].airborne() && transport.ents[actor].state==UState::Landed
            && transport.ents[actor].x==20.5f && transport.ents[actor].y==16.5f
            && transport.groundUnitBlocksCell((int)transport.ents[actor].x,(int)transport.ents[actor].y);
        check(ok,"a normal Nighthawk move takes off, reaches its destination, lands and occupies the ground");
        transport.orderMove({actor},20.5f,20.5f,false);bool departed=false;
        for(int i=0;i<600 && ok;++i){transport.update();departed |= transport.ents[actor].airborne();}
        check(ok && departed && !transport.ents[actor].airborne() && transport.ents[actor].x==20.5f && transport.ents[actor].y==20.5f,
            "a landed Nighthawk releases ground occupation, takes off again and completes its return move");
    }
    {
        World blocked;bool ok=fresh(blocked,"review_out/project-review-20261115/air-ground-reference/ra2-scene22.ini");const EID actor=spawn(blocked,0,UnitType::Nighthawk,20.5f,20.5f);
        blocked.orderMove({actor},20.5f,16.5f,false);int i=0;
        for(;i<600 && ok;++i){blocked.update();if(blocked.ents[actor].nativeJumpjet.phase==NativeJumpjetPhase::Descent && blocked.ents[actor].nativeAltitude<100)break;}
        ok &= i<600;const int x=(int)blocked.ents[actor].x,y=(int)blocked.ents[actor].y;
        const EID obstruction=spawn(blocked,1,UnitType::MCV,x+.5f,y+.5f);
        for(i=0;i<160 && ok;++i){blocked.update();ok &= blocked.ents[actor].airborne() && blocked.groundUnitBlocksCell(x,y,actor,0);}
        check(ok && roundtrip(blocked,20),"the actual Scene22 profile preserves its historical blocked-cell hover through a saved descent");
        blocked.kill(obstruction,false);for(i=0;i<600 && ok;++i)blocked.update();
        check(ok && !blocked.ents[actor].airborne() && blocked.ents[actor].state==UState::Landed,
            "clearing a blocked landing cell allows the waiting transport to descend safely");
    }
    {
        Game frozen;bool ok=fresh(frozen.world,"assets/rules/profiles/ra2.ini");const EID actor=spawn(frozen.world,1,UnitType::Kirov,20.5f,20.5f);
        for(int i=0;i<21 && ok;++i)frozen.world.update();const auto before=frozen.world.ents[actor].nativeJumpjet;
        const float height=frozen.world.ents[actor].nativeAltitude;frozen.world.ents[actor].tpSick=4;
        for(int i=0;i<4 && ok;++i){frozen.world.update();const auto& e=frozen.world.ents[actor];
            frozen.interpAlpha=0;const float early=frozen.unitRenderLift(e);frozen.interpAlpha=1;
            ok &= e.nativeJumpjet==before && e.nativeAltitude==height && frozen.unitRenderLift(e)==early;}
        check(ok && roundtrip(frozen.world,40),"an immobilized Jumpjet freezes both physics and render height without replaying the preceding interpolation");
    }
    {
        World infantry;bool ok=fresh(infantry,"assets/rules/profiles/ra2.ini");const EID actor=spawn(infantry,0,UnitType::Rocketeer,20.5f,20.5f);
        ok &= infantry.ents[actor].nativeIdentity.definition->jumpjet.infantry;infantry.orderMove({actor},20.5f,8.5f,false);
        for(int i=0;i<20 && ok;++i)infantry.update();check(ok && infantry.ents[actor].nativeAltitude==NativeJumpjetReference::cases[4].frames[9].z,
            "flying infantry uses its original ascent parameters rather than the ground Speed field");
    }
    Game app;app.world=copy;app.cfgRules=RulesChoice::RA2;app.cfgAI=1;app.cfgMapSize=64;app.cfgCountry=(int)Country::America;app.localPlayer=0;app.phase=Phase::InGame;
    const auto appPath=(std::filesystem::path(outputDir)/"jumpjet-app11.sav").string();bool application=app.saveGameFile(appPath.c_str());Game restored;
    application &= restored.loadGameFile(appPath.c_str()) && app.world.checksum()==restored.world.checksum();
    for(int i=0;i<120 && application;++i){app.world.update();restored.world.update();application &= app.world.checksum()==restored.world.checksum();}
    check(application,"the real App11 archive resumes native Jumpjet movement for 120 matching ticks");
    {
        std::ifstream input(appPath,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        const auto found=embeddedWorldSaveOffset(bytes);
        const size_t offset=found.value_or(bytes.size());
        const auto section=found?locateWorldSaveSection(std::span<const uint8_t>(bytes).subspan(offset),WorldSaveSection::NativeJumpjet):std::nullopt;
        bool rejected=section && section->length>=64;const auto before=restored.world.checksum();
        if(rejected){const double invalid=std::numeric_limits<double>::infinity();std::memcpy(bytes.data()+offset+section->offset+56,&invalid,8);
            const auto path=(std::filesystem::path(outputDir)/"invalid-jumpjet-app.sav").string();
            {std::ofstream out(path,std::ios::binary);out.write((const char*)bytes.data(),bytes.size());}
            rejected=!restored.loadGameFile(path.c_str()) && restored.world.checksum()==before && restored.cfgRules==RulesChoice::RA2;}
        check(rejected,"an invalid nested Jumpjet state rejects App11 loading without replacing the live match");
    }
    Game old;const bool historical=old.loadGameFile("review_out/project-review-20261115/legacy-world52-app11-scene21.sav");
    check(historical && !old.world.nativeJumpjetEnabled && old.world.nativeFlightEnabled,
        "an actual World52/Scene21 archive retains fixed-wing motion and its previous Jumpjet behavior");
    if(historical){old.restartCurrentMatch();check(!old.world.nativeJumpjetEnabled && old.world.nativeFlightEnabled,
        "Restart preserves the legacy Jumpjet flag and embedded source definitions");}
    World yr;bool yuri=fresh(yr,"assets/rules/profiles/yr.ini");int count=0;for(const auto& d:yr.nativeTypes)count+=d->jumpjet.available;
    check(yuri && count==5,"the YR source package retains all five Jumpjet parameter sets");
    if(yuri && count==5){
        const EID disc=spawn(yr,1,UnitType::FloatingDisc,20.5f,20.5f);yr.orderMove({disc},20.5f,8.5f,false);
        const auto& p=yr.ents[disc].nativeIdentity.definition->jumpjet;
        check(p.speed==16 && p.turnRate==100 && p.climb==8 && p.height==750,"the YR disc retains its distinct source speed, turn rate, climb and altitude");
        for(int i=0;i<21;++i)yr.update();bool moving=yr.ents[disc].nativeAltitude>0 && yr.ents[disc].airborne();
        check(moving && roundtrip(yr,260),"a YR disc ascends, moves and resumes its original-parameter state through 260 matching ticks");
        for(bool immediate:{false,true}){
            World chopper;bool ok=fresh(chopper,"assets/rules/profiles/yr.ini");const EID actor=spawn(chopper,1,UnitType::SiegeChopper,20.5f,20.5f);
            if(!immediate)for(int i=0;i<160;++i)chopper.update();const float initial=chopper.ents[actor].nativeAltitude;
            ok &= chopper.orderDeployment({actor},true).accepted==1;chopper.update();ok &= chopper.ents[actor].nativeAltitude<=initial;
            ok &= roundtrip(chopper,7);for(int i=0;i<g_gameRules.chopperLandingTicks+g_gameRules.chopperDeployTicks+8;++i)chopper.update();
            ok &= chopper.ents[actor].deployed && !chopper.ents[actor].deploying() && chopper.ents[actor].nativeAltitude==0
                && !chopper.ents[actor].airborne() && chopper.groundUnitBlocksCell(20,20);
            check(ok,immediate?"deploying a newly spawned chopper keeps its physical height on the ground":"an airborne YR chopper descends, unfolds, saves and occupies the ground");
            const bool accepted=chopper.orderDeployment({actor},true).accepted==1;const bool resumed=roundtrip(chopper,7);ok &= accepted && resumed;
            for(int i=0;i<g_gameRules.chopperLandingTicks+g_gameRules.chopperDeployTicks+80;++i)chopper.update();
            if(!ok || chopper.ents[actor].nativeAltitude!=500)std::printf("NATIVE_JUMPJET CHOPPER immediate=%d accepted=%d resumed=%d prior=%d height=%.9g state=%d phase=%d deployed=%d anim=%d\n",
                immediate,accepted,resumed,ok,chopper.ents[actor].nativeAltitude,(int)chopper.ents[actor].state,(int)chopper.ents[actor].nativeJumpjet.phase,chopper.ents[actor].deployed,chopper.ents[actor].deployAnim);
            check(ok && !chopper.ents[actor].deployed && !chopper.ents[actor].deploying() && chopper.ents[actor].airborne()
                && chopper.ents[actor].nativeAltitude==500,"a deployed YR chopper folds, takes off and returns to its source cruise height");
        }
    }
    int port=NET_PORT+850;
    for(auto choice:{RulesChoice::RA2,RulesChoice::YR}){
        Game host,client;host.selectRulesChoice(choice);client.selectRulesChoice(choice);
        host.cfgCountry=client.cfgCountry=(int)Country::America;host.cfgMapSize=64;host.cfgCrates=false;host.cfgColor=0;client.cfgColor=1;
        host.lobbyRole=0;client.lobbyRole=1;host.phase=client.phase=Phase::NetLobby;host.lobbyState=client.lobbyState=1;
        bool connected=false;for(int i=0;i<100 && !connected;++i)connected=host.net.host(++port);
        connected &= client.net.connectTo("127.0.0.1",port);
        for(int i=0;i<1000 && connected && (!host.net.connected() || !client.net.connected());++i){host.net.poll();client.net.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        for(int i=0;i<1000 && connected && host.lobbyState!=2;++i){host.netHandleMsgs();client.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        connected &= host.lobbyState==2 && host.netStartHandshake(20261118);
        for(int i=0;i<1000 && connected && (!host.netGame || !client.netGame);++i){host.netHandleMsgs();client.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        bool synchronized=connected && host.netGame && client.netGame;EID aircraft[2]={-1,-1};
        if(synchronized)for(Game* peer:{&host,&client})for(int p=0;p<2;++p){
            const auto type=choice==RulesChoice::RA2?(p?UnitType::Nighthawk:UnitType::Kirov):(p?UnitType::SiegeChopper:UnitType::FloatingDisc);
            const EID id=peer->world.spawnUnit(p,type,20.5f+p*16,30.5f);if(peer==&host)aircraft[p]=id;else synchronized &= aircraft[p]==id;}
        for(int stage=0;stage<2 && synchronized;++stage){
            for(Game* peer:{&host,&client}){World::Cmd move{};move.type=World::Cmd::Move;move.ids={aircraft[peer->localPlayer]};
                move.x=stage?45.5f:20.5f+peer->localPlayer*16;move.y=stage?30.5f:8.5f;peer->issueCmd(move);}
            const int until=(stage+1)*120;
            for(int i=0;i<4000 && (host.netSimTick<until || client.netSimTick<until) && !host.netDesync && !client.netDesync;++i){
                host.netHandleMsgs();client.netHandleMsgs();if(host.netSimTick<until)host.netAdvance();if(client.netSimTick<until)client.netAdvance();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            synchronized &= host.netSimTick==until && client.netSimTick==until && !host.netDesync && !client.netDesync && host.world.checksum()==client.world.checksum();
        }
        check(synchronized && host.world.valid(aircraft[0]) && host.world.ents[aircraft[0]].nativeJumpjet.motion.started && roundtrip(host.world,60),
            "real TCP Move and mid-flight turn commands synchronize native Jumpjet height and facing for 240 ticks, then survive a save");
    }
    {
        bool ok=fresh(world,"assets/rules/profiles/yr.ini");
        const EID zep=spawn(world,0,UnitType::Kirov,18.5f,20.5f),rocketeer=spawn(world,0,UnitType::Rocketeer,22.5f,21.5f);
        const EID transport=spawn(world,0,UnitType::Nighthawk,22.5f,25.5f),disc=spawn(world,0,UnitType::FloatingDisc,26.5f,23.5f);
        for(int i=0;i<300 && ok;++i)world.update();
        world.map.bldOccRef=&world.bldOcc;resetMatchInteraction();resetNotifications();ais.clear();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=paused=showMenu=netGame=false;
        sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;SetTargetFPS(0);bakeTerrain();
        int x,y;tileToScreen(22,22,x,y);camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;clampCameraToMap();
        shotAtDisplaySize=true;
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){
            SetWindowSize(size.x,size.y);const auto before=world.checksum();
            const auto path=(std::filesystem::path(outputDir)/("source-heights-"+std::to_string(size.x)+".png")).string();shotFile=path;render();
            Image image=LoadImage(path.c_str());bool visible=image.data && image.width==size.x && image.height==size.y && before==world.checksum();UnloadImage(image);
            for(const EID actor:{zep,rocketeer,transport,disc}){
                const auto& e=world.ents[actor];const auto box=unitScreenRect(e);
                visible &= pickUnit((int)(box.x+box.width*.5f),(int)(box.y+box.height*.5f))==actor;
                PresentationEvent event;event.x=e.x;event.y=e.y;world.captureVisualTarget(event,e);
                visible &= eventRenderLift(event)==unitRenderLift(e);
            }
            check(ok && visible,"source cruise heights and a landed transport render, remain selectable and match event height at the selected display resolution");
        }
    }
    g_script.shutdown();std::printf("NATIVE_JUMPJET failures=%d\n",failures);return failures;
}
