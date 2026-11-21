#include "game_regression.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

int GameRegression::nativeDemolitionRegression(const char* outputDir) {
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str())){std::printf("NATIVE_DEMOLITION SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_DEMOLITION %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    g_script.shutdown();ais.clear();world=World{};localPlayer=0;phase=Phase::InGame;campaignMission=-1;gameOver=victory=false;
    const auto load=[&](const std::string& file){return world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        && world.loadNativeScene(file.c_str());};
    if(!load(scene)){check(false,"the source demolition profile loads");return failures;}
    World initial=world;initial.map.bldOccRef=&initial.bldOcc;
    const auto reset=[&](){world=initial;world.map.bldOccRef=&world.bldOcc;};
    const auto runtime=[&](const char* key){const int definition=world.nativeTriggerIndex(key);
        for(int i=0;i<(int)world.nativeEvents.triggers.size();++i)if(world.nativeEvents.triggers[i].definition==definition)return i;return -1;};
    const auto resume=[&](int frames) {
        FILE* file=tmpfile();World restored;bool exact=file && world.saveGame(file);
        if(exact){rewind(file);exact=restored.loadGame(file);}if(file)fclose(file);
        exact &= world.checksum()==restored.checksum();
        for(int i=0;i<frames && exact;++i){world.update();restored.update();exact &= world.checksum()==restored.checksum();}return exact;
    };
    const auto& demolition=world.nativeEventDefinition->demolition;
    check(demolition.available && demolition.warhead=="Super" && demolition.bridgeStrength==1500 && demolition.flightLevel==1500
        && demolition.spread==0 && demolition.percentAtMax==1 && !demolition.wall && !demolition.wood
        && std::all_of(demolition.verses.begin(),demolition.verses.end(),[](float v){return v==1;}),
        "the actual C4Warhead, bridge strength, default spread, defenses and eleven verses come from source rules");
    std::set<EID> oldBridges;for(const auto& b:world.nativeBridgeBlueprints)oldBridges.insert(world.liveBridgeSegment(b));
    const size_t deck=std::count_if(world.map.bridgeCells.begin(),world.map.bridgeCells.end(),[](const auto& cell){return cell.owner>=0;});
    check(oldBridges.size()==9 && deck==27 && !world.nativeBridgesDestroyable,"the unstarted authored scene retains its nine intact sections and protected ordinary bridge damage");
    world.startNativeEvents();world.update();const int breaker=runtime("080777BC");
    bool opened=breaker>=0 && world.nativeEvents.triggers[breaker].firings==1 && world.nativeEvents.triggers[breaker].blockedAction==-1
        && !world.map.hasBridgeDecks() && !world.nativeBridgesDestroyable;
    for(int group=0;group<(int)world.nativeBridgeGroups.size();++group)opened &= world.bridgeNeedsRepair(group);
    for(EID id:oldBridges)opened &= !world.valid(id);
    check(opened && resume(20),"the original zero-time ten-action list collapses both spans, without enabling ordinary bridge damage, and resumes identically");
    World broken=world;broken.map.bldOccRef=&broken.bldOcc;
    const auto save=(std::filesystem::path(outputDir)/"authored-broken-bridges.sav").string();const auto hash=world.checksum();
    check(saveGameFile(save.c_str()) && loadGameFile(save.c_str()) && world.checksum()==hash,"application v10 / World43 restores the source blast profile and destroyed topology");
    const auto old="review_out/native-campaign/legacy-native41-active.sav";
    if(FileExists(old)) {
        const bool loaded=loadGameFile(old);
        check(loaded && world.nativeEvents.active && !world.nativeEventDefinition->demolition.available && resume(20),
            "an actual pre-upgrade World41/Scene9 event save retains its explicit older capability boundary and resumes exactly");
    }

    // A counter-scene adds one waypoint and replaces only the named blast
    // list. Every other source record remains present; runtime triggers are
    // disabled solely to isolate blast recipients and delayed persistence.
    const auto set=[&](std::string& source,const char* section,const char* key,const std::string& value) {
        const auto header=source.find("["+std::string(section)+"]");if(header==std::string::npos)return false;
        const auto begin=source.find('\n',header)+1,end=source.find("\n[",begin);auto at=source.find(std::string(key)+" = ",begin);
        if(at==std::string::npos || (end!=std::string::npos && at>=end))source.insert(begin,std::string(key)+" = "+value+"\n");
        else {at=source.find('=',at)+2;source.replace(at,source.find('\n',at)-at,value);}return true;
    };
    const auto clear=[&]() {
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id)) {
            const auto& d=world.ents[id].nativeIdentity.definition;
            if(d->kind || d->bridgeRepairHut || d->id=="CANEWY04")continue;world.kill(id,false);
        }
        world.projs.clear();world.effects.clear();world.timedBombs.clear();
    };
    reset();clear();Vec2i origin{-1,-1};
    for(int y=6;y<world.map.h-6 && origin.x<0;++y)for(int x=6;x<world.map.w-6 && origin.x<0;++x) {
        bool free=true;for(int dy=-3;dy<=3 && free;++dy)for(int dx=-3;dx<=3 && free;++dx)
            free=world.map.surfacePassable(x+dx,y+dy,0,0,true,true) && world.map.at(x+dx,y+dy).height==world.map.at(x,y).height;
        if(free)origin={x,y};
    }
    check(origin.x>=0,"a level source-terrain counter arena is available");if(origin.x<0)return failures;
    const auto counter=[&](int waypoint,float spread) {
        auto source=initial.nativeScenarioSource;
        if(waypoint==4094)set(source,"NativeEvent.waypoints","4094",std::to_string(origin.y*1000+origin.x));
        set(source,"NativeEvent.actions","080777bc","1,63,0,"+std::to_string(waypoint)+",0,0,0,0,A");
        set(source,"NativeEvent.events","080777bc","1,13,0,2");
        set(source,"NativeEvent.Demolition","CellSpread",std::to_string(spread));
        const auto path=(std::filesystem::path(outputDir)/("counter-blast-"+std::to_string(waypoint)+".ini")).string();
        {std::ofstream out(path,std::ios::binary);out<<source;}
        if(!load(path))return false;clear();if(!world.startNativeEvents())return false;
        const int active=runtime("080777BC");for(int i=0;i<(int)world.nativeEvents.triggers.size();++i)world.nativeEvents.triggers[i].enabled=i==active;
        return active>=0;
    };
    if(counter(4094,0)) {
        const EID victim=world.spawnUnit(0,UnitType::Conscript,origin.x+.5f,origin.y+.5f);
        const EID protectedUnit=world.spawnUnit(1,UnitType::Conscript,origin.x+.5f,origin.y+.5f);
        const EID adjacent=world.spawnUnit(1,UnitType::Conscript,origin.x+1.5f,origin.y+.5f);
        for(EID id:{victim,protectedUnit,adjacent}){world.ents[id].hp=10000;world.ents[id].tpSick=10000;}
        world.ents[protectedUnit].invuln=10000;
        check(resume(30),"a pending numeric-waypoint blast list survives before its elapsed-time deadline");
        while(world.tick<59)world.update();const auto sequence=world.presentationSequence;world.update();int hits=0;
        for(const auto& e:world.presentationEvents)if(e.sequence>=sequence && e.kind==PresentationKind::Impact && e.entity==victim)hits+=e.damage==100;
        check(world.ents[victim].hp==9500 && hits==5 && world.ents[protectedUnit].hp==10000 && world.ents[adjacent].hp==10000
            && world.nativeEvents.triggers[runtime("080777BC")].firings==1,
            "five separate 100-point cell-local blasts use the numeric waypoint and ignore the mismatched A label, while respecting invulnerability");
        bool noCredit=true;for(auto amount:world.combatDamage)noCredit &= amount==0;
        check(noCredit && world.ents[victim].target==INVALID_EID && resume(10),"unattributed script damage creates no combat credit or retaliation and its aftermath resumes exactly");
    } else check(false,"the numeric-waypoint blast counter scene loads");
    if(counter(79,1)) {
        const auto point=world.nativeWaypoint(79);
        const EID above=world.spawnUnit(0,UnitType::AmphTransport,point.x+.5f,point.y+.5f);
        const EID below=world.spawnUnit(1,UnitType::SeaScorpion,point.x-.5f,point.y+.5f);
        const EID tank=world.spawnUnit(0,UnitType::Rhino,point.x+1.5f,point.y+.5f);
        for(EID id:{above,below,tank}){world.ents[id].hp=10000;world.ents[id].tpSick=10000;}
        world.ents[above].invuln=world.ents[tank].invuln=10000;world.ents[above].vetRank=2;world.ents[above].crateArmorBoost=10000;
        const auto target=world.map.bridgeCells[world.cellIdx(point.x,point.y)].owner;int group=-1;
        for(int g=0;g<(int)world.nativeBridgeGroups.size();++g)for(int b:world.nativeBridgeGroups[g])if(world.liveBridgeSegment(world.nativeBridgeBlueprints[b])==target)group=g;
        const bool placed=world.ents[above].navLayer==1 && world.ents[tank].navLayer==1 && world.ents[below].navLayer==0;
        while(world.tick<60)world.update();
        bool otherIntact=true;for(int g=0;g<(int)world.nativeBridgeGroups.size();++g)if(g!=group)otherIntact &= !world.bridgeNeedsRepair(g);
        check(placed && world.valid(above) && world.ents[above].navLayer==0 && world.ents[above].hp==8500
            && world.valid(below) && world.ents[below].hp==10000 && !world.valid(tank) && otherIntact && resume(5),
            "deck-height blasts leave the lower boat intact; scripted collapse applies source forced damage, removes unsupported tanks and preserves the other span");
    } else check(false,"the elevated blast/collapse counter scene loads");

    // Original entry/handover/gate chain, followed by a real engineer repair.
    world=broken;world.map.bldOccRef=&world.bldOcc;
    while(world.tick<2400 && (world.tick<1650 || world.nativeEvents.inputLocked))world.update();
    const int entry=world.nativeTriggerIndex("096564CC");int tag=-1;
    for(int i=0;i<(int)world.nativeEventDefinition->tags.size();++i)if(world.nativeEventDefinition->tags[i].trigger==entry)tag=i;
    Vec2i cell{-1,-1},outside{-1,-1};
    for(const auto& [point,binding]:world.nativeEventDefinition->cells)if(binding==tag && cell.x<0)
        for(auto step:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}) {
            const Vec2i next{point.x+step.x,point.y+step.y};
            if(!world.map.surfacePassable(point.x,point.y,0,0,true,true) || !world.map.surfacePassable(next.x,next.y,0,0,true,true)
                || world.unitAtCell(point.x,point.y)>=0 || world.unitAtCell(next.x,next.y)>=0)continue;
            if(std::any_of(world.nativeEventDefinition->cells.begin(),world.nativeEventDefinition->cells.end(),[&](const auto& c){return c.first==next;}))continue;
            std::vector<Vec2i> path;if(world.map.findPath(next.x,next.y,point.x,point.y,path) && path.size()==1){cell=point;outside=next;break;}
        }
    check(cell.x>=0 && !world.nativeEvents.inputLocked,"the original opening unlocks input with both scripted bridges already broken");
    EID engineer=-1,hut=-1;bool repaired=false;
    if(cell.x>=0) {
        engineer=world.spawnUnit(0,UnitType::Engineer,outside.x+.5f,outside.y+.5f);World::Cmd move{};move.type=World::Cmd::Move;move.ids={engineer};move.x=cell.x+.5f;move.y=cell.y+.5f;world.applyCmd(0,move);
        for(int i=0;i<240 && world.nativeEvents.triggers[runtime("0965E07C")].firings==0;++i)world.update();
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->bridgeRepairHut){hut=id;break;}
        const int group=world.bridgeForHut(hut);World::Cmd repair{};repair.type=World::Cmd::Repair;repair.ids={engineer};repair.a=hut;repair.b=1;world.applyCmd(0,repair);
        repaired=world.nativeEvents.triggers[runtime("0965E07C")].firings==1 && group>=0 && world.ents[engineer].engineerIntent==EngineerIntent::RepairBridge;
        check(repaired && resume(30),"real entry and the authored two-second handover give the engineer gate access and a repair order that resumes en route");
        for(int i=0;i<900 && world.valid(engineer);++i)world.update();
        repaired &= !world.valid(engineer) && !world.bridgeNeedsRepair(group);
        for(int g=0;g<(int)world.nativeBridgeGroups.size();++g)if(g!=group)repaired &= world.bridgeNeedsRepair(g);
    }
    check(repaired && resume(10),"an actual engineer passes the original gate and restores only its associated author-destroyed span, without altering alliances");
    World restored=world;restored.map.bldOccRef=&restored.bldOcc;
    for(auto pair:{std::pair{"BridgeStrength = 1500","BridgeStrength = 0"},std::pair{"CellSpread = 0","CellSpread = nan"}}) {
        auto source=initial.nativeScenarioSource;auto at=source.find("[NativeEvent.Demolition]");at=source.find(pair.first,at);bool rejected=at!=std::string::npos;
        if(rejected){source.replace(at,strlen(pair.first),pair.second);World invalid=initial;invalid.map.bldOccRef=&invalid.bldOcc;rejected=!invalid.restoreNativeDefinitions(source);}
        check(rejected,"malformed script-blast rules are rejected before scene installation");
    }
    for(auto* snapshot:{&broken,&restored}) {
        world=*snapshot;world.map.bldOccRef=&world.bldOcc;gameOver=victory=false;camZoom=.85f;interpAlpha=1;showMenu=placing=dragging=targetingParadrop=false;
        sideMode=0;targetingSW=SWType::COUNT;sel.clear();selBuilding=INVALID_EID;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);fogKnowledge={};syncFogKnowledge();
        for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);render();int x,y;tileToScreen(82,51,x,y);camX=x-(SCREEN_W-sidebarW)/(2.f*camZoom);camY=y-SCREEN_H/(2.f*camZoom);
            shotFile=(std::filesystem::path(outputDir)/(std::string(snapshot==&broken?"authored-broken-":"engineer-restored-")+std::to_string(size.x)+".png")).string();
            const auto hash=world.checksum();render();check(hash==world.checksum(),"the author-destroyed and engineer-restored bridge render without mutating simulation");
        }
    }
    std::printf("NATIVE_DEMOLITION failures=%d\n",failures);return failures;
}
