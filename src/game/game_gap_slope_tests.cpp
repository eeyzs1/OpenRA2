#include "game_regression.h"
#include "game/native_map.h"
#include "game/native_scene_contract.h"
#include "game/skirmish_map.h"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
constexpr int side=64,offset=66,anchor=offset+32,caseCount=15*21;
using Mask=std::array<uint64_t,side>;
struct SlopeCase {int level=0,slope=0,z=0,effective=0,hidden=0;Mask mask{};};
bool fixtures(std::vector<SlopeCase>& cases) {
    std::ifstream input("tests/fixtures/ra2-gap-slope-masks.csv");std::string line;
    bool ok=bool(std::getline(input,line));
    while(std::getline(input,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);SlopeCase c;std::string bits,extra;
        row>>c.level>>c.slope>>c.z>>c.effective>>c.hidden>>bits;
        if(!row || row>>extra || bits.size()!=side*16)return false;
        ok &= c.level==(int)cases.size()/21 && c.slope==(int)cases.size()%21 && c.z>=0 && c.effective==c.z/104;
        int count=0;
        for(int y=0;y<side;++y) {
            const char* first=bits.data()+y*16;const auto parsed=std::from_chars(first,first+16,c.mask[y],16);
            ok &= parsed.ec==std::errc{} && parsed.ptr==first+16;count+=std::popcount(c.mask[y]);
        }
        ok &= count==c.hidden;cases.push_back(c);
    }
    return ok && cases.size()==caseCount;
}
bool sell(World& world) {
    World::Cmd command{};command.type=World::Cmd::SellBuilding;command.ids={world.bldAt(anchor,anchor)};
    world.bindCommand(command);world.applyCmd(0,command);
    return std::any_of(world.presentationEvents.begin(),world.presentationEvents.end(),[](const auto& e) {
        return e.kind==PresentationKind::BuildingOrder && e.building.action==BuildingAction::Sell && e.building.accepted;
    });
}
void clearFeedback(World& world){world.presentationEvents.clear();world.evaQueue.clear();}
Mask observed(const World& world,int& mismatches,const Mask& expected) {
    Mask result{};mismatches=0;
    for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
        const auto fog=world.map.fogAt(1,offset+x,offset+y);
        if(fog==FOG_UNSEEN)result[y]|=uint64_t(1)<<x;
        mismatches+=fog!=((expected[y]&(uint64_t(1)<<x))?FOG_UNSEEN:FOG_VISIBLE);
    }
    return result;
}
void writeMask(std::ostream& file,const Mask& mask) {
    for(uint64_t row:mask)file<<std::hex<<std::setw(16)<<std::setfill('0')<<row;
    file<<std::dec<<'\n';
}
uint64_t terrainHash(const World& world) {
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<world.map.cells.size();++i)for(int n:{int(world.map.cells[i].height),int(world.map.cells[i].present),int(world.map.native.tiles[i].ramp)}) {
        hash^=(unsigned)n;hash*=1099511628211ull;
    }
    return hash;
}
bool replayCase(const SlopeCase& c) {
    return (c.level==0 && c.slope==9) || (c.level==1 && c.slope==16)
        || (c.level==14 && c.slope==9) || (c.level==7 && c.slope==17);
}
bool archived(const std::filesystem::path& directory,const std::string& name) {
    World w;FILE* file=std::fopen((directory/(name+"-ordered.sav")).string().c_str(),"rb");
    bool exact=file && w.loadGame(file);if(file)std::fclose(file);
    std::ifstream trace(directory/(name+".trace")),final(directory/(name+".final"));uint64_t expected=0;
    exact &= w.nativeRulesBaseVersion==53 && bool(trace>>expected) && w.checksum()==expected;
    for(int frame=1;frame<=600 && exact;++frame) {
        if(frame==120)exact=sell(w);
        w.update();exact=exact && bool(trace>>expected) && w.checksum()==expected;clearFeedback(w);
    }
    return exact && bool(final>>expected) && w.checksum()==expected;
}
}

int GameRegression::gapSlopeRegression(const char* outputDir) {
    std::vector<SlopeCase> cases;
    if(!fixtures(cases)){std::printf("GAP_SLOPE FAIL invalid original fixture\n");return 1;}
    OriginalSkirmishMaps::Data data;std::string source;selectRulesChoice(RulesChoice::RA2);
    if(!OriginalSkirmishMaps::load("mp15s4",data,2)
        || !OriginalSkirmishMaps::composeRules(rulesInfo[(int)RulesChoice::RA2].source,data,source))return 1;
    const World::RulesSnapshot rules{source,true,true};World base;
    if(!base.init(data.terrain.w,data.terrain.h,20261123,0,2,{Faction::Allies,Faction::Allies},0,
        data.terrainPath.c_str(),false,true,nullptr,{},&rules))return 1;
    base.cratesEnabled=false;base.crates.clear();base.aiAlliance=false;base.sharedVision=false;
    for(EID id=0;id<(EID)base.ents.size();++id)if(base.valid(id))base.kill(id,false);
    const std::filesystem::path output=outputDir;std::filesystem::create_directories(output);
    std::ofstream matrix(output/"slope-observed.csv");matrix<<"level,slope,hidden,mismatches,rows_hex_lsb_x\n";
    int failures=0;
    for(const auto& c:cases) {
        World w=base;w.map.bldOccRef=&w.bldOcc;
        for(size_t i=0;i<w.map.cells.size();++i) {
            auto& cell=w.map.cells[i];const bool present=cell.present;cell=Cell{};cell.present=present;cell.blocked=!present;
            if(present){cell.height=(uint8_t)c.level;w.map.native.tiles[i].ramp=(uint8_t)c.slope;}
        }
        w.map.native.tiles[(size_t)anchor*w.map.w+anchor].ramp=0;
        bool staged=validateNativeMap(w.map);
        for(int y=0;y<side;++y)for(int x=0;x<side;++x)staged &= w.map.inBounds(offset+x,offset+y);
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_UNSEEN);
        for(auto& player:w.players){player.money=0;player.defeated=false;}
        const auto gap=w.entityRef(w.spawnBuilding(0,BldType::GapGenerator,anchor,anchor,true));
        w.spawnBuilding(0,BldType::PowerPlant,offset+54,offset+54,true);
        w.spawnBuilding(1,BldType::PowerPlant,offset+2,offset+2,true);
        w.spawnBuilding(1,BldType::SpySat,offset+5,offset+2,true);w.update();
        staged &= w.valid(gap) && w.nativeRulesBaseVersion==nativeCurrentRulesSceneVersion
            && !w.players[0].lowPower() && !w.players[1].lowPower() && w.hasBld(1,BldType::SpySat);
        const auto terrain=terrainHash(w);int mismatches=0;const auto mask=observed(w,mismatches,c.mask);
        int hidden=0;for(auto row:mask)hidden+=std::popcount(row);
        matrix<<c.level<<','<<c.slope<<','<<hidden<<','<<mismatches<<',';writeMask(matrix,mask);
        bool exact=true,admitted=true,cleared=true;int removed=0;
        if(replayCase(c)) {
            const auto name="slope-"+std::to_string(c.level)+"-"+std::to_string(c.slope);
            FILE* file=std::fopen((output/(name+"-ordered.sav")).string().c_str(),"w+b");World resumed;
            exact=file && w.saveGame(file);
            if(exact){std::rewind(file);exact=resumed.loadGame(file) && resumed.checksum()==w.checksum();}
            if(file)std::fclose(file);
            std::ofstream trace(output/(name+".trace"));trace<<w.checksum()<<'\n';
            for(int frame=1;frame<=600;++frame) {
                if(frame==120){admitted=sell(w);if(exact)exact=sell(resumed);}
                w.update();if(exact){resumed.update();exact=w.checksum()==resumed.checksum();}
                trace<<w.checksum()<<'\n';
                if(!removed && !w.valid(gap))removed=frame;
                clearFeedback(w);clearFeedback(resumed);
            }
            int remaining=0;observed(w,remaining,Mask{});
            cleared=removed>120 && remaining==0 && terrainHash(w)==terrain && validateNativeMap(w.map);
            std::ofstream(output/(name+".final"))<<w.checksum()<<'\n';
        }
        const bool ok=staged && !mismatches && exact && admitted && cleared;failures+=!ok;
        std::printf("GAP_SLOPE %s level=%d slope=%d scene=%d staged=%d hidden=%d/%d mismatch=%d replayCase=%d replay=%d admitted=%d cleared=%d removed=%d\n",
            ok?"PASS":"FAIL",c.level,c.slope,w.nativeRulesBaseVersion,staged,hidden,c.hidden,mismatches,replayCase(c),exact,admitted,cleared,removed);
        if(replayCase(c))if(const char* previous=std::getenv("OPENRA2_LEGACY_GAP_SLOPE53_INPUT_DIR")) {
            const auto name="slope-"+std::to_string(c.level)+"-"+std::to_string(c.slope);
            const bool old=archived(previous,name);failures+=!old;
            std::printf("GAP_SLOPE_LEGACY53 %s name=%s exact=%d\n",old?"PASS":"FAIL",name.c_str(),old);
        }
    }
    return failures;
}
