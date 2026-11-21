#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include "gfx/sprites.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <limits>

int GameRegression::nativeBridgeRegression(const char* outputDir) {
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str())){std::printf("NATIVE_BRIDGE SKIP: export the original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* message){std::printf("NATIVE_BRIDGE %s: %s\n",ok?"PASS":"FAIL",message);failures+=!ok;};
    g_script.shutdown();world=World{};localPlayer=0;campaignMission=-1;phase=Phase::InGame;gameOver=victory=false;ais.clear();
    const bool loaded=world.init(64,64,17,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && world.loadNativeScene(scene.c_str());
    check(loaded,"the actual original map and all source scene actors load");if(!loaded)return failures;
    const int deckCells=(int)std::count_if(world.map.bridgeCells.begin(),world.map.bridgeCells.end(),[](const auto& cell){return cell.owner>=0;});
    check(deckCells==27 && world.map.at(62,58).height==2 && world.map.at(62,58).terrain==Terrain::Water
        && world.map.surfaceHeight(62,58,1)==6 && world.map.surfacePassable(62,58,0,1)
        && world.map.surfacePassable(62,58,1,0),"nine authored segments create 27 deck cells while preserving height-2 water below height-6 road");
    std::vector<Vec2i> route;int layer=0,x=62,y=56;bool traversed=false;
    bool path=world.map.findPath(62,56,62,61,route,20000,0,nullptr,0,0);
    for(const auto point:route){layer=world.map.stepLayer(x,y,layer,point.x,point.y,0);traversed |= layer==1;x=point.x;y=point.y;if(layer<0)path=false;}
    check(path && traversed && layer==0 && Vec2i{x,y}==Vec2i{62,61},"ground A* enters the real bridge, follows the deck and returns to the far bank");
    check(world.map.findPath(59,58,65,58,route,20000,1,nullptr,0,0),"naval A* crosses beneath the bridge on the original water surface");
    // Isolate movement from mission combat; this is a mechanism fixture on the
    // real source geometry, not a playable replacement for original triggers.
    for(auto& row:world.nativeAllies)row=255;
    const EID tank=world.spawnUnit(0,UnitType::Grizzly,62.5f,56.5f);
    const EID ship=world.spawnUnit(0,UnitType::SeaScorpion,59.5f,58.5f);
    const EID infantry=world.spawnUnit(0,UnitType::GI,63.5f,56.5f);
    auto move=[&](EID id,float x,float y,int layer,bool append=false) {
        World::Cmd command{};command.type=World::Cmd::Move;command.ids={id};command.x=x;command.y=y;
        command.a=append?1:0;command.b=layer+1;world.applyCmd(0,command);
    };
    move(tank,62.5f,58.5f,1);move(ship,62.5f,58.5f,0);move(infantry,63.5f,61.5f,0);
    for(int tick=0;tick<240;++tick)world.update();
    const auto at=[&](EID id,float x,float y,int layer) {
        return world.valid(id) && std::abs(world.ents[id].x-x)<.05f && std::abs(world.ents[id].y-y)<.05f && world.ents[id].navLayer==layer;
    };
    check(at(tank,62.5f,58.5f,1) && at(ship,62.5f,58.5f,0)
        && world.unitAtCell(62,58,1)==tank && world.unitAtCell(62,58,0)==ship,
        "real move commands let a tank and a fitting small naval craft occupy the same XY cell on different surfaces");
    check(at(infantry,63.5f,61.5f,0),"infantry crosses a full-width side lane and steps onto the far bank");
    if(!at(tank,62.5f,58.5f,1) || !at(ship,62.5f,58.5f,0)) {
        for(auto id:{tank,ship,infantry})if(world.valid(id))std::printf("NATIVE_BRIDGE actor=%d xy=%.3f,%.3f layer=%u state=%d path=%d/%zu goal=%.1f,%.1f\n",
            id,world.ents[id].x,world.ents[id].y,world.ents[id].navLayer,(int)world.ents[id].state,world.ents[id].pathIdx,world.ents[id].path.size(),world.ents[id].goalX,world.ents[id].goalY);
    }
    FILE* file=tmpfile();World restored;bool resume=file && world.saveGame(file);
    if(resume){rewind(file);resume=restored.loadGame(file);}if(file)fclose(file);
    resume=resume && restored.checksum()==world.checksum();
    for(int tick=0;tick<120 && resume;++tick){world.update();restored.update();resume=world.checksum()==restored.checksum();}
    check(resume && restored.valid(tank) && restored.ents[tank].navLayer==1,"World v32 rebuilds bridge geometry and restores distinct occupied surfaces for 120 ticks");
    move(tank,62.5f,61.5f,0);move(tank,62.5f,58.5f,1,true);move(ship,65.5f,58.5f,0);
    for(int tick=0;tick<11;++tick)world.update();
    file=tmpfile();bool queued=file && world.saveGame(file);
    if(queued){rewind(file);queued=restored.loadGame(file);}if(file)fclose(file);
    bool returnedToBank=false;
    for(int tick=0;tick<240 && queued;++tick) {
        world.update();restored.update();queued=world.checksum()==restored.checksum();
        returnedToBank |= world.valid(tank) && world.ents[tank].navLayer==0 && (int)world.ents[tank].y==61;
    }
    check(queued && returnedToBank && at(tank,62.5f,58.5f,1) && at(ship,65.5f,58.5f,0),
        "a moving save preserves bridge exit, queued deck reentry and 240 resumed tick checksums");
    move(ship,62.5f,58.5f,0);for(int tick=0;tick<120;++tick)world.update();
    check(at(tank,62.5f,58.5f,1) && at(ship,62.5f,58.5f,0),"both physical surfaces are occupied again for render and collapse checks");
    {
        World interactions=world;interactions.map.bldOccRef=&interactions.bldOcc;
        interactions.kill(tank,false);
        const auto chrono=interactions.spawnUnit(0,UnitType::Chrono,62.5f,56.5f);
        const auto arrived=interactions.orderMove({chrono},62.5f,58.5f,false);
        check(arrived.accepted==1 && interactions.ents[chrono].navLayer==1
            && interactions.unitAtCell(62,58,1)==chrono && interactions.unitAtCell(62,58,0)==ship,
            "chrono arrival lands on the deck without treating the naval craft below as a blocker");
        interactions.kill(chrono,false);
        const auto dropped=interactions.spawnUnit(0,UnitType::Grizzly,62.5f,56.5f);
        auto& falling=interactions.ents[dropped];falling.x=falling.px=62.5f;falling.y=falling.py=58.5f;
        falling.navLayer=0;falling.magneticHeight=1;interactions.unitOccSync(dropped);
        const int hp=falling.hp;
        interactions.update();
        check(interactions.valid(dropped) && falling.navLayer==1
            && falling.hp==hp-12 && interactions.unitAtCell(62,58,0)==ship,
            "magnetic release resolves deck height and occupancy before applying fall damage");
        const auto boat=interactions.spawnUnit(0,UnitType::AmphTransport,60.5f,59.5f);
        auto& below=interactions.ents[boat];below.navLayer=below.previousNavLayer=below.goalLayer=0;interactions.unitOccSync(boat);
        const auto rider=interactions.spawnUnit(0,UnitType::GI,61.5f,59.5f);
        const auto refused=interactions.orderBoard({rider},boat);
        check(refused.accepted==0 && refused.reason==EntryRejectReason::Unreachable,
            "deck infantry cannot board a nearby boat four height levels below");
        below.cargo.push_back({UnitType::GI,75,0,0,0});
        const auto trapped=interactions.orderUnload({boat});
        check(trapped.released==0 && interactions.ents[boat].cargo.size()==1,
            "cargo stays aboard when no matching-height shore is connected within the release radius");
        const auto carrier=interactions.spawnUnit(0,UnitType::IFV,61.5f,58.5f);
        const auto boarded=interactions.orderBoard({rider},carrier);
        for(int tick=0;tick<3;++tick)interactions.update();
        bool aboard=boarded.accepted==1 && !interactions.valid(rider) && interactions.ents[carrier].cargo.size()==1;
        const auto unloaded=interactions.orderUnload({carrier});
        bool deckExit=false;
        for(EID id=0;id<(EID)interactions.ents.size();++id)if(interactions.valid(id)) {
            const auto& unit=interactions.ents[id];
            deckExit |= !unit.isBuilding && unit.utype==UnitType::GI && unit.navLayer==1
                && std::abs(unit.x-61.5f)<.01f && std::abs(unit.y-57.5f)<.01f;
        }
        check(aboard && unloaded.released==1 && deckExit,
            "a deck transport boards infantry at matching height and releases it to a connected deck cell");
    }
    {
        World columns=world;columns.map.bldOccRef=&columns.bldOcc;
        for(auto id:{tank,ship,infantry})columns.kill(id,false);
        std::vector<EID> forward,backward;
        for(int x=61;x<=63;++x) {
            forward.push_back(columns.spawnUnit(0,UnitType::Grizzly,x+.5f,56.5f));
            backward.push_back(columns.spawnUnit(0,UnitType::Grizzly,x+.5f,61.5f));
        }
        // Keep the entire requested formation beyond the bank. A target on
        // the lip legitimately assigns its near row to an empty deck cell.
        const auto north=columns.orderMove(forward,62.5f,63.5f,false);
        const auto south=columns.orderMove(backward,62.5f,54.5f,false);
        for(int tick=0;tick<900;++tick)columns.update();
        bool arrived=north.accepted==3 && south.accepted==3;
        for(auto id:forward)arrived &= columns.valid(id) && columns.ents[id].navLayer==0 && columns.ents[id].y>=61.f;
        for(auto id:backward)arrived &= columns.valid(id) && columns.ents[id].navLayer==0 && columns.ents[id].y<57.f;
        check(arrived,"opposing three-tank columns pass through the authored three-cell-wide bridge and reach both banks");
        if(!arrived)for(auto ids:{forward,backward})for(auto id:ids)if(columns.valid(id)) {
            const auto& unit=columns.ents[id];std::printf("NATIVE_BRIDGE column=%d xy=%.3f,%.3f layer=%u state=%d path=%d/%zu wait=%d\n",
                id,unit.x,unit.y,unit.navLayer,(int)unit.state,unit.pathIdx,unit.path.size(),unit.blockTick);
        }
    }
    {
        World channel=world;channel.map.bldOccRef=&channel.bldOcc;channel.kill(ship,false);
        // A controlled one-cell water lane and one passing bay exercise
        // yielding below the actual deck while a tank remains parked above it.
        for(int x=58;x<=66;++x)for(int y=57;y<=59;++y)if(channel.map.inBounds(x,y))
            channel.map.at(x,y).blocked=y!=58 && !(x==62 && y==59);
        const auto parked=channel.spawnUnit(0,UnitType::SeaScorpion,62.5f,58.5f);
        channel.ents[parked].navLayer=channel.ents[parked].previousNavLayer=channel.ents[parked].goalLayer=0;channel.unitOccSync(parked);
        const auto passing=channel.spawnUnit(0,UnitType::SeaScorpion,59.5f,58.5f);
        channel.ents[tank].guard=true;
        const auto sent=channel.orderMove({passing},65.5f,58.5f,false,false,false,0);
        bool layersHeld=true;
        for(int tick=0;tick<600;++tick) {
            channel.update();layersHeld &= channel.valid(passing) && channel.valid(parked)
                && channel.ents[passing].navLayer==0 && channel.ents[parked].navLayer==0;
        }
        const bool yielded=sent.accepted==1 && channel.valid(passing) && channel.ents[passing].x>65.f && channel.ents[passing].navLayer==0
            && layersHeld && channel.valid(parked) && channel.ents[parked].navLayer==0
            && distf(channel.ents[parked].x,channel.ents[parked].y,62.5f,58.5f)>.9f
            && channel.map.surfacePassable((int)channel.ents[parked].x,(int)channel.ents[parked].y,2,0)
            && channel.valid(tank) && channel.ents[tank].navLayer==1 && channel.ents[tank].x==62.5f && channel.ents[tank].y==58.5f;
        check(yielded,
            "under-deck small naval traffic yields into a water bay without reserving or displacing the tank above");
        if(!yielded)for(auto id:{passing,parked,tank})if(channel.valid(id)) {
            const auto& unit=channel.ents[id];std::printf("NATIVE_BRIDGE channel=%d xy=%.3f,%.3f layer=%u state=%d path=%d/%zu wait=%d goal=%.1f,%.1f accepted=%d\n",
                id,unit.x,unit.y,unit.navLayer,(int)unit.state,unit.pathIdx,unit.path.size(),unit.blockTick,unit.goalX,unit.goalY,sent.accepted);
        }
    }
    const auto before=world.checksum();if(world.valid(tank))world.ents[tank].navLayer=0;
    check(world.checksum()!=before,"physical surface participates in deterministic state hashing");if(world.valid(tank))world.ents[tank].navLayer=1;
    bool rejected=true;
    for(int layer:{std::numeric_limits<int>::min(),-1,3,std::numeric_limits<int>::max()}) {
        World::Cmd invalid{};invalid.type=World::Cmd::Move;invalid.ids={tank};invalid.x=62.5f;invalid.y=58.5f;invalid.b=layer;
        world.applyCmd(0,invalid);rejected &= world.checksum()==before;
    }
    check(rejected,"invalid wire surface selectors including INT_MIN preserve the existing order without integer overflow");
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);syncFogKnowledge();bakeTerrain();
    const float deckX=(62.5f-58.5f)*(TILE_W/2.f),deckY=(62.5f+58.5f)*(TILE_H/2.f)-heightScreenY(6);
    int hitX,hitY,hitLayer;surfaceAt(deckX,deckY,hitX,hitY,&hitLayer);
    check(hitX==62 && hitY==58 && hitLayer==1,"native screen picking selects the raised deck cell");
    surfaceAt(deckX,deckY+heightScreenY(4),hitX,hitY,&hitLayer);
    check(hitX==62 && hitY==58 && hitLayer==0,"native screen picking also resolves the water below the same map coordinate");
    const auto upper=unitScreenPos(world.ents[tank]);
    const auto lowerHeight=world.map.surfaceHeight(62,58,0);
    check(world.actorSurfaceHeight(world.ents[tank])==6 && lowerHeight==2
        && std::abs(upper.y-((world.ents[tank].x+world.ents[tank].y)*(TILE_H/2.f)-heightScreenY(6)-camY))<.1f,
        "actual unit rendering applies the deck elevation instead of the underwater cell height");
    {
        SetWindowSize(1280,720);render();camZoom=1;interpAlpha=1;
        camX=deckX-480;camY=deckY-320;
        dragging=showMenu=placing=targetingParadrop=false;sideMode=0;targetingSW=SWType::COUNT;sel={infantry};selBuilding=INVALID_EID;
        const float x=(61.5f-59.5f)*(TILE_W/2.f),y=(61.5f+59.5f)*(TILE_H/2.f)-heightScreenY(6);
        int mx,my;worldToScreen(x,y,mx,my);const auto state=world.checksum();
        updateHoverCursor(mx,my);const bool cursor=cursorKind==CursorKind::Move;
        netGame=true;pendingCmds.clear();issueSmartOrder(mx,my);
        const bool intent=pendingCmds.size()==1 && pendingCmds[0].type==World::Cmd::Move && pendingCmds[0].b==2
            && pendingCmds[0].x==61 && pendingCmds[0].y==59 && pendingCmds[0].ids==std::vector<EID>{infantry};
        pendingCmds.clear();netGame=false;
        int tx,ty;worldToScreen(deckX,deckY,tx,ty);
        check(cursor && intent && pickUnit(tx,ty)!=ship && state==world.checksum(),
            "the real hover and smart-order path emits a deck move and cannot pick the naval craft through the road");
        const auto save=(std::filesystem::path(outputDir)/"bridge-application.sav").string();
        check(saveGameFile(save.c_str()) && loadGameFile(save.c_str()) && world.checksum()==state
            && world.ents[tank].navLayer==1 && world.ents[ship].navLayer==0,
            "the application save envelope restores deck and underwater occupancy together");
    }
    shotAtDisplaySize=true;SetTargetFPS(0);camZoom=1;sel={tank,ship,infantry};selBuilding=INVALID_EID;msg.clear();msgTimer=0;
    for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
        SetWindowSize(size.x,size.y);render();int sx,sy;tileToScreen(62,58,sx,sy);
        camX=sx-(size.x-sidebarW)/2.f;camY=sy-heightScreenY(6)-size.y/2.f;fogMaskTick=-1;
        shotFile=(std::filesystem::path(outputDir)/("two-surfaces-"+std::to_string(size.x)+".png")).string();
        const auto state=world.checksum();render();check(state==world.checksum(),"the actual bridge frame leaves simulation and RNG unchanged");
        if(size.x==1280) {
            // Shift within the outer water lane so the same hull has both
            // a portion under the bridge and a portion beyond its artwork.
            const float savedX=world.ents[ship].x,savedPX=world.ents[ship].px;
            world.ents[ship].x=world.ents[ship].px=63.5f;shotAtDisplaySize=false;
            sel.clear();const auto present=(std::filesystem::path(outputDir)/"occlusion-present.png").string();
            const auto absent=(std::filesystem::path(outputDir)/"occlusion-absent.png").string();
            shotFile=present;render();world.ents[ship].alive=false;shotFile=absent;render();world.ents[ship].alive=true;
            Image with=LoadImage(present.c_str()),without=LoadImage(absent.c_str());
            const auto& mobile=world.ents[ship];const auto& body=g_sprites.unitBody(mobile.utype,mobile.dir,unitBodyFrame(mobile),world.players[mobile.player].colorId);
            Image hull=body.tex.id?LoadImageFromTexture(body.tex):Image{};const auto point=unitScreenPos(mobile);
            const int left=(int)point.x-body.ox,top=(int)point.y-body.oy;
            std::vector<uint8_t> covered(hull.data?(size_t)hull.width*hull.height:0,0);
            for(const auto& bridge:world.ents)if(bridge.alive && bridge.nativeIdentity.definition && bridge.nativeIdentity.definition->kind==3) {
                const auto& sprite=nativeActorSprite(bridge);Image mask=sprite.tex.id?LoadImageFromTexture(sprite.tex):Image{};
                auto origin=bldScreenPos(bridge);const int frame=nativeActorFrame(bridge);if(frame>=9 && frame<=17)origin.y-=TILE_H/2;
                const int bx=(int)origin.x-sprite.ox,by=(int)origin.y-sprite.oy;
                if(hull.data && mask.data)for(int y=0;y<hull.height;++y)for(int x=0;x<hull.width;++x) {
                    const int mx=left+x-bx,my=top+y-by;
                    if(mx>=0 && my>=0 && mx<mask.width && my<mask.height && GetImageColor(mask,mx,my).a==255)
                        covered[(size_t)y*hull.width+x]=1;
                }
                if(mask.data)UnloadImage(mask);
            }
            int hiddenPixels=0,visiblePixels=0;bool intact=with.data && without.data && hull.data;
            if(intact)for(int y=0;y<hull.height;++y)for(int x=0;x<hull.width;++x) {
                const int sx=left+x,sy=top+y;
                if(sx<0 || sy<0 || sx>=SCREEN_W-sidebarW || sy>=SCREEN_H-BOTTOM_BAR_H || GetImageColor(hull,x,y).a!=255)continue;
                const bool same=ColorToInt(GetImageColor(with,sx,sy))==ColorToInt(GetImageColor(without,sx,sy));
                if(covered[(size_t)y*hull.width+x]){++hiddenPixels;intact &= same;}
                else visiblePixels+=!same;
            }
            world.ents[ship].x=savedX;world.ents[ship].px=savedPX;
            check(intact && hiddenPixels>0 && visiblePixels>0 && world.checksum()==state,
                "actual GPU pixels hide the lower hull behind opaque bridge artwork while its exposed hull remains visible");
            std::printf("NATIVE_BRIDGE occluded_pixels=%d exposed_pixels=%d intact=%d hull=%d,%d,%d,%d screen=%d,%d zoom=%.3f\n",
                hiddenPixels,visiblePixels,(int)intact,left,top,hull.width,hull.height,SCREEN_W,SCREEN_H,camZoom);
            if(with.data)UnloadImage(with);if(without.data)UnloadImage(without);if(hull.data)UnloadImage(hull);
            sel={tank,ship,infantry};shotAtDisplaySize=true;
        }
    }
    const auto owner=world.map.bridgeCells[world.cellIdx(62,58)].owner;
    if(world.valid(owner))world.kill(owner,false);
    check(!world.map.hasDeck(62,58) && !world.valid(tank) && world.valid(ship),
        "removing a real segment closes its deck and retires unsupported armor without deleting the naval craft below");
    std::printf("NATIVE_BRIDGE failures=%d deck_cells=%d\n",failures,deckCells);return failures;
}
