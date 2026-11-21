#include "game_regression.h"
#include "core/content.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include <cstdio>
#include <algorithm>
#include <filesystem>

int GameRegression::rulesProfileRegression(const char* outputDir) {
    if(contentResolve("assets/rules/profiles/ra2.ini").empty()) {
        std::printf("RULES_PROFILE SKIP: export the source profiles\n");return 77;
    }
    int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("RULES_PROFILE %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const char* old="review_out/project-review-20261115/legacy-world49-scene17.sav";
    if(FileExists(old)) {
        bool compatible=loadGameFile(old) && world.usesRulesProfile() && world.nativeRocketDefinitions.empty();
        FILE* saved=tmpfile();World copy;
        compatible &= saved && world.saveGame(saved);
        if(compatible){rewind(saved);compatible=copy.loadGame(saved);}if(saved)fclose(saved);
        compatible &= world.checksum()==copy.checksum();
        for(int tick=0;tick<20 && compatible;++tick){world.update();copy.update();compatible &= world.checksum()==copy.checksum();}
        check(compatible,"an actual World49/Scene17 application save retains its embedded original rules across current source upgrades");
    }
    World sample;
    const bool loaded=sample.init(64,64,711,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,"assets/rules/profiles/ra2.ini");
    check(loaded,"the map-independent original rules package parses before ordinary starting units are created");
    if(!loaded)return failures;
    check(sample.usesRulesProfile() && sample.rulesProfileId=="ra2-local-base" && !sample.nativeEvents.active
        && sample.nativeProductionRules.available && sample.nativeMovementRules.available && !sample.map.native.active(),
        "an original rules match retains generated terrain and ordinary match termination while using source combat/production/movement");
    bool typed=true;for(const auto& unit:sample.ents)if(unit.alive && unit.player>=0)typed &= unit.nativeIdentity.definition && unit.nativeIdentity.definition->rulesOnly;
    check(typed,"every original-profile starting actor binds its immutable source definition");
    const EID soldier=sample.spawnUnit(1,UnitType::Conscript,10.5f,10.5f);
    check(sample.valid(soldier) && unitDef(sample.ents[soldier]).armor==Armor::Flak
        && sample.effWeapon(sample.ents[soldier]).damage==15 && sample.effWeapon(sample.ents[soldier]).cooldown==50,
        "ordinary Conscript spawning uses base damage 15, source flak armor and 25 source frames converted to 50 ticks");
    const EID tank=sample.spawnUnit(0,UnitType::Grizzly,12.5f,10.5f);
    check(sample.valid(tank) && sample.effWeapon(sample.ents[tank]).damage==65 && sample.effWeapon(sample.ents[tank]).cooldown==120,
        "ordinary Grizzly spawning shares the source 65-damage / 120-tick gun contract");
    check(sample.isEnemy(0,1) && !sample.rulesCountryAllowed(Country::China) && !sample.rulesCountryAllowed(Country::Yuri),
        "a source profile preserves normal player hostility and admits only its source countries");
    FILE* file=tmpfile();World restored;bool exact=file && sample.saveGame(file);
    if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
    check(exact && restored.rulesProfileId==sample.rulesProfileId && restored.checksum()==sample.checksum(),
        "a World snapshot embeds and restores the exact rules package and actor bindings");
    const auto unchanged=sample.checksum();
    check(!sample.loadRulesProfile("assets/rules/profiles/yr.ini") && unchanged==sample.checksum(),
        "source rules cannot be replaced underneath an existing match");
    World national;
    const bool countries=national.init(64,64,743,1,1,{Faction::Soviet,Faction::Allies},0,nullptr,false,false,
        "assets/rules/profiles/ra2.ini",{Country::Cuba,Country::UK});
    const int sovietEngineer=countries?national.rulesTypeIndex(0,true,(int)UnitType::Engineer):-1;
    const int sovietRefinery=countries?national.rulesTypeIndex(0,false,(int)BldType::OreRefinery):-1;
    check(countries && national.productionCountry(0)=="CONFEDERATION" && national.productionCountry(1)=="BRITISH"
        && sovietEngineer>=0 && national.nativeTypes[sovietEngineer]->id=="SENGINEER"
        && sovietRefinery>=0 && national.nativeTypes[sovietRefinery]->id=="NAREFN",
        "explicit original countries resolve their engineer and refinery aliases despite shared source Owner lists");
    World invalid;
    check(!invalid.init(64,64,743,1,1,{Faction::China,Faction::Soviet},0,nullptr,false,false,
        "assets/rules/profiles/ra2.ini",{Country::China,Country::Russia}),
        "a base RA2 rules package rejects a fusion-only country before creating actors");
    // Exercise the enum API used by the ordinary AI and the same command path
    // used by input. Neither may create a legacy queue in a source world.
    sample.kill(soldier,false);sample.kill(tank,false);
    EID mcv=INVALID_EID;for(EID id=0;id<(EID)sample.ents.size();++id)if(sample.valid(id) && sample.ents[id].player==0 && !sample.ents[id].isBuilding && sample.ents[id].utype==UnitType::MCV){mcv=id;break;}
    const EID yard=sample.orderDeploy(mcv);
    check(sample.valid(yard) && sample.ents[yard].nativeIdentity.definition->id=="GACNST",
        "the ordinary MCV command deploys its original construction yard definition");
    if(sample.valid(yard)) {
        World captured=sample;captured.map.bldOccRef=&captured.bldOcc;captured.mcvRepacks=true;captured.ents[yard].player=1;captured.ents[yard].hp=321;
        const EID packed=captured.orderDeploy(yard);
        const bool original=captured.valid(packed) && captured.ents[packed].nativeIdentity.definition->id=="AMCV";
        const EID rebuilt=original?captured.orderDeploy(packed):INVALID_EID;
        check(original && captured.valid(rebuilt) && captured.ents[rebuilt].nativeIdentity.definition->id=="GACNST" && captured.ents[rebuilt].player==1,
            "captured construction technology keeps its original MCV/yard pair when repacked and redeployed");
        check(captured.valid(rebuilt) && captured.ents[rebuilt].hp==321,
            "captured source construction technology preserves damage through both conversions");
    }
    const auto money=sample.players[0].money;
    World::Cmd build{};build.type=World::Cmd::StartBldProd;build.a=(int)BldType::PowerPlant;
    sample.applyProductionCommand(0,build);bool paid=sample.players[0].bldProd.active && sample.nativeProductionType(sample.players[0].bldProd.typeIdx);
    for(int tick=0;tick<6000 && paid && !sample.players[0].bldProd.ready;++tick)sample.update();
    paid=paid && sample.players[0].bldProd.ready && money-sample.players[0].money==800;
    EID power=INVALID_EID;
    if(paid)for(int y=0;y<sample.map.h && power<0;++y)for(int x=0;x<sample.map.w;++x)
        if(sample.canPlace(BldType::PowerPlant,x,y,0) && sample.placeBuilding(0,BldType::PowerPlant,x,y)){power=sample.bldAt(x,y);break;}
    check(paid && sample.valid(power) && sample.ents[power].nativeIdentity.definition->id=="GAPOWR" && bldDef(sample.ents[power]).power==200,
        "ordinary production input pays the source 800 credits and places the original 200-power Allied plant");
    bool finite=true;
    for(const char* edition:{"ra2","yr"}) {
        const std::string file=std::string("assets/rules/profiles/")+edition+".ini";
        if(contentResolve(file.c_str()).empty()){finite=false;continue;}
        World match;const bool yr=std::string(edition)=="yr";
        const bool ready=match.init(64,64,922,1,yr?2:1,yr?std::vector<Faction>{Faction::Allies,Faction::Soviet,Faction::Yuri}:std::vector<Faction>{Faction::Allies,Faction::Soviet},0,nullptr,false,false,file.c_str());
        check(ready,yr?"the original YR base rules package parses for all three factions":"the original RA2 package starts a normal AI match");
        if(!ready){finite=false;continue;}
        std::vector<SkirmishAI> opponents(match.numPlayers-1);
        for(int i=0;i<(int)opponents.size();++i){opponents[i].reset(i+1);opponents[i].difficulty=AIDiff::Hard;}
        for(int tick=0;tick<18000;++tick){for(auto& ai:opponents)ai.update(match);match.update();}
        for(int player=1;player<match.numPlayers;++player) {
            int buildings=0;bool typed=true;
            for(const auto& e:match.ents)if(e.alive && e.player==player){buildings+=e.isBuilding;typed &= e.nativeIdentity.definition && e.nativeIdentity.definition->rulesOnly;}
            const bool progressing=buildings>=4 && match.producedUnits[player]>0 && match.harvestedIncome[player]>0 && typed;
            std::printf("RULES_PROFILE AI edition=%s player=%d buildings=%d produced=%llu income=%lld typed=%d\n",edition,player,buildings,(unsigned long long)match.producedUnits[player],(long long)match.harvestedIncome[player],typed);
            if(!progressing){const auto& p=match.players[player];std::printf("RULES_PROFILE AI DIAG money=%lld power=%d/%d active=%d type=%d progress=%d ready=%d\n",(long long)p.money,p.powerMade,p.powerUsed,p.bldProd.active,p.bldProd.typeIdx,p.bldProd.progress,p.bldProd.ready);
                for(const auto& e:match.ents)if(e.alive && e.player==player)std::printf("RULES_PROFILE AI ACTOR %s pos=%.2f,%.2f state=%d\n",e.nativeIdentity.definition?e.nativeIdentity.definition->id.c_str():e.isBuilding?bldName(e.btype):unitName(e.utype),e.x,e.y,(int)e.state);}
            check(progressing,"ordinary source AI deploys, builds, harvests and trains through source queues without untyped actors");finite &= progressing;
        }
        FILE* state=tmpfile();World clone;bool saved=state && match.saveGame(state);
        if(saved){rewind(state);saved=clone.loadGame(state);}if(state)fclose(state);
        bool replay=saved && match.checksum()==clone.checksum();
        for(int tick=0;tick<60 && replay;++tick){match.update();clone.update();replay=match.checksum()==clone.checksum();}
        check(replay,"a developed source match resumes active production, movement and combat identically after snapshot restore");finite &= replay;
    }
    check(finite,"both original source editions complete the AI and replay matrix");
    World sea;const bool islands=sea.init(64,64,941,1,1,{Faction::Allies,Faction::Soviet},1,nullptr,false,false,"assets/rules/profiles/ra2.ini");
    EID ship=INVALID_EID;
    if(islands)for(int y=4;y<sea.map.h-4 && ship<0;++y)for(int x=4;x<sea.map.w-10;++x) {
        bool water=true;for(int step=0;step<7;++step)water &= sea.map.surfacePassable(x+step,y,1,0,true) && !sea.bldBlocked(x+step,y);
        if(water){ship=sea.spawnUnit(0,UnitType::Destroyer,x+.5f,y+.5f);sea.orderMove({ship},x+6.5f,y+.5f,false);break;}
    }
    for(int tick=0;tick<10 && sea.valid(ship);++tick)sea.update();
    check(sea.valid(ship) && sea.ents[ship].nativeMovement.started && sea.ents[ship].nativeMovement.targetThrottle==1
        && sea.ents[ship].nativeMovement.heldSpeed>0 && sea.ents[ship].nativeMovement.heldSpeed<=sea.ents[ship].nativeIdentity.definition->movement.maxSpeed,
        "a normal generated-map ship uses Water/Float source speed instead of the Clear/Float zero-cost fallback");
    const EID carrier=sea.spawnUnit(0,UnitType::AircraftCarrier,30.5f,30.5f);bool passengers=sea.valid(carrier) && sea.ents[carrier].cargo.size()==3;
    std::unordered_set<std::string> identities;
    if(passengers)for(const auto& aircraft:sea.ents[carrier].cargo) {
        passengers &= aircraft.nativeIdentity.definition && aircraft.nativeIdentity.definition->id=="HORNET"
            && aircraft.hp==aircraft.nativeIdentity.definition->unit.hp && identities.insert(aircraft.nativeIdentity.object).second;
    }
    check(passengers,"source carrier construction initializes three aircraft with original stats and distinct passenger identities");
    // Use the real renderer, building hit test, sidebar and placement receipt.
    // This prevents the source identity from accidentally selecting the map-
    // specific actor-art path on a normal generated map.
    std::filesystem::create_directories(outputDir);g_script.shutdown();ais.clear();
    world=sample;world.map.bldOccRef=&world.bldOcc;resetMatchInteraction();resetNotifications();
    phase=Phase::InGame;localPlayer=0;campaignMission=-1;gameOver=victory=paused=showMenu=netGame=false;
    sim=SimInput{};sim.active=true;sim.pos={-100,-100};auditHud=true;uiTab=0;uiScroll=0;interpAlpha=1;camZoom=1;
    SetTargetFPS(0);bakeTerrain();const int missing=g_sprites.missingCount();
    if(world.valid(yard)) {
        const auto& actor=world.ents[yard];int px,py;tileToScreen((int)actor.x+1,(int)actor.y+1,px,py);
        camX=px-(SCREEN_W-sidebarW)/2.f;camY=py-SCREEN_H/2.f;clampCameraToMap();render();
        const auto point=bldScreenPos(actor);const auto& art=g_sprites.building(actor.btype,world.players[0].colorId,false,world.players[0].country);
        bool clickable=false;
        for(int y=art.visT;y<=art.visB && !clickable;y+=5)for(int x=art.visL;x<=art.visR;x+=5) {
            const int mx=(int)(point.x-art.ox+x),my=(int)(point.y-art.oy+y);
            if(pickBuilding(mx,my)==yard && pickUnit(mx,my)==INVALID_EID){sim.pos={(float)mx,(float)my};clickable=true;break;}
        }
        sim.pressedL=true;handleInput();sim.pressedL=false;sim.releasedL=true;handleInput();sim.releasedL=false;
        check(clickable && selBuilding==yard,"a generated-map source building renders and accepts the actual selection click");
    }
    const int barracks=world.productionSelector(0,false,(int)BldType::Barracks);
    render();Rectangle cameo{};for(const auto& item:hudItems)if(item.type==barracks)cameo=item.bounds;
    sim.pos={cameo.x+cameo.width/2,cameo.y+cameo.height/2};sim.pressedL=true;render();sim.pressedL=false;sim.pos={-100,-100};
    check(cameo.width>0 && world.players[0].bldProd.active && world.players[0].bldProd.typeIdx==barracks,
        "the real source sidebar starts the paid original barracks order");
    for(int tick=0;tick<6000 && world.players[0].bldProd.active && !world.players[0].bldProd.ready;++tick)world.update();
    Vec2i site{-1,-1};float nearest=1e9f;
    for(int y=4;y<world.map.h-4;++y)for(int x=4;x<world.map.w-4;++x)if(world.canPlaceNativeBuilding(0,barracks,x,y)) {
        const auto& base=world.ents[yard];const float distance=distf((float)x,(float)y,base.x,base.y);
        if(distance<nearest){nearest=distance;site={x,y};}
    }
    bool placement=false;
    if(site.x>=0 && world.players[0].bldProd.ready) {
        const auto& def=world.nativeTypes[barracks-NATIVE_PRODUCTION_BASE]->building;
        int px,py;tileToScreen(site.x+def.w-1,site.y+def.h-1,px,py);
        camX=px-(SCREEN_W-sidebarW)/2.f;camY=py-SCREEN_H/2.f;clampCameraToMap();render();
        sim.pos={cameo.x+cameo.width/2,cameo.y+cameo.height/2};sim.pressedL=true;render();sim.pressedL=false;
        const bool preview=placing && nativePlacementType==barracks;const auto before=world.checksum();
        render();const bool unchanged=before==world.checksum();
        sim.pos={(px-camX)*camZoom,(py+TILE_H/2-heightScreenY(world.map.at(site.x+def.w-1,site.y+def.h-1).height)-camY)*camZoom};
        float wx,wy;int tx,ty;screenToWorld((int)sim.pos.x,(int)sim.pos.y,wx,wy);surfaceAt(wx,wy,tx,ty);
        check(tx==site.x+def.w-1 && ty==site.y+def.h-1,"generated-map elevated tile picking agrees with the displayed placement surface");
        netGame=true;pendingCmds.clear();sim.pressedL=true;handleInput();sim.pressedL=false;
        bool submitted=pendingCmds.size()==1 && pendingCmds.front().a==barracks && pendingCmds.front().x==site.x && pendingCmds.front().y==site.y;
        if(!submitted)std::printf("RULES_PROFILE UI DIAG preview=%d unchanged=%d native=%d site=%d,%d picked=%d,%d queued=%zu selected=%d\n",preview,unchanged,nativePlacementType,site.x,site.y,tx,ty,pendingCmds.size(),selBuilding);
        for(const auto& command:pendingCmds){if(!submitted)std::printf("RULES_PROFILE UI CMD type=%d a=%d xy=%.1f,%.1f\n",(int)command.type,command.a,command.x,command.y);world.applyCmd(0,command);}
        pendingCmds.clear();consumePresentationEvents();netGame=false;sim.pos={-100,-100};
        const EID factory=world.bldAt(site.x,site.y);
        placement=preview && unchanged && submitted && !placing && world.valid(factory) && world.ents[factory].nativeIdentity.definition->id=="GAPILE";
        if(!placement)std::printf("RULES_PROFILE UI RESULT preview=%d unchanged=%d submitted=%d placing=%d factory=%d msg=%s\n",preview,unchanged,submitted,placing,factory,msg.c_str());
    }
    check(placement,"the source building ghost and normal placement click complete the original barracks without renderer mutation");
    if(placement)for(int tick=0;tick<g_gameRules.buildingAnimationTicks;++tick)world.update();
    bool pictures=true;shotAtDisplaySize=true;
    for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
        SetWindowSize(size.x,size.y);const auto before=world.checksum();
        const auto file=(std::filesystem::path(outputDir)/("source-ra2-"+std::to_string(size.x)+".png")).string();shotFile=file;render();
        Image picture=LoadImage(file.c_str());pictures &= picture.data && picture.width==size.x && picture.height==size.y && before==world.checksum();UnloadImage(picture);
    }
    shotAtDisplaySize=false;
    check(pictures && g_sprites.missingCount()==missing,"both source-match display sizes render with ordinary assets and preserve World/RNG");
    const auto appSave=(std::filesystem::path(outputDir)/"source-match.sav").string();const auto before=world.checksum();
    check(saveGameFile(appSave.c_str()) && loadGameFile(appSave.c_str()) && world.usesRulesProfile() && world.checksum()==before,
        "the actual application save restores the complete original rules match and presentation state");
    sim=SimInput{};
    std::printf("RULES_PROFILE failures=%d\n",failures);return failures;
}
