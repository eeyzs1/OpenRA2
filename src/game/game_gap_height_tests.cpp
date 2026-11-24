#include "game_regression.h"
#include "game/native_scene_contract.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
constexpr int side=64,area=side*side,caseCount=21;
struct Region {int x0=-1,y0=-1,x1=-1,y1=-1,height=0;};
struct GroundCase {
    int id=-1,height=0,gaps=0;
    std::array<Region,2> regions;
    std::array<Vec2i,2> positions;
    uint64_t terrain=0;
    int active=0,afterFirst=0,onlyFirst=0;
};
using Mask=std::vector<uint8_t>;
using Phases=std::array<Mask,4>;
bool fixtures(std::vector<GroundCase>& cases,std::vector<Phases>& masks) {
    std::ifstream input("tests/fixtures/ra2-gap-height-cases.csv");std::string line;
    bool ok=bool(std::getline(input,line));
    while(std::getline(input,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);GroundCase c;
        row>>c.id>>c.height;
        for(auto& r:c.regions)row>>r.x0>>r.y0>>r.x1>>r.y1>>r.height;
        row>>c.gaps>>c.positions[0].x>>c.positions[0].y>>c.positions[1].x>>c.positions[1].y
            >>std::hex>>c.terrain>>std::dec>>c.active>>c.afterFirst>>c.onlyFirst;
        ok &= bool(row) && c.id==(int)cases.size() && c.height>=0 && c.height<=15 && c.gaps>=1 && c.gaps<=2;
        for(const auto& r:c.regions)ok &= r.x0<0 || (r.x0<r.x1 && r.y0>=0 && r.y0<r.y1 && r.x1<=side && r.y1<=side && r.height>=0 && r.height<=15);
        for(int i=0;i<c.gaps && i<2;++i)ok &= c.positions[i].x>=0 && c.positions[i].x<side && c.positions[i].y>=0 && c.positions[i].y<side;
        cases.push_back(c);
    }
    ok &= cases.size()==caseCount;if(!ok)return false;masks.resize(caseCount);
    for(auto& phases:masks)for(auto& mask:phases)mask.assign(area,0);
    FILE* captured=std::fopen("tests/fixtures/ra2-gap-height-masks.csv","r");
    if(!captured)return false;
    char header[96];ok &= std::fgets(header,sizeof(header),captured)!=nullptr;
    int c=0,phase=0,x=0,y=0,rows=0;
    while(std::fscanf(captured,"%d,%d,%d,%d",&c,&phase,&x,&y)==4) {
        if(c<0 || c>=caseCount || phase<0 || phase>=4 || x<0 || x>=side || y<0 || y>=side){ok=false;continue;}
        auto& value=masks[c][phase][y*side+x];ok &= value==0;value=1;++rows;
    }
    ok &= std::feof(captured)!=0 && rows==9294;std::fclose(captured);
    if(cases.size()==caseCount)for(const auto& item:cases) {
        const auto count=[&](int p){return std::count(masks[item.id][p].begin(),masks[item.id][p].end(),uint8_t(1));};
        ok &= count(0)==item.active && count(1)==item.afterFirst && count(2)==0 && count(3)==item.onlyFirst;
    }
    return ok;
}
uint64_t terrainHash(const World& world) {
    uint64_t value=14695981039346656037ull;
    for(const auto& cell:world.map.cells){value^=cell.height;value*=1099511628211ull;}
    return value;
}
bool sell(World& world,EID id) {
    World::Cmd command{};command.type=World::Cmd::SellBuilding;command.ids={id};
    world.bindCommand(command);world.applyCmd(0,command);
    return std::any_of(world.presentationEvents.begin(),world.presentationEvents.end(),[](const auto& e) {
        return e.kind==PresentationKind::BuildingOrder && e.building.action==BuildingAction::Sell && e.building.accepted;
    });
}
void clearFeedback(World& world){world.presentationEvents.clear();world.evaQueue.clear();}
struct Comparison {int covered=0,mismatches=0;};
Comparison compare(const World& world,const Mask& expected,int phase,std::ostream& observed) {
    Comparison result;
    for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
        const auto fog=world.map.fogAt(1,x,y);const bool hidden=fog==FOG_UNSEEN;
        result.covered+=hidden;result.mismatches+=fog!=(expected[y*side+x]?FOG_UNSEEN:FOG_VISIBLE);
        if(hidden)observed<<phase<<','<<x<<','<<y<<'\n';
    }
    return result;
}
bool replayArchived(const std::filesystem::path& directory,const std::string& prefix,int requiredVersion,
                    int ticks,const char* saveSuffix,bool scheduled) {
    World world;FILE* input=std::fopen((directory/(prefix+saveSuffix)).string().c_str(),"rb");
    bool exact=input && world.loadGame(input);if(input)std::fclose(input);
    std::ifstream trace(directory/(prefix+".trace")),final(directory/(prefix+".final"));
    uint64_t expected=0;exact &= world.nativeRulesBaseVersion==requiredVersion && bool(trace>>expected) && world.checksum()==expected;
    std::vector<std::pair<int,Vec2i>> orders;
    if(scheduled) {
        std::ifstream commands(directory/(prefix+".commands"));int at=0,x=0,y=0;std::string line;
        if(prefix=="gap-area") {
            if(commands>>at)orders.push_back({at,{32,32}});
        } else while(std::getline(commands,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            if(row>>at>>x>>y)orders.push_back({at,{x,y}});
            else return false;
        }
        exact &= !orders.empty();
    }
    for(int frame=1;frame<=ticks && exact;++frame) {
        for(const auto& order:orders)if(order.first==frame)exact &= sell(world,world.bldAt(order.second.x,order.second.y));
        world.update();exact=exact && bool(trace>>expected) && world.checksum()==expected;clearFeedback(world);
    }
    exact &= bool(final>>expected) && world.checksum()==expected;
    return exact;
}
}

int GameRegression::gapHeightRegression(const char* outputDir) {
    std::vector<GroundCase> cases;std::vector<Phases> masks;
    if(!fixtures(cases,masks)){std::printf("GAP_HEIGHT FAIL invalid captured original fixture\n");return 1;}
    const std::filesystem::path output=outputDir;std::filesystem::create_directories(output);
    int failures=0;
    for(const auto& item:cases) {
        World w;bool staged=w.init(side,side,20261123,2,0,{Faction::Allies,Faction::Allies},0,nullptr,
            false,false,"assets/rules/profiles/ra2.ini");
        if(!staged){++failures;std::printf("GAP_HEIGHT FAIL case=%d initialization\n",item.id);continue;}
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells){cell=Cell{};cell.height=(uint8_t)item.height;}
        for(const auto& r:item.regions)if(r.x0>=0)
            for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x)w.map.at(x,y).height=(uint8_t)r.height;
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        for(auto& player:w.players){player.money=0;player.defeated=false;}
        w.cratesEnabled=false;w.crates.clear();w.aiAlliance=false;w.sharedVision=false;
        std::array<World::EntityRef,2> gaps;
        for(int i=0;i<item.gaps;++i)gaps[i]=w.entityRef(w.spawnBuilding(0,BldType::GapGenerator,item.positions[i].x,item.positions[i].y,true));
        w.spawnBuilding(0,BldType::PowerPlant,54,54,true);
        w.spawnBuilding(1,BldType::PowerPlant,2,2,true);w.spawnBuilding(1,BldType::SpySat,5,2,true);w.update();
        staged &= terrainHash(w)==item.terrain && w.nativeRulesBaseVersion==nativeCurrentRulesSceneVersion
            && !w.players[0].lowPower() && !w.players[1].lowPower() && w.hasBld(1,BldType::SpySat);
        for(int i=0;i<item.gaps;++i)staged &= w.valid(gaps[i]);
        const auto prefix="height-"+std::to_string(item.id);
        std::ofstream observed(output/(prefix+"-observed.csv"));observed<<"phase,x,y\n";
        std::array<Comparison,4> comparison;comparison[0]=compare(w,masks[item.id][0],0,observed);
        bool control=true;
        if(item.gaps==2) {
            const auto original=w.checksum();World onlyFirst=w;onlyFirst.map.bldOccRef=&onlyFirst.bldOcc;
            control=sell(onlyFirst,gaps[1].id);
            for(int frame=0;frame<120 && onlyFirst.valid(gaps[1]);++frame){onlyFirst.update();clearFeedback(onlyFirst);}
            onlyFirst.update();clearFeedback(onlyFirst);
            control &= !onlyFirst.valid(gaps[1]) && onlyFirst.valid(gaps[0]) && w.checksum()==original;
            comparison[3]=compare(onlyFirst,masks[item.id][3],3,observed);
            std::ofstream(output/(prefix+"-control.final"))<<onlyFirst.checksum()<<'\n';
        }
        FILE* ordered=std::fopen((output/(prefix+"-ordered.sav")).string().c_str(),"w+b");
        World resumed,midway;bool exact=ordered && w.saveGame(ordered),middle=true,middleActive=false;
        if(exact){std::rewind(ordered);exact=resumed.loadGame(ordered) && resumed.checksum()==w.checksum();}
        if(ordered)std::fclose(ordered);
        std::ofstream trace(output/(prefix+".trace"));trace<<w.checksum()<<'\n';
        std::ofstream commands(output/(prefix+".commands"));commands<<120<<','<<item.positions[0].x<<','<<item.positions[0].y<<'\n';
        if(item.gaps==2)commands<<240<<','<<item.positions[1].x<<','<<item.positions[1].y<<'\n';
        std::ofstream middleTrace;
        bool admitted=true,firstSample=false,clearedSample=false;int firstGone=0,lastGone=0;
        for(int frame=1;frame<=600;++frame) {
            const int index=frame==120?0:frame==240 && item.gaps==2?1:-1;
            if(index>=0) {
                admitted &= sell(w,gaps[index].id);
                if(exact)exact=sell(resumed,gaps[index].id);
                if(middleActive && middle)middle=sell(midway,gaps[index].id);
            }
            w.update();if(exact){resumed.update();exact=w.checksum()==resumed.checksum();}
            if(middleActive && middle){midway.update();middle=w.checksum()==midway.checksum();}
            trace<<w.checksum()<<'\n';
            if(middleActive)middleTrace<<w.checksum()<<'\n';
            if(!firstGone && !w.valid(gaps[0]))firstGone=frame;
            const bool allGone=!w.valid(gaps[0]) && (item.gaps==1 || !w.valid(gaps[1]));
            if(!lastGone && allGone)lastGone=frame;
            if(!firstSample && firstGone && frame>firstGone) {
                control &= item.gaps==1 || w.valid(gaps[1]);
                comparison[1]=compare(w,masks[item.id][1],1,observed);firstSample=true;
            }
            if(!clearedSample && lastGone && frame>lastGone) {
                if(item.gaps==2)comparison[2]=compare(w,masks[item.id][2],2,observed);
                clearedSample=true;
            }
            if(frame==180 && item.gaps==2) {
                FILE* save=std::fopen((output/(prefix+"-midway.sav")).string().c_str(),"w+b");
                middle=save && w.saveGame(save);
                if(middle){std::rewind(save);middle=midway.loadGame(save) && midway.checksum()==w.checksum();}
                if(save)std::fclose(save);
                middleActive=true;middleTrace.open(output/(prefix+"-midway.trace"));middleTrace<<w.checksum()<<'\n';
            }
            clearFeedback(w);clearFeedback(resumed);clearFeedback(midway);
        }
        std::ofstream(output/(prefix+".final"))<<w.checksum()<<'\n';
        bool geometry=true;for(const auto& result:comparison)geometry &= result.mismatches==0;
        const bool cleared=firstSample && clearedSample && terrainHash(w)==item.terrain;
        const bool ok=staged && geometry && control && admitted && exact && middle && cleared;failures+=!ok;
        std::printf("GAP_HEIGHT %s case=%d scene=%d staged=%d geometry=%d active=%d/%d mismatch=%d,%d,%d,%d control=%d admitted=%d replay=%d midway=%d cleared=%d removed=%d,%d\n",
            ok?"PASS":"FAIL",item.id,w.nativeRulesBaseVersion,staged,geometry,comparison[0].covered,item.active,
            comparison[0].mismatches,comparison[1].mismatches,comparison[2].mismatches,comparison[3].mismatches,
            control,admitted,exact,middle,cleared,firstGone,lastGone);
    }
    for(const auto& [key,version]:{std::pair{"OPENRA2_LEGACY_GAP52_INPUT_DIR",52},
                                  std::pair{"OPENRA2_LEGACY_GAP53_INPUT_DIR",53}})
    if(const char* historical=std::getenv(key)) {
        for(const char* name:{"gap-0","gap-1","gap-2","gap-3","gap-4","gap-5","gap-7","gap-8","gap-depart","gap-area"}) {
            const std::string prefix=name;
            const bool exact=replayArchived(historical,prefix,version,600,prefix=="gap-depart"?"-midway.sav":"-ordered.sav",prefix=="gap-area");
            failures+=!exact;std::printf("GAP_PREVIOUS%d %s name=%s exact=%d\n",version,exact?"PASS":"FAIL",name,exact);
        }
    }
    for(const auto& [key,version]:{std::pair{"OPENRA2_LEGACY_GAP_HEIGHT52_INPUT_DIR",52},
                                  std::pair{"OPENRA2_LEGACY_GAP_HEIGHT53_INPUT_DIR",53}})
    if(const char* historical=std::getenv(key)) {
        for(int i=0;i<caseCount;++i) {
            const auto prefix="height-"+std::to_string(i);
            const bool exact=replayArchived(historical,prefix,version,600,"-ordered.sav",true);
            failures+=!exact;std::printf("GAP_HEIGHT_LEGACY%d %s case=%d exact=%d\n",version,exact?"PASS":"FAIL",i,exact);
        }
    }
    return failures;
}
