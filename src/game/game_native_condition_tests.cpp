#include "game_regression.h"
#include "core/content.h"
#include "sfx/voices.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <set>

int GameRegression::nativeConditionRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_CONDITIONS %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto resume=[&](World& a,int ticks){FILE* file=tmpfile();World b;bool equal=file && a.saveGame(file);const bool written=equal;
        if(equal){rewind(file);equal=b.loadGame(file);}if(file)fclose(file);
        if(!equal)std::printf("NATIVE_CONDITIONS RESUME written=%d loaded=%d events=%d conditions=%d ai=%d tick=%llu\n",written,equal,a.validateNativeEventState(),a.validateNativeConditionState(),a.validateNativeAIState(),(unsigned long long)a.tick);
        if(equal && a.checksum()!=b.checksum())std::printf("NATIVE_CONDITIONS RESUME initial hash=%u/%u condition=%u/%u\n",a.checksum(),b.checksum(),a.nativeConditionChecksum(),b.nativeConditionChecksum());
        equal &= a.checksum()==b.checksum();
        for(int i=0;i<ticks && equal;++i){a.update();b.update();equal &= a.checksum()==b.checksum();
            if(!equal)std::printf("NATIVE_CONDITIONS RESUME frame=%d hash=%u/%u condition=%u/%u\n",i,a.checksum(),b.checksum(),a.nativeConditionChecksum(),b.nativeConditionChecksum());}return equal;};
    const char* previous="review_out/native-campaign/legacy-native45-conditions.sav";
    if(FileExists(previous))check(loadGameFile(previous) && !world.nativeConditions.active && !world.nativeEventDefinition->extendedConditions
        && resume(world,40),"an actual pre-upgrade World45/Scene13 active save retains its old event semantics and resumes");
    const int production=nativeProductionRegression(outputDir);check(production==0,"the real original Tanya, base handover and paid engineer production still work");
    const auto saved=(std::filesystem::path(outputDir)/"native-engineer-produced.sav").string();
    if(!loadGameFile(saved.c_str())){check(false,"the real paid-engineer checkpoint loads");return failures;}
    const auto runtime=[&](World& w,const char* name){const int definition=w.nativeTriggerIndex(name);
        for(int i=0;i<(int)w.nativeEvents.triggers.size();++i)if(w.nativeEvents.triggers[i].definition==definition)return i;return -1;};
    world.update();const int engineerHint=runtime(world,"0B415B3C");
    check(world.nativeConditions.active && engineerHint>=0 && world.nativeConditions.locals[0]==0 && world.nativeConditions.locals[1]==1
        && world.nativeConditions.lastBuilt[0][2]==3 && world.nativeConditions.lastBuilt[0][1]==-1,
        "source local defaults apply; actual engineer release records infantry index 3; authored CMIN reinforcement is not a factory build");
    check(engineerHint>=0 && world.nativeEvents.triggers[engineerHint].firings==1,
        "the original Build Infantry Type engineer hint fires from actual paid production");
    const int gi=world.nativeProductionCode("E1"),e1=world.nativeUnitIndex("E1"),e2=world.nativeUnitIndex("E2"),sapc=world.nativeUnitIndex("SAPC");
    const auto produced=world.producedUnits[0];World::Cmd start{};start.type=World::Cmd::StartUnitProd;start.a=gi;world.applyCmd(0,start);
    for(int i=0;i<2000 && world.producedUnits[0]==produced;++i)world.update();
    check(world.producedUnits[0]==produced+1 && world.nativeConditions.lastBuilt[0][2]==0
        && world.nativeEvents.triggers[engineerHint].eventMask==1
        && !world.nativeExtendedCondition({21,0,3},13,-1,engineerHint) && world.nativeExtendedCondition({21,0,0},13,-1,engineerHint),
        "a real GI factory release replaces the last infantry type instead of retaining an ever-built engineer flag");
    check(resume(world,40),"real production history, local variables and discovery masks resume tick for tick");
    World real=world;real.map.bldOccRef=&real.bldOcc;
    const auto reset=[&](){world=real;world.map.bldOccRef=&world.bldOcc;netGame=false;gameOver=false;sel.clear();selBuilding=-1;};
    const auto bind=[&](EID actor,const char* name){const int r=runtime(world,name);if(r<0 || !world.valid(actor))return -1;
        auto& state=world.nativeEvents.triggers[r];state.enabled=true;state.destroyed=false;state.firings=state.eventMask=0;state.timerStart=world.tick;
        world.nativeEvents.tags[state.tag].alive=true;world.ents[actor].nativeEventTag=state.tag;return r;};
    // Controlled input branch: enable the author's hint and attach it to an
    // actually paid-for GI. This is not a claim of a completed hint timeline.
    reset();EID soldier=-1;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0
        && world.ents[id].nativeIdentity.type==e1){soldier=id;break;}
    const int selected=bind(soldier,"0A96A5EC");
    if(world.valid(soldier) && selected>=0) {
        const int x=(int)world.ents[soldier].x,y=(int)world.ents[soldier].y;
        bool moved=false;for(int radius=2;radius<=5 && !moved;++radius)for(int dy=-radius;dy<=radius && !moved;++dy)for(int dx=-radius;dx<=radius && !moved;++dx) {
            if(std::max(abs(dx),abs(dy))!=radius || !world.map.inBounds(x+dx,y+dy) || world.unitAtCell(x+dx,y+dy)>=0)continue;
            World::Cmd command;command.type=World::Cmd::Move;command.ids={soldier};command.x=x+dx+.5f;command.y=y+dy+.5f;world.applyCmd(0,command);
            moved=world.ents[soldier].state==UState::Moving;
        }
        for(int i=0;i<250 && world.ents[soldier].state==UState::Moving;++i)world.update();
        interpAlpha=1;camZoom=1;auto p=unitScreenPos(world.ents[soldier]);camX+=p.x-(SCREEN_W-sidebarW)/2;camY+=p.y-SCREEN_H/2;
        const auto rect=unitScreenRect(world.ents[soldier]);const int mx=(int)(rect.x+rect.width/2),my=(int)(rect.y+rect.height/2);
        g_unitVoices.setMasterVolume(0);doSelect(mx,my,false);g_unitVoices.setMasterVolume(1);
        check(std::find(sel.begin(),sel.end(),soldier)!=sel.end() && world.nativeEvents.triggers[selected].firings==1
            && world.nativeExtendedCondition({33,0,16},33,soldier,selected),
            "actual projected mouse selection fires the original GI hint even with unit speech muted");
        const auto count=world.nativeEvents.triggers[selected].firings;doSelect(mx,my,false);
        check(world.nativeEvents.triggers[selected].firings==count,"reselecting an already selected actor does not invent another selection occurrence");
        const auto hash=world.checksum();netGame=true;reportNativeSelection({soldier},{});netGame=false;render();
        check(world.checksum()==hash && pendingCmds.empty(),"rendering and network-local selection do not mutate the simulation or enqueue selection packets");
    } else check(false,"an actually trained player GI is available for mouse selection");
    reset();EID recycled=-1;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0 && world.ents[id].nativeIdentity.type==e1){recycled=id;break;}
    if(recycled>=0){const auto identity=world.nativeSelectionIdentity(recycled);const float x=world.ents[recycled].x,y=world.ents[recycled].y;
        world.kill(recycled,false);const EID replacement=world.spawnNativeActor(e1,0,x,y,256,0,"condition:selection-reuse","","Guard");const int r=bind(replacement,"0A96A5EC");
        World::Cmd select{};select.type=World::Cmd::NativeSelect;select.ids={recycled};select.a=(int)identity;world.applyCmd(0,select);
        const bool stale=replacement==recycled && world.nativeEvents.triggers[r].firings==0;
        select.a=(int)world.nativeSelectionIdentity(replacement);world.applyCmd(0,select);
        check(stale && world.nativeEvents.triggers[r].firings==1,"a stale selection identity cannot affect a new source object reusing the same entity slot");}
    reset();const auto old=world.nativeConditions;const int local=runtime(world,"0860EA2C");
    world.setNativeLocal(0,true);const auto at=world.tick;world.update();world.setNativeLocal(0,true);
    check(local>=0 && world.nativeEvents.triggers[local].timerStart==at && world.nativeConditions.locals[0]==1,
        "changing a local resets its dependent timer; assigning the same value again does not reset it");
    const auto hash=world.checksum();world.nativeConditions.locals[0]=0;
    check(world.checksum()!=hash,"local-variable changes participate in lockstep checksum");world.nativeConditions=old;
    // Quiet mechanism branch: retain the original map and source definitions,
    // explicitly disable author events, and remove actors only in this branch.
    const auto quiet=[&](){reset();for(auto& t:world.nativeEvents.triggers)t.enabled=false;
        for(int i=0;i<(int)world.nativeEvents.teams.size();++i)world.finishNativeTeam(i);
        for(auto& house:world.nativeAI.houses){house.enabled=false;house.production=false;}
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        world.projs.clear();world.effects.clear();world.timedBombs.clear();};
    quiet();Vec2i origin{-1,-1};
    for(int y=8;y<world.map.h-8 && origin.x<0;++y)for(int x=8;x<world.map.w-12 && origin.x<0;++x){bool open=true;
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=9;++dx)open &= world.map.inBounds(x+dx,y+dy)
            && world.map.surfacePassable(x+dx,y+dy,0,0,true,true) && world.map.at(x+dx,y+dy).height==world.map.at(x,y).height;
        if(open)origin={x,y};}
    check(origin.x>=0 && sapc>=0,"the source SAPC and a genuine connected original-map area are available");if(origin.x<0 || sapc<0)return failures;
    const EID observer=world.spawnNativeActor(e1,0,origin.x+.5f,origin.y+.5f,256,0,"condition:observer","","Guard");
    const EID victim=world.spawnNativeActor(e1,1,origin.x+8.5f,origin.y+.5f,256,0,"condition:discover","","Guard");
    const int discovered=bind(victim,"073FE71C");
    // Clear previous observation only in this controlled exploration fixture.
    std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_UNSEEN);world.update();
    const bool hidden=world.nativeEvents.triggers[discovered].firings==0;
    World::Cmd move{};move.type=World::Cmd::Move;move.ids={observer};move.x=origin.x+4.5f;move.y=origin.y+.5f;world.applyCmd(0,move);
    for(int i=0;i<200 && world.ents[victim].player!=0;++i)world.update();
    check(hidden && world.nativeEvents.triggers[discovered].firings==1 && world.ents[victim].player==0,
        "real movement reveals an enemy object and the original discovery action transfers its house exactly once");
    check(resume(world,30),"first-discovery marks and the changed ownership survive continuation without replay");
    quiet();const EID attacker=world.spawnNativeActor(e1,0,origin.x+.5f,origin.y+.5f,256,0,"condition:attacker","","Guard");
    const EID target=world.spawnNativeActor(e1,0,origin.x+3.5f,origin.y+.5f,256,0,"condition:attacked","","Guard");
    const int attacked=bind(target,"0945BF3C");world.damage(target,5,0,attacker);
    const bool first=world.nativeEvents.triggers[attacked].firings==1 && world.ents[target].hp==unitDef(world.ents[target]).hp-5;
    for(int i=0;i<31;++i)world.update();const auto before=world.nativeEvents.triggers[attacked].firings;
    world.damage(target,5,0,-1);world.damage(target,0,0,attacker);world.ents[target].invuln=10;world.damage(target,5,0,attacker);world.ents[target].invuln=0;
    const bool rejected=world.nativeEvents.triggers[attacked].firings==before;
    world.damage(target,5,0,attacker);
    check(first && rejected && world.nativeEvents.triggers[attacked].firings==before+1 && world.nativeEvents.triggers[attacked].eventMask==0,
        "positive credited damage includes friendly fire; incidental, zero and immune hits do not fire; attacked is never remembered");
    const EID doomed=world.spawnNativeActor(e1,1,origin.x+5.5f,origin.y+.5f,256,0,"condition:lethal","","Guard");
    const int lethal=bind(doomed,"073FE71C");world.damage(doomed,10000,0,attacker);
    check(!world.valid(doomed) && world.nativeEvents.triggers[lethal].firings==1,"a credited lethal hit offers original discovery before the death notification");
    // Original unload/local/retreat scripts, with explicitly assembled fixture
    // members. Automatic mixed transport-team loading remains a separate gate.
    quiet();const EID boat=world.spawnNativeActor(sapc,1,origin.x+2.5f,origin.y+.5f,256,0,"condition:boat","","Guard");
    std::vector<EID> crew;std::set<std::string> identities;
    for(int n=0;n<5;++n){const auto key="condition:crew:"+std::to_string(n);identities.insert(key);
        crew.push_back(world.spawnNativeActor(e2,1,origin.x+.5f,origin.y+n%2+.5f,256,0,key,"","Guard"));}
    world.orderBoard(crew,boat);
    for(int i=0;i<300 && world.ents[boat].cargo.size()!=5;++i)world.update();
    const int team=world.createNativeTeam(world.nativeTeamIndex("0AB436CC"),false);
    check(team>=0 && world.ents[boat].cargo.size()==5 && unitDef(world.ents[boat]).cargoCap==12 && unitDef(world.ents[boat]).transportSize()==15,
        "five actual source E2 board the original 12-capacity / size-15 SAPC through real boarding orders");if(team<0)return failures;
    world.nativeEvents.teams[team].members={boat};world.nativeEvents.teams[team].assembled=true;world.nativeAIJoined(team,boat);
    world.setNativeTeamScriptStep(team,1);world.ents[boat].tpSick=1000;
    const auto frozen=world.orderUnload({boat});
    check(frozen.released==0 && frozen.remaining==5 && frozen.reason==ReleaseRejectReason::Immobilized,
        "the shared unload entry rejects a frozen carrier and reports its five retained passengers");world.update();
    std::printf("NATIVE_CONDITIONS UNLOAD pending=%d keys=%zu cargo=%zu step=%d started=%d event_valid=%d condition_valid=%d\n",world.nativeConditions.unloading.contains(team),world.nativeConditions.unloading.contains(team)?world.nativeConditions.unloading.at(team).size():0,world.ents[boat].cargo.size(),world.nativeEvents.teams[team].step,world.nativeEvents.teams[team].lineStarted,world.validateNativeEventState(),world.validateNativeConditionState());
    check(world.nativeConditions.unloading.contains(team) && world.nativeConditions.unloading[team].size()==5
        && world.ents[boat].cargo.size()==5 && resume(world,20),"a frozen source unload keeps actual passengers and pending identities across a mid-script save");
    world.ents[boat].tpSick=0;const int retreat=runtime(world,"0860EA2C");world.nativeEvents.triggers[retreat].enabled=true;
    check(resume(world,80),"unload, Set Local, real retreat-team creation and movement continue identically after a snapshot");
    bool same=world.ents[boat].cargo.empty() && world.nativeConditions.locals[0]==1 && !world.nativeConditions.unloading.contains(team);
    std::set<std::string> released;for(EID id:world.nativeEvents.teams[team].members)if(world.valid(id))released.insert(world.ents[id].nativeIdentity.object);
    bool recruited=false;for(const auto& t:world.nativeEvents.teams)if(t.alive && t.definition==world.nativeTeamIndex("0AB4920C"))
        recruited |= std::find(t.members.begin(),t.members.end(),boat)!=t.members.end();
    check(same && released==identities && recruited && world.nativeEvents.triggers[retreat].firings==1,
        "source unload=2 keeps all five real infantry, detaches the boat, and local 0 lets the author's retreat team recruit it");
    const auto checkpoint=(std::filesystem::path(outputDir)/"native-conditions-unloaded.sav").string();
    check(saveGameFile(checkpoint.c_str()) && loadGameFile(checkpoint.c_str()),"application v10 / World46 restores active source locals, discovery and transport script state");
    const auto state=world.nativeConditions;world.nativeConditions.locals[0]=2;const bool badLocal=!world.validateNativeConditionState();world.nativeConditions=state;
    world.nativeConditions.lastBuilt[0][2]=100000;const bool badType=!world.validateNativeConditionState();world.nativeConditions=state;
    world.nativeConditions.discoveredHumans["nonexistent:future-actor"]=1;const bool badIdentity=!world.validateNativeConditionState();world.nativeConditions=state;
    check(badLocal && badType && badIdentity,"invalid booleans, original type indices and nonexisting discovery identities are rejected");
    {
        FILE* file=tmpfile();bool serialized=file && world.writeNativeConditionState(file);std::vector<uint8_t> bytes;
        if(serialized){const long size=ftell(file);rewind(file);bytes.resize((size_t)size);serialized=fread(bytes.data(),1,bytes.size(),file)==bytes.size();}if(file)fclose(file);
        const auto rejected=[&](const std::vector<uint8_t>& data){FILE* input=tmpfile();World probe=world;probe.map.bldOccRef=&probe.bldOcc;bool accepted=false;
            if(input){if(fwrite(data.data(),1,data.size(),input)==data.size()){rewind(input);accepted=probe.readNativeConditionState(input);}fclose(input);}return !accepted;};
        bool corrupt=serialized && bytes.size()>2+(size_t)world.numPlayers*16+50+4;
        if(corrupt){auto data=bytes;data[1]=2;corrupt &= rejected(data);data=bytes;const uint32_t huge=0xffffffff;
            memcpy(data.data()+2+world.numPlayers*16+50,&huge,4);corrupt &= rejected(data);data=bytes;data.pop_back();corrupt &= rejected(data);}
        check(corrupt,"binary condition flags, oversized record counts and truncation are rejected before unbounded allocation");
    }
    // Explicit observer branch, solely for the newly imported transport art.
    localPlayer=1;for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);sel={boat};selBuilding=-1;interpAlpha=1;camZoom=1;syncFogKnowledge();bakeTerrain();
    for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){SetWindowSize(size.x,size.y);render();auto point=unitScreenPos(world.ents[boat]);camX+=point.x-(SCREEN_W-sidebarW)/2;camY+=point.y-SCREEN_H/2;render();
        const auto path=(std::filesystem::path(outputDir)/("native-conditions-"+std::to_string(size.x)+".png")).string();
        shotAtDisplaySize=true;shotFile=path;const auto checksum=world.checksum();render();Image frame=LoadImage(path.c_str());
        check(frame.data && frame.width==size.x && frame.height==size.y && world.checksum()==checksum,
            "the actual SAPC/crew display framebuffer has the requested dimensions and does not mutate simulation");UnloadImage(frame);}
    localPlayer=0;
    reset();std::printf("NATIVE_CONDITIONS failures=%d (source conditions and explicit transport fixture; not complete campaign proof)\n",failures);return failures;
}
