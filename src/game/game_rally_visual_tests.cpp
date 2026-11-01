#include "game/game.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

int Game::rallyVisualRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;const int languageBefore=g_lang;
    SetTargetFPS(0);shotAtDisplaySize=true;
    for(int language=0;language<2;++language) for(int scenario=0;scenario<8;++scenario) {
        g_lang=language;world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<world.ents.size();++i) if(world.ents[i].alive) world.kill((EID)i,false);
        for(auto& cell:world.map.cells) cell=Cell{};
        for(auto& player:world.players) { player.defeated=false;player.money=0; }
        world.effects.clear();world.projs.clear();world.presentationEvents.clear();world.cratesEnabled=false;
        localPlayer=0;phase=Phase::InGame;sim=SimInput{};campaignMission=-1;netGame=false;sel.clear();
        const EID factory=world.spawnBuilding(scenario==6?1:0,BldType::WarFactory,10,10,true);
        const int x=25,y=20;EID hidden=INVALID_EID;
        const bool fog=scenario==2 || scenario==3;
        if(scenario==1 || scenario==3) for(int ty=y-2;ty<=y+2;++ty) for(int tx=x-2;tx<=x+2;++tx) world.map.at(tx,ty).height=3;
        for(auto& layer:world.map.fog) std::fill(layer.begin(),layer.end(),fog?FOG_UNSEEN:FOG_VISIBLE);
        if(fog) {
            for(int ty=7;ty<=15;++ty) for(int tx=7;tx<=15;++tx) world.map.fog[0][ty*64+tx]=FOG_VISIBLE;
            hidden=world.spawnUnit(1,UnitType::Rhino,x+0.5f,y+0.5f);
        }
        World::Cmd rally{};rally.type=World::Cmd::SetRally;rally.ids={factory};rally.x=(float)x;rally.y=(float)y;
        world.applyCmd(scenario==6?1:0,rally);consumePresentationEvents();selBuilding=scenario==4?INVALID_EID:factory;
        if(scenario==5) { rally.a=1;world.applyCmd(0,rally);consumePresentationEvents(); }
        if(scenario==7) world.kill(factory,false);
        moveFeedbackTimer=0;interpAlpha=1;camZoom=scenario==1 || scenario==3?1.5f:1;
        camX=(x-y)*32-700/camZoom;camY=(x+y+1)*16-420/camZoom;
        bakeTerrain();minimapTimer=0;
        // A synthetic entity at the rally tile uses the independent unit
        // projection, including terrain height, to locate the expected flag.
        World::Ent anchor;anchor.x=anchor.px=x+0.5f;anchor.y=anchor.py=y+0.5f;
        const auto p=unitScreenPos(anchor);const auto hash=world.checksum();
        const bool expect=scenario<4;
        for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();const auto dst=letterboxDst();
            const auto stem="case"+std::to_string(scenario)+"_lang"+std::to_string(language)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y);
            auto capture=[&](const std::string& suffix) {
                const auto path=(std::filesystem::path(outputDir)/(stem+suffix+".png")).string();shotFile=path;render();return LoadImage(path.c_str());
            };
            Image selected=capture("");
            const int oldX=world.ents[factory].rallyX,oldY=world.ents[factory].rallyY;
            world.ents[factory].rallyX=world.ents[factory].rallyY=-1;
            Image absent=capture("_without");world.ents[factory].rallyX=oldX;world.ents[factory].rallyY=oldY;
            const int px=(int)(dst.x+p.x*camZoom*dst.width/SCREEN_W),py=(int)(dst.y+p.y*camZoom*dst.height/SCREEN_H);
            const float scale=camZoom*dst.width/SCREEN_W;
            const int radius=(int)(28*scale);int green=0,face=0;bool dimensions=selected.width==size.x && selected.height==size.y && absent.width==size.x && absent.height==size.y;
            if(dimensions) for(int ty=std::max(0,py-radius);ty<std::min(size.y,py+radius+1);++ty)
                for(int tx=std::max(0,px-radius);tx<std::min(size.x,px+radius+1);++tx) {
                    const auto a=GetImageColor(selected,tx,ty),b=GetImageColor(absent,tx,ty);
                    const bool tinted=abs(a.r-b.r)+abs(a.g-b.g)+abs(a.b-b.b)>30 && a.g>120 && a.g>a.r*1.4f;
                    green+=tinted;
                    const float dx=(tx-px)/scale,dy=(ty-py)/scale;
                    face+=tinted && dx>=3 && dx<=10 && dy>=-14 && dy<=-7;
                }
            bool privacy=true;int terrainLeaks=0;
            if(fog) {
                world.ents[hidden].alive=false;Image noEnemy=capture("_no_enemy");world.ents[hidden].alive=true;
                privacy=dimensions && noEnemy.width==size.x && noEnemy.height==size.y;
                if(privacy) for(int ty=std::max(0,py-radius);ty<std::min(size.y,py+radius+1);++ty)
                    for(int tx=std::max(0,px-radius);tx<std::min(size.x,px+radius+1);++tx) {
                        const auto a=GetImageColor(selected,tx,ty),b=GetImageColor(noEnemy,tx,ty);
                        privacy &= a.r==b.r && a.g==b.g && a.b==b.b;
                    }
                UnloadImage(noEnemy);
                privacy &= world.map.fogAt(0,x,y)==FOG_UNSEEN;
                if(dimensions) for(int ty=std::max(0,py+(int)(24*scale));ty<std::min(size.y,py+(int)(110*scale));++ty)
                    for(int tx=std::max(0,px-(int)(130*scale));tx<std::min(size.x,px+(int)(130*scale));++tx) {
                        const auto pixel=GetImageColor(absent,tx,ty);
                        terrainLeaks+=pixel.r>4 || pixel.g>4 || pixel.b>4;
                    }
                privacy &= terrainLeaks==0;
            }
            UnloadImage(selected);UnloadImage(absent);
            bool ok=dimensions && (expect?green>=8 && face>=3:green==0) && privacy && hash==world.checksum();failures+=!ok;
            std::printf("RALLY_VISUAL %s scenario=%d language=%d size=%dx%d green=%d face=%d expected=%d privacy=%d terrain_leaks=%d simulation_unchanged=%d\n",
                ok?"PASS":"FAIL",scenario,language,size.x,size.y,green,face,expect,privacy,terrainLeaks,hash==world.checksum());
        }
    }
    g_lang=languageBefore;shotAtDisplaySize=false;SetTargetFPS(60);return failures;
}
