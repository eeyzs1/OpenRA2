#include "game_regression.h"
#include "gfx/sprites.h"
#include <filesystem>
#include <set>
#include <cstdio>

int GameRegression::chronoVisualRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("CHRONO_VISUAL %s %s\n",ok?"PASS":"FAIL",name); };
    SetTargetFPS(0);localPlayer=0;phase=Phase::InGame;showMenu=gameOver=paused=netGame=false;camX=-600;camY=250;camZoom=1;
    sel.clear();selBuilding=INVALID_EID;clearPlacement();ais.clear();combatCues.clear();moveFeedbackTimer=msgTimer=0;
    sim=SimInput{};sim.active=true;sim.pos={-100,-100};
    auto reset=[&]() {
        world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
        for(auto& c:world.map.cells) c=Cell{};
        std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_VISIBLE);fogMaskTick=-1;
    };
    auto isolated=[&](const std::string& name) {
        const auto hash=world.checksum();BeginTextureMode(canvas);ClearBackground(BLACK);drawEntities();drawEffectsLayer();EndTextureMode();
        check(hash==world.checksum(),"chrono drawing does not mutate simulation");
        Image image=LoadImageFromTexture(canvas.texture);ImageFlipVertical(&image);
        if(!name.empty()) ExportImage(image,(std::filesystem::path(outputDir)/(name+".png")).string().c_str());return image;
    };
    auto changed=[](Image a,Image b) {
        if(!a.data || !b.data || a.width!=b.width || a.height!=b.height) return -1;
        Color* x=LoadImageColors(a);Color* y=LoadImageColors(b);int count=0;
        for(int i=0;x && y && i<a.width*a.height;++i) count+=x[i].r!=y[i].r || x[i].g!=y[i].g || x[i].b!=y[i].b;
        if(x) UnloadImageColors(x);if(y) UnloadImageColors(y);return count;
    };
    reset();Image empty=isolated("");bool visible=true,hidden=true,animated=true;
    for(int visual=0;visual<3;++visual) {
        Effect warp{};warp.kind=9;warp.aux=visual;warp.x=warp.y=18.5f;warp.maxAge=24;
        world.effects={warp};world.effects[0].age=4;Image a=isolated("warp_"+std::to_string(visual)+"_early");
        world.effects[0].age=14;Image b=isolated("warp_"+std::to_string(visual)+"_late");
        visible &= changed(empty,a)>10;animated &= changed(a,b)>10;
        std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_SEEN);Image fog=isolated("");hidden &= changed(empty,fog)==0;
        std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_VISIBLE);
        for(Image image:{a,b,fog}) UnloadImage(image);
    }
    check(visible && animated,"arrival, departure and erasure sequences are visible and advance");check(hidden,"hidden chrono effects never reveal their positions");UnloadImage(empty);
    for(UnitType type:{UnitType::Grizzly,UnitType::ChronoMiner,UnitType::Typhoon}) {
        reset();EID unit=world.spawnUnit(0,type,18.5f,18.5f);Image normal=isolated("");
        world.ents[unit].tpSick=45;Image sick=isolated("phase_"+std::to_string((int)type));
        world.ents[unit].tpSick=0;world.ents[unit].chrono=20;Image frozen=isolated("frozen_"+std::to_string((int)type));
        world.ents[unit].chrono=0;Image recovered=isolated("");
        check(changed(normal,sick)>20 && changed(normal,frozen)>20 && changed(normal,recovered)==0,
            "vehicles, miners and ships visibly hold phase/freeze state and recover exactly");
        for(Image image:{normal,sick,frozen,recovered}) UnloadImage(image);
    }
    reset();EID building=world.spawnBuilding(0,BldType::WarFactory,18,18,true);world.ents[building].constructAnim=0;
    Image normal=isolated("");world.ents[building].chrono=20;Image frozen=isolated("frozen_building");
    check(changed(normal,frozen)>20,"chrono-frozen buildings show their disabled state");UnloadImage(normal);UnloadImage(frozen);
    reset();EID tank=world.spawnUnit(0,UnitType::Grizzly,18.5f,18.5f);Image actor=isolated("");
    Effect ground{};ground.kind=9;ground.aux=1;ground.x=ground.y=18.5f;ground.age=6;ground.maxAge=24;world.effects={ground};
    Image layered=isolated("ground_under_tank");world.ents[tank].alive=false;Image groundOnly=isolated("");world.ents[tank].alive=true;
    const auto& body=g_sprites.unitBody(UnitType::Grizzly,world.ents[tank].dir,0,world.players[0].colorId);
    Image bodyImage=LoadImageFromTexture(body.tex);Color* alpha=LoadImageColors(bodyImage);Color* a=LoadImageColors(actor);Color* b=LoadImageColors(layered);Color* g=LoadImageColors(groundOnly);
    const Vector2 point=unitScreenPos(world.ents[tank]);bool occluded=alpha && a && b && g;int overlap=0;
    for(int y=0;occluded && y<bodyImage.height;++y) for(int x=0;x<bodyImage.width;++x) {
        const int px=(int)point.x-body.ox+x,py=(int)point.y-body.oy+y;
        if(px<0 || py<0 || px>=actor.width || py>=actor.height || alpha[y*bodyImage.width+x].a!=255) continue;
        const int i=py*actor.width+px;if(!(g[i].r || g[i].g || g[i].b)) continue;
        ++overlap;occluded &= a[i].r==b[i].r && a[i].g==b[i].g && a[i].b==b[i].b;
    }
    check(occluded && overlap>5,"opaque vehicle pixels correctly cover the original ground warp animation");
    for(Color* pixels:{alpha,a,b,g}) if(pixels) UnloadImageColors(pixels);
    for(Image image:{actor,layered,groundOnly,bodyImage}) UnloadImage(image);
    reset();EID hospital=world.spawnBuilding(0,BldType::Hospital,18,18,true);
    world.ents[hospital].constructAnim=0;world.ents[hospital].chrono=20;world.ents[hospital].bldAnim=7;
    Image heldA=isolated("");world.tick+=8;Image heldB=isolated("");
    check(changed(heldA,heldB)==0,"frozen owned building activity stays paused while world time advances");UnloadImage(heldA);UnloadImage(heldB);
    reset();EID unit=world.spawnUnit(0,UnitType::Chrono,10.5f,18.5f);
    World::Cmd move{};move.type=World::Cmd::Move;move.ids={unit};move.x=20.5f;move.y=18.5f;world.applyCmd(0,move);
    bool departure=false,arrival=false;for(auto& effect:world.effects) if(effect.kind==9) {
        departure |= effect.aux==1;arrival |= effect.aux==0;effect.age=4;
    }
    check(departure && arrival,"a real jump distinguishes departure from arrival without changing simulation lifetimes");
    World restored;bool replay=false;
    if(FILE* file=tmpfile()) { replay=world.saveGame(file);rewind(file);replay=replay && restored.loadGame(file);fclose(file); }
    if(replay) {
        World simulation=world;simulation.map.bldOccRef=&simulation.bldOcc;
        replay=restored.checksum()==world.checksum();
        for(int tick=0;tick<60 && replay;++tick) { simulation.update();restored.update();replay=simulation.checksum()==restored.checksum(); }
    }
    check(replay,"current snapshot preserves chrono visual variants and continued simulation");
    const int originalVariant=world.effects.front().aux;world.effects.front().aux=3;bool rejects=false;
    if(FILE* file=tmpfile()) { bool saved=world.saveGame(file);rewind(file);World invalid;rejects=saved && !invalid.loadGame(file);fclose(file); }
    world.effects.front().aux=originalVariant;check(rejects,"snapshot validation rejects an unsupported chrono visual variant");
    bakeTerrain();
    for(int width:{1280,1920}) {
        SetWindowSize(width,width==1280?720:1080);shotAtDisplaySize=true;
        auto frame=[&](const char* suffix) {
            const auto hash=world.checksum();const auto file=(std::filesystem::path(outputDir)/(std::string("jump_")+std::to_string(width)+suffix+".png")).string();
            shotFile=file;render();check(world.checksum()==hash,"normal chrono render preserves simulation checksum");return LoadImage(file.c_str());
        };
        Image effects=frame("_effects");const auto saved=world.effects;world.effects.clear();Image control=frame("_control");world.effects=saved;
        check(effects.width==width && effects.height==(width==1280?720:1080) && changed(effects,control)>20,
            "actual departure and arrival appear in complete display-sized battlefield frames");UnloadImage(effects);UnloadImage(control);
    }
    shotAtDisplaySize=false;sim=SimInput{};std::printf("CHRONO_VISUAL failures=%d\n",failures);return failures;
}
