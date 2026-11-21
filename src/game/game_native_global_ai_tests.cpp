#include "game_regression.h"
#include "game/native_nearby_clearance.h"
#include "game/native_team_geometry.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>

namespace {
std::vector<char> save(const World& w) {
    std::vector<char> bytes;FILE* file=tmpfile();if(!file)return bytes;
    if(w.saveGame(file)){const long n=ftell(file);if(n>0){bytes.resize(n);rewind(file);if(fread(bytes.data(),1,bytes.size(),file)!=bytes.size())bytes.clear();}}
    fclose(file);return bytes;
}
bool load(World& w,const std::vector<char>& bytes) {
    FILE* file=tmpfile();if(!file)return false;
    const bool wrote=fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();rewind(file);const bool ok=wrote && w.loadGame(file);fclose(file);return ok;
}
}

int GameRegression::nativeGlobalAIRegression(const char* outputDir) {
    const std::string directory=outputDir;
    for(const char* edition:{"ra2","yr"})if(!FileExists((directory+"/"+edition+"-profile.ini").c_str()))return 77;
    int failures=0;const auto check=[&](bool ok,const char* text){std::printf("GLOBAL_AI %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    {
        std::ifstream fixture("tests/fixtures/native-team-staging-ra2.csv");
        std::string line;bool exact=fixture.good();int samples=0;
        while(std::getline(fixture,line)) {
            if(line.empty() || line[0]=='#')continue;
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            int action,distance,x,y;Vec2i own,enemy;
            if(!(row>>action>>own.x>>own.y>>enemy.x>>enemy.y>>distance>>x>>y)){exact=false;break;}
            exact &= NativeTeamGeometry::cell(own,enemy,distance,action==53)==Vec2i{x,y};++samples;
        }
        check(exact && samples==800,"compiled source staging geometry matches 800 original RA2 instruction samples");
    }
    for(const char* edition:{"ra2","yr"}) {
        const auto profile=directory+"/"+edition+"-profile.ini";
        const bool initialized=world.init(64,64,20261118,1,1,{Faction::Soviet,Faction::Allies},0,nullptr,false,false,profile.c_str(),{Country::Russia,Country::America});
        check(initialized && world.nativeSceneVersion==41 && world.nativeAIRules.hasOriginalNearbyGeometry() && world.nativeAIRules.hasOriginalSourceCoordinates() && world.nativeAIRules.isGlobal() && world.nativeEventDefinition
            && world.nativeEventDefinition->aiOnly && world.nativeEventDefinition->playerHouse==-1,"Scene40 imports the source Nearby graph with no human campaign house");
        if(!initialized || !world.nativeAIRules.isGlobal()){++failures;continue;}
        const size_t expected=std::string(edition)=="ra2"?107:165;
        check(world.nativeAIRules.triggers.size()==expected && std::all_of(world.nativeAIRules.triggers.begin(),world.nativeAIRules.triggers.end(),[](const auto& t){return t.enabled;}),
            "all original global declarations start enabled even without a local enable table");
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& cell:world.map.cells){cell=Cell{};cell.terrain=Terrain::Clear;}
        world.rebuildUnitOcc();world.rebuildAirOcc();world.cratesEnabled=false;
        for(auto& p:world.players){p.active=true;p.defeated=false;p.money=100000;p.isAI=false;}
        world.players[0].isAI=true;
        world.spawnBuilding(0,BldType::ConYard,3,3,true);world.spawnBuilding(0,BldType::WarFactory,10,5,true);
        world.spawnBuilding(0,BldType::Radar,3,10,true);world.spawnBuilding(0,BldType::Barracks,10,12,true);
        world.spawnBuilding(0,BldType::BattleLab,3,17,true);
        for(int i=0;i<6;++i)world.spawnBuilding(0,BldType::PowerPlant,18+i*4,4,true);
        const EID enemy=world.spawnBuilding(1,BldType::ConYard,41,24,true);
        check(world.configureNativeSkirmishAI(0,3,true,1) && world.nativeAI.houses[0].sourceDifficulty==2
            && world.nativeAI.houses[0].sourceEnemy==1 && !world.nativeEvents.inputLocked,
            "Brutal maps explicitly to source Hard and each house retains its own enemy");
        const auto originalDelay=world.nativeAI.houses[0].nextPass;
        check(world.configureNativeSkirmishAI(0,2,true,1) && world.nativeAI.houses[0].nextPass==originalDelay
            && !world.configureNativeSkirmishAI(0,2,true,0),"repeated configuration preserves cadence and refuses an allied/self enemy");
        check(world.validateNativeAIGlobalState() && world.nativeAI.jobs.empty(),"global production starts without independent campaign factory jobs");
        const int v3=world.nativeUnitIndex("V3"),escort=world.nativeUnitIndex("HTK");int definition=-1;
        bool hardV3=false;for(int i=0;i<(int)world.nativeAIRules.triggers.size();++i){const auto& t=world.nativeAIRules.triggers[i];
            const auto& force=world.nativeEventDefinition->taskForces[world.nativeEventDefinition->teams[t.first].taskForce];
            if(std::any_of(force.units.begin(),force.units.end(),[](const auto& u){return u.second=="V3";}))hardV3 |= world.nativeAITriggerEligible(0,i);}
        check(!hardV3,"original source Hard flags do not invent an enabled V3 trigger");
        world.configureNativeSkirmishAI(0,1,true,1);
        for(int i=0;i<(int)world.nativeAIRules.triggers.size();++i){const auto& t=world.nativeAIRules.triggers[i];
            const auto& force=world.nativeEventDefinition->taskForces[world.nativeEventDefinition->teams[t.first].taskForce];
            const bool v3s=std::any_of(force.units.begin(),force.units.end(),[](const auto& u){return u.first==2 && u.second=="V3";});
            const bool escorts=std::any_of(force.units.begin(),force.units.end(),[](const auto& u){return u.first==2 && u.second=="HTK";});
            if(v3s && escorts && world.nativeAITriggerEligible(0,i)){definition=t.first;break;}}
        check(v3>=0 && escort>=0 && definition>=0 && world.nativeAITypeBuildable(0,v3),"Normal admits the enabled original two-V3 plus two-Flak-Track force with its actual ownership condition");
        if(definition<0 || v3<0)continue;
        if(std::string(edition)=="yr")world.spawnBuilding(0,BldType::IndustrialPlant,35,5,true);
        const int quote=world.nativeProductionCost(0,v3);
        check(quote==world.unitProductionCost(0,UnitType::V3Launcher) && (std::string(edition)!="yr" || quote<world.nativeTypes[v3]->unit.cost),
            "global requests use the shared source quote including the actual YR industrial-plant discount");
        const int team=world.createNativeTeam(definition,false);if(team<0){check(false,"create original V3 demand");continue;}
        world.nativeEvents.teams[team].memberHouse=0;world.nativeAI.autocreate[definition]=1;
        const auto beforeMoney=world.players[0].money;const auto beforeProduced=world.producedUnits[0];bool paid=false,exact=true,quotes=true;int produced=0;
        World replay;bool checkpoint=false;std::vector<char> first;
        for(int frame=0;frame<16000;++frame) {
            world.update();if(checkpoint){replay.update();exact &= world.checksum()==replay.checksum();}
            auto& job=world.players[0].unitProd[1];
            if(frame%500==0)std::printf("GLOBAL_AI QUEUE edition=%s tick=%llu selector=%d active=%d progress=%d work=%d paid=%d quote=%d queued=%zu V3roster=%d HTKroster=%d\n",
                edition,(unsigned long long)world.tick,job.typeIdx,job.active,job.progress,job.workRemainder,job.paid,job.totalCost,
                world.players[0].unitQueue[1].size(),world.nativeAITeamCount(team,v3),world.nativeAITeamCount(team,escort));
            if(job.active && job.typeIdx==NATIVE_PRODUCTION_BASE+v3)quotes &= job.totalCost==quote;
            if(job.active && job.typeIdx==NATIVE_PRODUCTION_BASE+v3 && job.paid>0 && !checkpoint){first=save(world);checkpoint=load(replay,first);exact &= checkpoint && world.checksum()==replay.checksum();paid=true;}
            produced=0;for(const auto& actor:world.ents)produced+=actor.alive && actor.player==0 && actor.nativeIdentity.type==v3;
            if(produced>=2 && world.nativeEvents.teams[team].assembled)break;
        }
        check(paid && checkpoint && exact,"a genuinely paid normal V3 queue resumes tick-identically with the original team request");
        std::printf("GLOBAL_AI DIAG edition=%s tick=%llu V3=%d HTK=%d total=%llu members=%zu rosterV3=%d assembled=%d quotes=%d teams=%zu\n",
            edition,(unsigned long long)world.tick,produced,world.nativeAITeamCount(team,escort),(unsigned long long)(world.producedUnits[0]-beforeProduced),
            world.nativeEvents.teams[team].members.size(),world.nativeAITeamCount(team,v3),world.nativeEvents.teams[team].assembled,quotes,world.nativeEvents.teams.size());
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && (world.ents[id].nativeIdentity.type==v3 || world.ents[id].nativeIdentity.type==escort))
            std::printf("GLOBAL_AI ACTOR id=%d type=%s state=%d team=%d pos=%.2f,%.2f\n",id,world.ents[id].nativeIdentity.definition->id.c_str(),(int)world.ents[id].state,
                world.nativeAI.memberTeams.contains(world.ents[id].nativeIdentity.object)?world.nativeAI.memberTeams.at(world.ents[id].nativeIdentity.object):-1,world.ents[id].x,world.ents[id].y);
        check(produced==2 && world.producedUnits[0]==beforeProduced+4 && world.nativeAITeamCount(team,escort)==2
            && quotes && world.players[0].money<beforeMoney && world.nativeAI.jobs.empty(),
            "the original mixed force leaves real exits, preserves frozen quotes and pays for all four actors without duplicate jobs");
        check(world.nativeAITeamCount(team,v3)==2 && world.nativeAI.memberTeams.size()>=2,"recruitment binds the produced actors to a persistent source roster");
        const auto final=save(world);check(!final.empty() && std::string(final.data(),8)=="RA2WLD62","the opt-in global graph writes World62 with persistent source house/Foot state");
        std::filesystem::create_directories(directory+"/"+edition);
        const auto dump=[&](const char* name,const std::vector<char>& data){std::ofstream file(directory+"/"+edition+"/"+name,std::ios::binary);file.write(data.data(),data.size());};
        dump("paid.world",first);dump("produced.world",final);
        if(!final.empty()) {
            auto bad=final;bad.pop_back();World rejected;check(!load(rejected,bad),"truncating the actual AI extension is rejected");
            const auto marker=std::string(final.data(),final.size()).rfind("AIGL");
            if(marker!=std::string::npos && marker+12<bad.size()){bad=final;bad[marker+12]=2;check(!load(rejected,bad),"a nonboolean per-house configuration flag is rejected");}
        }
        // Run the unchanged source script: gather at own base (54), gather
        // near the enemy (53), then its actual attack/success lines.
        world.nativeAI.houses[0].enabled=world.nativeAI.houses[0].production=false;
        bool ownGather=false,enemyGather=false,ownArrived=false,enemyArrived=false,missile=false,damage=false,stagingExact=true;World stagingReplay;bool stagingLoaded=false;
        std::vector<char> enemyStage;
        for(int frame=0;frame<6500 && !(ownGather && enemyGather && missile && damage);++frame) {
            const auto previous=world.nativeEvents.teams[team];
            std::vector<EID> candidates;
            if(world.valid(enemy))for(const auto& rocket:world.nativeRockets)if(world.valid(rocket.actor)
                && rocket.phase==NativeRocketPhase::Flight && world.valid(rocket.source) && world.ents[rocket.source].nativeIdentity.type==v3) {
                const auto& target=world.ents[enemy];
                if(rocket.destinationX>=target.x && rocket.destinationY>=target.y
                    && rocket.destinationX<=target.x+bldDef(target).w && rocket.destinationY<=target.y+bldDef(target).h)candidates.push_back(rocket.actor);
            }
            std::unique_ptr<World> cancelled;
            const int before=world.valid(enemy)?world.ents[enemy].hp:0;
            if(!candidates.empty()) {
                // A launch plus any target damage can count the HTKs' earlier
                // shots. Cancel only V3 missiles in a one-tick control copy;
                // the actual source-script trajectory receives no new orders.
                cancelled=std::make_unique<World>(world);cancelled->map.bldOccRef=&cancelled->bldOcc;
                for(EID actor:candidates)cancelled->kill(actor,false);
                cancelled->update();
            }
            world.update();if(stagingLoaded){stagingReplay.update();stagingExact &= world.checksum()==stagingReplay.checksum();}
            if(cancelled) {
                const int actual=world.valid(enemy)?world.ents[enemy].hp:0,control=cancelled->valid(enemy)?cancelled->ents[enemy].hp:0;
                const bool retired=std::any_of(candidates.begin(),candidates.end(),[&](EID actor){return !world.valid(actor);});
                if(retired && actual<control){damage=true;std::printf("GLOBAL_AI IMPACT edition=%s tick=%llu before=%d actual=%d cancelled=%d additionalDamage=%d\n",
                    edition,(unsigned long long)world.tick,before,actual,control,control-actual);}
            }
            const auto& live=world.nativeEvents.teams[team];
            const auto& script=world.nativeEventDefinition->scripts[world.nativeEventDefinition->teams[live.definition].script];
            const int action=live.step<(int)script.steps.size()?script.steps[live.step].x:-1;
            if(previous.lineStarted && previous.focusCell>=0 && previous.step<(int)script.steps.size()
                && live.step!=previous.step && (script.steps[previous.step].x==53 || script.steps[previous.step].x==54)) {
                const auto point=nativeNearbyUnpack(previous.focusCell);
                const float x=point.x+.5f,y=point.y+.5f;
                const bool allArrived=std::all_of(previous.members.begin(),previous.members.end(),[&](EID id){
                    return world.valid(id) && (world.ents[id].airborne() || world.ents[id].navLayer==previous.focusLayer)
                        && distf(world.ents[id].x,world.ents[id].y,x,y)<=world.nativeEventDefinition->relaxedStrayDistance;});
                if(script.steps[previous.step].x==54)ownArrived=allArrived;else enemyArrived=allArrived;
            }
            ownGather |= action==54 && live.lineStarted && live.focusCell>=0;
            enemyGather |= action==53 && live.lineStarted && live.focusCell>=0;
            if(enemyGather && !stagingLoaded){enemyStage=save(world);dump("staging.world",enemyStage);stagingLoaded=load(stagingReplay,enemyStage);stagingExact &= stagingLoaded && save(stagingReplay)==enemyStage;}
            if(frame%500==0)std::printf("GLOBAL_AI SCRIPT edition=%s tick=%llu step=%d action=%d started=%d focus=%d blocked=%d\n",edition,
                (unsigned long long)world.tick,live.step,action,live.lineStarted,live.focusCell,live.blockedScript);
            for(const auto& actor:world.ents)missile |= actor.alive && actor.nativeIdentity.definition && actor.nativeIdentity.definition->nativeRocket;
        }
        check(ownGather && enemyGather && ownArrived && enemyArrived && stagingLoaded && stagingExact,"source scripts 54/53 physically gather the paid mixed force and preserve exact staging continuation");
        check(missile && damage,"the unchanged source script makes actually produced V3s launch missiles and damage the enemy without an injected attack order");
        dump("source-fire.world",save(world));
        for(const auto* state:std::array<const std::vector<char>*,2>{&final,&enemyStage}) {
            World isolated;bool held=load(isolated,*state);
            if(held) {
                isolated.nativeAI.houses[0].enabled=isolated.nativeAI.houses[0].production=false;
                const auto initial=isolated.nativeEvents.teams[team];
                const auto focus=nativeNearbyUnpack(initial.focusCell);const int cx=focus.x,cy=focus.y;
                held=initial.lineStarted && initial.focusCell>=0;
                for(EID id:initial.members)if(isolated.valid(id))isolated.orderStop({id});
                // A clear destination surrounded by four cells of water is
                // selected but unreachable to the actual tracked force.
                for(int y=cy-4;y<=cy+4;++y)for(int x=cx-4;x<=cx+4;++x)
                    if(isolated.map.inBounds(x,y) && (x!=cx || y!=cy))isolated.map.at(x,y).terrain=Terrain::Water;
                for(int frame=0;frame<900;++frame)isolated.update();
                const auto& live=isolated.nativeEvents.teams[team];
                held &= live.alive && live.step==initial.step && live.focusCell==initial.focusCell;
                const auto blocked=save(isolated);World restored;held &= !blocked.empty() && load(restored,blocked) && save(restored)==blocked;
                for(int frame=0;frame<120 && held;++frame){isolated.update();restored.update();held &= isolated.checksum()==restored.checksum();}
                dump(state==&final?"unreachable-own.world":"unreachable-enemy.world",blocked);
            }
            check(held,"an unreachable gathering cell remains on its actual source line and survives exact continuation");
        }
        int infantryDefinition=-1;const int e2=world.nativeUnitIndex("E2");
        for(int t=0;t<(int)world.nativeEventDefinition->teams.size();++t){const auto& force=world.nativeEventDefinition->taskForces[world.nativeEventDefinition->teams[t].taskForce];
            if(world.nativeTeamBlocker(t)==-1 && std::any_of(force.units.begin(),force.units.end(),[](const auto& u){return u.second=="E2";})){infantryDefinition=t;break;}}
        EID carrier=INVALID_EID;for(EID id:world.nativeEvents.teams[team].members)if(world.valid(id) && world.ents[id].nativeIdentity.type==escort){carrier=id;break;}
        if(infantryDefinition<0 || !world.valid(carrier) || e2<0){check(false,"actual source infantry and produced transport are available");continue;}
        // Boarding is a separate public-order fixture until source 14/43 is
        // implemented. Retire the completed siege fixture before taking its
        // carrier, so a live gathering/attack line cannot redirect it.
        world.finishNativeTeam(team);world.orderStop({carrier});
        const int infantryTeam=world.createNativeTeam(infantryDefinition,false);world.nativeEvents.teams[infantryTeam].memberHouse=0;
        const EID passenger=world.spawnUnit(0,UnitType::Conscript,world.ents[carrier].x+1.5f,world.ents[carrier].y+1.5f,e2);
        world.nativeEvents.teams[infantryTeam].members.push_back(passenger);world.nativeAIJoined(infantryTeam,passenger);
        const auto key=world.ents[passenger].nativeIdentity.object;
        bool boarding=world.orderBoard({passenger},carrier).accepted==1;
        for(int frame=0;frame<240 && world.valid(passenger);++frame)world.update();
        boarding &= !world.valid(passenger) && world.ents[carrier].cargo.size()==1 && world.nativeAITeamCount(infantryTeam,e2)==1;
        const auto aboard=save(world);World boarded;bool aboardExact=boarding && load(boarded,aboard) && boarded.checksum()==world.checksum();
        for(int frame=0;frame<60 && aboardExact;++frame){world.update();boarded.update();aboardExact &= world.checksum()==boarded.checksum();}
        check(boarding && aboardExact,"real boarding retains the source team roster through cargo and World61 continuation");
        dump("boarding.world",aboard);
        world.orderUnload({carrier});boarded.orderUnload({carrier});
        for(int frame=0;frame<180 && aboardExact;++frame){world.update();boarded.update();aboardExact &= world.checksum()==boarded.checksum();if(world.ents[carrier].cargo.empty())break;}
        EID returned=INVALID_EID;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.object==key)returned=id;
        check(aboardExact && world.valid(returned) && world.nativeAITeamCount(infantryTeam,e2)==1
            && std::find(world.nativeEvents.teams[infantryTeam].members.begin(),world.nativeEvents.teams[infantryTeam].members.end(),returned)!=world.nativeEvents.teams[infantryTeam].members.end(),
            "real unloading reconnects the physical source actor to its original team instead of a retired numeric slot");
        dump("unloaded.world",save(world));
    }
    for(const char* name:{"GGI-normal-ground-world60.sav","E1-elite-ground-world60.sav","GHOST-normal-building-world60.sav","CLEG-elite-ground-world60.sav"}) {
        const std::string path=std::string("tests/fixtures/native-crew-v60/")+name;
        if(!FileExists(path.c_str())){check(false,"required real World60 fixture is present");continue;}
        FILE* file=fopen(path.c_str(),"rb");World old;bool ok=file && old.loadGame(file);if(file)fclose(file);
        const auto data=save(old);World other;ok &= !data.empty() && std::string(data.data(),8)=="RA2WLD60" && load(other,data) && old.checksum()==other.checksum();
        std::ifstream originalFile(path,std::ios::binary);const std::vector<char> original((std::istreambuf_iterator<char>(originalFile)),{});
        ok &= data==original;
        {std::ofstream output(directory+"/legacy-"+name,std::ios::binary);output.write(data.data(),data.size());}
        for(int i=0;i<120 && ok;++i){old.update();other.update();ok &= old.checksum()==other.checksum();}
        check(ok,"actual pre-global World60 fixtures keep their schema and exact continuation");
    }
    return failures;
}
