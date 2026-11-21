#include "game_regression.h"
#include "core/content.h"
#include "game/script.h"
#include "gfx/sprites.h"
#include "gfx/assets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

int GameRegression::nativeMotionRegression(const char* outputDir) {
    const auto path=contentResolve("assets/maps/native/all01t/scene.ini");
    if(path.empty() || !FileExists(path.c_str())){std::printf("NATIVE_MOTION SKIP: export the original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_MOTION %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto resume=[&](World& a,int ticks){FILE* f=tmpfile();World b;bool equal=f && a.saveGame(f);
        if(equal){rewind(f);equal=b.loadGame(f);}if(f)fclose(f);
        equal &= a.checksum()==b.checksum();for(int i=0;i<ticks && equal;++i){a.update();b.update();equal &= a.checksum()==b.checksum();}return equal;};
    g_script.shutdown();ais.clear();localPlayer=0;phase=Phase::InGame;gameOver=victory=paused=showMenu=netGame=false;campaignMission=-1;
    const char* old="review_out/native-campaign/legacy-native46-movement.sav";
    if(FileExists(old))check(loadGameFile(old) && !world.nativeTypes.empty()
        && std::all_of(world.nativeTypes.begin(),world.nativeTypes.end(),[](const auto& d){return d->walkSpeedLeptons==-1;}) && resume(world,60),
        "the actual World46/Scene14 paid-engineer save retains historical speed and deterministic continuation");
    World source;
    if(path.empty() || !source.init(64,64,73,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true) || !source.loadNativeScene(path.c_str())){
        check(false,"the current source motion scene loads");return failures;}
    for(const auto& item:{std::pair{"TANY",15},std::pair{"E1",10},std::pair{"E2",10},std::pair{"ADOG",20},std::pair{"ENGINEER",10}}) {
        const int type=source.nativeUnitIndex(item.first);
        check(type>=0 && source.nativeTypes[type]->walkSpeedLeptons==item.second,"source walking infantry import their actual 0..255 lepton speed");
    }
    World cleared=source;for(EID id=0;id<(EID)cleared.ents.size();++id)if(cleared.valid(id))cleared.kill(id,false);cleared.effects.clear();
    Vec2i lane{-1,-1},water{-1,-1};
    for(int y=2;y<cleared.map.h-2;++y)for(int x=2;x<cleared.map.w-12;++x){
        bool flat=true;for(int n=0;n<11;++n)flat &= cleared.map.surfacePassable(x+n,y,0,0,false) && cleared.map.at(x+n,y).height==cleared.map.at(x,y).height;
        if(flat && lane.x<0)lane={x,y};
        if(cleared.map.at(x,y).terrain==Terrain::Water && cleared.map.surfacePassable(x,y,2,0,false) && !cleared.map.hasDeck(x,y) && water.x<0)water={x,y};
    }
    check(lane.x>=0 && water.x>=0,"the explicit motion fixture finds real flat land and real base-layer water");if(lane.x<0 || water.x<0)return failures;
    for(const char* name:{"TANY","E1","ENGINEER"}) {
      for(int rank=0;rank<3;++rank) {
        World motion=cleared;const int type=motion.nativeUnitIndex(name);
        const EID id=motion.spawnNativeActor(type,0,lane.x+.5f,lane.y+.5f,256,0,"motion:walk","","Guard");
        motion.ents[id].vetRank=rank;
        World::Cmd c;c.type=World::Cmd::Move;c.ids={id};c.x=lane.x+10.5f;c.y=lane.y+.5f;motion.applyCmd(0,c);
        for(int i=0;i<10;++i)motion.update();
        const float distance=motion.valid(id)?motion.ents[id].x-(lane.x+.5f):0;
        const int expectedLeptons=std::string(name)=="TANY"?15:std::string(name)=="E1" && rank>0?12:10;
        const float expected=expectedLeptons*15.f*10/(256.f*LOGIC_FPS);
        std::printf("NATIVE_MOTION SPEED type=%s rank=%d distance=%.7f expected=%.7f\n",name,rank,distance,expected);
        check(std::abs(distance-expected)<.0001f,"actual Cmd movement uses source distance per second without inverted or integer-delay rounding");
        check(resume(motion,45),"part-cell source walking and its animation clock survive World47 save continuation");
      }
    }
    const int heroType=source.nativeUnitIndex("TANY");std::string stationary=source.nativeScenarioSource;
    const auto section=stationary.find("[Type."+std::to_string(heroType)+"]");const auto speed=stationary.find("\nOriginalSpeed =",section);
    bool stopped=false;
    if(section!=std::string::npos && speed!=std::string::npos) {
        stationary.replace(speed+1,stationary.find('\n',speed+1)-speed-1,"OriginalSpeed = 0");World zero=cleared;
        if(zero.restoreNativeDefinitions(stationary)){
            const EID id=zero.spawnNativeActor(heroType,0,lane.x+.5f,lane.y+.5f,256,0,"motion:stationary","","Guard");
            World::Cmd c;c.type=World::Cmd::Move;c.ids={id};c.x=lane.x+5.5f;c.y=lane.y+.5f;zero.applyCmd(0,c);
            for(int i=0;i<20;++i)zero.update();stopped=zero.valid(id) && zero.ents[id].x==lane.x+.5f && zero.ents[id].walkAnim==0;
        }
    }
    check(stopped,"an explicit source Speed=0 remains stationary without advancing the walking clock");
    // Explicit presentation fixture: poses are isolated here; natural campaign
    // screenshots are captured separately by nativePlaythroughRegression.
    world=cleared;world.map.bldOccRef=&world.bldOcc;clearPlacement();sel.clear();selBuilding=-1;interpAlpha=1;camZoom=1;
    sim=SimInput{};sim.active=true;sim.pos={-100,-100};auditHud=true;
    const auto tanya=world.spawnNativeActor(world.nativeUnitIndex("TANY"),0,water.x+.5f,water.y+.5f,256,0,"motion:swimmer","","Guard");
    sel={tanya};for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);syncFogKnowledge();bakeTerrain();
    Image sheet=GenImageColor(6*80,3*2*80,Color{16,24,32,255});bool poses=true,rects=true,isolated=true;
    for(int row=0;row<2;++row)for(int pose=0;pose<3;++pose){
        auto& e=world.ents[tanya];e.utype=row?UnitType::NavySEAL:UnitType::Tanya;e.dir=0;e.px=e.x;e.py=e.y;e.state=UState::Idle;
        const auto& info=g_sprites.animInfo(e.utype);std::set<uint32_t> fingerprints;
        for(int frame=0;frame<6;++frame){
            e.fireAnim=pose==2?g_gameRules.fireAnimationTicks-frame*g_gameRules.fireAnimationTicks/6:0;
            e.px=pose==0?e.x-.02f:e.x;e.walkAnim=frame*info.swimRate;world.tick=frame*info.treadRate;
            const auto* sprite=swimmingSprite(e,e.utype,world.players[0].colorId);
            const auto anim=pose==0?UAnim::Swim:pose==1?UAnim::Tread:UAnim::WetFire;
            const int expected=pose==2?std::clamp((g_gameRules.fireAnimationTicks-e.fireAnim)*info.wetFire/std::max(1,g_gameRules.fireAnimationTicks),0,info.wetFire-1):frame;
            poses &= sprite && sprite==&g_sprites.unitAnim(e.utype,anim,e.dir,expected,world.players[0].colorId);
            auto p=unitScreenPos(e);camX+=p.x-200;camY+=p.y-200;p=unitScreenPos(e);
            const auto r=unitScreenRect(e);rects &= sprite && std::abs(r.x-(p.x-sprite->ox+sprite->visL-4))<.01f
                && std::abs(r.height-(sprite->visH()+8))<.01f;
            const auto checksum=world.checksum();BeginTextureMode(canvas);ClearBackground(Color{16,24,32,255});drawEntities();EndTextureMode();isolated &= world.checksum()==checksum;
            Image full=LoadImageFromTexture(canvas.texture);ImageFlipVertical(&full);Image crop=ImageFromImage(full,{160,160,80,80});UnloadImage(full);
            Color* pixels=LoadImageColors(crop);uint32_t hash=2166136261u;for(int i=0;i<80*80;++i){hash=(hash^pixels[i].r)*16777619u;hash=(hash^pixels[i].g)*16777619u;hash=(hash^pixels[i].b)*16777619u;}
            fingerprints.insert(hash);UnloadImageColors(pixels);ImageDraw(&sheet,crop,{0,0,80,80},{(float)frame*80,(float)(row*3+pose)*80,80,80},WHITE);UnloadImage(crop);
        }
        std::printf("NATIVE_MOTION ART type=%s pose=%d distinct=%zu\n",unitAssetName(e.utype),pose,fingerprints.size());poses &= fingerprints.size()>=4;
    }
    const auto image=(std::filesystem::path(outputDir)/"water-pose-fixture.png").string();ExportImage(sheet,image.c_str());UnloadImage(sheet);
    check(poses && rects && isolated,"actual water poses animate, share their visible selection bounds, and leave simulation unchanged");
    auto& e=world.ents[tanya];e.navLayer=1;check(!swimmingSprite(e,e.utype,0),"a swimmer on a bridge deck keeps the land pose above water");e.navLayer=0;
    e.x=lane.x+.5f;e.y=lane.y+.5f;check(!swimmingSprite(e,e.utype,0),"the same infantry uses land poses after leaving water");
    std::printf("NATIVE_MOTION failures=%d\n",failures);return failures;
}
