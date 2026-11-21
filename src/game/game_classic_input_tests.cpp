#include "game_regression.h"
#include "game/script.h"
#include "scoped_user_file.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

int GameRegression::classicInputRegression(const char* outputDir) {
    ScopedUserFile settings("settings.ini");if(!settings.ready())return 1;
    std::filesystem::create_directories(outputDir);g_script.shutdown();SetTargetFPS(0);int failures=0;
    const auto check=[&](bool ok,const char* label){
        std::printf("CLASSIC_INPUT %s rules=%d %s\n",ok?"PASS":"FAIL",(int)cfgRules,label);
        std::fflush(stdout);failures+=!ok;return ok;
    };
    const auto click=[&](Vector2 point,bool right=false,bool shift=false,bool attack=false){
        sim={};sim.active=true;sim.pos=point;if(shift)sim.keysDown.insert(KEY_LEFT_SHIFT);
        if(attack)sim.keysDown.insert(KEY_A);
        sim.pressedL=sim.downL=!right;sim.pressedR=sim.downR=right;handleInput();
        sim.pressedL=sim.pressedR=sim.downL=sim.downR=false;
        sim.releasedL=!right;sim.releasedR=right;handleInput();sim.releasedL=sim.releasedR=false;
        sim.keysDown.clear();
    };
    const auto point=[&](EID id){const auto box=unitScreenRect(world.ents[id]);
        return Vector2{(box.x+box.width*.5f)*camZoom,(box.y+box.height*.5f)*camZoom};};
    const auto ground=[&](int x,int y){int sx,sy;tileToScreen(x,y,sx,sy);
        return Vector2{(sx-camX)*camZoom,(sy+TILE_H/2-camY)*camZoom};};
    for(auto rules:{RulesChoice::Fusion,RulesChoice::RA2,RulesChoice::YR}) {
        selectRulesChoice(rules);cfgClassicOrders=true;resetMatchInteraction();
        world=World{};const char* profile=rules==RulesChoice::Fusion?nullptr:rules==RulesChoice::RA2?
            "assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
        const bool loaded=world.init(64,64,922,2,0,{Faction::Allies,Faction::Soviet},0,
            REFERENCE_MAP,true,true,profile,{Country::America,Country::Russia});
        if(!check(loaded,"explicit flat mechanism fixture loads the selected rules"))continue;
        for(auto& cell:world.map.cells)cell=Cell{};
        world.cratesEnabled=false;ais.clear();localPlayer=0;campaignMission=-1;
        phase=Phase::InGame;paused=showMenu=gameOver=netGame=false;camZoom=1;
        camX=-(SCREEN_W-sidebarW)*.5f;camY=0;camEdgeLock=10000;
        const EID tank=world.spawnUnit(0,UnitType::Grizzly,12.5f,12.5f);
        const EID other=world.spawnUnit(0,UnitType::Grizzly,16.5f,12.5f);
        const EID mcv=world.spawnUnit(0,UnitType::MCV,9.5f,13.5f);
        const EID soldier=world.spawnUnit(0,UnitType::GI,12.5f,16.5f);
        const EID carrier=world.spawnUnit(0,UnitType::IFV,16.5f,16.5f);
        const EID enemy=world.spawnUnit(1,UnitType::Rhino,30.5f,15.5f);
        for(int p=0;p<2;++p)world.map.reveal(p,32,32,100);
        if(!check(world.valid(tank) && world.valid(other) && world.valid(mcv) && world.valid(soldier)
            && world.valid(carrier) && world.valid(enemy),"real typed units supply selection and contextual-order targets"))continue;
        bakeTerrain();clampCameraToMap();sim={};sim.active=true;sim.pos={-100,-100};
        auto before=world.checksum();click(point(tank));
        check(sel==std::vector<EID>{tank} && world.checksum()==before,"left-click selects an owned unit without issuing an order");
        const float startX=world.ents[tank].x,startY=world.ents[tank].y;
        const auto orderPoint=ground(20,8);
        check(pickUnit((int)orderPoint.x,(int)orderPoint.y)==INVALID_EID
            && pickBuilding((int)orderPoint.x,(int)orderPoint.y)==INVALID_EID,
            "movement fixture clicks visible ground rather than an overhanging transport sprite");
        click(orderPoint);
        const bool ordered=sel==std::vector<EID>{tank} && world.ents[tank].state==UState::Moving;
        for(int tick=0;tick<60;++tick)world.update();
        check(ordered && distf(startX,startY,world.ents[tank].x,world.ents[tank].y)>.5f,
            "classic ground click retains selection and physically moves the unit");
        click(point(tank));before=world.checksum();click(ground(20,17),true);
        check(sel.empty() && selBuilding==INVALID_EID && world.checksum()==before,
            "classic right-click cancels selection without replacing the current order");
        click(point(tank));before=world.checksum();
        sim={};sim.active=true;sim.pos={450,200};sim.pressedL=sim.downL=true;handleInput();
        sim.pressedL=false;sim.pos={860,620};handleInput();sim.downL=false;sim.releasedL=true;handleInput();sim.releasedL=false;
        check(sel.size()>=2 && std::find(sel.begin(),sel.end(),other)!=sel.end() && world.checksum()==before,
            "dragging on ground still box-selects after a unit was already selected");
        click(point(tank));before=world.checksum();
        sim={};sim.active=true;sim.pos={(float)(SCREEN_W-sidebarW-12),350};sim.pressedL=sim.downL=true;handleInput();
        sim.pressedL=sim.downL=false;sim.pos={(float)(SCREEN_W-sidebarW+12),350};sim.releasedL=true;handleInput();sim.releasedL=false;
        check(sel==std::vector<EID>{tank} && world.checksum()==before,
            "releasing a potential order over the HUD preserves selection and issues nothing");
        click(point(tank));before=world.checksum();click(point(other));
        check(sel==std::vector<EID>{other} && world.checksum()==before,"an ordinary friendly-unit click switches selection");
        click(point(tank));click(point(other),false,true);
        check(sel.size()==2 && std::find(sel.begin(),sel.end(),tank)!=sel.end(),"Shift left-click adds to the selection");
        // The physical movement sample recomputed fog for 60 ticks. This
        // separate command mechanism requires an actually observed target.
        world.map.reveal(0,(int)world.ents[enemy].x,(int)world.ents[enemy].y,2);
        check(pickUnit((int)point(enemy).x,(int)point(enemy).y)==enemy,"attack fixture supplies a visible, hittable enemy");
        click(point(tank));click(point(enemy));
        check(world.ents[tank].target==enemy && (world.ents[tank].state==UState::Chasing || world.ents[tank].state==UState::Attacking),
            "classic enemy click goes through the real attack admission");
        click(point(soldier));click(point(carrier));
        check(world.ents[soldier].state==UState::Boarding && sel==std::vector<EID>{soldier},
            "a compatible friendly transport receives a boarding order rather than stealing selection");
        click(point(tank));pendingCmds.clear();netGame=true;before=world.checksum();click(ground(20,17));
        check(pendingCmds.size()==1 && pendingCmds.front().type==World::Cmd::Move
            && pendingCmds.front().ids==std::vector<EID>{tank} && pendingCmds.front().generations.size()==1
            && pendingCmds.front().generations.front()==world.ents[tank].generation && world.checksum()==before,
            "classic input queues a generation-bound LAN command without locally executing it");
        netGame=false;pendingCmds.clear();click(point(mcv));click(ground(14,20));
        check(world.valid(mcv) && !world.ents[mcv].isBuilding && world.ents[mcv].utype==UnitType::MCV
            && world.ents[mcv].state==UState::Moving,"a classic MCV ground click moves instead of deploying in place");
        click(point(mcv));
        check(world.countBlds(0,BldType::ConYard)==1,"clicking the selected MCV itself still deploys it");
        const EID factory=world.spawnBuilding(0,BldType::Barracks,22,8,true);
        click(ground(23,9));click(ground(20,22));
        check(world.valid(factory) && selBuilding==factory && world.ents[factory].rallyX==20 && world.ents[factory].rallyY==22,
            "a selected production building receives a classic left-click rally point");
        cfgClassicOrders=false;click(point(tank));before=world.checksum();click(ground(20,18));
        check(sel.empty() && before==world.checksum(),"the modern scheme keeps left ground clicks as selection operations");
        click(point(tank));click(ground(20,18),true);
        check(sel==std::vector<EID>{tank} && world.ents[tank].state==UState::Moving,"the optional modern right-click scheme still sends movement");
    }
    selectRulesChoice(RulesChoice::RA2);
    for(bool modern:{false,true}) {
        resetMatchInteraction();cfgClassicOrders=!modern;world=World{};
        const bool loaded=world.init(64,64,922,2,0,{Faction::Allies,Faction::Soviet},0,
            REFERENCE_MAP,true,true,"assets/rules/profiles/ra2.ini",{Country::America,Country::Russia});
        if(!check(loaded,"formation click fixture loads RA2 rules"))continue;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        world.cratesEnabled=false;ais.clear();localPlayer=0;campaignMission=-1;phase=Phase::InGame;
        paused=showMenu=gameOver=netGame=false;camZoom=1;interpAlpha=1;
        camX=-(SCREEN_W-sidebarW)*.5f;camY=0;camEdgeLock=10000;
        const EID west=world.spawnUnit(0,UnitType::Grizzly,8.5f,11.5f);
        const EID east=world.spawnUnit(0,UnitType::Grizzly,11.5f,8.5f);
        if(!check(world.valid(west) && world.valid(east),"formation click fixture supplies two real tanks"))continue;
        world.map.reveal(0,16,16,24);bakeTerrain();clampCameraToMap();
        const auto left=point(west),right=point(east);
        sim={};sim.active=true;sim.pos={left.x-24.f,std::min(left.y,right.y)-24.f};
        sim.pressedL=sim.downL=true;handleInput();sim.pressedL=false;
        sim.pos={right.x+24.f,std::max(left.y,right.y)+24.f};handleInput();
        sim.downL=false;sim.releasedL=true;handleInput();sim.releasedL=false;
        if(!check(sel.size()==2 && std::find(sel.begin(),sel.end(),west)!=sel.end()
            && std::find(sel.begin(),sel.end(),east)!=sel.end(),"drag selects both tanks through normal mouse input"))continue;
        const auto destination=ground(15,15);
        click(destination,modern,false,modern);
        const auto parked=[&] {
            const auto& l=world.ents[west];const auto& r=world.ents[east];
            return l.state==UState::Idle && r.state==UState::Idle
                && distf(l.x,l.y,14.5f,15.5f)<.01f && distf(r.x,r.y,15.5f,14.5f)<.01f;
        };
        const auto expectedState=modern?UState::AttackMoving:UState::Moving;
        const bool commanded=world.ents[west].state==expectedState && world.ents[east].state==expectedState;
        for(int tick=0;tick<180 && world.ents[west].pathIdx==0;++tick)world.update();
        if(!check(commanded && world.ents[west].pathIdx>0 && world.ents[west].state==expectedState
            && pickUnit((int)destination.x,(int)destination.y)==INVALID_EID,
            "formation is moving and the repeated click still hits open ground"))continue;
        auto sequence=world.presentationSequence;
        const auto movingHash=world.checksum();
        click(destination,modern,false,modern);
        std::printf("FORMATION_REPEAT_MOVING mouse=%s command=%d checksum_equal=%d\n",modern?"modern":"classic",
            world.presentationSequence>sequence,world.checksum()==movingHash);
        check(world.presentationSequence>sequence && world.checksum()==movingHash,
            "repeating the issued movement retains the active formation routes");
        click(destination,modern,false,!modern);
        const auto changedMode=modern?UState::Moving:UState::AttackMoving;
        check(world.ents[west].state==changedMode && world.ents[east].state==changedMode,
            "changing the attack modifier orders a new movement mode at the same destination");
        click(destination,modern,false,modern);
        for(int tick=0;tick<1800 && !parked();++tick)world.update();
        const auto& westBefore=world.ents[west];const auto& eastBefore=world.ents[east];
        std::printf("FORMATION_ARRIVAL mouse=%s sent=%d screen=%.0f,%.0f hit=%d west=%.3f,%.3f state=%d east=%.3f,%.3f state=%d\n",
            modern?"modern":"classic",commanded,destination.x,destination.y,pickUnit((int)destination.x,(int)destination.y),
            westBefore.x,westBefore.y,(int)westBefore.state,eastBefore.x,eastBefore.y,(int)eastBefore.state);
        if(!check(parked(),"both tanks reach their diagonal slots"))continue;
        // The south part of the same diamond lies beyond the bodies of both
        // parked tanks, but within the ordinary selection radius.
        const Vector2 gap{destination.x,destination.y+12.f};
        float wx,wy;int tx,ty;
        screenToWorld((int)gap.x,(int)gap.y,wx,wy);surfaceAt(wx,wy,tx,ty);
        const auto outsideBody=[&](EID id){
            auto rect=unitScreenRect(world.ents[id]);
            rect.x-=8;rect.y-=8;rect.width+=16;rect.height+=16;
            return !CheckCollisionPointRec(gap,rect);
        };
        if(!check(tx==15 && ty==15 && world.unitAtCell(tx,ty)==INVALID_EID && outsideBody(west) && outsideBody(east)
            && world.valid(pickUnit((int)gap.x,(int)gap.y)),"the nearby destination is empty ground outside both tank bodies"))continue;
        updateHoverCursor((int)gap.x,(int)gap.y);
        const bool showsMove=cursorKind==CursorKind::Move;
        const auto beforeGap=world.checksum();
        sequence=world.presentationSequence;
        click(gap,modern,false,modern);
        const bool issued=world.presentationSequence>sequence;
        const bool same=beforeGap==world.checksum();
        std::printf("FORMATION_CLEAR_CLICK mode=%s cursor=%d command=%d selected=%zu parked=%d same=%d\n",
            modern?"modern":"classic",showsMove,issued,sel.size(),parked(),same);
        if(!check(showsMove && issued && sel.size()==2 && parked() && same,
            "the movement cursor sends an order at nearby empty ground and retains both parked tanks"))continue;
        if(!modern) {
            pendingCmds.clear();netGame=true;
            click(gap);
            const bool queued=sel.size()==2 && pendingCmds.size()==1 && pendingCmds.front().type==World::Cmd::Move
                && pendingCmds.front().ids==sel && pendingCmds.front().identitiesBound
                && pendingCmds.front().generations==std::vector<uint64_t>{world.ents[sel[0]].generation,world.ents[sel[1]].generation}
                && pendingCmds.front().x==15 && pendingCmds.front().y==15
                && beforeGap==world.checksum();
            check(queued,"a nearby classic ground click queues both generation-bound actors for LAN");
            netGame=false;pendingCmds.clear();
        }
        click(ground(20,14),modern,false,modern);
        check(sel.size()==2 && world.ents[west].state==expectedState && world.ents[east].state==expectedState,
            "a different destination orders both tanks to move again");
        const auto beforeSelection=world.checksum();
        const auto target=point(east);
        updateHoverCursor((int)target.x,(int)target.y);
        const bool showsSelection=modern || cursorKind==CursorKind::Arrow;
        click(target);
        check(showsSelection && sel==std::vector<EID>{east} && world.checksum()==beforeSelection,
            "an explicit click on a tank still selects that unit from the formation");
    }
    cfgClassicOrders=true;saveSettings();cfgClassicOrders=false;loadSettings();
    check(cfgClassicOrders,"settings restore a saved classic scheme");
    cfgClassicOrders=false;saveSettings();cfgClassicOrders=true;loadSettings();
    check(!cfgClassicOrders,"settings restore a saved modern scheme");
    cfgClassicOrders=true;phase=Phase::Settings;settingsFromGame=false;
    sim={};sim.active=true;sim.pos=menuCanvasFromUi(254,245);sim.pressedL=true;render();sim.pressedL=false;
    check(!cfgClassicOrders,"the real settings mouse-order button changes the active scheme");
    shotFile=(std::filesystem::path(outputDir)/"mouse-settings.png").string();render();
    check(settings.restore(),"the input audit restores the player's settings byte-for-byte");
    cfgClassicOrders=true;sim={};return failures;
}
