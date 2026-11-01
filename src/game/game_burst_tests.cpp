#include "game/game.h"
#include "game/world_priv.h"
#include <cstdio>
#include <filesystem>
#include <set>

int Game::burstRegression() {
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
    return failures;
}
