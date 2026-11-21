#include "game_regression.h"
#include "game/world_priv.h"
#include "game/script.h"
#include "core/content.h"
#include <filesystem>
#include <fstream>
#include <cstdio>

int GameRegression::nativeSceneRegression(const char* outputDir) {
    const std::string path=contentResolve("assets/maps/native/all01t/scene.ini");
    if(path.empty() || !FileExists(path.c_str())){std::printf("NATIVE_SCENE SKIP: export the original actor scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* text){std::printf("NATIVE_SCENE %s: %s\n",ok?"PASS":"FAIL",text);if(!ok)++failures;};
    world=World{};g_script.shutdown();localPlayer=0;campaignMission=-1;phase=Phase::InGame;gameOver=victory=false;ais.clear();
    const bool loaded=world.init(64,64,17,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true) && world.loadNativeScene(path.c_str());
    check(loaded,"the authored scene loads transactionally over the original terrain");if(!loaded)return failures;
    auto actor=[&](const char* id){for(EID i=0;i<(EID)world.ents.size();++i)if(world.ents[i].nativeIdentity.definition->id==id)return i;return INVALID_EID;};
    int buildings=0,units=0,terrain=0,overlays=0,bridges=0;
    for(const auto& e:world.ents)if(e.alive) {
        const int kind=e.nativeIdentity.definition->kind;
        if(kind){terrain+=kind==1;overlays+=kind==2;bridges+=kind==3;}
        else {buildings+=e.isBuilding;units+=!e.isBuilding;}
    }
    check(buildings==190 && units==59 && world.nativeTypes.size()==109 && world.numPlayers==8,
          "all 249 placed actors retain their distinct definitions alongside source scenery types");
    check(terrain==344 && overlays==136 && bridges==9 && world.ents.size()==738,
          "all 344 terrain objects, 130 sandbag segments, six rocks and nine bridge segments are instantiated");
    const EID statue=actor("CANEWY04"),tanya=actor("TANY");
    check(world.valid(statue) && world.valid(tanya) && bldDef(world.ents[statue]).hp==10000 && world.ents[statue].hp==5117
        && bldDef(world.ents[statue]).w==3 && bldDef(world.ents[statue]).h==3 && unitDef(world.ents[tanya]).hp==250,
        "map-local Statue of Liberty and Tanya health override the generic/skirmish definitions");
    const auto initial=world.checksum();const int nativeTanya=world.ents[tanya].nativeIdentity.type;
    check(world.spawnNativeActor(nativeTanya,0,1e30f,1,256,0,"invalid","","")==INVALID_EID
        && world.spawnNativeActor(nativeTanya,0,1,1,256,256,"invalid","","")==INVALID_EID
        && world.checksum()==initial,"out-of-map finite coordinates and invalid facings reject before allocation or integer conversion");
    check(world.players[0].money==4000 && world.players[5].country==Country::Germany && world.nativeColors[5]==0x838383
        && world.isAllied(0,5) && world.isEnemy(5,1) && world.isAllied(1,6) && world.isEnemy(6,1),
        "authored credits, countries and directed house alliances replace the all-AI alliance shortcut");
    auto& monument=world.ents[statue];WeaponDef weapon=unitDef(UnitType::Grizzly).weapon;
    check(monument.nativeIdentity.definition->armor==Armor::Special1
        && weaponMultiplier(weapon,monument)==weaponVsArmor(weapon,Armor::Special1,false,true),"original building armor participates in actual weapon target multipliers");
    bool placed=true;for(int y=69;y<72;++y)for(int x=61;x<64;++x)placed &= world.bldOcc[world.cellIdx(x,y)]==statue+1;
    check(placed,"the monument blocks its original 3x3 footprint");
    bool art=true;for(const auto& e:world.ents)if(e.isBuilding || e.utype==UnitType::NativeVehicle)art &= nativeActorSprite(e).tex.id!=0;
    check(art,"every original structure and civilian vehicle has its own loadable artwork");
    failures+=nativeSceneryChecks(outputDir);
    {
        World progression=world;progression.map.bldOccRef=&progression.bldOcc;
        for(int i=0;i<5;++i) {
            const auto victim=progression.spawnUnit(1,UnitType::Grizzly,63.5f,108.5f);
            progression.damage(victim,100000,0,tanya);
        }
        check(unitDef(progression.ents[tanya]).cost==1000 && progression.ents[tanya].veterancyValue==3500
            && progression.ents[tanya].vetRank==1,"native Tanya promotes using her original 1000-credit cost rather than the 1500-credit skirmish template");
    }
    // Keep the original scene for reference frames; use a copy for mechanics.
    World mechanics=world;mechanics.map.bldOccRef=&mechanics.bldOcc;
    const EID carrier=mechanics.spawnUnit(0,UnitType::IFV,63.5f,108.5f);
    World::Cmd board{};board.type=World::Cmd::Board;board.ids={tanya};board.a=carrier;mechanics.applyCmd(0,board);
    for(int i=0;i<90 && mechanics.valid(tanya);++i)mechanics.update();
    check(mechanics.valid(carrier) && mechanics.ents[carrier].cargo.size()==1
        && mechanics.ents[carrier].cargo.front().nativeIdentity.definition
        && unitDef(mechanics.ents[carrier].cargo.front()).hp==250,"boarding retains original type, tags and per-unit definition");
    FILE* file=tmpfile();World restored;bool resumed=file && mechanics.saveGame(file);
    if(resumed){rewind(file);resumed=restored.loadGame(file);}if(file)fclose(file);
    for(int i=0;i<120 && resumed;++i){mechanics.update();restored.update();resumed=mechanics.checksum()==restored.checksum();}
    check(resumed,"World v31 restores native definitions, house relations and loaded passengers for 120 ticks");
    if(resumed) {
        const auto released=restored.orderUnload({carrier});bool found=false;
        for(const auto& e:restored.ents)if(e.alive && !e.isBuilding && e.nativeIdentity.definition
            && e.nativeIdentity.definition->id=="TANY")found=unitDef(e).hp==250 && e.hp==250 && !e.nativeIdentity.object.empty();
        check(released.released==1 && found,"unloading restores the authored unit with full native health and its original object identity");
    }
    {
        World overlap=world;overlap.map.bldOccRef=&overlap.bldOcc;int light=-1;
        for(int i=0;i<(int)overlap.nativeTypes.size();++i)if(overlap.nativeTypes[i]->isBuilding && !overlap.nativeTypes[i]->blocksMovement){light=i;break;}
        const EID prop=overlap.spawnNativeActor(light,5,61,69,256,0,"overlay-light","","");
        if(overlap.valid(prop))overlap.kill(prop,false);
        check(prop!=INVALID_EID && overlap.bldOcc[overlap.cellIdx(61,69)]==statue+1,
              "removing a nonblocking source object cannot erase another building's footprint");
    }
    World diplomacy=world;diplomacy.map.bldOccRef=&diplomacy.bldOcc;diplomacy.nativeAllies[0]|=1u<<1;
    file=tmpfile();bool relations=file && diplomacy.saveGame(file);
    if(relations){rewind(file);relations=restored.loadGame(file);}if(file)fclose(file);
    check(relations && restored.isAllied(0,1) && restored.checksum()==diplomacy.checksum(),"changed native diplomacy survives independently of the initial source relations");
    const auto original=world.checksum();bool malformed=true;
    for(const std::string bad:{std::string{},world.nativeScenarioSource.substr(0,world.nativeScenarioSource.find("[Type.1]"))}) {
        malformed &= !world.restoreNativeDefinitions(bad) && world.checksum()==original;
    }
    check(malformed,"incomplete native definitions cannot replace the live catalog");
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);syncFogKnowledge();
    const auto before=world.checksum();const auto save=(std::filesystem::path(outputDir)/"native-scene.sav").string();
    check(saveGameFile(save.c_str()) && loadGameFile(save.c_str()) && world.checksum()==before,
          "application v10 preserves original observed building types and the complete native scene");
    bakeTerrain();shotAtDisplaySize=true;SetTargetFPS(0);camZoom=1;msg.clear();msgTimer=0;sel.clear();selBuilding=INVALID_EID;
    struct View{const char* name;int x,y;};
    for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}})for(auto view:{View{"liberty",61,70},View{"wall-street",19,54},View{"allied-base",71,36}}) {
        SetWindowSize(size.x,size.y);render();int x,y;tileToScreen(view.x,view.y,x,y);
        camX=x-(size.x-sidebarW)/2.f;camY=y-heightScreenY(world.map.at(view.x,view.y).height)-size.y/2.f;fogMaskTick=-1;
        shotFile=(std::filesystem::path(outputDir)/(std::string(view.name)+"-"+std::to_string(size.x)+".png")).string();
        const auto state=world.checksum();render();check(state==world.checksum(),"actual native actor frame leaves World and RNG unchanged");
    }
    std::printf("NATIVE_SCENE failures=%d actors=%d scenery=%d houses=%d\n",failures,buildings+units,terrain+overlays+bridges,world.numPlayers);return failures;
}
