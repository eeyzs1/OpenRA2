#include "game_regression.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

int GameRegression::nativeAIRegression(const char* outputDir) {
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str()))return 77;
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* message){std::printf("NATIVE_AI %s: %s\n",ok?"PASS":"FAIL",message);std::fflush(stdout);failures+=!ok;};
    const auto resume=[&](int ticks){FILE* file=tmpfile();World other;bool ok=file && world.saveGame(file);
        if(ok){rewind(file);ok=other.loadGame(file);}if(file)fclose(file);ok &= world.checksum()==other.checksum();
        for(int i=0;i<ticks && ok;++i){world.update();other.update();ok &= world.checksum()==other.checksum();}return ok;};
    const char* old="review_out/native-campaign/legacy-native43-ai.sav";
    if(FileExists(old))check(loadGameFile(old) && !world.nativeAIRules.available && resume(25),
        "an actual pre-upgrade World43/Scene11 paid-engineer save keeps its historical AI boundary and exact continuation");
    // Reuse the actual original Tanya/base/paid-barracks/paid-engineer journey,
    // including real input. It leaves the real repaired source world installed.
    const int journey=nativeProductionRegression(outputDir);if(journey==77)return 77;
    failures+=journey;if(journey)return failures;
    check(world.nativeAI.active && world.nativeAIRules.ignoreGlobal && world.nativeAIRules.triggers.size()==6
        && world.nativeAI.houses[1].enabled && world.nativeAI.houses[1].production,
        "the real engineer's hut entry starts exactly the authored local AI and production switches");
    const int e2=world.nativeUnitIndex("E2");const auto initialCount=world.producedUnits[1];
    EID tanya=INVALID_EID;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0
        && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->id=="TANY")tanya=id;
    const auto tanyaObject=world.ents[tanya].nativeIdentity.object;
    const auto heroAlive=[&](){return world.valid(tanya) && world.ents[tanya].nativeIdentity.object==tanyaObject;};
    const int gi=world.nativeProductionCode("E1");EID barracks=INVALID_EID;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0 && world.ents[id].nativeIdentity.definition
        && world.ents[id].nativeIdentity.definition->id=="GAPILE")barracks=id;
    World::Cmd rally{};rally.type=World::Cmd::SetRally;rally.ids={barracks};rally.x=78.5f;rally.y=44.5f;world.applyCmd(0,rally);
    for(int i=0;i<12;++i){World::Cmd train{};train.type=World::Cmd::StartUnitProd;train.a=gi;world.applyCmd(0,train);}
    std::vector<std::pair<EID,std::string>> army;
    const auto aliveGI=[&](const auto& member){return world.valid(member.first) && world.ents[member.first].nativeIdentity.object==member.second;};
    bool returning=false;
    for(int i=0;i<4000 && army.size()<12;++i){
        if(i%15==0 && heroAlive()){EID nearest=-1;float distance=1e9f;bool western=false;
            for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==1 && world.ents[id].nativeIdentity.type==e2 && world.ents[id].x<80){
                western=true;const float d=distf(world.ents[tanya].x,world.ents[tanya].y,world.ents[id].x,world.ents[id].y);
                if(d<distance && world.attackRejection(tanya,id)==AttackRejectReason::None){distance=d;nearest=id;}}
            if(nearest>=0){World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={tanya};attack.a=nearest;world.applyCmd(0,attack);}
            else if((western && world.ents[tanya].state==UState::Idle) || (!western && !returning)){
                World::Cmd move{};move.type=World::Cmd::Move;move.ids={tanya};move.x=western?40.5f:64.5f;move.y=western?31.5f:39.5f;world.applyCmd(0,move);returning=!western;}}
        world.update();army.clear();
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0 && world.ents[id].nativeIdentity.type==gi-NATIVE_PRODUCTION_BASE
            && world.ents[id].nativeIdentity.object.rfind("production:0:",0)==0)army.emplace_back(id,world.ents[id].nativeIdentity.object);}
    check(army.size()==12 && heroAlive(),"the player actually pays for twelve native GI and rallies them through the real barracks while keeping the original Tanya alive");
    if(army.size()!=12)return failures+1;
    World paid;bool capturedPaid=false,parallel=false,assembled=false;
    for(int tick=0;tick<12000;++tick){
        // The author already supplies 22 conscripts. Genuine combat losses,
        // rather than deleting that supply, create the new factory demand.
        for(const auto& member:army)if(aliveGI(member)){const auto& e=world.ents[member.first];EID visible=-1,nearest=-1;float vd=1e9f,nd=1e9f;
            for(EID id=0;id<(EID)world.ents.size();++id){const auto& target=world.ents[id];if(!target.alive || target.player!=1 || target.nativeIdentity.type!=e2 || target.x<80)continue;
                const float distance=distf(e.x,e.y,target.x,target.y);if(distance<nd){nd=distance;nearest=id;}
                if(distance<vd && world.attackRejection(member.first,id)==AttackRejectReason::None){vd=distance;visible=id;}}
            const bool retreat=capturedPaid;
            if((e.deployed && (retreat || visible<0 || vd>7)) || (!e.deployed && !retreat && visible>=0 && vd<6)){
                World::Cmd deploy{};deploy.type=World::Cmd::Deploy;deploy.ids={member.first};world.applyCmd(0,deploy);}
            if(!retreat && visible>=0){if(e.target!=visible){World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={member.first};attack.a=visible;world.applyCmd(0,attack);}}
            else if(!e.deployed && (e.state==UState::Idle || e.state==UState::Chasing || e.state==UState::Attacking)){
                World::Cmd move{};move.type=World::Cmd::Move;move.ids={member.first};
                move.x=retreat || nearest<0?78.5f:world.ents[nearest].x;move.y=retreat || nearest<0?44.5f:world.ents[nearest].y;world.applyCmd(0,move);}
        }
        world.update();int jobs=0;
        for(const auto& j:world.nativeAI.jobs)if(j.house==1 && j.type==e2){++jobs;
            if(j.progress>0 && j.progress<54 && !capturedPaid){paid=world;paid.map.bldOccRef=&paid.bldOcc;capturedPaid=true;}}
        parallel |= jobs==2;
        for(const auto& t:world.nativeEvents.teams)if(t.alive && t.memberHouse==1 && t.assembled && world.nativeAI.autocreate[t.definition])assembled=true;
        if(capturedPaid && parallel && assembled && world.producedUnits[1]>=initialCount+2)break;
    }
    int produced=0;for(const auto& e:world.ents)if(e.alive && e.player==1 && e.nativeIdentity.type==e2 && e.nativeIdentity.object.rfind("production:1:",0)==0)++produced;
    std::printf("NATIVE_AI actual tick=%llu produced_total=%llu alive_source=%d jobs=%zu teams=%zu\n",(unsigned long long)world.tick,
        (unsigned long long)world.producedUnits[1],produced,world.nativeAI.jobs.size(),world.nativeEvents.teams.size());
    std::printf("NATIVE_AI actual Tanya alive=%d hp=%d pos=%.2f,%.2f outcome=%d\n",world.valid(tanya),world.ents[tanya].hp,
        world.ents[tanya].x,world.ents[tanya].y,(int)world.nativeEvents.outcome);
    if(!capturedPaid){const auto& h=world.nativeAI.houses[1];std::printf("NATIVE_AI DIAG house ai=%d money=%d switches=%d,%d choice=%llu wanted=%d rejection=%d\n",
        world.players[1].isAI,world.players[1].money,h.enabled,h.production,(unsigned long long)h.nextChoice,h.wanted[0],(int)world.productionStartRejection(1,true,NATIVE_PRODUCTION_BASE+e2));
        for(int i=0;i<(int)world.nativeAIRules.triggers.size();++i)std::printf("NATIVE_AI DIAG trigger=%s eligible=%d weight=%.2f\n",world.nativeAIRules.triggers[i].id.c_str(),world.nativeAITriggerEligible(1,i),world.nativeAI.triggers[i].weight);
        for(const auto& t:world.nativeEvents.teams)if(t.alive && t.memberHouse==1)std::printf("NATIVE_AI DIAG team=%s members=%zu assembled=%d auto=%d\n",
            world.nativeEventDefinition->teams[t.definition].id.c_str(),t.members.size(),t.assembled,world.nativeAI.autocreate[t.definition]);
        int originals=0;for(const auto& e:world.ents)originals+=e.alive && e.player==1 && e.nativeIdentity.type==e2 && e.nativeIdentity.object.rfind("production:",0)!=0;
        const auto& t=world.ents[tanya];std::printf("NATIVE_AI DIAG originals=%d tanyaState=%d target=%d cd=%d locked=%d outcome=%s\n",originals,(int)t.state,t.target,t.atkCd,
            world.nativeEvents.inputLocked,world.nativeEvents.outcomeTrigger>=0?world.nativeEventDefinition->triggers[world.nativeEvents.outcomeTrigger].name.c_str():"none");
        if(world.valid(t.target))std::printf("NATIVE_AI DIAG target pos=%.2f,%.2f hp=%d height=%d source=%s\n",world.ents[t.target].x,world.ents[t.target].y,world.ents[t.target].hp,
            world.actorSurfaceHeight(world.ents[t.target]),world.ents[t.target].nativeIdentity.definition->id.c_str());
    }
    check(capturedPaid && parallel && assembled && world.producedUnits[1]>=initialCount+2 && produced>0
        && !world.players[1].unitProd[0].active && world.nativeEvents.outcome==NativeMissionOutcome::Running,
        "two actual authored barracks pay independent 54-step jobs, release native E2 and assemble original teams without using the player queue");
    if(!capturedPaid)return failures+1;
    World natural=world;natural.map.bldOccRef=&natural.bldOcc;
    check(resume(120),"active AI selection, parallel factory work, permissions and team state resume tick-identically");
    const auto save=(std::filesystem::path(outputDir)/"native-ai-active.sav").string();const auto hash=world.checksum();
    check(saveGameFile(save.c_str()) && loadGameFile(save.c_str()) && hash==world.checksum(),"application v10 / World45 restores the actual active original AI");
    world=paid;world.map.bldOccRef=&world.bldOcc;
    for(const auto& member:army)if(aliveGI(member) && world.ents[member.first].deployed){World::Cmd pack{};pack.type=World::Cmd::Deploy;pack.ids={member.first};world.applyCmd(0,pack);}
    World::Cmd retreat{};retreat.type=World::Cmd::Move;retreat.x=78.5f;retreat.y=44.5f;
    for(const auto& member:army)if(aliveGI(member))retreat.ids.push_back(member.first);world.applyCmd(0,retreat);
    const auto doorJob=*std::find_if(world.nativeAI.jobs.begin(),world.nativeAI.jobs.end(),[&](const auto& j){return j.house==1 && j.type==e2;});
    const auto exits=world.factoryExitCells(doorJob.actor,world.nativeTypes[e2]->unit);std::vector<EID> blockers;
    for(auto cell:exits){EID id=world.unitAtCell(cell.x,cell.y);
        if(!world.valid(id))id=world.spawnNativeActor(e2,1,cell.x+.5f,cell.y+.5f,256,0,"counter:ai-blocked-door:"+std::to_string(blockers.size()),"","Guard");
        if(world.valid(id)){world.ents[id].tpSick=1000;world.ents[id].invuln=1000;blockers.push_back(id);}}
    const EID blocker=blockers.empty()?INVALID_EID:blockers.front();
    const auto beforeBlocked=world.producedUnits[1];for(int i=0;i<130;++i)world.update();
    const auto blocked=std::find_if(world.nativeAI.jobs.begin(),world.nativeAI.jobs.end(),[&](const auto& j){return j.actor==doorJob.actor;});
    std::printf("NATIVE_AI DOOR actor=%d type=%d blocker=%d progress=%d paid=%d released=%llu valid=%d\n",doorJob.actor,doorJob.type,world.valid(blocker),
        blocked==world.nativeAI.jobs.end()?-1:blocked->progress,blocked==world.nativeAI.jobs.end()?-1:blocked->paid,
        (unsigned long long)(world.producedUnits[1]-beforeBlocked),world.validateNativeAIState());
    if(world.producedUnits[1]==beforeBlocked)for(const auto& j:world.nativeAI.jobs)std::printf("NATIVE_AI DOOR other actor=%d type=%d progress=%d paid=%d\n",j.actor,j.type,j.progress,j.paid);
    check(world.valid(blocker) && blocked!=world.nativeAI.jobs.end() && blocked->progress==54 && blocked->paid==100
        && world.producedUnits[1]>beforeBlocked && resume(10),
        "an explicit occupied-door counterbranch retains one paid AI job while the other original barracks releases its own unit");
    const auto beforeClear=world.producedUnits[1];if(world.valid(blocker)){world.ents[blocker].tpSick=0;World::Cmd move{};
        move.type=World::Cmd::Move;move.ids={blocker};move.x=world.ents[blocker].x+3;move.y=world.ents[blocker].y;world.applyCmd(world.ents[blocker].player,move);}
    for(int i=0;i<100 && std::any_of(world.nativeAI.jobs.begin(),world.nativeAI.jobs.end(),[&](const auto& j){return j.actor==doorJob.actor && j.progress==54;});++i)world.update();
    check(world.producedUnits[1]==beforeClear+1,"a real move clearing the counterbranch door releases exactly its one completed AI object");
    world=paid;world.map.bldOccRef=&world.bldOcc;world.players[1].money=0;
    std::vector<int> progress;for(const auto& j:world.nativeAI.jobs)progress.push_back(j.progress);
    for(int i=0;i<20;++i)world.update();bool stopped=progress.size()==world.nativeAI.jobs.size();
    for(size_t i=0;i<progress.size() && i<world.nativeAI.jobs.size();++i)stopped &= progress[i]==world.nativeAI.jobs[i].progress;
    check(stopped && world.players[1].money==0 && resume(5),"an explicit zero-funds branch pauses paid factory work without negative credits or free progress");
    world=paid;world.map.bldOccRef=&world.bldOcc;const auto lost=world.nativeAI.jobs.front();
    world.damage(lost.actor,100000,0);const auto replacement=world.spawnUnit(1,UnitType::Conscript,89.5f,69.5f);
    check(std::none_of(world.nativeAI.jobs.begin(),world.nativeAI.jobs.end(),[&](const auto& j){return j.actor==lost.actor;})
        && world.valid(replacement) && resume(15),"destroying a factory discards its own job and never transfers work to a reused entity slot");
    world=paid;world.map.bldOccRef=&world.bldOcc;world.changeNativeHouse(-1,0,true,1);
    check(resume(1) && std::none_of(world.nativeAI.jobs.begin(),world.nativeAI.jobs.end(),[&](const auto& j){return j.actor==lost.actor;}),
        "an authored whole-house ownership change clears the former owner's factory work and preserves exact continuation");
    for(int difficulty=0;difficulty<3;++difficulty){World probe;bool ok=probe.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && probe.loadNativeScene(scene.c_str()) && probe.startNativeEvents(difficulty);int eligible=0;
        if(ok)for(int i=0;i<(int)probe.nativeAIRules.triggers.size();++i)if(probe.nativeAITriggerEligible(1,i)){
            ++eligible;const auto& d=probe.nativeEventDefinition->teams[probe.nativeAIRules.triggers[i].first];
            int wanted=0;for(const auto& [n,id]:probe.nativeEventDefinition->taskForces[d.taskForce].units)wanted+=n;
            ok &= wanted==(difficulty==0?2:difficulty==1?5:10) && d.max==difficulty+2;}
        check(ok && eligible==2,"source difficulty gates select exactly its attack/defense pair and original 2/5/10 roster and Max2/3/4");
        if(ok){const int first=probe.nativeAIRules.triggers[difficulty==0?0:difficulty==1?2:3].first;
            const int maximum=probe.nativeEventDefinition->teams[first].max;
            for(int i=0;i<maximum;++i)ok &= probe.createNativeTeam(first,false)>=0;
            for(int i=0;i<(int)probe.nativeAIRules.triggers.size();++i)if(probe.nativeAIRules.triggers[i].first==first)ok &= !probe.nativeAITriggerEligible(1,i);
            check(ok,"automatic eligibility enforces Max while explicit Create Team retains its independent semantics");}
    }
    world=natural;world.map.bldOccRef=&world.bldOcc;
    bool tracked=false;for(size_t i=0;i<world.nativeAI.triggers.size();++i){const auto& s=world.nativeAI.triggers[i];
        if(s.runs && !s.successes && s.weight==60)tracked=true;}
    check(tracked,"actual original Guard(0) team retirement has recorded unsuccessful runs using the source -50 delta and minimum weight 60");
    World::Cmd stop{};stop.type=World::Cmd::Stop;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==1 && world.ents[id].nativeIdentity.type==e2)stop.ids.push_back(id);
    world.applyCmd(1,stop);
    const int attackType=world.nativeAIRules.triggers.front().first;
    const auto stagedTeam=[&](){const int id=world.createNativeTeam(attackType,false);if(id<0)return id;
        world.recruitNativeAITeam(id);world.recruitNativeAITeam(id);world.nativeEvents.teams[id].assembled=world.nativeEvents.teams[id].members.size()==2;return id;};
    const auto runs=world.nativeAI.triggers[0].runs,successes=world.nativeAI.triggers[0].successes;
    const int failure=stagedTeam();if(failure>=0)world.finishNativeTeam(failure);
    const int success=stagedTeam();bool originalSuccess=success>=0 && world.nativeEvents.teams[success].assembled;
    if(originalSuccess){world.setNativeTeamScriptStep(success,1);world.updateNativeTeamControl(success);originalSuccess=world.nativeEvents.teams[success].succeeded;
        world.finishNativeTeam(success);world.finishNativeTeam(success);}
    check(originalSuccess && world.nativeAI.triggers[0].runs==runs+2 && world.nativeAI.triggers[0].successes==successes+1
        && world.nativeAI.triggers[0].weight==70 && resume(1),
        "an explicit script-cursor branch uses real recruited E2, original Success(49) and source +20 to restore weight 70, recording each removal once");
    world=natural;world.map.bldOccRef=&world.bldOcc;
    int team=-1;for(int i=0;i<(int)world.nativeEvents.teams.size();++i)if(world.nativeEvents.teams[i].alive && world.nativeEvents.teams[i].memberHouse==1
        && world.nativeAI.autocreate[world.nativeEvents.teams[i].definition]){team=i;break;}
    if(team>=0){const int definition=world.nativeEvents.teams[team].definition;int trigger=-1;
        for(int i=0;i<(int)world.nativeAIRules.triggers.size();++i)if(world.nativeAIRules.triggers[i].first==definition)trigger=i;
        const auto previous=world.nativeAI.triggers[trigger].runs;world.finishNativeTeam(team);
        check(world.nativeAI.triggers[trigger].runs==previous+1 && world.nativeAI.triggers[trigger].weight==60 && resume(1),
            "removing an actual team before Success records failure once and clamps the original -50 delta at the source minimum 60");}
    world=natural;world.map.bldOccRef=&world.bldOcc;
    FILE* f=tmpfile();std::vector<uint8_t> bytes;bool serialized=f && world.saveGame(f);
    if(serialized){const auto n=ftell(f);rewind(f);bytes.resize((size_t)n);serialized=fread(bytes.data(),1,bytes.size(),f)==bytes.size();}if(f)fclose(f);
    const auto rejected=[&](std::vector<uint8_t> corrupt){FILE* input=tmpfile();World copy;bool accepted=false;
        if(input){if(fwrite(corrupt.data(),1,corrupt.size(),input)==corrupt.size()){rewind(input);accepted=copy.loadGame(input);}fclose(input);}return !accepted;};
    bool corrupt=serialized;if(serialized){auto data=bytes;data.pop_back();corrupt &= rejected(data);data=bytes;data.push_back(0);corrupt &= rejected(data);
        auto saved=world.nativeAI;world.nativeAI.houses[1].wanted[0]=100000;corrupt &= !world.validateNativeAIState();world.nativeAI=saved;
        world.nativeAI.triggers[0].successes=world.nativeAI.triggers[0].runs+1;corrupt &= !world.validateNativeAIState();world.nativeAI=saved;}
    check(corrupt,"truncated/trailing AI saves and invalid production selectors or outcome counters are rejected");
    localPlayer=1;for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);syncFogKnowledge();
    clearPlacement();sel.clear();selBuilding=-1;camZoom=1;interpAlpha=1;bakeTerrain();
    for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){SetWindowSize(size.x,size.y);render();int x,y;tileToScreen(89,68,x,y);
        camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;shotAtDisplaySize=true;
        shotFile=(std::filesystem::path(outputDir)/("native-ai-"+std::to_string(size.x)+".png")).string();
        const auto before=world.checksum();render();check(before==world.checksum(),"actual source factories and AI-produced units draw without changing the simulation");}
    localPlayer=0;std::printf("NATIVE_AI failures=%d\n",failures);return failures;
}
