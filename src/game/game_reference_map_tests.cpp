#include "game_regression.h"
#include <cstdio>
#include <filesystem>

int GameRegression::referenceMapRegression(const char* outputDir) {
    selectRulesChoice(RulesChoice::Fusion); // This fixture covers all four extended factions.
    std::filesystem::create_directories(outputDir);
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        failures += !ok; std::printf("REFERENCE %s %s\n", ok ? "PASS" : "FAIL", name);
    };
    auto sameMap = [](const Map& a, const Map& b) {
        if (a.w != b.w || a.h != b.h || a.cells.size() != b.cells.size()) return false;
        for (size_t i=0; i<a.cells.size(); ++i) {
            const auto& x=a.cells[i]; const auto& y=b.cells[i];
            if (x.terrain!=y.terrain || x.overlay!=y.overlay || x.variant!=y.variant ||
                x.height!=y.height || x.ore!=y.ore || x.oreMax!=y.oreMax) return false;
        }
        return true;
    };
    SetTargetFPS(0); shotAtDisplaySize=true;
    auto click = [&](float x, float y) {
        sim = SimInput{}; sim.active=true; sim.pos=menuCanvasFromUi(x,y); sim.pressedL=true;
        render(); sim.pressedL=false; render();
    };
    phase=Phase::Setup; cfgReferenceMap=false; cfgMapSize=96; cfgAI=2;
    cfgGameMode=(int)SkirmishMode::NavalWar; cfgMapType=1; previewDirty=true;
    sim=SimInput{}; sim.active=true; render();
    click(320,430); click(320,408); // Open map type and select its last item.
    check(cfgReferenceMap && cfgMapType<7 && cfgMapSize==64 && cfgAI==1 &&
          cfgGameMode==(int)SkirmishMode::Battle && referenceMapReady,
          "actual setup dropdown selects local two-player land profile from naval mode");
    World expected;
    check(expected.init(64,64,previewSeed,1,1,{Faction::Allies,Faction::Soviet},0,REFERENCE_MAP,false,true) &&
          sameMap(previewMap,expected.map) && previewMap.bldOccRef==nullptr,
          "preview loads the actual fixed terrain without a dangling world occupancy pointer");
    const int languageBefore=g_lang;
    g_lang=0;
    bool labelGlyphs=true;
    for (S label : {S::MapReference,S::MsgMapUnavailable}) {
        int count=0; int* cps=LoadCodepoints(TR(label),&count);
        for (int i=0;i<count;++i) labelGlyphs=labelGlyphs && font.glyphs[GetGlyphIndex(font,cps[i])].value==cps[i];
        UnloadCodepoints(cps);
    }
    check(labelGlyphs,"Chinese map label and failure message have real font glyphs");
    for (int language=0; language<2; ++language) for (Vec2i size : {Vec2i{1280,720},Vec2i{1920,1080}}) {
        g_lang=language; SetWindowSize(size.x,size.y); render();
        shotFile=(std::filesystem::path(outputDir)/("setup_lang"+std::to_string(language)+"_"+
                  std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string(); render();
    }
    const Country countries[]={Country::America,Country::Russia,Country::China,Country::Yuri};
    for (Country country : countries) {
        selectReferenceMap(); cfgCountry=(int)country; aiCountry[0]=(int)Country::Russia;
        cfgMoney=20000; cfgColor=3; aiColor[0]=5; aiDiff[0]=2;
        cfgCrates=false; cfgSuperweapons=false; cfgShortGame=true;
        phase=Phase::Setup; render();
        const Rectangle side=menuShellSide(), mon=menuShellMonitor();
        click(side.x+side.width*0.5f,mon.y+mon.height+51.f);
        bool start=phase==Phase::InGame && world.numPlayers==2 && sameMap(world.map,expected.map) &&
            world.map.bldOccRef==&world.bldOcc && world.players[0].faction==countryFaction(country) &&
            world.players[0].money==20000 && world.players[1].money==20000 &&
            world.players[0].colorId==3 && world.players[1].colorId==5 && ais.size()==1 &&
            ais[0].difficulty==AIDiff::Hard && !world.cratesEnabled && !world.superweaponsEnabled && world.shortGame;
        EID mcvs[2]={INVALID_EID,INVALID_EID}; int units=0;
        for (EID id=0;id<(EID)world.ents.size();++id) { const auto& e=world.ents[id]; if (e.alive) {
            ++units;
            if (!e.isBuilding && e.utype==UnitType::MCV && e.player>=0 && e.player<2) mcvs[e.player]=id;
        } }
        start=start && units==10;
        for (int p=0;p<2;++p) start=start && world.valid(mcvs[p]) && world.canDeployMcv(mcvs[p]) &&
            world.ents[mcvs[p]].x==(p==0?12.5f:51.5f) && world.ents[mcvs[p]].y==(p==0?12.5f:51.5f);
        check(start, TextFormat("normal newGame preserves map, options and deployable starts for country=%d",(int)country));
        if (country==Country::China) {
            shotFile=(std::filesystem::path(outputDir)/"china_natural_start.png").string(); render();
        }
        World::Cmd deploy{}; deploy.type=World::Cmd::Deploy;
        for (int p=0;p<2;++p) { deploy.ids={mcvs[p]}; world.applyCmd(p,deploy); }
        FILE* file=tmpfile(); World restored; bool replay=file && world.saveGame(file);
        if (replay) { rewind(file); replay=restored.loadGame(file); }
        if (file) fclose(file);
        for (int i=0;i<180 && replay;++i) { world.update(); restored.update(); replay=world.checksum()==restored.checksum(); }
        check(replay,"normal fixed-map deployment continues identically after a save restore");
    }
    // Selecting a generated terrain explicitly leaves the fixed profile.
    phase=Phase::Setup; render(); click(320,430); click(320,266);
    check(!cfgReferenceMap && cfgMapType==0,"generated map selection leaves the fixed profile");
    refreshMapPreview(); newGame(previewSeed);
    check(phase==Phase::InGame && sameMap(world.map,previewMap),"generated preview and normal game still agree");
    phase=Phase::Setup; selectReferenceMap(); render();
    const Rectangle side=menuShellSide(), mon=menuShellMonitor();
    click(side.x+side.width*0.5f,mon.y+mon.height+97.f);
    check(!cfgReferenceMap,"random new map button leaves the fixed profile");
    selectReferenceMap(); render(); click(110,430); click(110,328);
    check(!cfgReferenceMap && cfgMapSize==96,"changing size leaves the fixed profile");
    World unavailable;
    check(!unavailable.init(64,64,1,1,1,{Faction::Allies,Faction::Soviet},0,
              "maps/__missing_reference_regression__.txt",false,true) && unavailable.players.empty(),
          "required missing map cannot silently start a generated replacement");
    const auto invalidPath=(std::filesystem::path(outputDir)/"invalid-map.txt").string();
    FILE* invalid=fopen(invalidPath.c_str(),"w");
    if (invalid) { std::fputs("size 16 16\n",invalid); std::fclose(invalid); }
    World malformed;
    check(invalid && !malformed.init(64,64,1,1,1,{Faction::Allies,Faction::Soviet},0,
              invalidPath.c_str(),false,true) && malformed.players.empty(),
          "required malformed map cannot silently start a generated replacement");
    g_lang=languageBefore; sim=SimInput{};
    std::printf("REFERENCE failures=%d\n",failures); return failures;
}
