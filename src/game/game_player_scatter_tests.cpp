#include "game_regression.h"
#include "game/script.h"
#include "scoped_user_file.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
World scatterFixture(const World::RulesSnapshot* rules=nullptr) {
    World w;w.init(64,64,20261124,2,0,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,
        rules?nullptr:"assets/rules/profiles/ra2.ini",{Country::America,Country::Russia},rules);
    w.cratesEnabled=false;w.crates.clear();
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& cell:w.map.cells)cell=Cell{};
    for(auto& p:w.players){p.money=10000;p.defeated=false;p.isAI=false;}
    w.spawnBuilding(0,BldType::ConYard,48,48,true);w.spawnBuilding(1,BldType::ConYard,2,48,true);
    return w;
}
World::RulesSnapshot scatterRules(const World& base,bool fraidy,bool playerScatter) {
    World::RulesSnapshot r{base.nativeScenarioSource};
    if(playerScatter){const auto at=r.source.find("PlayerScatter = 0");if(at!=std::string::npos)r.source.replace(at,17,"PlayerScatter = 1");}
    if(fraidy){const auto type=r.source.find("Id = E1\n");const auto at=r.source.find("Fraidycat = 0",type);
        if(type!=std::string::npos && at!=std::string::npos)r.source.replace(at,13,"Fraidycat = 1");}
    return r;
}
void scatterCommand(World& w,EID id,World::Cmd::Type type=World::Cmd::Scatter) {
    World::Cmd c{};c.type=type;c.ids={id};c.x=30.5f;c.y=22.5f;w.bindCommand(c);w.applyCmd(0,c);
}
bool scatterSave(World& w,World& copy,const std::filesystem::path& p) {
    FILE* f=std::fopen(p.string().c_str(),"wb");bool ok=f && w.saveGame(f);if(f)std::fclose(f);
    f=std::fopen(p.string().c_str(),"rb");ok &= f && copy.loadGame(f);if(f)std::fclose(f);
    return ok && w.checksum()==copy.checksum();
}
void scatterFeedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
bool scatterRead(World& w,const std::filesystem::path& p){FILE* f=std::fopen(p.string().c_str(),"rb");const bool ok=f && w.loadGame(f);if(f)std::fclose(f);return ok;}

int scatterType(const World& w,const char* name) {
    for(int i=0;i<(int)w.nativeTypes.size();++i)if(w.nativeTypes[i]->id==name)return i;
    return -1;
}
EID scatterVehicle(World& w,const char* name,float x=22.5f,float y=22.5f) {
    const int type=scatterType(w,name);
    return type<0?INVALID_EID:w.spawnUnit(0,w.nativeTypes[type]->unit.type,x,y,type);
}
void scatterWater(World& w) {
    for(int y=10;y<=36;++y)for(int x=10;x<=36;++x)w.map.at(x,y).terrain=Terrain::Water;
}
World::RulesSnapshot scatterTrainRules(const World& base) {
    World::RulesSnapshot r{base.nativeScenarioSource};
    for(const char* name:{"MTNK","FV","DEST","LCRF","CMIN","HTK"}) {
        const auto type=r.source.find(std::string("Id = ")+name+'\n');
        const auto at=r.source.find("IsTrain = 0",type),end=r.source.find("\n[",type);
        if(type!=std::string::npos && at!=std::string::npos && (end==std::string::npos || at<end))
            r.source.replace(at,11,"IsTrain = 1");
    }
    return r;
}
bool prepareScatterCargo(World& w,EID host) {
    const auto passenger=w.spawnUnit(0,UnitType::GI,23.5f,22.5f);if(!w.valid(passenger))return false;
    World::Cmd c{};c.type=World::Cmd::Board;c.ids={passenger};c.a=host;w.bindCommand(c);w.applyCmd(0,c);
    for(int at=0;at<600 && w.valid(passenger);++at){w.update();scatterFeedback(w);}
    return w.valid(host) && !w.valid(passenger) && w.ents[host].cargo.size()==1;
}
int vehicleScatterRegression(const World& base,const std::filesystem::path& out) {
    int failures=0;
    const auto check=[&](bool ok,const char* kind,const std::string& name){failures+=!ok;std::printf("VEHICLE_SCATTER %s %s %s\n",ok?"PASS":"FAIL",kind,name.c_str());std::fflush(stdout);};
    const auto trainRules=scatterTrainRules(base);const World trainBase=scatterFixture(&trainRules);
    std::ifstream input("tests/fixtures/ra2-vehicle-scatter-cases.csv");std::string line;std::getline(input,line);
    std::ofstream observed(out/"vehicle-scatter-matrix.csv");observed<<"pattern,speed,frame,expectedX,expectedY,actualX,actualY,exact\n";
    int cases=0,matched=0;bool setup=true;
    while(std::getline(input,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
        int pattern,speed,frame,destination,x,y,draws,missions,queries,releases;
        if(!(row>>pattern>>speed>>frame>>destination>>x>>y>>draws>>missions>>queries>>releases)){setup=false;break;}
        const char* name=speed==1?"MTNK":speed==2?"FV":speed==3?"DEST":speed==5?"LCRF":nullptr;if(!name)continue;
        World w=base;w.map.bldOccRef=&w.bldOcc;if(speed==3)scatterWater(w);
        const auto id=scatterVehicle(w,name);if(!w.valid(id)){setup=false;break;}
        setup &= w.ents[id].nativeIdentity.definition && (int)w.ents[id].nativeIdentity.definition->movement.speedType==speed;
        if(pattern==1 || pattern==3 || pattern==4)for(int cy=0;cy<w.map.h;++cy)for(int cx=0;cx<w.map.w;++cx) {
            const int dx=cx-22,dy=cy-22;
            const bool open=pattern==1?cx==23 && cy==22:pattern==4?dx>=-1 && dx<=1 && dy>=-1 && dy<=1 && (dx+dy)%2!=0:false;
            if((cx!=22 || cy!=22) && !open)w.map.at(cx,cy).blocked=true;
        }
        if(pattern==2)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx || dy) {
            const auto other=scatterVehicle(w,speed==3?"DEST":"MTNK",22.5f+dx,22.5f+dy);
            setup &= w.valid(other);if(w.valid(other))w.ents[other].guard=true;
        }
        w.tick=frame*2;w.rng=Rng(1024);const auto before=w.checksum();const auto rngBefore=w.rng.s;
        scatterCommand(w,id);const auto& e=w.ents[id];
        const bool same=w.rng.s==rngBefore && (destination?e.state==UState::Moving && e.goalX==x+.5f && e.goalY==y+.5f:w.checksum()==before);
        ++cases;matched+=same;observed<<pattern<<','<<speed<<','<<frame<<','<<x<<','<<y<<','<<e.goalX<<','<<e.goalY<<','<<same<<'\n';
    }
    std::printf("VEHICLE_SCATTER MATRIX cases=%d matched=%d setup=%d\n",cases,matched,setup);
    check(setup && cases==960 && matched==cases && bool(observed),"SOURCE_MATRIX","ground-navalguid-nearby-without-rng");
    {
        std::ifstream guards("tests/fixtures/ra2-vehicle-scatter-guards.csv");std::getline(guards,line);
        std::ofstream output(out/"vehicle-scatter-guards.csv");output<<"mission,destination,rotating,train,locomotor,deployed,expected,actual,exact\n";
        int count=0,sameCount=0;bool ready=true;
        while(std::getline(guards,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            int mission,destination,rotating,train,kind,deployed,moving,accepted,draws,queries,releases;
            if(!(row>>mission>>destination>>rotating>>train>>kind>>deployed>>moving>>accepted>>draws>>queries>>releases)){ready=false;break;}
            if(moving || !((mission==0 || mission==5) && !destination || mission==2 && destination))continue;
            World w=train?trainBase:base;w.map.bldOccRef=&w.bldOcc;if(kind==3)scatterWater(w);
            const char* name=kind==0?"MTNK":kind==1?"LCRF":kind==2?"CMIN":"DEST";
            const auto id=scatterVehicle(w,name);if(!w.valid(id)){ready=false;break;}
            ready &= w.ents[id].nativeIdentity.definition->isTrain==bool(train);
            if(destination)scatterCommand(w,id,World::Cmd::Move);
            if(mission==0)w.ents[id].nativeIdentity.sleeping=true;
            if(deployed)w.ents[id].deployed=true;
            w.tick=14;if(rotating){auto& e=w.ents[id];const auto angle=w.nativeHullHeading(e,w.tick);
                e.nativeGround.initialized=true;e.nativeGround.hull={angle,angle,w.tick/2,0};
                NativeGroundMath::desire(e.nativeGround.hull,static_cast<uint16_t>(angle+16384),e.nativeIdentity.definition->production.rotationRate,w.tick/2);}
            const auto before=w.checksum();const auto rngBefore=w.rng.s;scatterCommand(w,id);
            const bool changed=w.checksum()!=before;
            const bool same=w.rng.s==rngBefore && (accepted?w.ents[id].state==UState::Moving && changed:!changed);
            ++count;sameCount+=same;output<<mission<<','<<destination<<','<<rotating<<','<<train<<','<<kind<<','<<deployed<<','<<accepted<<','<<changed<<','<<same<<'\n';
        }
        std::printf("VEHICLE_SCATTER GUARDS cases=%d matched=%d setup=%d\n",count,sameCount,ready);
        check(ready && count==96 && sameCount==count && bool(output),"SOURCE_GUARDS","mission-destination-facing-train-teleport-deployed");
    }
    {
        std::ifstream timers("tests/fixtures/ra2-vehicle-scatter-timers.csv");std::getline(timers,line);int count=0,matchedTimers=0;
        while(std::getline(timers,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);int frame,expected,accepted,draws,queries,releases;
            if(!(row>>frame>>expected>>accepted>>draws>>queries>>releases))break;
            World w=base;w.map.bldOccRef=&w.bldOcc;const auto id=scatterVehicle(w,"MTNK");
            if(!w.valid(id))break;
            w.ents[id].nativeGround.initialized=true;w.ents[id].nativeGround.hull={0,16384,0,10};w.tick=frame*2;
            const auto before=w.checksum();const auto rngBefore=w.rng.s;scatterCommand(w,id);
            const bool changed=w.checksum()!=before;matchedTimers+=w.rng.s==rngBefore && changed==bool(accepted);++count;
        }
        check(count==6 && matchedTimers==count,"SOURCE_TIMER","stored-hull-timer-expiry");
    }
    constexpr const char* types[]{"MTNK","MTNK","MTNK","MTNK","MTNK","CMIN","DEST","DEST","LCRF","HTK","MTNK","MTNK","MTNK"};
    for(int mode=0;mode<13;++mode) {
        World w=mode==11?trainBase:base;w.map.bldOccRef=&w.bldOcc;if(mode==6 || mode==7)scatterWater(w);
        if(mode==12){for(auto& c:w.map.cells)c.terrain=Terrain::Water;
            for(int x=14;x<=26;++x)w.map.at(x,20)=Cell{};for(int y=20;y<=30;++y)w.map.at(26,y)=Cell{};}
        const auto id=scatterVehicle(w,types[mode],mode==12?14.5f:22.5f,mode==12?20.5f:22.5f);
        bool prepared=w.valid(id);if(!prepared){check(false,"SETUP","vehicle-type");continue;}
        if(mode==1 || mode==7)scatterCommand(w,id,World::Cmd::Move);
        if(mode==2)scatterCommand(w,id,World::Cmd::Guard);
        if(mode==3 || mode==4) {
            World::Cmd c{};c.type=World::Cmd::Move;c.ids={id};c.x=22.5f;c.y=15.5f;w.bindCommand(c);w.applyCmd(0,c);w.update();
            scatterCommand(w,id,mode==3?World::Cmd::Guard:World::Cmd::Stop);
            prepared &= mode==3?w.nativeHullHeading(w.ents[id],w.tick)!=w.ents[id].nativeGround.hull.target:
                w.nativeHullHeading(w.ents[id],w.tick)==w.ents[id].nativeGround.hull.target;
        }
        if(mode==9){prepared &= prepareScatterCargo(w,id);scatterCommand(w,id,World::Cmd::Unload);prepared &= w.ents[id].nativeTransportUnload.active();}
        if(mode==10)for(auto& c:w.map.cells)c.blocked=true;
        if(mode==12) {
            World::Cmd c{};c.type=World::Cmd::Move;c.ids={id};c.x=26.5f;c.y=30.5f;w.bindCommand(c);w.applyCmd(0,c);
            bool bent=false;for(int t=0;t<2000 && !bent;++t){w.update();const auto& a=w.ents[id].driveArc;bent=a.active() && a.wide && a.progress>.2f && a.progress<.5f;scatterFeedback(w);}
            prepared &= bent;
        }
        const auto name="vehicle-journey-"+std::to_string(mode);std::ofstream(out/(name+".case"))<<id<<'\n';
        World before,copy,middle;bool initial=scatterSave(w,before,out/(name+"-before.sav"));
        const auto prior=w.checksum();const auto rngBefore=w.rng.s;const auto startX=w.ents[id].x,startY=w.ents[id].y;
        scatterCommand(w,id);if(initial){copy=before;copy.map.bldOccRef=&copy.bldOcc;scatterCommand(copy,id);}
        const bool declines=mode==1 || mode==3 || mode==5 || mode==7 || mode==9 || mode==10 || mode==11 || mode==12;
        const bool semantics=w.rng.s==rngBefore && (declines?w.checksum()==prior:w.ents[id].state==UState::Moving && !w.ents[id].guard);
        World ordered;const bool saved=scatterSave(w,ordered,out/(name+"-ordered.sav"));
        bool exact=initial && copy.checksum()==w.checksum(),mid=false,arrived=false;
        const float gx=w.ents[id].goalX,gy=w.ents[id].goalY;std::ofstream trace(out/(name+".trace"));trace<<w.checksum()<<'\n';
        for(int t=1;t<=600;++t) {
            w.update();if(initial){copy.update();exact &= w.checksum()==copy.checksum();}
            if(t>1 && mid){middle.update();mid &= w.checksum()==middle.checksum();}
            if(t==1)mid=scatterSave(w,middle,out/(name+"-mid.sav"));
            trace<<w.checksum()<<'\n';arrived |= w.valid(id) && distf(w.ents[id].x,w.ents[id].y,gx,gy)<.01f;
            scatterFeedback(w);scatterFeedback(copy);scatterFeedback(middle);
        }
        std::ofstream(out/(name+".final"))<<w.checksum()<<'\n';World final;const bool finalSaved=scatterSave(w,final,out/(name+"-final.sav"));
        bool outcome=prepared && w.valid(id);
        if(mode==3 || mode==5 || mode==9 || mode==10 || mode==11)outcome &= distf(w.ents[id].x,w.ents[id].y,startX,startY)<.01f;
        if(mode==9)outcome &= w.ents[id].cargo.empty() && !w.ents[id].nativeTransportUnload.active() && w.countUnits(0,UnitType::GI)==1;
        else if(mode!=3 && mode!=5 && mode!=10 && mode!=11)outcome &= arrived;
        std::printf("VEHICLE_SCATTER OBSERVED mode=%d prepared=%d initial=%d ordered=%d middle=%d final=%d semantics=%d outcome=%d scene=%d\n",mode,prepared,initial,saved,mid,finalSaved,semantics,outcome,w.nativeRulesBaseVersion);
        check(prepared && initial,"SETUP",name);check(semantics && outcome,"COMMAND",name);check(exact && mid && saved && finalSaved,"REPLAY",name);
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_VEHICLE_SCATTER67_INPUT_DIR"))for(int mode=0;mode<13;++mode) {
        const std::filesystem::path old(directory);const auto name="vehicle-journey-"+std::to_string(mode);World w;EID id=INVALID_EID;
        std::ifstream(old/(name+".case"))>>id;bool exact=scatterRead(w,old/(name+"-before.sav")) && w.nativeRulesBaseVersion==67 && w.valid(id);
        if(exact)scatterCommand(w,id);std::ifstream trace(old/(name+".trace"));uint64_t expected=0;
        exact &= bool(trace>>expected) && w.checksum()==expected;
        for(int t=1;t<=600 && exact;++t){w.update();exact=bool(trace>>expected) && w.checksum()==expected;scatterFeedback(w);}
        check(exact,"LEGACY67",name);
    }
    std::printf("VEHICLE_SCATTER_RESULT failures=%d\n",failures);return failures;
}
}

int GameRegression::playerScatterRegression(const char* outputDir) {
    ScopedUserFile settings("settings.ini");if(!settings.ready())return 1;
    const std::filesystem::path out(outputDir);std::filesystem::create_directories(out);int failures=0;
    const auto check=[&](bool ok,const char* kind,const std::string& name){failures+=!ok;std::printf("PLAYER_SCATTER %s %s %s\n",ok?"PASS":"FAIL",kind,name.c_str());std::fflush(stdout);};
    const World base=scatterFixture();World fixtures[4];
    for(int f=0;f<2;++f)for(int p=0;p<2;++p){const auto rules=scatterRules(base,f,p);fixtures[f*2+p]=scatterFixture(&rules);}
    std::ifstream input("tests/fixtures/ra2-player-scatter-cases.csv");std::string line;std::getline(input,line);
    std::ofstream observed(out/"player-scatter-matrix.csv");observed<<"human,moving,fraidy,playerScatter,sequence,frame,expectedX,expectedY,actualX,actualY,draws,exact\n";
    int cases=0,matched=0;bool setup=true;
    while(std::getline(input,line)) {
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
        int human,moving,fraidy,ps,seq,frame,queued,scatter,force,ignore,destination,x,y,draws,commanded;
        if(!(row>>human>>moving>>fraidy>>ps>>seq>>frame>>queued>>scatter>>force>>ignore>>destination>>x>>y>>draws>>commanded)){setup=false;break;}
        if(seq!=1 && seq!=27)continue;
        World w=fixtures[fraidy*2+ps];w.map.bldOccRef=&w.bldOcc;w.players[0].isAI=!human;
        const auto id=w.spawnUnit(0,UnitType::GI,22.5f,22.5f);
        if(!w.valid(id)){setup=false;break;}
        const auto* def=w.ents[id].nativeIdentity.definition.get();
        setup &= def && def->fraidycat==bool(fraidy) && w.nativeCrushWarningRules.playerScatter==bool(ps);
        if(moving)scatterCommand(w,id,World::Cmd::Move);
        if(seq==27)w.ents[id].deployed=true;
        w.tick=frame*2;w.rng=Rng(1024);Rng expected=w.rng;if(draws)expected.range(0,4);
        const auto oldGoal=Vec2f{w.ents[id].goalX,w.ents[id].goalY};const auto oldState=w.ents[id].state;
        const auto oldPath=w.ents[id].path;scatterCommand(w,id);
        const auto& foot=w.ents[id];
        const bool same=w.rng.s==expected.s && (destination?
            foot.state==UState::Moving && foot.goalX==x+.5f && foot.goalY==y+.5f:
            foot.goalX==oldGoal.x && foot.goalY==oldGoal.y && foot.state==oldState && foot.path==oldPath);
        ++cases;matched+=same;
        observed<<human<<','<<moving<<','<<fraidy<<','<<ps<<','<<seq<<','<<frame<<','<<x<<','<<y<<','<<foot.goalX<<','<<foot.goalY<<','<<draws<<','<<same<<'\n';
    }
    std::printf("PLAYER_SCATTER MATRIX cases=%d matched=%d setup=%d\n",cases,matched,setup);
    check(setup && cases==96 && matched==cases && bool(observed),"SOURCE_MATRIX","manual-command-destination-and-rng");
    for(int mode=0;mode<6;++mode) {
        World w=mode==4?fixtures[3]:fixtures[0];w.map.bldOccRef=&w.bldOcc;
        const auto id=w.spawnUnit(0,UnitType::GI,22.5f,22.5f);const auto ref=w.entityRef(id);
        if(mode==1 || mode==4)scatterCommand(w,id,World::Cmd::Move);
        if(mode==2)scatterCommand(w,id,World::Cmd::Deploy);
        if(mode==3)scatterCommand(w,id,World::Cmd::Guard);
        if(mode==5)for(auto& cell:w.map.cells)cell.blocked=true;
        const auto name="journey-"+std::to_string(mode);std::ofstream(out/(name+".case"))<<id<<'\n';
        World before,copy,middle;bool exact=scatterSave(w,before,out/(name+"-before.sav"));
        const auto oldX=w.ents[id].goalX,oldY=w.ents[id].goalY;const auto oldRng=w.rng.s;
        scatterCommand(w,id);const bool declined=mode==1 || mode==2;
        bool semantics=declined?w.ents[id].goalX==oldX && w.ents[id].goalY==oldY && w.rng.s==oldRng:
            mode==5?w.ents[id].path.empty():w.ents[id].state==UState::Moving && !w.ents[id].guard;
        if(mode==5){Rng expected(oldRng);expected.range(0,4);semantics &= w.rng.s==expected.s;}
        exact &= scatterSave(w,copy,out/(name+"-ordered.sav"));bool mid=false,arrived=false;
        const float targetX=w.ents[id].goalX,targetY=w.ents[id].goalY;
        std::ofstream trace(out/(name+".trace"));trace<<w.checksum()<<'\n';
        for(int at=1;at<=600;++at) {
            w.update();copy.update();exact &= w.checksum()==copy.checksum();
            if(at>1 && mid){middle.update();mid &= w.checksum()==middle.checksum();}
            if(at==1)mid=scatterSave(w,middle,out/(name+"-mid.sav"));
            trace<<w.checksum()<<'\n';arrived |= w.valid(ref) && distf(w.ents[id].x,w.ents[id].y,targetX,targetY)<.01f;
            scatterFeedback(w);scatterFeedback(copy);scatterFeedback(middle);
        }
        std::ofstream(out/(name+".final"))<<w.checksum()<<'\n';World final;exact &= scatterSave(w,final,out/(name+"-final.sav"));
        check(semantics && w.valid(ref) && (mode==2?w.ents[id].deployed:mode==5?true:arrived),"COMMAND",name);
        check(exact && mid,"REPLAY",name);
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_PLAYER_SCATTER66_INPUT_DIR"))for(int mode=0;mode<6;++mode) {
        const std::filesystem::path old(directory);const auto name="journey-"+std::to_string(mode);World w;
        EID id=INVALID_EID;std::ifstream(old/(name+".case"))>>id;
        bool exact=scatterRead(w,old/(name+"-before.sav")) && w.nativeRulesBaseVersion==66 && w.valid(id);
        if(exact)scatterCommand(w,id);
        std::ifstream trace(old/(name+".trace"));uint64_t expected=0;exact &= bool(trace>>expected) && w.checksum()==expected;
        for(int at=1;at<=600 && exact;++at){w.update();exact=bool(trace>>expected) && w.checksum()==expected;scatterFeedback(w);}
        check(exact,"LEGACY66",name);
    }
    for(int mode=0;mode<4;++mode) {
        World w=fixtures[0];w.map.bldOccRef=&w.bldOcc;const auto id=w.spawnUnit(mode==0?1:0,UnitType::GI,22.5f,22.5f);
        World::Cmd c{};c.type=World::Cmd::Scatter;c.ids={id};w.bindCommand(c);
        if(mode==1){w.kill(id,false);w.spawnUnit(0,UnitType::GI,22.5f,22.5f);}
        if(mode==2)w.ents[id].chrono=20;
        if(mode==3)w.nativeEvents.inputLocked=true,w.nativeEvents.active=true;
        const auto before=w.checksum();w.applyCmd(0,c);check(w.checksum()==before,"ADMISSION","owner-generation-immobilized-inputlock-"+std::to_string(mode));
    }
    {
        World w=fixtures[0];w.map.bldOccRef=&w.bldOcc;const auto foot=w.spawnUnit(0,UnitType::GI,22.5f,22.5f);
        const auto tank=w.spawnUnit(0,UnitType::Grizzly,35.5f,22.5f);World vehicle=w;vehicle.map.bldOccRef=&vehicle.bldOcc;
        scatterCommand(w,foot,World::Cmd::Move);scatterCommand(vehicle,foot,World::Cmd::Move);
        World::Cmd c{};c.type=World::Cmd::Scatter;c.ids={foot,tank};w.bindCommand(c);w.applyCmd(0,c);scatterCommand(vehicle,tank);
        check(w.checksum()==vehicle.checksum(),"MIXED","declined-infantry-retains-vehicle-policy-and-rng");
    }
    for(bool moving:{false,true}) {
        World w=fixtures[0];w.map.bldOccRef=&w.bldOcc;const auto id=w.spawnUnit(0,UnitType::GI,22.5f,22.5f);
        if(moving)scatterCommand(w,id,World::Cmd::Move);
        w.nativeEvents.active=true;NativeTeamState team;team.members={id};w.nativeEvents.teams.push_back(team);
        scatterCommand(w,id);
        check(moving?w.nativeEvents.teams[0].members==std::vector<int>{id}:w.nativeEvents.teams[0].members.empty(),
            "CONTROLLER",moving?"declined-order-retains-team":"admitted-order-releases-team");
    }
    {
        selectRulesChoice(RulesChoice::RA2);g_script.shutdown();resetMatchInteraction();ais.clear();
        world=fixtures[0];world.map.bldOccRef=&world.bldOcc;localPlayer=0;campaignMission=-1;phase=Phase::InGame;
        paused=showMenu=gameOver=false;netGame=true;pendingCmds.clear();
        const auto id=world.spawnUnit(0,UnitType::GI,22.5f,22.5f);sel={id};selBuilding=INVALID_EID;
        const auto prior=world.checksum();const int key=keyBind[KA_Scatter];keyBind[KA_Scatter]=KEY_X;
        sim={};sim.active=true;sim.pos={-100,-100};sim.keysPressed.insert(KEY_X);handleInput();
        sim={};keyBind[KA_Scatter]=key;
        bool ok=world.checksum()==prior && pendingCmds.size()==1 && pendingCmds.front().type==World::Cmd::Scatter
            && pendingCmds.front().identitiesBound && pendingCmds.front().ids==sel;
        World direct=world;direct.map.bldOccRef=&direct.bldOcc;scatterCommand(direct,id);
        for(const auto& c:pendingCmds)world.applyCmd(0,c);pendingCmds.clear();netGame=false;
        check(ok && world.checksum()==direct.checksum(),"INPUT","X-queues-bound-command-before-simulation");
    }
    failures+=vehicleScatterRegression(base,out);
    std::printf("PLAYER_SCATTER_RESULT failures=%d\n",failures);return failures;
}
