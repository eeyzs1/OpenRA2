#include "game_regression.h"
#include "game/world_priv.h"
#include <cstdio>
#include <filesystem>
#include <set>

int GameRegression::burstRegression() {
    int failures=0;
    auto check=[&](bool ok,const char* label){failures+=!ok;std::printf("BURST %s %s\n",ok?"PASS":"FAIL",label);};
    auto reset=[](World& w) {
        w.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        w.crates.clear();w.cratesEnabled=false;w.aiAlliance=false;
        for(auto& player:w.players){player.defeated=false;player.isAI=false;}
        w.map.reveal(0,32,32,64);w.map.reveal(1,32,32,64);
    };
    auto attack=[](World& w,EID actor,EID target) {
        World::Cmd cmd{};cmd.type=World::Cmd::Attack;cmd.ids={actor};cmd.a=target;w.applyCmd(0,cmd);
    };
    auto shots=[](const World& w,EID actor) {
        int result=0;for(const auto& event:w.presentationEvents)
            result+=event.kind==PresentationKind::Muzzle && event.entity==actor;
        return result;
    };
    std::set<int> observedGaps;
    for(int mode=0;mode<3;++mode) {
        World w;reset(w);const EID actor=w.spawnUnit(0,UnitType::Apocalypse,20.5f,20.5f);
        const EID target=w.spawnUnit(1,mode==2?UnitType::Kirov:UnitType::MCV,24.5f,20.5f);
        w.ents[actor].vetRank=mode==1?2:0;w.ents[actor].turretDir=dirFromVec(4,0);
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;attack(w,actor,target);attack(replay,actor,target);
        const int count=mode==1?4:2;const auto weapon=w.effWeapon(w.ents[actor],&w.ents[target]);
        int projectileCount=0;bool physical=true;
        std::vector<int> ticks;bool equal=w.checksum()==replay.checksum(),saved=false,hashed=false;
        for(int frame=0;frame<weapon.cooldown+20;++frame) {
            w.presentationEvents.clear();replay.presentationEvents.clear();w.update();replay.update();
            equal &= w.checksum()==replay.checksum();
            int born=0;
            for(const auto& p:w.projs)if(p.src==actor && p.trail==1) {
                ++born;physical &= p.w.damage==weapon.damage && p.w.range==weapon.range
                    && p.kind==(mode==2?ProjKind::Missile:ProjKind::Shell);
            }
            projectileCount+=born;physical &= born==shots(w,actor);
            for(int n=shots(w,actor);n>0;--n)ticks.push_back((int)w.tick);
            if(frame==0) {
                const auto hash=w.checksum();auto& e=w.ents[actor];const int delay=e.burstDelay;
                ++e.burstDelay;hashed=w.checksum()!=hash;e.burstDelay=delay;
                FILE* file=std::tmpfile();
                if(file){saved=w.saveGame(file) && std::fseek(file,0,SEEK_SET)==0 && replay.loadGame(file)
                    && hash==w.checksum() && hash==replay.checksum() && replay.ents[actor].burstRemaining==count-1;std::fclose(file);}
            }
        }
        bool cadence=ticks.size()==(size_t)count*2;
        for(size_t n=1;n<ticks.size();++n) {
            if(n%(size_t)count==0)cadence &= ticks[n]-ticks[n-count]==weapon.cooldown;
            else {const int gap=ticks[n]-ticks[n-1];observedGaps.insert(gap);cadence &= gap>=3 && gap<=5;}
        }
        check(cadence && equal && saved && hashed && physical && projectileCount==count*2,"ordinary/elite/air count cadence replay and mid-burst snapshot");
        std::printf("BURST_TRACE mode=%d count=%zu checks=%d ticks=",mode,ticks.size(),weapon.cooldown+20);
        for(int tick:ticks)std::printf("%d,",tick);std::printf(" saved=%d replay=%d projectiles=%d physical=%d\n",saved,equal,projectileCount,physical);
    }
    check(observedGaps.size()>1,"default intra-volley delay varies within simulation RNG bounds");
    for(int scenario=0;scenario<12;++scenario) {
        World w;reset(w);EID actor=w.spawnUnit(0,UnitType::Apocalypse,20.5f,20.5f);
        EID target=w.spawnUnit(1,scenario==10?UnitType::SiegeChopper:UnitType::MCV,24.5f,20.5f);
        w.ents[actor].turretDir=dirFromVec(4,0);attack(w,actor,target);w.update();
        const bool started=shots(w,actor)==1 && w.ents[actor].burstRemaining==1;
        const int delay=w.ents[actor].burstDelay;
        World::Cmd cmd{};cmd.ids={actor};
        if(scenario==0){cmd.type=World::Cmd::Stop;w.applyCmd(0,cmd);}
        if(scenario==1){cmd.type=World::Cmd::Move;cmd.x=10;cmd.y=20;w.applyCmd(0,cmd);}
        if(scenario==2){EID next=w.spawnUnit(1,UnitType::MCV,23.5f,24.5f);attack(w,actor,next);}
        if(scenario==3)attack(w,actor,INVALID_EID); // rejected command preserves the burst
        bool reused=true;
        if(scenario==4){w.kill(target,false);reused=w.spawnUnit(1,UnitType::MCV,24.5f,20.5f)==target;}
        if(scenario==5){w.kill(actor,false);reused=w.spawnUnit(0,UnitType::MCV,20.5f,20.5f)==actor;}
        if(scenario==6)w.ents[actor].player=1;
        if(scenario==7)w.ents[actor].tpSick=12;
        if(scenario==8)w.ents[target].x=40.5f;
        if(scenario==9)w.ents[target].player=0;
        if(scenario==10){w.ents[target].deployed=true;w.ents[target].state=UState::Idle;}
        if(scenario==11)w.ents[actor].chrono=12;
        int emitted=0;bool paused=true;
        const int frozenFrames=scenario==7?12:scenario==11?8:0;
        for(int frame=0;frame<frozenFrames;++frame) {
            w.presentationEvents.clear();w.update();emitted+=shots(w,actor);
            paused &= w.ents[actor].burstRemaining==1 && w.ents[actor].burstDelay==delay;
        }
        if(scenario==11)w.ents[actor].chrono=0;
        for(int frame=0;frame<10;++frame){w.presentationEvents.clear();w.update();emitted+=shots(w,actor);}
        const bool continued=scenario==3 || scenario==7 || scenario==11;
        bool ok=started && reused && paused && emitted==(continued?1:0) && w.ents[actor].burstRemaining==0;
        check(ok,"orders death slot reuse ownership range airborne switch and immobilization");
        std::printf("BURST_CANCEL scenario=%d started=%d reused=%d emitted=%d expected=%d paused=%d remaining=%d\n",
            scenario,started,reused,emitted,continued?1:0,paused,w.ents[actor].burstRemaining);
    }
    {
        World w;reset(w);const EID actor=w.spawnUnit(0,UnitType::Apocalypse,20.5f,20.5f);
        const EID target=w.spawnUnit(1,UnitType::MCV,24.5f,20.5f);w.ents[actor].turretDir=dirFromVec(4,0);
        attack(w,actor,target);w.update();
        const int remaining=w.ents[actor].burstRemaining,delay=w.ents[actor].burstDelay;
        w.kill(actor,false);w.kill(target,false);
        const auto roundTrip=[&]() {
            World restored;FILE* file=std::tmpfile();bool equal=file && w.saveGame(file);
            if(equal){std::rewind(file);equal=restored.loadGame(file) && w.checksum()==restored.checksum();}
            if(file)std::fclose(file);
            for(int frame=0;frame<30 && equal;++frame){w.update();restored.update();equal=w.checksum()==restored.checksum();}
            return equal;
        };
        check(remaining>0 && !w.ents[actor].burstRemaining && w.ents[actor].burstTarget==INVALID_EID && roundTrip(),
            "retiring a mid-volley attacker without slot reuse leaves an immediately resumable save after its target also dies");
        // Earlier World writers retained these inert fields on a dead actor.
        // Preserve the exact historical bytes/checksum when reading that state.
        auto& retired=w.ents[actor];retired.burstRemaining=remaining;retired.burstDelay=delay;
        retired.burstOwner=0;retired.burstTarget=target;
        check(roundTrip(),"a historical dead actor may retain an inert volley referencing a retired in-range entity slot");
        retired.burstTarget=(EID)w.ents.size()+1;FILE* file=std::tmpfile();World guard=w;
        guard.map.bldOccRef=&guard.bldOcc;const auto hash=guard.checksum();
        bool rejected=file && w.saveGame(file);
        if(rejected){std::rewind(file);rejected=!guard.loadGame(file) && hash==guard.checksum();}
        if(file)std::fclose(file);
        check(rejected,"historical dead volleys still reject out-of-range references without changing the destination world");
    }
    auto& primary=const_cast<UnitDef&>(unitDef(UnitType::Apocalypse)).weapon;
    const WeaponDef original=primary;
    for(int single=0;single<2;++single) {
        primary.burst=single?1:2;primary.burstDelayMin=primary.burstDelayMax=7;
        World w;reset(w);const EID actor=w.spawnUnit(0,UnitType::Apocalypse,20.5f,20.5f);
        const EID target=w.spawnUnit(1,UnitType::MCV,24.5f,20.5f);w.ents[actor].turretDir=dirFromVec(4,0);
        attack(w,actor,target);const auto random=w.rng.s;std::vector<int> ticks;
        for(int frame=0;frame<100;++frame){w.presentationEvents.clear();w.update();for(int n=shots(w,actor);n>0;--n)ticks.push_back((int)w.tick);}
        check(single?ticks==std::vector<int>{1,81} && w.rng.s==random:ticks==std::vector<int>{1,8,81,88},
            "configured fixed interval and single-shot fallback preserve cadence");
        primary=original;
    }
    for(int invalid=0;invalid<5;++invalid) {
        World w;reset(w);const EID actor=w.spawnUnit(0,UnitType::Apocalypse,20.5f,20.5f);
        const EID target=w.spawnUnit(1,UnitType::MCV,24.5f,20.5f);w.ents[actor].turretDir=dirFromVec(4,0);
        attack(w,actor,target);w.update();auto& pending=w.ents[actor];
        if(invalid==0)pending.burstRemaining=16;
        if(invalid==1)pending.burstDelay=601;
        if(invalid==2)pending.burstOwner=w.numPlayers;
        if(invalid==3)pending.burstTarget=INVALID_EID;
        if(invalid==4){pending.cancelBurst();pending.burstDelay=1;}
        bool rejected=false;World loaded;FILE* file=std::tmpfile();
        if(file){rejected=w.saveGame(file) && std::fseek(file,0,SEEK_SET)==0 && !loaded.loadGame(file);std::fclose(file);}
        check(rejected,"snapshot rejects malformed pending-burst state");
    }
    std::filesystem::create_directories("review_out");exportRules("review_out/burst-roundtrip.ini");
    loadRules("review_out/burst-roundtrip.ini");const auto& d=unitDef(UnitType::Apocalypse);
    check(d.weapon.burst==2 && d.elite && d.elite->burst==4 && d.airWeapon.burst==2
        && d.weapon.burstDelayMin==3 && d.weapon.burstDelayMax==5,"rules export and reload preserve burst parameters");
    {
        World w;
        const bool loaded=w.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"maps/skirmish-reference.txt",true,true,
            "assets/rules/profiles/ra2.ini",{Country::Russia,Country::America});
        check(loaded,"the timed ground burst fixture loads its rules and map");
        if(!loaded)return failures;
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& player:w.players)player.isAI=false;
        w.cratesEnabled=false;w.crates.clear();
        w.spawnBuilding(0,BldType::ConYard,3,3,true);
        w.spawnBuilding(1,BldType::ConYard,50,50,true);
        const EID gunner=w.spawnUnit(0,UnitType::Apocalypse,20.5f,20.5f);
        const EID mover=w.spawnUnit(1,UnitType::IFV,21.5f,20.5f);
        if(!w.valid(gunner) || !w.valid(mover))return failures+1;
        w.ents[gunner].turretDir=0;
        auto& target=w.ents[mover];
        target.dir=target.turretDir=3;target.vetRank=2;target.crateSpeedBoost=300;
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={mover};move.x=21.5f;move.y=32.5f;
        w.bindCommand(move);w.applyCmd(1,move);
        w.map.reveal(0,20,20,18);
        World::Cmd attackOrder{};attackOrder.type=World::Cmd::Attack;attackOrder.ids={gunner};attackOrder.a=mover;
        w.bindCommand(attackOrder);w.applyCmd(0,attackOrder);
        const bool admitted=target.state==UState::Moving && w.ents[gunner].target==mover && w.usesNativeGround(w.ents[gunner]);
        World replay;
        bool waited=false,saved=false,exact=true;
        int first=0,second=0,error=0;
        for(int elapsed=0;elapsed<90 && !second && w.valid(gunner) && w.valid(mover);++elapsed) {
            const auto& actor=w.ents[gunner];
            const auto timed=w.nativeTurretHeading(actor,w.tick+1);
            const bool continuing=actor.burstRemaining>0;
            const bool due=continuing && actor.burstDelay<=1;
            w.presentationEvents.clear();w.update();
            if(saved) {
                replay.presentationEvents.clear();replay.update();
                exact &= w.checksum()==replay.checksum();
            }
            for(const auto& event:w.presentationEvents)if(event.kind==PresentationKind::Muzzle && event.entity==gunner) {
                if(!continuing)first=(int)w.tick;
                else {
                    second=(int)w.tick;
                    const auto wanted=NativeGroundMath::vector(event.targetX-event.x,event.targetY-event.y);
                    error=std::abs(NativeGroundMath::delta(timed,wanted));
                }
            }
            if(due && !second && w.ents[gunner].burstRemaining==1 && w.ents[gunner].burstDelay==0) {
                waited=true;
                if(!saved) {
                    FILE* snapshot=std::tmpfile();
                    if(snapshot) {
                        saved=w.saveGame(snapshot) && std::fseek(snapshot,0,SEEK_SET)==0 && replay.loadGame(snapshot)
                            && w.checksum()==replay.checksum();
                        std::fclose(snapshot);
                    }
                }
            }
        }
        std::printf("BURST_NATIVE_FACING admitted=%d first=%d second=%d error=%d waited=%d saved=%d replay=%d\n",
            admitted,first,second,error,waited,saved,exact);
        check(admitted && first>0 && second>first && target.y>20.5f && error<=8192 && waited && saved && exact,
            "a timed ground follow-up waits for the real gun angle and replays from the ready burst state");
    }
    return failures;
}
