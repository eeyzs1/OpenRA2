#include "game/game.h"
#include <cstdio>

int Game::immobilizedRegression() {
    int failures=0;
    for(int scenario=0;scenario<13;++scenario) {
        World w;w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) cell=Cell{};
        for(auto& player:w.players) { player.money=0;player.defeated=false; }
        w.cratesEnabled=false;
        const auto type=scenario==1 || scenario==9 || scenario==12?UnitType::GI:scenario==2 || scenario==3 || scenario==7 || scenario==11?UnitType::SiegeChopper:
            scenario==4 || scenario==5?UnitType::Chrono:UnitType::MCV;
        EID actor=w.spawnUnit(0,type,30.5f,30.5f);
        if(scenario==10) { w.kill(actor,false);actor=w.spawnBuilding(0,BldType::ConYard,30,30,true); }
        World::Cmd deploy{};deploy.type=World::Cmd::RadDeploy;deploy.ids={actor};
        if(scenario==1 || scenario==2 || scenario==7 || scenario==11) w.applyCmd(0,deploy);
        if(scenario==8) w.orderMove({actor},35.5f,30.5f,false);
        bool actual=true;
        if(scenario==7) {
            EID shooter=w.spawnUnit(1,UnitType::Chrono,26.5f,30.5f);
            w.ents[shooter].invuln=10000;
            World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={shooter};attack.a=actor;w.applyCmd(1,attack);
            for(int i=0;i<200 && w.ents[actor].chrono==0;++i) w.update();
            actual=w.ents[actor].chrono>0;
            World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={shooter};w.applyCmd(1,stop);
            w.ents[shooter].guard=false;
            w.orderMove({shooter},10.5f,10.5f,false);
        } else if(scenario==3 || scenario==4) w.ents[actor].tpSick=40;
        else w.ents[actor].chrono=scenario==6 || scenario==11?unitDef(type).hp/3+20:40;
        const float x=w.ents[actor].x,y=w.ents[actor].y;
        const auto originalState=w.ents[actor].state;const auto originalPath=w.ents[actor].path;
        const bool deployed=w.ents[actor].deployed;
        bool command=true;
        if(scenario==9 || scenario==12) {
            EID follower=w.spawnUnit(scenario==12?1:0,UnitType::MCV,29.5f,30.5f);
            w.orderMove({follower},35.5f,30.5f,false);
        }
        if(scenario!=6 && scenario!=8 && scenario!=9 && scenario!=11 && scenario!=12) {
            if(scenario==1 || scenario==2 || scenario==7) {
                w.applyCmd(0,deploy);command=w.ents[actor].deployed==deployed;
            } else {
                World::Cmd move{};move.type=World::Cmd::Move;move.ids={actor};move.x=40.5f;move.y=40.5f;
                w.applyCmd(0,move);
                command=w.ents[actor].x==x && w.ents[actor].y==y && w.ents[actor].state==originalState
                    && w.ents[actor].path==originalPath && !w.presentationEvents.empty()
                    && w.presentationEvents.back().kind==PresentationKind::MoveOrder
                    && w.presentationEvents.back().move.reason==MoveRejectReason::Immobilized;
            }
        }
        if(scenario==0) { World::Cmd expand{};expand.type=World::Cmd::Deploy;expand.ids={actor};w.applyCmd(0,expand);command &= w.valid(actor) && !w.ents[actor].isBuilding; }
        if(scenario==10) { World::Cmd pack{};pack.type=World::Cmd::Deploy;pack.ids={actor};w.applyCmd(0,pack);command &= w.valid(actor) && w.ents[actor].isBuilding; }
        FILE* file=tmpfile();World replay;bool equal=file && w.saveGame(file);
        if(equal) { rewind(file);equal=replay.loadGame(file); }
        if(file) fclose(file);
        bool frozen=true;
        for(int i=0;i<15 && equal;++i) {
            w.update();replay.update();equal &= w.checksum()==replay.checksum();
            if(scenario==6 || scenario==11) frozen &= !w.valid(actor);
            else frozen &= w.valid(actor) && w.ents[actor].x==x && w.ents[actor].y==y
                && w.ents[actor].deployed==deployed && (w.ents[actor].chrono>0 || w.ents[actor].tpSick>0);
        }
        bool recovery=true;
        if(scenario!=6 && scenario!=11) {
            for(int i=0;i<500 && equal;++i) { w.update();replay.update();equal &= w.checksum()==replay.checksum(); }
            recovery=w.valid(actor) && w.ents[actor].chrono==0 && w.ents[actor].tpSick==0;
            if(scenario==12) recovery=!w.valid(actor) || recovery;
            if(scenario==8) recovery &= w.ents[actor].x>34 && w.ents[actor].state==UState::Idle;
        }
        const bool ok=actual && command && frozen && equal && recovery;
        failures+=!ok;
        std::printf("IMMOBILIZED %s scenario=%d actual=%d command=%d frozen=%d replay=%d recovery=%d\n",
            ok?"PASS":"FAIL",scenario,actual,command,frozen,equal,recovery);
    }
    return failures;
}
