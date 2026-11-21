#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include "scoped_user_file.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <thread>

int GameRegression::rulesSelectionRegression(const char* outputDir){
    if(contentResolve("assets/rules/profiles/ra2.ini").empty() || contentResolve("assets/rules/profiles/yr.ini").empty())return 77;
    std::filesystem::create_directories(outputDir);g_script.shutdown();SetTargetFPS(0);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("RULES_SELECTION %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    check(cfgRules==RulesChoice::RA2 && cfgCountry==(int)Country::America,
        "normal startup defaults to classic RA2 and an original country");
    const auto click=[&](float x,float y){sim={};sim.active=true;sim.pos=menuCanvasFromUi(x,y);sim.pressedL=true;render();sim.pressedL=false;render();};
    const auto picture=[&](const std::string& name){bool ok=true;shotAtDisplaySize=true;
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){SetWindowSize(size.x,size.y);const auto before=world.checksum();
            const auto file=(std::filesystem::path(outputDir)/(name+"-"+std::to_string(size.x)+".png")).string();shotFile=file;render();Image image=LoadImage(file.c_str());
            ok &= image.data && image.width==size.x && image.height==size.y && before==world.checksum();UnloadImage(image);}
        shotAtDisplaySize=false;return ok;};
    selectRulesChoice(RulesChoice::Fusion);cfgCountry=(int)Country::China;
    cfgAI=1;cfgMapSize=64;cfgReferenceMap=false;cfgGameMode=0;phase=Phase::Setup;sim={};sim.active=true;sim.pos={-100,-100};render();
    click(360,400);click(360,442);
    check(cfgRules==RulesChoice::RA2 && cfgCountry==(int)Country::America,"the real setup dropdown selects RA2 and replaces the unavailable Chinese country");
    const auto choices=selectableCountries();check(choices.size()==10 && std::find(choices.begin(),choices.end(),(int)Country::China)==choices.end()
        && std::find(choices.begin(),choices.end(),(int)Country::Yuri)==choices.end(),"RA2 offers its nine countries plus random, with no fusion or Yuri country");
    const int lang=g_lang;bool pictures=true;
    for(auto choice:{RulesChoice::Fusion,RulesChoice::RA2,RulesChoice::YR}){
        selectRulesChoice(choice);cfgCountry=(int)Country::COUNT;aiCountry[0]=(int)Country::COUNT;cfgAI=1;
        cfgMoney=20000;cfgMapSize=64;cfgMapType=2;cfgGameMode=0;cfgCrates=false;cfgShortGame=true;cfgSuperweapons=false;cfgMcvRepacks=true;
        aiDiff[0]=2;aiPersonality[0]=1;aiColor[0]=3;phase=Phase::Setup;
        for(int language:{0,1}){g_lang=language;pictures &= picture("setup-"+std::to_string((int)choice)+"-"+std::to_string(language));}g_lang=lang;
        const auto side=menuShellSide(),mon=menuShellMonitor();click(side.x+side.width*.5f,mon.y+mon.height+51.f);
        bool source=choice!=RulesChoice::Fusion;check(phase==Phase::InGame && world.usesRulesProfile()==source && world.players.size()==2
            && (!source || world.rulesCountryAllowed(world.players[0].country)),"the normal Start button installs the selected rules before creating both starting armies");
        if(phase!=Phase::InGame)continue;
        bool identities=true;for(const auto& e:world.ents)if(e.alive && e.player>=0 && !e.isBuilding)identities &= source==(bool)e.nativeIdentity.definition;
        check(identities,"every ordinary starting unit uses the chosen edition rather than fusion aliases");
        const auto initial=world.checksum();const auto savedSetup=*matchSetup;const auto savedRules=world.nativeScenarioSource;
        for(int tick=0;tick<25;++tick)world.update();
        const auto checksum=world.checksum();const auto file=(std::filesystem::path(outputDir)/("choice-"+std::to_string((int)choice)+".sav")).string();
        bool loaded=saveGameFile(file.c_str());cfgRules=RulesChoice::Fusion;cfgCountry=(int)Country::China;cfgAI=7;cfgMoney=5000;cfgMapType=0;aiDiff[0]=0;
        loaded &= loadGameFile(file.c_str());
        check(loaded && world.checksum()==checksum && cfgRules==choice && cfgCountry==savedSetup.country && cfgAI==1 && cfgMoney==20000
            && cfgMapType==2 && aiDiff[0]==2 && cfgShortGame && !cfgSuperweapons,"the real application snapshot restores the edition, random choices, AI difficulty, money, map and match options");
        restartCurrentMatch();check(phase==Phase::InGame && world.tick==0 && world.checksum()==initial && world.nativeScenarioSource==savedRules,
            "Restart recreates the same map, random countries and embedded source mechanics from the saved setup");
    }
    check(pictures,"all three rule selections remain readable in both languages and both display sizes without changing simulation");
    phase=Phase::Setup;cfgAI=7;cfgMapSize=128;previewDirty=true;
    check(picture("setup-eight-players"),"the eight-player setup retains room for rules, map controls and match options");
    cfgAI=1;cfgMapSize=64;
    selectRulesChoice(RulesChoice::YR);cfgCountry=(int)Country::Yuri;aiCountry[0]=(int)Country::Russia;newGame(314159);
    bool yuri=false;for(const auto& e:world.ents)if(e.alive && e.player==0 && e.utype==UnitType::MCV)yuri=e.nativeIdentity.definition && e.nativeIdentity.definition->id=="PCV";
    check(yuri,"a selected Yuri country creates the real Yuri MCV at startup");
    const auto held=world.checksum();const auto profile=contentResolve("assets/rules/profiles/ra2.ini");
    {
        ScopedUserFile guard(std::filesystem::u8path(profile));bool ok=guard.ready();
        if(ok){std::filesystem::remove(std::filesystem::u8path(profile));cfgRules=RulesChoice::RA2;cfgCountry=(int)Country::America;newGame(99);ok=phase==Phase::Setup && world.checksum()==held;}
        check(ok && guard.restore(),"a missing selected rules package refuses Start and preserves the current world");
    }
    const auto art=contentResolve("assets/rules/profiles/ra2-local-base/actors/asw_d0.png");
    {
        ScopedUserFile guard(std::filesystem::u8path(art));bool ok=guard.ready() && !art.empty();
        if(ok){std::filesystem::remove(std::filesystem::u8path(art));newGame(100);ok=phase==Phase::Setup && world.checksum()==held;}
        check(ok && guard.restore(),"a missing source actor image refuses Start without falling back to fusion mechanics");
    }
    const auto old=contentResolve("review_out/project-review-20261115/legacy-world51-app10-scene20.sav");
    if(!old.empty())check(loadGameFile(old.c_str()) && world.usesRulesProfile() && cfgRules==RulesChoice::RA2 && matchSetup.has_value(),
        "an actual App10 source save still loads and derives a matching rule selection");
    {
        const auto file=(std::filesystem::path(outputDir)/"choice-1.sav").string();bool ok=loadGameFile(file.c_str());
        std::ifstream input(file,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{}),section;
        FILE* encoded=tmpfile();ok &= encoded && writeMatchSetup(encoded,matchSetup);
        if(ok){const auto size=ftell(encoded);section.resize((size_t)size);rewind(encoded);ok=fread(section.data(),1,section.size(),encoded)==section.size();}
        if(encoded)fclose(encoded);const auto found=std::search(bytes.begin(),bytes.end(),section.begin(),section.end());ok &= !section.empty() && found!=bytes.end();
        const auto before=world.checksum();const auto choice=cfgRules;const auto money=cfgMoney;
        if(ok){bytes[(size_t)(found-bytes.begin())+5]=255;const auto bad=(std::filesystem::path(outputDir)/"invalid-setup.sav").string();
            std::ofstream output(bad,std::ios::binary);output.write((const char*)bytes.data(),bytes.size());output.close();
            ok=!loadGameFile(bad.c_str()) && world.checksum()==before && cfgRules==choice && cfgMoney==money;
        }
        check(ok,"an invalid persisted rule selection rejects the whole application snapshot without changing the live world or setup");
        restartCurrentMatch();const auto initial=world.checksum();const auto original=world.nativeScenarioSource;const auto digest=selectedRulesFingerprint();
        ScopedUserFile guard(std::filesystem::u8path(profile));ok=guard.ready();
        if(ok){auto changed=original;
            if(world.nativeWeaponCatalog) {
                const auto field=[&](const std::string& section,const std::string& key) {
                    const auto start=changed.find("["+section+"]"),limit=changed.find("\n[",start==std::string::npos?0:start+1);
                    const auto at=changed.find("\n"+key+" = ",start);
                    if(start==std::string::npos || at==std::string::npos || (limit!=std::string::npos && at>=limit))return false;
                    const auto value=at+key.size()+4;changed.replace(value,changed.find('\n',value)-value,"137");return true;
                };
                const NativeWeaponData* m60=nullptr;for(const auto& w:world.nativeWeaponCatalog->weapons)if(w.id=="M60")m60=&w;
                ok=m60 && field("NativeWeapon."+std::to_string(m60?m60->index:0),"Damage");int mirrors=0;
                for(int i=0;i<(int)world.nativeTypes.size();++i)for(int slot=0;slot<12;++slot) {
                    const auto& gun=world.nativeTypes[i]->guns.slots[slot];
                    if(gun.present && gun.id=="M60"){++mirrors;ok &= field("Type."+std::to_string(i),"Gun"+std::to_string(slot)+"Damage");}
                }
                ok &= mirrors>0;
            } else {
                const std::string key="Gun0Damage = 15";const auto at=changed.find(key);ok=at!=std::string::npos;
                if(ok)changed.replace(at,key.size(),"Gun0Damage = 137");
            }
            if(ok){std::ofstream f(profile,std::ios::binary);f<<changed;f.close();
                const auto altered=selectedRulesFingerprint();restartCurrentMatch();ok=digest && altered && *digest!=*altered && world.checksum()==initial && world.nativeScenarioSource==original;
            }
        }
        check(ok && guard.restore(),"Restart reuses the saved source snapshot even when the installed source package changes");
        refreshRulesChoice(RulesChoice::RA2,true);
    }
    for(auto choice:{RulesChoice::Fusion,RulesChoice::RA2,RulesChoice::YR}){
        selectRulesChoice(choice);phase=Phase::NetLobby;lobbyState=0;sim={};sim.active=true;sim.pos={-100,-100};
        for(int language:{0,1}){g_lang=language;pictures &= picture("lobby-"+std::to_string((int)choice)+"-"+std::to_string(language));}g_lang=lang;
    }
    check(pictures,"the real lobby shows the rule selection and allowed country controls in both languages and display sizes");
    lobbyState=3;lobbyRole=0;lobbyError=NetLink::RejectReason::Profile;
    for(int language:{0,1}){g_lang=language;pictures &= picture("lobby-different-rules-"+std::to_string(language));}g_lang=lang;
    check(pictures,"the actual rule-selection mismatch prompt remains readable in both languages at both sizes");
    int port=NET_PORT+250;
    const auto connect=[&](Game& host,Game& client){bool listening=false;for(int i=0;i<100 && !listening;++i)listening=host.net.host(++port);
        if(!listening || !client.net.connectTo("127.0.0.1",port))return false;
        for(int i=0;i<1000 && (!host.net.connected() || !client.net.connected());++i){host.net.poll();client.net.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        return host.net.connected() && client.net.connected();};
    for(auto choice:{RulesChoice::RA2,RulesChoice::YR}){
        Game host,client;host.selectRulesChoice(choice);client.selectRulesChoice(choice);host.cfgCountry=client.cfgCountry=(int)Country::COUNT;
        host.cfgMapSize=64;host.cfgCrates=false;host.cfgColor=0;client.cfgColor=1;host.lobbyRole=0;client.lobbyRole=1;
        host.phase=client.phase=Phase::NetLobby;host.lobbyState=client.lobbyState=1;
        bool ok=connect(host,client);
        for(int i=0;i<1000 && ok && host.lobbyState!=2;++i){host.netHandleMsgs();client.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        ok &= host.lobbyState==2 && host.netStartHandshake(20261116);
        for(int i=0;i<1000 && ok && (!host.netGame || !client.netGame);++i){host.netHandleMsgs();client.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(ok && host.netGame && client.netGame && host.world.usesRulesProfile() && host.world.rulesProfileId==client.world.rulesProfileId
            && host.world.checksum()==client.world.checksum() && host.world.players[0].country!=Country::China && host.world.players[1].country!=Country::China,
            "a real TCP source-edition handshake resolves allowed random countries and creates identical source worlds");
        if(!host.netGame || !client.netGame)continue;
        for(Game* peer:{&host,&client}){World::Cmd deploy{};deploy.type=World::Cmd::Deploy;
            for(EID id=0;id<(EID)peer->world.ents.size();++id)if(peer->world.valid(id) && peer->world.ents[id].player==peer->localPlayer && peer->world.ents[id].utype==UnitType::MCV)deploy.ids.push_back(id);
            peer->issueCmd(deploy);
        }
        for(int i=0;i<4000 && (host.netSimTick<240 || client.netSimTick<240) && !host.netDesync && !client.netDesync;++i){
            host.netHandleMsgs();client.netHandleMsgs();if(host.netSimTick<240)host.netAdvance();if(client.netSimTick<240)client.netAdvance();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(host.netSimTick==240 && client.netSimTick==240 && !host.netDesync && !client.netDesync && host.world.checksum()==client.world.checksum(),
            "actual queued deployment commands and 240 lockstep ticks remain identical under each source edition");
    }
    sim={};g_lang=lang;std::printf("RULES_SELECTION failures=%d\n",failures);return failures;
}
