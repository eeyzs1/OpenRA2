#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

int GameRegression::attackMoveRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("ATTACK_MOVE %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    g_script.shutdown();ais.clear();phase=Phase::InGame;campaignMission=-1;localPlayer=0;gameOver=victory=paused=showMenu=netGame=false;camZoom=1;
    const auto fresh=[](World& w){w.init(48,48,47,2,0,{Faction::Allies,Faction::Soviet});
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=Terrain::Clear;}w.cratesEnabled=false;};
    const auto move=[](World& w,EID id,float x,float y,bool attack=true,bool append=false,int selector=0){World::Cmd c;
        c.type=World::Cmd::Move;c.ids={id};c.x=x;c.y=y;c.attackMove=attack;c.a=append?1:0;c.b=selector;w.applyCmd(0,c);};
    const auto clone=[](const World& a,World& b){FILE* f=tmpfile();bool same=f && a.saveGame(f);if(same){rewind(f);same=b.loadGame(f);}if(f)fclose(f);return same && a.checksum()==b.checksum();};
    const auto resume=[&](World& a,int ticks){World b;bool same=clone(a,b);for(int i=0;i<ticks && same;++i){a.update();b.update();same=a.checksum()==b.checksum();}return same;};
    const auto at=[](const World& w,EID id,float x,float y){return w.valid(id) && distf(w.ents[id].x,w.ents[id].y,x,y)<.1f;};
    // A real YR eight-house save contained a Boris that had spent over 54000
    // ticks inside the goal cell, off its center, retrying an empty cell path.
    // Reconstruct that legal post-combat state and also exercise real commands.
    for(int choice=0;choice<3;++choice)for(UnitType type:{UnitType::Tanya,UnitType::Grizzly,UnitType::Boris}) {
        if(type==UnitType::Boris && choice!=2)continue;
        const auto arrivalWorld=[&](World& w) {
            const char* profile=choice==0?nullptr:choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
            const bool ok=w.init(48,48,47,2,0,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,profile);
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
            for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=Terrain::Clear;}w.cratesEnabled=false;
            return ok;
        };
        {
            World w;const bool ready=arrivalWorld(w);const EID id=w.spawnUnit(0,type,5.9034f,15.0966f);
            const auto before=w.ents[id];const auto result=w.orderMove({id},5.5f,15.5f,false);
            const bool physical=ready && result.accepted==1 && w.ents[id].x==before.x && w.ents[id].y==before.y;
            check(physical,"a same-cell ordinary order is accepted without teleporting the actor");
            bool budget=true;for(int i=0;i<120;++i){w.update();budget &= w.repathsThisTick<=8;}
            std::printf("ATTACK_MOVE SAME_CELL choice=%d type=%d xy=%.7f,%.7f state=%d\n",choice,(int)type,w.ents[id].x,w.ents[id].y,(int)w.ents[id].state);
            check(physical && budget && distf(w.ents[id].x,w.ents[id].y,5.5f,15.5f)<.0001f && w.ents[id].state==UState::Idle,
                "ordinary infantry and vehicle movement walks the remaining distance inside its current cell");
        }
        {
            World w;const bool ready=arrivalWorld(w);const EID id=w.spawnUnit(0,type,5.5f,15.5f);
            const auto order=w.orderMove({id},25.5f,15.5f,true);
            // Incidental pursuit can finish anywhere in the destination cell.
            auto& e=w.ents[id];e.x=25.9034f;e.y=15.0966f;e.state=UState::Idle;e.clearPath();w.unitOccSync(id);
            const bool retained=ready && order.accepted==1 && e.attackMove.active;
            const auto input=(std::filesystem::path(outputDir)/("same-cell-"+std::to_string(choice)+"-"+std::to_string((int)type)+"-input-world60.sav")).string();
            FILE* f=std::fopen(input.c_str(),"wb");const bool saved=f && w.saveGame(f);if(f)std::fclose(f);
            const bool exact=saved && resume(w,120);
            check(retained && exact,"an off-center post-combat arrival preserves its intent and replays exactly across a real save/load");
            check(exact && distf(w.ents[id].x,w.ents[id].y,25.5f,15.5f)<.0001f && !w.ents[id].attackMove.active
                && w.ents[id].state==UState::Idle,
                "same-cell attack-move resumption reaches the physical endpoint and releases the retained order");
            const auto output=(std::filesystem::path(outputDir)/("same-cell-"+std::to_string(choice)+"-"+std::to_string((int)type)+"-result-world60.sav")).string();
            f=std::fopen(output.c_str(),"wb");const bool written=f && w.saveGame(f);if(f)std::fclose(f);
            check(written,"the actual same-cell movement result is retained for cross-build comparison");
        }
    }
    // Snow Valley left two Kirovs indefinitely resuming an already completed
    // flight leg: physical cell arrival and retained-order arrival differed.
    for(int choice=1;choice<3;++choice)for(UnitType type:{UnitType::Kirov,UnitType::Rocketeer}) {
        World w;const char* profile=choice==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
        const bool ready=w.init(48,48,47,2,0,{Faction::Soviet,Faction::Allies},0,nullptr,false,false,profile);
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& p:w.players){p.money=0;p.defeated=false;}w.cratesEnabled=false;
        const EID actor=w.spawnUnit(0,type,5.5f,15.5f);
        const auto first=w.orderMove({actor},25.5f,15.5f,true);
        w.orderMove({actor},35.5f,30.5f,false,true);
        const bool queued=w.ents[actor].wps.size()==1 && w.ents[actor].wps.front().attackMode==0;
        World replay;bool copied=false,exact=true,firstReached=false,secondStarted=false;
        for(int i=0;i<10000;++i){
            if(i==120){copied=clone(w,replay);exact &= copied;}
            w.update();if(copied){replay.update();exact &= w.checksum()==replay.checksum();}
            const auto& e=w.ents[actor];firstReached |= distf(e.x,e.y,25.5f,15.5f)<1;
            secondStarted |= firstReached && e.goalX==35.5f && e.goalY==30.5f;
            if(secondStarted && distf(e.x,e.y,35.5f,30.5f)<1 && !e.attackMove.active && e.wps.empty()
                && e.state!=UState::Moving && e.state!=UState::AttackMoving)break;
        }
        const auto& e=w.ents[actor];
        std::printf("ATTACK_MOVE AIR_ARRIVAL choice=%d type=%d pos=%.5f,%.5f state=%d active=%d queued=%zu copied=%d exact=%d first=%d next=%d\n",
            choice,(int)type,e.x,e.y,(int)e.state,e.attackMove.active,e.wps.size(),copied,exact,firstReached,secondStarted);
        check(ready && first.accepted==1 && queued && copied && exact && firstReached && secondStarted
            && distf(e.x,e.y,35.5f,30.5f)<1 && !e.attackMove.active && e.wps.empty()
            && e.state!=UState::Moving && e.state!=UState::AttackMoving,
            "physical aerial arrival completes its attack-move and starts the queued leg through exact save/load");
    }
    for(UnitType type:{UnitType::Tanya,UnitType::Grizzly,UnitType::Rocketeer}) {
        World w;w.init(48,48,47,2,0,{Faction::Allies,Faction::Soviet});
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.terrain=Terrain::Clear;}
        w.cratesEnabled=false;
        const EID actor=w.spawnUnit(0,type,5.5f,15.5f),target=w.spawnUnit(1,UnitType::Conscript,12.5f,15.5f);
        World::Cmd c;c.type=World::Cmd::Move;c.ids={actor};c.x=35.5f;c.y=15.5f;c.attackMove=true;w.applyCmd(0,c);
        bool fought=false,saved=false;
        for(int tick=0;tick<1400 && w.valid(actor);++tick){w.update();
            fought |= w.ents[actor].target==target;
            if(type==UnitType::Tanya && fought && !saved && w.valid(target)) {
                saved=true;check(resume(w,10),"a live incidental fight restores its destination and continues with equal per-tick checksums");
            }
        }
        const bool arrived=w.valid(actor) && distf(w.ents[actor].x,w.ents[actor].y,c.x,c.y)<.6f;
        std::printf("ATTACK_MOVE CASE type=%d fought=%d target_dead=%d arrived=%d pos=%.3f,%.3f state=%d\n",(int)type,fought,!w.valid(target),arrived,w.valid(actor)?w.ents[actor].x:0,w.valid(actor)?w.ents[actor].y:0,w.valid(actor)?(int)w.ents[actor].state:-1);
        check(fought && !w.valid(target) && arrived,"one ordinary attack-move command survives incidental combat and reaches its destination");
        if(type==UnitType::Tanya){world=w;world.map.bldOccRef=&world.bldOcc;fogKnowledge={};syncFogKnowledge();
            const auto file=(std::filesystem::path(outputDir)/"infantry-result.sav").string();check(saveGameFile(file.c_str()),"the actual movement result saves for review");}
    }
    // New queued modes must neither replace the current fight nor turn a later
    // ordinary move into another attack-move.
    for(bool firstAttack:{true,false}) {
        World w;fresh(w);const EID actor=w.spawnUnit(0,UnitType::Tanya,5.5f,15.5f);
        const EID target=w.spawnUnit(1,UnitType::Conscript,firstAttack?12.5f:31.5f,15.5f);
        move(w,actor,25.5f,15.5f,firstAttack);w.update();move(w,actor,37.5f,15.5f,!firstAttack,true);
        const bool queued=w.ents[actor].wps.size()==1 && w.ents[actor].wps.front().attackMode==(!firstAttack?1:0)
            && (!firstAttack || w.ents[actor].attackMove.active);
        bool reachedFirst=false,arrived=false,earlyFight=false;World b;bool equal=clone(w,b);
        for(int i=0;i<1200 && equal;++i){w.update();b.update();equal=w.checksum()==b.checksum();
            if(!reachedFirst && !firstAttack)earlyFight |= w.ents[actor].target==target;
            reachedFirst |= at(w,actor,25.5f,15.5f);if(at(w,actor,37.5f,15.5f)){arrived=true;break;}}
        check(queued && equal && reachedFirst && arrived && !earlyFight && !w.valid(target),"queued ordinary/attack moves preserve the first destination and their own modes through combat and save/load");
    }
    {
        World w;fresh(w);const EID actor=w.spawnUnit(0,UnitType::Tanya,5.5f,15.5f),target=w.spawnUnit(1,UnitType::Conscript,12.5f,15.5f);
        move(w,actor,35.5f,15.5f);w.update();move(w,actor,37.5f,30.5f,false,true);const auto before=w.checksum();
        move(w,actor,30.5f,30.5f,false,false,2);const bool blocked=w.checksum()==before;
        move(w,actor,std::numeric_limits<float>::quiet_NaN(),30.5f,false);World::Cmd bad;bad.type=World::Cmd::Attack;bad.ids={actor};bad.a=actor;w.applyCmd(0,bad);
        check(blocked && w.checksum()==before && w.ents[actor].attackMove.active && w.ents[actor].wps.size()==1,
            "unreachable explicit deck, non-finite move and rejected attack preserve the whole existing command and queue");
        World changed=w;changed.ents[actor].attackMove.x+=1;const bool goalHashed=changed.checksum()!=before;
        changed=w;changed.ents[actor].wps.front().attackMode=1;
        check(goalHashed && changed.checksum()!=before,"both retained destinations and queued modes affect the simulation checksum");
        w.ents[actor].tpSick=8;const float x=w.ents[actor].x,y=w.ents[actor].y;const auto frozen=w.checksum();move(w,actor,6.5f,30.5f,false);
        check(w.checksum()==frozen && resume(w,5) && w.ents[actor].x==x && w.ents[actor].y==y && w.ents[actor].attackMove.active,
            "temporary immobilization and its rejected replacement do not erase or execute the saved order");
        World::Cmd stop;stop.type=World::Cmd::Stop;stop.ids={actor};w.applyCmd(0,stop);const bool cancelled=!w.ents[actor].attackMove.active && w.ents[actor].wps.empty();
        for(int i=0;i<600;++i)w.update();check(cancelled && w.valid(actor) && !at(w,actor,35.5f,15.5f) && !w.valid(target),"Stop cancels the retained route even if ordinary defensive combat happens later");
    }
    {
        World w;fresh(w);const EID actor=w.spawnUnit(0,UnitType::Tanya,5.5f,15.5f);w.spawnUnit(1,UnitType::Conscript,12.5f,15.5f);
        move(w,actor,35.5f,15.5f);w.update();move(w,actor,5.5f,30.5f,false);const bool cancelled=!w.ents[actor].attackMove.active;
        for(int i=0;i<700 && !at(w,actor,5.5f,30.5f);++i)w.update();check(cancelled && at(w,actor,5.5f,30.5f),"a successful replacement move cancels the old route and reaches its new destination");
    }
    {
        World w;fresh(w);const EID actor=w.spawnUnit(0,UnitType::Grizzly,5.5f,15.5f);move(w,actor,35.5f,15.5f);move(w,actor,37.5f,30.5f,false,true);
        for(int y=0;y<w.map.h;++y)w.map.at(20,y).terrain=Terrain::Water;
        for(int i=0;i<800;++i)w.update();const bool held=w.valid(actor) && w.ents[actor].attackMove.active && w.ents[actor].x<20 && w.ents[actor].wps.size()==1;
        std::printf("ATTACK_MOVE BLOCKED pos=%.3f,%.3f active=%d queued=%zu retry=%llu tick=%llu\n",w.ents[actor].x,w.ents[actor].y,w.ents[actor].attackMove.active,w.ents[actor].wps.size(),(unsigned long long)w.ents[actor].attackMove.retryAt,(unsigned long long)w.tick);
        check(held && resume(w,65),"a newly disconnected route remains pending with its queue and saved retry timing rather than treating the near side as arrival");
        for(int y=0;y<w.map.h;++y)w.map.at(20,y).terrain=Terrain::Clear;
        bool first=false;for(int i=0;i<1500 && w.valid(actor);++i){w.update();first |= at(w,actor,35.5f,15.5f);if(at(w,actor,37.5f,30.5f))break;}
        check(first && at(w,actor,37.5f,30.5f) && !w.ents[actor].attackMove.active,"restoring the real route resumes the same destination before executing the queued ordinary move");
    }
    // Explicit scheduler fixture: discard accepted path caches as after a
    // route failure, then disconnect their real destinations. The later Tanya
    // still fights an actual enemy on an open route; air needs no A* slot.
    for(int count:{0,239,270})for(UnitType kind:{UnitType::Tanya,UnitType::Rocketeer}) {
        const bool air=kind==UnitType::Rocketeer;if(air && count!=270)continue;
        World w;fresh(w);std::vector<EID> blocked;bool accepted=true;
        for(int i=0;i<count;++i){const EID id=w.spawnUnit(0,UnitType::Grizzly,2.5f+i%18,2.5f+i/18);blocked.push_back(id);
            move(w,id,35.5f,15.5f);accepted &= w.ents[id].attackMove.active && w.ents[id].attackMove.x>23;
            w.ents[id].clearPath();w.ents[id].state=UState::Idle;}
        for(int y=0;y<w.map.h;++y)w.map.at(23,y).terrain=Terrain::Water;
        const EID actor=w.spawnUnit(0,kind,2.5f,40.5f);
        const EID target=air?INVALID_EID:w.spawnUnit(1,UnitType::Conscript,9.5f,40.5f);
        move(w,actor,18.5f,40.5f);accepted &= w.ents[actor].attackMove.active;
        if(air)w.ents[actor].state=UState::Circling;
        bool fought=air,reached=false,budget=true,equal=false,immediate=!air;
        for(int i=0;i<900 && w.valid(actor);++i){w.update();budget &= w.repathsThisTick<=8;
            fought |= w.ents[actor].target==target;
            if(air && !i)immediate=w.repathsThisTick==8 && w.ents[actor].state==UState::AttackMoving;
            if(i==10)equal=resume(w,20);
            if(at(w,actor,18.5f,40.5f) && !w.ents[actor].attackMove.active){reached=true;break;}}
        int tried=0;for(EID id:blocked)tried+=w.ents[id].attackMove.retryAt>0;
        std::printf("ATTACK_MOVE FAIRNESS kind=%d blocked=%d tried=%d reached=%d immediate=%d budget=%d tick=%llu\n",
            (int)kind,count,tried,reached,immediate,budget,(unsigned long long)w.tick);
        check(accepted && fought && (air || !w.valid(target)) && reached && budget && equal && immediate && tried==count,
            air?"air resumes immediately with a full ground A* budget and keeps its saved destination":
            "0/239/270 disconnected jobs cannot starve a later actual combat-resume order; the rotating schedule survives save/load");
    }
    {
        World w;fresh(w);w.spawnBuilding(0,BldType::AirForceCmd,4,29,true);
        const EID actor=w.spawnUnit(0,UnitType::Intruder,5.5f,15.5f),target=w.spawnUnit(1,UnitType::Conscript,12.5f,15.5f);
        move(w,actor,35.5f,15.5f);World b;bool saved=false,equal=true,returned=false,landed=false,arrived=false;
        for(int i=0;i<1400 && w.valid(actor);++i){
            if(!saved && w.ents[actor].state==UState::Returning){saved=true;equal=clone(w,b);}
            w.update();if(saved && equal){b.update();equal=w.checksum()==b.checksum();}
            if(!w.valid(actor))break;returned |= w.ents[actor].state==UState::Returning;landed |= w.ents[actor].state==UState::Landed;
            if(at(w,actor,35.5f,15.5f) && !w.ents[actor].attackMove.active){arrived=true;break;}
        }
        check(saved && equal && returned && landed && arrived && !w.valid(target) && !w.ents[actor].attackMove.active,
            "a finite-ammo fighter keeps its destination through a real return, full reload and saved continuation");
    }
    {
        World w;fresh(w);const EID actor=w.spawnUnit(0,UnitType::Grizzly,5.5f,15.5f);
        move(w,actor,35.5f,15.5f);move(w,actor,37.5f,30.5f,true,true);
        const auto generation=w.ents[actor].generation;w.changeActorOwner(actor,1);World saved;
        check(w.ents[actor].player==1 && w.ents[actor].generation==generation
            && !w.ents[actor].attackMove.active && w.ents[actor].wps.empty() && clone(w,saved),
            "the ownership service preserves identity and produces an immediately saveable handover");
        World::Cmd owned;owned.type=World::Cmd::Move;owned.ids={actor};owned.x=35.5f;owned.y=30.5f;owned.attackMove=true;w.applyCmd(1,owned);
        check(w.ents[actor].attackMove.active && w.ents[actor].attackMove.owner==1,
            "the receiving player can issue a fresh retained command after handover");
    }
    for(int kind=0;kind<3;++kind) {
        World w;fresh(w);const EID actor=w.spawnUnit(0,UnitType::Grizzly,5.5f,15.5f);
        const EID controller=kind<2?w.spawnUnit(1,kind==0?UnitType::Yuri:UnitType::MasterMind,8.5f,15.5f)
            :w.spawnBuilding(1,BldType::PsychicTower,8,14,true);
        const EID power=kind==2?w.spawnBuilding(1,BldType::PowerPlant,30,5,true):INVALID_EID;
        move(w,actor,35.5f,15.5f);move(w,actor,37.5f,30.5f,true,true);World::Cmd c;c.type=World::Cmd::Attack;c.ids={controller};c.a=actor;w.applyCmd(1,c);
        for(int i=0;i<200 && w.valid(actor) && w.ents[actor].player==0;++i)w.update();
        const bool acquired=w.valid(actor) && w.ents[actor].player==1 && w.ents[actor].mindBy==controller;
        const bool taken=acquired && !w.ents[actor].attackMove.active && w.ents[actor].wps.empty();
        World controlSnapshot;const bool capturedSave=clone(w,controlSnapshot);
        if(!taken || !capturedSave)std::printf("ATTACK_MOVE CONTROL kind=%d acquired=%d actor_owner=%d intent=%d intent_owner=%d waypoints=%zu saveable=%d\n",
            kind,acquired,w.ents[actor].player,w.ents[actor].attackMove.active,w.ents[actor].attackMove.owner,w.ents[actor].wps.size(),capturedSave);
        check(acquired,"unit and tower controllers acquire a real attack-moving target");
        check(taken && capturedSave,"control acquisition clears the previous player's route before an immediate save");
        World::Cmd owned;owned.type=World::Cmd::Move;owned.ids={actor};owned.x=35.5f;owned.y=30.5f;owned.attackMove=true;w.applyCmd(1,owned);
        const bool newOwnerOrder=w.valid(actor) && w.ents[actor].attackMove.active && w.ents[actor].attackMove.owner==1;
        if(kind==2){w.kill(power,false);w.update();}
        else{w.kill(controller,false);for(int i=0;i<10;++i)w.update();}
        World releaseSnapshot;const bool releasedSave=clone(w,releaseSnapshot);
        if(!releasedSave)std::printf("ATTACK_MOVE RELEASE kind=%d actor_owner=%d intent=%d intent_owner=%d waypoints=%zu saveable=%d\n",
            kind,w.ents[actor].player,w.ents[actor].attackMove.active,w.ents[actor].attackMove.owner,w.ents[actor].wps.size(),releasedSave);
        check(newOwnerOrder && w.valid(actor) && w.ents[actor].player==0 && !w.ents[actor].attackMove.active && w.ents[actor].wps.empty() && releasedSave,
            "controller death or tower power loss clears the released player's route before an immediate save");
    }
    {
        World w;fresh(w);const EID actor=w.spawnUnit(0,UnitType::Grizzly,5.5f,15.5f);move(w,actor,35.5f,15.5f);move(w,actor,37.5f,30.5f,true,true);
        const auto before=w.ents[actor];std::vector<int> blocked;
        for(int dy=-4;dy<=4;++dy)for(int dx=-4;dx<=4;++dx)if(dx || dy){const int x=5+dx,y=15+dy;if(w.map.inBounds(x,y)){blocked.push_back(w.cellIdx(x,y));w.map.at(x,y).blocked=true;}}
        World::Cmd scatter;scatter.type=World::Cmd::Scatter;scatter.ids={actor};w.applyCmd(0,scatter);const auto& e=w.ents[actor];
        check(e.attackMove==before.attackMove && e.wps==before.wps && e.path==before.path && e.target==before.target && e.state==before.state,
            "a scatter with no legal pullout preserves its old route, target and queued modes");
        for(int cell:blocked)w.map.cells[cell].blocked=false;w.applyCmd(0,scatter);
        check(!w.ents[actor].attackMove.active && w.ents[actor].wps.empty() && w.ents[actor].state==UState::Moving,
            "a successful scatter replaces the old route only after finding a legal destination");
    }
    {
        World w;fresh(w);const EID actor=w.spawnUnit(0,UnitType::Tanya,5.5f,15.5f),boat=w.spawnUnit(0,UnitType::AmphTransport,8.5f,20.5f);
        move(w,actor,35.5f,15.5f);move(w,actor,37.5f,30.5f,false,true);World::Cmd c;c.type=World::Cmd::Board;c.ids={actor};c.a=boat;w.applyCmd(0,c);
        const bool cancelled=!w.ents[actor].attackMove.active && w.ents[actor].wps.empty();
        for(int i=0;i<500 && w.ents[boat].cargo.empty();++i)w.update();const bool boarded=!w.ents[boat].cargo.empty();
        c.type=World::Cmd::Unload;c.ids={boat};w.applyCmd(0,c);bool released=false;
        for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.utype==UnitType::Tanya)released |= !e.attackMove.active && e.wps.empty();
        check(cancelled && boarded && released,"a real board/unload cycle consumes the old order and releases the passenger without a stale route");
        const EID gi=w.spawnUnit(0,UnitType::GI,15.5f,15.5f);move(w,gi,35.5f,15.5f);c.type=World::Cmd::RadDeploy;c.ids={gi};w.applyCmd(0,c);
        check(w.ents[gi].deployed && !w.ents[gi].attackMove.active && w.ents[gi].wps.empty(),"successful infantry deployment cancels the retained movement order immediately");
    }
    {
        World w;fresh(w);const EID actor=w.spawnUnit(0,UnitType::Tanya,5.5f,15.5f);w.spawnUnit(1,UnitType::Conscript,12.5f,15.5f);
        move(w,actor,35.5f,15.5f);w.update();move(w,actor,37.5f,30.5f,false,true);
        FILE* f=tmpfile();std::vector<uint8_t> bytes;bool encoded=f && w.saveGame(f);
        if(encoded){fseek(f,0,SEEK_END);const auto size=ftell(f);rewind(f);if(size>0){bytes.resize((size_t)size);encoded=fread(bytes.data(),1,bytes.size(),f)==bytes.size();}}
        if(f)fclose(f);
        const auto section=locateWorldSaveSection(bytes,WorldSaveSection::AttackMove);
        const size_t start=section?section->offset:bytes.size();
        const auto rejects=[&](const std::vector<uint8_t>& bad){FILE* file=tmpfile();World broken;bool ok=file && fwrite(bad.data(),1,bad.size(),file)==bad.size();
            if(ok){rewind(file);ok=!broken.loadGame(file);}if(file)fclose(file);return ok;};
        bool rejected=encoded && section && section->length>=35 && start+35<=section->footer;
        const auto badAt=[&](size_t offset,const auto& value){auto bad=bytes;if(offset+sizeof(value)>bad.size())return false;memcpy(bad.data()+offset,&value,sizeof(value));return rejects(bad);};
        if(rejected){rejected &= badAt(start,uint32_t{0xffffffff}) && badAt(start+4,int{-1}) && badAt(start+8,uint8_t{2})
            && badAt(start+9,int{1}) && badAt(start+13,std::numeric_limits<float>::quiet_NaN()) && badAt(start+21,uint8_t{2})
            && badAt(start+22,w.tick+LOGIC_FPS+1) && badAt(start+30,uint32_t{2}) && badAt(start+34,int8_t{2}) && badAt(section->footer,uint32_t{0});
            auto truncated=bytes;truncated.pop_back();rejected &= rejects(truncated);auto trailing=bytes;trailing.push_back(0);rejected &= rejects(trailing);}
        check(rejected,"invalid order counts, IDs, flags, owners, coordinates, layers, retries, queue modes, footer lengths and truncated/trailing data are rejected");
        world=w;world.map.bldOccRef=&world.bldOcc;fogKnowledge={};syncFogKnowledge();
        const auto live=(std::filesystem::path(outputDir)/"combat-queued.sav").string();check(saveGameFile(live.c_str()),"the current in-combat queued application checkpoint saves");
        std::ifstream input(live,std::ios::binary);std::vector<char> app((std::istreambuf_iterator<char>(input)),{});const auto before=world.checksum();
        const auto badFile=(std::filesystem::path(outputDir)/"bad-order.sav").string();bool transaction=app.size()>4;
        if(transaction){app.pop_back();{std::ofstream output(badFile,std::ios::binary);output.write(app.data(),app.size());}transaction=!loadGameFile(badFile.c_str()) && world.checksum()==before;}
        check(transaction && loadGameFile(live.c_str()) && world.checksum()==before,"malformed application order state leaves the live match intact and a valid active queue restores exactly");
    }
    // Explicit combat fixtures on the original bridge geometry. The source
    // mission graph is not started and other actors are removed here.
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(!scene.empty() && FileExists(scene.c_str())) {
        World native;const bool loaded=native.init(64,64,47,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true) && native.loadNativeScene(scene.c_str());
        check(loaded,"the actual source bridge geometry and conventional weapon definitions load");
        if(loaded) {
            for(EID id=0;id<(EID)native.ents.size();++id)if(native.valid(id) && native.ents[id].nativeIdentity.definition->kind!=3)native.kill(id,false);
            const int tanyaType=native.nativeUnitIndex("TANY"),dredType=native.nativeUnitIndex("DRED");
            for(int kind=0;kind<2 && tanyaType>=0 && dredType>=0;++kind) {
                World w=native;w.map.bldOccRef=&w.bldOcc;
                const EID actor=kind?w.spawnNativeActor(tanyaType,0,62.5f,58.5f,256,0,"attack-move:source-tanya","","Guard"):
                    w.spawnUnit(0,UnitType::Tanya,62.5f,58.5f);
                if(!w.valid(actor)){check(false,"the amphibious bridge-fixture actor spawns");continue;}
                // Specify the starting surface, without changing coordinates.
                w.ents[actor].navLayer=w.ents[actor].previousNavLayer=w.ents[actor].goalLayer=0;w.unitOccSync(actor);
                const EID target=kind?w.spawnNativeActor(dredType,2,65.5f,58.5f,256,0,"attack-move:source-dred","","Sleep"):
                    w.spawnUnit(1,UnitType::Conscript,64.5f,56.5f);
                const float gx=kind?59.5f:62.5f,gy=58.5f;const int layer=kind?0:1;
                move(w,actor,gx,gy,true,false,layer+1);
                bool fought=false,saved=false,restored=true,reached=false;
                for(int i=0;i<5000 && w.valid(actor);++i){w.update();fought |= w.ents[actor].target==target;
                    if(fought && !saved){saved=true;
                        if(kind){world=w;world.map.bldOccRef=&world.bldOcc;fogKnowledge={};syncFogKnowledge();const auto path=(std::filesystem::path(outputDir)/"native-c4-combat.sav").string();
                            check(saveGameFile(path.c_str()),"the actual original-rule Tanya C4 fight saves with its retained destination");}
                        restored=resume(w,20);
                    }
                    if(at(w,actor,gx,gy) && w.ents[actor].navLayer==layer && !w.ents[actor].attackMove.active){reached=true;break;}
                }
                std::printf("ATTACK_MOVE NATIVE kind=%d fought=%d target_dead=%d reached=%d layer=%d pos=%.3f,%.3f tick=%llu\n",kind,fought,!w.valid(target),reached,w.valid(actor)?w.ents[actor].navLayer:-1,w.valid(actor)?w.ents[actor].x:0,w.valid(actor)?w.ents[actor].y:0,(unsigned long long)w.tick);
                check(fought && saved && restored && !w.valid(target) && reached,kind?
                    "original Tanya's real automatic C4 attack on a native DRED resumes the same water destination after ship destruction":
                    "combat below the original bridge resumes to the explicitly requested upper surface of the same cell");
            }
            int hutType=-1;for(int i=0;i<(int)native.nativeTypes.size();++i)if(native.nativeTypes[i]->id=="CABHUT")hutType=i;
            if(hutType>=0 && tanyaType>=0){
                World w=native;w.map.bldOccRef=&w.bldOcc;
                const EID actor=w.spawnNativeActor(tanyaType,0,62.5f,56.5f,256,0,"attack-move:immune-probe","","Guard");
                const EID hut=w.spawnNativeActor(hutType,1,65,56,256,0,"attack-move:immune-hut","","Guard");
                const bool immune=w.valid(hut) && w.ents[hut].nativeIdentity.definition->immune;
                move(w,actor,62.5f,61.5f,true,false,1);bool selected=false;
                const auto retained=w.checksum();const auto rejected=w.orderAttack({actor},hut);
                check(!rejected.accepted && rejected.reason==AttackRejectReason::Ineffective && w.checksum()==retained,
                    "an explicit ineffective C4 order preserves the existing route to pass the original immune hut");
                for(int i=0;i<1600 && w.valid(actor);++i){w.update();selected |= w.ents[actor].target==hut;if(at(w,actor,62.5f,61.5f) && !w.ents[actor].attackMove.active)break;}
                std::printf("ATTACK_MOVE IMMUNE immune=%d selected=%d reached=%d\n",immune,selected,at(w,actor,62.5f,61.5f));
                check(immune && !selected && at(w,actor,62.5f,61.5f) && w.valid(hut),"an original immune repair hut is not repeatedly acquired while an attack-move passes it");
                const EID eraser=w.spawnUnit(0,UnitType::Chrono,62.5f,56.5f);
                const auto erasing=w.orderAttack({eraser},hut);for(int i=0;i<30;++i)w.update();
                check(erasing.accepted==1 && w.valid(hut) && w.ents[hut].chrono>0,
                    "damage immunity does not disable the existing non-damage chrono erasure effect");
            }
        }
    } else std::printf("ATTACK_MOVE NOTE: optional original geometry/weapon fixtures are not installed\n");
    for(const char* old:{"review_out/native-campaign/legacy-world46-attackmove.sav","review_out/native-campaign/legacy-world46-attackmove-native-victory.sav"})if(FileExists(old)) {
        const bool loaded=loadGameFile(old);bool empty=loaded;
        if(loaded)for(const auto& e:world.ents){empty &= !e.attackMove.active;for(const auto& p:e.wps)empty &= p.attackMode==-1;}
        check(empty && resume(world,40),"an actual World46 generic/native application save loads with historical intent defaults and continues deterministically");
    }
    std::printf("ATTACK_MOVE failures=%d\n",failures);return failures;
}
