#include "game/game.h"
#include <cstdio>

int Game::instantCombatRegression() {
    int failures=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("INSTANT %s %s\n",ok?"PASS":"FAIL",name); };
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;
        for(auto& player:world.players) { player.powerMade=10000;player.powerUsed=0; }
    };
    // Fire through the normal entity update, not a direct damage() surrogate.
    for(int kind=0;kind<4;++kind) {
        reset();
        EID shooter=kind<2?world.spawnUnit(0,kind==0?UnitType::PrismTank:UnitType::TeslaTank,17.5f,20.5f)
            :world.spawnBuilding(0,kind==2?BldType::PrismTower:BldType::TeslaCoil,16,20,true);
        EID house=world.spawnBuilding(1,BldType::CivHouse,20,20,true);
        world.ents[house].hp=bldDef(BldType::CivHouse).hp/2+1;
        world.ents[house].garrison={{UnitType::GI,67,2,400,1},{UnitType::GI,68,3,500,1}};
        EID chain=world.spawnUnit(1,UnitType::MCV,22.5f,20.5f);
        world.ents[chain].atkCd=1000;
        const int oldHp=world.ents[chain].hp;
        world.ents[shooter].target=house;world.ents[shooter].state=UState::Attacking;
        world.ents[shooter].constructAnim=0;
        world.ents[shooter].dir=world.ents[shooter].turretDir=dirFromVec(
            20+bldDef(BldType::CivHouse).w/2.f-world.ents[shooter].x,
            20+bldDef(BldType::CivHouse).h/2.f-world.ents[shooter].y);
        for(auto& player:world.players) { player.powerMade=10000;player.powerUsed=0; }
        world.ents.shrink_to_fit();
        const size_t oldSize=world.ents.size();
        const World::Ent* address=&world.ents[shooter];
        world.update();
        std::printf("INSTANT_STATE kind=%d actor=%d stable=%d cd=%d house=%d hp=%d owner=%d crew=%zu size=%zu old=%zu chain=%d/%d\n",
            kind,world.valid(shooter),address==&world.ents[shooter],world.ents[shooter].atkCd,world.valid(house),
            world.ents[house].hp,world.ents[house].player,world.ents[house].garrison.size(),world.ents.size(),oldSize,world.ents[chain].hp,oldHp);
        check(world.valid(shooter) && address==&world.ents[shooter] && world.ents[shooter].atkCd>0
            && world.valid(house) && world.ents[house].garrison.empty() && world.ents[house].player==-1
            && world.ents.size()==oldSize+2,kind==0?"prism tank evacuation keeps actor and cooldown stable":
            kind==1?"tesla tank evacuation keeps actor and cooldown stable":kind==2?
            "prism tower evacuation keeps actor and cooldown stable":"tesla coil evacuation keeps actor and cooldown stable");
        if(kind==0) check(world.ents[chain].hp<oldHp,"prism chain still damages the adjacent enemy after evacuation");
        bool deferred=true;
        for(size_t id=oldSize;id<world.ents.size();++id)
            deferred &= world.ents[id].atkCd==0 && world.ents[id].fireAnim==0;
        check(deferred,"evacuated units first act on the following entity-update phase");
        FILE* file=tmpfile();World restored;bool replay=file && world.saveGame(file);
        if(replay) { rewind(file);replay=restored.loadGame(file); }
        if(file) fclose(file);
        for(int frame=0;frame<180 && replay;++frame) {
            world.update();restored.update();replay=world.checksum()==restored.checksum();
        }
        check(replay,"instant-hit evacuation save-resume matches for 180 ticks");
    }
    reset();
    EID shooter=world.spawnUnit(0,UnitType::PrismTank,19.5f,21.5f);
    EID reactor=world.spawnBuilding(1,BldType::NuclearReactor,20,20,true);
    EID house=world.spawnBuilding(1,BldType::CivHouse,23,20,true);
    world.ents[house].hp=bldDef(BldType::CivHouse).hp/2+1;
    world.ents[house].garrison={{UnitType::GI,67,2,400,1},{UnitType::GI,68,3,500,1}};
    world.ents[reactor].hp=1;world.ents[shooter].hp=1;
    world.ents[shooter].target=reactor;world.ents[shooter].state=UState::Attacking;
    world.ents[shooter].dir=world.ents[shooter].turretDir=dirFromVec(
        20+bldDef(BldType::NuclearReactor).w/2.f-world.ents[shooter].x,
        20+bldDef(BldType::NuclearReactor).h/2.f-world.ents[shooter].y);
    world.update();
    bool retired=!world.valid(shooter) && !world.valid(reactor) && world.valid(house)
        && world.ents[house].garrison.empty() && world.ents.size()>=5
        && world.ents[shooter].utype==UnitType::PrismTank;
    check(retired,"reactor retaliation cannot recycle the firing actor into an evacuated passenger within the same tick");
    // All retired slots become available again at the boundary and can be saved.
    FILE* file=tmpfile();World restored;bool saved=file && world.saveGame(file);
    if(saved) { rewind(file);saved=restored.loadGame(file); }
    if(file) fclose(file);
    EID next=world.spawnUnit(0,UnitType::Engineer,40.5f,40.5f);
    EID nextRestored=saved?restored.spawnUnit(0,UnitType::Engineer,40.5f,40.5f):INVALID_EID;
    check(saved && next==nextRestored && (size_t)next<5 && world.checksum()==restored.checksum(),
        "retired slots are reusable after the tick and retain save-resume allocation order");
    reset();shooter=world.spawnUnit(0,UnitType::PrismTank,19.5f,21.5f);
    reactor=world.spawnBuilding(1,BldType::NuclearReactor,20,20,true);
    world.ents[reactor].hp=1;world.ents[shooter].hp=1;
    world.orderMove({shooter},17,21,true);
    world.ents[shooter].target=reactor;
    world.ents[shooter].dir=world.ents[shooter].turretDir=dirFromVec(
        20+bldDef(BldType::NuclearReactor).w/2.f-world.ents[shooter].x,
        20+bldDef(BldType::NuclearReactor).h/2.f-world.ents[shooter].y);
    world.update();
    check(!world.valid(shooter) && !world.valid(reactor) && world.ents[shooter].x==19.5f
        && world.ents[shooter].y==21.5f && world.ents[shooter].atkCd==0,
        "attack-moving actor killed by retaliation cannot move or receive a firing cooldown afterward");
    return failures;
}
