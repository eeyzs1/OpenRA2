#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>

int GameRegression::nativeBridgeRepairRegression(const char* outputDir) {
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str())){std::printf("BRIDGE_REPAIR SKIP: export the original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* message){std::printf("BRIDGE_REPAIR %s: %s\n",ok?"PASS":"FAIL",message);failures+=!ok;};
    g_script.shutdown();world=World{};localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;ais.clear();
    const bool loaded=world.init(64,64,17,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && world.loadNativeScene(scene.c_str());
    check(loaded,"the complete original first-mission scene loads");if(!loaded)return failures;
    for(auto& row:world.nativeAllies)row=255; // Mechanism fixture; no replacement mission events.
    std::vector<EID> huts;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.definition
        && world.ents[id].nativeIdentity.definition->bridgeRepairHut)huts.push_back(id);
    const int group=huts.empty()?-1:world.bridgeForHut(huts.front());
    check(huts.size()==2 && world.nativeBridgeGroups.size()==2 && group>=0 && world.nativeBridgeGroups[group].size()==5
        && world.bridgeForHut(huts.back())==group,"both authored repair huts resolve to their five-segment span, independently of the other four-segment bridge");
    if(huts.size()!=2 || group<0)return failures;
    const EID hut=huts.front();const auto hutTag=world.ents[hut].nativeIdentity.tag;const int hutOwner=world.ents[hut].player;
    const int hutHP=world.ents[hut].hp;world.damage(hut,100000,1);
    check(world.valid(hut) && world.ents[hut].hp==hutHP && world.ents[hut].nativeIdentity.definition->immune,
        "the source Immune=yes rule protects the bridge repair hut from ordinary damage");
    const auto protectedSpan=world.liveBridgeSegment(world.nativeBridgeBlueprints[world.nativeBridgeGroups[group].front()]);
    const int spanHP=world.ents[protectedSpan].hp;world.damage(protectedSpan,100000,1);
    check(world.valid(protectedSpan) && world.ents[protectedSpan].hp==spanHP,
        "the original mission's DestroyableBridges=no override prevents ordinary combat from breaking the span");
    {
        World changed=world;changed.map.bldOccRef=&changed.bldOcc;changed.nativeBridgesDestroyable=true;
        changed.damage(protectedSpan,100,1);FILE* file=tmpfile();World restored;bool resume=file && changed.saveGame(file);
        if(resume){rewind(file);resume=restored.loadGame(file);}if(file)fclose(file);
        check(changed.ents[protectedSpan].hp==spanHP-100 && resume && restored.nativeBridgesDestroyable && restored.checksum()==changed.checksum()
            && !world.nativeBridgesDestroyable,"a runtime change to the authored bridge-destruction flag is independent, hashed and persisted");
    }
    {
        World fresh=world;fresh.map.bldOccRef=&fresh.bldOcc;
        const bool initialized=fresh.init(64,64,19,1,0,{Faction::Allies},0,"",true);
        check(initialized && fresh.nativeScenarioSource.empty() && fresh.nativeTypes.empty() && fresh.nativeBridgeBlueprints.empty()
            && fresh.nativeBridgeGroups.empty() && fresh.nativeBridgesDestroyable && !fresh.map.hasBridgeDecks(),
            "starting a normal match clears the previous native bridge topology and destruction flag");
    }
    const auto intactEngineer=world.spawnUnit(0,UnitType::Engineer,80.5f,46.5f);const auto intactState=world.checksum();
    const auto intact=world.orderBridgeRepair({intactEngineer},hut);
    check(world.valid(intactEngineer) && intact.accepted==0 && intact.reason==EngineerRejectReason::BridgeIntact && intactState==world.checksum(),
        "an intact span refuses the repair without consuming or changing the engineer");
    World closed=world;closed.map.bldOccRef=&closed.bldOcc;
    const auto blockedSegment=world.liveBridgeSegment(world.nativeBridgeBlueprints[world.nativeBridgeGroups[group].front()]);
    world.kill(blockedSegment,false);
    const auto inaccessible=world.orderBridgeRepair({intactEngineer},hut);
    check(inaccessible.accepted==1 && world.valid(intactEngineer) && world.bridgeNeedsRepair(group)
        && !world.ents[intactEngineer].path.empty(),
        "the intact authored friendly gate admits a planned engineer route without repairing remotely");
    world=closed;world.map.bldOccRef=&world.bldOcc;world.kill(intactEngineer,false);
    std::vector<EID> gates;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.definition
        && world.ents[id].nativeIdentity.definition->id=="GAGATE_A")gates.push_back(id);
    check(!gates.empty() && std::all_of(gates.begin(),gates.end(),[&](auto id){return world.valid(id)
        && world.ents[id].nativeGatePhase==NativeGatePhase::Closed;}),
        "all source gates remain alive and closed before each repair fixture, with no demolition shortcut");
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto reset=[&](){world=initial;world.map.bldOccRef=&world.bldOcc;};
    auto destroy=[&](bool all) {
        std::vector<EID> dead;
        for(int index:world.nativeBridgeGroups[group]) {
            const auto id=world.liveBridgeSegment(world.nativeBridgeBlueprints[index]);
            if(world.valid(id)){dead.push_back(id);world.kill(id,false);}
            if(!all)break;
        }
        return dead;
    };
    auto engineer=[&](int distance) {
        const auto target=world.ents[hut];
        World::Ent profile;profile.player=0;
        for(int radius=distance;radius<=distance+8;++radius)for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x) {
            if(std::max(abs(x),abs(y))!=radius)continue;
            const int tx=(int)target.x+x,ty=(int)target.y+y;
            if(!world.map.inBounds(tx,ty) || !world.map.surfacePassable(tx,ty,0,0) || world.unitAtCell(tx,ty)!=INVALID_EID
                || world.map.surfaceHeight(tx,ty,0)!=world.actorSurfaceHeight(target))continue;
            std::vector<Vec2i> path;
            if(world.map.findPathToRange(tx,ty,target.x+.5f,target.y+.5f,1.099f,path,20000,0,nullptr,0,-1,world.actorSurfaceHeight(target),true,world.nativeGateAccess(profile)))
                return world.spawnUnit(0,UnitType::Engineer,tx+.5f,ty+.5f);
        }
        std::printf("BRIDGE_REPAIR no engineer ingress at hut %.0f,%.0f\n",target.x,target.y);
        return INVALID_EID;
    };
    {
        const auto old=destroy(true);std::set<EID> replaced;
        for(int y=10;y<100 && replaced.size()<old.size();++y)for(int x=10;x<100 && replaced.size()<old.size();++x) {
            if(!world.map.surfacePassable(x,y,0,0) || world.unitAtCell(x,y)!=INVALID_EID)continue;
            replaced.insert(world.spawnUnit(0,UnitType::GI,x+.5f,y+.5f));
        }
        bool reused=replaced.size()==old.size();for(EID id:replaced)reused &= std::find(old.begin(),old.end(),id)!=old.end();
        const int otherGroup=group==0?1:0;
        const auto unrelated=world.liveBridgeSegment(world.nativeBridgeBlueprints[world.nativeBridgeGroups[otherGroup].front()]);world.kill(unrelated,false);
        const auto actor=engineer(5);const auto accepted=world.orderBridgeRepair({actor},hut);
        for(int tick=0;tick<11;++tick)world.update();
        FILE* file=tmpfile();World replay;bool resumed=file && world.saveGame(file);
        if(resumed){rewind(file);resumed=replay.loadGame(file);}if(file)fclose(file);
        bool walked=false;
        for(int tick=0;tick<240 && resumed;++tick) {
            if(world.valid(actor))walked |= !world.ents[actor].path.empty();
            world.update();replay.update();resumed=world.checksum()==replay.checksum();
        }
        bool restored=true;
        for(int index:world.nativeBridgeGroups[group]) {
            const auto& original=world.nativeBridgeBlueprints[index];const auto id=world.liveBridgeSegment(original);
            restored &= world.valid(id) && world.ents[id].hp==bldDef(world.ents[id]).hp && world.map.hasDeck(original.x,original.y)
                && world.map.native.tiles[world.cellIdx(original.x,original.y)].overlayData==original.frame;
        }
        bool retained=true;for(EID id:replaced)retained &= world.valid(id) && !world.ents[id].isBuilding && world.ents[id].utype==UnitType::GI;
        check(reused && accepted.accepted==1 && walked && resumed && !world.valid(actor) && restored && retained,
            "walking repair survives 240 resumed ticks, restores every authored segment and never revives reused entity slots");
        check(world.ents[hut].player==hutOwner && world.ents[hut].nativeIdentity.tag==hutTag && world.ents[hut].hp==hutHP,
            "repair preserves the hut owner, source tag and health rather than capturing the building");
        check(world.bridgeNeedsRepair(otherGroup),"repairing this hut leaves damage to the unrelated bridge unchanged");
        std::vector<Vec2i> path;
        check(world.map.findPath(82,50,82,56,path,20000,0,nullptr,0,0)
            && std::any_of(path.begin(),path.end(),[&](auto cell){return world.map.hasDeck(cell.x,cell.y);}),
            "the repaired bridge restores the actual cross-bank A* route");
    }
    reset();
    {
        const auto first=world.liveBridgeSegment(world.nativeBridgeBlueprints[world.nativeBridgeGroups[group].front()]);
        world.kill(first,false);const auto a=engineer(1),b=engineer(1);
        const auto accepted=world.orderBridgeRepair({a,b},hut);
        for(int tick=0;tick<180;++tick)world.update();
        check(accepted.accepted==2 && (world.valid(a)+world.valid(b))==1 && !world.bridgeNeedsRepair(group),
            "two accepted engineers racing to the same repair consume exactly one and retain the other");
    }
    reset();
    {
        destroy(false);const auto actor=engineer(5);const auto accepted=world.orderBridgeRepair({actor},hut);
        // Direct removal represents a map/script action, bypassing Immune.
        world.kill(hut,false);for(int tick=0;tick<120;++tick)world.update();
        check(accepted.accepted==1 && world.valid(actor) && world.ents[actor].engineerIntent==EngineerIntent::None && world.bridgeNeedsRepair(group),
            "removing the hut while the engineer is travelling cancels repair without consuming the unit");
    }
    reset();
    {
        destroy(false);const auto& source=world.nativeBridgeBlueprints[world.nativeBridgeGroups[group].front()];
        auto& tile=world.map.native.tiles[world.cellIdx(source.x,source.y)];tile.overlay=0;tile.overlayData=5;
        const auto actor=engineer(3);const auto state=world.checksum();const auto rejected=world.orderBridgeRepair({actor},hut);
        check(rejected.accepted==0 && rejected.reason==EngineerRejectReason::BridgeBlocked && world.valid(actor) && state==world.checksum()
            && tile.overlay==0 && tile.overlayData==5,"repair refuses a replaced overlay and preserves both the obstacle and the engineer");
    }
    for(auto replacement:{World::Cmd::Stop,World::Cmd::Move,World::Cmd::Scatter}) {
        reset();destroy(false);const auto actor=engineer(4);const auto begun=world.orderBridgeRepair({actor},hut);
        World::Cmd cancel{};cancel.type=replacement;cancel.ids={actor};cancel.x=82.5f;cancel.y=46.5f;world.applyCmd(0,cancel);
        FILE* file=tmpfile();World restored;bool saved=file && world.saveGame(file);
        if(saved){rewind(file);saved=restored.loadGame(file);}if(file)fclose(file);
        check(begun.accepted==1 && world.valid(actor) && world.ents[actor].engineerIntent==EngineerIntent::None
            && saved && restored.checksum()==world.checksum(),"stop, move and scatter clear repair intent before an immediate save, without waiting for another tick");
    }
    reset();
    {
        destroy(true);const auto actor=engineer(3);const auto large=world.spawnUnit(0,UnitType::Typhoon,82.5f,53.5f);
        const auto state=world.checksum();const auto blocked=world.orderBridgeRepair({actor},hut);
        check(blocked.accepted==0 && blocked.reason==EngineerRejectReason::BridgeBlocked && world.valid(actor) && world.checksum()==state,
            "a craft lacking bridge clearance blocks restoration without consuming the engineer or trapping the ship");
        world.kill(large,false);const auto small=world.spawnUnit(0,UnitType::SeaScorpion,82.5f,53.5f);
        const auto allowed=world.orderBridgeRepair({actor},hut);for(int tick=0;tick<180;++tick)world.update();
        check(allowed.accepted==1 && !world.valid(actor) && !world.bridgeNeedsRepair(group) && world.valid(small) && world.ents[small].navLayer==0,
            "a fitting craft can remain on the water while an engineer restores the deck above");
    }
    reset();
    {
        const auto large=world.spawnUnit(0,UnitType::Typhoon,59.5f,58.5f);
        const auto small=world.spawnUnit(0,UnitType::SeaScorpion,59.5f,59.5f);
        check(!unitDef(world.ents[large]).fitsUnderBridge() && unitDef(world.ents[small]).fitsUnderBridge()
            && !world.movementPassable(world.ents[large],62,58,0) && world.movementPassable(world.ents[small],62,58,0),
            "source clearance rules block the large submarine while admitting the small naval craft below the same deck");
        const auto result=world.orderMove({large},62.5f,58.5f,false,false,false,0);
        bool legal=result.accepted==1 && result.adjusted==1;
        for(auto cell:world.ents[large].path)legal &= !world.map.hasDeck(cell.x,cell.y);
        for(int tick=0;tick<180;++tick){world.update();legal &= !world.map.hasDeck((int)world.ents[large].x,(int)world.ents[large].y);}
        check(legal,"large-ship commands choose a legal nearby water position and never enter the forbidden lower surface");
    }
    reset();destroy(true);const auto actor=engineer(4);if(!world.valid(actor))return failures+1;
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
    fogKnowledge={};syncFogKnowledge();bakeTerrain();sel={actor};selBuilding=INVALID_EID;
    camZoom=1;interpAlpha=1;showMenu=placing=dragging=targetingParadrop=false;sideMode=0;targetingSW=SWType::COUNT;
    const auto& target=world.ents[hut];const float hx=(target.x-target.y)*(TILE_W/2.f),hy=(target.x+target.y+1)*(TILE_H/2.f)-heightScreenY((uint8_t)world.actorSurfaceHeight(target));
    camX=hx-450;camY=hy-300;int mx,my;worldToScreen(hx,hy,mx,my);
    const auto state=world.checksum();updateHoverCursor(mx,my);const bool hover=cursorKind==CursorKind::Repair;
    netGame=true;pendingCmds.clear();issueSmartOrder(mx,my);
    const bool intent=pendingCmds.size()==1 && pendingCmds.front().type==World::Cmd::Repair && pendingCmds.front().a==hut && pendingCmds.front().b==1;
    const auto command=intent?pendingCmds.front():World::Cmd{};pendingCmds.clear();netGame=false;
    check(hover && intent && world.checksum()==state,"real hover and right-click emit the distinct bridge repair intent without capturing the neutral hut");
    if(intent)world.applyCmd(0,command);
    const auto save=(std::filesystem::path(outputDir)/"engineer-en-route.sav").string();const auto moving=world.checksum();
    check(saveGameFile(save.c_str()) && loadGameFile(save.c_str()) && world.checksum()==moving
        && world.ents[actor].engineerIntent==EngineerIntent::RepairBridge,"application saves restore the actual travelling repair order and source bridge topology");
    auto frame=[&](const char* name) {
        shotAtDisplaySize=true;SetTargetFPS(0);sel.clear();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();int x,y;tileToScreen(82,53,x,y);
            camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-heightScreenY(6)-SCREEN_H/2.f;
            shotFile=(std::filesystem::path(outputDir)/(std::string(name)+"-"+std::to_string(size.x)+".png")).string();
            const auto before=world.checksum();render();check(before==world.checksum(),"actual damaged/repaired bridge drawing preserves simulation state");
        }
    };
    frame("before-repair");for(int tick=0;tick<240;++tick)world.update();
    check(!world.valid(actor) && !world.bridgeNeedsRepair(group),"the real right-click order reaches the hut and restores the entire span");
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);syncFogKnowledge();frame("after-repair");
    std::printf("BRIDGE_REPAIR failures=%d blueprints=%zu groups=%zu\n",failures,world.nativeBridgeBlueprints.size(),world.nativeBridgeGroups.size());return failures;
}
