#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

int GameRegression::netProductionRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    g_script.shutdown();SetTargetFPS(0);int failures=0,port=55850;
    const auto check=[&](bool ok,const char* label){
        std::printf("NET_PRODUCTION %s rules=%d tick=%llu %s\n",ok?"PASS":"FAIL",(int)cfgRules,
            (unsigned long long)world.tick,label);std::fflush(stdout);failures+=!ok;return ok;
    };
    for(auto rules:{RulesChoice::Fusion,RulesChoice::RA2,RulesChoice::YR}) {
        net.close();netGame=false;netResetHandshake();resetMatchInteraction();selectRulesChoice(rules);
        if(rules!=RulesChoice::Fusion) {
            world.init(48,48,941,2,0,{Faction::Allies,Faction::Soviet});bakeTerrain();
        }
        cfgCountry=(int)Country::America;cfgColor=0;cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgCrates=false;
        phase=Phase::NetLobby;lobbyRole=0;lobbyState=1;
        Game peer;peer.selectRulesChoice(rules);peer.cfgCountry=(int)Country::Russia;peer.cfgColor=1;
        peer.phase=Phase::NetLobby;peer.lobbyRole=1;peer.lobbyState=1;
        bool listening=false;for(int attempt=0;attempt<40 && !listening;++attempt)listening=net.host(++port);
        bool connected=listening && peer.net.connectTo("127.0.0.1",port);
        for(int wait=0;wait<2000 && connected && lobbyState!=2;++wait){
            netHandleMsgs();peer.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        connected=connected && lobbyState==2 && netStartHandshake(922);
        for(int wait=0;wait<2000 && connected && (!netGame || !peer.netGame);++wait){
            netHandleMsgs();peer.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(!check(connected && netGame && peer.netGame && world.checksum()==peer.world.checksum(),
            "real lobby handshake starts matching ordinary worlds"))continue;
        sim={};sim.active=true;sim.pos={-100,-100};handleInput();
        bool ground=terrainTex.id && peer.terrainTex.id && terrainW==4096 && terrainH==2048
            && peer.terrainW==terrainW && peer.terrainH==terrainH;
        Image ours=terrainTex.id?LoadImageFromTexture(terrainTex):Image{};
        Image theirs=peer.terrainTex.id?LoadImageFromTexture(peer.terrainTex):Image{};
        ground &= ours.data && theirs.data && ours.width==theirs.width && ours.height==theirs.height;
        int opaque=0;
        if(ground)for(int y=0;y<ours.height;y+=31)for(int x=0;x<ours.width;x+=29) {
            const Color a=GetImageColor(ours,x,y),b=GetImageColor(theirs,x,y);
            ground &= a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;opaque+=a.a>0;
        }
        if(ours.data)UnloadImage(ours);if(theirs.data)UnloadImage(theirs);
        check(ground && opaque>500,"first and subsequent LAN openings have matching ground for the new map on both peers");
        const auto opening=world.checksum();
        shotFile=(std::filesystem::path(outputDir)/("opening-rules-"+std::to_string((int)rules)+".png")).string();render();
        Image first=LoadImageFromTexture(canvas.texture);ImageFlipVertical(&first);
        shotFile=(std::filesystem::path(outputDir)/("opening-rules-"+std::to_string((int)rules)+"-second.png")).string();render();
        Image second=LoadImageFromTexture(canvas.texture);ImageFlipVertical(&second);
        const bool captured=first.data && second.data && first.width==SCREEN_W && second.width==SCREEN_W
            && first.height==SCREEN_H && second.height==SCREEN_H;
        int changed=0,minY=SCREEN_H,maxY=-1,lowerBezel=0;
        if(captured)for(int y=0;y<SCREEN_H;++y)for(int x=SCREEN_W-sidebarW;x<SCREEN_W;++x){
            const auto a=GetImageColor(first,x,y),b=GetImageColor(second,x,y);
            if(y>=SCREEN_H/2 && y<SCREEN_H-40 && x>=SCREEN_W-10 && x<SCREEN_W-3
                && std::max({a.r,a.g,a.b})>=64)++lowerBezel;
            if(a.r!=b.r || a.g!=b.g || a.b!=b.b || a.a!=b.a){++changed;minY=std::min(minY,y);maxY=std::max(maxY,y);}}
        std::printf("NET_PRODUCTION_FIRST_FRAME rules=%d changed=%d y=%d..%d lowerBezel=%d\n",(int)rules,changed,minY,maxY,lowerBezel);
        UnloadImage(first);UnloadImage(second);
        check(captured && changed==0 && lowerBezel>500,"the complete actual first LAN sidebar matches the next frame without a simulation tick");
        check(world.checksum()==opening && world.checksum()==peer.world.checksum(),
            "drawing the actual LAN opening leaves both simulation states unchanged");
        const auto advance=[&](int ticks){
            const uint64_t goal=netSimTick+ticks;bool same=true;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while((netSimTick<goal || peer.netSimTick<goal) && std::chrono::steady_clock::now()<deadline){
                netHandleMsgs();peer.netHandleMsgs();
                if(netPeerLeft || peer.netPeerLeft || netDesync || peer.netDesync)return false;
                if(netSimTick<goal)netAdvance();if(peer.netSimTick<goal)peer.netAdvance();
                consumePresentationEvents();peer.consumePresentationEvents();
                if(netSimTick==peer.netSimTick)same &= world.checksum()==peer.world.checksum();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return same && netSimTick==goal && peer.netSimTick==goal && world.checksum()==peer.world.checksum();
        };
        EID mcv=INVALID_EID;for(EID id=0;id<(EID)world.ents.size();++id){const auto& e=world.ents[id];
            if(e.alive && !e.isBuilding && e.player==0 && e.utype==UnitType::MCV){mcv=id;break;}}
        World::Cmd deploy;deploy.type=World::Cmd::Deploy;deploy.ids={mcv};issueCmd(deploy);
        if(!check(advance(8) && world.countBlds(0,BldType::ConYard)==1,
            "ordinary MCV deployment reaches both peers"))continue;
        sim={};sim.active=true;sim.pos={-100,-100};auditHud=true;uiTab=0;
        const auto click=[&](bool unit,int selector,bool right=false){
            uiTab=unit?2:0;sim.pos={-100,-100};render();
            Rectangle box{};for(const auto& item:hudItems)if(item.type==selector && item.unit==unit){box=item.bounds;break;}
            if(box.width<=0)return false;
            sim.pos={box.x+box.width*.5f,box.y+box.height*.5f};sim.pressedL=!right;sim.pressedR=right;
            render();sim.pressedL=sim.pressedR=false;return true;
        };
        const int power=world.productionSelector(0,false,(int)BldType::PowerPlant);
        const Credits original=world.players[0].money;
        if(!check(click(false,power) && advance(8) && world.players[0].bldProd.active,
            "the real power-plant cameo starts production over TCP"))continue;
        if(!check(click(false,power,true) && advance(8) && world.players[0].bldProd.held,
            "right-click pauses source or fusion production over TCP"))continue;
        const auto held=world.players[0].bldProd;
        check(advance(12) && world.players[0].bldProd.progress==held.progress
            && world.players[0].bldProd.workRemainder==held.workRemainder && world.players[0].bldProd.paid==held.paid,
            "paused production keeps paid work unchanged on both peers");
        if(!check(click(false,power) && advance(30) && !world.players[0].bldProd.held,
            "left-click resumes the same paid order"))continue;
        if(!check(click(false,power,true) && advance(8) && click(false,power,true) && advance(8)
            && !world.players[0].bldProd.active && world.players[0].money==original,
            "pause then cancel refunds exactly the paid amount on both peers"))continue;
        const auto build=[&](BldType type){
            const int selector=world.productionSelector(0,false,(int)type);
            if(!click(false,selector) || !advance(8))return false;
            for(int budget=0;budget<6000 && !world.players[0].bldProd.ready;budget+=30)if(!advance(30))return false;
            if(!world.players[0].bldProd.ready || !click(false,selector) || !placing)return false;
            const auto& d=world.nativeProductionType(selector)?world.nativeTypes[selector-NATIVE_PRODUCTION_BASE]->building:bldDef(type);
            Vector2 point{-1,-1};Vec2i intended{-1,-1};
            for(int y=3;y<25 && point.x<0;++y)for(int x=3;x<25;++x){
                const bool allowed=world.nativeProductionType(selector)?world.canPlaceNativeBuilding(0,selector,x,y):world.canPlace(type,x,y,0);
                if(!allowed)continue;
                const int cornerX=x+d.w-1,cornerY=y+d.h-1;
                // The player places against observed terrain. Do not use an
                // omniscient height to aim a click behind unexplored high ground.
                if(world.map.fogAt(0,cornerX,cornerY)!=FOG_VISIBLE)continue;
                int sx,sy;tileToScreen(x+d.w-1,y+d.h-1,sx,sy);
                Vector2 candidate{(sx-camX)*camZoom,(sy+TILE_H/2-heightScreenY(world.map.at(x,y).height)-camY)*camZoom};
                if(candidate.x<=4 || candidate.x>=SCREEN_W-sidebarW-4 || candidate.y<=4 || candidate.y>=SCREEN_H-BOTTOM_BAR_H-4)continue;
                float wx,wy;screenToWorld((int)candidate.x,(int)candidate.y,wx,wy);int tx,ty;surfaceAt(wx,wy,tx,ty);
                if(tx==cornerX && ty==cornerY){point=candidate;intended={x,y};break;}
            }
            if(point.x<0)return false;
            sim.pos=point;sim.pressedL=true;handleInput();sim.pressedL=false;
            std::printf("NET_PRODUCTION_PLACE rules=%d selector=%d intended=%d,%d click=%.2f,%.2f pending=%zu\n",
                (int)cfgRules,selector,intended.x,intended.y,point.x,point.y,pendingCmds.size());
            if(!pendingCmds.empty()){const auto& c=pendingCmds.back();std::printf("NET_PRODUCTION_PLACE_CMD type=%d selector=%d cell=%.1f,%.1f\n",(int)c.type,c.a,c.x,c.y);}
            if(pendingCmds.empty() || pendingCmds.back().type!=World::Cmd::PlaceBuilding || pendingCmds.back().x!=intended.x || pendingCmds.back().y!=intended.y)return false;
            const bool advanced=advance(60);
            std::printf("NET_PRODUCTION_PLACED advanced=%d count=%d/%d preview=%d message=%s\n",advanced,world.countBlds(0,type),peer.world.countBlds(0,type),placing,msg.c_str());
            return advanced && world.countBlds(0,type)==1 && peer.world.countBlds(0,type)==1 && !placing;
        };
        if(!check(build(BldType::PowerPlant),"paid completion and real map placement create the same power plant"))continue;
        if(!check(build(BldType::Barracks),"a paid barracks is completed and placed through the same UI and TCP path"))continue;
        const int infantry=world.productionSelector(0,true,(int)UnitType::GI);
        const Credits money=world.players[0].money;const int cost=world.unitProductionCost(0,UnitType::GI);
        bool trained=click(true,infantry) && advance(8);
        const auto produced=world.producedUnits[0];
        for(int budget=0;budget<6000 && trained && world.producedUnits[0]==produced;budget+=30)trained=advance(30);
        check(trained && world.producedUnits[0]==produced+1 && peer.world.producedUnits[0]==produced+1
            && world.players[0].money==money-cost,"the infantry cameo pays and physically produces the same unit on both peers");
        net.close();peer.net.close();netGame=false;
    }
    net.close();netGame=false;sim={};auditHud=false;return failures;
}
