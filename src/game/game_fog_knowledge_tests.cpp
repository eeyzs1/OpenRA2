#include "game/game.h"
#include <cstdio>
#include <filesystem>

int Game::fogKnowledgeRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("FOG_KNOWLEDGE %s %s\n",ok?"PASS":"FAIL",name); };
    SetTargetFPS(0);world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
    localPlayer=0;phase=Phase::InGame;showMenu=false;gameOver=false;netGame=false;paused=false;clearPlacement();
    sel.clear();selBuilding=INVALID_EID;sim=SimInput{};sim.active=true;sim.pos={-100,-100};msgTimer=0;
    world.spawnBuilding(0,BldType::ConYard,8,8,true);world.spawnBuilding(0,BldType::PowerPlant,8,14,true);
    world.spawnBuilding(0,BldType::Radar,8,20,true);
    EID enemy=world.spawnBuilding(1,BldType::WarFactory,25,20,true);
    for(auto& e:world.ents) e.constructAnim=0;
    world.map.at(14,17).overlay=Overlay::Tree1;
    ais.clear();campaignMission=-1;missionTriggers.clear();objectiveText.clear();
    camX=-500;camY=150;camZoom=1;shotAtDisplaySize=false;bakeTerrain();
    auto capture=[&](const char* name) {
        const auto hash=world.checksum();minimapTimer=0;const auto path=(std::filesystem::path(outputDir)/(std::string(name)+".png")).string();
        shotFile=path;render();check(hash==world.checksum(),"observing and drawing fog knowledge never mutate simulation");return LoadImage(path.c_str());
    };
    auto differences=[&](Image a,Image b) {
        Color* x=LoadImageColors(a);Color* y=LoadImageColors(b);int changed=0;
        for(int py=0;py<SCREEN_H-BOTTOM_BAR_H;++py) for(int px=0;px<SCREEN_W-sidebarW;++px) {
            const int i=py*a.width+px;changed+=x && y && (x[i].r!=y[i].r || x[i].g!=y[i].g || x[i].b!=y[i].b);
        }
        if(x) UnloadImageColors(x);if(y) UnloadImageColors(y);return changed;
    };
    std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_VISIBLE);fogMaskTick=-1;
    Image visible=capture("visible");
    std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_SEEN);fogMaskTick=-1;
    Image remembered=capture("remembered");
    Image radarRemembered=LoadImageFromTexture(minimap.texture);
    auto samePixels=[](Image a,Image b) {
        if(!a.data || !b.data || a.width!=b.width || a.height!=b.height) return false;
        Color* x=LoadImageColors(a);Color* y=LoadImageColors(b);bool same=x && y;
        for(int i=0;same && i<a.width*a.height;++i) same=x[i].r==y[i].r && x[i].g==y[i].g && x[i].b==y[i].b;
        if(x) UnloadImageColors(x);if(y) UnloadImageColors(y);return same;
    };
    const int hp=world.ents[enemy].hp;world.ents[enemy].hp=hp/4;
    Image damaged=capture("hidden_damage");
    check(differences(remembered,damaged)==0,"hidden building damage cannot change the remembered battlefield");
    world.ents[enemy].hp=hp;world.map.harvestAt(19,14,world.map.at(19,14).ore);world.map.at(14,17).overlay=Overlay::None;
    Image mined=capture("hidden_mining");
    check(differences(remembered,mined)==0,"depletion and tree removal under lost vision preserve the observed battlefield");
    world.kill(enemy,false);EID replacement=world.spawnBuilding(1,BldType::NuclearReactor,15,20,true);
    world.ents[replacement].constructAnim=0;
    Image destroyed=capture("hidden_replacement");
    check(differences(remembered,destroyed)==0,"hidden destruction and a reused entity slot preserve the last seen building");
    Image radarHidden=LoadImageFromTexture(minimap.texture);
    check(samePixels(radarRemembered,radarHidden),"radar retains last observed minerals and building markers");
    check(fogKnowledge.buildings.contains(20*64+25) && !fogKnowledge.buildings.contains(20*64+15),
        "memory keeps the scouted anchor and never discovers the hidden replacement");
    const auto snapshot=(std::filesystem::path(outputDir)/"scout-memory.sav").string();
    const auto hash=world.checksum();
    check(saveGameFile(snapshot.c_str()),"snapshot stores scout memory while vision is lost");
    fogKnowledge=FogKnowledge{};
    const bool restored=loadGameFile(snapshot.c_str());
    check(restored && hash==world.checksum(),"current snapshot restores simulation without observation changing its hash");
    Image resumed=capture("resumed");
    check(differences(remembered,resumed)==0,"loading under lost vision preserves the last observed battlefield");
    Image radarResumed=LoadImageFromTexture(minimap.texture);
    check(samePixels(radarRemembered,radarResumed),"loading retains radar scout memory");
    if(restored) {
        auto invalid=fogKnowledge;invalid.buildings.begin()->second.color=-1;
        check(!validateFogKnowledge(invalid,world,localPlayer),"malformed owned building color cannot index radar palette");
    }
    for(int width:{1280,1920}) {
        const int height=width==1280?720:1080;SetWindowSize(width,height);shotAtDisplaySize=true;
        const auto file=(std::filesystem::path(outputDir)/(std::string("remembered_")+std::to_string(width)+".png")).string();
        shotFile=file;render();Image image=LoadImage(file.c_str());
        check(image.data && image.width==width && image.height==height,"display-sized scout memory screenshot exports correctly");UnloadImage(image);
    }
    shotAtDisplaySize=false;
    std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_VISIBLE);fogMaskTick=-1;
    Image revisited=capture("revisited");
    check(differences(visible,revisited)>50,"scouting again reveals actual resource and building changes");
    for(Image image:{visible,remembered,damaged,mined,destroyed,revisited}) UnloadImage(image);
    check(!fogKnowledge.buildings.contains(20*64+25) && fogKnowledge.buildings.contains(20*64+15),
        "re-scouting removes the destroyed building and records its replacement");
    std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_UNSEEN);fogMaskTick=-1;syncFogKnowledge();
    bool shrouded=true;for(const auto& [key,b]:fogKnowledge.buildings) shrouded &= !buildingExplored(b);
    check(shrouded,"unexplored shroud suppresses remembered silhouettes");
    // A visible footprint edge is sufficient to observe and interact with a building.
    world.map.fog[0][20*64+15+bldDef(BldType::NuclearReactor).w-1]=FOG_VISIBLE;syncFogKnowledge();
    check(buildingVisible(15,20,BldType::NuclearReactor),"footprint edge vision reveals a building even with its anchor hidden");
    localPlayer=1;syncFogKnowledge();check(!fogKnowledge.buildings.contains(20*64+15),"changing viewer cannot retain the other player's scout memory");
    localPlayer=0;world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
    syncFogKnowledge();check(fogKnowledge.buildings.empty(),"a fresh world at the same tick and dimensions clears prior scout memory");
    for(Image image:{radarRemembered,radarHidden,radarResumed,resumed}) UnloadImage(image);
    std::error_code error;std::filesystem::remove(snapshot,error);
    sim=SimInput{};std::printf("FOG_KNOWLEDGE failures=%d\n",failures);return failures;
}
