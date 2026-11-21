#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <tuple>

int GameRegression::nativeV3Regression(const char* outputDir) {
    const auto path=contentResolve("assets/maps/native/all01t/scene.ini");
    if(path.empty() || !FileExists(path.c_str())){std::printf("NATIVE_V3 SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0,serial=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_V3 %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    g_script.shutdown();ais.clear();world=World{};localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    if(!world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        || !world.loadNativeScene(path.c_str())){check(false,"original V3 scene loads");return failures;}
    int definition=-1,placed=0;
    for(int i=0;i<(int)world.nativeRocketDefinitions.size();++i)
        if(world.nativeTypes[world.nativeRocketDefinitions[i].launcher]->id=="V3")definition=i;
    for(const auto& e:world.ents)if(e.alive && e.nativeIdentity.definition->id=="V3")++placed;
    check(definition>=0 && placed==3 && world.nativeLaunchers.size()==7,"the three authored V3s receive their own launchers alongside the four original DREDs");
    if(definition<0)return failures+1;
    const auto d=world.nativeRocketDefinitions[definition];
    check(d.capacity==1 && d.pause==0 && d.tilt==60 && d.regenerate==400 && d.reload==0 && d.rof==150
        && d.minimumRange==5 && d.range==18 && d.damage==200 && d.eliteDamage==400 && d.lazyCurve
        && d.flh[0]==-160 && d.flh[1]==0 && d.flh[2]==75 && d.normal.verses[9]==500 && d.elite.verses[9]==.8f,
        "V3 keeps its source single slot, zero pause, 5..18 range, rear FLH, 200/400 payload and map-local 50000-percent warhead");
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto resume=[&](int ticks) {
        FILE* file=tmpfile();World restored;bool exact=file && world.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
        exact &= restored.checksum()==world.checksum();
        for(int i=0;i<ticks && exact;++i){world.update();restored.update();exact &= restored.checksum()==world.checksum();}
        return exact;
    };
    const auto find=[&](EID id)->const NativeRocketState*{for(const auto& r:world.nativeRockets)if(r.actor==id)return &r;return nullptr;};
    const char* previous="review_out/native-campaign/legacy-native40-missile.sav";
    if(FileExists(previous)) {
        check(loadGameFile(previous) && world.nativeRocketDefinitions.size()==1 && resume(50),
            "an actual pre-upgrade World40/Scene8 missile save still restores the one-launcher schema and resumes exactly");
    }
    Vec2i origin{-1,-1};
    const auto arena=[&]() {
        world=initial;world.map.bldOccRef=&world.bldOcc;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        world.projs.clear();world.timedBombs.clear();world.effects.clear();world.update();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        origin={-1,-1};
        for(int y=5;y<world.map.h-5 && origin.x<0;++y)for(int x=5;x<world.map.w-22 && origin.x<0;++x) {
            bool clear=true;
            for(int dx=-2;dx<=20 && clear;++dx)for(int dy=-2;dy<=2 && clear;++dy)
                clear=world.map.surfacePassable(x+dx,y+dy,0,0,true,true)
                    && world.map.at(x+dx,y+dy).height==world.map.at(x,y).height;
            if(clear)origin={x,y};
        }
        return origin.x>=0;
    };
    const auto spawn=[&](const char* name,int player,float x,float y=0) {
        int type=-1;for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==name){type=i;break;}
        return world.spawnNativeActor(type,player,origin.x+x+.5f,origin.y+y+.5f,256,64,"counter:v3:"+std::to_string(serial++),"","Guard");
    };
    const auto stop=[&](EID actor){World::Cmd cmd{};cmd.type=World::Cmd::Stop;cmd.ids={actor};world.applyCmd(world.ents[actor].player,cmd);};
    const auto setup=[&](float distance,int rank=0) {
        if(!arena())return std::pair<EID,EID>{-1,-1};
        const EID actor=spawn("V3",0,0),target=spawn("E2",1,distance);
        if(world.valid(actor))world.ents[actor].vetRank=rank;
        if(world.valid(target)){world.ents[target].hp=10000;world.ents[target].tpSick=10000;}
        return std::pair{actor,target};
    };
    {
        const auto [actor,target]=setup(8);if(!world.valid(actor) || !world.valid(target)){check(false,"a level native-terrain V3 arena is available");return failures;}
        const auto rejected=world.orderDeployment({actor});
        check(rejected.accepted==0 && rejected.rejected==1 && rejected.reason==DeployRejectReason::Unsupported && !world.ents[actor].deployed,
            "native V3 does not inherit the prototype's manual deployment mode");
        for(float range:{4.99f,18.01f}){world.ents[target].x=world.ents[actor].x+range;
            check(!world.launchNativeRocket(actor,target) && world.nativeRockets.empty(),"the source minimum and maximum reject out-of-annulus launches");}
    }
    World tiltShot,flightShot;EID shotActor=-1;
    std::ofstream trace(std::filesystem::path(outputDir)/"native-v3-flight.csv");
    trace<<"range,rank,tick,x,y,altitude,pitch,speed,diving\n";
    for(const auto [distance,rank]:{std::pair{5.f,0},{5.5f,0},{8.f,0},{12.f,0},{18.f,0},{12.f,2}}) {
        const auto [actor,target]=setup(distance,rank);bool exact=world.valid(actor) && world.valid(target);
        if(!exact){check(false,"source V3 and actual target spawn");continue;}
        exact &= world.orderAttack({actor},target,false).accepted==1;world.update();
        const EID missile=world.nativeRockets.empty()?-1:world.nativeRockets.front().actor;
        exact &= world.valid(missile) && find(missile) && find(missile)->phase==NativeRocketPhase::Tilt
            && world.ents[missile].hp==50 && unitDef(world.ents[missile]).armor==Armor::Special2
            && world.ents[missile].nativeIdentity.type==d.missile && world.projs.empty() && !world.ents[actor].deployed;
        if(!world.valid(missile)){check(false,"a real V3ROCKET begins tilting without a zero-duration pause or phantom shell");continue;}
        const auto* attached=find(missile);
        exact &= std::abs(world.ents[missile].x-world.ents[actor].x-d.flh[0]*std::cos(attached->heading)/256)<1e-4f
            && std::abs(world.ents[missile].y-world.ents[actor].y-d.flh[0]*std::sin(attached->heading)/256)<1e-4f;
        if(distance==12 && rank==0) {
            check(exact && resume(30),"the 50-HP missile's zero-pause tilt, rear launch offset and slot restore exactly");
            tiltShot=world;tiltShot.map.bldOccRef=&tiltShot.bldOcc;shotActor=actor;
        }
        const auto birth=world.tick-(uint64_t)find(missile)->phaseTicks+1;
        while(world.valid(missile) && find(missile)->phase!=NativeRocketPhase::Flight && world.tick<birth+125)world.update();
        const uint64_t detached=world.tick;
        exact &= world.valid(missile) && find(missile)->phase==NativeRocketPhase::Flight && find(missile)->slot==-1
            && detached==birth+119 && world.nativeDockedSpawns(actor)==0
            && nativeActorFrame(world.ents[actor])==(world.ents[actor].dir&7)+8;
        stop(actor); // Isolate this real shot, without modifying its payload or flight state.
        const int hp=world.ents[target].hp;float peak=0;bool rising=false,falling=false,midpoint=false;
        while(world.valid(missile) && world.tick<detached+1400) {
            const auto* r=find(missile);if(!r){exact=false;break;}
            peak=std::max(peak,r->altitude-r->destinationZ);rising |= r->pitch>.05f;falling |= r->pitch<-.05f;
            exact &= (!r->cruised || r->diving) && world.projs.empty();
            trace<<distance<<','<<rank<<','<<world.tick<<','<<world.ents[missile].x<<','<<world.ents[missile].y<<','<<r->altitude<<','<<r->pitch<<','<<r->speed<<','<<r->diving<<'\n';
            if(distance==12 && rank==0 && !midpoint && r->diving) {
                flightShot=world;flightShot.map.bldOccRef=&flightShot.bldOcc;midpoint=true;exact &= resume(20);
            } else world.update();
        }
        const int delivered=hp-world.ents[target].hp;
        exact &= !world.valid(missile) && rising && falling && peak>200 && delivered==(rank==2?360:180);
        std::printf("NATIVE_V3 TRACE range=%.1f rank=%d impact=%llu peak=%.1f damage=%d\n",distance,rank,(unsigned long long)world.tick,peak,delivered);
        check(exact,"a commanded V3 flies an ascending/descending arc and applies its real normal/elite warhead once within a bounded flight");
        if(distance==12 && rank==0) {
            const uint64_t ready=detached+800;
            exact=world.nativeLaunchers.size()==1 && world.nativeLaunchers.front().nodes[0].readyAt==ready;
            while(world.tick+1<ready)world.update();exact &= world.nativeDockedSpawns(actor)==0;
            world.update();exact &= world.tick==ready && world.nativeDockedSpawns(actor)==1;
            check(exact && resume(1),"the empty V3WO body and one spent slot regenerate at the converted 400-frame deadline");
        }
    }
    {
        const auto [actor,target]=setup(8);world.orderAttack({actor},target,false);world.update();
        const EID first=world.nativeRockets.front().actor;const uint64_t born=world.tick;
        while(world.valid(first) && find(first)->phase!=NativeRocketPhase::Flight)world.update();
        const uint64_t ready=world.tick+800;bool early=false;uint64_t second=0;
        while(world.tick<=ready+2 && !second) {
            world.update();for(const auto& r:world.nativeRockets)if(r.source==actor) {
                if(r.phase==NativeRocketPhase::Tilt){second=world.tick;early |= world.tick<ready;}
            }
        }
        std::printf("NATIVE_V3 RELOAD first=%llu next=%llu ready=%llu\n",(unsigned long long)born,(unsigned long long)second,(unsigned long long)ready);
        check(!early && second==ready && resume(5),"a retained attack order launches again only after the single slot is actually regenerated");
    }
    {
        const auto [actor,target]=setup(10);world.orderAttack({actor},target,false);world.update();const EID missile=world.nativeRockets.front().actor;
        world.kill(actor,false);
        check(!world.valid(missile) && world.nativeRockets.empty() && world.nativeLaunchers.empty() && resume(2),
            "destroying a launcher during tilt retires its attached V3ROCKET and all slot references");
    }
    {
        const auto [actor,target]=setup(10);world.orderAttack({actor},target,false);world.update();const EID missile=world.nativeRockets.front().actor;
        while(world.valid(missile) && find(missile)->phase!=NativeRocketPhase::Flight)world.update();
        const float x=find(missile)->destinationX,y=find(missile)->destinationY;
        world.kill(actor,false);world.kill(target,false);const EID replacement=spawn("E2",1,1);world.ents[replacement].tpSick=10000;
        check((replacement==actor || replacement==target) && world.valid(missile) && find(missile)->source==-1
            && find(missile)->destinationX==x && find(missile)->destinationY==y && resume(50),
            "airborne V3 survives launcher/target retirement and ID reuse without changing its captured ground destination");
        const int hp=world.ents[replacement].hp;while(world.valid(missile) && world.tick<1500)world.update();
        check(!world.valid(missile) && world.valid(replacement) && world.ents[replacement].hp==hp,"a reused entity is not pursued or hit at its new location");
    }
    {
        const auto [actor,target]=setup(10);const EID sam=spawn("NASAM",1,5,1);
        world.spawnBuilding(1,BldType::PowerPlant,origin.x+13,origin.y+2,true);
        world.orderAttack({actor},target,false);world.update();bool hit=false,sourceShot=false;EID missile=-1;
        if(!world.nativeRockets.empty())missile=world.nativeRockets.front().actor;
        for(int i=0;i<350 && !hit;++i) {
            const auto sequence=world.presentationSequence;world.update();
            for(const auto& p:world.projs)sourceShot |= p.alive && p.target==missile && p.w.nativeType==world.ents[sam].nativeIdentity.type && p.w.damage==75;
            for(const auto& event:world.presentationEvents)if(event.sequence>=sequence && event.kind==PresentationKind::Impact
                && event.entity==missile && event.visualTarget==VisualTarget::Aircraft && event.lethal)hit=true;
        }
        check(world.valid(sam) && !world.players[1].lowPower() && sourceShot && hit && !world.valid(missile)
            && world.ents[target].hp==10000,"a powered source Patriot actually kills the 50-HP missile and prevents its ground detonation");
        check(resume(5),"the intercepted missile's spent slot remains consistent across a snapshot");
    }
    for(int rank:{0,2}) {
        const auto [actor,unused]=setup(12,rank);world.kill(unused,false);const EID statue=spawn("CANEWY04",1,11,-1);
        bool exact=world.valid(statue) && world.ents[statue].hp==10000 && world.orderAttack({actor},statue,false).accepted==1;
        world.update();const EID missile=world.nativeRockets.empty()?-1:world.nativeRockets.front().actor;
        exact &= world.valid(missile);stop(actor);
        for(int i=0;i<1500 && world.valid(missile);++i)world.update();
        exact &= !world.valid(missile) && (rank==0?!world.valid(statue):world.valid(statue) && world.ents[statue].hp<10000);
        check(exact,"an actual V3 impact preserves the original normal/elite difference against the special_1 statue foundation");
    }
    for(auto [section,key,value]:{std::tuple{"NativeRockets","Count","17"},{"NativeRockets","Version","3"},
        {"NativeRockets","Version","1"}}) {
        auto source=initial.nativeScenarioSource;const auto begin=source.find("["+std::string(section)+"]");
        const auto at=source.find(std::string(key)+" = ",begin);bool rejected=begin!=std::string::npos && at!=std::string::npos;
        if(rejected){const auto first=source.find('=',at)+2;source.replace(first,source.find('\n',first)-first,value);World invalid=initial;
            invalid.map.bldOccRef=&invalid.bldOcc;rejected=!invalid.restoreNativeDefinitions(source);}
        check(rejected,"malformed multi-launcher counts and unsupported schemas are rejected");
    }
    {
        auto source=initial.nativeScenarioSource;const auto begin=source.find("[NativeRocket.1]");const auto at=source.find("Missile = ",begin)+10;
        source.replace(at,source.find('\n',at)-at,std::to_string(initial.nativeRocketDefinitions[0].missile));World invalid=initial;
        invalid.map.bldOccRef=&invalid.bldOcc;check(!invalid.restoreNativeDefinitions(source),"two launchers cannot alias the same missile type in the strict source schema");
    }
    for(auto* snapshot:{&tiltShot,&flightShot})if(snapshot->valid(shotActor)) {
        world=*snapshot;world.map.bldOccRef=&world.bldOcc;camZoom=.9f;interpAlpha=1;showMenu=placing=dragging=targetingParadrop=false;
        sideMode=0;targetingSW=SWType::COUNT;sel={shotActor};selBuilding=INVALID_EID;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        fogKnowledge={};syncFogKnowledge();
        const EID air=world.nativeRockets.front().actor;
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();camX=camY=0;
            const auto ground=unitScreenPos(world.ents[shotActor]),above=unitScreenPos(world.ents[air]);
            camX=(ground.x+above.x)/2-(SCREEN_W-sidebarW)/(2.f*camZoom);
            camY=(ground.y+above.y-unitRenderLift(world.ents[air]))/2-(SCREEN_H-BOTTOM_BAR_H)/(2.f*camZoom);
            shotFile=(std::filesystem::path(outputDir)/(std::string(snapshot==&tiltShot?"v3-tilt-":"v3-arc-")+std::to_string(size.x)+".png")).string();
            const auto checksum=world.checksum();const auto filename=shotFile;render();Image with=LoadImage(filename.c_str());
            world.ents[air].alive=false;shotFile=filename+"-without.png";render();world.ents[air].alive=true;
            Image without=LoadImage((filename+"-without.png").c_str());int changed=0;
            const auto rect=unitScreenRect(world.ents[air]);const auto dst=letterboxDst();
            const int left=(int)(dst.x+rect.x*camZoom*dst.width/SCREEN_W),top=(int)(dst.y+rect.y*camZoom*dst.height/SCREEN_H);
            const int right=(int)(dst.x+(rect.x+rect.width)*camZoom*dst.width/SCREEN_W),bottom=(int)(dst.y+(rect.y+rect.height)*camZoom*dst.height/SCREEN_H);
            for(int y=std::max(0,top);y<std::min(with.height,bottom);++y)
                for(int x=std::max(0,left);x<std::min(with.width,right);++x)
                    if(with.data && without.data)changed+=ColorToInt(GetImageColor(with,x,y))!=ColorToInt(GetImageColor(without,x,y));
            if(with.data)UnloadImage(with);if(without.data)UnloadImage(without);
            std::printf("NATIVE_V3 VISIBLE size=%d phase=%d pixels=%d rect=%d,%d,%d,%d\n",size.x,(int)world.nativeRockets.front().phase,changed,left,top,right,bottom);
            check(changed>20 && checksum==world.checksum(),"the pitched missile is visibly drawn in both viewports and presentation preserves the simulation");
            if(size.x==1280) {
                const int cell=(int)world.ents[air].y*world.map.w+(int)world.ents[air].x;
                const auto fog=world.map.fog[1][cell];localPlayer=1;
                const auto probe=LoadRenderTexture(SCREEN_W,SCREEN_H);
                const auto countPixels=[&]() {
                    BeginTextureMode(probe);ClearBackground(BLANK);drawNativeRockets();EndTextureMode();
                    Image image=LoadImageFromTexture(probe.texture);int count=0;
                    if(image.data){for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x)count+=GetImageColor(image,x,y).a>0;UnloadImage(image);}
                    return count;
                };
                world.map.fog[1][cell]=FOG_UNSEEN;const int hidden=countPixels();
                world.map.fog[1][cell]=FOG_VISIBLE;const int visible=countPixels();
                world.map.fog[1][cell]=fog;localPlayer=0;UnloadRenderTexture(probe);
                check(hidden==0 && visible>20 && checksum==world.checksum(),
                    "the post-fog missile pass still hides enemies outside ground-cell visibility without changing fog or simulation");
            }
        }
    }
    std::printf("NATIVE_V3 failures=%d\n",failures);return failures;
}
