#include "game_regression.h"
#include "game/native_scene_contract.h"
#include "game/script.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

int GameRegression::driveBlockageRegression(const char* outputDir) {
    g_script.shutdown();ais.clear();netGame=false;phase=Phase::InGame;localPlayer=0;
    campaignMission=-1;matchSetup.reset();selectRulesChoice(RulesChoice::RA2);
    const std::filesystem::path output(outputDir);std::filesystem::create_directories(output);
    int failures=0;
    const auto check=[&](bool value,const char* text) {
        std::printf("DRIVE_BLOCKAGE %s %s\n",value?"PASS":"FAIL",text);failures+=!value;
    };
    constexpr int window=600;
    for(int scenario=0;scenario<10;++scenario) {
        const bool computer=scenario==2,deployed=scenario==3,frozen=scenario==4;
        const bool enemy=scenario==5,noExit=scenario==6,moving=scenario==7;
        const bool upper=scenario==8,lateSpawn=scenario==9;
        const int owner=computer || enemy?1:0;
        if(!world.init(64,64,724,computer?0:2,computer?2:0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,
            "assets/rules/profiles/ra2.ini",{Country::America,Country::Russia}))return failures+1;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& cell:world.map.cells){cell=Cell{};cell.terrain=Terrain::Water;}
        for(int x=8;x<=20;++x)world.map.at(x,16).terrain=Terrain::Clear;
        for(int y:{15,17})world.map.at(11,y).terrain=Terrain::Clear;
        world.cratesEnabled=false;world.crates.clear();
        world.aiAlliance=computer;
        const auto type=[&](const char* name) {
            for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==name)return i;
            return -1;
        };
        const EID vehicle=world.spawnUnit(0,upper?UnitType::WarMiner:scenario==1?UnitType::IFV:UnitType::Grizzly,
            10.5f,16.5f,type(upper?"HARV":scenario==1?"FV":"MTNK"));
        EID foot=lateSpawn?INVALID_EID:world.spawnUnit(owner,enemy?UnitType::TeslaTrooper:UnitType::GI,
            11.5f,15.5f,type(enemy?"SHK":"E1"));
        if(!world.valid(vehicle) || (!lateSpawn && !world.valid(foot)))return failures+1;
        const auto issue=[&](World& target,World::Cmd::Type kind,EID id,float x=0,float y=0) {
            World::Cmd cmd{};cmd.type=kind;cmd.ids={id};cmd.x=x;cmd.y=y;
            target.bindCommand(cmd);target.applyCmd(target.ents[id].player,cmd);
        };
        // Two immobilized occupants leave one legal infantry slot, while the
        // vehicle cannot bypass the blocked route through either side cell.
        for(int y:{15,17})for(int n=0;n<2;++n) {
            const auto id=world.spawnUnit(0,UnitType::GI,11.5f,y+.5f,type("E1"));
            world.ents[id].atkCd=1000000;world.ents[id].tpSick=window+180;issue(world,World::Cmd::Guard,id);
        }
        world.ents[vehicle].atkCd=1000000;
        world.ents[vehicle].dir=world.ents[vehicle].turretDir=5;
        issue(world,World::Cmd::Move,vehicle,13.5f,16.5f);
        const bool admitted=world.ents[vehicle].state==UState::Moving && !world.ents[vehicle].path.empty()
            && world.ents[vehicle].path.back()==Vec2i{13,16};
        int entered=lateSpawn?1:0;
        if(lateSpawn)foot=world.spawnUnit(owner,UnitType::GI,11.5f,16.5f,type("E1"));
        if(!world.valid(foot))return failures+1;
        world.ents[foot].atkCd=1000000;
        if(!lateSpawn) {
            issue(world,World::Cmd::Move,foot,11.5f,16.5f);
            for(int elapsed=1;elapsed<=120;++elapsed) {
                world.update();
                if(world.ents[foot].x==11.5f && world.ents[foot].y==16.5f) {entered=elapsed;break;}
            }
        }
        issue(world,World::Cmd::Guard,foot);
        if(deployed)issue(world,World::Cmd::Deploy,foot);
        if(frozen)world.ents[foot].tpSick=window+180;
        if(moving)issue(world,World::Cmd::Move,foot,11.5f,17.5f);
        if(noExit) {
            for(int y:{15,17}) {
                const auto id=world.spawnUnit(0,UnitType::GI,11.5f,y+.5f,type("E1"));
                world.ents[id].atkCd=1000000;world.ents[id].tpSick=window+180;issue(world,World::Cmd::Guard,id);
            }
            for(int n=0;n<3;++n) {
                const auto id=world.spawnUnit(0,UnitType::GI,12.5f,16.5f,type("E1"));
                world.ents[id].atkCd=1000000;world.ents[id].tpSick=window+180;issue(world,World::Cmd::Guard,id);
            }
        }
        if(upper) {
            world.map.bridgeCells.assign(world.map.cells.size(),{});
            world.map.bridgeCells[16*world.map.w+11]={0,4,0};
            world.ents[foot].navLayer=world.ents[foot].previousNavLayer=world.ents[foot].goalLayer=1;
            world.unitOccSync(foot);
        }
        const auto blocked=output/("blocked-"+std::to_string(scenario)+"-ordered.sav");
        FILE* file=nullptr;bool exact=true;World replay;
        if(upper) {
            // This manually supplied deck isolates the layer query. Ordinary
            // profiles cannot encode authored bridge actors; real source-deck
            // persistence belongs to nativeBridgeRegression's valid scene.
            replay=world;replay.map.bldOccRef=&replay.bldOcc;
        } else {
            file=std::fopen(blocked.string().c_str(),"wb");exact=file && world.saveGame(file);if(file)std::fclose(file);
            file=std::fopen(blocked.string().c_str(),"rb");exact &= file && replay.loadGame(file);if(file)std::fclose(file);
        }
        exact &= world.checksum()==replay.checksum();
        std::ofstream trace(output/("blocked-"+std::to_string(scenario)+".trace"));trace<<world.checksum()<<'\n';
        int response=0,arrived=0;bool displaced=false,healthy=true,quiet=true,midway=false,aligned=true,turningReplay=!lateSpawn;
        bool movingOrder=true,passedBelow=false;
        const int hp=world.ents[foot].hp;
        for(int elapsed=1;elapsed<=window;++elapsed) {
            world.update();if(exact){replay.update();exact=world.checksum()==replay.checksum();}
            trace<<world.checksum()<<'\n';
            if(lateSpawn && elapsed==12) {
                const auto path=output/("blocked-"+std::to_string(scenario)+"-turning.sav");
                file=std::fopen(path.string().c_str(),"wb");turningReplay=file && world.saveGame(file);if(file)std::fclose(file);
                turningReplay &= !response && world.nativeHullHeading(world.ents[vehicle],world.tick)!=0x4000;
                World a=world,b;a.map.bldOccRef=&a.bldOcc;
                file=std::fopen(path.string().c_str(),"rb");turningReplay &= file && b.loadGame(file);if(file)std::fclose(file);
                for(int n=0;n<120 && turningReplay;++n){a.update();b.update();turningReplay=a.checksum()==b.checksum();}
            }
            healthy &= world.valid(foot) && world.ents[foot].hp==hp;
            const auto& target=world.ents[foot];
            displaced |= target.x!=11.5f || target.y!=16.5f;
            if(moving && target.pathIdx<(int)target.path.size())movingOrder &= target.goalX==11.5f && target.goalY==17.5f;
            if(!response && !moving && target.state==UState::Moving && target.pathIdx<(int)target.path.size()) {
                response=elapsed;aligned=world.nativeHullHeading(world.ents[vehicle],world.tick)==0x4000;
                const auto path=output/("blocked-"+std::to_string(scenario)+"-midway.sav");
                file=std::fopen(path.string().c_str(),"wb");midway=file && world.saveGame(file);if(file)std::fclose(file);
                World a=world,b;a.map.bldOccRef=&a.bldOcc;
                file=std::fopen(path.string().c_str(),"rb");midway &= file && b.loadGame(file);if(file)std::fclose(file);
                for(int n=0;n<120 && midway;++n){a.update();b.update();midway=a.checksum()==b.checksum();}
            }
            if(upper)passedBelow |= world.ents[vehicle].navLayer==0 && (int)world.ents[vehicle].x==11;
            for(const auto& event:world.presentationEvents)
                if((event.kind==PresentationKind::MoveOrder || event.kind==PresentationKind::DeployOrder)
                    && event.tick==world.tick && event.entity==foot)quiet=false;
            if(!arrived && world.ents[vehicle].x==13.5f && world.ents[vehicle].y==16.5f)arrived=elapsed;
        }
        std::printf("DRIVE_BLOCKAGE_CASE case=%d scene=%d admitted=%d entered=%d response=%d moved=%d arrived=%d healthy=%d replay=%d quiet=%d mover=%.3f,%.3f foot=%.3f,%.3f checksum=%u\n",
            scenario,world.nativeSceneVersion,admitted,entered,response,displaced,arrived,healthy,exact,quiet,
            world.ents[vehicle].x,world.ents[vehicle].y,world.ents[foot].x,world.ents[foot].y,world.checksum());
        check(world.nativeSceneVersion==nativeCurrentRulesSceneVersion && admitted && entered>0 && exact && healthy && bool(trace),
            upper?"a supplied cross-layer mechanism has exact copied continuation; real bridge persistence is checked separately"
                :"actual Move then Guard establishes a current-profile blocked route with exact saved continuation");
        const bool shouldRespond=!frozen && !enemy && !noExit && !moving && !upper;
        check(quiet && movingOrder && (noExit?response>0 && displaced && !arrived && midway && aligned
                :shouldRespond?response>0 && displaced && arrived>0 && midway && aligned
                :!response && (moving?displaced && arrived>0:upper?passedBelow && arrived>0:!displaced && !arrived)),
            "forced blockage scatter preserves movement, immobilization, hostile, full-exit and surface boundaries");
        if(deployed)check(!world.ents[foot].deployed,"a forced GI scatter completes ordinary undeployment before leaving");
        if(lateSpawn)check(response>=48 && turningReplay,"Drive aligns before forced scatter and resumes exactly from a mid-turn save");
        std::printf("DRIVE_BLOCKAGE_CONTROL case=%d expected=%d aligned=%d midpoint=%d turning_replay=%d moving_order=%d below=%d persistence=%d\n",
            scenario,shouldRespond,aligned,midway,turningReplay,movingOrder,passedBelow,!upper);
        std::ofstream final(output/("blocked-"+std::to_string(scenario)+".final"));final<<world.checksum()<<'\n';
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_DRIVE_BLOCKAGE_DIR"))for(int scenario=0;scenario<2;++scenario) {
        const auto name="blocked-"+std::to_string(scenario);const std::filesystem::path legacy(directory);World old;
        FILE* file=std::fopen((legacy/(name+"-ordered.sav")).string().c_str(),"rb");bool exact=file && old.loadGame(file);if(file)std::fclose(file);
        std::ifstream trace(legacy/(name+".trace")),final(legacy/(name+".final"));uint32_t initial=0,last=0;
        exact &= old.nativeSceneVersion==50 && bool(trace>>initial) && bool(final>>last) && old.checksum()==initial;
        for(int elapsed=0;elapsed<window && exact;++elapsed) {old.update();uint32_t value=0;exact=bool(trace>>value) && old.checksum()==value;}
        exact &= old.checksum()==last;
        std::printf("DRIVE_BLOCKAGE_LEGACY case=%d scene=%d exact=%d\n",scenario,old.nativeSceneVersion,exact);
        check(exact,"actual Scene50 blocked orders retain all 601 checksums and their original final outcome");
    }
    std::printf("DRIVE_BLOCKAGE_RESULT failures=%d\n",failures);return failures;
}
