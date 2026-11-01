#include "game/game.h"
#include "gfx/sprites.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <set>

int Game::siegePoseRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    int failures=0;
    auto check=[&](bool ok,const char* name){failures+=!ok;std::printf("SIEGE_POSE %s %s\n",ok?"PASS":"FAIL",name);};
    auto hashSprite=[](const Sprite& sprite) {
        Image image=LoadImageFromTexture(sprite.tex);Color* pixels=LoadImageColors(image);
        uint32_t hash=2166136261u;
        for(int i=0;pixels && i<image.width*image.height;++i)for(uint8_t value:{pixels[i].r,pixels[i].g,pixels[i].b,pixels[i].a})hash=(hash^value)*16777619u;
        if(pixels)UnloadImageColors(pixels);UnloadImage(image);return hash;
    };
    const auto flightBefore=hashSprite(g_sprites.unitBody(UnitType::SiegeChopper,0,0,0));
    Image sheet=GenImageColor(1792,896,Color{22,28,28,255});
    auto sheetSprite=[&](const Sprite& sprite,int col,int row) {
        Image source=LoadImageFromTexture(sprite.tex);
        ImageDraw(&sheet,source,{0,0,(float)source.width,(float)source.height},
            {(float)(col*224+112-sprite.ox),(float)(row*224+164-sprite.oy),(float)source.width,(float)source.height},WHITE);
        UnloadImage(source);
    };
    bool variants=true,colored=true,bounded=true;
    for(int body=0;body<8;++body) {
        std::set<uint32_t> directions;
        sheetSprite(g_sprites.unitBody(UnitType::SiegeChopper,body,0,0),body,0);
        for(int turret=0;turret<8;++turret) {
            const auto& gold=g_sprites.unitDeployed(UnitType::SiegeChopper,body,turret,0);
            const auto& red=g_sprites.unitDeployed(UnitType::SiegeChopper,body,turret,1);
            directions.insert(hashSprite(gold));colored &= hashSprite(gold)!=hashSprite(red);
            bounded &= gold.valid() && gold.visL>0 && gold.visT>0 && gold.visR<gold.tex.width-1 && gold.visB<gold.tex.height-1
                && gold.ox>=0 && gold.ox<gold.tex.width && gold.oy>=0 && gold.oy<gold.tex.height;
            if(!(gold.visL>0 && gold.visT>0 && gold.visR<gold.tex.width-1 && gold.visB<gold.tex.height-1))
                std::printf("SIEGE_CLIP body=%d turret=%d size=%d,%d anchor=%d,%d vis=%d,%d,%d,%d\n",body,turret,gold.tex.width,gold.tex.height,gold.ox,gold.oy,gold.visL,gold.visT,gold.visR,gold.visB);
            if(body==0 || body==2 || body==5)sheetSprite(gold,turret,body==0?1:body==2?2:3);
        }
        variants &= directions.size()==8;
    }
    ExportImage(sheet,(std::filesystem::path(outputDir)/"pose_matrix.png").string().c_str());UnloadImage(sheet);
    std::printf("SIEGE_ASSET variants=%d colored=%d bounded=%d missing=%d\n",variants,colored,bounded,g_sprites.missingCount());
    check(variants && colored && bounded && g_sprites.assetsOk(),"eight turret directions, house remap, complete sprite bounds");
    check(flightBefore==hashSprite(g_sprites.unitBody(UnitType::SiegeChopper,0,0,0)),"ground pose cache leaves flying body unchanged");

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
    EID chopper=world.spawnUnit(0,UnitType::SiegeChopper,20.5f,20.5f);
    World::Cmd deploy{};deploy.type=World::Cmd::Deploy;deploy.ids={chopper};world.applyCmd(0,deploy);
    for(int tick=0;tick<g_gameRules.chopperLandingTicks+g_gameRules.chopperDeployTicks;++tick)world.update();
    check(world.ents[chopper].deployed && !world.ents[chopper].airborne(),"actual deploy command creates a grounded actor");
    bakeTerrain();
    auto capture=[&](const char* name,const Sprite& expected,Vec2i size) {
        SetWindowSize(size.x,size.y);sel.clear();
        auto path=(std::filesystem::path(outputDir)/(std::string(name)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();
        const auto checksum=world.checksum();shotFile=path;render();
        Image actual=LoadImage(path.c_str()),source=LoadImageFromTexture(expected.tex);
        Color* pixels=LoadImageColors(actual);Color* reference=LoadImageColors(source);
        Vector2 at=unitScreenPos(world.ents[chopper]);at.y-=unitRenderLift(world.ents[chopper]);
        const auto viewport=letterboxDst();int samples=0,hits=0;
        for(int y=0;reference && pixels && y<source.height;++y)for(int x=0;x<source.width;++x) {
            Color color=reference[y*source.width+x];if(color.a<245)continue;
            float lx=(at.x-expected.ox+x+0.5f)*camZoom,ly=(at.y-expected.oy+y+0.5f)*camZoom;
            const int sx=(int)(viewport.x+lx*viewport.width/SCREEN_W),sy=(int)(viewport.y+ly*viewport.height/SCREEN_H);
            if(sx<0 || sy<0 || sx>=actual.width || sy>=actual.height)continue;
            Color drawn=pixels[sy*actual.width+sx];++samples;
            hits+=std::abs((int)drawn.r-color.r)<36 && std::abs((int)drawn.g-color.g)<36 && std::abs((int)drawn.b-color.b)<36;
        }
        const Rectangle box=unitScreenRect(world.ents[chopper]);
        const bool contains=box.x<=at.x-expected.ox+expected.visL && box.y<=at.y-expected.oy+expected.visT
            && box.x+box.width>=at.x-expected.ox+expected.visR && box.y+box.height>=at.y-expected.oy+expected.visB;
        const bool picked=pickUnit((int)((box.x+box.width/2)*camZoom),(int)((box.y+box.height/2)*camZoom))==chopper;
        bool ok=samples>100 && hits*100>=samples*70 && contains && picked && checksum==world.checksum();failures+=!ok;
        std::printf("SIEGE_FRAME %s name=%s size=%dx%d samples=%d hits=%d contains=%d picked=%d isolated=%d\n",ok?"PASS":"FAIL",name,size.x,size.y,samples,hits,contains,picked,checksum==world.checksum());
        if(std::string(name)=="zoomed" || std::string(name)=="firing") {
            sel={chopper};const auto selectedPath=(std::filesystem::path(outputDir)/("selected_"+std::filesystem::path(path).filename().string())).string();
            shotFile=selectedPath;render();Image selected=LoadImage(selectedPath.c_str());Color* chosen=LoadImageColors(selected);int changed=0;
            const int left=std::max(0,(int)(viewport.x+(box.x-20)*camZoom*viewport.width/SCREEN_W));
            const int top=std::max(0,(int)(viewport.y+(box.y-20)*camZoom*viewport.height/SCREEN_H));
            const int right=std::min(actual.width,(int)(viewport.x+(box.x+box.width+20)*camZoom*viewport.width/SCREEN_W));
            const int bottom=std::min(actual.height,(int)(viewport.y+(box.y+box.height+20)*camZoom*viewport.height/SCREEN_H));
            if(chosen && pixels && selected.width==actual.width && selected.height==actual.height)
                for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
                    const auto a=pixels[y*actual.width+x],b=chosen[y*selected.width+x];changed+=a.r!=b.r || a.g!=b.g || a.b!=b.b;
                }
            const bool marked=changed>16 && checksum==world.checksum();failures+=!marked;
            std::printf("SIEGE_SELECTION %s name=%s size=%dx%d zoom=%.1f changed=%d isolated=%d\n",marked?"PASS":"FAIL",name,size.x,size.y,camZoom,changed,checksum==world.checksum());
            if(chosen)UnloadImageColors(chosen);UnloadImage(selected);sel.clear();
        }
        if(pixels)UnloadImageColors(pixels);if(reference)UnloadImageColors(reference);UnloadImage(actual);UnloadImage(source);
    };
    for(int direction=0;direction<8;++direction) {
        world.ents[chopper].dir=direction;world.ents[chopper].turretDir=(direction+2)&7;
        for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            const auto name="deployed_body"+std::to_string(direction)+"_turret"+std::to_string((direction+2)&7);
            capture(name.c_str(),g_sprites.unitDeployed(UnitType::SiegeChopper,direction,(direction+2)&7,0),size);
        }
    }
    world.ents[chopper].dir=3;world.ents[chopper].turretDir=5;camZoom=1.5f;camX=-550/camZoom;camY=656-350/camZoom;
    for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}})capture("zoomed",g_sprites.unitDeployed(UnitType::SiegeChopper,3,5,0),size);
    camZoom=1;camX=-550;camY=306;
    world.ents[chopper].dir=7;world.ents[chopper].turretDir=7;
    EID target=world.spawnUnit(1,UnitType::MCV,24.5f,20.5f);world.ents[target].hp=10000;
    World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={chopper};attack.a=target;world.applyCmd(0,attack);
    World replay=world;replay.map.bldOccRef=&replay.bldOcc;
    bool heldBody=true,aimed=false,equal=true,saved=false,fired=false;const int heading=world.ents[chopper].dir;
    for(int frame=0;frame<600;++frame) {
        world.update();replay.update();heldBody &= world.ents[chopper].dir==heading;
        aimed |= world.ents[chopper].turretDir!=heading;equal &= world.checksum()==replay.checksum();
        if(frame==30){FILE* file=tmpfile();equal &= file && world.saveGame(file);if(file){rewind(file);equal &= replay.loadGame(file);fclose(file);}saved=true;}
        if(!fired && world.ents[chopper].fireAnim>0) {
            fired=true;
            for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}})
                capture("firing",g_sprites.unitDeployed(UnitType::SiegeChopper,world.ents[chopper].dir,world.ents[chopper].turretDir,0),size);
        }
        world.presentationEvents.clear();replay.presentationEvents.clear();world.evaQueue.clear();replay.evaQueue.clear();
    }
    check(heldBody && aimed && fired && world.combatDamage[0]>0 && equal && saved,"real artillery fire turns turret while hull remains fixed; saved replay agrees");
    world.orderStop({chopper});world.applyCmd(0,deploy);
    for(int tick=0;tick<g_gameRules.chopperLandingTicks+g_gameRules.chopperDeployTicks;++tick)world.update();
    check(!world.ents[chopper].deployed && world.ents[chopper].airborne(),"actual undeploy restores airborne actor");
    for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}})
        capture("undeployed",g_sprites.unitBody(UnitType::SiegeChopper,world.ents[chopper].dir,0,0),size);
    return failures;
}
