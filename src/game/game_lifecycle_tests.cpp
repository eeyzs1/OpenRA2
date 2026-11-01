#include "game/game.h"
#include "game/lang.h"
#include "game/script.h"
#include "core/content.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

// Slot-reuse mechanisms are intentionally separate from natural-start matches.
int Game::lifecycleRegression() {
    int failures=0;localPlayer=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("LIFECYCLE %s %s\n",ok?"PASS":"FAIL",name); };
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& cell:world.map.cells) cell=Cell{};
        world.cratesEnabled=false;world.presentationEvents.clear();
    };
    reset();World pristine=world;pristine.map.bldOccRef=&pristine.bldOcc;
    world.crates.push_back({true,10,10,7});world.timedBombs.push_back({10.5f,10.5f,1,0,INVALID_EID,100,2});
    reset();
    check(world.crates.empty() && world.timedBombs.empty() && world.checksum()==pristine.checksum(),
        "reinitializing world clears previous match bombs and crates");
    EID actor=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    EID target=world.spawnUnit(1,UnitType::MCV,30.5f,10.5f);
    World::Cmd cmd{};cmd.type=World::Cmd::Attack;cmd.ids={actor};cmd.a=target;world.applyCmd(0,cmd);
    world.kill(target);EID replacement=world.spawnUnit(1,UnitType::MCV,40.5f,10.5f);world.update();
    check(replacement==target && world.ents[actor].target==INVALID_EID,"old attack does not acquire a replacement in the same slot");
    reset();actor=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);
    target=world.spawnBuilding(-1,BldType::OilDerrick,25,10,true);
    cmd.type=World::Cmd::Capture;cmd.ids={actor};cmd.a=target;world.applyCmd(0,cmd);world.presentationEvents.clear();
    world.kill(target,false);replacement=world.spawnBuilding(-1,BldType::OilDerrick,25,10,true);
    FILE* file=tmpfile();World restored;bool replay=file && world.saveGame(file);
    if(replay) { rewind(file);replay=restored.loadGame(file); }
    if(file) fclose(file);
    for(int frame=0;frame<600;++frame) {
        world.update();if(replay) { restored.update();replay=world.checksum()==restored.checksum(); }
    }
    int reports=0;bool staleMarker=false;
    for(const auto& event:world.presentationEvents) if(event.kind==PresentationKind::EngineerOrder) {
        ++reports;staleMarker|=event.positional;
    }
    check(replacement==target && world.valid(actor) && world.ents[replacement].player==-1,
        "capture order does not capture a replacement building at the same location");
    check(replay && reports==1 && !staleMarker,"retired capture reports once without binding marker to replacement and resumes identically");
    reset();actor=world.spawnUnit(0,UnitType::Engineer,5.5f,10.5f);
    target=world.spawnBuilding(0,BldType::OilDerrick,25,10,true);world.damage(target,100,-1);
    cmd.type=World::Cmd::Repair;cmd.ids={actor};cmd.a=target;world.applyCmd(0,cmd);
    world.kill(target,false);replacement=world.spawnBuilding(0,BldType::OilDerrick,25,10,true);world.damage(replacement,100,-1);
    for(int frame=0;frame<600;++frame) world.update();
    check(replacement==target && world.valid(actor) && world.ents[replacement].hp==bldDef(BldType::OilDerrick).hp-100,
        "repair order does not consume engineer on a replacement building");
    reset();actor=world.spawnUnit(0,UnitType::Intruder,20.5f,20.5f);
    target=world.spawnBuilding(0,BldType::AirForceCmd,2,2,true);
    EID alternative=world.spawnBuilding(0,BldType::AirForceCmd,4,12,true);
    world.ents[actor].airbase=target;world.ents[actor].ammo=0;world.ents[actor].state=UState::Returning;
    world.kill(target,false);replacement=world.spawnBuilding(0,BldType::ConYard,50,50,true);world.update();
    check(replacement==target && world.ents[actor].airbase==alternative,"returning aircraft does not land on a reused airfield slot");
    reset();actor=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    target=world.spawnUnit(1,UnitType::MCV,30.5f,10.5f);
    Projectile missile{};missile.player=0;missile.kind=ProjKind::Missile;missile.x=10.5f;missile.y=10.5f;
    missile.tx=30.5f;missile.ty=10.5f;missile.target=target;missile.src=actor;missile.rot=8;
    world.projs.push_back(missile);
    World::TimedBomb bomb{};bomb.x=30.5f;bomb.y=10.5f;bomb.player=0;bomb.timer=60;bomb.attachedTo=target;world.timedBombs.push_back(bomb);
    world.kill(target);replacement=world.spawnUnit(1,UnitType::MCV,40.5f,10.5f);
    check(replacement==target && world.projs.front().target==INVALID_EID && world.timedBombs.front().attachedTo==INVALID_EID
        && world.timedBombs.front().x==30.5f,"missile and attached bomb retain the old impact location after target slot reuse");
    world.kill(actor);replacement=world.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
    check(replacement==actor && world.projs.front().src==INVALID_EID,"old projectile cannot credit kills to a replacement shooter");
    reset();actor=world.spawnUnit(0,UnitType::WarMiner,10.5f,10.5f);
    target=world.spawnBuilding(0,BldType::OreRefinery,25,10,true);world.ents[actor].dockRefinery=target;
    world.kill(target,false);replacement=world.spawnBuilding(0,BldType::OreRefinery,45,40,true);
    check(replacement==target && world.ents[actor].dockRefinery==INVALID_EID,"harvester dock does not follow a replacement refinery");
    reset();actor=world.spawnUnit(0,UnitType::Grizzly,30.5f,30.5f);
    target=world.spawnUnit(1,UnitType::Magnetron,35.5f,35.5f);
    world.ents[target].target=actor;world.ents[actor].magneticBy=target;world.ents[actor].magneticHeight=4;
    world.kill(target);replacement=world.spawnUnit(1,UnitType::Magnetron,35.5f,35.5f);world.ents[replacement].target=actor;world.update();
    check(replacement==target && world.ents[actor].magneticBy==INVALID_EID && world.ents[actor].magneticHeight==0
        && world.ents[actor].hp==unitDef(UnitType::Grizzly).hp-48,"lost magnetic source falls instead of inheriting replacement beam");
    reset();actor=world.spawnBuilding(0,BldType::PowerPlant,25,10,true);
    target=world.spawnUnit(1,UnitType::FloatingDisc,26.5f,11.5f);
    cmd.type=World::Cmd::Attack;cmd.ids={target};cmd.a=actor;world.applyCmd(1,cmd);world.update();
    bool drained=world.ents[actor].drainedBy==target;
    world.kill(target);replacement=world.spawnUnit(1,UnitType::FloatingDisc,45.5f,40.5f);
    check(drained && replacement==target && world.ents[actor].drainedBy==INVALID_EID && world.players[0].powerMade>=bldDef(BldType::PowerPlant).power,
        "retired disc releases power drain before its slot is reused");
    reset();actor=world.spawnUnit(0,UnitType::Grizzly,30.5f,30.5f);
    target=world.spawnUnit(1,UnitType::TerrorDrone,30.5f,30.5f);
    world.ents[actor].parasite=target;world.ents[target].parasiting=true;world.ents[target].parasiteHost=actor;world.unitOccSync(target);
    world.kill(actor);replacement=world.spawnUnit(0,UnitType::Grizzly,45.5f,40.5f);
    check(replacement==actor && world.valid(target) && !world.ents[target].parasiting && world.ents[target].parasiteHost==INVALID_EID
        && world.unitAtCell(30,30)==target,"parasite detaches into spatial index without following replacement host");
    reset();actor=world.spawnUnit(0,UnitType::Yuri,7.5f,7.5f);
    target=world.spawnUnit(1,UnitType::Spy,5.5f,10.5f);
    world.ents[target].origPlayer=1;world.ents[target].player=0;world.ents[target].mindBy=actor;world.ents[actor].mindTarget=target;
    EID infiltrated=world.spawnBuilding(1,BldType::OreRefinery,8,10,true);
    cmd.type=World::Cmd::Attack;cmd.ids={target};cmd.a=infiltrated;world.applyCmd(0,cmd);
    for(int frame=0;frame<600 && world.valid(target);++frame) world.update();
    check(!world.valid(target) && world.ents[actor].mindTarget==INVALID_EID,"non-death spy consumption clears controller reference");
    reset();target=world.spawnBuilding(1,BldType::NuclearReactor,25,25,true);
    bomb={};bomb.player=0;bomb.x=26.5f;bomb.y=26.5f;bomb.radius=1;bomb.dmg=100000;bomb.timer=1;bomb.attachedTo=target;
    world.timedBombs.push_back(bomb);world.timedBombs.shrink_to_fit();world.update();
    bool waves=!world.valid(target) && world.timedBombs.size()==4;
    for(const auto& wave:world.timedBombs) waves &= wave.timer>=30;
    file=tmpfile();replay=file && world.saveGame(file);
    if(replay) { rewind(file);replay=restored.loadGame(file); }
    if(file) fclose(file);
    for(int frame=0;frame<240;++frame) {
        world.update();if(replay) { restored.update();replay=world.checksum()==restored.checksum(); }
    }
    check(waves && replay,"nuclear death appending bomb waves during detonation is stable and save-resume deterministic");
    if(!waves || !replay) std::printf("LIFECYCLE_NUCLEAR waves=%d replay=%d bombs=%zu dead=%d\n",waves,replay,world.timedBombs.size(),!world.valid(target));
    // Exercise actual Lua reentry while recursive death damage grows ents.
    reset();target=world.spawnUnit(0,UnitType::Kirov,30.5f,30.5f);
    EID first=world.spawnUnit(1,UnitType::Engineer,30.5f,30.5f);world.ents[first].hp=1;
    EID second=world.spawnUnit(1,UnitType::MCV,32.5f,30.5f);world.ents[second].hp=100;
    std::filesystem::path mod="review_out/lifecycle-mod";
    std::filesystem::create_directories(mod/"scripts");
    {
        std::ofstream script(mod/"scripts/zz_lifecycle.lua");
        script<<"local spawned=false\nfunction OnUnitKilled(eid,player,kind)\n if spawned then return end\n spawned=true\n for i=0,255 do ra2.spawnUnit(0,'GI',40.5+i%16,40.5+math.floor(i/16)) end\nend\n";
    }
    contentAddModPath(mod.string(),true);g_script.init(&world,0,-1);world.ents.shrink_to_fit();
    const size_t before=world.ents.size();world.kill(target);g_script.shutdown();
    check(!world.valid(target) && !world.valid(first) && !world.valid(second) && world.ents.size()>=before+256,
        "recursive Kirov death reserves retiring slots across Lua spawning and entity vector growth");
    return failures;
}
