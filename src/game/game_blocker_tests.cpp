#include "game/game.h"
#include <cstdio>

int Game::blockerRegression() {
    int failures=0;
    for(int scenario=0;scenario<15;++scenario) {
        World w;w.init(64,64,20261031,2,0,{scenario==10?Faction::China:Faction::Allies,scenario==10?Faction::China:Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass)
            for(size_t id=0;id<w.ents.size();++id) if(w.ents[id].alive) w.kill((EID)id,false);
        for(auto& cell:w.map.cells) { cell=Cell{};cell.overlay=Overlay::Rock1; }
        const Terrain road=scenario==8 || scenario==10?Terrain::Water:Terrain::Clear;
        auto clear=[&](int x0,int y0,int x1,int y1) {
            for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) { w.map.at(x,y)=Cell{};w.map.at(x,y).terrain=road; }
        };
        clear(2,8,12,20);clear(13,14,48,14);clear(49,8,61,20);
        if(scenario==1) clear(2,8,61,20);
        for(auto& player:w.players) { player.defeated=false;player.money=0; }
        w.cratesEnabled=false;w.presentationEvents.clear();
        const auto mover=scenario>=11?UnitType::Engineer:scenario==10?UnitType::Aegis:scenario==8?UnitType::AmphTransport:UnitType::MCV;
        EID actor=w.spawnUnit(0,mover,scenario==6?29.5f:8.5f,14.5f);
        // Mechanism fixture: isolate collision handling from hostile gunfire.
        w.ents[actor].invuln=30000;
        UnitType obstacle=scenario==14?UnitType::TerrorDrone:scenario==2 || scenario==3 || scenario==4 || scenario==9?UnitType::GI:
            scenario==7?UnitType::Desolator:mover;
        EID blocker=w.spawnUnit(scenario==2 || scenario==9?0:scenario==5?-1:1,obstacle,30.5f,14.5f);
        if(scenario==12 || scenario==13) {
            w.spawnUnit(scenario==13?0:1,UnitType::Engineer,30.5f,14.5f);
            w.spawnUnit(1,UnitType::Engineer,30.5f,14.5f);
        }
        if(scenario==14) {
            EID host=w.spawnUnit(0,UnitType::MCV,30.5f,14.5f);w.ents[host].invuln=30000;
            w.ents[blocker].parasiting=true;w.ents[blocker].parasiteHost=host;w.ents[host].parasite=blocker;
            w.unitOccSync(blocker);w.orderMove({host},59.5f,18.5f,false);
        }
        if(scenario==2 || scenario==7 || scenario==9) w.orderRadDeploy({blocker});
        if(scenario==3) w.ents[blocker].invuln=30000;
        if(scenario==6) {
            w.orderMove({blocker},59.5f,18.5f,false);
            w.ents[blocker].path.insert(w.ents[blocker].path.begin(),90,Vec2i{30,14});
        }
        World::Cmd command{};command.type=World::Cmd::Move;command.ids={actor};command.x=55.5f;command.y=14.5f;
        w.applyCmd(0,command);command.a=1;command.x=57.5f;w.applyCmd(0,command);
        if(scenario==9) { w.ents[blocker].x=55.5f;w.unitOccSync(blocker); }
        if(scenario>=11) w.spawnBuilding(0,BldType::PowerPlant,10,13,true);
        w.presentationEvents.clear();
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        bool legal=true,equal=true,saved=false,budget=true;int reports=0,adjustments=0;
        auto step=[&] {
            w.update();replay.update();equal &= w.checksum()==replay.checksum();budget &= w.repathsThisTick<=8;
            const auto& e=w.ents[actor];
            legal &= w.valid(actor) && w.passableFor((int)e.x,(int)e.y,unitDef(e.utype).pathDomain()) && !w.bldBlocked((int)e.x,(int)e.y);
            int infantry=0,vehicles=0;
            for(const auto& other:w.ents) if(other.alive && !other.isBuilding && !other.parasiting
                && (!unitDef(other.utype).isAir() || other.state==UState::Landed)
                && (int)other.x==(int)e.x && (int)other.y==(int)e.y) {
                if(unitDef(other.utype).isInfantry()) ++infantry;else ++vehicles;
            }
            legal &= unitDef(e.utype).isInfantry()?vehicles==0 && infantry<=3:vehicles==1 && infantry==0;
            for(const auto& event:w.presentationEvents) if(event.kind==PresentationKind::MoveOrder && event.entity==actor && event.move.rejected) {
                ++reports;legal &= event.move.reason==MoveRejectReason::Unreachable;
            }
            for(const auto& event:w.presentationEvents) if(event.kind==PresentationKind::MoveOrder && event.entity==actor && event.move.adjusted) {
                ++adjustments;legal &= event.move.accepted==1 && event.tick==w.tick && (event.x!=55.5f || event.y!=14.5f);
            }
            w.presentationEvents.clear();replay.presentationEvents.clear();
        };
        for(int frame=0;frame<1800;++frame) {
            step();
            if(frame==200) {
                FILE* file=tmpfile();saved=file && w.saveGame(file);
                if(saved) { rewind(file);saved=replay.loadGame(file); }
                if(file) fclose(file);
                if(!saved) break;
                equal &= w.checksum()==replay.checksum();
            }
        }
        if(!saved) { ++failures;std::printf("BLOCKER FAIL scenario=%d saved=0\n",scenario);continue; }
        const bool pass=scenario==1 || scenario==4 || scenario==6 || scenario==9 || scenario==11 || scenario==13 || scenario==14;
        const auto requests=w.repathRequests;
        for(int frame=0;frame<300;++frame) step();
        const bool bounded=requests==w.repathRequests;
        bool outcome=pass?w.ents[actor].state==UState::Idle && (int)w.ents[actor].x==57 && reports==0:
            w.ents[actor].state==UState::Idle && w.ents[actor].path.empty() && w.ents[actor].wps.empty() && reports==1;
        bool recovered=pass;
        if(!pass && outcome) {
            w.kill(blocker,false);replay.kill(blocker,false);
            command.a=0;command.x=55.5f;w.applyCmd(0,command);replay.applyCmd(0,command);
            for(int frame=0;frame<1200;++frame) step();
            recovered=w.ents[actor].state==UState::Idle && (int)w.ents[actor].x==55;
        }
        bool ok=legal && equal && saved && budget && outcome && bounded && recovered
            && reports==(pass?0:1) && adjustments==(scenario==9?1:0);
        failures+=!ok;
        std::printf("BLOCKER %s scenario=%d legal=%d replay=%d saved=%d budget=%d outcome=%d reports=%d adjusted=%d bounded=%d recovered=%d requests=%llu\n",
            ok?"PASS":"FAIL",scenario,legal,equal,saved,budget,outcome,reports,adjustments,bounded,recovered,(unsigned long long)requests);
    }
    return failures;
}
