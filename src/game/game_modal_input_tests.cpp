#include "game_regression.h"
#include <cstdio>
#include <filesystem>

int GameRegression::modalInputRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);SetTargetFPS(0);int failures=0;
    const auto check=[&](bool ok,const char* message){
        std::printf("MODAL_INPUT %s rules=%d finished=%d %s\n",ok?"PASS":"FAIL",(int)cfgRules,gameOver,message);
        std::fflush(stdout);failures+=!ok;return ok;
    };
    for(auto rules:{RulesChoice::Fusion,RulesChoice::RA2,RulesChoice::YR})for(bool finished:{false,true}) {
        selectRulesChoice(rules);cfgCountry=(int)Country::America;cfgAI=1;aiCountry[0]=(int)Country::Russia;
        cfgMapSize=64;cfgMapType=0;cfgGameMode=(int)SkirmishMode::Battle;cfgReferenceMap=false;cfgMoney=10000;
        sim={};sim.active=true;sim.pos={-100,-100};newGame(922);
        EID mcv=INVALID_EID;for(EID id=0;id<(EID)world.ents.size();++id){const auto& e=world.ents[id];
            if(e.alive && !e.isBuilding && e.player==0 && e.utype==UnitType::MCV){mcv=id;break;}}
        World::Cmd deploy;deploy.type=World::Cmd::Deploy;deploy.ids={mcv};issueCmd(deploy);
        auditHud=true;uiTab=0;render();Rectangle box{};
        const int selector=world.productionSelector(0,false,(int)BldType::PowerPlant);
        for(const auto& item:hudItems)if(!item.unit && item.type==selector)box=item.bounds;
        if(!check(phase==Phase::InGame && world.hasBld(0,BldType::ConYard) && box.width>0,
            "ordinary opening and real power-plant cameo supply the fixture"))continue;
        const Vector2 point{box.x+box.width*.5f,box.y+box.height*.5f};
        // The obscured first cameo maps above menu actions, so this never
        // activates save/load/restart or writes the player's quicksave.
        if(!check(menuUiFromCanvas(point).y<178,"background click is outside every menu action"))continue;
        showMenu=true;paused=true;gameOver=finished;victory=finished;
        auto checksum=world.checksum();sim.pos=point;sim.pressedL=true;render();sim.pressedL=false;
        check(world.checksum()==checksum && !world.players[0].bldProd.active && showMenu,
            "menu and game-over fixture overlays capture left-click before the hidden production cameo");
        showMenu=false;paused=gameOver=victory=false;
        if(!world.players[0].bldProd.active){sim.pressedL=true;render();sim.pressedL=false;}
        if(!check(world.players[0].bldProd.active,"the ordinary unobscured cameo still starts a real order"))continue;
        showMenu=true;paused=true;gameOver=finished;victory=finished;
        checksum=world.checksum();sim.pressedR=true;render();sim.pressedR=false;
        check(world.checksum()==checksum && !world.players[0].bldProd.held,
            "the obscured right-click cannot pause or cancel paid production");
        netGame=true;pendingCmds.clear();sim.pressedL=true;render();sim.pressedL=false;
        check(pendingCmds.empty(),"a network overlay cannot enqueue a hidden gameplay command");
        netGame=false;pendingCmds.clear();
        const auto picture=std::filesystem::path(outputDir)/("rules-"+std::to_string((int)rules)+"-finished-"+std::to_string(finished)+".png");
        shotFile=picture.string();render();
        if(!finished) {
            const auto side=menuShellSide();sim.pos=menuCanvasFromUi(side.x+side.width*.5f,side.y+side.height-34);
            checksum=world.checksum();sim.pressedL=true;render();sim.pressedL=false;
            check(!showMenu && world.checksum()==checksum,"Continue consumes its click for the entire frame and leaves gameplay state unchanged");
        }
    }
    netGame=false;sim={};auditHud=false;return failures;
}
