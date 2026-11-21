#include "game_regression.h"
#include "game/lang.h"
#include "gfx/sprites.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>

int GameRegression::chopperTransitionRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* name){failures+=!ok;std::printf("CHOPPER_TRANSITION %s %s\n",ok?"PASS":"FAIL",name);};
    const auto rules=g_gameRules;const int language=g_lang;
    struct Restore { GameRules rules;int language;~Restore(){g_gameRules=rules;g_lang=language;} } restore{rules,language};
    auto fixture=[&] {
        world.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"",true);
        for(int pass=0;pass<2;++pass)for(size_t id=0;id<world.ents.size();++id)if(world.ents[id].alive)world.kill((EID)id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        for(auto& player:world.players){player.money=0;player.defeated=false;}
        world.cratesEnabled=false;world.effects.clear();world.projs.clear();world.presentationEvents.clear();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        localPlayer=0;netGame=false;phase=Phase::InGame;showMenu=false;gameOver=false;campaignMission=-1;
        sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;
        sel.clear();selBuilding=INVALID_EID;clearPlacement();msg.clear();msgTimer=0;moveFeedbackTimer=0;
        camX=-550;camY=306;fogMaskTick=-1;shotAtDisplaySize=true;SetTargetFPS(0);
        return world.spawnUnit(0,UnitType::SiegeChopper,20.5f,20.5f);
    };
    auto toggle=[&](EID id){World::Cmd c{};c.type=World::Cmd::Deploy;c.ids={id};world.applyCmd(world.ents[id].player,c);};
    auto snapshot=[&](World& replay) {
        FILE* file=tmpfile();bool ok=file && world.saveGame(file);if(ok){rewind(file);ok=replay.loadGame(file);}if(file)fclose(file);return ok;
    };
    auto hashSprite=[](const Sprite& sprite) {
        Image image=LoadImageFromTexture(sprite.tex);Color* colors=LoadImageColors(image);uint32_t hash=2166136261u;
        for(int pixel=0;colors && pixel<image.width*image.height;++pixel)for(uint8_t value:{colors[pixel].r,colors[pixel].g,colors[pixel].b,colors[pixel].a})hash=(hash^value)*16777619u;
        if(colors)UnloadImageColors(colors);UnloadImage(image);return hash;
    };
    std::set<uint32_t> frames;bool colored=true,bounded=true;
    Image sheet=GenImageColor(224*11,448,Color{22,28,28,255});
    for(int frame=0;frame<g_sprites.chopperTransformFrames();++frame) {
        const auto& sprite=g_sprites.chopperTransform(frame,0);frames.insert(hashSprite(sprite));
        colored &= hashSprite(sprite)!=hashSprite(g_sprites.chopperTransform(frame,1));
        bounded &= sprite.visL>0 && sprite.visT>0 && sprite.visR<sprite.tex.width-1 && sprite.visB<sprite.tex.height-1;
        for(int row=0;row<2;++row) {
            const auto& pose=g_sprites.chopperTransform(row?g_sprites.chopperTransformFrames()-1-frame:frame,row);
            Image image=LoadImageFromTexture(pose.tex);ImageDraw(&sheet,image,{0,0,(float)image.width,(float)image.height},
                {(float)(frame*224+112-pose.ox),(float)(row*224+112-pose.oy),(float)image.width,(float)image.height},WHITE);UnloadImage(image);
        }
    }
    ExportImage(sheet,(std::filesystem::path(outputDir)/"native_sequence.png").string().c_str());UnloadImage(sheet);
    check(frames.size()==11 && colored && bounded,"eleven distinct original poses, house colors and unclipped paired shadows");
    for(int fast=0;fast<2;++fast) {
        g_gameRules.chopperLandingTicks=fast?1:rules.chopperLandingTicks;
        g_gameRules.chopperDeployTicks=fast?1:rules.chopperDeployTicks;
        EID actor=fixture();EID target=world.spawnUnit(1,UnitType::MCV,24.5f,20.5f);world.ents[target].hp=10000;
        toggle(actor);consumePresentationEvents();
        check(msg==TextFormat(TR(S::MsgDeployStarting),1) && world.ents[actor].dir==dirFromVec(1,0),"accepted command reports starting and adopts original east deployment facing");
        bool phases=true,locked=true,save=true,frozen=true,quiet=true;
        for(int stage=0;stage<4;++stage) {
            if(stage==2)toggle(actor);
            const UState expected=stage==0?UState::DeployLanding:stage==1?UState::DeployUnfolding:stage==2?UState::DeployFolding:UState::DeployTakeoff;
            const int duration=stage==0 || stage==3?g_gameRules.chopperLandingTicks:g_gameRules.chopperDeployTicks;
            World replay;bool equal=snapshot(replay);save &= equal && replay.checksum()==world.checksum();
            const int originalTimer=world.ents[actor].deployAnim;
            world.orderStop({actor});const auto rejected=world.orderDeployment({actor},false);
            locked &= world.ents[actor].state==expected && world.ents[actor].deployAnim==originalTimer
                && rejected.accepted==0 && rejected.reason==DeployRejectReason::Busy
                && world.orderMove({actor},25.5f,25.5f,false).reason==MoveRejectReason::Busy
                && world.attackRejection(actor,target)==AttackRejectReason::Busy;
            world.ents[actor].tpSick=replay.ents[actor].tpSick=3;
            for(int tick=0;tick<3;++tick){world.update();replay.update();frozen &= world.ents[actor].deployAnim==originalTimer && world.checksum()==replay.checksum();}
            const auto damageBefore=world.combatDamage[0];
            for(int tick=0;tick<duration;++tick) {
                const auto& unit=world.ents[actor];const bool air=stage==0 || stage==3;
                phases &= unit.state==expected && unit.deployAnim==duration-tick && unit.airborne()==air
                    && (unit.airBucket>=0)==air && (unit.occCell>=0)==(stage!=3)
                    && world.cellHardBlockedForMove(20,20,target)==(stage!=3);
                const bool animation=stage==1 || stage==2;
                if(!fast && (animation?(tick==duration/4 || tick==duration*3/4):tick==duration/2)) {
                    save &= snapshot(replay) && replay.checksum()==world.checksum();
                    bakeTerrain();
                    for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
                        SetWindowSize(size.x,size.y);const auto before=world.checksum();
                        const auto path=(std::filesystem::path(outputDir)/("stage"+std::to_string(stage)+"_work"+std::to_string(tick)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();shotFile=path;render();
                        phases &= before==world.checksum();
                        if(animation) {
                            const int count=g_sprites.chopperTransformFrames();const int frame=stage==1?tick*count/duration:count-1-tick*count/duration;
                            const auto& expectedSprite=g_sprites.chopperTransform(frame,0);
                            Image actual=LoadImage(path.c_str()),source=LoadImageFromTexture(expectedSprite.tex);
                            Color* actualPixels=LoadImageColors(actual);Color* expectedPixels=LoadImageColors(source);int samples=0,hits=0;
                            Vector2 at=unitScreenPos(unit);at.y-=unitRenderLift(unit);const auto viewport=letterboxDst();
                            for(int y=0;actualPixels && expectedPixels && y<source.height;++y)for(int x=0;x<source.width;++x) {
                                const Color color=expectedPixels[y*source.width+x];if(color.a<245)continue;
                                const int px=(int)(viewport.x+(at.x-expectedSprite.ox+x+.5f)*viewport.width/SCREEN_W);
                                const int py=(int)(viewport.y+(at.y-expectedSprite.oy+y+.5f)*viewport.height/SCREEN_H);
                                if(px<0 || py<0 || px>=actual.width || py>=actual.height)continue;
                                const Color drawn=actualPixels[py*actual.width+px];++samples;
                                hits+=std::abs((int)drawn.r-color.r)<36 && std::abs((int)drawn.g-color.g)<36 && std::abs((int)drawn.b-color.b)<36;
                            }
                            const Rectangle box=unitScreenRect(unit);
                            const bool contained=box.x<=at.x-expectedSprite.ox+expectedSprite.visL && box.y<=at.y-expectedSprite.oy+expectedSprite.visT
                                && box.x+box.width>=at.x-expectedSprite.ox+expectedSprite.visR && box.y+box.height>=at.y-expectedSprite.oy+expectedSprite.visB;
                            const bool picked=pickUnit((int)(box.x+box.width/2),(int)(box.y+box.height/2))==actor;
                            const bool correct=chopperTransformFrame(unit)==frame && samples>100 && hits*100>=samples*70 && contained && picked && actual.width==size.x && actual.height==size.y;
                            phases &= correct;
                            std::printf("CHOPPER_ANIMATION %s stage=%d work=%d frame=%d size=%dx%d pixels=%d hits=%d contained=%d picked=%d\n",correct?"PASS":"FAIL",stage,tick,frame,size.x,size.y,samples,hits,contained,picked);
                            if(actualPixels)UnloadImageColors(actualPixels);if(expectedPixels)UnloadImageColors(expectedPixels);UnloadImage(actual);UnloadImage(source);
                        }
                    }
                }
                world.update();replay.update();save &= equal && world.checksum()==replay.checksum();
                quiet &= world.ents[actor].fireAnim==0;
            }
            if(stage<2)quiet &= world.combatDamage[0]==damageBefore;
            if(stage==1) {
                consumePresentationEvents();check(msg==TextFormat(TR(S::MsgDeployDone),1),"completion receipt follows unfolding");
                for(int tick=0;tick<180;++tick)world.update();
                check(world.combatDamage[0]>0,"completed deployment permits actual artillery damage");
            }
        }
        check(phases,"four explicit phases preserve ground reservation and air target domain");
        check(locked && frozen,"stop, movement, attack and duplicate deployment preserve phases; freeze pauses work");
        check(save && quiet,"every phase saves and replays identically without firing during transition");
        check(!world.ents[actor].deploying() && world.ents[actor].airborne() && world.ents[actor].occCell<0,"takeoff completes and releases ground occupancy");
    }
    g_gameRules=rules;
    for(int stage=0;stage<4;++stage) {
        EID actor=fixture();toggle(actor);
        if(stage>=1)for(int tick=0;tick<rules.chopperLandingTicks;++tick)world.update();
        if(stage>=2){for(int tick=0;tick<rules.chopperDeployTicks;++tick)world.update();toggle(actor);}
        if(stage>=3)for(int tick=0;tick<rules.chopperDeployTicks;++tick)world.update();
        world.kill(actor,false);check(world.ents[actor].occCell<0 && world.ents[actor].airBucket<0,"destruction releases both indexes in each transition phase");
    }
    {
        EID actor=fixture(),second=world.spawnUnit(0,UnitType::SiegeChopper,20.5f,20.5f);
        const auto result=world.orderDeployment({actor,second},false);
        check(result.accepted==1 && result.rejected==1 && result.reason==DeployRejectReason::Blocked
            && world.ents[actor].deploying() && !world.ents[second].deploying(),"same-cell multi-selection reserves one landing slot");
        world.map.at(20,20).overlay=Overlay::Rock1;
        for(int tick=0;tick<rules.chopperLandingTicks;++tick)world.update();
        check(world.ents[actor].state==UState::DeployTakeoff && !world.ents[actor].deployed && world.ents[actor].occCell<0,"terrain changed during descent aborts deployment and takes off");
    }
    {
        EID actor=fixture();toggle(actor);for(int tick=0;tick<rules.chopperLandingTicks;++tick)world.update();
        EID controller=world.spawnUnit(1,UnitType::Yuri,18.5f,20.5f);
        world.orderAttack({controller},actor);
        for(int tick=0;tick<10 && world.ents[actor].player==0;++tick)world.update();
        const int timer=world.ents[actor].deployAnim;
        const bool taken=world.ents[actor].player==1 && world.ents[actor].state==UState::DeployUnfolding;
        world.kill(controller,false);
        check(taken && world.ents[actor].player==0 && world.ents[actor].deployAnim==timer && world.ents[actor].state==UState::DeployUnfolding,"mind control and release preserve deployment work");
        World replay;
        world.ents[actor].deployAnim=0;check(!snapshot(replay),"zero remaining work in a transition is rejected by snapshot validation");
        world.ents[actor].deployAnim=timer;world.ents[actor].deployed=false;check(!snapshot(replay),"inconsistent transition target domain is rejected by snapshot validation");
    }
    return failures;
}
