#include "game/game.h"
#include <cstdio>
#include <filesystem>

int Game::productionFeedbackRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("PRODUCTION_FEEDBACK %s %s\n",ok?"PASS":"FAIL",name); };
    world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
    world.players[0].country=Country::America;
    world.spawnBuilding(0,BldType::ConYard,10,10,true);world.spawnBuilding(0,BldType::PowerPlant,15,10,true);
    world.spawnBuilding(0,BldType::Barracks,10,20,true);
    world.startUnitProd(0,UnitType::GI);world.holdUnitProd(0,UnitType::GI,true);world.presentationEvents.clear();
    phase=Phase::InGame;localPlayer=0;paused=false;showMenu=false;gameOver=false;clearPlacement();
    netGame=true;pendingCmds.clear();SetTargetFPS(0);auditHud=true;uiTab=2;uiScroll=0;
    sim=SimInput{};sim.active=true;sim.pos={-100,-100};render();
    Rectangle icon{};for(const auto& item:hudItems) if(item.unit && item.type==(int)UnitType::GI) icon=item.bounds;
    msg="pending cancel";const auto hash=world.checksum();sim.pos={icon.x+icon.width/2,icon.y+icon.height/2};
    sim.pressedR=true;render();sim.pressedR=false;
    check(icon.width>0 && pendingCmds.size()==1 && world.checksum()==hash && msg=="pending cancel",
          "sidebar waits for executed cancellation rather than claiming success on submission");
    auto delayed=pendingCmds.front();netGame=false;pendingCmds.clear();
    bool receipts=true,consumption=true,monotonic=true;uint64_t lastSequence=0;bool first=true;
    auto execute=[&](World::Cmd::Type action,int type,int value=0) {
        world.presentationEvents.clear();World::Cmd command;command.type=action;command.a=type;command.b=value;
        world.applyCmd(0,command);ProductionOrderResult result;
        bool receipt=!world.presentationEvents.empty() && world.presentationEvents.back().kind==PresentationKind::ProductionOrder;
        receipts &= receipt;
        if(receipt) {
            const auto& event=world.presentationEvents.back();result=event.production;
            receipts &= event.tick==world.tick && event.player==0;
            monotonic &= first || event.sequence>lastSequence;first=false;lastSequence=event.sequence;
        }
        const auto before=world.checksum();consumePresentationEvents();consumption &= before==world.checksum();
        return result;
    };
    auto canceled=execute(delayed.type,delayed.a);
    check(canceled.accepted && canceled.action==ProductionAction::Cancel && !canceled.queued && canceled.refund==0 &&
          !world.players[0].unitProd[0].active,"executed cancellation reports its actual order and refund");
    auto before=world.checksum();canceled=execute(delayed.type,delayed.a);
    check(!canceled.accepted && canceled.reason==ProductionRejectReason::NoOrder && before==world.checksum() &&
          msg.find(TR(S::MsgProductionNoOrder))!=std::string::npos,"stale cancel reports no order without an invented success or refund");
    auto started=execute(World::Cmd::StartUnitProd,(int)UnitType::GI);
    for(int i=0;i<29;++i) execute(World::Cmd::StartUnitProd,(int)UnitType::GI);
    before=world.checksum();auto full=execute(World::Cmd::StartUnitProd,(int)UnitType::GI);
    check(started.accepted && !started.queued && full.reason==ProductionRejectReason::QueueFull && !full.accepted &&
          world.unitQueuedCount(0,0)==30 && before==world.checksum(),"31st order cannot exceed the 30-item category limit");
    auto pausedResult=execute(World::Cmd::HoldUnitProd,(int)UnitType::GI,1);
    const auto paidBefore=world.players[0].unitProd[0].paid;const auto workBefore=world.players[0].unitProd[0].progress;
    for(int i=0;i<30;++i) world.update();
    check(pausedResult.accepted && pausedResult.action==ProductionAction::Pause &&
          world.players[0].unitProd[0].paid==paidBefore && world.players[0].unitProd[0].progress==workBefore,
          "pause receipt corresponds to stationary work and payment over actual ticks");
    before=world.checksum();auto repeated=execute(World::Cmd::HoldUnitProd,(int)UnitType::GI,1);
    check(repeated.accepted && before==world.checksum(),"repeated pause is idempotent");
    auto resumed=execute(World::Cmd::HoldUnitProd,(int)UnitType::GI,0);
    for(int i=0;i<30;++i) world.update();
    const int activePaid=world.players[0].unitProd[0].paid,activeWork=world.players[0].unitProd[0].progress;
    check(resumed.accepted && resumed.action==ProductionAction::Resume && activePaid>paidBefore && activeWork>workBefore,
          "resume receipt corresponds to actual work and incremental payment");
    const Credits money=world.players[0].money;
    canceled=execute(World::Cmd::CancelUnitProd,(int)UnitType::GI);
    check(canceled.accepted && canceled.queued && canceled.refund==0 && world.players[0].money==money &&
          world.players[0].unitProd[0].paid==activePaid && world.players[0].unitProd[0].progress==activeWork,
          "canceling a queued order preserves the paid current order and reports zero refund");
    while(!world.players[0].unitQueue[0].empty()) execute(World::Cmd::CancelUnitProd,(int)UnitType::GI);
    canceled=execute(World::Cmd::CancelUnitProd,(int)UnitType::GI);
    check(canceled.accepted && !canceled.queued && canceled.refund==activePaid && world.players[0].money==money+activePaid,
          "canceling the current order reports the exact actual refund");
    auto reset=[&](int faction=0) {
        world.init(64,64,20261031,2,0,{(Faction)faction,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        const Country countries[]={Country::America,Country::Russia,Country::China,Country::Yuri};
        world.players[0].country=countries[faction];world.cratesEnabled=false;
        world.spawnBuilding(0,BldType::ConYard,10,10,true);world.spawnBuilding(0,BldType::PowerPlant,15,10,true);
        world.spawnBuilding(0,BldType::Barracks,10,20,true);
        world.presentationEvents.clear();world.map.reveal(0,18,16,20);first=true;sim=SimInput{};sim.active=true;sim.pos={-100,-100};
        clearPlacement();uiScroll=0;uiTab=2;camX=-500;camY=100;camZoom=1;msgTimer=0;
    };
    reset();auto missing=execute(World::Cmd::StartUnitProd,(int)UnitType::Grizzly);
    check(!missing.accepted && missing.reason==ProductionRejectReason::MissingFactory,
          "vehicle order explains missing factory instead of silently failing");
    missing=execute(World::Cmd::StartUnitProd,(int)UnitType::Tanya);
    check(!missing.accepted && missing.reason==ProductionRejectReason::MissingPrerequisite,
          "hero order distinguishes missing prerequisite from a missing barracks");
    world.spawnBuilding(0,BldType::BattleLab,22,10,true);
    EID transport=world.spawnUnit(0,UnitType::IFV,25.5f,20.5f);
    world.ents[transport].cargo.push_back({UnitType::Tanya,unitDef(UnitType::Tanya).hp,0,0,0});
    auto unique=execute(World::Cmd::StartUnitProd,(int)UnitType::Tanya);
    check(!unique.accepted && unique.reason==ProductionRejectReason::UniqueLimit,
          "a hero in transport cargo still counts toward its unique limit");
    world.kill(transport,false);execute(World::Cmd::StartUnitProd,(int)UnitType::GI);
    auto queuedHero=execute(World::Cmd::StartUnitProd,(int)UnitType::Tanya);
    unique=execute(World::Cmd::StartUnitProd,(int)UnitType::Tanya);
    check(queuedHero.accepted && queuedHero.queued && !unique.accepted && unique.reason==ProductionRejectReason::UniqueLimit,
          "a hero already queued behind another unit cannot be queued twice");
    reset();world.spawnBuilding(0,BldType::BattleLab,22,10,true);world.spawnBuilding(0,BldType::OrePurifier,30,10,true);
    before=world.checksum();unique=execute(World::Cmd::StartBldProd,(int)BldType::OrePurifier);
    check(!unique.accepted && unique.reason==ProductionRejectReason::UniqueLimit && before==world.checksum(),
          "an existing unique building cannot enter a second construction order");
    reset();world.players[0].money=0;
    auto freeStart=execute(World::Cmd::StartUnitProd,(int)UnitType::GI);
    for(int i=0;i<30;++i) world.update();
    check(freeStart.accepted && world.players[0].unitProd[0].active && world.players[0].unitProd[0].paid==0 &&
          world.players[0].unitProd[0].progress==0,"insufficient money allows an order but never unpaid progress");
    reset();execute(World::Cmd::StartBldProd,(int)BldType::PowerPlant);
    before=world.checksum();auto busy=execute(World::Cmd::StartBldProd,(int)BldType::OreRefinery);
    check(!busy.accepted && busy.reason==ProductionRejectReason::Busy && before==world.checksum(),
          "busy construction category preserves its existing order");
    auto defense=execute(World::Cmd::StartBldProd,(int)BldType::Wall);
    check(defense.accepted && world.players[0].bldProd.active && world.players[0].defProd.active,
          "defense production remains independent of the construction category");
    const auto bldProgress=world.players[0].bldProd.progress;
    execute(World::Cmd::HoldBldProd,(int)BldType::Wall,1);
    for(int i=0;i<30;++i) world.update();
    check(world.players[0].defProd.held && world.players[0].defProd.progress==0 && world.players[0].bldProd.progress>bldProgress,
          "pausing defense does not pause ordinary construction");
    for(int i=0;i<6000 && !world.players[0].bldProd.ready;++i) world.update();
    before=world.checksum();auto ready=execute(World::Cmd::HoldBldProd,(int)BldType::PowerPlant,1);
    check(!ready.accepted && ready.reason==ProductionRejectReason::Ready && before==world.checksum(),
          "completed construction rejects pause while retaining the paid ready order");
    before=world.checksum();auto invalid=execute(World::Cmd::CancelBldProd,-1);
    check(!invalid.accepted && invalid.reason==ProductionRejectReason::InvalidType && before==world.checksum(),
          "malformed cancellation type cannot cancel an unrelated building category");
    const int readyPaid=world.players[0].bldProd.paid;
    canceled=execute(World::Cmd::CancelBldProd,(int)BldType::COUNT);
    check(canceled.accepted && canceled.type==(int)BldType::PowerPlant && canceled.refund==readyPaid && world.players[0].defProd.active,
          "explicit historical COUNT cancellation alias chooses the active construction category");
    reset();EID factory=world.spawnBuilding(0,BldType::WarFactory,25,20,true);
    world.spawnBuilding(0,BldType::OreRefinery,30,10,true);
    const auto submitted=world.productionStartRejection(0,true,(int)UnitType::Grizzly);
    world.kill(factory,false);before=world.checksum();missing=execute(World::Cmd::StartUnitProd,(int)UnitType::Grizzly);
    check(submitted==ProductionRejectReason::None && !missing.accepted && missing.reason==ProductionRejectReason::MissingFactory &&
          before==world.checksum(),"factory lost after local prediction produces an actual rejection without creating an order");
    reset();world.skirmishMode=SkirmishMode::MeatGrinder;
    auto denied=execute(World::Cmd::StartUnitProd,(int)UnitType::Intruder);
    check(!denied.accepted && denied.reason==ProductionRejectReason::Unavailable,"mode prohibition is reported independently of facilities");
    const UnitType infantry[]={UnitType::GI,UnitType::Conscript,UnitType::PLA,UnitType::Initiate};
    for(int faction=0;faction<4;++faction) {
        reset(faction);auto normal=execute(World::Cmd::StartUnitProd,(int)infantry[faction]);
        const Credits startingMoney=world.players[0].money;
        check(normal.accepted && world.players[0].unitProd[0].active && world.players[0].money==startingMoney,
              TextFormat("faction=%d starts its legal infantry with no up-front charge",faction));
    }
    const int languageBefore=g_lang;bool visual=true;
    auto hoverGI=[&]() {
        render();Rectangle r{};
        for(const auto& item:hudItems) if(item.unit && item.type==(int)UnitType::GI) r=item.bounds;
        sim.pos={r.x+r.width/2,r.y+r.height-3};render();return r.width>0;
    };
    auto capture=[&](const char* scenario) {
        shotAtDisplaySize=true;const auto checksum=world.checksum();
        for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();
            for(size_t line=0;line<hudTip.lines.size();++line)
                visual &= MeasureTextEx(font,hudTip.lines[line].c_str(),(float)hudTip.lineSizes[line],0).x<=hudTip.bounds.width-18+1;
            visual &= hudTip.bounds.x>=0 && hudTip.bounds.y>=0 && hudTip.bounds.x+hudTip.bounds.width<=SCREEN_W &&
                hudTip.bounds.y+hudTip.bounds.height<=SCREEN_H-BOTTOM_BAR_H;
            shotFile=(std::filesystem::path(outputDir)/(std::string(scenario)+"_lang"+std::to_string(g_lang)+"_"+
                std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
        }
        visual &= checksum==world.checksum();
    };
    for(int language=0;language<2;++language) {
        g_lang=language;reset();bakeTerrain();
        for(int i=0;i<30;++i) execute(World::Cmd::StartUnitProd,(int)UnitType::GI);
        visual &= hoverGI() && hudTip.reason==TR(S::MsgProductionFull);
        before=world.checksum();sim.pressedL=true;render();sim.pressedL=false;
        visual &= !world.presentationEvents.empty() && world.presentationEvents.back().kind==PresentationKind::ProductionOrder &&
            world.presentationEvents.back().production.reason==ProductionRejectReason::QueueFull && before==world.checksum();
        consumePresentationEvents();visual &= msg.find(TR(S::MsgProductionFull))!=std::string::npos;capture("full");
        sim.pressedR=true;render();sim.pressedR=false;consumePresentationEvents();render();
        visual &= world.players[0].unitProd[0].held && hudTip.reason==TR(S::TipHeld) && msg.find(TR(S::MsgProductionPaused))!=std::string::npos;
        capture("paused");
        reset();execute(World::Cmd::StartUnitProd,(int)UnitType::GI);
        for(int i=0;i<30;++i) world.update();
        hoverGI();sim.pressedR=true;render();sim.pressedR=false;consumePresentationEvents();
        sim.pressedR=true;render();sim.pressedR=false;consumePresentationEvents();
        visual &= !world.players[0].unitProd[0].active && msg.find("$")!=std::string::npos;capture("refund");
        reset();execute(World::Cmd::StartUnitProd,(int)UnitType::GI);EID barracks=INVALID_EID;
        for(EID id=0;id<(EID)world.ents.size();++id) if(world.ents[id].alive && world.ents[id].isBuilding &&
            world.ents[id].btype==BldType::Barracks) barracks=id;
        world.kill(barracks,false);visual &= hoverGI() && hudTip.reason==TextFormat(TR(S::TipRequireFmt),bldName(BldType::Barracks));
        sim.pressedL=true;render();sim.pressedL=false;consumePresentationEvents();
        visual &= msg.find(TR(S::MsgProductionFactory))!=std::string::npos;capture("missing_factory");
        auto& completed=world.players[0].unitProd[0];completed.ready=true;
        completed.progress=unitDef(UnitType::GI).buildTime;completed.workRemainder=0;completed.paid=completed.totalCost;
        render();visual &= hudTip.reason==TextFormat(TR(S::TipRequireFmt),bldName(BldType::Barracks));
        capture("ready_missing_factory");
    }
    g_lang=languageBefore;check(visual,"actual sidebar clicks, bilingual feedback and tooltip bounds agree across both display sizes");
    reset();execute(World::Cmd::StartBldProd,(int)BldType::PowerPlant);world.players[0].money=0;uiTab=0;render();
    for(const auto& item:hudItems) if(!item.unit && item.type==(int)BldType::PowerPlant)
        sim.pos={item.bounds.x+item.bounds.width/2,item.bounds.y+item.bounds.height-3};
    render();check(hudTip.reason==TR(S::TipNoMoney),"active construction tooltip retains its funding status instead of a redundant queue-busy warning");
    reset();FILE* file=tmpfile();World peer;bool replay=file && world.saveGame(file);
    if(replay) { rewind(file);replay=peer.loadGame(file); }if(file) fclose(file);
    for(int tick=0;tick<180 && replay;++tick) {
        World::Cmd command;command.a=(int)UnitType::GI;
        if(tick==0) command.type=World::Cmd::StartUnitProd;
        if(tick==30) { command.type=World::Cmd::HoldUnitProd;command.b=1; }
        if(tick==60) command.type=World::Cmd::HoldUnitProd;
        if(tick==90) command.type=World::Cmd::CancelUnitProd;
        if(command.type!=World::Cmd::None) { world.applyCmd(0,command);peer.applyCmd(0,command); }
        consumePresentationEvents();world.update();peer.update();replay=world.checksum()==peer.checksum();
    }
    check(replay,"consumed and unconsumed production feedback keep saved-peer per-tick simulation identical");
    check(receipts && consumption && monotonic,"production receipts carry ordered tick metadata and consumers never mutate simulation");
    netGame=false;pendingCmds.clear();sim=SimInput{};
    std::printf("PRODUCTION_FEEDBACK failures=%d\n",failures);return failures;
}
