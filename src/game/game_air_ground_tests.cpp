#include "game_regression.h"
#include "game/script.h"
#include "game/lang.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

int GameRegression::airGroundRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);g_script.shutdown();int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("AIR_GROUND %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto fresh=[&](World& w,const char* profile,int mapSize=64){
        if(!w.init(mapSize,mapSize,1911,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,profile))return false;
        w.ents.clear();w.freeList.clear();std::fill(w.bldOcc.begin(),w.bldOcc.end(),0);
        for(auto& cell:w.map.cells){cell.height=0;cell.terrain=Terrain::Clear;cell.ore=cell.oreMax=0;cell.blocked=false;cell.overlay=Overlay::None;}
        for(auto& p:w.players)p.isAI=false;w.rebuildUnitOcc();w.rebuildAirOcc();return true;
    };
    const auto roundtrip=[&](World& sample,int ticks){
        FILE* file=std::tmpfile();World replay;bool ok=file && sample.saveGame(file);
        if(ok){std::rewind(file);ok=replay.loadGame(file);}if(file)std::fclose(file);
        ok &= sample.checksum()==replay.checksum();
        for(int i=0;i<ticks && ok;++i){sample.update();replay.update();ok=sample.checksum()==replay.checksum();}return ok;
    };
    const auto finishBoarding=[&](World& sample,const std::vector<EID>& people,EID carrier) {
        int at=0;bool pending=true;
        for(;at<600;++at){pending=std::any_of(people.begin(),people.end(),[&](EID id){return sample.valid(id);});if(!pending)break;sample.update();}
        std::printf("AIR_GROUND_PREP ticks=%d cargo=%zu expected=%zu\n",at,sample.ents[carrier].cargo.size(),people.size());
        return !std::any_of(people.begin(),people.end(),[&](EID id){return sample.valid(id);}) && sample.ents[carrier].cargo.size()==people.size();
    };
    const auto bridge=[&](World& w){
        const EID owner=w.spawnBuilding(0,BldType::Wall,4,4,true);w.map.bridgeCells.assign(w.map.cells.size(),{});
        for(auto& cell:w.map.cells)cell.height=2;
        for(int y=20;y<=22;++y)for(int x=19;x<=23;++x){w.map.at(x,y).terrain=Terrain::Water;
            if(x>=20 && x<=22)w.map.bridgeCells[(size_t)y*w.map.w+x]={owner,6,1};}
        w.rebuildUnitOcc();w.rebuildAirOcc();
    };
    const auto capture=[&](const World& sample,EID actor){
        world=sample;world.map.bldOccRef=&world.bldOcc;resetMatchInteraction();resetNotifications();ais.clear();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        localPlayer=0;cfgRules=RulesChoice::RA2;phase=Phase::InGame;campaignMission=-1;gameOver=victory=paused=showMenu=netGame=false;
        sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;SetTargetFPS(0);bakeTerrain();
        sel={actor};int x,y;tileToScreen((int)world.ents[actor].x,(int)world.ents[actor].y,x,y);
        camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;clampCameraToMap();shotAtDisplaySize=true;
        World::Cmd command{};command.type=World::Cmd::Unload;command.ids={actor};world.bindCommand(command);world.applyCmd(0,command);consumePresentationEvents();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){
            SetWindowSize(size.x,size.y);const auto before=world.checksum();const auto path=(std::filesystem::path(outputDir)/("landing-"+std::to_string(size.x)+".png")).string();shotFile=path;render();
            Image image=LoadImage(path.c_str());bool ok=image.data && image.width==size.x && image.height==size.y && before==world.checksum();UnloadImage(image);
            const auto box=unitScreenRect(world.ents[actor]);ok &= pickUnit((int)(box.x+box.width*.5f),(int)(box.y+box.height*.5f))==actor;
            check(ok && msg==TextFormat(TR(S::MsgReleaseLanding),1) && !moveFeedbackRejected,"a selected airborne transport and actual pending-landing feedback render and remain selectable at the chosen display resolution");
        }
    };
    for(const char* edition:{"ra2","yr"}){
        const auto profile=std::string("assets/rules/profiles/")+edition+".ini";
        {
            World w;bool ok=fresh(w,profile.c_str());check(ok,"source world initializes for an actual passenger trip");if(!ok)continue;
            const EID air=w.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f),person=w.spawnUnit(0,UnitType::GI,19.5f,20.5f);
            w.ents[person].hp=67;w.ents[person].kills=3;const auto identity=w.ents[person].nativeIdentity;
            ok &= w.orderBoard({person},air).accepted==1;for(int i=0;i<240 && w.valid(person);++i)w.update();
            ok &= !w.valid(person) && w.ents[air].cargo.size()==1 && w.ents[air].cargo[0].nativeIdentity.object==identity.object;
            check(ok,"ground boarding retains the real source passenger identity and health");
            w.orderMove({air},20.5f,8.5f,false);for(int i=0;i<120;++i)w.update();
            ok &= w.ents[air].airborne();Game feedback;feedback.world=w;feedback.localPlayer=0;feedback.phase=Phase::InGame;
            World::Cmd unload{};unload.type=World::Cmd::Unload;unload.ids={air};feedback.world.applyCmd(0,unload);
            const bool reported=!feedback.world.presentationEvents.empty() && feedback.world.presentationEvents.back().kind==PresentationKind::ReleaseOrder;
            const auto released=reported?feedback.world.presentationEvents.back().release:ReleaseOrderResult{};
            feedback.consumePresentationEvents();w=feedback.world;w.map.bldOccRef=&w.bldOcc;
            const bool retained=ok && released.released==0 && w.ents[air].cargo.size()==1;
            check(retained,"an unload command during flight retains its passenger until the transport lands");
            check(retained && reported && released.pending==1 && !feedback.moveFeedbackRejected && feedback.msg==TextFormat(TR(S::MsgReleaseLanding),1),
                "the actual Unload command reports landing rather than immediate success or a blocked-exit error");
            if(retained && std::string(edition)=="ra2")capture(w,air);
            const auto raw=(std::filesystem::path(outputDir)/(std::string(edition)+"-pending-world56.sav")).string();
            FILE* file=std::fopen(raw.c_str(),"wb");bool saved=file && w.saveGame(file);if(file)std::fclose(file);
            Game app;app.world=w;app.cfgRules=std::string(edition)=="ra2"?RulesChoice::RA2:RulesChoice::YR;app.cfgMapSize=64;
            app.cfgAI=1;app.cfgCountry=(int)Country::America;app.localPlayer=0;app.phase=Phase::InGame;
            const auto path=(std::filesystem::path(outputDir)/(std::string(edition)+"-pending-app11.sav")).string();
            saved &= app.saveGameFile(path.c_str());Game resumed;saved &= resumed.loadGameFile(path.c_str()) && resumed.world.checksum()==w.checksum();
            for(int i=0;i<160 && saved;++i){app.world.update();resumed.world.update();saved &= app.world.checksum()==resumed.world.checksum();}
            check(retained && saved && app.world.ents[air].cargo.empty(),"App11 resumes the saved pending unload through landing and passenger restoration");
            {
                std::ifstream input(path,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
                const auto start=embeddedWorldSaveOffset(bytes);
                const size_t offset=start.value_or(bytes.size());
                const auto block=start?locateWorldSaveSection(std::span<const uint8_t>(bytes).subspan(offset),WorldSaveSection::AirNavigation):std::nullopt;
                bool rejected=block && block->length>=21;const auto before=resumed.world.checksum();
                if(rejected){bytes[offset+block->offset+12]=2;const auto bad=(std::filesystem::path(outputDir)/(std::string(edition)+"-invalid-pending-app.sav")).string();
                    {std::ofstream output(bad,std::ios::binary);output.write((const char*)bytes.data(),bytes.size());}
                    rejected=!resumed.loadGameFile(bad.c_str()) && resumed.world.checksum()==before && resumed.cfgRules==app.cfgRules;}
                check(rejected,"an invalid nested pending-unload record rejects App11 loading without replacing the live match");
            }
            check(retained && roundtrip(w,7),"World55 retains a pending unload during an unfinished descent");
            {
                World frozen=w;frozen.ents[air].tpSick=17;const float altitude=frozen.ents[air].nativeAltitude;bool paused=true;
                for(int i=0;i<17;++i){frozen.update();paused &= frozen.ents[air].nativeAltitude==altitude && frozen.ents[air].nativeAirUnload && frozen.ents[air].cargo.size()==1;}
                check(paused && roundtrip(frozen,160) && frozen.ents[air].cargo.empty(),"immobilization freezes a pending landing without losing it, and a saved resume still restores the passenger");
            }
            {
                World invalid=w;invalid.ents[air].nativeAirLandingRetry=invalid.tick+31;FILE* file=std::tmpfile();
                const bool rejected=file && !invalid.saveGame(file);if(file)std::fclose(file);
                check(rejected,"a landing retry deadline outside the bounded 30-tick window is rejected");
            }
            {
                std::ifstream input(raw,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
                const auto block=locateWorldSaveSection(bytes,WorldSaveSection::AirNavigation);bool rejected=block && block->length>=21;
                if(rejected){bytes[block->offset+12]=2;file=std::tmpfile();rejected=file && std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size() && !std::fseek(file,0,SEEK_SET);
                    World invalid;if(rejected)rejected=!invalid.loadGame(file);if(file)std::fclose(file);}
                check(rejected,"a nonboolean pending-unload record is rejected when reading an actual World55 file");
            }
            bool physical=true,completed=false;
            for(int i=0;i<900 && retained;++i){w.update();
                if(w.ents[air].cargo.empty()){physical &= !w.ents[air].airborne();completed=true;break;}}
            EID restored=INVALID_EID;for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].nativeIdentity.object==identity.object)restored=id;
            check(retained && physical && completed && w.valid(restored) && w.ents[restored].hp==67 && w.ents[restored].kills==3,
                "the same airborne unload command lands first and restores the original passenger without a second command");
        }
        {
            World w;bool ok=fresh(w,profile.c_str());const EID air=w.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f);
            const EID person=w.spawnUnit(0,UnitType::GI,19.5f,20.5f);w.orderBoard({person},air);ok &= finishBoarding(w,{person},air);
            w.orderMove({air},20.5f,8.5f,false);for(int i=0;i<120;++i)w.update();w.orderUnload({air});
            ok &= w.ents[air].nativeAirUnload;w.orderMove({air},30.5f,20.5f,false);ok &= !w.ents[air].nativeAirUnload;
            for(int i=0;i<900 && ok;++i)w.update();
            check(ok && w.ents[air].cargo.size()==1 && !w.ents[air].airborne(),"a later Move command cancels deferred unloading without losing or unexpectedly releasing the passenger");
        }
        {
            World w;bool ok=fresh(w,profile.c_str());const EID air=w.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f);
            const EID obstruction=w.spawnUnit(1,UnitType::MCV,20.5f,16.5f);
            w.orderMove({air},20.5f,16.5f,false);for(int i=0;i<1200 && ok;++i)w.update();
            const auto& landed=w.ents[air];
            check(ok && w.valid(obstruction) && !landed.airborne() && landed.state==UState::Landed
                && ((int)landed.x!=20 || (int)landed.y!=16) && w.nativeAirLandingLayer((int)landed.x,(int)landed.y,air)>=0,
                "a blocked destination chooses a nearby legal landing cell without overlapping the ground vehicle");
        }
        {
            World w;bool ok=fresh(w,profile.c_str());const EID air=w.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f);
            w.orderMove({air},20.5f,16.5f,false);int i=0;
            for(;i<700;++i){w.update();const auto& e=w.ents[air];if(e.nativeJumpjet.phase==NativeJumpjetPhase::Descent && e.nativeJumpjet.motion.half && e.nativeAltitude<=5)break;}
            ok &= i<700;const float height=w.ents[air].nativeAltitude;const int x=(int)w.ents[air].x,y=(int)w.ents[air].y;
            w.spawnUnit(0,UnitType::MCV,x+.5f,y+.5f);w.update();ok &= w.ents[air].nativeAltitude>=height && w.ents[air].nativeJumpjet.phase!=NativeJumpjetPhase::Descent;
            const bool interrupted=ok;float maxStep=0;
            for(i=0;i<1000 && ok && w.ents[air].airborne();++i){const float x=w.ents[air].x,y=w.ents[air].y;w.update();maxStep=std::max(maxStep,distf(x,y,w.ents[air].x,w.ents[air].y));ok &= maxStep<.3f;}
            if(!ok || w.ents[air].airborne()){const auto& e=w.ents[air];std::printf("AIR_GROUND LATE interrupted=%d before=%.9g after=%.9g xy=%.9g,%.9g phase=%d state=%d goal=%.9g,%.9g step=%.9g ticks=%d\n",interrupted,height,e.nativeAltitude,e.x,e.y,(int)e.nativeJumpjet.phase,(int)e.state,e.goalX,e.goalY,maxStep,i);}
            check(ok && !w.ents[air].airborne() && ((int)w.ents[air].x!=x || (int)w.ents[air].y!=y),
                "a late blocker cancels an unfinished descent half-step before the aircraft rises and flies to a legal neighbor");
        }
        {
            World w;bool ok=fresh(w,profile.c_str());const EID air=w.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f);
            const EID person=w.spawnUnit(0,UnitType::GI,19.5f,20.5f);w.orderBoard({person},air);ok &= finishBoarding(w,{person},air);
            w.orderMove({air},20.5f,8.5f,false);for(int i=0;i<120;++i)w.update();for(auto& cell:w.map.cells)cell.terrain=Terrain::Water;
            w.orderUnload({air});for(int i=0;i<150 && ok;++i){w.update();ok &= w.ents[air].airborne() && w.ents[air].cargo.size()==1;}
            check(ok && w.ents[air].nativeAirUnload && roundtrip(w,7),"a transport with no legal landing site keeps its passenger and saved retry state while hovering above water");
            const auto retry=(std::filesystem::path(outputDir)/(std::string(edition)+"-water-retry-world56.sav")).string();
            FILE* file=std::fopen(retry.c_str(),"wb");const bool stored=file && w.saveGame(file);if(file)std::fclose(file);
            check(ok && stored && w.ents[air].nativeAirLandingRetry>0,"an actual World55 archive records the pending unload together with its bounded water-search retry deadline");
            const auto before=w.checksum();bool idempotent=true;for(int i=0;i<8;++i){const auto queued=w.orderUnload({air});idempotent &= queued.pending==1 && w.checksum()==before;}
            check(ok && idempotent,"repeated unload requests preserve the same pending landing and bounded search retry instead of restarting it");
            const int cx=(int)w.ents[air].x,cy=(int)w.ents[air].y;
            for(int y=cy-1;y<=cy+1;++y)for(int x=cx+2;x<=cx+4;++x){auto& cell=w.map.at(x,y);cell.terrain=Terrain::Clear;cell.height=2;}
            for(int i=0;i<1200 && ok && !w.ents[air].cargo.empty();++i)w.update();
            if(!w.ents[air].cargo.empty() || w.ents[air].nativeAltitude!=208){const auto& e=w.ents[air];
                std::printf("AIR_GROUND SHORE xy=%.9g,%.9g z=%.9g phase=%d state=%d pending=%d cargo=%zu speed=%.9g goal=%.9g,%.9g ground=%d layer=%d retry=%llu\n",
                    e.x,e.y,e.nativeAltitude,(int)e.nativeJumpjet.phase,(int)e.state,e.nativeAirUnload,e.cargo.size(),e.nativeJumpjet.speed,e.goalX,e.goalY,e.nativeJumpjet.ground,e.navLayer,(unsigned long long)e.nativeAirLandingRetry);}
            check(ok && w.ents[air].cargo.empty() && !w.ents[air].airborne() && w.ents[air].nativeAltitude==208,
                "a newly available elevated shore resumes the same unload command and lands at its real terrain height");
        }
        {
            World w;bool ok=fresh(w,profile.c_str());const EID air=w.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f);
            w.orderMove({air},20.5f,8.5f,false);for(int i=0;i<120;++i)w.update();
            const EID person=w.spawnUnit(0,UnitType::GI,w.ents[air].x+1,w.ents[air].y);
            ok &= w.orderBoard({person},air).accepted==1;w.update();
            const bool waiting=ok && (w.valid(person) || !w.ents[air].airborne());
            check(waiting,"boarding a nearby flying transport waits for physical ground contact");
            auto replay=w;replay.map.bldOccRef=&replay.bldOcc;
            const bool resumed=roundtrip(replay,900);
            bool physical=true;for(int i=0;i<900 && w.valid(person);++i){w.update();if(!w.valid(person))physical &= !w.ents[air].airborne();}
            std::printf("AIR_GROUND_PICKUP admitted=%d waiting=%d physical=%d loaded=%zu live=%d replay=%d\n",ok,waiting,physical,w.ents[air].cargo.size(),w.valid(person),resumed);
            check(waiting && physical && resumed && !w.valid(person) && w.ents[air].cargo.size()==1,"an accepted pickup command lands the transport and completes boarding with exact saved continuation");
        }
        {
            World w;bool ok=fresh(w,profile.c_str());bridge(w);
            const EID air=w.spawnUnit(0,UnitType::Nighthawk,21.5f,26.5f),person=w.spawnUnit(0,UnitType::GI,20.5f,26.5f);
            const auto identity=w.ents[person].nativeIdentity.object;w.orderBoard({person},air);ok &= finishBoarding(w,{person},air);
            w.orderMove({air},21.5f,21.5f,false);for(int i=0;i<1000 && ok;++i)w.update();
            ok &= !w.ents[air].airborne() && w.ents[air].navLayer==1 && w.ents[air].nativeAltitude==624;
            const auto result=w.orderUnload({air});EID released=INVALID_EID;
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].nativeIdentity.object==identity)released=id;
            check(ok && result.released==1 && w.valid(released) && w.ents[released].navLayer==1 && w.actorSurfaceHeight(w.ents[released])==6,
                "on supplied height-6 bridge geometry above height-2 water, a transport lands and unloads on the deck surface");
        }
        {
            World w;bool ok=fresh(w,profile.c_str());for(int y=19;y<=21;++y)for(int x=19;x<=21;++x)w.map.at(x,y).height=5;
            const EID air=w.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f),person=w.spawnUnit(0,UnitType::GI,19.5f,20.5f);
            w.orderBoard({person},air);ok &= finishBoarding(w,{person},air);
            for(int y=19;y<=21;++y)for(int x=19;x<=21;++x)if(x!=20 || y!=20)w.map.at(x,y).height=0;
            const auto blocked=w.orderUnload({air});
            check(ok && blocked.released==0 && w.ents[air].cargo.size()==1,"a landed transport cannot unload through a five-level cliff into a disconnected lower surface");
            const EID below=w.spawnUnit(0,UnitType::GI,21.5f,20.5f);const auto boarding=w.orderBoard({below},air);
            check(boarding.accepted==0 && boarding.reason==EntryRejectReason::Unreachable,"a nearby passenger below a cliff cannot board a transport parked on the inaccessible upper ledge");
            ok &= w.ents[air].nativeAirUnload && roundtrip(w,7);w.map.at(19,20).height=5;
            for(int i=0;i<120 && !w.ents[air].cargo.empty();++i)w.update();
            check(ok && w.ents[air].cargo.empty() && !w.ents[air].nativeAirUnload,"opening a connected exit on the same ledge completes the original pending unload without a second command");
        }
        {
            World w;bool ok=fresh(w,profile.c_str());const EID air=w.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f);
            const EID a=w.spawnUnit(0,UnitType::GI,19.5f,20.5f),b=w.spawnUnit(0,UnitType::GI,20.5f,19.5f);
            const std::string first=w.ents[a].nativeIdentity.object,second=w.ents[b].nativeIdentity.object;w.ents[a].hp=61;w.ents[b].hp=62;
            w.orderBoard({a,b},air);ok &= finishBoarding(w,{a,b},air);
            for(auto& cell:w.map.cells)cell.terrain=Terrain::Water;w.map.at(20,20).terrain=w.map.at(21,20).terrain=Terrain::Clear;
            const auto partial=w.orderUnload({air});ok &= partial.released==1 && partial.pending==1 && w.ents[air].cargo.size()==1;
            Game feedback;feedback.world=w;feedback.localPlayer=0;feedback.phase=Phase::InGame;World::Cmd command{};command.type=World::Cmd::Unload;command.ids={air};
            feedback.world.bindCommand(command);feedback.world.applyCmd(0,command);feedback.consumePresentationEvents();
            check(ok && !feedback.moveFeedbackRejected && feedback.msg==TextFormat(TR(S::MsgReleaseWaiting),1),
                "a partially unloaded grounded transport reports waiting for an exit instead of claiming to be descending");
            const auto path=(std::filesystem::path(outputDir)/(std::string(edition)+"-partial-world56.sav")).string();FILE* file=std::fopen(path.c_str(),"wb");ok &= file && w.saveGame(file);if(file)std::fclose(file);
            ok &= roundtrip(w,7);w.map.at(19,20).terrain=Terrain::Clear;for(int i=0;i<120 && !w.ents[air].cargo.empty();++i)w.update();
            int restored=0;for(const auto& e:w.ents)if(e.alive && ((e.nativeIdentity.object==first && e.hp==61) || (e.nativeIdentity.object==second && e.hp==62)))++restored;
            check(ok && restored==2 && w.ents[air].cargo.empty() && !w.ents[air].nativeAirUnload,
                "partial unloading saves the exact remaining passenger and completes both original identities when another connected exit opens");
        }
    }
    {
        World w;bool ok=fresh(w,"assets/rules/profiles/yr.ini");bridge(w);
        const EID air=w.spawnUnit(0,UnitType::SiegeChopper,21.5f,21.5f);for(int i=0;i<160 && ok;++i)w.update();
        const EID ship=w.spawnUnit(0,UnitType::SeaScorpion,21.5f,21.5f);ok &= w.orderDeployment({air},true).accepted==1;
        for(int i=0;i<g_gameRules.chopperLandingTicks+g_gameRules.chopperDeployTicks+10;++i)w.update();
        check(ok && w.ents[air].deployed && !w.ents[air].airborne() && w.ents[air].navLayer==1 && w.ents[air].nativeAltitude==624
            && w.groundUnitBlocksCell(21,21,ship,1),"a YR chopper deploys on the supplied bridge deck while a separate ship occupies the water below");
        w.orderMove({ship},23.5f,21.5f,false);for(int i=0;i<300;++i)w.update();
        check(ok && distf(w.ents[ship].x,w.ents[ship].y,23.5f,21.5f)<.5f,"the deployed aircraft reserves the deck without blocking the ship's lower route");
        ok &= w.orderDeployment({air},true).accepted==1;for(int i=0;i<g_gameRules.chopperLandingTicks+g_gameRules.chopperDeployTicks+100;++i)w.update();
        check(ok && !w.ents[air].deployed && w.ents[air].airborne() && !w.groundUnitBlocksCell(21,21,INVALID_EID,1),"the bridge chopper can undeploy, take off and release its deck occupation");
    }
    {
        Game old;bool ok=old.loadGameFile("review_out/project-review-20261115/legacy-world53-app11-scene22.sav");
        bool versionOne=false;for(const auto& d:old.world.nativeTypes)if(d->jumpjet.available)versionOne |= d->jumpjet.version==1;
        check(ok && versionOne && old.world.nativeJumpjetEnabled,"an actual World53/App11/Scene22 archive retains its original Jumpjet movement version");
        if(ok){old.restartCurrentMatch();const EID air=old.world.spawnUnit(0,UnitType::Nighthawk,20.5f,20.5f);
            ok &= old.world.valid(air) && !old.world.usesNativeAirNavigation(old.world.ents[air]);
            old.world.ents[air].cargo.push_back({UnitType::GI,50,0,0,0});old.world.orderMove({air},20.5f,8.5f,false);
            for(int i=0;i<120 && ok;++i)old.world.update();const auto released=old.world.orderUnload({air});
            check(ok && released.released==1 && !old.world.ents[air].nativeAirUnload,"restarting that archive preserves its historical immediate-unload behavior instead of silently changing the saved rules");}
        std::ifstream input("review_out/project-review-20261115/air-ground-reference/ra2-scene22.ini",std::ios::binary);
        std::string source((std::istreambuf_iterator<char>(input)),{});const std::string key="JumpjetVersion = 1";
        const auto where=source.find(key);bool rejected=where!=std::string::npos;
        if(rejected){source[where+key.size()-1]='2';World incomplete;incomplete.numPlayers=2;incomplete.players.resize(2);rejected=!incomplete.restoreNativeDefinitions(source);}
        check(rejected,"version-two air navigation cannot be enabled inside a Scene22 package that lacks verified building-clearance metadata");
    }
    int port=NET_PORT+1050;
    for(auto choice:{RulesChoice::RA2,RulesChoice::YR}){
        Game host,client;host.selectRulesChoice(choice);client.selectRulesChoice(choice);
        host.cfgCountry=client.cfgCountry=(int)Country::America;host.cfgMapSize=64;host.cfgCrates=false;host.cfgColor=0;client.cfgColor=1;
        host.lobbyRole=0;client.lobbyRole=1;host.phase=client.phase=Phase::NetLobby;host.lobbyState=client.lobbyState=1;
        bool connected=false;for(int i=0;i<100 && !connected;++i)connected=host.net.host(++port);
        connected &= client.net.connectTo("127.0.0.1",port);
        for(int i=0;i<1000 && connected && (!host.net.connected() || !client.net.connected());++i){host.net.poll();client.net.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        for(int i=0;i<1000 && connected && host.lobbyState!=2;++i){host.netHandleMsgs();client.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        connected &= host.lobbyState==2 && host.netStartHandshake(20261119);
        for(int i=0;i<1000 && connected && (!host.netGame || !client.netGame);++i){host.netHandleMsgs();client.netHandleMsgs();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        bool synchronized=connected && host.netGame && client.netGame;EID air[2]={-1,-1},person[2]={-1,-1};std::string identities[2];
        if(synchronized)for(Game* peer:{&host,&client}){
            for(int y=6;y<=34;++y)for(int x=18;x<=40;++x){auto& cell=peer->world.map.at(x,y);cell.terrain=Terrain::Clear;cell.height=0;cell.blocked=false;cell.overlay=Overlay::None;cell.ore=cell.oreMax=0;}
            for(int p=0;p<2;++p){const EID a=peer->world.spawnUnit(p,UnitType::Nighthawk,20.5f+p*16,30.5f),b=peer->world.spawnUnit(p,UnitType::GI,19.5f+p*16,30.5f);
                if(peer==&host){air[p]=a;person[p]=b;identities[p]=peer->world.ents[b].nativeIdentity.object;}else synchronized &= air[p]==a && person[p]==b;}}
        const auto advance=[&](int until){
            for(int i=0;i<4000 && synchronized && (host.netSimTick<until || client.netSimTick<until) && !host.netDesync && !client.netDesync;++i){
                host.netHandleMsgs();client.netHandleMsgs();if(host.netSimTick<until)host.netAdvance();if(client.netSimTick<until)client.netAdvance();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            synchronized &= host.netSimTick==until && client.netSimTick==until && !host.netDesync && !client.netDesync && host.world.checksum()==client.world.checksum();
        };
        if(synchronized){
            for(Game* peer:{&host,&client}){World::Cmd command{};command.type=World::Cmd::Board;command.ids={person[peer->localPlayer]};command.a=air[peer->localPlayer];peer->issueCmd(command);}advance(30);
            for(int p=0;p<2;++p)synchronized &= host.world.ents[air[p]].cargo.size()==1;
            for(Game* peer:{&host,&client}){World::Cmd command{};command.type=World::Cmd::Move;command.ids={air[peer->localPlayer]};command.x=20.5f+peer->localPlayer*16;command.y=8.5f;peer->issueCmd(command);}advance(150);
            for(Game* peer:{&host,&client}){World::Cmd command{};command.type=World::Cmd::Unload;command.ids={air[peer->localPlayer]};peer->issueCmd(command);}advance(180);
        }
        bool pending=synchronized;for(int p=0;p<2 && pending;++p)pending &= host.world.ents[air[p]].nativeAirUnload && host.world.ents[air[p]].airborne();
        const auto path=(std::filesystem::path(outputDir)/(choice==RulesChoice::RA2?"net-ra2-pending-world56.sav":"net-yr-pending-world56.sav")).string();
        FILE* file=std::fopen(path.c_str(),"wb");pending &= file && host.world.saveGame(file);if(file)std::fclose(file);
        check(pending && roundtrip(host.world,0),"real TCP boarding, flight and unload commands retain matching pending landing state in a saved World55 snapshot");
        if(synchronized)advance(390);bool restored=synchronized;
        for(int p=0;p<2 && restored;++p){bool found=false;for(const auto& e:host.world.ents)found |= e.alive && e.nativeIdentity.object==identities[p];
            restored &= found && !host.world.ents[air[p]].nativeAirUnload && host.world.ents[air[p]].cargo.empty() && !host.world.ents[air[p]].airborne();}
        check(pending && restored,"both source editions finish the 390-tick TCP trip with restored passengers and identical world checksums");
    }
    {
        World ocean;bool ok=fresh(ocean,"assets/rules/profiles/ra2.ini",256);const EID air=ocean.spawnUnit(0,UnitType::Nighthawk,128.5f,128.5f);
        for(auto& cell:ocean.map.cells)cell.terrain=Terrain::Water;
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<32;++i){ocean.ents[air].nativeAirLandingRetry=0;ok &= !ocean.findNativeAirLanding(ocean.ents[air],air);}
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::printf("AIR_GROUND PERF map=256 searches=32 total_ms=%.3f per_search_ms=%.3f\n",ms,ms/32);
        check(ok && ocean.ents[air].nativeAirLandingRetry==ocean.tick+30,"a completely water-covered 256-cell map exhausts landing candidates and schedules one bounded retry");
        const EID balloon=ocean.spawnUnit(0,UnitType::Kirov,140.5f,140.5f);ocean.ents[balloon].nativeAirLandingRetry=ocean.tick+30;
        FILE* file=std::tmpfile();const bool rejected=file && !ocean.saveGame(file);if(file)std::fclose(file);
        check(rejected,"a landing-only retry record cannot be attached to a balloon actor that never lands");
    }
    g_script.shutdown();std::printf("AIR_GROUND failures=%d\n",failures);return failures;
}
