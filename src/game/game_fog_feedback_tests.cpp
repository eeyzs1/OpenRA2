#include "game_regression.h"
#include "gfx/fog_line.h"
#include <cmath>
#include <filesystem>
#include <cstdio>

int GameRegression::fogFeedbackRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("FOG_FEEDBACK %s %s\n",ok?"PASS":"FAIL",name); };
    SetTargetFPS(0);world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
    localPlayer=0;phase=Phase::InGame;showMenu=gameOver=paused=netGame=false;sel.clear();selBuilding=INVALID_EID;clearPlacement();
    camX=-600;camY=250;camZoom=1;sim=SimInput{};sim.active=true;sim.pos={-100,-100};
    for(auto& c:world.map.cells) c.overlay=Overlay::None;
    auto fog=[&](FogState value) { std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),value);fogMaskTick=-1;syncFogKnowledge(); };
    auto draw=[&](const std::string& name) {
        const auto hash=world.checksum();BeginTextureMode(canvas);ClearBackground(BLACK);drawEntities();drawEffectsLayer();EndTextureMode();
        Image image=LoadImageFromTexture(canvas.texture);ImageFlipVertical(&image);
        if(!name.empty()) ExportImage(image,(std::filesystem::path(outputDir)/(name+".png")).string().c_str());
        Color* pixels=LoadImageColors(image);int count=0;
        if(pixels) for(int i=0;i<image.width*image.height;++i) count+=pixels[i].r || pixels[i].g || pixels[i].b;
        if(pixels) UnloadImageColors(pixels);UnloadImage(image);
        check(world.checksum()==hash,"drawing visibility-filtered combat does not mutate simulation");return count;
    };
    bool allHidden=true,allVisible=true;
    for(int kind:{0,1,2,3,4,5,6,7,8,10,11,12}) {
        Effect effect{};effect.kind=kind;effect.x=12.5f;effect.y=18.5f;effect.x2=24.5f;effect.y2=18.5f;effect.maxAge=60;effect.age=2;
        effect.aux=kind==10?(int)UnitType::GI:0;
        world.effects={effect};fog(FOG_VISIBLE);const int visible=draw("visible_effect_"+std::to_string(kind));
        fog(FOG_SEEN);const int seen=draw("seen_effect_"+std::to_string(kind));
        fog(FOG_UNSEEN);const int unknown=draw("");
        std::printf("FOG_EFFECT kind=%d visible=%d seen=%d unknown=%d\n",kind,visible,seen,unknown);
        allHidden &= seen==0 && unknown==0;allVisible &= kind==10 || visible>0;
    }
    check(allHidden,"hidden explosions, smoke, beams, deaths and resource effects are absent");
    check(allVisible,"visible combat effects remain readable");
    world.effects.clear();Projectile projectile{};projectile.x=18.5f;projectile.y=18.5f;projectile.tx=22;projectile.ty=18;projectile.player=1;
    world.projs={projectile};fog(FOG_VISIBLE);check(draw("visible_projectile")>0,"visible projectile remains visible");
    fog(FOG_SEEN);check(draw("seen_projectile")==0,"hidden projectile cannot reveal combat");
    world.projs[0].alive=false;fog(FOG_VISIBLE);check(draw("")==0,"retired projectile slots are not drawn");world.projs.clear();
    Nuke nuke{};nuke.active=true;nuke.player=1;nuke.tx=18.5f;nuke.ty=18.5f;nuke.timer=35;world.nukes={nuke};
    fog(FOG_VISIBLE);check(draw("visible_nuke")>0,"visible hostile nuclear strike is shown");
    fog(FOG_SEEN);check(draw("seen_nuke")==0,"hidden hostile nuclear targeting marker is suppressed");world.nukes.clear();
    // The source and target are hidden, but the beam crosses three observed tiles.
    Effect beam{};beam.kind=3;beam.x=12.5f;beam.y=18.5f;beam.x2=24.5f;beam.y2=18.5f;beam.maxAge=60;beam.age=2;
    world.effects={beam};fog(FOG_SEEN);for(int x=17;x<=19;++x) world.map.fog[0][18*64+x]=FOG_VISIBLE;
    check(draw("beam_visible_middle")>0,"a visible middle segment survives even with both beam endpoints hidden");
    const auto intervals=visibleLineIntervals(world.map,0,beam.x,beam.y,beam.x2,beam.y2);
    const auto reverse=visibleLineIntervals(world.map,0,beam.x2,beam.y2,beam.x,beam.y);
    check(intervals.size()==1 && reverse==intervals && std::abs(intervals[0].first-0.375f)<0.00001f
        && std::abs(intervals[0].second-0.625f)<0.00001f,"beam intervals stop exactly at visible cell boundaries in both directions");
    Image segment=LoadImage((std::filesystem::path(outputDir)/"beam_visible_middle.png").string().c_str());
    Color* segmentPixels=LoadImageColors(segment);bool clipped=segmentPixels!=nullptr;
    for(int y=0;segmentPixels && y<segment.height;++y) for(int x=0;x<segment.width;++x) {
        const auto p=segmentPixels[y*segment.width+x];if(p.r || p.g || p.b) clipped &= x>=551 && x<=649;
    }
    check(clipped,"the rendered beam cannot extend toward either hidden endpoint");
    if(segmentPixels) UnloadImageColors(segmentPixels);UnloadImage(segment);
    // Isolate two disjoint visible islands along the same line.
    fog(FOG_SEEN);world.map.fog[0][18*64+15]=world.map.fog[0][18*64+21]=FOG_VISIBLE;
    const auto islands=visibleLineIntervals(world.map,0,beam.x,beam.y,beam.x2,beam.y2);
    check(islands.size()==2 && islands[0].second<islands[1].first,"separate visible islands never join across hidden terrain");
    world.effects.clear();
    // Cursor and submitted command must use exactly the same last observed mineral state.
    const EID miner=world.spawnUnit(0,UnitType::ChronoMiner,10.5f,10.5f);sel={miner};
    fog(FOG_VISIBLE);world.map.at(19,14).terrain=Terrain::Ore;world.map.at(19,14).ore=world.map.at(19,14).oreMax=500;syncFogKnowledge();fog(FOG_SEEN);
    const float wx=(19.5f-14.5f)*(TILE_W/2.0f),wy=(19.5f+14.5f)*(TILE_H/2.0f);
    int mx,my;worldToScreen(wx,wy,mx,my);world.map.harvestAt(19,14,500);
    const auto hash=world.checksum();updateHoverCursor(mx,my);
    check(cursorKind==CursorKind::Harvest && world.checksum()==hash,"hidden mineral depletion cannot change the hover cursor");
    issueSmartOrder(mx,my);check(world.ents[miner].state==UState::HarvestGo && world.ents[miner].oreCell.x==19 && world.ents[miner].oreCell.y==14,
        "right-click submits harvest toward the remembered mineral patch");
    fog(FOG_VISIBLE);updateHoverCursor(mx,my);check(cursorKind!=CursorKind::Harvest,"re-scouting depletion updates the hover cursor");
    fog(FOG_UNSEEN);world.map.at(19,14).terrain=Terrain::Ore;world.map.at(19,14).ore=500;updateHoverCursor(mx,my);
    check(cursorKind!=CursorKind::Harvest,"unknown mineral deposits cannot be discovered by hovering");
    // Exercise the complete render stack, including fog, at the two acceptance resolutions.
    sel.clear();fog(FOG_VISIBLE);bakeTerrain();fog(FOG_SEEN);world.effects={beam};
    for(int width:{1280,1920}) {
        const int height=width==1280?720:1080;SetWindowSize(width,height);shotAtDisplaySize=true;
        auto frame=[&](const char* suffix) {
            const auto file=(std::filesystem::path(outputDir)/(std::string("battlefield_")+std::to_string(width)+suffix+".png")).string();
            const auto hash=world.checksum();shotFile=file;render();check(world.checksum()==hash,"complete combat render preserves simulation checksum");return LoadImage(file.c_str());
        };
        Image hidden=frame("_hidden");world.effects.clear();Image control=frame("_control");
        bool same=hidden.data && control.data && hidden.width==width && hidden.height==height && control.width==width && control.height==height;
        Color* a=LoadImageColors(hidden);Color* b=LoadImageColors(control);
        if(!a || !b) same=false;
        for(int y=0;same && y<height*(SCREEN_H-BOTTOM_BAR_H)/SCREEN_H;++y)
            for(int x=0;same && x<width*(SCREEN_W-sidebarW)/SCREEN_W;++x) {
                const int index=y*width+x;same=a[index].r==b[index].r && a[index].g==b[index].g && a[index].b==b[index].b;
            }
        check(same,"hidden beam leaves no trace in the complete battlefield at display resolution");
        if(a) UnloadImageColors(a);if(b) UnloadImageColors(b);UnloadImage(hidden);UnloadImage(control);world.effects={beam};
    }
    shotAtDisplaySize=false;world.effects.clear();
    sim=SimInput{};std::printf("FOG_FEEDBACK failures=%d\n",failures);return failures;
}
