#include "game_regression.h"
#include "world_save_sections.h"
#include "game/script.h"
#include "game/native_movement_math.h"
#include "fixtures/native_movement_reference.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <bit>
#include <cstring>

int GameRegression::nativeMovementRegression(const char* outputDir) {
    if(!FileExists("assets/maps/native/all01t/scene.ini") || !FileExists("assets/maps/native/all01t/terrain.nmap")) {
        std::printf("NATIVE_MOVEMENT SKIP: export the original scene first\n");return 77;
    }
    int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_MOVEMENT %s %s\n",ok?"PASS":"FAIL",text);if(!ok)++failures;};
    std::filesystem::create_directories(outputDir);
    check((int)NativeMovementMath::multiply(10,1.2)==11
        && (int)NativeMovementMath::multiply(10,(double)1.2f)==12
        && (int)NativeMovementMath::multiply(20,1.2)==23,
        "portable arithmetic matches actual original current-speed function boundary vectors without changing the thread rounding mode");
    check(std::bit_cast<uint32_t>((float)NativeMovementMath::storedFloat(std::bit_cast<double>(0x3fe9999999999999ull)))==0x3f4cccccu,
        "original stored-float conversion matches the original INI 80-percent result");
    const auto replay=[](World source,int ticks){
        FILE* f=tmpfile();if(!f)return false;
        const bool saved=source.saveGame(f);rewind(f);World restored;
        const bool loaded=saved && restored.loadGame(f);fclose(f);if(!loaded || source.checksum()!=restored.checksum())return false;
        source.map.bldOccRef=&source.bldOcc;
        for(int i=0;i<ticks;++i){source.update();restored.update();if(source.checksum()!=restored.checksum())return false;}
        return true;
    };
    g_script.shutdown();ais.clear();netGame=false;phase=Phase::InGame;
    if(FileExists("review_out/native-mobile/legacy-world47-scene15.sav")) {
        const bool loaded=loadGameFile("review_out/native-mobile/legacy-world47-scene15.sav");
        check(loaded && !world.nativeMovementRules.available && replay(world,45),
            "an actual World47 Scene15 application save keeps historical movement and resumes deterministically");
    }
    World source;
    if(!source.init(64,64,73,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
       || !source.loadNativeScene("assets/maps/native/all01t/scene.ini")) {
        check(false,"the source movement scene loads");return failures;
    }
    int profiles=0;for(const auto& d:source.nativeTypes)profiles+=d->movement.available();
    check(source.nativeMovementRules.available && profiles==12 && source.nativeTypes.size()==109,
        "the twelve original mobile ground definitions load without replacing actors or missile locomotion");
    for(const auto& pair:{std::pair{"MTNK",17},std::pair{"V3",10},std::pair{"DRED",10},std::pair{"SAPC",15},std::pair{"CMIN",10},std::pair{"CONA",20}}) {
        const int type=source.nativeUnitIndex(pair.first);
        check(type>=0 && source.nativeTypes[type]->movement.maxSpeed==pair.second,
            "original Speed uses the original bounded percent-to-256 conversion");
    }
    World cleared=source;cleared.map.bldOccRef=&cleared.bldOcc;
    for(EID id=0;id<(EID)cleared.ents.size();++id)if(cleared.valid(id))cleared.kill(id,false);
    Vec2i land{-1,-1},water{-1,-1};
    for(int y=2;y<cleared.map.h-2;++y)for(int x=2;x<cleared.map.w-15;++x){
        bool flat=true,sea=true;
        for(int n=0;n<14;++n){
            flat &= cleared.map.surfacePassable(x+n,y,0,0,false) && cleared.map.at(x+n,y).height==cleared.map.at(x,y).height;
            sea &= cleared.map.inBounds(x+n,y) && cleared.map.at(x+n,y).terrain==Terrain::Water && !cleared.map.hasDeck(x+n,y);
        }
        if(flat && land.x<0)land={x,y};if(sea && water.x<0)water={x,y};
    }
    check(land.x>=0 && water.x>=0,"isolated fixtures find actual source-map land and water lanes");
    if(land.x<0 || water.x<0)return failures;
    const auto spawn=[&](World& w,const char* name,Vec2i p,int rank=0,int health=256){
        w.map.bldOccRef=&w.bldOcc;const int type=w.nativeUnitIndex(name);
        const EID id=w.spawnNativeActor(type,0,p.x+.5f,p.y+.5f,health,64,"movement:fixture","","Guard");
        if(w.valid(id))w.ents[id].vetRank=rank;
        return id;
    };
    const auto move=[](World& w,EID id,Vec2i p){World::Cmd c;c.type=World::Cmd::Move;c.ids={id};c.x=p.x+.5f;c.y=p.y+.5f;w.applyCmd(0,c);};
    const auto advance=[](World& w,int ticks){for(int i=0;i<ticks;++i)w.update();};
    const auto distance=[&](const World& w,EID id,Vec2i p){return w.ents[id].x-(p.x+.5f);};
    const auto edit=[](std::string text,const std::string& section,const std::string& key,const std::string& value){
        const auto begin=text.find("["+section+"]");if(begin==std::string::npos)return std::string{};
        const auto end=text.find("\n[",begin);const auto field=text.find("\n"+key+" =",begin);
        if(field==std::string::npos || (end!=std::string::npos && field>end))return std::string{};
        text.replace(field+1,text.find('\n',field+1)-field-1,key+" = "+value);return text;
    };
    // Explicit physical fixtures use a present source-map square; artwork and
    // authored-mission acceptance are exercised by the separate playthrough.
    Vec2i square{-1,-1};
    for(int y=2;y<cleared.map.h-12 && square.x<0;++y)for(int x=2;x<cleared.map.w-12 && square.x<0;++x) {
        bool present=true;for(int dy=0;dy<11;++dy)for(int dx=0;dx<11;++dx)present &= cleared.map.inBounds(x+dx,y+dy);
        if(present)square={x,y};
    }
    check(square.x>=0,"straight and diagonal mechanism fixtures find a full present map square");
    if(square.x>=0) {
        World flat=cleared;
        for(int dy=0;dy<11;++dy)for(int dx=0;dx<11;++dx) {
            const int x=square.x+dx,y=square.y+dy;flat.map.at(x,y)=Cell{};
            flat.map.native.tiles[(size_t)y*flat.map.w+x].terrainType=13;
        }
        const Vec2i origin{square.x+1,square.y+1};
        World diagonal=flat;const EID diag=spawn(diagonal,"MTNK",origin);move(diagonal,diag,{origin.x+8,origin.y+8});advance(diagonal,10);
        const double axis=68./7*8/256;
        check(std::abs(distance(diagonal,diag,origin)-axis)<.0002
            && std::abs(diagonal.ents[diag].y-(origin.y+.5)-axis)<.0002,
            "actual diagonal Cmd uses original 32-sample track timing instead of cardinal timing with Euclidean normalization");
        World continuous=flat;const EID tank=spawn(continuous,"MTNK",origin);move(continuous,tank,{origin.x+8,origin.y});advance(continuous,60);
        check(std::abs(distance(continuous,tank,origin)-(2+157./7*11/256))<.001 && replay(continuous,90),
            "credit crosses multiple original track cells without losing the remainder or charging a source frame twice");
        World dwell=flat;
        for(size_t i=0;i<dwell.map.cells.size();++i)if(dwell.map.cells[i].present){dwell.map.cells[i]=Cell{};dwell.map.native.tiles[i].terrainType=13;}
        const Vec2i bay{square.x+4,square.y+4};const EID delayed=spawn(dwell,"MTNK",bay);
        move(dwell,delayed,{bay.x+5,bay.y});dwell.ents[delayed].path.insert(dwell.ents[delayed].path.begin(),3,bay);
        bool waited=true;
        for(int i=0;i<3;++i){dwell.update();waited &= dwell.ents[delayed].pathIdx==i+1 && distance(dwell,delayed,bay)==0
            && dwell.ents[delayed].nativeMovement==NativeMovementState{} && dwell.ents[delayed].walkAnim==0;}
        check(waited,"native traffic waiting nodes advance once per tick without distance, animation or accumulated movement credit");
        check(replay(dwell,100),"a source vehicle resumes deterministically after its waiting prefix");advance(dwell,300);
        check(dwell.ents[delayed].x==bay.x+5.5f && dwell.ents[delayed].y==bay.y+.5f,
            "a source vehicle leaves its waiting bay and reaches the actual destination");
        World braking=flat;const EID slow=spawn(braking,"V3",origin);move(braking,slow,{origin.x+2,origin.y});advance(braking,80);
        check(braking.ents[slow].nativeMovement.started && braking.ents[slow].nativeMovement.throttle==(double).3f,
            "near-destination original drive uses its measured 0.3-float minimum throttle");
        advance(braking,600);
        check(braking.ents[slow].x==origin.x+2.5f && braking.ents[slow].y==origin.y+.5f
            && braking.ents[slow].nativeMovement==NativeMovementState{},
            "braking reaches the actual destination and discards residual motion credit");
        World blocked=flat;
        for(auto& cell:blocked.map.cells)cell.blocked=true;
        for(int x=origin.x;x<=origin.x+8;++x)blocked.map.at(x,origin.y).blocked=false;
        const EID waiting=spawn(blocked,"V3",origin);move(blocked,waiting,{origin.x+8,origin.y});advance(blocked,60);
        const EID obstacle=blocked.spawnUnit(0,UnitType::Grizzly,origin.x+1.5f,origin.y+.5f);
        blocked.ents[obstacle].deployed=true;blocked.ents[obstacle].guard=true;
        const float before=blocked.ents[waiting].x;advance(blocked,90);
        check(blocked.ents[waiting].x==before && blocked.ents[waiting].nativeMovement==NativeMovementState{},
            "a fixed physical blocker stops source movement and cannot accumulate future travel credit");
        blocked.kill(obstacle,false);move(blocked,waiting,{origin.x+8,origin.y});advance(blocked,10);
        check(blocked.ents[waiting].x==before && replay(blocked,90),
            "unblocked source movement restarts its real acceleration and resumes deterministically");
    }
    for(const char* name:{"DRED","V3"}) {
        World w=cleared;const auto lane=std::string(name)=="DRED"?water:land;
        const EID id=spawn(w,name,lane);move(w,id,{lane.x+12,lane.y});bool exact=true;
        const auto& frames=std::string(name)=="DRED"?NativeMovementReference::Constructor:NativeMovementReference::V3;
        for(const auto& expected:frames) {
            advance(w,2);const auto& actual=w.ents[id].nativeMovement;
            exact &= std::bit_cast<uint64_t>(actual.throttle)==expected.throttle && actual.heldSpeed==expected.speed;
        }
        check(exact,"thirty actual source-frame throttle bits and integer speeds match the independent original x86 numeric oracle");
    }
    for(int rank=0;rank<3;++rank) {
        World w=cleared;const EID id=spawn(w,"MTNK",land,rank);move(w,id,{land.x+12,land.y});advance(w,10);
        // Four original frames contribute after the initial driver frame.
        // Source Track1: every seven budget units advances eleven leptons.
        const double expected=(rank?80.:68.)/7*11/256;
        std::printf("NATIVE_MOVEMENT SPEED type=MTNK rank=%d distance=%.8f expected=%.8f\n",rank,distance(w,id,land),expected);
        check(std::abs(distance(w,id,land)-expected)<.0002,
            "actual Cmd displacement follows original straight-track timing and FASTER rather than prototype delay or generic elite speed");
        check(replay(w,130),"partial-track movement and 15-Hz frame halves resume tick for tick");
        if(!rank){world=w;world.map.bldOccRef=&world.bldOcc;gameOver=victory=paused=showMenu=false;campaignMission=-1;localPlayer=0;
            const auto file=(std::filesystem::path(outputDir)/"grizzly-track.sav").string();check(saveGameFile(file.c_str()),"actual partial-track application state saves");}
    }
    World accelerating=cleared;const EID v3=spawn(accelerating,"V3",land);move(accelerating,v3,{land.x+12,land.y});advance(accelerating,10);
    check(distance(accelerating,v3,land)==0 && accelerating.ents[v3].nativeMovement.throttle>0,
        "V3 original 0.01 acceleration does not move until its integer current speed becomes positive");
    advance(accelerating,50);
    std::printf("NATIVE_MOVEMENT SPEED type=V3 tick=60 distance=%.8f throttle=%.12f\n",distance(accelerating,v3,land),accelerating.ents[v3].nativeMovement.throttle);
    check(distance(accelerating,v3,land)>0 && distance(accelerating,v3,land)<.5f && replay(accelerating,160),
        "the slow original V3 accelerates physically and keeps its throttle through save/load");
    world=accelerating;world.map.bldOccRef=&world.bldOcc;
    check(saveGameFile((std::filesystem::path(outputDir)/"v3-accelerating.sav").string().c_str()),"actual accelerating V3 application state saves");
    const auto speedBefore=accelerating.ents[v3].nativeMovement;
    World hashed=accelerating;hashed.ents[v3].nativeMovement.throttle+=.001;
    check(hashed.checksum()!=accelerating.checksum(),"future movement throttle participates in the World checksum");
    hashed=accelerating;hashed.ents[v3].nativeMovement.throttle=std::numeric_limits<double>::quiet_NaN();
    check(!hashed.validateNativeMovementState(),"nonfinite movement state is rejected");
    hashed=accelerating;hashed.ents[v3].nativeMovement.nodeLayer=2;
    check(!hashed.validateNativeMovementState(),"invalid movement surface selectors are rejected");
    {
        FILE* f=tmpfile();bool rejected=false;
        if(f && accelerating.saveGame(f)) {
            const long n=ftell(f);rewind(f);std::vector<uint8_t> bytes((size_t)n);
            if(fread(bytes.data(),1,bytes.size(),f)==bytes.size() && bytes.size()>8) {
                const auto section=locateWorldSaveSection(bytes,WorldSaveSection::NativeMovement);
                if(section && section->length>4)bytes[section->footer-1]=2;
                fclose(f);f=tmpfile();
                if(f && fwrite(bytes.data(),1,bytes.size(),f)==bytes.size()){rewind(f);World invalid;rejected=!invalid.loadGame(f);}
            }
        }
        if(f)fclose(f);check(rejected,"a corrupted persisted source-frame half is rejected by actual World loading");
    }
    World::Cmd stop;stop.type=World::Cmd::Stop;stop.ids={v3};accelerating.applyCmd(0,stop);
    check(accelerating.ents[v3].nativeMovement==NativeMovementState{} && speedBefore.throttle>0,
        "an accepted Stop clears old throttle and residual travel credit");
    const float stopped=accelerating.ents[v3].x;advance(accelerating,8);move(accelerating,v3,{land.x+12,land.y});advance(accelerating,6);
    check(accelerating.ents[v3].x==stopped,"a fresh V3 order accelerates again without a stored-credit jump");
    for(const char* name:{"DRED","SAPC","CONA","CMIN"}) {
        World w=cleared;const auto lane=std::string(name)=="DRED"?water:land;
        const EID id=spawn(w,name,lane);move(w,id,{lane.x+12,lane.y});advance(w,60);
        std::printf("NATIVE_MOVEMENT SPEED type=%s tick=60 distance=%.8f\n",name,distance(w,id,lane));
        check(distance(w,id,lane)>0 && replay(w,100),"ship, hover, civilian and teleport-ground locomotion move and resume");
    }
    const int tankType=source.nativeUnitIndex("MTNK");
    const std::string tankSection="Type."+std::to_string(tankType);
    std::string zeroSource=edit(source.nativeScenarioSource,tankSection,"OriginalSpeed","0");
    World zero=cleared;check(!zeroSource.empty() && zero.restoreNativeDefinitions(zeroSource),"explicit zero-speed source profile parses");
    const EID immobile=spawn(zero,"MTNK",land);move(zero,immobile,{land.x+12,land.y});advance(zero,30);
    check(distance(zero,immobile,land)==0 && zero.ents[immobile].walkAnim==0,"source Speed=0 stays still without advancing the movement clock");
    for(const auto& variant:{std::pair{"healthy",256},std::pair{"yellow",128}}) {
        World w=cleared;const EID id=spawn(w,"MTNK",land,0,variant.second);move(w,id,{land.x+12,land.y});advance(w,10);
        const double expected=(variant.second==128?48.:68.)/7*11/256;
        check(std::abs(distance(w,id,land)-expected)<.0002,"original yellow-health boundary applies the 0.75 drive throttle before integer speed truncation");
    }
    World ore=cleared;for(int n=0;n<14;++n){auto& c=ore.map.at(land.x+n,land.y);c.terrain=Terrain::Ore;c.ore=300;}
    const EID onOre=spawn(ore,"MTNK",land);move(ore,onOre,{land.x+12,land.y});advance(ore,10);
    check(std::abs(distance(ore,onOre,land)-44./7*11/256)<.0002,"actual resource substrate selects original 70-percent Track speed");
    std::string biased=edit(source.nativeScenarioSource,"NativeMovement.House.0","SpeedUnitsMult","0.5");
    World country=cleared;check(country.restoreNativeDefinitions(biased),"source country unit-speed multiplier parses");
    const EID foreign=spawn(country,"MTNK",land,1);move(country,foreign,{land.x+12,land.y});advance(country,10);
    check(std::abs(distance(country,foreign,land)-36./7*11/256)<.0002,
        "house speed truncates before the original veteran multiplier, preserving original arithmetic order");
    const int minerType=source.nativeUnitIndex("CMIN");
    std::string rounded=edit(source.nativeScenarioSource,"NativeMovement.House.0","SpeedUnitsMult","0.7");
    rounded=edit(rounded,"Type."+std::to_string(minerType),"OriginalAccelerates","0");
    World originalFloat=cleared;check(originalFloat.restoreNativeDefinitions(rounded),"the original float country-multiplier boundary parses");
    const EID miner=spawn(originalFloat,"CMIN",land);move(originalFloat,miner,{land.x+12,land.y});advance(originalFloat,10);
    check(std::abs(distance(originalFloat,miner,land)-24./7*11/256)<.0002,
        "original float 0.7 times ten truncates to six rather than a higher double-only speed");
    for(const auto& row:{std::pair{"OriginalAccelerationFactor","nan"},std::pair{"OriginalSpeedType","Other"},std::pair{"OriginalSlowdownDistance","-1"},std::pair{"MovementLocomotor","Hover"}}) {
        World malformed=cleared;check(!malformed.restoreNativeDefinitions(edit(source.nativeScenarioSource,tankSection,row.first,row.second)),
            "malformed source movement metadata rejects before committing definitions");
    }
    std::printf("NATIVE_MOVEMENT failures=%d\n",failures);return failures;
}
