#include "game/game.h"
#include <cstdio>
#include <filesystem>
#include <limits>

int Game::placementRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    int failures=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("PLACEMENT %s %s\n",ok?"PASS":"FAIL",name); };
    SetTargetFPS(0);auditHud=true;sim=SimInput{};sim.active=true;sim.pos={-100,-100};
    localPlayer=0;netGame=false;paused=false;showMenu=false;
    selectReferenceMap();cfgCountry=(int)Country::America;cfgMoney=10000;newGame(20261031);
    World::Cmd cmd;cmd.type=World::Cmd::Deploy;
    for(EID id=0;id<(EID)world.ents.size();++id) if(world.ents[id].alive && world.ents[id].player==0 &&
        !world.ents[id].isBuilding && world.ents[id].utype==UnitType::MCV) { cmd.ids={id};break; }
    world.applyCmd(0,cmd);cmd=World::Cmd{};cmd.type=World::Cmd::StartBldProd;cmd.a=(int)BldType::PowerPlant;
    world.applyCmd(0,cmd);
    for(int i=0;i<6000 && !world.players[0].bldProd.ready;++i) world.update();
    check(world.players[0].bldProd.ready,"normal production completes before preview fixture");
    uiTab=0;render();Rectangle icon{};
    for(const auto& item:hudItems) if(!item.unit && item.type==(int)BldType::PowerPlant) icon=item.bounds;
    auto before=world.checksum();
    sim.pos={icon.x+icon.width/2,icon.y+icon.height/2};sim.pressedL=true;render();sim.pressedL=false;
    check(icon.width>0 && placing && before==world.checksum(),"opening a ready building preview leaves simulation unchanged");
    sim.pos={400,300};sim.pressedR=true;handleInput();sim.pressedR=false;
    check(!placing && before==world.checksum(),"canceling the local preview leaves the completed paid order unchanged");
    FILE* checkpoint=tmpfile();bool saved=checkpoint && world.saveGame(checkpoint);
    check(saved,"completed order fixture can be saved");
    if(!saved) { if(checkpoint) fclose(checkpoint);return failures+1; }
    const int paid=world.players[0].bldProd.paid;const Credits money=world.players[0].money;
    auto restore=[&]() {
        netGame=false;pendingCmds.clear();clearPlacement();rewind(checkpoint);
        bool ok=world.loadGame(checkpoint);world.presentationEvents.clear();sim=SimInput{};sim.active=true;
        sim.pos={-100,-100};render();return ok;
    };
    auto preview=[&]() {
        sim.pos={icon.x+icon.width/2,icon.y+icon.height/2};sim.pressedL=true;render();sim.pressedL=false;
    };
    Vec2i site{-1,-1},other{-1,-1};
    const auto& def=bldDef(BldType::PowerPlant);
    auto mouseAt=[&](Vec2i cell) {
        int x,y;tileToScreen(cell.x+def.w-1,cell.y+def.h-1,x,y);
        return Vector2{(x-camX)*camZoom,(y-camY)*camZoom};
    };
    for(int y=5;y<22;++y) for(int x=5;x<22;++x) if(world.canPlace(BldType::PowerPlant,x,y,0)) {
        auto p=mouseAt({x,y});if(p.x<40 || p.y<40 || p.x>SCREEN_W-sidebarW-40 || p.y>SCREEN_H-BOTTOM_BAR_H-40) continue;
        if(site.x<0) site={x,y};
        else if(std::abs(site.x-x)>def.w || std::abs(site.y-y)>def.h) { other={x,y};break; }
    }
    check(site.x>=0 && other.x>=0,"two visible legal placement sites exist on the unmodified reference map");
    if(site.x<0 || other.x<0) { fclose(checkpoint);return failures+1; }
    auto clickSite=[&](Vec2i cell) { sim.pos=mouseAt(cell);sim.pressedL=true;handleInput();sim.pressedL=false; };
    preview();netGame=true;clickSite(site);
    check(placing && placementPending && pendingCmds.size()==1 && before==world.checksum(),
          "deferred placement keeps preview and paid order until execution");
    clickSite(site);check(pendingCmds.size()==1,"pending receipt prevents repeated clicks from submitting duplicates");
    World::Cmd deferred=pendingCmds.front();pendingCmds.clear();
    EID blocker=world.spawnUnit(0,UnitType::Grizzly,site.x+0.5f,site.y+0.5f);
    before=world.checksum();world.applyCmd(0,deferred);
    check(world.checksum()==before && world.presentationEvents.back().placement.reason==PlacementRejectReason::Blocked &&
          world.players[0].bldProd.ready && world.players[0].bldProd.paid==paid && world.players[0].money==money,
          "occupancy introduced after submission rejects without losing or charging the completed order");
    consumePresentationEvents();
    check(placing && !placementPending && msg==TR(S::MsgCannotPlace) && moveFeedbackRejected && world.checksum()==before,
          "rejected receipt preserves preview for retry and consumption leaves simulation untouched");
    const int languageBefore=g_lang;shotAtDisplaySize=true;
    auto capture=[&](const char* stem) {
        for(int language=0;language<2;++language) for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            g_lang=language;message(TR(moveFeedbackRejected?S::MsgCannotPlace:S::MsgBuildingPlaced));
            SetWindowSize(size.x,size.y);render();
            shotFile=(std::filesystem::path(outputDir)/(std::string(stem)+"_lang"+std::to_string(language)+"_"+
                std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();render();
        }
    };
    capture("blocked");world.kill(blocker,false);netGame=false;
    FILE* peerFile=tmpfile();World peer;bool peerOk=peerFile && world.saveGame(peerFile);
    if(peerOk) { rewind(peerFile);peerOk=peer.loadGame(peerFile); }if(peerFile) fclose(peerFile);
    clickSite(site);before=world.checksum();
    deferred.b=999;peer.applyCmd(0,deferred);
    check(placing && placementPending && !world.players[0].bldProd.active && world.countBlds(0,BldType::PowerPlant)==1 &&
          world.players[0].money==money && peerOk && world.checksum()==peer.checksum(),
          "retry places exactly once and UI request tokens do not alter deterministic simulation");
    const auto receipt=world.presentationEvents.back();consumePresentationEvents();
    check(!placing && placementType==BldType::COUNT && !placementPending && !moveFeedbackRejected &&
          msg==TR(S::MsgBuildingPlaced) && world.checksum()==before && receipt.tick==world.tick &&
          receipt.entity>=0 && world.valid(receipt.entity),
          "successful authoritative receipt closes preview and reports actual building ID and tick");
    capture("placed");
    for(int i=0;i<180 && peerOk;++i) { world.update();peer.update();peerOk=world.checksum()==peer.checksum(); }
    check(peerOk,"saved peer and UI-driven placement continue with identical per-tick checksums");
    before=world.checksum();world.applyCmd(0,deferred);consumePresentationEvents();
    check(before==world.checksum() && !placing && msg==TR(S::MsgPlacementNotReady),
          "duplicate completed command cannot place or charge a second building");
    check(restore(),"restore completed order for cancellation and delayed-receipt scenarios");
    preview();cmd=World::Cmd{};cmd.type=World::Cmd::CancelBldProd;cmd.a=(int)BldType::PowerPlant;
    world.applyCmd(0,cmd);handleInput();
    check(!placing && world.players[0].money==money+paid && msg==TR(S::MsgPlacementNotReady),
          "canceling production invalidates preview and refunds precisely the amount paid");
    restore();preview();netGame=true;clickSite(site);World::Cmd old=pendingCmds.back();
    sim.pressedR=true;handleInput();sim.pressedR=false;preview();clickSite(other);
    World::Cmd current=pendingCmds.back();const int request=placementPending;
    blocker=world.spawnUnit(0,UnitType::Grizzly,site.x+0.5f,site.y+0.5f);
    world.applyCmd(0,old);consumePresentationEvents();
    check(old.b!=current.b && placing && placementPending==request,
          "receipt for a canceled preview cannot consume a newer pending placement");
    world.applyCmd(0,current);consumePresentationEvents();
    check(!placing && !world.players[0].bldProd.active,"newer request still finishes after an obsolete rejected receipt");
    restore();preview();const auto appPath=(std::filesystem::path(outputDir)/"preview.sav").string();
    before=world.checksum();bool appSaved=saveGameFile(appPath.c_str());
    bool appLoaded=appSaved && loadGameFile(appPath.c_str());
    check(appLoaded && !placing && placementType==BldType::COUNT && !placementPending &&
          world.players[0].bldProd.ready && before==world.checksum(),
          "application save during preview restores the paid ready order without serializing UI intent");
    world.players[0].placingBld=BldType::PowerPlant; // Historical save slot, not the new UI field.
    appSaved=saveGameFile(appPath.c_str());appLoaded=appSaved && loadGameFile(appPath.c_str());
    before=world.checksum();preview();sim.pos={400,300};sim.pressedR=true;handleInput();sim.pressedR=false;
    check(appLoaded && !placing && world.players[0].placingBld==BldType::PowerPlant && before==world.checksum(),
          "historical saved preview slot round trips without becoming current UI intent");
    restore();preview();netGame=true;sim.pos={-30000,-30000};sim.pressedL=true;handleInput();sim.pressedL=false;
    check(pendingCmds.empty() && !placementPending && placing && msg==TR(S::MsgCannotPlace),
          "off-map preview clicks never enqueue coordinates rejected by the existing command protocol");
    netGame=false;
    for(float invalid:{-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),1e30f}) {
        cmd=World::Cmd{};cmd.type=World::Cmd::PlaceBuilding;cmd.a=(int)BldType::PowerPlant;cmd.x=invalid;cmd.y=10;
        before=world.checksum();world.applyCmd(0,cmd);
        check(before==world.checksum() && world.presentationEvents.back().placement.reason==PlacementRejectReason::InvalidLocation &&
              !world.presentationEvents.back().positional,"invalid coordinates reject before float-to-integer conversion");
    }
    cmd.a=(int)BldType::COUNT;before=world.checksum();world.applyCmd(0,cmd);
    check(before==world.checksum() && world.presentationEvents.back().placement.reason==PlacementRejectReason::InvalidType,
          "invalid building type never indexes rule definitions");
    bool sequence=true;uint64_t previous=0;bool first=true;
    for(const auto& event:world.presentationEvents) {
        sequence=sequence && (first || event.sequence>previous);first=false;previous=event.sequence;
    }
    check(sequence,"placement receipts preserve monotonically ordered presentation event sequence");
    g_lang=languageBefore;fclose(checkpoint);netGame=false;pendingCmds.clear();
    std::printf("PLACEMENT failures=%d\n",failures);sim=SimInput{};return failures;
}
