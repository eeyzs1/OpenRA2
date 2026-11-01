#include "game/game.h"
#include "game/lang.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>

int Game::elevationFeedbackRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    struct Restore { GameRules rules;int language;~Restore(){g_gameRules=rules;g_lang=language;} } restore{g_gameRules,g_lang};
    g_gameRules.chopperLandingTicks=40;g_gameRules.chopperDeployTicks=20;
    struct Case { int scenario,kind;bool delayed; }; // kind: deploy, attack, impact, release, entry, muzzle
    std::vector<Case> cases;
    for(int scenario=0;scenario<5;++scenario)for(int kind=0;kind<3;++kind)cases.push_back({scenario,kind,false});
    for(int kind=0;kind<3;++kind)cases.push_back({0,kind,true});
    cases.push_back({4,0,true});cases.push_back({4,2,true});
    for(int kind:{3,4})for(bool delayed:{false,true})cases.push_back({0,kind,delayed});
    for(int scenario:{5,6,7})for(bool delayed:{false,true})cases.push_back({scenario,scenario==5?2:5,delayed});
    for(const auto test:cases) {
        const int scenario=test.scenario,kind=test.kind;g_lang=(scenario+kind)&1;
        world.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"",true);
        for(int pass=0;pass<2;++pass)for(size_t id=0;id<world.ents.size();++id)if(world.ents[id].alive)world.kill((EID)id,false);
        for(auto& cell:world.map.cells){cell=Cell{};cell.height=3;}
        for(auto& player:world.players){player.money=0;player.defeated=false;}
        world.cratesEnabled=false;world.effects.clear();world.projs.clear();world.presentationEvents.clear();combatCues.clear();
        localPlayer=0;netGame=false;phase=Phase::InGame;showMenu=false;gameOver=false;campaignMission=-1;
        sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;
        sel.clear();selBuilding=INVALID_EID;clearPlacement();msg.clear();msgTimer=0;moveFeedbackTimer=0;
        camX=-500;camY=356;fogMaskTick=-1;shotAtDisplaySize=true;SetTargetFPS(0);
        const int owner=kind==1?1:0;
        EID actor=scenario==5?world.spawnBuilding(owner,BldType::PowerPlant,20,20,true)
            :world.spawnUnit(owner,scenario==4 || scenario==6?UnitType::Rhino:scenario==7?UnitType::Nighthawk:UnitType::SiegeChopper,20.5f,20.5f);
        world.spawnUnit(1-owner,UnitType::MCV,50.5f,50.5f);
        auto step=[&](int count){for(int i=0;i<count;++i)world.update();};
        auto deploy=[&]{World::Cmd c{};c.type=World::Cmd::Deploy;c.ids={actor};world.applyCmd(owner,c);};
        if(scenario<4) {
            deploy();
            if(scenario<2)step(scenario==0?30:10);
            else {step(60);deploy();step(20+(scenario==2?10:30));}
        } else if(scenario==4)world.ents[actor].magneticHeight=6;
        world.presentationEvents.clear();
        PresentationKind wanted=kind==0?PresentationKind::DeployOrder:kind==1?PresentationKind::AttackOrder:kind==2?PresentationKind::Impact:
            kind==3?PresentationKind::ReleaseOrder:kind==4?PresentationKind::EntryOrder:PresentationKind::Muzzle;
        if(kind==0)deploy();
        else if(kind==1) {
            EID shooter=world.spawnUnit(0,UnitType::Grizzly,24.5f,20.5f);
            World::Cmd c{};c.type=World::Cmd::Attack;c.ids={shooter};c.a=actor;world.applyCmd(0,c);
        } else if(kind==2)world.damage(actor,8,1-owner);
        else if(kind==3) {
            World::Cmd c{};c.type=World::Cmd::Unload;c.ids={actor};world.applyCmd(0,c);
        } else if(kind==4) {
            EID passenger=world.spawnUnit(0,UnitType::Engineer,22.5f,20.5f);
            World::Cmd c{};c.type=World::Cmd::Board;c.ids={passenger};c.a=actor;world.applyCmd(0,c);
        } else {
            EID target=world.spawnUnit(1,UnitType::MCV,24.5f,20.5f);world.ents[target].hp=10000;
            world.orderAttack({actor},target);
            for(int tick=0;tick<600;++tick) {
                world.update();
                if(std::any_of(world.presentationEvents.begin(),world.presentationEvents.end(),[&](const auto& event){return event.kind==wanted && event.entity==actor;}))break;
            }
        }
        auto found=std::find_if(world.presentationEvents.begin(),world.presentationEvents.end(),[&](const auto& event){return event.kind==wanted;});
        if(found==world.presentationEvents.end()){++failures;std::printf("ELEVATION_EVENT FAIL scenario=%d kind=%d missing\n",scenario,kind);continue;}
        const auto event=*found;
        const float fraction=scenario==0 || scenario==2?0.25f:scenario==1 || scenario==3?0.75f:scenario==7?1.f:0.f;
        const float lift=scenario==4?18.f:fraction*AIR_ALT;
        const bool captured=std::abs(event.flightFraction-fraction)<0.001f && event.magneticHeight==(scenario==4?6:0) && event.terrainHeight==3;
        bool reused=true;
        if(test.delayed) {
            world.kill(actor,false);const EID replacement=world.spawnUnit(owner,UnitType::Kirov,40.5f,40.5f);reused=replacement==actor;
            world.map.at((int)event.x,(int)event.y).height=0;
        }
        // Deliver the saved receipt alone; unrelated retirement receipts are separate events.
        world.presentationEvents.clear();world.presentationEvents.push_back(event);
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        const auto checksum=world.checksum();consumePresentationEvents();msgTimer=0;
        const bool isolated=checksum==world.checksum();
        const bool heightCorrect=kind==2 || kind==5 || std::abs(feedbackLift-lift)<0.001f;
        bakeTerrain();fogMaskTick=-1;
        for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            if(test.delayed && size.x==1920)continue;
            SetWindowSize(size.x,size.y);
            const auto name="case"+std::to_string(scenario)+"_kind"+std::to_string(kind)+"_delayed"+std::to_string(test.delayed)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y);
            const auto withPath=(std::filesystem::path(outputDir)/(name+".png")).string();shotFile=withPath;render();Image with=LoadImage(withPath.c_str());
            const float timer=moveFeedbackTimer;auto cues=std::move(combatCues);combatCues.clear();moveFeedbackTimer=0;
            const auto withoutPath=(std::filesystem::path(outputDir)/(name+"_without.png")).string();shotFile=withoutPath;render();Image without=LoadImage(withoutPath.c_str());combatCues=std::move(cues);moveFeedbackTimer=timer;
            float x=(event.x-event.y)*32-camX,y=(event.x+event.y)*16-camY-48-lift;
            const bool cue=kind==2 || kind==5;
            if(cue)y-=scenario==5?28:10;
            if(kind==5) {
                const float dx=(event.targetX-event.targetY-event.x+event.y)*32,dy=(event.targetX+event.targetY-event.x-event.y)*16;
                const float distance=std::sqrt(dx*dx+dy*dy);if(distance>0){x+=dx/distance*17;y+=dy/distance*17;}
            }
            const auto viewport=letterboxDst();const float scale=viewport.width/SCREEN_W;
            const int cx=(int)(viewport.x+x*scale),cy=(int)(viewport.y+y*scale),radius=(int)((cue?12:36)*scale);
            Color* a=LoadImageColors(with);Color* b=LoadImageColors(without);int pixels=0,minY=with.height,maxY=-1;
            if(a && b && with.width==without.width && with.height==without.height)
                for(int py=std::max(0,cy-radius);py<std::min(with.height,cy+radius+1);++py)
                    for(int px=std::max(0,cx-radius);px<std::min(with.width,cx+radius+1);++px) {
                        const auto first=a[py*with.width+px],second=b[py*with.width+px];
                        bool changed=std::abs((int)first.r-second.r)+std::abs((int)first.g-second.g)+std::abs((int)first.b-second.b)>30;
                        const bool colored=cue?first.r>150 && first.g>70:first.r>170 && first.g<150 && first.b<150;
                        if(changed && colored){++pixels;minY=std::min(minY,py);maxY=std::max(maxY,py);}
                    }
            const bool centered=kind==5 || (maxY>=0 && std::abs((minY+maxY)*0.5f-cy)<=2.0f*scale);
            const bool ok=captured && reused && isolated && heightCorrect && pixels>=6 && centered && checksum==world.checksum();failures+=!ok;
            std::printf("ELEVATION_FRAME %s scenario=%d kind=%d delayed=%d size=%dx%d captured=%d reused=%d isolated=%d height_correct=%d pixels=%d centered=%d lift=%.2f\n",
                ok?"PASS":"FAIL",scenario,kind,test.delayed,size.x,size.y,captured,reused,isolated,heightCorrect,pixels,centered,lift);
            if(a)UnloadImageColors(a);if(b)UnloadImageColors(b);UnloadImage(with);UnloadImage(without);
        }
    }
    return failures;
}
