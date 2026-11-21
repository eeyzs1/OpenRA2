#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>

int GameRegression::nativeProductionRegression(const char* outputDir) {
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str())){std::printf("NATIVE_PRODUCTION SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_PRODUCTION %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    g_script.shutdown();ais.clear();phase=Phase::InGame;localPlayer=0;gameOver=victory=paused=showMenu=netGame=false;campaignMission=-1;clearPlacement();
    const auto resume=[&](int ticks){FILE* file=tmpfile();World restored;bool exact=file && world.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);exact &= world.checksum()==restored.checksum();
        for(int i=0;i<ticks && exact;++i){world.update();restored.update();exact &= world.checksum()==restored.checksum();}return exact;};
    const auto old="review_out/native-campaign/legacy-native42-production.sav";
    if(FileExists(old)){const bool loaded=loadGameFile(old);check(loaded && !world.nativeProductionRules.available && resume(20),
        "an actual World42/Scene10 save preserves its old production boundary and successful script state across resumed ticks");}
    int mission=-1;for(int i=0;i<(int)missionTable().size();++i)
        if(missionTable()[i].nativeScene=="assets/maps/native/all01t/scene.ini"){mission=i;break;}
    if(mission>=0)newCampaignGame(mission,false);
    if(mission<0 || campaignMission!=mission || !world.nativeEvents.active){check(false,"the source production journey starts through the installed campaign entry");return failures;}
    const int barracks=world.nativeProductionCode("GAPILE"),engineer=world.nativeProductionCode("ENGINEER");
    check(barracks>=0 && engineer>=0 && world.nativeProductionRules.techLevels[0]==3
        && world.nativeTypes[engineer-NATIVE_PRODUCTION_BASE]->unit.hp==75
        && world.nativeTypes[engineer-NATIVE_PRODUCTION_BASE]->unit.cost==500,
        "the source house tech gate, native barracks and 75-HP / 500-credit engineer are present");
    if(barracks<0 || engineer<0)return failures;
    check(world.productionStartRejection(0,false,barracks)==ProductionRejectReason::MissingFactory
        && world.productionStartRejection(0,true,(int)UnitType::Engineer)==ProductionRejectReason::Unavailable,
        "the unrecovered base cannot produce, and the native scene refuses a prototype-unit substitution");
    while(world.tick<2400 && (world.tick<1650 || world.nativeEvents.inputLocked))world.update();
    const int entry=world.nativeTriggerIndex("096564CC"),handover=world.nativeTriggerIndex("0965E07C");int tag=-1,runtime=-1;
    for(int i=0;i<(int)world.nativeEventDefinition->tags.size();++i)if(world.nativeEventDefinition->tags[i].trigger==entry)tag=i;
    for(int i=0;i<(int)world.nativeEvents.triggers.size();++i)if(world.nativeEvents.triggers[i].definition==handover)runtime=i;
    EID mover=-1;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0
        && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->id=="TANY"){mover=id;break;}
    std::vector<Vec2i> cells;for(const auto& [cell,binding]:world.nativeEventDefinition->cells)if(binding==tag)cells.push_back(cell);
    if(world.valid(mover))std::stable_sort(cells.begin(),cells.end(),[&](auto a,auto b){return distf(world.ents[mover].x,world.ents[mover].y,a.x+.5f,a.y+.5f)<distf(world.ents[mover].x,world.ents[mover].y,b.x+.5f,b.y+.5f);});
    Vec2i destination{-1,-1};
    for(auto cell:cells)if(world.valid(mover) && ((int)world.ents[mover].x!=cell.x || (int)world.ents[mover].y!=cell.y)) {
        World probe=world;probe.map.bldOccRef=&probe.bldOcc;World::Cmd move{};move.type=World::Cmd::Move;move.ids={mover};move.x=cell.x+.5f;move.y=cell.y+.5f;probe.applyCmd(0,move);
        if(probe.ents[mover].state==UState::Moving && probe.ents[mover].goalX==move.x && probe.ents[mover].goalY==move.y){destination=cell;break;}
    }
    const auto moverKey=world.valid(mover)?world.ents[mover].nativeIdentity.object:std::string{};
    if(destination.x>=0){World::Cmd move{};move.type=World::Cmd::Move;move.ids={mover};move.x=destination.x+.5f;move.y=destination.y+.5f;world.applyCmd(0,move);}
    int moveBudget=3600;
    if(world.valid(mover) && world.ents[mover].nativeIdentity.definition->walkSpeedLeptons>=0) {
        const auto& actor=world.ents[mover];float length=0,x=actor.x,y=actor.y;
        for(const auto& cell:actor.path){const float nx=cell.x+.5f,ny=cell.y+.5f;length+=distf(x,y,nx,ny);x=nx;y=ny;}
        const float step=actor.nativeIdentity.definition->walkSpeedLeptons*(15.f/(256.f*LOGIC_FPS));
        if(step>0)moveBudget=std::min(18000,(int)std::ceil(length/step)+30*LOGIC_FPS);
        std::printf("NATIVE_PRODUCTION route length=%.3f base_step=%.7f budget=%d\n",length,step,moveBudget);
    }
    for(int i=0;i<moveBudget && runtime>=0 && world.nativeEvents.triggers[runtime].firings==0;++i) {
        if(!world.valid(mover) || world.ents[mover].nativeIdentity.object!=moverKey)break;
        if(i%15==0 && destination.x>=0) {
            const auto& actor=world.ents[mover];EID threat=INVALID_EID;float nearest=6;
            for(EID enemy=0;enemy<(EID)world.ents.size();++enemy){const auto& target=world.ents[enemy];
                if(!target.alive || target.isBuilding || !world.isEnemy(0,target.player) || !unitDef(target).isInfantry()
                    || world.map.fogAt(0,(int)target.x,(int)target.y)!=FOG_VISIBLE || world.attackRejection(mover,enemy)!=AttackRejectReason::None)continue;
                const float distance=distf(actor.x,actor.y,target.x,target.y);if(distance<nearest){nearest=distance;threat=enemy;}}
            if(threat>=0 && actor.target!=threat){World::Cmd fight{};fight.type=World::Cmd::Attack;fight.ids={mover};fight.a=threat;world.applyCmd(0,fight);}
            else if(threat<0 && actor.state!=UState::Moving){World::Cmd move{};move.type=World::Cmd::Move;move.ids={mover};move.x=destination.x+.5f;move.y=destination.y+.5f;world.applyCmd(0,move);}
        }
        world.update();
    }
    const bool recovered=runtime>=0 && world.nativeEvents.triggers[runtime].firings==1;
    std::printf("NATIVE_PRODUCTION entry tick=%llu tanya=%d destination=%d,%d recovered=%d\n",(unsigned long long)world.tick,mover,destination.x,destination.y,recovered);
    check(recovered && world.valid(mover) && world.ents[mover].nativeIdentity.object==moverKey,"normal player move/visible-threat attack commands take the original Tanya to a real CellTag and its delayed base handover");
    if(!recovered){if(world.valid(mover)){const auto& actor=world.ents[mover];std::printf("NATIVE_PRODUCTION BLOCKED key=%s expected=%s pos=%.3f,%.3f hp=%d state=%d path=%d/%zu target=%d\n",actor.nativeIdentity.object.c_str(),moverKey.c_str(),actor.x,actor.y,actor.hp,(int)actor.state,actor.pathIdx,actor.path.size(),actor.target);}
        const auto blocked=(std::filesystem::path(outputDir)/"native-production-blocked.sav").string();saveGameFile(blocked.c_str());return failures;}
    check(world.productionStartRejection(0,false,barracks)==ProductionRejectReason::None
        && world.productionStartRejection(0,true,engineer)!=ProductionRejectReason::None,
        "recovered source power and construction yard unlock a barracks, while engineering still needs that barracks");
    const auto command=[&](World::Cmd::Type action,int selector,int value=0){World::Cmd cmd{};cmd.type=action;cmd.a=selector;cmd.b=value;world.applyCmd(0,cmd);};
    command(World::Cmd::StartBldProd,barracks);for(int i=0;i<9;++i)world.update();
    check(world.players[0].bldProd.progress==0 && world.players[0].bldProd.paid==0,"native construction does not charge or advance before the first source production-step deadline");
    world.update();check(world.players[0].bldProd.progress==1 && world.players[0].bldProd.paid==9,
        "the 54-step source build takes its first 9-credit installment at tick10 rather than prototype per-frame payment");
    World early=world;early.map.bldOccRef=&early.bldOcc;
    command(World::Cmd::HoldBldProd,barracks,1);const auto held=world.players[0].bldProd;
    for(int i=0;i<20;++i)world.update();
    check(world.players[0].bldProd.progress==held.progress && world.players[0].bldProd.paid==held.paid && resume(5),"native HOLD freezes progress and payments and survives a snapshot");
    command(World::Cmd::HoldBldProd,barracks,0);check(resume(20),"partly paid source construction resumes identically");
    for(int i=0;i<2000 && !world.players[0].bldProd.ready;++i)world.update();
    check(world.players[0].bldProd.ready && world.players[0].bldProd.paid==500 && world.players[0].bldProd.progress==54,
        "the source barracks reaches exactly 54 steps and 500 paid credits before placement");
    Vec2i site{-1,-1};float nearest=1e9f;const auto& b=world.nativeTypes[barracks-NATIVE_PRODUCTION_BASE]->building;
    EID yard=-1;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0
        && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->id=="GACNST")yard=id;
    for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x)if(world.canPlaceNativeBuilding(0,barracks,x,y)
        && world.map.surfacePassable(x+b.w/2,y+b.h,0,0,true,true) && world.unitAtCell(x+b.w/2,y+b.h)==INVALID_EID) {
        const float distance=world.valid(yard)?distf(x,y,world.ents[yard].x,world.ents[yard].y):0;
        if(distance<nearest){nearest=distance;site={x,y};}
    }
    check(site.x>=0,"a legal native flat foundation and clear barracks door exist beside the recovered base");if(site.x<0)return failures;
    // Exercise the real sidebar and placement intent. Deferring the command
    // tests that the UI cannot spend or place ahead of its World receipt.
    auditHud=true;sim=SimInput{};sim.active=true;sim.pos={-100,-100};uiTab=0;uiScroll=0;camZoom=.85f;SetTargetFPS(0);SetWindowSize(1280,720);bakeTerrain();render();
    int px,py;tileToScreen(site.x+b.w-1,site.y+b.h-1,px,py);camX=px-(SCREEN_W-sidebarW)/(2*camZoom);camY=py-SCREEN_H/(2*camZoom);render();
    Rectangle icon{};for(const auto& item:hudItems)if(item.type==barracks)icon=item.bounds;
    const auto before=world.checksum();sim.pos={icon.x+icon.width/2,icon.y+icon.height/2};sim.pressedL=true;render();sim.pressedL=false;
    check(icon.width>0 && placing && nativePlacementType==barracks && before==world.checksum(),"the original barracks cameo opens its native preview without changing simulation");
    sim.pos={(px-camX)*camZoom,(py+TILE_H/2-heightScreenY(world.map.at(site.x+b.w-1,site.y+b.h-1).height)-camY)*camZoom};
    netGame=true;sim.pressedL=true;handleInput();sim.pressedL=false;
    std::printf("NATIVE_PRODUCTION placement site=%d,%d mouse=%.1f,%.1f pending=%zu preview=%d selector=%d\n",site.x,site.y,sim.pos.x,sim.pos.y,pendingCmds.size(),placing,nativePlacementType);
    if(!pendingCmds.empty())std::printf("NATIVE_PRODUCTION placement command=%d x=%.2f y=%.2f expected=%d\n",pendingCmds.front().a,pendingCmds.front().x,pendingCmds.front().y,barracks);
    const bool queued=pendingCmds.size()==1 && pendingCmds.front().a==barracks && before==world.checksum();
    for(const auto& cmd:pendingCmds)world.applyCmd(0,cmd);pendingCmds.clear();consumePresentationEvents();netGame=false;
    EID factory=world.bldAt(site.x,site.y);
    std::printf("NATIVE_PRODUCTION placement result queued=%d preview=%d factory=%d\n",queued,placing,factory);
    check(queued && !placing && world.valid(factory) && world.ents[factory].nativeIdentity.definition->id=="GAPILE",
        "the actual placement click sends the source identity through Cmd; receipt closes the preview only after a native GAPILE is placed");
    if(!world.valid(factory))return failures;
    for(int i=0;i<300 && world.ents[factory].constructAnim>0;++i)world.update();
    check(world.productionStartRejection(0,true,engineer)==ProductionRejectReason::None,"the placed source infantry factory unlocks the native engineer");
    uiTab=2;sim.pos={-100,-100};render();Rectangle engineerIcon{};for(const auto& item:hudItems)if(item.type==engineer)engineerIcon=item.bounds;
    sim.pos={engineerIcon.x+engineerIcon.width/2,engineerIcon.y+engineerIcon.height/2};sim.pressedL=true;render();sim.pressedL=false;sim.pos={-100,-100};
    check(engineerIcon.width>0 && world.players[0].unitProd[0].active && world.players[0].unitProd[0].typeIdx==engineer,
        "clicking the original engineer cameo starts the source-specific paid order through the real sidebar command path");
    check(resume(30),"native unit identity and partially paid production survive World45 continuation");
    EID trainee=-1;
    for(int i=0;i<2000 && trainee<0;++i){world.update();for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id)
        && world.ents[id].player==0 && world.ents[id].nativeIdentity.type==engineer-NATIVE_PRODUCTION_BASE)trainee=id;}
    check(world.valid(trainee) && world.ents[trainee].hp==75 && world.ents[trainee].nativeIdentity.object.rfind("production:0:",0)==0
        && world.ents[trainee].utype==UnitType::Engineer,"paid training releases an actual 75-HP native ENGINEER with its own durable source identity through the original factory door");
    if(!world.valid(trainee))return failures;
    World produced=world;produced.map.bldOccRef=&produced.bldOcc;
    const auto save=(std::filesystem::path(outputDir)/"native-engineer-produced.sav").string();const auto producedHash=world.checksum();
    check(saveGameFile(save.c_str()) && loadGameFile(save.c_str()) && producedHash==world.checksum(),"application v10 / World45 restores the real produced engineer and production identity sequence");
    EID hut=-1;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->bridgeRepairHut){hut=id;break;}
    const int group=world.bridgeForHut(hut);World::Cmd repair{};repair.type=World::Cmd::Repair;repair.ids={trainee};repair.a=hut;repair.b=1;world.applyCmd(0,repair);
    const bool accepted=world.ents[trainee].engineerIntent==EngineerIntent::RepairBridge;
    check(accepted && resume(30),"a paid native engineer receives a real bridge-hut order under the original handover permissions and resumes while travelling");
    for(int i=0;i<1800 && world.valid(trainee);++i)world.update();
    check(accepted && !world.valid(trainee) && group>=0 && !world.bridgeNeedsRepair(group),"the actually trained native engineer reaches its hut, is consumed and restores the author-destroyed span");
    if(world.nativeAIRules.available)check(world.nativeAI.houses[1].enabled && world.nativeAI.houses[1].production,
        "actual bridge-hut entry executes the author's AI Triggers Begin and Production Begins actions");
    World repaired=world;repaired.map.bldOccRef=&repaired.bldOcc;

    world=early;world.map.bldOccRef=&world.bldOcc;const auto paid=world.players[0].bldProd.paid;const auto money=world.players[0].money;
    command(World::Cmd::CancelBldProd,barracks);
    check(!world.players[0].bldProd.active && world.players[0].money==money+paid && resume(1),"canceling native construction clears its liability and refunds precisely the source installments already paid");
    world=produced;world.map.bldOccRef=&world.bldOcc;command(World::Cmd::StartUnitProd,engineer);
    for(int i=0;i<2000 && world.players[0].unitProd[0].active;++i)world.update();
    int count=0;for(const auto& e:world.ents)if(e.alive && e.player==0 && e.nativeIdentity.type==engineer-NATIVE_PRODUCTION_BASE)++count;
    check(!world.players[0].unitProd[0].active && count==2 && world.valid(trainee) && resume(20),
        "the occupied preferred barracks cell uses another legal source exit and releases exactly one additional paid engineer with durable identity");
    for(World* snapshot:{&produced,&repaired}) {
        world=*snapshot;world.map.bldOccRef=&world.bldOcc;clearPlacement();gameOver=victory=false;sim.pos={-100,-100};uiTab=2;sel.clear();selBuilding=factory;
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){SetWindowSize(size.x,size.y);render();tileToScreen(site.x,site.y,px,py);camX=px-(SCREEN_W-sidebarW)/(2*camZoom);camY=py-SCREEN_H/(2*camZoom);
            shotAtDisplaySize=true;shotFile=(std::filesystem::path(outputDir)/(std::string(snapshot==&produced?"native-engineer-":"native-repaired-")+std::to_string(size.x)+".png")).string();
            const auto hash=world.checksum();render();check(hash==world.checksum(),"native production cameos, actual source barracks and trained/repaired states render without simulation mutation");}
    }
    sim=SimInput{};std::printf("NATIVE_PRODUCTION failures=%d\n",failures);return failures;
}
