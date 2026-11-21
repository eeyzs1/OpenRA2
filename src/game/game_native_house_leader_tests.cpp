#include "game_regression.h"
#include "game/native_house_geometry.h"
#include "game/native_team_leader.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace {
std::vector<char> save(const World& world) {
    std::vector<char> bytes;FILE* file=tmpfile();if(!file)return bytes;
    if(world.saveGame(file)){const long n=ftell(file);if(n>0){bytes.resize(n);rewind(file);if(fread(bytes.data(),1,bytes.size(),file)!=bytes.size())bytes.clear();}}
    fclose(file);return bytes;
}
bool load(World& world,const std::vector<char>& bytes) {
    FILE* file=tmpfile();if(!file)return false;const bool wrote=fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();rewind(file);
    const bool ok=wrote && world.loadGame(file);fclose(file);return ok;
}
std::vector<char> read(const std::string& file) {std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
std::string blockedScript(std::string source,std::string id) {
    for(char& c:id)c=(char)std::tolower((unsigned char)c);
    const auto begin=source.find("[NativeEvent.record."+id+"]");if(begin==std::string::npos)return {};
    const auto end=source.find("\n[",begin);const auto row=source.find("\n0",begin);
    if(row==std::string::npos || (end!=std::string::npos && row>end))return {};
    const auto finish=source.find('\n',row+1);if(finish==std::string::npos)return {};
    source.replace(row+1,finish-row-1,"0=255,0");return source;
}
void flat(World& world) {
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
    for(auto& cell:world.map.cells){cell=Cell{};cell.terrain=Terrain::Clear;}
    world.rebuildUnitOcc();world.rebuildAirOcc();world.cratesEnabled=false;
    for(auto& player:world.players){player.active=true;player.defeated=false;player.money=100000;}
}
}

int GameRegression::nativeHouseLeaderRegression(const char* outputDir) {
    const std::string directory=outputDir;
    for(const char* edition:{"ra2","yr"})if(!FileExists((std::string("tests/fixtures/native-nearby-v62/")+edition+"-profile.ini").c_str()))return 77;
    int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("HOUSE_LEADER %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    {
        std::ifstream fixture("tests/fixtures/native-house-center-ra2.csv");std::string line;bool exact=fixture.good();int samples=0;
        while(std::getline(fixture,line))if(!line.empty() && line[0]!='#') {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);int count=0;Vec2i expected;
            if(!(row>>count>>expected.x>>expected.y) || count<0 || count>64){exact=false;break;}
            std::vector<NativeHouseBuildingPoint> buildings(count);
            for(auto& b:buildings){int limbo=0;if(!(row>>b.x>>b.y>>b.cost>>b.health>>limbo))exact=false;b.limbo=limbo!=0;}
            exact &= nativeHouseWeightedCenter(buildings)==expected;++samples;
        }
        check(exact && samples==122,"compiled house-center arithmetic matches 122 original RA2 instruction samples");
    }
    {
        std::ifstream fixture("tests/fixtures/native-team-leader-ra2.csv");std::string line;bool exact=fixture.good();int samples=0;
        while(std::getline(fixture,line))if(!line.empty() && line[0]!='#') {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);int count=0,expected=-1;
            if(!(row>>count>>expected) || count<1 || count>64){exact=false;break;}
            std::vector<NativeTeamLeaderCandidate> members(count);
            for(int i=0;i<count;++i){auto& m=members[i];m.actor=i;int leader=0,aircraft=0,onMap=0,limbo=0;
                if(!(row>>m.rating>>leader>>aircraft>>onMap>>m.health>>limbo))exact=false;
                m.leader=leader!=0;m.aircraft=aircraft!=0;m.onMap=onMap!=0;m.limbo=limbo!=0;}
            exact &= nativeSelectTeamLeader(members)==expected;++samples;
        }
        check(exact && samples==320,"compiled leader eligibility, rating and first-tie behavior match 320 original instruction samples");
    }
    for(const char* edition:{"ra2","yr"}) {
        const auto input=read(std::string("tests/fixtures/native-nearby-v62/")+edition+"-profile.ini");const std::string source(input.begin(),input.end());
        const auto initialize=[&](World& w,const std::string& text){World::RulesSnapshot rules{text};return w.init(64,64,20261118,1,1,
            {Faction::Soviet,Faction::Allies},0,nullptr,false,false,nullptr,{Country::Russia,Country::America},&rules);};
        const auto path=directory+"/house-leader/"+edition;std::filesystem::create_directories(path);
        const auto dump=[&](const char* name,const World& w){const auto bytes=save(w);std::ofstream output(path+"/"+name,std::ios::binary);output.write(bytes.data(),bytes.size());return !bytes.empty();};
        World house;const bool started=initialize(house,source);
        check(started && house.nativeSceneVersion==39 && house.nativeAIRules.hasOriginalTeamControl() && house.nativeAI.centers.size()==2,
            "Scene39 installs persistent source house state before an AI context starts");
        if(!started)continue;
        check(!house.nativeAI.active && house.nativeAI.centers[0].center==Vec2i{} && house.nativeAI.centers[1].center==Vec2i{},
            "an MCV-only opening has the original empty base center, independent of AI activation");
        const auto zero=save(house);World zeroReplay;
        check(!zero.empty() && std::string(zero.data(),8)=="RA2WLD62" && load(zeroReplay,zero) && save(zeroReplay)==zero,
            "an actual pre-AI World62 preserves house centers and exact bytes");
        const auto initialHash=house.checksum();auto originalCenter=house.nativeAI.centers[0].center;
        house.nativeAI.centers[0].center={128,128};
        check(initialHash!=house.checksum(),"source house state affects lockstep checksums even before AI activation");
        house.nativeAI.centers[0].center=originalCenter;
        flat(house);
        const EID yard=house.spawnBuilding(0,BldType::ConYard,5,5,true);
        const EID power=house.spawnBuilding(0,BldType::TeslaReactor,25,5,true);
        const EID barracks=house.spawnBuilding(0,BldType::Barracks,5,25,true);
        house.spawnBuilding(1,BldType::ConYard,41,41,true);
        const bool prices=house.valid(yard) && house.valid(power) && house.valid(barracks)
            && house.nativeProductionCost(0,house.ents[yard].nativeIdentity.type)==3000
            && house.nativeProductionCost(0,house.ents[power].nativeIdentity.type)==600
            && house.nativeProductionCost(0,house.ents[barracks].nativeIdentity.type)==500;
        std::printf("HOUSE_LEADER COST edition=%s yard=%s:%d power=%s:%d barracks=%s:%d center=%d,%d repacks=%d\n",edition,
            house.ents[yard].nativeIdentity.definition->id.c_str(),house.nativeProductionCost(0,house.ents[yard].nativeIdentity.type),
            house.ents[power].nativeIdentity.definition->id.c_str(),house.nativeProductionCost(0,house.ents[power].nativeIdentity.type),
            house.ents[barracks].nativeIdentity.definition->id.c_str(),house.nativeProductionCost(0,house.ents[barracks].nativeIdentity.type),
            house.nativeAI.centers[0].center.x,house.nativeAI.centers[0].center.y,house.mcvRepacks);
        check(prices && house.nativeAI.centers[0].center==Vec2i{8*256+128,8*256+128},
            "real building creation uses actual source costs and the weighted cell center rather than one surviving yard");
        check(dump("house-weighted.world",house),"the actual weighted house state writes");
        house.kill(power,false);
        check(house.nativeAI.centers[0].center==Vec2i{5*256+128,9*256+128},"building destruction recalculates the surviving weighted center");
        const EID engineer=house.spawnUnit(1,UnitType::Engineer,6.5f,29.5f);
        const bool ordered=house.orderEngineer({engineer},barracks,EngineerIntent::Capture,false).accepted>0;
        for(int frame=0;frame<900 && house.valid(barracks) && house.ents[barracks].player==0;++frame)house.update();
        check(ordered && house.valid(barracks) && house.ents[barracks].player==1
            && house.nativeAI.centers[0].center==Vec2i{5*256+128,5*256+128}
            && house.nativeAI.centers[1].center==Vec2i{34*256+128,38*256+128},
            "an actual engineer capture updates both owners' centers through the building lifecycle");
        check(dump("house-captured.world",house),"the actual captured house state writes");
        const auto captured=save(house);World capturedReplay;bool capturedExact=load(capturedReplay,captured) && save(capturedReplay)==captured;
        for(int frame=0;frame<120 && capturedExact;++frame){house.update();capturedReplay.update();capturedExact &= house.checksum()==capturedReplay.checksum();}
        check(capturedExact,"derived building caches rebuild after load without recalculating a different persistent center");
        house.mcvRepacks=true;
        World::Cmd repack{};repack.type=World::Cmd::Deploy;repack.ids={yard};house.applyCmd(0,repack);
        bool mcv=false;for(const auto& actor:house.ents)mcv |= actor.alive && !actor.isBuilding && actor.player==0 && actor.utype==UnitType::MCV;
        std::printf("HOUSE_LEADER REPACK edition=%s mcv=%d yardAlive=%d center=%d,%d enabled=%d\n",edition,mcv,house.valid(yard),house.nativeAI.centers[0].center.x,house.nativeAI.centers[0].center.y,house.mcvRepacks);
        check(mcv && house.nativeAI.centers[0].center==Vec2i{},"public MCV repacking leaves the original empty center after the last building is removed");
        check(dump("house-repacked.world",house),"the repacked source house state writes");

        int definition=-1;for(int i=0;i<(int)house.nativeEventDefinition->teams.size();++i) {
            const auto& d=house.nativeEventDefinition->teams[i];const auto& force=house.nativeEventDefinition->taskForces[d.taskForce];
            int count=0;bool conscripts=true;for(const auto& row:force.units){count+=row.first;conscripts &= row.second=="E2";}
            if(conscripts && count>=2 && count<=20 && house.nativeTeamBlocker(i)==-1){definition=i;break;}
        }
        check(definition>=0 && house.nativeAIRules.leadership.at("ghost")==8 && house.nativeAIRules.leadership.at("e2")==5,
            "the actual source table retains hero and default leadership values with a valid conscript force");
        if(definition<0)continue;
        const auto& d=house.nativeEventDefinition->teams[definition];
        // A deliberate unsupported custom line isolates roster/boarding from
        // source mission orders. It is never counted as original boarding AI.
        const auto fixture=blockedScript(source,house.nativeEventDefinition->scripts[d.script].id);
        World roles;const bool roleStarted=!fixture.empty() && initialize(roles,fixture);
        check(roleStarted,"a declared custom unsupported-line fixture isolates actual roster and cargo behavior");if(!roleStarted)continue;
        flat(roles);roles.spawnBuilding(0,BldType::ConYard,5,5,true);roles.spawnBuilding(1,BldType::ConYard,45,45,true);
        roles.players[0].isAI=true;const bool configured=roles.configureNativeSkirmishAI(0,1,true,1);roles.nativeAI.houses[0].production=false;
        const int team=configured?roles.createNativeTeam(definition,false):-1;
        if(team<0){check(false,"the source force is created in its real global context");continue;}
        roles.nativeEvents.teams[team].memberHouse=0;roles.nativeAI.autocreate[definition]=1;
        const int type=roles.nativeUnitIndex("E2");const int htk=roles.nativeUnitIndex("HTK");
        const EID carrier=roles.spawnUnit(0,roles.nativeTypes[htk]->unit.type,25.5f,25.5f,htk);
        int wanted=0;for(const auto& row:roles.nativeEventDefinition->taskForces[d.taskForce].units)wanted+=row.first;
        std::vector<EID> members;
        for(int i=0;i<wanted;++i){const EID actor=roles.spawnUnit(0,roles.nativeTypes[type]->unit.type,23.5f+(i%4),23.5f+(i/4),type);
            members.push_back(actor);roles.nativeEvents.teams[team].members.push_back(actor);roles.nativeAIJoined(team,actor);}
        const auto firstKey=roles.ents[members.front()].nativeIdentity.object;const auto newestKey=roles.ents[members.back()].nativeIdentity.object;
        check(roles.nativeAI.feet.at(firstKey).leader && !roles.nativeAI.feet.at(newestKey).leader
            && roles.nativeTeamLeader(roles.nativeEvents.teams[team])==members.front(),
            "before formation, original fresh-join flags select the first joined member instead of the latest EID");
        check(dump("roles-joined.world",roles),"the actual joined sequence and pre-formation flags write");
        roles.update();bool allLeaders=true;for(EID actor:members)allLeaders &= roles.nativeAI.feet.at(roles.ents[actor].nativeIdentity.object).leader;
        check(allLeaders && roles.nativeEvents.teams[team].assembled && roles.nativeEvents.teams[team].blockedScript==255
            && roles.nativeTeamLeader(roles.nativeEvents.teams[team])==members.back(),
            "the strength transition marks the completed force and equal ratings keep source head order");
        check(dump("roles-full.world",roles),"the actual completed force writes");
        const uint64_t joined=roles.nativeAI.feet.at(firstKey).joined;
        bool aboard=roles.orderBoard({members.front()},carrier).accepted==1;
        for(int frame=0;frame<240 && roles.valid(members.front());++frame)roles.update();
        aboard &= !roles.valid(members.front()) && !roles.ents[carrier].cargo.empty() && roles.nativeAI.feet.contains(firstKey)
            && roles.nativeAI.feet.at(firstKey).joined==joined;
        const auto boarding=save(roles);World replay;bool exact=aboard && load(replay,boarding) && save(replay)==boarding;
        check(aboard && exact && dump("roles-boarding.world",roles),"real boarding preserves original join order and leader flags through World62");
        roles.orderUnload({carrier});replay.orderUnload({carrier});
        for(int frame=0;frame<240 && exact;++frame){roles.update();replay.update();exact &= roles.checksum()==replay.checksum();if(roles.ents[carrier].cargo.empty())break;}
        EID returned=INVALID_EID;for(EID id=0;id<(EID)roles.ents.size();++id)if(roles.valid(id) && roles.ents[id].nativeIdentity.object==firstKey)returned=id;
        check(exact && roles.valid(returned) && roles.nativeAI.feet.at(firstKey).joined==joined
            && roles.nativeTeamLeader(roles.nativeEvents.teams[team])==members.back(),
            "real unloading restores the original Foot order rather than treating the returned actor as the newest member");
        check(dump("roles-unloaded.world",roles),"the actual unloaded source Foot state writes");
        roles.kill(members.back(),false);roles.update();
        check(!roles.nativeAI.feet.contains(newestKey) && !roles.nativeAI.memberTeams.contains(newestKey),"actual death prunes the retired source Foot identity");
        check(dump("roles-loss.world",roles),"the actual loss and remaining leader state write");
        roles.releaseNativePlayerUnits({returned});roles.update();
        check(!roles.nativeAI.feet.contains(firstKey) && !roles.nativeAI.memberTeams.contains(firstKey)
            && std::find(roles.nativeEvents.teams[team].members.begin(),roles.nativeEvents.teams[team].members.end(),returned)==roles.nativeEvents.teams[team].members.end(),
            "an explicitly released actor is not silently reattached through the durable roster");
        check(dump("roles-released.world",roles),"the actual release state writes");
        if(!boarding.empty()) {
            const auto marker=std::string(boarding.data(),boarding.size()).rfind("AIGL");
            auto bad=boarding;if(marker!=std::string::npos && marker+8<bad.size()){bad[marker+4]=1;World rejected;check(!load(rejected,bad),"World62 rejects a legacy-version AI suffix instead of inventing missing Foot state");}
            size_t cursor=marker+8;bool layout=marker!=std::string::npos;
            const auto number=[&](){uint32_t value=0;if(cursor+4>boarding.size()){layout=false;return value;}std::memcpy(&value,boarding.data()+cursor,4);cursor+=4;return value;};
            const auto countHouses=number();cursor+=9ull*countHouses;
            const auto countMembers=number();if(countMembers>65536)layout=false;
            for(uint32_t i=0;i<countMembers && layout;++i){const auto n=number();if(n>128)layout=false;cursor+=n+4;}
            const auto countCenters=number();const size_t firstCenter=cursor;cursor+=9ull*countCenters;
            const size_t sequence=cursor;cursor+=8;const auto countFeet=number();if(countFeet>65536)layout=false;
            size_t firstJoined=0,secondJoined=0,firstLeader=0;
            for(uint32_t i=0;i<countFeet && layout;++i){const auto n=number();if(n>128)layout=false;cursor+=n;
                if(i==0){firstJoined=cursor;firstLeader=cursor+8;}if(i==1)secondJoined=cursor;cursor+=9;}
            const auto countStrength=number();const size_t firstStrength=cursor;
            layout &= countCenters==2 && countFeet>=2 && countStrength>0 && firstStrength+countStrength==boarding.size();
            check(layout,"the actual World62 fixture exposes bounded house, Foot-order and strength fields");
            if(layout) {
                const auto reject=[&](std::vector<char> bytes,const char* text){World rejected;check(!load(rejected,bytes),text);};
                bad=boarding;bad[firstCenter]=2;reject(bad,"a nonboolean house initialization byte is rejected");
                bad=boarding;std::fill_n(bad.begin()+firstCenter+1,4,(char)0xff);reject(bad,"a negative saved source house coordinate is rejected");
                bad=boarding;std::fill_n(bad.begin()+sequence,8,0);reject(bad,"a Foot order outside its saved global sequence is rejected");
                bad=boarding;bad[firstLeader]=2;reject(bad,"a nonboolean source leader flag is rejected");
                bad=boarding;std::copy_n(boarding.begin()+firstJoined,8,bad.begin()+secondJoined);reject(bad,"duplicate source join orders are rejected");
                bad=boarding;bad[firstStrength]=2;reject(bad,"a nonboolean source strength state is rejected");
            }
        }
    }
    for(const char* edition:{"ra2","yr"}) {
        const auto file=std::string("tests/fixtures/native-house-leader-v61/")+edition+"-staging.world";const auto original=read(file);World legacy;
        bool exact=!original.empty() && load(legacy,original) && !legacy.nativeAIRules.hasOriginalTeamControl() && save(legacy)==original;
        World replay;if(exact)exact=load(replay,original);
        for(int i=0;i<600 && exact;++i){legacy.update();replay.update();exact &= legacy.checksum()==replay.checksum();}
        check(exact,"actual pre-house-control World61 fixtures keep exact bytes and their historical simulation contract");
        const auto app=std::string("tests/fixtures/native-house-leader-v61/")+edition+"-tick0.app11";
        const auto old=read(app);const auto resaved=directory+"/legacy-"+edition+".app11";
        check(!old.empty() && loadGameFile(app.c_str()) && saveGameFile(resaved.c_str()) && read(resaved)==old,
            "actual Scene38 App11 opens and resaves with its embedded original rules unchanged");
    }
    return failures;
}
