#include "game_regression.h"
#include "world_save_sections.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

int GameRegression::nativeGateRegression(const char* outputDir) {
    const auto source=contentResolve("assets/maps/native/all01t/scene.ini");
    if(source.empty() || !FileExists(source.c_str())){std::printf("NATIVE_GATE SKIP: export the original scene\n");return 77;}
    int failures=0;const auto check=[&](bool ok,const char* message){std::printf("NATIVE_GATE %s: %s\n",ok?"PASS":"FAIL",message);failures+=!ok;};
    std::filesystem::create_directories(outputDir);g_script.shutdown();world=World{};ais.clear();
    localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    const bool loaded=world.init(64,64,29,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && world.loadNativeScene(source.c_str());
    check(loaded,"the complete original scene loads");if(!loaded)return failures;
    EID gate=INVALID_EID;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.definition
        && world.ents[id].nativeIdentity.definition->gate){gate=id;break;}
    check(world.valid(gate),"the authored gate is identified by Gate=yes");if(!world.valid(gate))return failures;
    const auto& d=*world.ents[gate].nativeIdentity.definition;
    const int owner=world.ents[gate].player,x=(int)world.ents[gate].x+1,y=(int)world.ents[gate].y;
    const auto object=world.ents[gate].nativeIdentity.object,tag=world.ents[gate].nativeIdentity.tag;
    check(d.id=="GAGATE_A" && d.gateStages==9 && d.artFrames==21 && d.gateOpenTicks==78 && d.gateCloseTicks==360
        && bldDef(world.ents[gate]).w==3 && bldDef(world.ents[gate]).h==1,
        "original door stages, separate damaged frames and minute-based times use the 30-Hz simulation clock");
    check(owner==4 && !world.isAllied(owner,0) && tag=="0965BDDC",
        "the original civilian owner and Change House tag survive; the mission handover is not silently invented");
    // Isolate movement from mission combat. Keep every authored building,
    // wall, terrain cell and the gate's original owner/object/tag intact.
    for(auto& allies:world.nativeAllies)allies=255;
    const int enemy=owner==1?2:1;world.nativeAllies[owner]&=(uint8_t)~(1u<<enemy);
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && !world.ents[id].isBuilding
        && std::abs(world.ents[id].x-x)<5 && std::abs(world.ents[id].y-y)<5)world.kill(id,false);
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    {
        World changed=world;changed.map.bldOccRef=&changed.bldOcc;changed.nativeAllies[0]&=(uint8_t)~(1u<<owner);
        const auto prime=changed.spawnUnit(0,UnitType::YuriPrime,x+.5f,y-.5f);
        const auto accepted=changed.orderAttack({prime},gate);
        for(int i=0;i<5 && changed.ents[gate].player==owner;++i)changed.update();
        check(accepted.accepted==1 && changed.ents[gate].player==0 && changed.map.gateCells[changed.cellIdx(x,y)].house==0,
            "an ownership change refreshes gate access immediately, independently of the next simulation tick");
    }
    {
        World changed=world;changed.map.bldOccRef=&changed.bldOcc;changed.changeActorOwner(gate,enemy);
        FILE* file=tmpfile();bool saved=file && changed.saveGame(file);World restored;
        if(saved){rewind(file);saved=restored.loadGame(file) && restored.checksum()==changed.checksum();}
        if(file)fclose(file);
        check(changed.ents[gate].player==enemy && changed.map.gateCells[changed.cellIdx(x,y)].house==enemy
            && changed.ents[gate].nativeIdentity.object==object && saved
            && restored.map.gateCells[restored.cellIdx(x,y)].house==enemy,
            "the ownership service synchronizes gate access and persisted identity without waiting for a tick");
    }
    const auto reset=[&](){world=initial;world.map.bldOccRef=&world.bldOcc;};
    const auto unit=[&](int house,int side){const auto id=world.spawnUnit(house,UnitType::Engineer,x+.5f,y+side+.5f);world.ents[id].invuln=100000;return id;};
    const auto inside=[&](EID id){return world.valid(id) && (int)world.ents[id].y==y && world.ents[id].x>=x-1 && world.ents[id].x<x+2;};
    const auto roundtrip=[&](const char* label,int ticks,EID crossing=INVALID_EID) {
        FILE* file=tmpfile();World replay;bool ok=file && world.saveGame(file);
        if(ok){rewind(file);ok=replay.loadGame(file);}if(file)fclose(file);
        ok &= replay.checksum()==world.checksum();
        bool crossed=crossing==INVALID_EID;
        for(int i=0;i<ticks && ok;++i){
            world.update();replay.update();ok &= world.checksum()==replay.checksum();
            if(inside(crossing)){crossed=true;ok &= world.ents[gate].nativeGatePhase==NativeGatePhase::Open;}
        }
        ok &= crossed;
        check(ok,label);
    };
    {
        const auto spawned=world.spawnUnit(enemy,UnitType::Engineer,x+.5f,y+.5f);world.ents[spawned].invuln=100000;
        world.update();
        check(world.ents[gate].nativeGatePhase==NativeGatePhase::Closed && !world.ents[gate].nativeGateHold,
            "a map-spawned occupant inside a closed hostile gate does not create an invalid close timer or permission");
        roundtrip("closed-gate state remains saveable after an explicit map spawn",5);
    }
    reset();
    {
        const auto hostile=unit(enemy,-1);const auto& actor=world.ents[hostile];
        check(world.isAllied(enemy,owner) && !world.isAllied(owner,enemy)
            && world.map.cellBlocked(x,y) && world.map.cellBlocked(x,y,world.nativeGateAccess(actor))
            && world.bldAt(x,y)==gate,"a closed enemy gate is solid and retains its selectable building identity");
        world.orderMove({hostile},x+.5f,y+.5f,false);
        for(int i=0;i<120;++i)world.update();
        check(world.ents[gate].nativeGatePhase==NativeGatePhase::Closed && !inside(hostile),
            "an enemy movement order never opens or crosses a closed gate");
    }
    reset();
    EID actor=unit(0,-1);const auto before=world.checksum();
    check(!world.map.cellBlocked(x,y,world.nativeGateAccess(world.ents[actor])) && world.map.cellBlocked(x,y)
        && before==world.checksum(),"friendly route planning is read-only and does not physically open the door");
    const auto move=world.orderMove({actor},x+.5f,y+2.5f,false);
    const bool through=std::any_of(world.ents[actor].path.begin(),world.ents[actor].path.end(),[&](auto cell){return cell.y==y && std::abs(cell.x-x)<=1;});
    check(move.accepted==1 && through && world.ents[gate].nativeGatePhase==NativeGatePhase::Closed,
        "the actual move order plans through the intact friendly gate while it remains closed");
    const float startX=world.ents[actor].x,startY=world.ents[actor].y;
    bool waits=true;for(int i=0;i<20;++i){world.update();waits &= world.ents[actor].x==startX && world.ents[actor].y==startY;}
    check(waits && world.ents[gate].nativeGatePhase==NativeGatePhase::Opening && world.ents[gate].nativeGateProgress==20,
        "the engineer waits outside throughout the opening animation");
    roundtrip("an opening gate resumes 120 identical ticks, crossing only after it is fully open",120,actor);
    bool crossed=false,solid=true;
    for(int i=0;i<120;++i){world.update();crossed |= inside(actor);if(inside(actor))solid &= world.ents[gate].nativeGatePhase==NativeGatePhase::Open;}
    check(world.ents[actor].state==UState::Idle && (int)world.ents[actor].y==y+2 && solid,
        "the engineer crosses legally and reaches the far side without destroying the gate");
    (void)crossed;
    int waited=0;while(world.ents[gate].nativeGatePhase!=NativeGatePhase::Closing && waited++<500)world.update();
    check(world.ents[gate].nativeGatePhase==NativeGatePhase::Closing && world.map.cellBlocked(x,y),
        "a clear gate observes its close delay and blocks new entry while closing");
    roundtrip("closing progress and remaining delay survive a snapshot continuation",10);
    world.kill(actor,false);actor=unit(0,-1);world.orderMove({actor},x+.5f,y+1.5f,false);
    const int oldProgress=world.ents[gate].nativeGateProgress;world.update();
    check(world.ents[gate].nativeGatePhase==NativeGatePhase::Opening && world.ents[gate].nativeGateProgress>=oldProgress,
        "a new allied approach reverses closing from the current pose");
    for(int i=0;i<160;++i)world.update();world.orderMove({actor},x+.5f,y+.5f,false);
    for(int i=0;i<100 && !inside(actor);++i)world.update();world.orderStop({actor});
    for(int i=0;i<d.gateCloseTicks+5;++i)world.update();
    check(inside(actor) && world.ents[gate].nativeGatePhase==NativeGatePhase::Open && world.bldAt(x,y)==gate,
        "an idle occupant holds the gate open past the entire delay without removing the building footprint");
    check(world.bldOcc[world.cellIdx(x,y)]==gate+1 && !world.canPlace(BldType::Wall,x,y,0),
        "an open gate remains a building footprint and cannot be built over");
    roundtrip("an occupied open gate resumes the same holding behavior",30);
    const auto hostile=unit(enemy,-1);const auto hostileMove=world.orderMove({hostile},x+.5f,y+2.5f,false);
    for(int i=0;i<160;++i)world.update();
    check(hostileMove.accepted==1 && (int)world.ents[hostile].y==y+2,
        "an enemy can pass an already open gate without acquiring permission to open a closed one");
    const int openFrame=nativeActorFrame(world.ents[gate]);world.ents[gate].hp=d.building.hp/2;
    check(openFrame==8 && nativeActorFrame(world.ents[gate])==18,
        "healthy and damaged doors use the original separate animation blocks");
    world.ents[gate].hp=d.building.hp;
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
    fogKnowledge={};syncFogKnowledge();
    auto rememberedFrame=[&](){for(const auto& [key,memory]:fogKnowledge.buildings)if(memory.nativeType==world.ents[gate].nativeIdentity.type
        && memory.x==(int)world.ents[gate].x && memory.y==y)return memory.nativeFrame;return -1;};
    check(rememberedFrame()==8,"visible fog knowledge records the observed open door");
    world.kill(actor,false);world.kill(hostile,false);
    for(int i=0;i<d.gateCloseTicks+d.gateOpenTicks+3;++i)world.update();
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_SEEN);syncFogKnowledge();
    check(nativeActorFrame(world.ents[gate])==0 && rememberedFrame()==8,
        "a hidden gate closing does not overwrite the previously observed open pose");
    check(world.valid(gate) && world.ents[gate].hp==d.building.hp && world.ents[gate].player==owner
        && world.ents[gate].nativeIdentity.object==object && world.ents[gate].nativeIdentity.tag==tag,
        "all movement phases preserve the original gate identity, ownership, tag and health");
    {
        FILE* file=tmpfile();bool saved=file && world.saveGame(file);std::vector<uint8_t> bytes;
        if(saved){const long size=ftell(file);saved=size>=17 && fseek(file,0,SEEK_SET)==0;
            if(saved){bytes.resize((size_t)size);saved=fread(bytes.data(),1,bytes.size(),file)==bytes.size();}}
        if(file)fclose(file);bool rejected=saved;
        auto bad=[&](std::vector<uint8_t> data) {
            FILE* input=tmpfile();World other;bool accepted=false;
            if(input){if(fwrite(data.data(),1,data.size(),input)==data.size()){rewind(input);accepted=other.loadGame(input);}fclose(input);}
            return !accepted;
        };
        if(saved) {
            FILE* tail=tmpfile();size_t suffix=0;
            const auto modern=locateWorldSaveSection(bytes,WorldSaveSection::AttackMove);
            const bool written=tail && modern && world.writeNativeEventState(tail)
                && world.writeNativeRocketState(tail) && world.writeNativeProductionState(tail) && world.writeNativeAIState(tail)
                && world.writeNativeCrateState(tail) && world.writeNativeConditionState(tail);
            if(written)suffix=(size_t)ftell(tail)+bytes.size()-modern->offset;if(tail)fclose(tail);
            saved=written && suffix<=bytes.size() && bytes.size()-suffix>=17;
            rejected &= saved;
            if(saved) {
                const size_t gateEnd=bytes.size()-suffix;
                auto invalid=bytes;invalid[gateEnd-9]=4;rejected &= bad(invalid);
                invalid=bytes;const int32_t negative=-1;memcpy(invalid.data()+gateEnd-8,&negative,4);rejected &= bad(invalid);
                invalid=bytes;const int32_t excessive=d.gateCloseTicks+1;memcpy(invalid.data()+gateEnd-4,&excessive,4);rejected &= bad(invalid);
                invalid=bytes;const uint32_t missing=0;memcpy(invalid.data()+gateEnd-17,&missing,4);rejected &= bad(invalid);
                invalid=bytes;invalid.pop_back();rejected &= bad(invalid);
            }
        }
        check(rejected,"invalid gate phase, progress, deadline, count and truncated state are rejected");
        world.damage(gate,100000,enemy);const auto replacement=world.spawnUnit(0,UnitType::GI,x+.5f,y+.5f);
        check(world.valid(replacement) && replacement==gate && world.map.gateCells.empty() && world.bldAt(x,y)==INVALID_EID
            && world.ents[replacement].nativeGatePhase==NativeGatePhase::Closed && !world.ents[replacement].nativeGateProgress,
            "destroying the gate removes only its blocker and never transfers gate state to a reused entity slot");
        roundtrip("a source with all gates destroyed persists its empty gate state",20);
    }
    {
        World fresh=world;fresh.map.bldOccRef=&fresh.bldOcc;
        check(fresh.init(64,64,31,1,0,{Faction::Allies},0,"",true) && fresh.map.gateCells.empty(),
            "starting a generated match clears native gate geometry");
    }
    // Actual GPU frames at both supported display sizes, with no sim mutation.
    reset();actor=unit(0,-1);world.orderMove({actor},x+.5f,y+1.5f,false);
    camZoom=1;interpAlpha=1;showMenu=placing=dragging=targetingParadrop=false;sideMode=0;targetingSW=SWType::COUNT;
    sel.clear();selBuilding=INVALID_EID;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
    const auto frame=[&](const char* name) {
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);syncFogKnowledge();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();int sx,sy;tileToScreen(x,y,sx,sy);
            camX=sx-(SCREEN_W-sidebarW)/2.f;camY=sy-heightScreenY(6)-SCREEN_H/2.f;
            shotFile=(std::filesystem::path(outputDir)/(std::string(name)+"-"+std::to_string(size.x)+".png")).string();
            const auto state=world.checksum();render();check(state==world.checksum(),"actual door drawing does not mutate simulation state");
        }
    };
    frame("closed");for(int i=0;i<d.gateOpenTicks/2;++i)world.update();frame("opening");
    for(int i=0;i<d.gateOpenTicks;++i)world.update();frame("open");
    world.ents[gate].hp=d.building.hp/2;frame("damaged-open");
    std::printf("NATIVE_GATE failures=%d stages=%d\n",failures,d.gateStages);return failures;
}
