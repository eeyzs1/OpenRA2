#include "game_regression.h"
#include <cstdio>

int GameRegression::chopperGroundRegression() {
    int failures=0;
    for(int scenario=0;scenario<9;++scenario) {
        World w;w.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"",true);
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) cell=Cell{};
        if(scenario==8) {
            for(auto& cell:w.map.cells) cell.overlay=Overlay::Rock1;
            for(int x=20;x<=45;++x) w.map.at(x,30)=Cell{};
        }
        for(auto& player:w.players) { player.defeated=false;player.money=0; }
        w.cratesEnabled=false;
        const EID chopper=w.spawnUnit(0,UnitType::SiegeChopper,30.5f,30.5f);
        EID other=INVALID_EID;
        if(scenario==1) other=w.spawnUnit(0,UnitType::MCV,30.5f,30.5f);
        if(scenario==2) other=w.spawnUnit(0,UnitType::Engineer,30.5f,30.5f);
        if(scenario==3) other=w.spawnBuilding(0,BldType::PowerPlant,30,30,true);
        if(scenario==4) w.map.at(30,30).terrain=Terrain::Water;
        if(scenario==5) w.map.at(30,30).overlay=Overlay::Rock1;
        if(scenario==6) other=w.spawnUnit(0,UnitType::SiegeChopper,30.5f,30.5f);
        if(scenario>=1 && scenario<=5) w.orderMove({chopper},40.5f,40.5f,false);
        World::Cmd deploy{};deploy.type=World::Cmd::RadDeploy;deploy.ids={chopper};
        if(scenario==6) deploy.ids.push_back(other);
        w.applyCmd(0,deploy);
        const bool shouldDeploy=scenario==0 || scenario>=6;
        if(shouldDeploy)for(int tick=0;tick<g_gameRules.chopperLandingTicks+g_gameRules.chopperDeployTicks;++tick)w.update();
        bool state=w.ents[chopper].deployed==shouldDeploy;
        if(!shouldDeploy) state &= w.ents[chopper].state==UState::Moving && w.ents[chopper].goalX==40.5f;
        bool indexed=(w.ents[chopper].occCell==30*w.map.w+30)==shouldDeploy
            && (w.ents[chopper].airBucket>=0)!=shouldDeploy;
        if(scenario==6) state &= !w.ents[other].deployed && w.ents[other].occCell<0 && w.ents[other].airBucket>=0;
        const EID tank=w.spawnUnit(1,UnitType::Grizzly,27.5f,30.5f);
        const auto gun=w.effWeapon(w.ents[tank]);
        const bool target=w.attackRejection(tank,chopper)==(shouldDeploy?AttackRejectReason::None:AttackRejectReason::NoAntiAir)
            && (w.findNearestEnemy(1,27.5f,30.5f,5,false,&gun,UnitType::Grizzly,tank)==chopper)==shouldDeploy;
        if(shouldDeploy) {
            state &= w.cellHardBlockedForMove(30,30,tank);
            const auto move=w.orderMove({tank},scenario==8?35.5f:30.5f,30.5f,false);
            state &= move.accepted==1 && (scenario==8 || move.adjusted==1) && !w.ents[tank].path.empty()
                && w.ents[tank].path.back()!=Vec2i{30,30};
        }
        // Prevent combat from obscuring the occupancy/save mechanism checks.
        w.ents[chopper].invuln=10000;w.ents[tank].invuln=10000;
        FILE* file=tmpfile();World replay;
        bool equal=file && w.saveGame(file);
        if(equal) { rewind(file);equal=replay.loadGame(file); }
        if(file) fclose(file);
        bool persisted=equal && replay.ents[chopper].deployed==shouldDeploy
            && (replay.ents[chopper].occCell>=0)==shouldDeploy;
        for(int frame=0;frame<180 && equal;++frame) {
            w.update();replay.update();equal &= w.checksum()==replay.checksum();
            indexed &= (w.ents[chopper].occCell>=0)==shouldDeploy && (w.ents[chopper].airBucket>=0)!=shouldDeploy;
            if(scenario==8) indexed &= w.ents[tank].x<30 && w.ents[tank].occCell!=w.ents[chopper].occCell;
        }
        bool released=true;
        if(shouldDeploy && equal && scenario!=7) {
            deploy.ids={chopper};w.applyCmd(0,deploy);replay.applyCmd(0,deploy);
            for(int tick=0;tick<g_gameRules.chopperLandingTicks+g_gameRules.chopperDeployTicks;++tick){w.update();replay.update();equal &= w.checksum()==replay.checksum();}
            released=!w.ents[chopper].deployed && w.ents[chopper].occCell<0 && w.ents[chopper].airBucket>=0
                && w.attackRejection(tank,chopper)==AttackRejectReason::NoAntiAir;
            World::Cmd move{};move.type=World::Cmd::Move;move.ids={chopper};move.x=40.5f;move.y=40.5f;
            w.applyCmd(0,move);replay.applyCmd(0,move);
            if(scenario==8) {
                move.ids={tank};move.x=35.5f;move.y=30.5f;w.applyCmd(1,move);replay.applyCmd(1,move);
            }
            for(int frame=0;frame<400;++frame) { w.update();replay.update();equal &= w.checksum()==replay.checksum(); }
            released &= w.ents[chopper].x>39 && w.ents[chopper].y>39 && w.ents[chopper].occCell<0;
            if(scenario==8) released &= w.ents[tank].x>34;
        }
        if(scenario==7) {
            w.kill(chopper,false);replay.kill(chopper,false);
            released &= w.unitAtCell(30,30)!=chopper && w.ents[chopper].occCell<0 && w.ents[chopper].airBucket<0;
            equal &= w.checksum()==replay.checksum();
        }
        const bool ok=state && indexed && target && persisted && equal && released;
        failures+=!ok;
        std::printf("CHOPPER_GROUND %s scenario=%d state=%d indexed=%d target=%d persisted=%d replay=%d released=%d\n",
            ok?"PASS":"FAIL",scenario,state,indexed,target,persisted,equal,released);
    }
    return failures;
}
