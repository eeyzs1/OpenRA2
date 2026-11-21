#include "game_regression.h"
#include "game/production_work.h"
#include <array>
#include <cstdio>
#include <filesystem>

int GameRegression::factoryStateRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;EID yard=-1;std::array<EID,4> factories{};
    auto check=[&](bool ok,const char* name){failures+=!ok;std::printf("FACTORY_STATE %s %s\n",ok?"PASS":"FAIL",name);};
    const std::array<UnitType,4> types{UnitType::GI,UnitType::Grizzly,UnitType::Intruder,UnitType::Destroyer};
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        world.cratesEnabled=false;for(auto& c:world.map.cells)c=Cell{};
        world.spawnUnit(1,UnitType::MCV,60.5f,60.5f);
        yard=world.spawnBuilding(0,BldType::ConYard,6,6,true);
        for(auto p:{Vec2i{6,18},Vec2i{6,22},Vec2i{10,18}})world.spawnBuilding(0,BldType::PowerPlant,p.x,p.y,true);
        factories[0]=world.spawnBuilding(0,BldType::Barracks,14,6,true);
        factories[1]=world.spawnBuilding(0,BldType::WarFactory,20,12,true);
        factories[2]=world.spawnBuilding(0,BldType::AirForceCmd,30,10,true);
        for(int y=8;y<23;++y)for(int x=38;x<50;++x)world.map.at(x,y).terrain=Terrain::Water;
        factories[3]=world.spawnBuilding(0,BldType::NavalYard,40,10,true);
        world.spawnBuilding(0,BldType::OreRefinery,6,26,true);world.spawnBuilding(0,BldType::Radar,14,18,true);
        world.spawnBuilding(0,BldType::BattleLab,14,25,true);world.players[0].money=100000;
    };
    auto start=[&](bool unit,int type) {World::Cmd c;c.type=unit?World::Cmd::StartUnitProd:World::Cmd::StartBldProd;c.a=type;world.applyCmd(0,c);};
    auto work=[](const ProdItem& p){return int64_t(p.progress)*PRODUCTION_WORK_SCALE+p.workRemainder;};
    reset();for(auto t:types)start(true,(int)t);start(false,(int)BldType::PowerPlant);start(false,(int)BldType::Pillbox);
    for(int tick=0;tick<5;++tick)world.update();
    std::array<ProdItem,4> units;std::copy_n(world.players[0].unitProd,4,units.begin());const auto building=world.players[0].bldProd,defense=world.players[0].defProd;
    for(EID factory:factories)world.ents[factory].chrono=50;world.ents[yard].chrono=50;const auto money=world.players[0].money;
    for(int tick=0;tick<10;++tick)world.update();
    bool paused=world.players[0].money==money;
    for(int cat=0;cat<4;++cat)paused &= units[cat].active && work(world.players[0].unitProd[cat])==work(units[cat]) && world.players[0].unitProd[cat].paid==units[cat].paid;
    paused &= work(world.players[0].bldProd)==work(building) && work(world.players[0].defProd)==work(defense);
    check(paused,"frozen factories and yard pause all six queues without progress or debit");
    for(EID factory:factories)world.ents[factory].chrono=0;world.ents[yard].chrono=0;world.update();
    bool resumed=work(world.players[0].bldProd)>work(building)&&work(world.players[0].defProd)>work(defense);
    for(int cat=0;cat<4;++cat)resumed &= work(world.players[0].unitProd[cat])>work(units[cat]);
    check(resumed,"all queues automatically resume from accumulated work after thaw");
    reset();EID second=world.spawnBuilding(0,BldType::WarFactory,25,12,true);world.ents[factories[1]].chrono=50;start(true,(int)UnitType::Grizzly);world.update();
    check(work(world.players[0].unitProd[1])==1024,"one frozen factory contributes no multi-factory speed bonus");
    world.ents[factories[1]].chrono=0;world.update();check(work(world.players[0].unitProd[1])==2560,"thaw restores only future multi-factory work rate");
    reset();start(true,(int)UnitType::Grizzly);auto& order=world.players[0].unitProd[1];order.progress=unitDef(UnitType::Grizzly).buildTime;order.paid=order.totalCost;order.ready=true;
    world.players[0].money-=order.paid;world.ents[factories[1]].chrono=50;const auto completedMoney=world.players[0].money;world.update();
    check(world.countUnits(0,UnitType::Grizzly)==0 && order.active && order.ready && world.players[0].money==completedMoney,
        "a frozen factory cannot release an already paid completed order");
    world.ents[factories[1]].chrono=0;world.update();check(world.countUnits(0,UnitType::Grizzly)==1 && !order.active && world.players[0].money==completedMoney,
        "thaw releases the completed order once without another debit");
    reset();EID vat=world.spawnBuilding(0,BldType::CloningVat,24,24,true);world.ents[vat].chrono=50;start(true,(int)UnitType::GI);
    auto& infantry=world.players[0].unitProd[0];infantry.progress=unitDef(UnitType::GI).buildTime;infantry.paid=infantry.totalCost;infantry.ready=true;world.players[0].money-=infantry.paid;world.update();
    check(world.countUnits(0,UnitType::GI)==1,"a frozen cloning vat cannot produce a duplicate");
    reset();second=world.spawnBuilding(0,BldType::WarFactory,25,12,true);EID disc=world.spawnUnit(1,UnitType::FloatingDisc,21.5f,13.5f);
    world.ents[disc].target=factories[1];world.ents[disc].state=UState::Attacking;world.ents[factories[1]].drainedBy=disc;
    start(true,(int)UnitType::Grizzly);world.update();check(work(world.players[0].unitProd[1])==1024,"a drained factory does not stop a separate operational factory");
    for(bool selling:{false,true}) {
        reset();world.ents[factories[1]].constructAnim=100;world.ents[factories[1]].selling=selling;start(true,(int)UnitType::Grizzly);
        const auto before=world.players[0].money;world.update();check(work(world.players[0].unitProd[1])==0 && world.players[0].money==before,
            "unfinished or selling factories cannot advance production");
    }
    // Actual temporal fire must stop the queue, then naturally decay after the attacker is removed.
    reset();start(true,(int)UnitType::Grizzly);EID attacker=world.spawnUnit(1,UnitType::Chrono,25.5f,15.5f);
    World::Cmd attack;attack.type=World::Cmd::Attack;attack.ids={attacker};attack.a=factories[1];world.applyCmd(1,attack);
    for(int tick=0;tick<120 && world.ents[factories[1]].chrono==0;++tick)world.update();
    const auto hitWork=work(world.players[0].unitProd[1]);const auto hitMoney=world.players[0].money;
    bool hit=world.ents[factories[1]].chrono>0;for(int tick=0;tick<5;++tick)world.update();
    check(hit && work(world.players[0].unitProd[1])==hitWork && world.players[0].money==hitMoney,
        "real chrono weapon fire pauses the factory queue and its debit");
    world.kill(attacker,false);for(int tick=0;tick<200 && world.ents[factories[1]].chrono>0;++tick)world.update();world.update();
    check(world.valid(factories[1]) && work(world.players[0].unitProd[1])>hitWork,"removing the chrono attacker allows natural thaw and production recovery");
    reset();start(true,(int)UnitType::Grizzly);for(int tick=0;tick<5;++tick)world.update();world.ents[factories[1]].chrono=50;
    World restored;bool replay=false;if(FILE* file=tmpfile()){replay=world.saveGame(file);rewind(file);replay=replay && restored.loadGame(file);fclose(file);}
    for(int tick=0;tick<120 && replay;++tick){world.update();restored.update();replay=world.checksum()==restored.checksum();}
    check(replay,"current save resumes frozen production, thaw and payment identically for 120 ticks");
    reset();start(true,(int)UnitType::Grizzly);for(int tick=0;tick<5;++tick)world.update();world.ents[factories[1]].chrono=50;
    const auto refund=world.players[0].unitProd[1].paid;const auto beforeRefund=world.players[0].money;
    World::Cmd cancel;cancel.type=World::Cmd::CancelUnitProd;cancel.a=(int)UnitType::Grizzly;world.applyCmd(0,cancel);
    check(!world.players[0].unitProd[1].active && world.players[0].money==beforeRefund+refund,"canceling a frozen order refunds exactly the amount already paid");
    reset();second=world.spawnBuilding(0,BldType::WarFactory,25,12,true);world.ents[factories[1]].chrono=50;start(true,(int)UnitType::Grizzly);
    auto& ready=world.players[0].unitProd[1];ready.progress=unitDef(UnitType::Grizzly).buildTime;ready.ready=true;ready.paid=ready.totalCost;world.players[0].money-=ready.paid;world.update();
    bool alternative=false;for(const auto& e:world.ents)if(e.alive && !e.isBuilding && e.player==0 && e.utype==UnitType::Grizzly)alternative=e.x>24;
    check(alternative,"completed units use the operational alternate factory rather than a frozen exit");
    SetTargetFPS(0);phase=Phase::InGame;localPlayer=0;netGame=paused=showMenu=gameOver=false;clearPlacement();sel.clear();selBuilding=INVALID_EID;
    ais.clear();sim=SimInput{};sim.active=true;sim.pos={-100,-100};auditHud=true;uiTab=3;uiScroll=0;camX=-300;camY=0;camZoom=1;moveFeedbackTimer=msgTimer=0;
    bool glyphs=true,visual=true;
    for(int language=0;language<2;++language){g_lang=language;for(S key:{S::TipFacilityFrozen,S::TipFacilityDrained,S::TipFacilityWaiting,S::TipFacilityHalted}){
        int count=0;int* points=LoadCodepoints(TR(key),&count);for(int i=0;i<count;++i)glyphs &= font.glyphs[GetGlyphIndex(font,points[i])].value==points[i];UnloadCodepoints(points);
    }}check(glyphs,"facility status translations have real preloaded font glyphs");
    for(int scenario=0;scenario<4;++scenario){
        reset();start(true,(int)UnitType::Grizzly);for(int tick=0;tick<5;++tick)world.update();
        S expected=S::TipFacilityFrozen;
        if(scenario<=1)world.ents[factories[1]].chrono=50;
        if(scenario==1){auto& item=world.players[0].unitProd[1];world.players[0].money-=item.totalCost-item.paid;item.paid=item.totalCost;item.progress=unitDef(UnitType::Grizzly).buildTime;item.workRemainder=0;item.ready=true;}
        if(scenario==2){world.ents[factories[1]].constructAnim=20;expected=S::TipFacilityWaiting;}
        if(scenario==3){EID source=world.spawnUnit(1,UnitType::FloatingDisc,21.5f,13.5f);world.ents[source].target=factories[1];world.ents[factories[1]].drainedBy=source;expected=S::TipFacilityDrained;}
        std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_VISIBLE);fogMaskTick=-1;bakeTerrain();
        for(int language=0;language<2;++language)for(int width:{1280,1920}){
            g_lang=language;SetWindowSize(width,width==1280?720:1080);shotAtDisplaySize=true;sim.pos={-100,-100};render();Rectangle icon{};
            for(const auto& item:hudItems)if(item.unit && item.type==(int)UnitType::Grizzly)icon=item.bounds;
            sim.pos={icon.x+icon.width/2,icon.y+icon.height-3};render();visual &= icon.width>0 && hudTip.reason==TR(expected);
            for(size_t line=0;line<hudTip.lines.size();++line)visual &= MeasureTextEx(font,hudTip.lines[line].c_str(),(float)hudTip.lineSizes[line],0).x<=hudTip.bounds.width-17;
            const auto hash=world.checksum();shotFile=(std::filesystem::path(outputDir)/(std::string("facility_")+std::to_string(scenario)+"_"+std::to_string(language)+"_"+std::to_string(width)+".png")).string();render();
            visual &= world.checksum()==hash;
        }
    }
    check(visual,"real queued orders show correct bounded bilingual facility status at both resolutions without mutating simulation");
    auditHud=false;shotAtDisplaySize=false;sim=SimInput{};g_lang=0;
    std::printf("FACTORY_STATE failures=%d\n",failures);return failures;
}
