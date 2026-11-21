#include "game_regression.h"
#include "game/world_priv.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>

int GameRegression::apocalypseRegression() {
    int failures=0;
    auto check=[&](bool ok,const char* label){failures+=!ok;std::printf("APOCALYPSE %s %s\n",ok?"PASS":"FAIL",label);};
    auto reset=[](World& w) {
        w.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        w.crates.clear();w.cratesEnabled=false;w.aiAlliance=false;
        for(auto& player:w.players){player.defeated=false;player.isAI=false;}
        w.map.reveal(0,32,32,64);w.map.reveal(1,32,32,64);
    };
    World w;reset(w);
    EID actor=w.spawnUnit(0,UnitType::Apocalypse,20.5f,20.5f);
    EID ground=w.spawnUnit(1,UnitType::MCV,24.5f,20.5f);
    EID air=w.spawnUnit(1,UnitType::Kirov,27.5f,20.5f);
    auto cannon=w.effWeapon(w.ents[actor],&w.ents[ground]);
    auto missile=w.effWeapon(w.ents[actor],&w.ents[air]);
    check(std::abs(cannon.range-5.75f)<.001f && cannon.damage==100 && cannon.cooldown==80
        && !cannon.antiAir && cannon.antiGround && !std::strcmp(cannon.projSprite,"shell"),"ground cannon stats and fractional range");
    check(missile.range==8 && missile.damage==50 && missile.cooldown==80 && missile.antiAir
        && !missile.antiGround && !std::strcmp(missile.projSprite,"missile"),"independent anti-air missile stats");
    World::Ent dummy{};dummy.utype=UnitType::GI;
    check(std::abs(weaponMultiplier(cannon,dummy)-.25f)<.001f,"ground infantry armor multiplier");
    dummy.utype=UnitType::Rhino;
    check(std::abs(weaponMultiplier(cannon,dummy)-1.f)<.001f,"ground heavy armor multiplier");
    dummy.utype=UnitType::Intruder;
    check(std::abs(weaponMultiplier(missile,dummy)-.7f)<.001f,"anti-air light armor multiplier");
    w.ents[actor].vetRank=2;
    auto eliteGround=w.effWeapon(w.ents[actor],&w.ents[ground]);
    auto eliteAir=w.effWeapon(w.ents[actor],&w.ents[air]);
    dummy.utype=UnitType::GI;
    check(eliteGround.range==5.75f && !eliteGround.antiAir && !std::strcmp(eliteGround.projSprite,"shell")
        && weaponMultiplier(eliteGround,dummy)==1.f,"elite retains ground cannon and elite armor table");
    check(eliteAir.range==8 && eliteAir.antiAir && !eliteAir.antiGround
        && eliteAir.damage==(int)(50*g_gameRules.veteranismDmgBonus[2]),"elite retains anti-air secondary");
    check(unitDef(UnitType::Apocalypse).hasAntiAir() && !unitDef(UnitType::Rhino).hasAntiAir(),
        "AI capability includes secondary without arming ground-only tank");
    w.ents[actor].vetRank=0;
    // Retaining an air target must not hide nearby ground targets from the index scan.
    w.ents[actor].target=air;
    auto current=w.effWeapon(w.ents[actor]);
    check(w.findNearestEnemy(0,20.5f,20.5f,8,true,&current,UnitType::Apocalypse,actor)==ground,
        "secondary selection still scans ground occupancy");
    // At seven cells a ground target is closer than the aircraft, but out of
    // cannon range. Attack-moving must select the shootable aircraft instead.
    w.ents[ground].x=26.5f;w.unitOccSync(ground);
    check(w.findNearestEnemy(0,20.5f,20.5f,8,true,&current,UnitType::Apocalypse,actor,true)==air,
        "firing query filters each target by its selected weapon range");
    w.ents[ground].x=24.5f;w.unitOccSync(ground);
    // Explicit commands must use the selected target's weapon, including path range.
    World::Cmd cmd{};cmd.type=World::Cmd::Attack;cmd.ids={actor};cmd.a=air;
    auto before=w.checksum();World replay=w;replay.map.bldOccRef=&replay.bldOcc;
    w.applyCmd(0,cmd);replay.applyCmd(0,cmd);
    bool accepted=w.ents[actor].target==air && before!=w.checksum();
    const float initialX=w.ents[actor].x,initialY=w.ents[actor].y;
    bool equal=w.checksum()==replay.checksum(),saved=false,projectile=false;
    for(int tick=0;tick<220;++tick) {
        w.update();replay.update();
        equal &= w.checksum()==replay.checksum();
        for(const auto& p:w.projs)if(p.src==actor)projectile |= p.kind==ProjKind::Missile && p.w.damage==50 && p.w.range==8;
        if(tick==35) {
            auto hash=w.checksum();FILE* file=std::tmpfile();
            if(file){saved=w.saveGame(file) && std::fseek(file,0,SEEK_SET)==0 && replay.loadGame(file)
                && hash==w.checksum() && hash==replay.checksum();std::fclose(file);}
        }
    }
    std::printf("APOCALYPSE_OBS command accepted=%d projectile=%d damage=%lld x=%.4f y=%.4f target=%d state=%d\n",accepted,projectile,(long long)w.combatDamage[0],w.ents[actor].x,w.ents[actor].y,w.ents[actor].target,(int)w.ents[actor].state);
    check(accepted && projectile && w.combatDamage[0]>0 && w.ents[actor].x==initialX && w.ents[actor].y==initialY,
        "command fires missiles at seven cells without approaching");
    check(equal && saved,"independent commands replay through snapshot continuation");
    // An idle dual-weapon unit must discover aircraft outside the ground range.
    reset(w);actor=w.spawnUnit(0,UnitType::Apocalypse,20.5f,20.5f);
    air=w.spawnUnit(1,UnitType::Kirov,27.5f,20.5f);
    for(int tick=0;tick<220;++tick)w.update();
    std::printf("APOCALYPSE_OBS idle damage=%lld x=%.4f target=%d expected=%d state=%d\n",(long long)w.combatDamage[0],w.ents[actor].x,w.ents[actor].target,air,(int)w.ents[actor].state);
    check(w.ents[actor].target==air && w.combatDamage[0]>0 && w.ents[actor].x==20.5f,
        "idle acquisition discovers secondary-range aircraft");
    // Fractional boundary: reaching a 5.9-cell target requires approaching.
    reset(w);actor=w.spawnUnit(0,UnitType::Apocalypse,20.5f,20.5f);
    ground=w.spawnUnit(1,UnitType::MCV,26.4f,20.5f);cmd.ids={actor};cmd.a=ground;w.applyCmd(0,cmd);
    for(int tick=0;tick<100;++tick)w.update();
    std::printf("APOCALYPSE_OBS ground damage=%lld x=%.4f targetx=%.4f state=%d\n",(long long)w.combatDamage[0],w.ents[actor].x,w.ents[ground].x,(int)w.ents[actor].state);
    check(w.ents[actor].x>20.5f && w.combatDamage[0]>0,"ground target at 5.9 cells requires approach");
    w.ents[ground].x=w.ents[actor].x+6;w.unitOccSync(ground);
    w.ents[actor].atkCd=0;w.presentationEvents.clear();w.update();
    bool premature=false;
    for(const auto& event:w.presentationEvents)premature |= event.kind==PresentationKind::Muzzle && event.entity==actor;
    check(!premature && w.ents[actor].state==UState::Chasing,
        "retreating target outside range resumes pursuit without an extra-cell shot");
    bool fractionalSaved=false,fractionalHashed=false,invalidRejected=false;
    if(!w.projs.empty()) {
        w.projs.front().w.range=5.75f;
        auto hash=w.checksum();World loaded;FILE* file=std::tmpfile();
        if(file){fractionalSaved=w.saveGame(file) && std::fseek(file,0,SEEK_SET)==0 && loaded.loadGame(file)
            && loaded.checksum()==hash && loaded.projs.front().w.range==5.75f;std::fclose(file);}
        w.projs.front().w.range=5.875f;fractionalHashed=w.checksum()!=hash;
        w.projs.front().w.range=std::numeric_limits<float>::quiet_NaN();file=std::tmpfile();
        if(file){invalidRejected=w.saveGame(file) && std::fseek(file,0,SEEK_SET)==0 && !loaded.loadGame(file);std::fclose(file);}
    }
    check(fractionalSaved && fractionalHashed && invalidRejected,"fractional projectile range is saved hashed and validated");
    std::filesystem::create_directories("review_out");
    exportRules("review_out/apocalypse-roundtrip.ini");
    auto& definition=const_cast<UnitDef&>(unitDef(UnitType::Apocalypse));
    definition.weapon.range=2;definition.airWeapon.range=3;
    loadRules("review_out/apocalypse-roundtrip.ini");
    const auto& restored=unitDef(UnitType::Apocalypse);
    check(restored.weapon.range==5.75f && restored.hasAirWeapon && restored.airWeapon.range==8
        && !std::strcmp(restored.airWeapon.projectile,"AAHeatSeeker")
        && !std::strcmp(restored.airWeapon.report,"ApocalypseAttackAir"),"rules export reload retains both weapons and fractional range");
    FILE* disabled=std::fopen("review_out/apocalypse-disabled.ini","wb");
    if(disabled) {
        std::fputs("[Unit.Apocalypse]\nHasAirWeapon=no\nAirWeapon.Range=8\n",disabled);
        std::fclose(disabled);loadRules("review_out/apocalypse-disabled.ini");
    }
    check(disabled && !unitDef(UnitType::Apocalypse).hasAirWeapon && !unitDef(UnitType::Apocalypse).hasAntiAir(),
        "explicitly disabled secondary stays disabled with retained keys");
    loadRules("review_out/apocalypse-roundtrip.ini");
    return failures;
}
