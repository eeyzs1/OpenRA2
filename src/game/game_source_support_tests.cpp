#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <chrono>
#include <thread>

int GameRegression::sourceSupportRegression(const char* outputDir) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* label){failures+=!ok;std::printf("SOURCE_SUPPORT %s %s\n",ok?"PASS":"FAIL",label);std::fflush(stdout);};
    const auto type=[&](const char* identity){
        for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==identity)return i;return -1;
    };
    const auto fresh=[&](int choice,Faction faction=Faction::Allies){
        resetMatchInteraction();ais.clear();matchSetup.reset();campaignMission=-1;localPlayer=0;
        phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;clearPlacement();sim=SimInput{};
        selectRulesChoice(choice==0?RulesChoice::Fusion:choice==1?RulesChoice::RA2:RulesChoice::YR);
        const auto country=faction==Faction::Soviet?Country::Russia:faction==Faction::Yuri?Country::Yuri:faction==Faction::China?Country::China:Country::America;
        const auto enemy=faction==Faction::Allies?Faction::Soviet:Faction::Allies;
        const auto other=enemy==Faction::Allies?Country::America:Country::Russia;
        cfgCountry=(int)country;cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgReferenceMap=false;cfgGameMode=0;
        const char* profile=choice==0?nullptr:choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
        if(!world.init(64,64,711,2,0,{faction,enemy},0,"maps/skirmish-reference.txt",true,true,profile,{country,other}))return false;
        // The existing timer/source suite retains the World56 instantaneous contract.
        // The separate delivery suite exercises the new physical mission path.
        world.supportDeliveryEnabled=false;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        world.cratesEnabled=false;world.crates.clear();for(auto& player:world.players){player.isAI=false;player.money=10000;}
        world.spawnBuilding(0,BldType::ConYard,3,3,true);world.spawnBuilding(1,BldType::ConYard,55,55,true);
        for(int i=0;i<5;++i)world.spawnBuilding(0,BldType::TeslaReactor,35+i*4,4,true,choice?type("NAPOWR"):-1);
        return world.independentSupportEnabled && world.veterancyProgressEnabled;
    };
    const auto sources=[&](int choice){
        std::array<EID,SUPPORT_COUNT> ids;
        ids[0]=world.spawnBuilding(0,BldType::TechAirport,8,15,true,choice?type("CAAIRP"):-1);
        ids[1]=world.spawnBuilding(0,BldType::AirForceCmd,15,15,true,choice?type("AMRADR"):-1);
        ids[2]=ids[3]=INVALID_EID;
        if(choice==2){ids[2]=world.spawnBuilding(0,BldType::Radar,22,15,true,type("NARADR"));ids[3]=world.spawnBuilding(0,BldType::PsychicSensor,28,15,true,type("NAPSIS"));}
        return ids;
    };
    const auto command=[&](SupportType ability,float x,float y){
        World::Cmd cmd{};cmd.type=World::Cmd::Paradrop;cmd.a=(int)ability+1;cmd.x=x;cmd.y=y;world.bindCommand(cmd);world.applyCmd(0,cmd);
    };
    const auto count=[&](UnitType unit){int n=0;for(const auto& actor:world.ents)n+=actor.alive && !actor.isBuilding && actor.player==0 && actor.utype==unit;return n;};
    const auto worldRoundTrip=[&](const std::string& name){
        const auto path=(std::filesystem::path(outputDir)/(name+"-world56.sav")).string();
        FILE* file=std::fopen(path.c_str(),"wb");bool same=file && world.saveGame(file);if(file)std::fclose(file);
        World restored;file=std::fopen(path.c_str(),"rb");same &= file && restored.loadGame(file);if(file)std::fclose(file);
        same &= restored.checksum()==world.checksum();
        for(int i=0;i<90 && same;++i){world.update();restored.update();same &= restored.checksum()==world.checksum();}return same;
    };
    for(int choice=0;choice<3;++choice) {
        const bool initialized=fresh(choice);check(initialized,"a normal new rules choice enables independent support and factory experience");
        if(!initialized)return failures;
        const auto ids=sources(choice);const unsigned available=choice==2?15:3;
        bool membership=true;for(int i=0;i<SUPPORT_COUNT;++i)membership &= world.hasSupportSource(0,(SupportType)i)==((available&(1u<<i))!=0);
        check(membership,"source identity grants exactly the available original support types, including captured YR buildings");
        world.update();world.players[0].powerSabotage=90;
        for(int frame=0;frame<30;++frame)world.update();
        bool charging=true;for(int i=0;i<SUPPORT_COUNT;++i)if(available&(1u<<i))charging &= world.players[0].support[i].ticks==31;
        check(charging,"original IsPowered=false support keeps charging during a real house power outage");
        while(world.players[0].support[0].ticks<3600)world.update();
        check(worldRoundTrip("half-"+std::to_string(choice)),"independent half-charge timers survive World56 and exact continuation");
        const auto app=(std::filesystem::path(outputDir)/("half-"+std::to_string(choice)+"-app11.sav")).string();
        const auto checksum=world.checksum();
        check(saveGameFile(app.c_str()) && loadGameFile(app.c_str()) && world.checksum()==checksum && world.independentSupportEnabled,
            "the actual App11 reader restores all independent support timers from World56");
        while(!world.supportReady(0,SupportType::Paradrop))world.update();
        bool ready=true;for(int i=0;i<SUPPORT_COUNT;++i)if(available&(1u<<i))ready &= world.supportReady(0,(SupportType)i);
        check(ready && world.tick==7200,"every granted support type independently becomes ready after 7200 actual ticks");
        const auto before=world.checksum();command(SupportType::AmericanParadrop,-1,40);
        check(world.checksum()==before,"an invalid support target preserves every charge and all simulation state");
        const int infantry=count(UnitType::GI);command(SupportType::AmericanParadrop,40.5f,40.5f);
        check(count(UnitType::GI)-infantry==8 && !world.supportReady(0,SupportType::AmericanParadrop) && world.supportReady(0,SupportType::Paradrop),
            "an explicit American command drops eight GI and leaves the generic airport ready");
        command(SupportType::Paradrop,30.5f,40.5f);
        check(count(UnitType::GI)-infantry==14 && !world.supportReady(0,SupportType::Paradrop),
            "the generic airport immediately drops its separate six GI for an American player");
        if(choice==2) {
            world.map.fog[0][10*64+55]=FOG_UNSEEN;command(SupportType::SpyPlane,55.5f,10.5f);
            check(world.map.fogAt(0,55,10)!=FOG_UNSEEN && !world.supportReady(0,SupportType::SpyPlane) && world.supportReady(0,SupportType::PsychicReveal),
                "the still-ready spy plane executes after both drops and preserves psychic reveal");
            world.map.fog[0][55*64+55]=FOG_UNSEEN;command(SupportType::PsychicReveal,55.5f,55.5f);
            check(world.map.fogAt(0,55,55)!=FOG_UNSEEN && !world.supportReady(0,SupportType::PsychicReveal),
                "psychic reveal then executes independently through its explicit player command");
        }
        world.players[0].support[0]={7200,true};world.players[0].support[1]={7200,true};
        const auto backup=world.spawnBuilding(0,BldType::TechAirport,8,25,true,choice?type("CAAIRP"):-1);
        world.kill(ids[0],false);check(world.supportReady(0,SupportType::Paradrop),"losing one of two generic airports preserves that house charge");
        world.kill(backup,false);
        check(!world.supportReady(0,SupportType::Paradrop) && world.players[0].support[0].ticks==0 && world.supportReady(0,SupportType::AmericanParadrop),
            "losing the last generic airport clears only its own slot immediately");
        world.spawnBuilding(0,BldType::TechAirport,8,25,true,choice?type("CAAIRP"):-1);
        check(!world.supportReady(0,SupportType::Paradrop) && !world.players[0].support[0].ticks,
            "a same-turn airport rebuild cannot revive a retired charge");
    }
    for(int choice=1;choice<3;++choice) {
        if(!fresh(choice,Faction::Soviet))return 1;
        const auto air=world.spawnBuilding(1,BldType::AirForceCmd,20,20,true,type("AMRADR"));
        const auto engineer=world.spawnUnit(0,UnitType::Engineer,19.5f,21.5f);world.map.reveal(0,22,22,10);
        World::Cmd capture{};capture.type=World::Cmd::Capture;capture.ids={engineer};capture.a=air;world.bindCommand(capture);world.applyCmd(0,capture);
        for(int i=0;i<180 && world.valid(engineer);++i)world.update();
        check(!world.valid(engineer) && world.ents[air].player==0 && world.hasSupportSource(0,SupportType::AmericanParadrop),
            "an actual Soviet engineer captures the original American support identity");
        while(!world.supportReady(0,SupportType::AmericanParadrop))world.update();
        command(SupportType::AmericanParadrop,40.5f,40.5f);
        check(count(UnitType::GI)==8 && count(UnitType::Conscript)==0,
            "the captured American command retains eight source GI for its Soviet owner");
    }
    for(int choice=0;choice<3;++choice)for(auto faction:{Faction::Allies,Faction::Soviet,Faction::Yuri,Faction::China}) {
        if((choice==1 && faction==Faction::Yuri) || (choice && faction==Faction::China))continue;
        if(!fresh(choice,faction))return 1;world.spawnBuilding(0,BldType::TechAirport,10,15,true,choice?type("CAAIRP"):-1);
        while(!world.supportReady(0,SupportType::Paradrop))world.update();command(SupportType::Paradrop,40.5f,40.5f);
        const auto unit=faction==Faction::Allies?UnitType::GI:faction==Faction::Soviet?UnitType::Conscript:faction==Faction::Yuri?UnitType::Initiate:UnitType::PLA;
        check(count(unit)==(faction==Faction::Soviet?9:6),"an actual generic airport uses the corresponding 6/9/6 faction payload");
    }
    if(!fresh(2))return 1;sources(2);
    for(int i=0;i<SUPPORT_COUNT;++i)world.players[0].support[i]={7200,true};
    uiTab=1;uiScroll=0;auditHud=true;camZoom=1;camX=-600;camY=700;sim=SimInput{};sim.active=true;
    BeginTextureMode(canvas);drawHUD();EndTextureMode();const auto buttons=supportHudItems;
    bool controls=buttons.size()==4;
    for(const auto& item:buttons)controls &= item.ready && supportPowerIcon(item.type).tex.id!=0;
    check(controls,"the actual YR defense palette exposes all four independent original cameos");
    for(const auto& item:buttons) {
        const auto hash=world.checksum();sim.pos={item.bounds.x+item.bounds.width/2,item.bounds.y+item.bounds.height/2};sim.pressedL=true;
        BeginTextureMode(canvas);drawHUD();EndTextureMode();sim.pressedL=false;
        const bool targeted=targetingParadrop && targetingSupport==item.type;
        sim.pressedR=true;handleInput();sim.pressedR=false;
        check(targeted && !targetingParadrop && world.checksum()==hash,"each actual support button targets its own ability and cancels without consuming a charge");
    }
    world.map.reveal(0,32,32,64);bakeTerrain();shotAtDisplaySize=true;bool rendered=true;
    for(const auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
        SetWindowSize(size.x,size.y);const auto hash=world.checksum();
        const auto file=(std::filesystem::path(outputDir)/("four-support-"+std::to_string(size.x)+".png")).string();
        shotFile=file;render();Image image=LoadImage(file.c_str());rendered &= image.data && image.width==size.x && image.height==size.y && world.checksum()==hash;UnloadImage(image);
    }
    check(rendered,"all four original support controls render at both display sizes without changing simulation");shotAtDisplaySize=false;
    const auto american=std::find_if(buttons.begin(),buttons.end(),[](const auto& item){return item.type==SupportType::AmericanParadrop;});
    if(american!=buttons.end()) {
        sim.pos={american->bounds.x+american->bounds.width/2,american->bounds.y+american->bounds.height/2};sim.pressedL=true;
        BeginTextureMode(canvas);drawHUD();EndTextureMode();sim.pressedL=false;
        int x=0,y=0;worldToScreen(0,81*(TILE_H/2.f),x,y);sim.pos={(float)x,(float)y};sim.pressedL=true;handleInput();sim.pressedL=false;
        check(count(UnitType::GI)==8 && !world.supportReady(0,SupportType::AmericanParadrop) && world.supportReady(0,SupportType::Paradrop)
            && world.supportReady(0,SupportType::SpyPlane),"a real American button and ground click consume only the selected ability");
    } else check(false,"the American support control exists for its ground-click test");
    const auto app=(std::filesystem::path(outputDir)/"used-support-app11.sav").string();
    const auto saved=world.checksum();check(saveGameFile(app.c_str()) && loadGameFile(app.c_str()) && world.checksum()==saved,
        "the actual application save retains a used slot alongside other ready abilities");
    std::ifstream input(app,std::ios::binary);const std::string bytes((std::istreambuf_iterator<char>(input)),{});
    const auto header=bytes.rfind("PPUS");bool rejected=header!=std::string::npos && header+54<=bytes.size();
    for(int field:{8,9,14}) {
        auto corrupt=bytes;if(header!=std::string::npos)corrupt[header+field]=2;
        const auto path=(std::filesystem::path(outputDir)/("invalid-support-"+std::to_string(field)+".sav")).string();
        FILE* file=std::fopen(path.c_str(),"wb");bool wrote=file && std::fwrite(corrupt.data(),1,corrupt.size(),file)==corrupt.size();if(file)std::fclose(file);
        rejected &= wrote && !loadGameFile(path.c_str()) && world.checksum()==saved;
    }
    check(rejected,"corrupt support flags and readiness reject atomically in the real App11 reader");
    World legacy;FILE* file=std::fopen("tests/fixtures/legacy-support-world55.sav","rb");bool compatible=file && legacy.loadGame(file);if(file)std::fclose(file);
    compatible &= !legacy.independentSupportEnabled && !legacy.veterancyProgressEnabled && legacy.nativeProductionRules.version==3;
    World resumed;file=tmpfile();compatible &= file && legacy.saveGame(file);
    if(file){rewind(file);compatible &= resumed.loadGame(file);std::fclose(file);}
    for(int i=0;i<180 && compatible;++i){legacy.update();resumed.update();compatible &= legacy.checksum()==resumed.checksum();}
    check(compatible,"an actual historical World55 keeps its shared support and veteran rules after resave and 180-tick replay");
    if(!fresh(2))return 1;sources(2);
    for(int i=0;i<SUPPORT_COUNT;++i)world.players[0].support[i]={7200,true};
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
    SkirmishAI bot;bot.reset(0);for(int i=0;i<30;++i)bot.update(world);
    check(count(UnitType::GI)==0 && world.supportReady(0,SupportType::Paradrop) && world.supportReady(0,SupportType::AmericanParadrop)
        && !world.supportReady(0,SupportType::SpyPlane) && !world.supportReady(0,SupportType::PsychicReveal),
        "the support AI never obtains an unseen enemy base position from live entities");
    world.map.reveal(0,56,56,6);
    for(int i=0;i<30;++i)bot.update(world);
    check(count(UnitType::GI)==14 && !world.supportReady(0,SupportType::Paradrop) && !world.supportReady(0,SupportType::AmericanParadrop),
        "after observing a real enemy base the AI can use both independently charged drops");
    if(!fresh(2))return 1;sources(2);
    world.players[0].support[2]=world.players[0].support[3]={7200,true};
    g_script.init(&world,0,-1);g_script.onTrigger("support_api");
    check(g_script.enabled() && g_script.onTriggerCond("support_api") && !world.supportReady(0,SupportType::SpyPlane)
        && world.supportReady(0,SupportType::PsychicReveal),"the actual Lua API selects one support power without consuming another");
    g_script.shutdown();
    if(!fresh(2))return 1;sources(2);
    for(int i=0;i<SUPPORT_COUNT;++i)world.players[0].support[i]={7200,true};
    {
        Game receiver;receiver.world=world;receiver.world.map.bldOccRef=&receiver.world.bldOcc;
        receiver.netGame=true;receiver.netPlayer=1;receiver.localPlayer=1;receiver.netSimTick=receiver.world.tick=NET_DELAY;
        NetLink peer;bool linked=false;
        for(int port=18640;port<18656 && !linked;++port) {
            receiver.net.close();peer.close();if(!receiver.net.host(port) || !peer.connectTo("127.0.0.1",port))continue;
            for(int i=0;i<200 && !linked;++i){receiver.net.poll();peer.poll();linked=receiver.net.connected() && peer.connected();
                if(!linked)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        }
        const auto send=[&](int ability){
            NetLink::Writer frame;frame.w((uint32_t)receiver.netSimTick);frame.w((uint16_t)1);frame.w((uint8_t)World::Cmd::Paradrop);frame.w((uint16_t)0);
            frame.w(50.5f);frame.w(10.5f);frame.w((int32_t)ability);frame.w((int32_t)0);frame.w((uint64_t)0);frame.w((uint8_t)0);
            peer.send(NetLink::MsgCmdFrame,frame.b);
            for(int i=0;i<200 && receiver.net.inbox.empty();++i){peer.poll();receiver.net.poll();if(receiver.net.inbox.empty())std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            receiver.netHandleMsgs();
        };
        receiver.world.map.fog[0][10*64+50]=FOG_UNSEEN;if(linked)send(4);
        receiver.localCmds[NET_DELAY]={};if(linked)receiver.netAdvance();
        check(linked && !receiver.netDesync && receiver.world.map.fogAt(0,50,10)!=FOG_UNSEEN
            && !receiver.world.supportReady(0,SupportType::PsychicReveal) && receiver.world.supportReady(0,SupportType::SpyPlane),
            "an actual TCP frame selects the fourth support ability and preserves the other remote player's charge");
        const auto hash=receiver.world.checksum();if(linked)send(5);
        check(linked && receiver.netPeerLeft && receiver.world.checksum()==hash,"an out-of-range support selector rejects a real TCP frame without executing it");
        peer.close();receiver.net.close();
    }
    const auto replaceField=[](std::string text,const std::string& section,const char* key,const char* value){
        const auto start=text.find("["+section+"]"),end=text.find("\n[",start+1),at=text.find(std::string(key)+" = ",start);
        if(start==std::string::npos || at==std::string::npos || (end!=std::string::npos && at>=end))return std::string{};
        text.replace(at,text.find('\n',at)-at,std::string(key)+" = "+value);return text;
    };
    const auto source=world.nativeScenarioSource;bool malformed=true;
    for(const auto& text:{replaceField(source,"NativeProduction.Support.0","RechargeTicks","0"),
            replaceField(source,"NativeProduction.Support.0","IsPowered","2"),
            replaceField(source,"NativeProduction.Support.0","AlliedCount","65"),
            replaceField(source,"NativeProduction.Support.0","AlliedInfantry","MISSING"),
            replaceField(source,"NativeProduction.Support.0","Icon","../outside.png"),
            replaceField(source,"NativeProduction.Type."+std::to_string(type("AMRADR")),"SupportMask","16"),
            replaceField(source,"NativeProduction.Type."+std::to_string(type("AMRADR")),"SpyResetSupport","4")}) {
        World invalid;World::RulesSnapshot snapshot{text,true,true};
        malformed &= !text.empty() && !invalid.init(64,64,711,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,nullptr,{},&snapshot);
    }
    check(malformed,"malformed support timers, payloads, paths and primary weapon links reject before a match starts");
    const auto powered=replaceField(source,"NativeProduction.Support.0","IsPowered","1");World::RulesSnapshot poweredRules{powered,true,true};
    bool power=world.init(64,64,711,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,nullptr,{},&poweredRules);
    if(power) {
        for(auto& cell:world.map.cells)cell=Cell{};world.spawnBuilding(0,BldType::TechAirport,10,15,true,type("CAAIRP"));
        world.players[0].powerSabotage=90;for(int i=0;i<30;++i)world.update();power &= world.players[0].support[0].ticks==0;
        world.players[0].powerSabotage=0;world.update();power &= world.players[0].support[0].ticks==1;
        world.players[0].support[0]={7200,true};world.players[0].powerSabotage=90;
        power &= !world.supportReady(0,SupportType::Paradrop) && !world.orderSupport(0,SupportType::Paradrop,40.5f,40.5f)
            && world.players[0].support[0].ready;
        world.players[0].powerSabotage=0;power &= world.supportReady(0,SupportType::Paradrop);
    }
    check(power,"an explicit powered support rule pauses charging and firing during an outage without deleting readiness");
    sim=SimInput{};auditHud=false;shotFile.clear();std::printf("SOURCE_SUPPORT failures=%d\n",failures);return failures;
}
