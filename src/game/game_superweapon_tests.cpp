#include "game/game.h"
#include <set>
#include <cstdio>
#include <filesystem>
#include <limits>

int Game::superweaponRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("SUPERWEAPON %s %s\n",ok?"PASS":"FAIL",name); };
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        world.superweaponsEnabled=true;
        for(auto& c:world.map.cells) c=Cell{};
        world.spawnBuilding(0,BldType::ChronoSphere,20,10,true);world.spawnBuilding(0,BldType::NukeSilo,5,5,true);
        for(auto& e:world.ents) e.constructAnim=0;
        world.players[0].swReady[(int)SWType::ChronoShift]=world.players[0].swReady[(int)SWType::Nuke]=true;
        world.players[0].swCharge[(int)SWType::ChronoShift]=swDef(SWType::ChronoShift).chargeTime;
        world.players[0].swCharge[(int)SWType::Nuke]=swDef(SWType::Nuke).chargeTime;
        world.presentationEvents.clear();
    };
    reset();const auto before=world.checksum();
    check(!world.launchSW(0,SWType::Nuke,-1,40) && world.checksum()==before,"off-map launch preserves full charge and world state");
    reset();EID unit=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    for(int y=37;y<=43;++y) for(int x=37;x<=43;++x) world.map.at(x,y).terrain=Terrain::Water;
    world.ents[unit].state=UState::Moving;world.ents[unit].path={{11,10},{12,10}};world.ents[unit].pathIdx=0;
    const auto blocked=world.checksum();
    check(!world.launchChronoShift(0,{unit},40.5f,40.5f) && world.checksum()==blocked,"fully blocked Chronosphere cast preserves charge, order and path");
    reset();std::vector<EID> group;
    for(int i=0;i<20;++i) group.push_back(world.spawnUnit(0,UnitType::Grizzly,10.5f+i%5,20.5f+i/5));
    bool cast=world.launchChronoShift(0,group,40.5f,40.5f);std::set<int> cells;bool indexed=true;
    for(EID id:group) { const auto& e=world.ents[id];cells.insert((int)e.y*64+(int)e.x);indexed &= world.unitAtCell((int)e.x,(int)e.y)==id; }
    check(cast && cells.size()==group.size() && indexed,"same-tick group transfer reserves distinct cells and updates occupation indexes");
    reset();world.map.at(10,10).terrain=Terrain::Water;
    for(int y=37;y<=43;++y) for(int x=37;x<=43;++x) world.map.at(x,y).terrain=Terrain::Water;
    EID ship=world.spawnUnit(0,UnitType::Typhoon,10.5f,10.5f);
    cast=world.launchChronoShift(0,{ship},40.5f,40.5f);
    check(cast && world.map.at((int)world.ents[ship].x,(int)world.ents[ship].y).terrain==Terrain::Water && world.ents[ship].x>30,
        "naval Chronosphere transfer lands on legal water");
    reset();EID selected=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    EID unrelated=world.spawnUnit(0,UnitType::Grizzly,23.5f,12.5f);world.kill(selected,false);
    World::Cmd command{};command.type=World::Cmd::LaunchSW;command.a=(int)SWType::ChronoShift;command.ids={selected};command.x=40.5f;command.y=40.5f;
    const auto stale=world.checksum();world.applyCmd(0,command);
    check(world.checksum()==stale && world.ents[unrelated].x==23.5f,"stale source selection cannot fall back to unrelated vehicles near the sphere");
    reset();unit=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);world.ents[unit].tpSick=30;
    const auto frozen=world.checksum();
    check(!world.launchChronoShift(0,{unit},40.5f,40.5f) && world.checksum()==frozen,"immobilized source does not consume charge");
    reset();const auto safe=world.checksum();
    check(!world.swAvailable(0,SWType::COUNT) && !world.launchSW(-1,SWType::Nuke,20,20)
        && !world.launchSW(0,SWType::COUNT,20,20) && !world.launchSW(0,SWType::Nuke,std::numeric_limits<float>::quiet_NaN(),20)
        && world.checksum()==safe,"invalid player, type and non-finite coordinates reject before indexing state");
    reset();unit=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);int transferred=0;
    check(world.launchChronoShift(0,{unit,unit},40.5f,40.5f,&transferred) && transferred==1,
        "duplicate source IDs transfer and count a vehicle only once");
    World restored;bool replay=false;
    if(FILE* file=tmpfile()) { replay=world.saveGame(file);rewind(file);replay=replay && restored.loadGame(file);fclose(file); }
    for(int tick=0;tick<180 && replay;++tick) { world.update();restored.update();replay=world.checksum()==restored.checksum(); }
    check(replay,"current snapshot resumes transferred positions, sickness and occupation deterministically");
    reset();world.map.at(10,10).terrain=Terrain::Water;ship=world.spawnUnit(0,UnitType::Typhoon,10.5f,10.5f);
    const auto dry=world.checksum();check(!world.launchChronoShift(0,{ship},40.5f,40.5f) && world.checksum()==dry,
        "naval source cannot land on dry ground or spend charge on it");
    reset();EID first=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f),second=world.spawnUnit(0,UnitType::Grizzly,12.5f,10.5f);
    world.ents[second].state=UState::Moving;world.ents[second].path={{13,10}};world.ents[second].pathIdx=0;
    for(int y=37;y<=43;++y) for(int x=37;x<=43;++x) world.map.at(x,y).terrain=Terrain::Water;
    world.map.at(40,40).terrain=Terrain::Clear;transferred=0;
    check(world.launchChronoShift(0,{second,first},40.5f,40.5f,&transferred) && transferred==1 && world.ents[first].x==40.5f
        && world.ents[second].x==12.5f && world.ents[second].state==UState::Moving && world.ents[second].path.size()==1,
        "partial destination capacity uses stable ID priority and preserves untransferred orders");
    reset();unit=world.spawnUnit(0,UnitType::TerrorDrone,10.5f,10.5f);world.ents[unit].parasiting=true;world.unitOccSync(unit);
    const auto attached=world.checksum();check(!world.chronoSourceEligible(0,unit) && !world.launchChronoShift(0,{unit},40.5f,40.5f) && world.checksum()==attached,
        "attached parasites cannot be selected as independent Chronosphere sources");
    bool glyphs=true;
    for(int lang=0;lang<2;++lang) {
        g_lang=lang;
        for(S text:{S::MsgSWTransferredFmt,S::MsgSWExecuted,S::MsgSWUnavailable,S::MsgSWNotReady,S::MsgSWNoSource,S::MsgSWBlocked,S::MsgSWTargetInvalid,S::MsgSWInvalid,S::MsgSWChooseDestination}) {
            int count=0;int* points=LoadCodepoints(TR(text),&count);
            for(int i=0;i<count;++i) glyphs &= font.glyphs[GetGlyphIndex(font,points[i])].value==points[i];
            UnloadCodepoints(points);
        }
    }
    check(glyphs,"every new bilingual superweapon message has real preloaded font glyphs");g_lang=0;
    SetTargetFPS(0);localPlayer=0;phase=Phase::InGame;showMenu=gameOver=paused=netGame=false;
    ais.clear();campaignMission=-1;missionTriggers.clear();objectiveText.clear();sel.clear();selBuilding=INVALID_EID;
    sim=SimInput{};sim.active=true;sim.pos={600,350};camZoom=1;camX=-600;camY=1000;clearPlacement();
    // Actual execution events, rather than forged UI states, drive all screenshot captions.
    for(int scenario=0;scenario<6;++scenario) {
        reset();unit=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
        command=World::Cmd{};command.type=World::Cmd::LaunchSW;command.a=(int)SWType::ChronoShift;command.ids={unit};command.x=40.5f;command.y=40.5f;
        if(scenario==1) for(int y=37;y<=43;++y) for(int x=37;x<=43;++x) world.map.at(x,y).terrain=Terrain::Water;
        if(scenario==2) world.kill(unit,false);
        if(scenario==3) world.players[0].swReady[(int)SWType::ChronoShift]=false;
        if(scenario==4) world.superweaponsEnabled=false;
        if(scenario==5) { command.a=(int)SWType::Nuke;command.ids.clear(); }
        world.presentationEvents.clear();world.applyCmd(0,command);
        const PresentationEvent* receipt=nullptr;
        for(const auto& event:world.presentationEvents) if(event.kind==PresentationKind::SuperweaponOrder) receipt=&event;
        check(receipt && receipt->superweapon.accepted==(scenario==0 || scenario==5),"command receipt reports actual superweapon outcome");
        const auto result=receipt?receipt->superweapon:SuperweaponOrderResult{};
        const auto hash=world.checksum();consumePresentationEvents();check(hash==world.checksum(),"consuming superweapon receipts never changes simulation");
        std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_VISIBLE);fogMaskTick=-1;bakeTerrain();
        for(int lang=0;lang<2;++lang) for(int width:{1280,1920}) {
            g_lang=lang;message(superweaponFeedbackText(result));moveFeedbackTimer=1;
            SetWindowSize(width,width==1280?720:1080);shotAtDisplaySize=true;
            const auto path=(std::filesystem::path(outputDir)/(std::string("outcome_")+std::to_string(scenario)+"_"+std::to_string(lang)+"_"+std::to_string(width)+".png")).string();
            const auto beforeDraw=world.checksum();shotFile=path;render();Image image=LoadImage(path.c_str());
            check(image.data && image.width==width && image.height==(width==1280?720:1080) && beforeDraw==world.checksum(),
                "bilingual display-sized superweapon feedback draws without changing simulation");UnloadImage(image);
        }
    }
    reset();targetingSW=SWType::ChronoShift;unit=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);chronoSourceSel={unit};
    sim=SimInput{};sim.active=true;sim.pos={600,350};sim.pressedR=true;
    const auto cancel=world.checksum();handleInput();check(targetingSW==SWType::COUNT && chronoSourceSel.empty() && cancel==world.checksum(),
        "canceling the second Chronosphere stage preserves charge and simulation");
    reset();targetingSW=SWType::Nuke;chronoSourceSel.clear();netGame=true;pendingCmds.clear();message("await execution");
    sim=SimInput{};sim.active=true;camX=-600;camY=1000;int clickX,clickY;worldToScreen(0,81*(TILE_H/2.0f),clickX,clickY);
    sim.pos={(float)clickX,(float)clickY};sim.pressedL=true;const auto queued=world.checksum();handleInput();
    check(pendingCmds.size()==1 && queued==world.checksum() && msg=="await execution",
        "delayed submission never announces a launch before execution");
    world.players[0].swReady[(int)SWType::Nuke]=false;
    if(!pendingCmds.empty()) world.applyCmd(0,pendingCmds.front());
    pendingCmds.clear();netGame=false;const auto rejected=world.checksum();consumePresentationEvents();
    check(rejected==world.checksum() && moveFeedbackRejected && msg!="await execution",
        "execution-time readiness failure produces rejection feedback without consuming charge");
    sim=SimInput{};shotAtDisplaySize=false;g_lang=0;
    std::printf("SUPERWEAPON failures=%d\n",failures);return failures;
}
