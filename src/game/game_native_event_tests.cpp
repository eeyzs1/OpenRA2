#include "game_regression.h"
#include "world_save_sections.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

int GameRegression::nativeEventRegression(const char* outputDir) {
    const auto path=contentResolve("assets/maps/native/all01t/scene.ini");
    if(path.empty() || !FileExists(path.c_str())){std::printf("NATIVE_EVENT SKIP: export the original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* message){std::printf("NATIVE_EVENT %s: %s\n",ok?"PASS":"FAIL",message);failures+=!ok;};
    g_script.shutdown();world=World{};ais.clear();localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    const auto load=[&](const char* source){return world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true) && world.loadNativeScene(source);};
    const bool loaded=load(path.c_str());check(loaded,"the full native map, actor scene and authored trigger graph load");if(!loaded)return failures;
    const auto& definitions=*world.nativeEventDefinition;
    check(definitions.triggers.size()==130 && definitions.tags.size()==128 && definitions.cells.size()==180
        && definitions.teams.size()==49 && definitions.taskForces.size()==29 && definitions.scripts.size()==38,
        "every original trigger, tag, cell attachment, team, task force and script is retained");
    check(world.nativeHouseNumber(13)==0 && world.nativeHouseNumber(17)==4 && !definitions.fogOfWar && definitions.revealRadius==9,
        "original appended country numbers, player house, fog flag and reveal radius are interpreted explicitly");
    EID gate=INVALID_EID;std::vector<EID> attached;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id)) {
        const auto& e=world.ents[id];if(e.nativeIdentity.definition && e.nativeIdentity.definition->gate)gate=id;
        if(e.nativeIdentity.tag=="0965BDDC")attached.push_back(id);
    }
    check(world.valid(gate) && world.ents[gate].player==4 && !world.isAllied(4,0) && attached.size()==27,
        "the gate starts with its original hostile civilian owner and the handover has exactly 27 attachments");
    if(!world.valid(gate) || attached.size()!=27)return failures+1;
    check(world.startNativeEvents() && world.validateNativeEventState(),"native events start explicitly, without registering an unfinished campaign");
    const int enter=world.nativeTriggerIndex("096564CC"),handover=world.nativeTriggerIndex("0965E07C");
    const auto runtime=[&](int definition){for(int i=0;i<(int)world.nativeEvents.triggers.size();++i)if(world.nativeEvents.triggers[i].definition==definition)return i;return -1;};
    const int enterState=runtime(enter),handoverState=runtime(handover);
    int entryTag=-1;for(int i=0;i<(int)definitions.tags.size();++i)if(definitions.tags[i].trigger==enter)entryTag=i;
    Vec2i cell{-1,-1},outside{-1,-1};
    for(const auto& [point,tag]:definitions.cells)if(tag==entryTag && cell.x<0) {
        for(auto step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
            const Vec2i next{point.x+step.x,point.y+step.y};
            if(!world.map.inBounds(next.x,next.y) || !world.map.surfacePassable(point.x,point.y,0,0)
                || !world.map.surfacePassable(next.x,next.y,0,0) || world.unitAtCell(point.x,point.y)!=INVALID_EID
                || world.unitAtCell(next.x,next.y)!=INVALID_EID)continue;
            if(std::any_of(definitions.cells.begin(),definitions.cells.end(),[&](const auto& binding){return binding.first==next;}))continue;
            std::vector<Vec2i> route;if(!world.map.findPath(next.x,next.y,point.x,point.y,route) || route.size()!=1)continue;
            cell=point;outside=next;break;
        }
    }
    check(enterState>=0 && handoverState>=0 && entryTag>=0 && cell.x>=0,"a genuine source CellTag has a legal adjacent approach");
    if(enterState<0 || handoverState<0 || cell.x<0)return failures+1;
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto reset=[&](){world=initial;world.map.bldOccRef=&world.bldOcc;};
    const auto enterRegion=[&](int house) {
        const auto actor=world.spawnUnit(house,UnitType::GI,outside.x+.5f,outside.y+.5f);world.ents[actor].invuln=100000;
        World::Cmd command{};command.type=World::Cmd::Move;command.ids={actor};command.x=cell.x+.5f;command.y=cell.y+.5f;
        world.applyCmd(house,command);return actor;
    };
    {
        enterRegion(1);for(int i=0;i<45;++i)world.update();
        check(!world.nativeEvents.triggers[handoverState].enabled && world.nativeEvents.triggers[enterState].firings==0 && world.ents[gate].player==4,
            "a different house entering the actual tagged cell does not activate the player's handover");
    }
    reset();const auto actor=enterRegion(0);
    bool fractional=false,early=false;
    for(int i=0;i<90 && !world.nativeEvents.triggers[enterState].firings;++i) {
        world.update();if((int)world.ents[actor].x==cell.x && (int)world.ents[actor].y==cell.y
            && (world.ents[actor].x!=cell.x+.5f || world.ents[actor].y!=cell.y+.5f)) {
            fractional=true;early |= world.nativeEvents.triggers[enterState].firings!=0;
        }
    }
    check(fractional && !early && world.nativeEvents.triggers[enterState].firings==1
        && world.nativeEvents.triggers[enterState].blockedAction==-1 && world.nativeEvents.triggers[handoverState].enabled,
        "actual movement fires the source entry event only at the cell center, then enables Change House 2b");
    const uint64_t enabledAt=world.nativeEvents.triggers[handoverState].timerStart;
    for(int i=0;i<20;++i)world.update();
    check(world.ents[gate].player==4 && world.nativeEvents.triggers[handoverState].firings==0,
        "enabling the elapsed-time trigger starts its two-second delay instead of changing the gate immediately");
    const bool entrySpeech=std::any_of(world.nativeEventCues.begin(),world.nativeEventCues.end(),[](const auto& cue){return cue.action==21 && cue.key=="Mis_A1_EvaControlFort";});
    FILE* file=tmpfile();World replay;bool continued=file && world.saveGame(file);
    if(continued){rewind(file);continued=replay.loadGame(file);}if(file)fclose(file);
    const auto application=(std::filesystem::path(outputDir)/"pending-handover.sav").string();const auto appState=world.checksum();
    const bool appSaved=saveGameFile(application.c_str());if(appSaved)world.players[0].money+=17;
    check(appSaved && loadGameFile(application.c_str()) && world.checksum()==appState && world.nativeEvents.active,
        "application v10 restores the active source graph and pending handover without replaying scene startup");
    continued &= replay.checksum()==world.checksum();bool premature=false;uint64_t firedAt=0;
    for(int i=0;i<120 && continued;++i) {
        world.update();replay.update();continued &= replay.checksum()==world.checksum();
        if(world.ents[gate].player==0 && !firedAt)firedAt=world.tick;
        if(world.tick-enabledAt<2*LOGIC_FPS)premature |= world.ents[gate].player!=4;
    }
    check(continued && !premature && firedAt==enabledAt+2*LOGIC_FPS,
        "the pending native timer resumes identically and performs handover at exactly the authored deadline");
    bool transferred=world.nativeEvents.triggers[handoverState].firings==1 && world.nativeEvents.triggers[handoverState].blockedAction==-1;
    for(EID id:attached)transferred &= world.valid(id) && world.ents[id].player==0 && world.ents[id].nativeIdentity.tag=="0965BDDC";
    check(transferred && world.map.gateCells[world.cellIdx(82,48)].house==0,
        "the real Change House action transfers all 27 attached actors, preserves provenance and refreshes gate access");
    EID miner=INVALID_EID;int miners=0;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && !world.ents[id].isBuilding && world.ents[id].utype==UnitType::ChronoMiner){miner=id;++miners;}
    check(miners==1 && world.ents[miner].player==0 && world.ents[miner].nativeIdentity.definition
        && world.ents[miner].nativeIdentity.definition->id=="CMIN" && world.ents[miner].nativeIdentity.definition->nativeBody && world.ents[miner].autoHarvest,
        "the original Reinforcement-at-waypoint action creates one actual CMIN with its own art and mining profile");
    const bool replayedSpeech=std::any_of(world.nativeEventCues.begin(),world.nativeEventCues.end(),[](const auto& cue){return cue.action==21 && cue.key=="Mis_A1_EvaControlFort";});
    const bool movie=std::any_of(world.nativeEventCues.begin(),world.nativeEventCues.end(),[](const auto& cue){return cue.action==100 && cue.value==68;});
    check(entrySpeech && movie && !replayedSpeech,"authored speech and movie requests retain their parameters, and loading does not replay the earlier speech");
    consumeNativeEventCues();
    check(world.nativeEventCues.empty() && !pendingNativeEventCues.empty(),"the app retains unresolved native cues instead of replacing them with unrelated skirmish effects");
    const uint64_t fires=world.nativeEvents.triggers[enterState].firings;
    for(int i=0;i<30;++i)world.update();
    check(world.nativeEvents.triggers[enterState].firings==fires && world.validateNativeEventState(),"the volatile entry tag does not fire again and runtime attachments remain valid");
    {
        const auto engineer=world.spawnUnit(0,UnitType::Engineer,82.5f,47.5f);world.ents[engineer].invuln=100000;
        const auto accepted=world.orderMove({engineer},82.5f,49.5f,false);
        bool waited=false,legal=true;
        for(int i=0;i<180;++i){world.update();const auto& door=world.ents[gate];const auto& moving=world.ents[engineer];
            waited |= door.nativeGatePhase==NativeGatePhase::Opening && (int)moving.y==47;
            if((int)moving.y==48)legal &= door.nativeGatePhase==NativeGatePhase::Open;}
        check(accepted.accepted==1 && waited && legal && (int)world.ents[engineer].y==49 && world.ents[gate].player==0,
            "the authored handover grants actual gate passage without changing alliances, ownership by hand or demolishing the gate");
    }
    if(world.valid(miner)) {
        camZoom=1;interpAlpha=1;showMenu=placing=dragging=targetingParadrop=false;sideMode=0;targetingSW=SWType::COUNT;sel={miner};selBuilding=INVALID_EID;
        shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);fogKnowledge={};syncFogKnowledge();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();int x,y;tileToScreen((int)world.ents[miner].x,(int)world.ents[miner].y,x,y);
            camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-heightScreenY((uint8_t)world.actorSurfaceHeight(world.ents[miner]))-SCREEN_H/2.f;
            shotFile=(std::filesystem::path(outputDir)/("handover-miner-"+std::to_string(size.x)+".png")).string();
            const auto state=world.checksum();render();check(state==world.checksum(),"the actual handed-over base and original miner render without changing simulation state");
        }
    }
    {
        std::ofstream report(std::filesystem::path(outputDir)/"coverage.txt");
        for(const auto& trigger:definitions.triggers) {
            for(const auto& event:trigger.events)if(event.kind!=0 || (event.type!=1 && event.type!=13 && event.type!=32
                && event.type!=57 && event.type!=48 && event.type!=9 && event.type!=10 && event.type!=11
                && !(definitions.extendedConditions && (event.type==4 || event.type==6 || event.type==33 || (event.type>=19 && event.type<=22) || event.type==36 || event.type==37))))
                report<<"UNIMPLEMENTED EVENT "<<event.type<<" "<<trigger.id<<" "<<trigger.name<<"\n";
            for(const auto& action:trigger.actions)if(world.nativeActionBlocker(action,-1)!=-1)
                report<<"BLOCKED ACTION "<<action.type<<" "<<trigger.id<<" "<<trigger.name<<"\n";
        }
        for(const auto& script:definitions.scripts)for(size_t line=0;line<script.steps.size();++line) {
            const auto step=script.steps[line];
            const bool known=(step.x==0 && step.y!=10 && step.y!=11)
                || ((step.x==1 || step.x==3) && world.nativeWaypoint(step.y).x>=0)
                || (step.x==5 && step.y>=0 && step.y<=86400) || (step.x==20 && step.y>=0 && step.y<=1023)
                || step.x==6 || step.x==49 || (definitions.extendedConditions && ((step.x==8 && step.y>=0 && step.y<=3) || ((step.x==39 || step.x==40) && step.y>=0 && step.y<50)));
            if(!known)report<<"UNIMPLEMENTED SCRIPT "<<step.x<<" "<<script.id<<" line="<<line<<" arg="<<step.y<<"\n";
        }
    }
    reset();
    {
        const auto wp=world.nativeWaypoint(4);
        for(int y=-6;y<=6;++y)for(int x=-6;x<=6;++x) {
            const int tx=wp.x+x,ty=wp.y+y;
            if(!world.map.inBounds(tx,ty) || !world.map.surfacePassable(tx,ty,0,0) || world.unitAtCell(tx,ty)!=INVALID_EID)continue;
            const auto blocker=world.spawnUnit(0,UnitType::MCV,tx+.5f,ty+.5f);world.ents[blocker].invuln=100000;world.ents[blocker].guard=true;
        }
        enterRegion(0);for(int i=0;i<120;++i)world.update();
        check(world.nativeEvents.triggers[handoverState].blockedAction==80 && world.nativeEvents.triggers[handoverState].firings==1
            && world.ents[gate].player==0 && std::any_of(world.nativeEventCues.begin(),world.nativeEventCues.end(),[](const auto& cue){return cue.action==100 && cue.value==68;}),
            "an unavailable reinforcement records its failure after earlier authored ownership, cue and trigger actions execute in source order");
    }
    reset();
    {
        const auto wp=world.nativeWaypoint(4);const auto carrier=world.spawnUnit(0,UnitType::AmphTransport,wp.x+.5f,wp.y+.5f);
        const auto passenger=world.spawnUnit(0,UnitType::GI,wp.x+.5f,wp.y-.5f);world.ents[passenger].invuln=100000;
        int tag=-1;for(int i=0;i<(int)definitions.tags.size();++i)if(definitions.tags[i].id=="0a8dedfc")tag=i;
        world.ents[passenger].nativeEventTag=tag;const int lastCell=world.ents[passenger].nativeLastEnteredCell;
        const auto boarded=world.orderBoard({passenger},carrier);for(int i=0;i<60 && world.valid(passenger);++i)world.update();
        bool restored=boarded.accepted==1 && !world.valid(passenger) && world.ents[carrier].cargo.size()==1
            && world.ents[carrier].cargo.front().nativeEventTag==tag;
        FILE* snapshot=tmpfile();World resumed;bool saved=snapshot && world.saveGame(snapshot);
        if(saved){rewind(snapshot);saved=resumed.loadGame(snapshot);}if(snapshot)fclose(snapshot);
        restored &= saved && resumed.checksum()==world.checksum();
        const auto released=world.orderUnload({carrier});EID actual=INVALID_EID;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && id!=carrier && world.ents[id].nativeEventTag==tag)actual=id;
        check(restored && released.released==1 && world.valid(actual) && world.ents[actual].nativeLastEnteredCell!=lastCell,
            "runtime tag and last-entered cell survive boarding, active-event save/load and connected unloading");
        if(world.valid(actual)){world.kill(actual,false);const auto replacement=world.spawnUnit(0,UnitType::GI,wp.x+.5f,wp.y-.5f);
            check(replacement==actual && world.ents[replacement].nativeEventTag==-1,
                "a reused unit slot does not inherit the dead object's runtime trigger attachment");}
    }
    reset();
    {
        FILE* whole=tmpfile();FILE* extension=tmpfile();bool saved=whole && extension && world.saveGame(whole) && world.writeNativeEventState(extension);
        auto bytes=[](FILE* source){std::vector<uint8_t> result;if(!source)return result;const long n=ftell(source);if(n<0 || fseek(source,0,SEEK_SET))return result;
            result.resize((size_t)n);if(fread(result.data(),1,result.size(),source)!=result.size())result.clear();return result;};
        const auto full=bytes(whole),tail=bytes(extension);if(whole)fclose(whole);if(extension)fclose(extension);
        FILE* rocket=tmpfile();size_t rocketBytes=0;if(rocket && world.writeNativeRocketState(rocket))rocketBytes=(size_t)ftell(rocket);if(rocket)fclose(rocket);
        FILE* production=tmpfile();if(production && world.writeNativeProductionState(production))rocketBytes+=(size_t)ftell(production);if(production)fclose(production);
        FILE* ai=tmpfile();if(ai && world.writeNativeAIState(ai))rocketBytes+=(size_t)ftell(ai);if(ai)fclose(ai);
        FILE* crates=tmpfile();if(crates && world.writeNativeCrateState(crates))rocketBytes+=(size_t)ftell(crates);if(crates)fclose(crates);
        FILE* conditions=tmpfile();if(conditions && world.writeNativeConditionState(conditions))rocketBytes+=(size_t)ftell(conditions);if(conditions)fclose(conditions);
        const auto modern=locateWorldSaveSection(full,WorldSaveSection::AttackMove);saved &= modern.has_value();
        if(modern)rocketBytes+=full.size()-modern->offset;
        saved &= !tail.empty() && tail.size()+rocketBytes<=full.size() && std::equal(tail.begin(),tail.end(),full.end()-tail.size()-rocketBytes);bool rejected=saved;
        auto bad=[&](std::vector<uint8_t> data){FILE* input=tmpfile();World out;bool accepted=false;
            if(input){if(fwrite(data.data(),1,data.size(),input)==data.size()){rewind(input);accepted=out.loadGame(input);}fclose(input);}return !accepted;};
        if(saved){const auto start=full.size()-tail.size()-rocketBytes;auto invalid=full;invalid[start]=2;rejected &= bad(invalid);
            invalid=full;const int difficulty=3;memcpy(invalid.data()+start+1,&difficulty,4);rejected &= bad(invalid);
            invalid=full;const uint32_t count=0xffffffff;memcpy(invalid.data()+start+6,&count,4);rejected &= bad(invalid);
            invalid=full;const int definition=1000000;memcpy(invalid.data()+start+10,&definition,4);rejected &= bad(invalid);
            invalid=full;invalid.pop_back();rejected &= bad(invalid);}
        check(rejected,"invalid native event activation, difficulty, counts, references and truncated attachment state are rejected");
    }
    // Unsupported actions are tested as an explicit modified-source fixture.
    // Source v3 attempts all actions in order, independently of their results.
    std::string changed=initial.nativeScenarioSource;const auto& actions=definitions.triggers[enter].actions;std::string old=std::to_string(actions.size());
    for(const auto& a:actions){old+=","+std::to_string(a.type);for(const auto& arg:a.args)old+=","+arg;}
    std::string added=std::to_string(actions.size()+2)+old.substr(old.find(','))+",250,0,0,0,0,0,0,A,46,0,0,0,0,0,0,A";
    const auto offset=changed.find(old);bool blocked=false;
    if(offset!=std::string::npos){changed.replace(offset,old.size(),added);const auto unsupported=std::filesystem::path(outputDir)/"unsupported-scene.ini";
        {std::ofstream output(unsupported,std::ios::binary);output<<changed;}
        if(load(unsupported.string().c_str()) && world.startNativeEvents()) {
            enterRegion(0);for(int i=0;i<45;++i)world.update();
            blocked=world.nativeEvents.triggers[enterState].blockedAction==250 && world.nativeEvents.triggers[enterState].firings==1
                && world.nativeEvents.triggers[handoverState].enabled && world.nativeEvents.inputLocked;
        }
    }
    check(blocked,"an unsupported action is reported while both earlier and later source actions still execute exactly once");
    std::printf("NATIVE_EVENT failures=%d transferred=%zu\n",failures,attached.size());return failures;
}
