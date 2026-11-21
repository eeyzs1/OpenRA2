#include "game_regression.h"
#include "gfx/sprites.h"
#include "gfx/terrain_material.h"
#include <cstdio>
#include <filesystem>

int GameRegression::terrainVisualRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("TERRAIN %s %s\n",ok?"PASS":"FAIL",name); };
    SetTargetFPS(0);localPlayer=0;selectReferenceMap();newGame(20261031);
    sim=SimInput{};sim.active=true;sim.pos={-100,-100};sel.clear();selBuilding=INVALID_EID;
    clearPlacement();shotAtDisplaySize=false;camZoom=1;
    int px,py;tileToScreen(19,14,px,py);camX=px-600.f;camY=py-350.f;
    world.map.reveal(0,19,14,12);fogMaskTick=-1;
    auto& cell=world.map.at(19,14);const int amount=cell.ore;
    auto capture=[&](const char* name) {
        const auto hash=world.checksum();const auto path=(std::filesystem::path(outputDir)/(std::string(name)+".png")).string();
        shotFile=path;render();check(hash==world.checksum(),"terrain presentation leaves simulation unchanged");return LoadImage(path.c_str());
    };
    auto difference=[&](Image a,Image b,bool wholeField=false) {
        Color* x=LoadImageColors(a);Color* y=LoadImageColors(b);int changes=0;
        for(int py=wholeField?0:350;py<(wholeField?SCREEN_H-BOTTOM_BAR_H:382);++py)
            for(int px=wholeField?0:568;px<(wholeField?SCREEN_W-sidebarW:632);++px) {
            int index=py*a.width+px;
            changes+=x && y && (x[index].r!=y[index].r || x[index].g!=y[index].g || x[index].b!=y[index].b || x[index].a!=y[index].a);
        }
        if(x) UnloadImageColors(x);if(y) UnloadImageColors(y);return changes;
    };
    Image full=capture("full");cell.ore=std::max(1,amount/8);Image thin=capture("thin");
    check(amount>0 && difference(full,thin)>5,"remaining ore changes actual mineral density before the cell is empty");
    world.map.harvestAt(19,14,cell.ore);Image depleted=capture("depleted");bakeTerrain();Image rebuilt=capture("rebuilt");
    check(difference(depleted,rebuilt)==0,"depleted ground matches a fresh terrain bake without a stale brown patch");
    for(Image image:{full,thin,depleted,rebuilt}) UnloadImage(image);
    const auto simulation=world.checksum();bool levels=true;int counts[9]{};
    for(Terrain type:{Terrain::Ore,Terrain::Gems}) for(int variant=0;variant<8;++variant) {
        std::vector<Color> previous;int countBefore=0,firstCount=0,lastCount=0;
        for(int level=0;level<=RESOURCE_VISUAL_LEVELS;++level) {
            const auto& sprite=g_sprites.resourceTile(type,variant,level,RESOURCE_VISUAL_LEVELS);
            Image image=LoadImageFromTexture(sprite.tex);Color* pixels=LoadImageColors(image);int count=0;
            levels &= pixels && image.width==TILE_W && image.height==TILE_H;
            std::vector<Color> current;
            for(int i=0;pixels && i<image.width*image.height;++i) {
                const auto c=pixels[i];count+=c.a>0;current.push_back(c);
                if(!previous.empty() && previous[i].a>0) {
                    const auto p=previous[i];levels &= c.r==p.r && c.g==p.g && c.b==p.b && c.a==p.a;
                }
            }
            levels &= count>=countBefore && (level!=0 || count==0);
            if(level==1) firstCount=count;
            if(level==RESOURCE_VISUAL_LEVELS) {
                lastCount=count;levels &= sprite.tex.id==g_sprites.tile(type,variant).tex.id;
            }
            if(type==Terrain::Ore && variant==0) counts[level]=count;
            previous=std::move(current);countBefore=count;
            if(pixels) UnloadImageColors(pixels);UnloadImage(image);
        }
        levels &= firstCount>0 && firstCount<lastCount*3/4;
    }
    check(levels && world.checksum()==simulation,"all gold/gem variants have monotonic whole-cluster levels without changing authored colors or simulation");
    std::printf("TERRAIN_DENSITY ore_variant0=%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
        counts[0],counts[1],counts[2],counts[3],counts[4],counts[5],counts[6],counts[7],counts[8]);
    for(int y=0;y<world.map.h;++y) for(int x=0;x<world.map.w;++x) {
        auto& c=world.map.at(x,y);if(c.ore>0) world.map.harvestAt(x,y,c.ore);
    }
    Image emptyField=capture("field_depleted");bakeTerrain();Image emptyRebuilt=capture("field_rebuilt");
    check(difference(emptyField,emptyRebuilt,true)==0,"depleting a complete resource field keeps the same native substrate across the whole playfield after rebaking");
    UnloadImage(emptyField);UnloadImage(emptyRebuilt);
    newGame(20261031);sel.clear();selBuilding=INVALID_EID;msgTimer=0;clearPlacement();sim.pos={-100,-100};
    tileToScreen(19,14,px,py);camX=px-600.f;camY=py-350.f;camZoom=1;
    world.map.reveal(0,19,14,12);fogMaskTick=-1;shotAtDisplaySize=true;
    bool shots=true;
    for(int level:{8,4,1,0}) {
        for(int y=0;y<world.map.h;++y) for(int x=0;x<world.map.w;++x) {
            auto& c=world.map.at(x,y);if(c.terrain!=Terrain::Ore && c.terrain!=Terrain::Gems) continue;
            if(level) c.ore=(int16_t)std::max(1,c.oreMax*level/8);
            else world.map.harvestAt(x,y,c.ore);
        }
        for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);const auto hash=world.checksum();render();
            const auto path=(std::filesystem::path(outputDir)/("density"+std::to_string(level)+"_"+
                std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();shotFile=path;render();
            Image image=LoadImage(path.c_str());shots &= hash==world.checksum() && image.width==size.x && image.height==size.y;
            UnloadImage(image);
        }
    }
    check(shots,"full, half, sparse and depleted fields render at both actual framebuffer sizes without simulation changes");
    sim=SimInput{};std::printf("TERRAIN failures=%d\n",failures);return failures;
}
