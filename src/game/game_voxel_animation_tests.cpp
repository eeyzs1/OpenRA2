#include "game/game.h"
#include "gfx/sprites.h"
#include "gfx/vxl.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

int Game::voxelAnimationRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    int failures=0;
    auto check=[&](bool ok,const char* name){failures+=!ok;std::printf("VOXEL_ANIMATION %s %s\n",ok?"PASS":"FAIL",name);};
    auto hash=[](const Sprite& sprite) {
        Image image=LoadImageFromTexture(sprite.tex);Color* pixels=LoadImageColors(image);uint32_t result=2166136261u;
        for(int i=0;pixels && i<image.width*image.height;++i)for(uint8_t c:{pixels[i].r,pixels[i].g,pixels[i].b,pixels[i].a})result=(result^c)*16777619u;
        if(pixels)UnloadImageColors(pixels);UnloadImage(image);return result;
    };
    Image sheet=GenImageColor(1536,576,Color{22,28,28,255});
    for(int row=0;row<3;++row) {
        const UnitType type=row==0?UnitType::Nighthawk:row==1?UnitType::SiegeChopper:UnitType::GatlingTank;
        check((row<2?g_sprites.unitBodyFrames(type):g_sprites.unitTurretFrames(type))==2,"native two-frame animation declared");
        for(int direction=0;direction<8;++direction) {
            auto sprite=[&](int frame,int player)->const Sprite& {
                return row<2?g_sprites.unitBody(type,direction,frame,player):g_sprites.unitTurret(type,direction,player,frame);
            };
            const auto& first=sprite(0,0);const auto& next=sprite(1,0);
            bool ok=hash(first)!=hash(next) && hash(first)==hash(sprite(2,0)) && hash(next)==hash(sprite(3,0))
                && first.tex.width==next.tex.width && first.tex.height==next.tex.height && first.ox==next.ox && first.oy==next.oy
                && next.visL>0 && next.visT>0 && next.visR<next.tex.width-1 && next.visB<next.tex.height-1
                && hash(next)!=hash(sprite(1,1));
            failures+=!ok;std::printf("VOXEL_VARIANT %s type=%d direction=%d first=%08x next=%08x anchor=%d,%d size=%dx%d\n",
                ok?"PASS":"FAIL",(int)type,direction,hash(first),hash(next),first.ox,first.oy,first.tex.width,first.tex.height);
            for(int frame=0;frame<2;++frame) {
                const auto& pose=sprite(frame,0);Image image=LoadImageFromTexture(pose.tex);
                ImageDraw(&sheet,image,{0,0,(float)image.width,(float)image.height},
                    {(float)(direction*192+96-pose.ox),(float)(row*192+frame*88+64-pose.oy),(float)image.width,(float)image.height},WHITE);
                UnloadImage(image);
            }
        }
    }
    ExportImage(sheet,(std::filesystem::path(outputDir)/"native_frames.png").string().c_str());UnloadImage(sheet);
    check(hash(g_sprites.unitBody(UnitType::Rhino,0,0,0))==hash(g_sprites.unitBody(UnitType::Rhino,0,19,0)),"single-frame vehicle stays static");
    check(hash(g_sprites.unitBody(UnitType::WarMiner,0,0,0))!=hash(g_sprites.unitBody(UnitType::WarMiner,0,1,0)),"miner load appearance remains separate from animation");
    check(hash(g_sprites.unitBody(UnitType::GI,0,0,0))!=hash(g_sprites.unitBody(UnitType::GI,0,1,0)),"SHP walk frame remains available");

    world.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"",true);
    for(int pass=0;pass<2;++pass)for(size_t id=0;id<world.ents.size();++id)if(world.ents[id].alive)world.kill((EID)id,false);
    for(auto& cell:world.map.cells)cell=Cell{};
    for(auto& player:world.players){player.money=0;player.defeated=false;}
    world.cratesEnabled=false;world.effects.clear();world.projs.clear();world.presentationEvents.clear();
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
    localPlayer=0;netGame=false;phase=Phase::InGame;showMenu=false;gameOver=false;campaignMission=-1;
    sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;
    sel.clear();selBuilding=INVALID_EID;clearPlacement();msg.clear();msgTimer=0;moveFeedbackTimer=0;
    camX=-550;camY=306;fogMaskTick=-1;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
    for(UnitType type:{UnitType::Nighthawk,UnitType::SiegeChopper,UnitType::GatlingTank}) {
        EID actor=world.spawnUnit(0,type,20.5f,20.5f);world.ents[actor].dir=0;world.ents[actor].turretDir=0;
        for(int active=0;active<2;++active)for(int frame=0;frame<2;++frame)for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            world.tick=frame*2;
            if(type==UnitType::GatlingTank)world.ents[actor].fireAnim=active?g_gameRules.fireAnimationTicks:0;
            else world.ents[actor].state=active?UState::Idle:UState::Landed;
            const int expectedPhase=active?frame:0;
            const auto& expected=type==UnitType::GatlingTank?g_sprites.unitTurret(type,0,0,expectedPhase):g_sprites.unitBody(type,0,expectedPhase,0);
            SetWindowSize(size.x,size.y);const auto before=world.checksum();
            const auto path=(std::filesystem::path(outputDir)/(std::to_string((int)type)+"_active"+std::to_string(active)+"_frame"+std::to_string(frame)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();
            shotFile=path;render();Image actual=LoadImage(path.c_str()),source=LoadImageFromTexture(expected.tex);
            const auto& staticPose=type==UnitType::GatlingTank?g_sprites.unitTurret(type,0,0,0):g_sprites.unitBody(type,0,0,0);
            Image rest=LoadImageFromTexture(staticPose.tex);Color* restPixels=LoadImageColors(rest);
            Color* pixels=LoadImageColors(actual);Color* reference=LoadImageColors(source);int samples=0,hits=0,sensitive=0,sensitiveHits=0;
            Vector2 at=unitScreenPos(world.ents[actor]);at.y-=unitRenderLift(world.ents[actor]);const auto viewport=letterboxDst();
            for(int y=0;reference && pixels && y<source.height;++y)for(int x=0;x<source.width;++x) {
                const Color color=reference[y*source.width+x];if(color.a<245)continue;
                const int sx=(int)(viewport.x+(at.x-expected.ox+x+0.5f)*viewport.width/SCREEN_W);
                const int sy=(int)(viewport.y+(at.y-expected.oy+y+0.5f)*viewport.height/SCREEN_H);
                if(sx<0 || sy<0 || sx>=actual.width || sy>=actual.height)continue;
                const Color drawn=pixels[sy*actual.width+sx];++samples;
                const bool matches=std::abs((int)drawn.r-color.r)<24 && std::abs((int)drawn.g-color.g)<24 && std::abs((int)drawn.b-color.b)<24;
                hits+=matches;
                if(expectedPhase && restPixels && rest.width==source.width && rest.height==source.height) {
                    const Color old=restPixels[y*rest.width+x];
                    if(old.a<245 || std::abs((int)old.r-color.r)>30 || std::abs((int)old.g-color.g)>30 || std::abs((int)old.b-color.b)>30) {
                        ++sensitive;sensitiveHits+=matches;
                    }
                }
            }
            const Rectangle box=unitScreenRect(world.ents[actor]);
            const bool picked=pickUnit((int)(box.x+box.width/2),(int)(box.y+box.height/2))==actor;
            const bool phaseVisible=!expectedPhase || (sensitive>10 && sensitiveHits*100>=sensitive*85);
            const bool ok=samples>20 && hits*100>=samples*85 && phaseVisible && picked && before==world.checksum();failures+=!ok;
            std::printf("VOXEL_FRAME %s type=%d active=%d frame=%d size=%dx%d samples=%d hits=%d sensitive=%d sensitive_hits=%d picked=%d isolated=%d\n",
                ok?"PASS":"FAIL",(int)type,active,frame,size.x,size.y,samples,hits,sensitive,sensitiveHits,picked,before==world.checksum());
            if(restPixels)UnloadImageColors(restPixels);UnloadImage(rest);
            if(pixels)UnloadImageColors(pixels);if(reference)UnloadImageColors(reference);UnloadImage(actual);UnloadImage(source);
        }
        world.kill(actor,false);
    }
    check(g_sprites.assetsOk(),"native voxel animations have no missing assets");
    return failures;
}
