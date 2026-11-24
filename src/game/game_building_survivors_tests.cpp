#include "game_regression.h"
#include "game/script.h"
#include "scoped_user_file.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
World survivorFixture(const World::RulesSnapshot* snapshot=nullptr) {
    World w;w.init(64,64,20261124,2,0,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,
        snapshot?nullptr:"assets/rules/profiles/ra2.ini",{Country::America,Country::Russia},snapshot);
    w.cratesEnabled=false;w.crates.clear();
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& cell:w.map.cells)cell=Cell{};
    for(auto& p:w.players){p.money=10000;p.defeated=false;p.isAI=false;}
    w.spawnBuilding(0,BldType::ConYard,48,48,true);w.spawnBuilding(1,BldType::ConYard,2,48,true);
    return w;
}
int survivorType(const World& w,const char* name) {
    for(int i=0;i<(int)w.nativeTypes.size();++i)if(w.nativeTypes[i]->id==name)return i;
    return -1;
}
void survivorSell(World& w,int owner,EID id) {
    World::Cmd c{};c.type=World::Cmd::SellBuilding;c.ids={id};w.bindCommand(c);w.applyCmd(owner,c);
}
bool survivorSave(World& w,World& copy,const std::filesystem::path& path) {
    FILE* f=std::fopen(path.string().c_str(),"wb");bool ok=f && w.saveGame(f);if(f)std::fclose(f);
    f=std::fopen(path.string().c_str(),"rb");ok &= f && copy.loadGame(f);if(f)std::fclose(f);
    return ok && copy.checksum()==w.checksum();
}
void survivorFeedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
int survivors(const World& w) {
    return (int)std::count_if(w.ents.begin(),w.ents.end(),[](const auto& e){return e.alive && !e.isBuilding;});
}
struct Case {const char* name;const char* type;int owner,count;bool ai=false;int capture=0;};
constexpr Case survivorCases[]{
    {"allied-yard","GACNST",0,3},{"soviet-yard","NACNST",1,5},
    {"allied-power","GAPOWR",0,1},{"soviet-power","NAPOWR",1,1},
    {"allied-refinery","GAREFN",0,1},{"soviet-refinery","NAREFN",1,1},
    {"uncrewed-defense","GAPILL",0,0},{"ai-soviet-power","NAPOWR",1,2,true},
    {"captured-soviet-yard","NACNST",0,3,false,1},
    {"recaptured-soviet-yard","NACNST",1,3,false,2},
};
}

int GameRegression::buildingSurvivorsRegression(const char* outputDir) {
    ScopedUserFile settings("settings.ini");if(!settings.ready())return 1;
    const std::filesystem::path out(outputDir);std::filesystem::create_directories(out);int failures=0;
    const auto check=[&](bool ok,const char* kind,const char* name){failures+=!ok;std::printf("BUILDING_SURVIVORS %s %s %s\n",ok?"PASS":"FAIL",kind,name);std::fflush(stdout);};
    for(const auto& c:survivorCases) {
        World w=survivorFixture();w.map.bldOccRef=&w.bldOcc;const int type=survivorType(w,c.type);
        if(type<0){check(false,"SETUP",c.name);continue;}
        const auto def=w.nativeTypes[type];const EID id=w.spawnBuilding(c.capture==1?1:c.owner,def->building.type,20,20,true,type);
        if(c.capture==1)w.changeActorOwner(id,c.owner);
        if(c.capture==2){w.changeActorOwner(id,0);w.changeActorOwner(id,1);}
        if(c.capture)w.synchronizeBuildingOwners();
        w.players[c.owner].isAI=c.ai;const auto ref=w.entityRef(id);survivorSell(w,c.owner,id);
        World copy,middle;bool exact=survivorSave(w,copy,out/(std::string(c.name)+"-ordered.sav")),mid=false;
        std::ofstream trace(out/(std::string(c.name)+".trace"));trace<<w.checksum()<<'\n';
        bool admission=w.valid(ref) && w.ents[id].selling && survivors(w)==0;
        bool phase=false;int first=-1,number=-1;bool identity=true;
        for(int at=1;at<=600;++at) {
            w.update();if(exact){copy.update();exact=w.checksum()==copy.checksum();}
            if(at>1 && mid){middle.update();mid=w.checksum()==middle.checksum();}
            const int n=survivors(w);if(n && first<0)first=at;
            if(at==1){number=n;phase=w.valid(ref) && w.players[c.owner].money==10000;
                mid=survivorSave(w,middle,out/(std::string(c.name)+"-released.sav"));
                int engineers=0;
                for(const auto& e:w.ents)if(e.alive && !e.isBuilding){
                    identity &= e.player==c.owner && e.nativeIdentity.definition && !e.nativeIdentity.object.empty()
                        && e.hp==unitDef(e).hp;
                    if(e.nativeIdentity.definition){const auto& name=e.nativeIdentity.definition->id;engineers+=name=="ENGINEER";
                        identity &= name=="E1" || name=="E2" || name=="ENGINEER" || name=="CTECH";
                        if(c.capture)identity &= name!="ENGINEER";}
                }
                identity &= engineers<=1;
            }
            trace<<w.checksum()<<'\n';survivorFeedback(w);survivorFeedback(copy);survivorFeedback(middle);
        }
        std::ofstream(out/(std::string(c.name)+".final"))<<w.checksum()<<'\n';
        World final;exact &= survivorSave(w,final,out/(std::string(c.name)+"-final.sav"));
        std::printf("BUILDING_SURVIVORS OBSERVED name=%s scene=%d count=%d expected=%d first=%d phase=%d identity=%d\n",c.name,w.nativeRulesBaseVersion,number,c.count,first,phase,identity);
        check(admission && phase && number==c.count && identity && (!c.count || first==1),"COMMAND",c.name);
        check(exact && mid,"REPLAY",c.name);
        bool exited=true;
        for(const auto& e:w.ents)if(e.alive && !e.isBuilding)
            exited &= e.x<20 || e.x>=20+def->building.w || e.y<20 || e.y>=20+def->building.h;
        check(exited,"EXIT",c.name);
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_SALE_SURVIVORS63_INPUT_DIR"))for(const auto& c:survivorCases) {
        const std::filesystem::path old(directory);World w;FILE* f=std::fopen((old/(std::string(c.name)+"-ordered.sav")).string().c_str(),"rb");
        bool exact=f && w.loadGame(f);if(f)std::fclose(f);std::ifstream trace(old/(std::string(c.name)+".trace")),final(old/(std::string(c.name)+".final"));uint64_t expected=0;
        exact &= w.nativeRulesBaseVersion==63 && bool(trace>>expected) && w.checksum()==expected;
        for(int at=1;at<=600 && exact;++at){w.update();exact=bool(trace>>expected) && w.checksum()==expected;survivorFeedback(w);}
        exact &= bool(final>>expected) && w.checksum()==expected && survivors(w)==0;check(exact,"LEGACY63",c.name);
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_SCATTER64_INPUT_DIR"))for(const auto& c:survivorCases) {
        const std::filesystem::path old(directory);World w;FILE* f=std::fopen((old/(std::string(c.name)+"-ordered.sav")).string().c_str(),"rb");
        bool exact=f && w.loadGame(f);if(f)std::fclose(f);std::ifstream trace(old/(std::string(c.name)+".trace"));uint64_t expected=0;
        exact &= w.nativeRulesBaseVersion==64 && bool(trace>>expected) && w.checksum()==expected;
        for(int at=1;at<=600 && exact;++at){w.update();exact=bool(trace>>expected) && w.checksum()==expected;survivorFeedback(w);}
        check(exact,"LEGACY64",c.name);
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_NEARBY65_INPUT_DIR"))for(const auto& c:survivorCases) {
        const std::filesystem::path old(directory);World w;FILE* f=std::fopen((old/(std::string(c.name)+"-ordered.sav")).string().c_str(),"rb");
        bool exact=f && w.loadGame(f);if(f)std::fclose(f);std::ifstream trace(old/(std::string(c.name)+".trace"));uint64_t expected=0;
        exact &= w.nativeRulesBaseVersion==65 && bool(trace>>expected) && w.checksum()==expected;
        for(int at=1;at<=600 && exact;++at){w.update();exact=bool(trace>>expected) && w.checksum()==expected;survivorFeedback(w);}
        check(exact,"LEGACY65",c.name);
    }
    {
        const World ordinary=survivorFixture();World::RulesSnapshot rules{ordinary.nativeScenarioSource};
        const std::string divisor="AlliedSurvivorDivisor = 500";const auto at=rules.source.find(divisor);
        bool setup=at!=std::string::npos;if(setup)rules.source.replace(at,divisor.size(),"AlliedSurvivorDivisor = 5000");
        const World base=survivorFixture(&rules);const int yardType=survivorType(base,"GACNST");
        std::ifstream input("tests/fixtures/ra2-nearby-scatter-cases.csv");std::string line;std::getline(input,line);
        std::ofstream observed(out/"nearby-sale-matrix.csv");observed<<"localX,localY,frame,poseX,poseY,expectedX,expectedY,actualX,actualY,exact\n";
        int cases=0,matched=0;
        while(std::getline(input,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            int kind,lx,ly,frame,px,py,heading,roll,destination,gx,gy,draws;
            if(!(row>>kind>>lx>>ly>>frame>>px>>py>>heading>>roll>>destination>>gx>>gy>>draws)){setup=false;break;}
            if(kind!=0)continue;
            World w=base;w.map.bldOccRef=&w.bldOcc;
            const auto yard=w.spawnBuilding(0,BldType::ConYard,20,20,true,yardType);const auto serial=w.nativeProductionSerial;
            uint64_t seed=1;Rng expected(seed);
            for(;;++seed) {
                Rng probe(seed);probe.range(0,99);const int cell=probe.range(0,15),start=probe.range(0,3);
                const int spot=start<2?0:start-1;
                const int sx=spot==1?64:192,sy=spot==0?64:192;
                if(cell==ly*4+lx && sx==px && sy==py){probe.range(0,4);expected=probe;break;}
            }
            w.tick=frame*2;w.rng=Rng(seed);survivorSell(w,0,yard);w.update();
            EID first=INVALID_EID;const std::string object="production:0:"+std::to_string(serial);
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && !w.ents[id].isBuilding && w.ents[id].nativeIdentity.object==object)first=id;
            const bool same=w.valid(first) && survivors(w)==1 && w.ents[first].x==20+lx+px/256.f && w.ents[first].y==20+ly+py/256.f
                && w.ents[first].goalX==gx+.5f && w.ents[first].goalY==gy+.5f && w.rng.s==expected.s;
            ++cases;matched+=same;
            observed<<lx<<','<<ly<<','<<frame<<','<<px<<','<<py<<','<<gx<<','<<gy<<','
                <<(w.valid(first)?w.ents[first].goalX:-1)<<','<<(w.valid(first)?w.ents[first].goalY:-1)<<','<<same<<'\n';
        }
        std::printf("BUILDING_SURVIVORS MATRIX cases=%d matched=%d setup=%d\n",cases,matched,setup);
        check(setup && cases==384 && matched==cases && bool(observed),"SOURCE_MATRIX","sale-subspots-frames-and-single-scatter-draw");
    }
    for(int mode=0;mode<5;++mode) {
        World w=survivorFixture();w.map.bldOccRef=&w.bldOcc;
        const EID yard=w.spawnBuilding(0,BldType::ConYard,20,20,true,survivorType(w,"GACNST"));const auto source=w.entityRef(yard);
        if(mode==4)for(int y=0;y<w.map.h;++y)for(int x=0;x<w.map.w;++x)
            if(x<20 || x>=24 || y<20 || y>=24)w.map.at(x,y).blocked=true;
        survivorSell(w,0,yard);w.update();EID interior=INVALID_EID;
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && !w.ents[id].isBuilding) {
            const auto& e=w.ents[id];const int x=(int)e.x,y=(int)e.y;
            if(w.bldAt(x-1,y)==yard && w.bldAt(x+1,y)==yard && w.bldAt(x,y-1)==yard && w.bldAt(x,y+1)==yard)interior=id;
        }
        bool ok=w.valid(interior);if(!ok){check(false,"DEPARTURE_CONTROL","interior-setup");continue;}
        const auto crew=w.entityRef(interior);const float x=w.ents[interior].x,y=w.ents[interior].y;
        const bool planned=!w.ents[interior].path.empty();
        if(mode==0 || mode==1) {
            World::Cmd c{};c.type=mode==0?World::Cmd::Stop:World::Cmd::Move;c.ids={interior};c.x=28.5f;c.y=28.5f;
            w.bindCommand(c);w.applyCmd(0,c);
            ok &= mode==0?w.ents[interior].path.empty():w.ents[interior].state==UState::Moving && w.ents[interior].goalX==28.5f && w.ents[interior].goalY==28.5f;
        }
        if(mode==2)w.kill(yard,false);
        if(mode==3){w.changeActorOwner(yard,1);w.synchronizeBuildingOwners();}
        const auto name="departure-control-"+std::to_string(mode);World copy;bool exact=survivorSave(w,copy,out/(name+".sav"));
        bool waited=true;int moved=-1,retired=w.valid(source)?-1:0;
        for(int at=1;at<=600;++at) {
            w.update();copy.update();exact &= w.checksum()==copy.checksum();
            if(!w.valid(source) && retired<0)retired=at;
            if(!w.valid(crew)){ok=false;break;}
            const auto& foot=w.ents[interior];const bool displaced=foot.x!=x || foot.y!=y;
            if(displaced && moved<0)moved=at;
            if(w.valid(source))waited &= !displaced;
            survivorFeedback(w);survivorFeedback(copy);
        }
        const auto& foot=w.ents[interior];const bool outside=foot.x<20 || foot.x>=24 || foot.y<20 || foot.y>=24;
        ok &= exact && retired>=0 && waited && (mode==4?!planned && moved<0:planned);
        if(mode==0)ok &= moved<0 && foot.path.empty();
        if(mode==1)ok &= foot.x==28.5f && foot.y==28.5f && foot.pathIdx>=(int)foot.path.size();
        if(mode==2 || mode==3)ok &= moved>=retired && outside;
        std::printf("BUILDING_SURVIVORS DEPARTURE mode=%d planned=%d moved=%d retired=%d waited=%d replay=%d x=%.6f y=%.6f\n",mode,planned,moved,retired,waited,exact,foot.x,foot.y);
        check(ok,"DEPARTURE_CONTROL",name.c_str());
    }
    for(int mode=0;mode<6;++mode) {
        World w=survivorFixture();w.map.bldOccRef=&w.bldOcc;
        const int type=survivorType(w,"GACNST");const auto def=w.nativeTypes[type];
        const EID id=w.spawnBuilding(0,def->building.type,20,20,true,type);const auto ref=w.entityRef(id);
        if(mode==0)for(int y=20;y<20+def->building.h;++y)for(int x=20;x<20+def->building.w;++x)w.map.at(x,y).blocked=true;
        if(mode==1)for(int y=20;y<20+def->building.h;++y)for(int x=20;x<20+def->building.w;++x)
            for(int n=0;n<3;++n)w.spawnUnit(0,UnitType::GI,x+.5f,y+.5f);
        if(mode==2)w.nextCombatActor=(uint64_t)INT64_MAX;
        if(mode==5){w.players[0].faction=Faction::Soviet;w.players[0].country=Country::Russia;}
        const int before=survivors(w);survivorSell(w,0,id);
        if(mode==3)w.kill(id,false);
        if(mode==4){w.changeActorOwner(id,1);w.synchronizeBuildingOwners();}
        const int added=mode==4?1:mode==5?3:0;
        w.update();bool ok=survivors(w)==before+added;
        if(mode==4)for(const auto& e:w.ents)if(e.alive && !e.isBuilding)ok &= e.player==1 && e.nativeIdentity.definition->id!="ENGINEER";
        if(mode==5)for(const auto& e:w.ents)if(e.alive && !e.isBuilding)
            ok &= e.player==0 && (e.nativeIdentity.definition->id=="E2" || e.nativeIdentity.definition->id=="ENGINEER");
        if(mode==0)for(int y=20;y<20+def->building.h;++y)for(int x=20;x<20+def->building.w;++x)w.map.at(x,y).blocked=false;
        if(mode==2)w.nextCombatActor=100000;
        const auto name="control-"+std::to_string(mode);World copy;bool exact=survivorSave(w,copy,out/(name+".sav"));
        for(int at=0;at<600;++at){w.update();copy.update();exact &= w.checksum()==copy.checksum();survivorFeedback(w);survivorFeedback(copy);}
        ok &= survivors(w)==before+added && !w.valid(ref);
        check(ok && exact,"CONTROL",name.c_str());
    }
    {
        const World base=survivorFixture();
        for(const char* field:{"AlliedCrew = E1","SovietCrew = E2","Technician = CTECH"}) {
            World::RulesSnapshot invalid{base.nativeScenarioSource};const auto at=invalid.source.find(field);
            bool rejected=at!=std::string::npos;
            if(rejected)invalid.source.replace(at,std::strlen(field),std::string(field).substr(0,std::string(field).find(" = "))+" = ENGINEER");
            World staged;rejected &= !staged.init(64,64,20261124,2,0,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,
                nullptr,{Country::America,Country::Russia},&invalid);
            check(rejected,"RULE_REJECT",field);
        }
    }
    {
        World base=survivorFixture();World::RulesSnapshot custom{base.nativeScenarioSource};
        const std::string old="AlliedCrew = E1";const auto at=custom.source.find(old);
        bool ok=at!=std::string::npos;if(ok)custom.source.replace(at,old.size(),"AlliedCrew = CTECH");
        World w=survivorFixture(&custom);w.map.bldOccRef=&w.bldOcc;
        const EID id=w.spawnBuilding(0,BldType::PowerPlant,20,20,true,survivorType(w,"GAPOWR"));
        survivorSell(w,0,id);w.update();EID technician=INVALID_EID;
        for(EID crew=0;crew<(EID)w.ents.size();++crew)if(w.valid(crew) && !w.ents[crew].isBuilding)technician=crew;
        ok &= w.valid(technician) && w.ents[technician].nativeIdentity.definition->id=="CTECH" && w.ents[technician].hp==50
            && unitDef(w.ents[technician]).weapon.damage>0 && !w.ents[technician].nativeIdentity.definition->trainable;
        World copy;ok &= survivorSave(w,copy,out/"technician-controlled-rule.sav");
        for(int tick=0;tick<600 && ok;++tick){w.update();copy.update();ok=w.checksum()==copy.checksum();survivorFeedback(w);survivorFeedback(copy);}
        check(ok,"CONTROL","source-technician-identity-art-weapon-and-save");
        world=w;world.map.bldOccRef=&world.bldOcc;phase=Phase::InGame;localPlayer=0;netGame=true;ais.clear();
        paused=showMenu=gameOver=false;resetMatchInteraction();world.map.reveal(0,20,20,40);bakeTerrain();
        if(world.valid(technician))sel={technician};
        int sx=0,sy=0;tileToScreen(21,21,sx,sy);camX=sx-(SCREEN_W-sidebarW)*.5f;camY=sy-SCREEN_H*.45f;
        const auto hash=world.checksum();render();Image screen=LoadImageFromTexture(canvas.texture);ImageFlipVertical(&screen);
        const auto path=out/"technician-controlled-rule.png";ok &= screen.data && ExportImage(screen,path.string().c_str());UnloadImage(screen);
        check(ok && world.checksum()==hash,"RENDER","technician-controlled-rule");
    }
    {
        World w=survivorFixture();w.map.bldOccRef=&w.bldOcc;
        const EID id=w.spawnBuilding(0,BldType::ConYard,20,20,true,survivorType(w,"GACNST"));survivorSell(w,0,id);
        world=w;world.map.bldOccRef=&world.bldOcc;g_script.shutdown();ais.clear();netGame=false;campaignMission=-1;matchSetup.reset();phase=Phase::InGame;
        const auto file=out/"survivors-app11.sav";Game restored;
        bool ok=saveGameFile(file.string().c_str()) && restored.loadGameFile(file.string().c_str()) && restored.world.checksum()==world.checksum();
        std::ifstream input(file,std::ios::binary);const std::vector<uint8_t> original{std::istreambuf_iterator<char>(input),{}};
        const auto header=embeddedWorldSaveOffset(original);const auto block=header?locateWorldSaveSection(std::span(original).subspan(*header),WorldSaveSection::BuildingSale):std::nullopt;
        int rejected=0;
        if(header && block && block->length>=22) {
            const size_t record=*header+block->offset+8;
            const auto before=restored.world.checksum();
            const auto reject=[&](const char* name,const std::vector<uint8_t>& bytes) {
                const auto bad=out/(std::string("app-bad-")+name+".sav");std::ofstream f(bad,std::ios::binary);f.write((const char*)bytes.data(),bytes.size());f.close();
                const bool atomic=f && !restored.loadGameFile(bad.string().c_str()) && restored.world.checksum()==before;
                ++rejected;check(atomic,"APP_REJECT",name);return atomic;
            };
            auto bytes=original;bytes[*header+block->offset]='X';ok &= reject("tag",bytes);
            bytes=original;bytes[*header+block->footer]^=1;ok &= reject("length",bytes);
            bytes=original;int value=65537;std::memcpy(bytes.data()+*header+block->offset+4,&value,4);ok &= reject("count",bytes);
            bytes=original;value=-1;std::memcpy(bytes.data()+record,&value,4);ok &= reject("entity",bytes);
            bytes=original;bytes[record+4]^=1;ok &= reject("generation",bytes);
            bytes=original;bytes[record+12]=3;ok &= reject("side",bytes);
            bytes=original;bytes[record+13]=4;ok &= reject("flags",bytes);
            size_t idle=record;
            for(size_t at=record;at+14<=*header+block->footer;at+=14) {
                EID actor=INVALID_EID;std::memcpy(&actor,original.data()+at,4);
                if(world.valid(actor) && !world.ents[actor].selling){idle=at;break;}
            }
            bytes=original;bytes[idle+13]=2;ok &= reject("pending-without-sale",bytes);
            bytes=original;bytes[*header+12]=4;ok &= reject("wrapper-flags",bytes);
            bytes=original;value=75;std::memcpy(bytes.data()+*header+8,&value,4);ok &= reject("base",bytes);
            bytes=original;std::memcpy(bytes.data()+*header,"RA2WLD76",8);ok &= reject("old-world",bytes);
            bytes=original;bytes.pop_back();ok &= reject("truncated",bytes);
            bytes=original;bytes.push_back(0);ok &= reject("trailing",bytes);
        } else ok=false;
        for(int at=0;at<600 && ok;++at){world.update();restored.world.update();ok=world.checksum()==restored.world.checksum();survivorFeedback(world);survivorFeedback(restored.world);}
        check(ok && rejected==13,"APP","pending-sale-and-atomic-validation");
    }
    return failures;
}
