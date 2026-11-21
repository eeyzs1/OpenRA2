#include "game_regression.h"
#include "core/content.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

int GameRegression::nativeMapRegression(const char* outputDir) {
    const std::string mapPath=contentResolve("assets/maps/native/all01t/terrain.nmap");
    if(mapPath.empty() || !FileExists(mapPath.c_str())) {
        std::printf("NATIVE_MAP SKIP: run python tools/ra2pack/gen_native_maps.py\n");return 77;
    }
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* label){std::printf("NATIVE_MAP %s: %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;};
    world=World{};localPlayer=0;campaignMission=-1;phase=Phase::InGame;gameOver=victory=false;ais.clear();
    const bool loaded=world.init(64,64,17,1,0,{Faction::Allies},0,mapPath.c_str(),true,true);
    check(loaded,"original terrain is required, with no generated replacement");if(!loaded)return failures;
    {
        World missing,generic;
        check(!missing.init(64,64,17,1,0,{Faction::Allies},0,"assets/maps/native/missing/terrain.nmap",true)
            && missing.map.cells.empty()
            && !generic.init(64,64,17,1,0,{Faction::Allies},0,mapPath.c_str(),false),
            "native requests never fall back to noise terrain or an invented MCV opening");
    }
    const auto& n=world.map.native;
    check(world.map.w==114 && world.map.h==114 && n.assetId=="all01t" && n.theater=="URBAN"
        && n.width==50 && n.height==64 && n.local==std::array<int,4>{2,6,46,52},"original dimensions, theater and viewport are preserved");
    int present=0,water=0,blocked=0,blockedIndex=-1;
    for(int y=0;y<114;++y)for(int x=0;x<114;++x) {
        const auto& c=world.map.at(x,y);present+=c.present;water+=c.present && c.terrain==Terrain::Water;blocked+=c.present && c.blocked;
        if(c.present && c.blocked && blockedIndex<0)blockedIndex=y*114+x;
    }
    const auto& start=world.map.at(62,108);const auto& raw=n.tiles[108*114+62];
    check(present==6336 && water==955 && blocked>0 && !world.map.inBounds(0,0)
        && !world.passableFor(0,0,0) && !world.passableFor(0,0,1) && !world.passableFor(0,0,2),
        "all 6336 original cells survive; the absent canvas is closed to every movement domain");
    check(raw.tile==309 && raw.subtile==13 && raw.terrainType==11 && start.height==6 && start.passable(),
          "Tanya's authored starting cell keeps exact tile 309/subtile 13/height 6");
    bool cliff=false;
    for(int y=0;y<114 && !cliff;++y)for(int x=0;x<114 && !cliff;++x)
        if(world.map.inBounds(x,y) && world.map.at(x,y).blocked)cliff=!world.passableFor(x,y,0) && !world.passableFor(x,y,1) && !world.passableFor(x,y,2);
    check(cliff,"a native blocked surface cannot be crossed by a naval or amphibious shortcut");
    auto point=[&](int x,int y){return (size_t)y*114+x;};
    const auto before=world.checksum();bakeTerrain();
    check(terrainTex.id && terrainW==7296 && terrainH==3648 && world.checksum()==before,
          "native TMP terrain loads at the exact coordinate scale without mutating World");
    Image actual=terrainTex.id?LoadImageFromTexture(terrainTex):Image{};
    Image source=LoadImage(contentPathFmt("assets/maps/native/%s/terrain.png",n.assetId.c_str()));
    bool pixels=actual.data && source.data && actual.width==source.width && actual.height==source.height;int opaque=0;
    if(pixels)for(int y=0;y<source.height;y+=19)for(int x=0;x<source.width;x+=23) {
        Color a=GetImageColor(actual,x,y),b=GetImageColor(source,x,y);pixels&=!memcmp(&a,&b,sizeof(Color));opaque+=b.a>0;
    }
    check(pixels && opaque>5000,"actual GPU ground pixels match the imported original raster, including alpha");
    if(actual.data)UnloadImage(actual);if(source.data)UnloadImage(source);

    const EID tanya=world.spawnUnit(0,UnitType::Tanya,62.5f,108.5f);
    World::Cmd move{};move.type=World::Cmd::Move;move.ids={tanya};move.x=64.5f;move.y=104.5f;world.applyCmd(0,move);
    for(int tick=0;tick<20;++tick)world.update();
    FILE* file=tmpfile();World restored;bool resumed=file && world.saveGame(file);
    if(resumed){rewind(file);resumed=restored.loadGame(file);}if(file)fclose(file);
    resumed=resumed && world.checksum()==restored.checksum();
    for(int tick=0;tick<240 && resumed;++tick){world.update();restored.update();resumed=world.checksum()==restored.checksum();}
    check(resumed && world.valid(tanya) && world.ents[tanya].x>63.5f,
          "real orders on original terrain continue identically for 240 ticks after a World save");
    check(restored.map.native.tiles[point(62,108)].tile==309 && !restored.map.at(0,0).present,
          "World restores native identifiers and the masked canvas");
    World changed=world;changed.map.bldOccRef=&changed.bldOcc;changed.map.native.tiles[point(62,108)].subtile^=1;
    check(changed.checksum()!=world.checksum(),"native source-cell metadata participates in the simulation checksum");

    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
    syncFogKnowledge();const auto appHash=world.checksum();
    const auto savePath=(std::filesystem::path(outputDir)/"native-application.sav").string();
    const bool savedApp=saveGameFile(savePath.c_str()),loadedApp=savedApp && loadGameFile(savePath.c_str());
    check(savedApp && loadedApp && world.checksum()==appHash && blockedIndex>=0
        && fogKnowledge.known[blockedIndex] && fogKnowledge.cells[blockedIndex].blocked
        && fogKnowledge.cells[point(62,108)].present,
          "application snapshot preserves original terrain and observed native masks");
    if(!loadedApp || world.checksum()!=appHash)std::printf("NATIVE_MAP application saved=%d loaded=%d hash=%u expected=%u\n",savedApp,loadedApp,world.checksum(),appHash);
    bakeFogMask();Image fogPixels=LoadImageFromTexture(fogMaskTex);bool nativeFog=fogPixels.data!=nullptr;int cliffSamples=0;
    const int fw=terrainW/8+2,fh=terrainH/8+2;
    if(nativeFog)for(size_t i=0;i+1<nativeFogOffsets.size();++i) {
        bool cliffPixel=false;
        for(uint32_t j=nativeFogOffsets[i];j<nativeFogOffsets[i+1];++j)
            cliffPixel |= world.map.native.tiles[nativeFogOwners[j]-1].terrainType==15;
        if(cliffPixel){++cliffSamples;nativeFog &= GetImageColor(fogPixels,(int)i%fw,(int)i/fw).a==0;}
    }
    check(nativeFog && cliffSamples>100,"visible native cliff faces remain visible throughout their authored extra image");
    if(fogPixels.data)UnloadImage(fogPixels);
    std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_UNSEEN);
    if(blockedIndex>=0)world.map.fog[0][blockedIndex]=FOG_VISIBLE;
    bakeFogMask();fogPixels=LoadImageFromTexture(fogMaskTex);nativeFog=fogPixels.data && nativeFogOffsets.size()==(size_t)fw*fh+1;
    int mixedSamples=0;
    if(nativeFog)for(size_t i=0;i+1<nativeFogOffsets.size();++i) {
        bool visible=nativeFogOffsets[i]!=nativeFogOffsets[i+1];
        if(nativeFogOffsets[i+1]-nativeFogOffsets[i]>1)++mixedSamples;
        for(uint32_t j=nativeFogOffsets[i];j<nativeFogOffsets[i+1];++j)visible &= nativeFogOwners[j]==(unsigned)blockedIndex+1;
        const unsigned expected=visible?0:255;
        nativeFog &= GetImageColor(fogPixels,(int)i%fw,(int)i/fw).a==expected;
    }
    check(nativeFog && mixedSamples>100,"unknown adjacent surfaces remain shrouded, including every mixed-owner fog pixel");
    if(fogPixels.data)UnloadImage(fogPixels);
    std::fill(world.map.fog[0].begin(),world.map.fog[0].end(),FOG_VISIBLE);

    // Corrupt imports validate in a temporary Map and cannot erase the live one.
    std::ifstream input(mapPath,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    bool rejects=true;
    for(int which=0;which<3;++which) {
        auto damaged=bytes;if(which==0)damaged.resize(damaged.size()/2);else if(which==1)damaged.push_back(1);else damaged[0]='X';
        const auto badPath=(std::filesystem::path(outputDir)/("bad-"+std::to_string(which)+".nmap")).string();
        {std::ofstream output(badPath,std::ios::binary);output.write(damaged.data(),damaged.size());}
        const auto old=world.checksum();std::vector<Vec2i> starts;
        rejects &= !loadNativeMap(badPath.c_str(),world.map,1,starts) && world.checksum()==old;
    }
    check(rejects,"truncated, trailing and invalid native imports leave the live map intact");

    shotAtDisplaySize=true;SetTargetFPS(0);camZoom=1;sel.clear();selBuilding=INVALID_EID;msg.clear();msgTimer=0;
    struct View {const char* name;int x,y;};
    for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}})for(View view: {View{"liberty-island",61,72},View{"new-york",49,35},View{"allied-base",71,37}}) {
        SetWindowSize(size.x,size.y);render();int sx,sy;tileToScreen(view.x,view.y,sx,sy);
        camX=sx-(size.x-sidebarW)/2.f;camY=sy-heightScreenY(world.map.at(view.x,view.y).height)-size.y/2.f;
        fogMaskTick=-1;
        shotFile=(std::filesystem::path(outputDir)/(std::string(view.name)+"-"+std::to_string(size.x)+".png")).string();
        const auto checksum=world.checksum();render();check(world.checksum()==checksum,"actual native-map frame leaves simulation unchanged");
    }
    std::printf("NATIVE_MAP failures=%d cells=%d water=%d blocked=%d\n",failures,present,water,blocked);return failures;
}
