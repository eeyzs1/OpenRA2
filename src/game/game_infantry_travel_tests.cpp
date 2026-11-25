#include "game_regression.h"
#include "game/native_walk.h"
#include "scoped_user_file.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace {
World walkFixture(const World::RulesSnapshot* snapshot=nullptr) {
    World w;w.init(64,64,20261124,2,0,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,
        snapshot?nullptr:"assets/rules/profiles/ra2.ini",{Country::America,Country::Russia},snapshot);
    w.cratesEnabled=false;w.crates.clear();
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& c:w.map.cells)c=Cell{};
    for(auto& p:w.players){p.money=10000;p.defeated=false;p.isAI=false;}
    w.spawnBuilding(0,BldType::ConYard,48,48,true);w.spawnBuilding(1,BldType::ConYard,2,48,true);return w;
}
World::RulesSnapshot walkRules(const World& base,int speed) {
    World::RulesSnapshot r{base.nativeScenarioSource};const auto type=r.source.find("Id = E1\n");const auto at=r.source.find("OriginalSpeed = ",type);
    if(type!=std::string::npos && at!=std::string::npos)r.source.replace(at,r.source.find('\n',at)-at,"OriginalSpeed = "+std::to_string(speed));
    return r;
}
int walkOriginalSpeed(int rate) {
    for(int speed=0;speed<=100;++speed)if(std::min(255,speed*256/100)==rate)return speed;return -1;
}
void walkCmd(World& w,EID id,World::Cmd::Type type,float x=0,float y=0) {
    World::Cmd c{};c.type=type;c.ids={id};c.x=x;c.y=y;w.bindCommand(c);w.applyCmd(0,c);
}
void walkFeedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
bool walkSave(World& w,World& copy,const std::filesystem::path& path) {
    FILE* f=std::fopen(path.string().c_str(),"wb");bool ok=f && w.saveGame(f);if(f)std::fclose(f);
    f=std::fopen(path.string().c_str(),"rb");ok &= f && copy.loadGame(f);if(f)std::fclose(f);return ok && w.checksum()==copy.checksum();
}
bool walkLoad(World& w,const std::filesystem::path& path) {
    FILE* f=std::fopen(path.string().c_str(),"rb");const bool ok=f && w.loadGame(f);if(f)std::fclose(f);return ok;
}
}

int GameRegression::infantryTravelRegression(const char* outputDir) {
    ScopedUserFile settings("settings.ini");if(!settings.ready())return 1;
    const std::filesystem::path out(outputDir);std::filesystem::create_directories(out);int failures=0;
    const auto check=[&](bool ok,const char* kind,const std::string& name){failures+=!ok;std::printf("INFANTRY_TRAVEL %s %s %s\n",ok?"PASS":"FAIL",kind,name.c_str());std::fflush(stdout);};
    const World base=walkFixture();std::map<int,World> templates;
    for(int rate:{0,2,5,7,10,12,15,17,20,25}){const auto rules=walkRules(base,walkOriginalSpeed(rate));templates.emplace(rate,walkFixture(&rules));}
    {
        std::ifstream speeds("tests/fixtures/ra2-walk-speed-cases.csv");std::string line;std::getline(speeds,line);
        int count=0,same=0;
        while(std::getline(speeds,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            int maximum,faster,panic,crawls,expected;double house,bonus,current;
            if(!(row>>maximum>>house>>bonus>>faster>>current>>panic>>crawls>>expected))break;
            if(panic || crawls)continue;
            const int actual=NativeWalkMath::speed(maximum,(float)house,bonus,faster?(double)1.2f:1.,current);
            ++count;same+=actual==expected;
        }
        check(count==792 && same==count,"SOURCE_SPEED","staged-speed-rounding-with-controlled-inputs");
    }
    std::ifstream input("tests/fixtures/ra2-walk-step-cases.csv");std::string line;std::getline(input,line);
    std::ofstream observed(out/"walk-first-frame.csv");observed<<"fx,fy,dx,dy,rate,expectedX,expectedY,actualX,actualY,exact\n";
    int cases=0,matched=0;bool setup=true;
    while(std::getline(input,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);int fx,fy,dx,dy,rate,x,y,nx,ny,admissions,writes,actualRate;
        if(!(row>>fx>>fy>>dx>>dy>>rate>>x>>y>>nx>>ny>>admissions>>writes>>actualRate)){setup=false;break;}
        if(std::abs(dx)>1 || std::abs(dy)>1 || !templates.contains(rate))continue;
        World w=templates.at(rate);w.map.bldOccRef=&w.bldOcc;const auto id=w.spawnUnit(0,UnitType::GI,x/256.f,y/256.f);
        if(!w.valid(id)){setup=false;break;}
        walkCmd(w,id,World::Cmd::Move,11+dx+.5f,17+dy+.5f);
        setup &= w.ents[id].nativeIdentity.definition->walkSpeedLeptons==rate && w.ents[id].state==UState::Moving && !w.ents[id].path.empty();
        w.update();w.update();const auto& e=w.ents[id];
        const bool exact=e.x*256==nx && e.y*256==ny;++cases;matched+=exact;
        observed<<fx<<','<<fy<<','<<dx<<','<<dy<<','<<rate<<','<<nx<<','<<ny<<','<<e.x*256<<','<<e.y*256<<','<<exact<<'\n';
    }
    std::printf("INFANTRY_TRAVEL MATRIX cases=%d matched=%d setup=%d\n",cases,matched,setup);
    check(setup && cases==720 && matched==cases && bool(observed),"SOURCE_MATRIX","integer-displacement-at-first-native-frame");
    {
        struct Frame{int frame,x,y;};std::map<std::pair<int,int>,std::vector<Frame>> groups;
        std::ifstream seq("tests/fixtures/ra2-walk-sequences.csv");std::getline(seq,line);bool ready=true;
        while(std::getline(seq,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            int mode,direction,rate,frame,x,y,nx,ny,hx,hy,tx,ty,admissions,writes,moving;
            if(!(row>>mode>>direction>>rate>>frame>>x>>y>>nx>>ny>>hx>>hy>>tx>>ty>>admissions>>writes>>moving)){ready=false;break;}
            if(mode==0 && (rate==7 || rate==10 || rate==15 || rate==20))groups[{rate,direction}].push_back({frame,nx,ny});
        }
        constexpr int dxs[]{1,1,0,-1,-1,-1,0,1},dys[]{0,1,1,1,0,-1,-1,-1};int frames=0,same=0;
        std::ofstream output(out/"walk-sequences.csv");output<<"rate,direction,frame,expectedX,expectedY,actualX,actualY,exact\n";
        for(const auto& [key,trajectory]:groups) {
            World w=templates.at(key.first);w.map.bldOccRef=&w.bldOcc;const auto id=w.spawnUnit(0,UnitType::GI,11.5f,17.5f);
            walkCmd(w,id,World::Cmd::Move,11+dxs[key.second]+.5f,17+dys[key.second]+.5f);
            for(const auto& f:trajectory) {
                w.update();w.update();const auto& e=w.ents[id];const bool exact=e.x*256==f.x && e.y*256==f.y;
                ++frames;same+=exact;output<<key.first<<','<<key.second<<','<<f.frame<<','<<f.x<<','<<f.y<<','<<e.x*256<<','<<e.y*256<<','<<exact<<'\n';walkFeedback(w);
            }
        }
        std::printf("INFANTRY_TRAVEL SEQUENCES groups=%zu frames=%d matched=%d setup=%d\n",groups.size(),frames,same,ready);
        check(ready && groups.size()==32 && frames>500 && frames==same && bool(output),"SOURCE_SEQUENCES","continuous-cardinal-diagonal-and-arrival");
    }
    for(int mode=0;mode<8;++mode) {
        World w=mode==6?templates.at(0):base;w.map.bldOccRef=&w.bldOcc;
        const auto id=w.spawnUnit(0,mode==2?UnitType::Tanya:mode==3?UnitType::AttackDog:UnitType::GI,20.5f,20.5f);
        bool ready=w.valid(id);if(!ready){check(false,"SETUP","walk-type");continue;}
        if(mode==4)w.ents[id].vetRank=1;
        walkCmd(w,id,World::Cmd::Move,mode==1?15.5f:28.5f,mode==1?25.5f:20.5f);
        const auto name="walk-journey-"+std::to_string(mode);std::ofstream(out/(name+".case"))<<id<<'\n';
        World copy,middle;bool exact=walkSave(w,copy,out/(name+"-ordered.sav")),mid=false,odd=false;
        std::ofstream trace(out/(name+".trace"));trace<<w.checksum()<<'\n';bool arrived=false,stopped=true;float stopX=0,stopY=0;
        for(int at=1;at<=600;++at) {
            if(mode==5 && at==6){walkCmd(w,id,World::Cmd::Stop);walkCmd(copy,id,World::Cmd::Stop);if(mid)walkCmd(middle,id,World::Cmd::Stop);stopX=w.ents[id].x;stopY=w.ents[id].y;}
            if(mode==7 && at==6){walkCmd(w,id,World::Cmd::Move,20.5f,28.5f);walkCmd(copy,id,World::Cmd::Move,20.5f,28.5f);if(mid)walkCmd(middle,id,World::Cmd::Move,20.5f,28.5f);}
            w.update();copy.update();exact &= w.checksum()==copy.checksum();
            if(at>1 && mid){middle.update();mid &= w.checksum()==middle.checksum();}
            if(at==1){mid=walkSave(w,middle,out/(name+"-odd.sav"));odd=mid;}
            if(at==2){World even;exact &= walkSave(w,even,out/(name+"-even.sav"));}
            trace<<w.checksum()<<'\n';
            if(mode==5 && at>=6)stopped &= w.ents[id].x==stopX && w.ents[id].y==stopY;
            const float gx=mode==7?20.5f:mode==1?15.5f:28.5f,gy=mode==7?28.5f:mode==1?25.5f:20.5f;
            arrived |= w.ents[id].state==UState::Idle && distf(w.ents[id].x,w.ents[id].y,gx,gy)<.01f;
            walkFeedback(w);walkFeedback(copy);walkFeedback(middle);
        }
        std::ofstream(out/(name+".final"))<<w.checksum()<<'\n';World final;exact &= walkSave(w,final,out/(name+"-final.sav"));
        const bool outcome=mode==5?stopped:mode==6?w.ents[id].x==20.5f && w.ents[id].y==20.5f:arrived;
        check(ready && outcome,"COMMAND",name);check(exact && mid && odd,"REPLAY",name);
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_WALK68_INPUT_DIR"))for(int mode=0;mode<8;++mode) {
        const std::filesystem::path old(directory);const auto name="walk-journey-"+std::to_string(mode);World w;EID id=INVALID_EID;
        std::ifstream(old/(name+".case"))>>id;bool exact=walkLoad(w,old/(name+"-ordered.sav")) && w.nativeRulesBaseVersion==68 && w.valid(id);
        std::ifstream trace(old/(name+".trace"));uint64_t expected=0;exact &= bool(trace>>expected) && w.checksum()==expected;
        for(int at=1;at<=600 && exact;++at) {
            if(mode==5 && at==6)walkCmd(w,id,World::Cmd::Stop);
            if(mode==7 && at==6)walkCmd(w,id,World::Cmd::Move,20.5f,28.5f);
            w.update();exact=bool(trace>>expected) && w.checksum()==expected;walkFeedback(w);
        }
        check(exact,"LEGACY68",name);
    }
    for(int mode=0;mode<3;++mode) {
        World w=base;w.map.bldOccRef=&w.bldOcc;
        if(mode==1){for(auto& c:w.map.cells)c.terrain=Terrain::Water;for(int x=19;x<=31;++x)w.map.at(x,20)=Cell{};}
        const auto id=w.spawnUnit(0,UnitType::GI,20.5f,20.5f);
        const auto second=mode==2?w.spawnUnit(0,UnitType::GI,20.5f,21.5f):INVALID_EID;
        const auto blocker=mode==1?w.spawnUnit(0,UnitType::Grizzly,21.5f,20.5f):INVALID_EID;
        if(mode==1){w.ents[blocker].tpSick=41;walkCmd(w,blocker,World::Cmd::Guard);}
        if(mode==2){World::Cmd c{};c.type=World::Cmd::Move;c.ids={id,second};c.x=28.5f;c.y=21;w.bindCommand(c);w.applyCmd(0,c);}
        else walkCmd(w,id,World::Cmd::Move,28.5f,20.5f);
        const float gx=w.ents[id].goalX,gy=w.ents[id].goalY;
        const float sx=mode==2?w.ents[second].goalX:0,sy=mode==2?w.ents[second].goalY:0;
        World copy;const auto name="walk-flow-"+std::to_string(mode);bool exact=walkSave(w,copy,out/(name+".sav")),held=true,arrived=false;
        std::ofstream trace(out/(name+".trace"));trace<<w.checksum()<<'\n';float freezeX=0,freezeY=0;
        for(int at=1;at<=800;++at) {
            if(mode==0 && at==12){w.ents[id].tpSick=copy.ents[id].tpSick=12;freezeX=w.ents[id].x;freezeY=w.ents[id].y;}
            if(mode==1 && at==42){walkCmd(w,blocker,World::Cmd::Move,30.5f,20.5f);walkCmd(copy,blocker,World::Cmd::Move,30.5f,20.5f);
                walkCmd(w,id,World::Cmd::Move,28.5f,20.5f);walkCmd(copy,id,World::Cmd::Move,28.5f,20.5f);}
            w.update();copy.update();exact &= w.checksum()==copy.checksum();trace<<w.checksum()<<'\n';
            if(mode==0 && at>=12 && at<=20)held &= w.ents[id].x==freezeX && w.ents[id].y==freezeY;
            if(mode==1 && at<=40)held &= w.ents[id].x==20.5f && w.ents[id].y==20.5f;
            arrived |= distf(w.ents[id].x,w.ents[id].y,gx,gy)<.01f
                && (mode!=2 || distf(w.ents[second].x,w.ents[second].y,sx,sy)<.01f);
            walkFeedback(w);walkFeedback(copy);
        }
        std::ofstream(out/(name+".final"))<<w.checksum()<<'\n';World final;exact &= walkSave(w,final,out/(name+"-final.sav"));
        check(held && arrived && exact,"FLOW",mode==0?"immobilization-and-recovery":mode==1?"blocked-corridor-clearance-and-new-order":"two-infantry-formation-arrival");
    }
    {
        World w=base;w.map.bldOccRef=&w.bldOcc;
        const auto id=w.spawnUnit(0,UnitType::GI,20.75f,21.02734375f);
        const auto vehicle=w.spawnUnit(0,UnitType::TerrorDrone,20.530695f,20.5f);
        const auto carrier=w.spawnUnit(0,UnitType::IFV,21.5f,20.5f);
        World::Cmd board{};board.type=World::Cmd::Board;board.ids={id};board.a=carrier;w.bindCommand(board);w.applyCmd(0,board);
        World copy;bool exact=walkSave(w,copy,out/"walk-occupied-corner.sav");
        bool clear=true,arrived=false;
        for(int at=0;at<400;++at){w.update();copy.update();exact &= w.checksum()==copy.checksum();
            if(w.valid(id))clear &= (int)w.ents[id].x!=20 || (int)w.ents[id].y!=20;
            arrived |= !w.valid(id) && w.ents[carrier].cargo.size()==1;walkFeedback(w);walkFeedback(copy);}
        clear &= w.valid(vehicle);
        check(clear && arrived && exact,"FLOW","off-center-diagonal-avoids-occupied-orthogonal-cell");
    }
    {
        world=base;world.map.bldOccRef=&world.bldOcc;const auto id=world.spawnUnit(0,UnitType::GI,20.5f,20.5f);
        walkCmd(world,id,World::Cmd::Move,28.5f,20.5f);world.update();world.update();camX=camY=0;camZoom=1;
        const auto sum=world.checksum();interpAlpha=0;const auto start=unitScreenPos(world.ents[id]);
        interpAlpha=1;const auto half=unitScreenPos(world.ents[id]);world.update();interpAlpha=0;const auto middle=unitScreenPos(world.ents[id]);
        interpAlpha=1;const auto end=unitScreenPos(world.ents[id]);
        const bool smooth=half.x==middle.x && half.y==middle.y && half.x>start.x && end.x>half.x
            && std::abs((half.x-start.x)-(end.x-half.x))<.001f;
        World independent=world;independent.map.bldOccRef=&independent.bldOcc;
        independent.ents[id].nativeWalkViewTick=999999;independent.ents[id].nativeWalkViewX=-100;independent.ents[id].nativeWalkViewY=100;
        independent.ents[id].px=-30;independent.ents[id].py=80;
        bool isolated=independent.checksum()==world.checksum();
        for(int at=0;at<120;++at){world.update();independent.update();isolated &= world.checksum()==independent.checksum();walkFeedback(world);walkFeedback(independent);}
        check(smooth && sum!=world.checksum() && isolated,"PRESENTATION","two-tick-view-interpolation-never-drives-simulation");
    }
    std::printf("INFANTRY_TRAVEL_RESULT failures=%d\n",failures);return failures;
}
