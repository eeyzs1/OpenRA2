#include "game_regression.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>

int GameRegression::feedbackVisualRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;const int savedLanguage=g_lang;
    SetTargetFPS(0);shotAtDisplaySize=true;auditHud=true;
    for(int language=0;language<2;++language) for(int scenario=0;scenario<6;++scenario) {
        g_lang=language;world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<world.ents.size();++i) if(world.ents[i].alive) world.kill((EID)i,false);
        for(auto& cell:world.map.cells) cell=Cell{};
        for(auto& player:world.players) { player.defeated=false;player.money=0; }
        world.cratesEnabled=false;world.effects.clear();world.projs.clear();world.presentationEvents.clear();
        localPlayer=0;netGame=false;pendingCmds.clear();phase=Phase::InGame;sim.active=false;campaignMission=-1;
        interpAlpha=1;camZoom=scenario==5?1.5f:1;selBuilding=INVALID_EID;msg.clear();
        const auto type=scenario==1 || scenario==5?UnitType::SiegeChopper:scenario==2?UnitType::MCV:UnitType::GI;
        EID target=world.spawnUnit(0,type,20.5f,20.5f);sel={target};
        if(scenario==1) world.spawnBuilding(0,BldType::PowerPlant,20,20,true);
        if(scenario==2 || scenario==5) {
            world.ents[target].chrono=40;
            for(int y=15;y<=25;++y) for(int x=15;x<=25;++x) world.map.at(x,y).height=3;
        }
        if(scenario==3) { target=world.spawnUnit(0,UnitType::Rhino,25.5f,20.5f);sel.push_back(target); }
        cmdDeploySel();consumePresentationEvents();
        if(scenario==4) {
            msg.clear();for(int repeat=0;repeat<14;++repeat) { if(repeat) msg+=" ";msg+=TR(S::MsgDeployBlocked); }
            msgTimer=5;
        }
        for(auto& fog:world.map.fog) std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        camX=(world.ents[target].x-world.ents[target].y)*32-500/camZoom;
        camY=(world.ents[target].x+world.ents[target].y)*16-300/camZoom;
        bakeTerrain();minimapTimer=0;
        const auto anchor=unitScreenPos(world.ents[target]);
        const float expectedX=anchor.x*camZoom,expectedY=(anchor.y-unitRenderLift(world.ents[target]))*camZoom;
        const auto hash=world.checksum();
        for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();
            const auto name="case"+std::to_string(scenario)+"_lang"+std::to_string(language)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y);
            const auto path=(std::filesystem::path(outputDir)/(name+".png")).string();shotFile=path;render();
            Image with=LoadImage(path.c_str());const auto dst=letterboxDst();
            bool bounded=!hudMessage.lines.empty() && hudMessage.bounds.x>=0 && hudMessage.bounds.y>=0
                && hudMessage.bounds.x+hudMessage.bounds.width<=SCREEN_W-sidebarW && hudMessage.bounds.y+hudMessage.bounds.height<SCREEN_H-BOTTOM_BAR_H;
            for(const auto& line:hudMessage.lines) bounded &= MeasureTextEx(font,line.c_str(),(float)hudMessage.size,0).x<=hudMessage.bounds.width-16+0.1f;
            if(scenario==4) bounded &= hudMessage.lines.size()>1;
            if(scenario==4 && language==1) {
                auto words=[](const std::string& value) {
                    std::istringstream stream(value);std::vector<std::string> tokens;std::string token;
                    while(stream>>token) tokens.push_back(token);return tokens;
                };
                std::string rejoined;for(const auto& line:hudMessage.lines) { rejoined+=line;rejoined+=' '; }
                bounded &= words(rejoined)==words(msg);
            }
            const float timer=moveFeedbackTimer;moveFeedbackTimer=0;
            const auto withoutPath=(std::filesystem::path(outputDir)/(name+"_without.png")).string();shotFile=withoutPath;render();moveFeedbackTimer=timer;
            Image without=LoadImage(withoutPath.c_str());
            const int px=(int)(dst.x+expectedX*dst.width/SCREEN_W),py=(int)(dst.y+expectedY*dst.height/SCREEN_H);
            const int radius=(int)(28*camZoom*dst.width/SCREEN_W);int pixels=0;
            if(with.width==size.x && with.height==size.y && without.width==size.x && without.height==size.y)
                for(int y=std::max(0,py-radius);y<std::min(with.height,py+radius+1);++y)
                    for(int x=std::max(0,px-radius);x<std::min(with.width,px+radius+1);++x) {
                        const Color a=GetImageColor(with,x,y),b=GetImageColor(without,x,y);
                        bool different=abs(a.r-b.r)+abs(a.g-b.g)+abs(a.b-b.b)>30;
                        bool tint=moveFeedbackRejected?a.r>140 && a.r>a.g*1.4f:moveFeedbackPartial?a.r>140 && a.g>100 && a.b<120:a.g>140 && a.g>a.r*1.4f;
                        pixels+=different && tint;
                    }
            UnloadImage(with);UnloadImage(without);
            bool ok=bounded && pixels>=6 && world.checksum()==hash;failures+=!ok;
            std::printf("FEEDBACK_VISUAL %s scenario=%d language=%d size=%dx%d bounded=%d lines=%zu pixels=%d simulation_unchanged=%d\n",
                ok?"PASS":"FAIL",scenario,language,size.x,size.y,bounded,hudMessage.lines.size(),pixels,world.checksum()==hash);
        }
    }
    for(int language=0;language<2;++language) {
        g_lang=language;world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<world.ents.size();++id)if(world.ents[id].alive)world.kill((EID)id,false);
        for(auto& cell:world.map.cells)cell=Cell{};
        world.cratesEnabled=false;ais.clear();netGame=false;campaignMission=-1;localPlayer=0;
        for(auto& player:world.players)player.defeated=false;
        gameOver=false;phase=Phase::InGame;showMenu=false;wasLowPower=false;msg.clear();msgTimer=0;
        world.spawnBuilding(0,BldType::ConYard,3,3,true);
        world.spawnBuilding(0,BldType::Barracks,8,8,true);
        EID plant=world.spawnBuilding(0,BldType::PowerPlant,12,8,true);
        world.spawnBuilding(1,BldType::ConYard,50,50,true);
        EID actor=world.spawnUnit(0,UnitType::Grizzly,20.5f,20.5f);world.ents[actor].tpSick=1000;
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        world.presentationEvents.clear();world.evaQueue.clear();
        World replay=world;replay.map.bldOccRef=&replay.bldOcc;bool equal=true;
        bool running=true;
        auto step=[&](){replay.update();logic();equal &= world.checksum()==replay.checksum();
            running &= !gameOver && world.matchOutcome(0)==MatchOutcome::Running;};
        world.kill(plant,false);replay.kill(plant,false);world.presentationEvents.clear();world.evaQueue.clear();
        evaLines={{"queued 1"},{"queued 2"},{"queued 3"},{"queued 4"}};
        World::Cmd command{};command.type=World::Cmd::Move;command.ids={actor};command.x=25.5f;command.y=20.5f;
        issueCmd(command);replay.applyCmd(0,command);step();
        const bool preserved=msg==TR(S::MsgImmobilized);
        const std::string warning=TR(S::MsgLowPower);
        bool queued=evaLines.size()==4 && evaLines.front().powerWarning && evaLines.front().text==warning && evaLines.back().text=="queued 3";
        const EID replacement=world.spawnBuilding(0,BldType::PowerPlant,12,8,true);
        equal &= replay.spawnBuilding(0,BldType::PowerPlant,12,8,true)==replacement;step();
        const bool canceled=std::none_of(evaLines.begin(),evaLines.end(),[](const Subtitle& line){return line.powerWarning;})
            && msg==TR(S::MsgImmobilized);
        world.kill(replacement,false);replay.kill(replacement,false);world.evaQueue.clear();step();
        const bool deduplicated=evaLines.size()==4 && std::count_if(evaLines.begin(),evaLines.end(),[](const Subtitle& line){return line.powerWarning;})==1 && evaLines.front().text==warning;
        camZoom=1;interpAlpha=1;sel={actor};selBuilding=INVALID_EID;
        camX=(world.ents[actor].x-world.ents[actor].y)*32-500;camY=(world.ents[actor].x+world.ents[actor].y)*16-300;
        bakeTerrain();minimapTimer=0;bool rendered=true;
        for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);const auto hash=world.checksum();render();
            const auto path=(std::filesystem::path(outputDir)/("priority_lang"+std::to_string(language)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();
            shotFile=path;render();Image capture=LoadImage(path.c_str());
            std::string visible;for(const auto& line:hudMessage.lines)visible+=line;
            rendered &= capture.width==size.x && capture.height==size.y && visible==TR(S::MsgImmobilized)
                && hudMessage.bounds.x+hudMessage.bounds.width<=SCREEN_W-sidebarW && hash==world.checksum();
            UnloadImage(capture);
        }
        g_lang=1-language;
        for(int frame=0;frame<125;++frame)step();
        const bool delivered=msg==TR(S::MsgLowPower) && msgTimer>0 && evaLines.size()==3;
        const bool localized=msg!=warning && msg==TR(S::MsgLowPower);g_lang=language;
        const EID last=world.spawnBuilding(0,BldType::PowerPlant,12,8,true);
        equal &= replay.spawnBuilding(0,BldType::PowerPlant,12,8,true)==last;step();
        evaLines.clear();msg.clear();msgTimer=0;world.kill(last,false);replay.kill(last,false);world.evaQueue.clear();step();
        const bool immediate=msg==warning && msgTimer>0 && evaLines.empty();
        const bool ok=preserved && queued && deduplicated && canceled && delivered && localized && immediate && rendered && equal && running;failures+=!ok;
        std::printf("FEEDBACK_PRIORITY %s language=%d preserved=%d queued=%d deduplicated=%d canceled=%d delivered=%d immediate=%d rendered=%d replay=%d running=%d localized=%d\n",
            ok?"PASS":"FAIL",language,preserved,queued,deduplicated,canceled,delivered,immediate,rendered,equal,running,localized);
    }
    g_lang=savedLanguage;shotAtDisplaySize=false;auditHud=false;SetTargetFPS(60);return failures;
}
