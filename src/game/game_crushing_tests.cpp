#include "game_regression.h"
#include "game/native_scene_contract.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <tuple>

int GameRegression::crushingRegression(const char* outputDir) {
    g_script.shutdown();ais.clear();netGame=false;phase=Phase::InGame;localPlayer=0;
    campaignMission=-1;matchSetup.reset();selectRulesChoice(RulesChoice::RA2);
    const std::filesystem::path output(outputDir);
    std::filesystem::create_directories(output);
    int failures=0;
    const auto check=[&](bool ok,const char* text) {
        std::printf("CRUSHING %s %s\n",ok?"PASS":"FAIL",text);failures+=!ok;
    };
    const auto fallingAt=[](const World& state,float x,float y) {
        return std::count_if(state.effects.begin(),state.effects.end(),[&](const Effect& effect) {
            return effect.alive && effect.kind==10 && effect.x==x && effect.y==y;
        });
    };
    const auto sameEffects=[](const World& a,const World& b) {
        const auto fields=[](const Effect& effect) {
            return std::tie(effect.alive,effect.kind,effect.x,effect.y,effect.x2,effect.y2,
                effect.age,effect.maxAge,effect.aux,effect.aux2,effect.aux3);
        };
        return a.effects.size()==b.effects.size() && std::equal(a.effects.begin(),a.effects.end(),b.effects.begin(),
            [&](const Effect& left,const Effect& right){return fields(left)==fields(right);});
    };
    const auto type=[&](const char* name) {
        for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==name)return i;
        return -1;
    };
    const auto fresh=[&](bool opposingAI=false,bool adjacentOnly=false) {
        World::RulesSnapshot prior;
        if(adjacentOnly) {
            std::ifstream file("assets/rules/profiles/ra2.ini",std::ios::binary);
            prior.source.assign(std::istreambuf_iterator<char>(file),{});
            const std::string current="[Scene]\nVersion = "+std::to_string(nativeCurrentRulesSceneVersion)+"\n";const auto at=prior.source.find(current);
            if(at==std::string::npos)return false;prior.source.replace(at,current.size(),"[Scene]\nVersion = 64\n");
        }
        if(!world.init(64,64,723,opposingAI?1:2,opposingAI?1:0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,
            adjacentOnly?nullptr:"assets/rules/profiles/ra2.ini",{Country::America,Country::Russia},adjacentOnly?&prior:nullptr))return false;
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& cell:world.map.cells){cell=Cell{};cell.terrain=Terrain::Water;}
        for(int x=8;x<=20;++x)world.map.at(x,16).terrain=Terrain::Clear;
        world.cratesEnabled=false;world.crates.clear();
        return true;
    };
    struct Case {
        const char *name,*vehicle,*victim;
        UnitType vehicleBase,victimBase;
        bool crushed;
        bool deployed=false,allied=false,invulnerable=false;
    };
    const std::array cases{
        Case{"grizzly-gi","MTNK","E1",UnitType::Grizzly,UnitType::GI,true},
        Case{"miner-gi","CMIN","E1",UnitType::ChronoMiner,UnitType::GI,true},
        Case{"war-miner-conscript","HARV","E2",UnitType::WarMiner,UnitType::Conscript,true},
        Case{"prism-gi","SREF","E1",UnitType::PrismTank,UnitType::GI,true},
        Case{"mirage-conscript","MGTK","E2",UnitType::MirageTank,UnitType::Conscript,true},
        Case{"grizzly-conscript","MTNK","E2",UnitType::Grizzly,UnitType::Conscript,true},
        Case{"grizzly-tanya","MTNK","TANY",UnitType::Grizzly,UnitType::Tanya,true},
        Case{"grizzly-legionnaire","MTNK","CLEG",UnitType::Grizzly,UnitType::Chrono,false},
        Case{"grizzly-tesla","MTNK","SHK",UnitType::Grizzly,UnitType::TeslaTrooper,false},
        Case{"grizzly-desolator","MTNK","DESO",UnitType::Grizzly,UnitType::Desolator,false},
        Case{"ifv-gi","FV","E1",UnitType::IFV,UnitType::GI,false},
        Case{"deployed-gi","MTNK","E1",UnitType::Grizzly,UnitType::GI,true,true},
        Case{"allied-gi","MTNK","E1",UnitType::Grizzly,UnitType::GI,false,false,true},
        Case{"invulnerable-gi","MTNK","E1",UnitType::Grizzly,UnitType::GI,false,false,false,true}
    };
    constexpr int window=600;
    for(const auto& sample:cases) {
        if(!fresh())return failures+1;
        const EID vehicle=world.spawnUnit(0,sample.vehicleBase,10.5f,16.5f,type(sample.vehicle));
        const EID victim=world.spawnUnit(sample.allied?0:1,sample.victimBase,11.5f,16.5f,type(sample.victim));
        if(!world.valid(vehicle) || !world.valid(victim))return failures+1;
        check(world.nativeSceneVersion==nativeCurrentRulesSceneVersion
            && world.ents[vehicle].nativeIdentity.definition && world.ents[victim].nativeIdentity.definition
            && world.ents[vehicle].nativeIdentity.definition->sourceCrushing
            && world.ents[victim].nativeIdentity.definition->sourceCrushing,
            "current RA2 actors retain their source crushing contract");
        world.ents[vehicle].atkCd=1000000;
        world.ents[victim].atkCd=1000000;world.ents[victim].guard=true;
        if(sample.invulnerable)world.ents[victim].invuln=window+60;
        if(sample.deployed) {
            World::Cmd deploy{};deploy.type=World::Cmd::Deploy;deploy.ids={victim};
            world.bindCommand(deploy);world.applyCmd(sample.allied?0:1,deploy);
        }
        const int initialHealth=world.ents[victim].hp;
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={vehicle};move.x=13.5f;move.y=16.5f;
        world.bindCommand(move);world.applyCmd(0,move);
        const bool admitted=world.ents[vehicle].state==UState::Moving && !world.ents[vehicle].path.empty()
            && world.ents[vehicle].path.back()==Vec2i{13,16} && (!sample.deployed || world.ents[victim].deployed);
        const auto path=output/(std::string(sample.name)+".sav");
        FILE* file=std::fopen(path.string().c_str(),"wb");bool exact=file && world.saveGame(file);if(file)std::fclose(file);
        World resumed;file=std::fopen(path.string().c_str(),"rb");exact=exact && file && resumed.loadGame(file);if(file)std::fclose(file);
        exact=exact && world.checksum()==resumed.checksum();
        int crushedAt=0;bool overlap=false;
        for(int elapsed=1;elapsed<=window;++elapsed) {
            world.update();
            if(exact){resumed.update();exact=world.checksum()==resumed.checksum();}
            if(!crushedAt && !world.valid(victim))crushedAt=elapsed;
            if(world.valid(victim) && world.valid(vehicle))overlap|=(int)world.ents[vehicle].x==(int)world.ents[victim].x
                && (int)world.ents[vehicle].y==(int)world.ents[victim].y;
        }
        const auto& mover=world.ents[vehicle];
        const bool arrived=world.valid(vehicle) && mover.state==UState::Idle
            && mover.x==13.5f && mover.y==16.5f;
        const bool protectedVictim=world.valid(victim) && world.ents[victim].hp==initialHealth;
        const bool passed=admitted && exact && !overlap
            && (sample.crushed?crushedAt>0 && arrived:!crushedAt && protectedVictim);
        std::printf("CRUSHING_CASE %s scene=%d admitted=%d expected=%d killed=%d arrived=%d overlap=%d replay=%d checksum=%u\n",
            sample.name,world.nativeSceneVersion,admitted,sample.crushed,crushedAt,arrived,overlap,exact,world.checksum());
        check(passed,sample.name);
        std::ofstream final(output/(std::string(sample.name)+".final"));final<<world.checksum()<<'\n';
        check(bool(final),"the actual outcome checksum is written for compatibility comparisons");
    }
    // Real commands expose premature kills during hull alignment and travel.
    for(int scenario=0;scenario<8;++scenario) {
        if(!fresh())return failures+1;
        const bool diagonal=scenario==1,stacked=scenario==2,stopShort=scenario==3;
        const bool mixed=scenario==4,boundary=scenario==5;
        const bool curve=scenario>=6;
        if(diagonal)for(int y=16;y<=20;++y)for(int x=9;x<=15;++x)world.map.at(x,y).terrain=Terrain::Clear;
        if(curve)for(int y=13;y<=19;++y)world.map.at(11,y).terrain=Terrain::Clear;
        const EID vehicle=world.spawnUnit(0,UnitType::Grizzly,boundary?11.f:10.5f,16.5f,type("MTNK"));
        const float victimY=diagonal?17.5f:16.5f;
        const EID victim=world.spawnUnit(1,UnitType::GI,11.5f,victimY,type("E1"));
        std::vector<EID> victims{victim};
        if(stacked)for(int extra=0;extra<2;++extra)victims.push_back(world.spawnUnit(1,UnitType::GI,11.5f,victimY,type("E1")));
        if(!world.valid(vehicle) || !world.valid(victim))return failures+1;
        auto& mover=world.ents[vehicle];
        mover.nativeGround.hull={0xc000,0xc000,world.tick/2,0};
        mover.nativeGround.turret=mover.nativeGround.hull;
        mover.atkCd=1000000;
        for(const auto id:victims){world.ents[id].atkCd=1000000;world.ents[id].guard=true;}
        check(world.cellHardBlockedForMove(11,(int)victimY,vehicle),
            "teleport, landing and pullout queries still require an unoccupied cell");
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={vehicle};move.x=curve?11.5f:13.5f;
        move.y=diagonal || scenario==6?19.5f:scenario==7?13.5f:16.5f;
        world.bindCommand(move);world.applyCmd(0,move);
        const bool admitted=mover.state==UState::Moving && !mover.path.empty();
        if(mixed) {
            const auto ally=world.spawnUnit(0,UnitType::GI,11.5f,victimY,type("E1"));
            world.ents[ally].atkCd=1000000;world.ents[ally].guard=true;
        }
        bool timing=true,stationary=false,shared=false,stopped=false,exact=true,curvedAtDeath=false;
        bool presentation=true;int fallsAtDeath=-1,crushSounds=0,losses=0;
        int killedAt=0;float lossX=0,lossY=0;std::array<bool,4> captured{};
        std::ofstream trace(output/("contact-"+std::to_string(scenario)+".trace"));
        trace<<world.checksum()<<'\n';
        for(int elapsed=1;elapsed<=window;++elapsed) {
            const float oldX=mover.x,oldY=mover.y;
            const bool alive=world.valid(victim);
            world.update();
            trace<<world.checksum()<<'\n';
            const bool moved=mover.x!=oldX || mover.y!=oldY;
            stationary |= !moved && world.valid(victim);
            if(alive && !world.valid(victim)) {
                killedAt=elapsed;lossX=mover.x;lossY=mover.y;
                curvedAtDeath=mover.driveArc.active();
                const float dx=mover.x-11.5f,dy=mover.y-victimY;
                timing &= moved && dx*dx+dy*dy<.25f;
                fallsAtDeath=(int)fallingAt(world,11.5f,victimY);
                for(const auto& event:world.presentationEvents)if(event.tick==world.tick
                    && event.kind==PresentationKind::Sound && event.sound==Sfx::Crush
                    && event.positional && event.x==11.5f && event.y==victimY)++crushSounds;
                for(const auto& event:world.evaQueue)if(event.player==1 && event.cue==EvaCue::UnitLost)++losses;
                presentation &= fallsAtDeath==0 && crushSounds==(int)victims.size() && losses==1;
            }
            if(world.valid(victim) && (int)mover.x==11 && (int)mover.y==(int)victimY)shared=true;
            if(stopShort && !stopped && moved && mover.x<11) {
                World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={vehicle};world.bindCommand(stop);world.applyCmd(0,stop);
                stopped=true;
            }
            const int stage=!moved?0:world.valid(victim)?(shared?2:1):3;
            if(!captured[stage]) {
                captured[stage]=true;
                const auto path=output/("contact-"+std::to_string(scenario)+"-"+std::to_string(stage)+".sav");
                FILE* file=std::fopen(path.string().c_str(),"wb");bool same=file && world.saveGame(file);if(file)std::fclose(file);
                World a=world,b;a.map.bldOccRef=&a.bldOcc;
                file=std::fopen(path.string().c_str(),"rb");same &= file && b.loadGame(file);if(file)std::fclose(file);
                same &= a.checksum()==world.checksum() && b.checksum()==world.checksum() && sameEffects(a,b);
                for(int tick=0;tick<120 && same;++tick){a.update();b.update();same=a.checksum()==b.checksum() && sameEffects(a,b);}
                exact &= same;
            }
        }
        bool allGone=true;for(const auto id:victims)allGone &= !world.valid(id);
        const bool expected=stopShort || mixed?world.valid(victim) && !killedAt && (!stopShort || stopped)
            :killedAt>0 && timing && allGone && mover.x==move.x && mover.y==move.y && (!curve || curvedAtDeath);
        std::printf("CRUSHING_CONTACT case=%d admitted=%d stationary=%d shared=%d stopped=%d killed=%d at=%.6f,%.6f timing=%d all=%d curve=%d replay=%d\n",
            scenario,admitted,stationary,shared,stopped,killedAt,lossX,lossY,timing,allGone,curvedAtDeath,exact);
        check(admitted && stationary && expected && exact,"normal Move resolves crushing only at physical contact and preserves stops, blockers and continuation");
        std::printf("CRUSHING_DEATH case=%d killed=%d falls=%d sounds=%d losses=%d effects_replay=%d simulation=%u\n",
            scenario,killedAt,fallsAtDeath,crushSounds,losses,exact,world.checksum());
        check(presentation && bool(trace),"physical crushing removes infantry without a falling sequence and retains crush audio and unit-lost feedback");
    }
    if(!fresh())return failures+1;
    {
        const EID victim=world.spawnUnit(1,UnitType::GI,11.5f,16.5f,type("E1"));
        world.effects.clear();
        const auto initial=world.entityRef(victim);
        world.damage(victim,world.ents[victim].hp,0);
        const auto path=output/"ordinary-infantry-death.sav";
        FILE* file=std::fopen(path.string().c_str(),"wb");bool exact=file && world.saveGame(file);if(file)std::fclose(file);
        World resumed;file=std::fopen(path.string().c_str(),"rb");exact &= file && resumed.loadGame(file);if(file)std::fclose(file);
        bool falls=!world.valid(initial) && fallingAt(world,11.5f,16.5f)==1;
        for(int elapsed=0;elapsed<=g_gameRules.deathAnimationTicks;++elapsed) {
            falls &= fallingAt(world,11.5f,16.5f)==(elapsed<g_gameRules.deathAnimationTicks?1:0);
            exact &= world.checksum()==resumed.checksum() && sameEffects(world,resumed);
            world.update();resumed.update();
        }
        std::printf("CRUSHING_ORDINARY_DEATH falls=%d effects_replay=%d\n",falls,exact);
        check(falls && exact,"ordinary lethal damage retains the complete falling animation and its saved lifetime");
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_CRUSH_DEATH_DIR")) {
        const auto legacy=std::filesystem::path(directory);
        World prior;FILE* file=std::fopen((legacy/"contact-0-3.sav").string().c_str(),"rb");
        bool same=file && prior.loadGame(file);if(file)std::fclose(file);
        const int initialFalls=(int)fallingAt(prior,11.5f,16.5f);
        int remaining=0;
        for(const auto& effect:prior.effects)if(effect.kind==10 && effect.x==11.5f && effect.y==16.5f)
            remaining=effect.maxAge-effect.age;
        bool lifetime=same && initialFalls==1 && remaining>0;
        for(int elapsed=0;elapsed<=remaining && same;++elapsed) {
            lifetime &= fallingAt(prior,11.5f,16.5f)==(elapsed<remaining?1:0);
            prior.update();
        }
        std::printf("CRUSHING_LEGACY_DEATH scene=%d initial_falls=%d remaining=%d lifetime=%d\n",prior.nativeSceneVersion,initialFalls,remaining,lifetime);
        check(lifetime,"an actual prechange save retains its already encoded falling effect until normal expiry");
    }
    // The original pre-entry warning scatters stationary AI infantry, while
    // default human control, existing movement and blocked exits remain distinct.
    for(int scenario=0;scenario<16;++scenario) {
        const bool human=scenario==1 || scenario==8 || scenario==9 || scenario==11 || scenario==15;
        const bool moving=scenario==2 || scenario==13,frozen=scenario==3;
        const bool nonCrusher=scenario==4,blocked=scenario==5,deployed=scenario==6 || scenario==11,firing=scenario==7;
        const bool elite=scenario==8 || scenario==11 || scenario==12;
        if(!fresh(!human))return failures+1;
        if(scenario==9 || scenario==10 || scenario==12 || scenario==13) {
            std::ifstream input("assets/rules/profiles/ra2.ini",std::ios::binary);
            std::string source{std::istreambuf_iterator<char>(input),{}};
            const auto replace=[&](const char* from,const char* to,size_t start=0) {
                const auto at=source.find(from,start);if(at==std::string::npos)return false;
                source.replace(at,std::char_traits<char>::length(from),to);return true;
            };
            bool changed=scenario==9?replace("PlayerScatter = 0","PlayerScatter = 1"):
                scenario==13?replace("Fraidycat = 0","Fraidycat = 1",source.find("[Type."+std::to_string(type("E1"))+"]")):
                replace("ScatterIQ = 2","ScatterIQ = 6");
            const auto path=output/("warning-override-"+std::to_string(scenario)+".ini");
            {std::ofstream profile(path,std::ios::binary);profile<<source;changed &= bool(profile);}
            check(changed && world.loadRulesProfile(path.string().c_str()),"controlled warning policy and Fraidycat profile overrides load through normal rules admission");
        }
        if(!blocked)for(int y=14;y<=20;++y)for(int x=9;x<=15;++x)world.map.at(x,y).terrain=Terrain::Clear;
        else {
            for(auto& cell:world.map.cells)cell.terrain=Terrain::Water;
            for(int x=10;x<=13;++x)world.map.at(x,16).terrain=Terrain::Clear;
        }
        world.map.at(10,16).terrain=world.map.at(11,16).terrain=Terrain::Clear;
        const EID vehicle=world.spawnUnit(0,nonCrusher?UnitType::IFV:UnitType::Grizzly,10.5f,16.5f,type(nonCrusher?"FV":"MTNK"));
        const float initialX=scenario==14?11.75f:11.5f,initialY=scenario==14?16.25f:16.5f;
        const EID victim=world.spawnUnit(1,UnitType::GI,initialX,initialY,type("E1"));
        if(!world.valid(vehicle) || !world.valid(victim))return failures+1;
        for(auto& player:world.players){player.active=true;player.defeated=false;}
        check(world.players[1].isAI!=human,"warning fixtures retain the AI/human controller selected during world initialization");
        world.ents[vehicle].atkCd=world.ents[victim].atkCd=1000000;
        world.ents[vehicle].dir=world.ents[vehicle].turretDir=5;
        const auto issue=[&](int player,World::Cmd::Type kind,EID id,float x=0,float y=0) {
            World::Cmd command{};command.type=kind;command.ids={id};command.x=x;command.y=y;
            world.bindCommand(command);world.applyCmd(player,command);
        };
        issue(1,World::Cmd::Guard,victim);
        if(moving)issue(1,World::Cmd::Move,victim,14.5f,18.5f);
        if(deployed)issue(1,World::Cmd::Deploy,victim);
        if(elite)world.ents[victim].vetRank=2;
        if(frozen)world.ents[victim].tpSick=window+60;
        if(firing || scenario==13) {
            if(firing)world.ents[victim].fireAnim=20;
            world.ents[vehicle].dir=world.ents[vehicle].turretDir=1;
        }
        if(blocked)for(int extra=0;extra<3;++extra) {
            const auto id=world.spawnUnit(1,UnitType::GI,12.5f,16.5f,type("E1"));
            world.ents[id].atkCd=1000000;issue(1,World::Cmd::Guard,id);
        }
        if(scenario==15)for(int extra=0;extra<2;++extra) {
            const auto id=world.spawnUnit(1,UnitType::GI,11.5f,16.5f,type("E1"));
            world.ents[id].atkCd=1000000;world.ents[id].vetRank=extra==1?2:0;issue(1,World::Cmd::Guard,id);
        }
        if(scenario==14) {
            uint64_t seed=1;for(;;++seed){Rng probe(seed);if(probe.range(0,4)==2)break;}
            world.rng=Rng(seed);
        }
        issue(0,World::Cmd::Move,vehicle,13.5f,16.5f);
        const bool admitted=world.ents[vehicle].state==UState::Moving && !world.ents[vehicle].path.empty();
        const auto ordered=output/("warning-"+std::to_string(scenario)+"-ordered.sav");
        FILE* file=std::fopen(ordered.string().c_str(),"wb");bool exact=file && world.saveGame(file);if(file)std::fclose(file);
        World resumed;file=std::fopen(ordered.string().c_str(),"rb");exact &= file && resumed.loadGame(file);if(file)std::fclose(file);
        std::printf("CRUSHING_WARNING_INITIAL case=%d saved_loaded=%d same=%d live=%u loaded=%u ai=%d\n",
            scenario,exact,world.checksum()==resumed.checksum(),world.checksum(),resumed.checksum(),world.players[1].isAI);
        exact &= world.checksum()==resumed.checksum() && sameEffects(world,resumed);
        if(exact) {
            const auto path=output/("warning-"+std::to_string(scenario)+"-restored.sav");
            file=std::fopen(path.string().c_str(),"wb");if(file){resumed.saveGame(file);std::fclose(file);}
        }
        int responseAt=0,killedAt=0;float responseX=0,responseY=0;bool displaced=false,adjacent=true,midway=false,quiet=true;
        std::ofstream trace(output/("warning-"+std::to_string(scenario)+".trace"));trace<<world.checksum()<<'\n';
        for(int elapsed=1;elapsed<=window;++elapsed) {
            world.update();if(exact){resumed.update();exact=world.checksum()==resumed.checksum() && sameEffects(world,resumed);}
            trace<<world.checksum()<<'\n';
            if(!world.valid(victim)) {
                if(!killedAt) {
                    killedAt=elapsed;
                    if(blocked) {
                        issue(0,World::Cmd::Stop,vehicle);
                        if(exact){World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={vehicle};resumed.bindCommand(stop);resumed.applyCmd(0,stop);}
                    }
                }
                continue;
            }
            const auto& foot=world.ents[victim];displaced |= foot.x!=initialX || foot.y!=initialY;
            if((!moving || (scenario==13 && (foot.goalX!=14.5f || foot.goalY!=18.5f)))
                && !responseAt && foot.pathIdx<(int)foot.path.size()) {
                responseAt=elapsed;responseX=foot.goalX;responseY=foot.goalY;
                for(const auto& event:world.presentationEvents)
                    if(event.tick==world.tick && event.kind==PresentationKind::MoveOrder && event.player==1)quiet=false;
                adjacent=std::abs(responseX-11.5f)<=1 && std::abs(responseY-16.5f)<=1
                    && (responseX!=11.5f || responseY!=16.5f);
                const auto path=output/("warning-"+std::to_string(scenario)+"-midway.sav");
                file=std::fopen(path.string().c_str(),"wb");midway=file && world.saveGame(file);if(file)std::fclose(file);
                World a=world,b;a.map.bldOccRef=&a.bldOcc;
                file=std::fopen(path.string().c_str(),"rb");midway &= file && b.loadGame(file);if(file)std::fclose(file);
                for(int step=0;step<120 && midway;++step){a.update();b.update();midway=a.checksum()==b.checksum() && sameEffects(a,b);}
            }
        }
        const bool shouldRespond=scenario==0 || scenario==6 || scenario==7 || scenario==8 || scenario==9 || scenario==12 || scenario==13 || scenario==14 || scenario==15;
        const bool expected=blocked?responseAt>0 && responseX==13.5f && responseY==16.5f && !displaced && killedAt>responseAt && midway
            :shouldRespond?responseAt>0 && (!killedAt || responseAt<killedAt) && adjacent && displaced && midway
            :!responseAt && (moving?displaced:frozen || nonCrusher?world.valid(victim):killedAt>0);
        std::printf("CRUSHING_WARNING case=%d ai=%d admitted=%d responded=%d goal=%.1f,%.1f moved=%d killed=%d adjacent=%d midway=%d replay=%d checksum=%u\n",
            scenario,!human,admitted,responseAt,responseX,responseY,displaced,killedAt,adjacent,midway,exact,world.checksum());
        check(admitted && expected && exact && quiet && bool(trace),"source pre-entry warnings use ordinary move admission quietly and preserve human, moving, frozen and blocked controls");
        if(scenario==14)check(world.valid(victim) && responseX==12.5f && responseY==15.5f,
            "a real offset infantry escape reaches the original-rule adjacent destination and survives the approaching crusher");
        std::ofstream final(output/("warning-"+std::to_string(scenario)+".final"));final<<world.checksum()<<'\n';
    }
    {
        std::ifstream reference("tests/fixtures/ra2-crush-warning-destinations.txt");
        std::string line;std::getline(reference,line);int samples=0,matched=0;
        while(std::getline(reference,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            int heading=0,roll=0,x=0,y=0,gx=0,gy=0;if(!(row>>heading>>roll>>x>>y>>gx>>gy))break;
            if(!fresh(true,true))return failures+1;
            for(int cy=14;cy<=18;++cy)for(int cx=9;cx<=14;++cx)world.map.at(cx,cy).terrain=Terrain::Clear;
            const auto vehicle=world.spawnUnit(0,UnitType::Grizzly,10.5f,16.5f,type("MTNK"));
            const auto victim=world.spawnUnit(1,UnitType::GI,11+x/256.f,16+y/256.f,type("E1"));
            world.ents[vehicle].dir=world.ents[vehicle].turretDir=1;
            world.ents[victim].dir=world.ents[victim].turretDir=NativeGroundMath::dir((uint16_t)heading);
            world.ents[vehicle].atkCd=world.ents[victim].atkCd=1000000;world.ents[victim].guard=true;
            uint64_t seed=1;for(;;++seed){Rng probe(seed);if(probe.range(0,4)==roll)break;}
            world.rng=Rng(seed);Rng expected=world.rng;expected.range(0,4);
            World::Cmd move{};move.type=World::Cmd::Move;move.ids={vehicle};move.x=13.5f;move.y=16.5f;
            world.bindCommand(move);world.applyCmd(0,move);world.update();
            const auto& foot=world.ents[victim];
            const bool same=world.valid(victim) && foot.goalX==gx+.5f && foot.goalY==gy+.5f && world.rng.s==expected.s;
            matched+=same;++samples;
            std::printf("CRUSHING_WARNING_REFERENCE sample=%d heading=%d roll=%d pose=%d,%d expected=%d,%d actual=%.1f,%.1f exact=%d\n",
                samples,heading,roll,x,y,gx,gy,foot.goalX,foot.goalY,same);
        }
        check(samples==80 && matched==samples,"Scene64 retains all eighty accepted adjacent-only warning samples with one random draw");
    }
    {
        std::ifstream input("tests/fixtures/ra2-nearby-scatter-cases.csv");std::string line;std::getline(input,line);
        std::ofstream observed(output/"nearby-fallback-matrix.csv");observed<<"heading,roll,poseX,poseY,expectedX,expectedY,actualX,actualY,exact\n";
        int cases=0,matched=0;
        while(std::getline(input,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            int kind,lx,ly,frame,px,py,heading,roll,destination,gx,gy,draws;
            if(!(row>>kind>>lx>>ly>>frame>>px>>py>>heading>>roll>>destination>>gx>>gy>>draws))break;
            if(kind!=3)continue;
            if(!fresh(true))return failures+1;
            for(auto& cell:world.map.cells)cell.terrain=Terrain::Clear;
            const auto vehicle=world.spawnUnit(0,UnitType::Grizzly,21.5f,22.5f,type("MTNK"));
            const auto victim=world.spawnUnit(1,UnitType::GI,22+px/256.f,22+py/256.f,type("E1"));
            world.ents[vehicle].dir=world.ents[vehicle].turretDir=1;
            world.ents[victim].dir=world.ents[victim].turretDir=NativeGroundMath::dir((uint16_t)heading);
            for(int y=21;y<=23;++y)for(int x=21;x<=23;++x)if((x!=21 || y!=22) && (x!=22 || y!=22))
                world.spawnUnit(1,UnitType::GI,x+.5f,y+.5f,type("E1"));
            for(auto& actor:world.ents)if(actor.alive){actor.atkCd=1000000;if(!actor.isBuilding && unitDef(actor).isInfantry())actor.guard=true;}
            World::Cmd move{};move.type=World::Cmd::Move;move.ids={vehicle};move.x=24.5f;move.y=22.5f;
            world.bindCommand(move);world.applyCmd(0,move);
            const bool admitted=world.ents[vehicle].state==UState::Moving;
            // Change the mechanism's exterior after route admission so the
            // warning sees no entirely clear cell within the primary search.
            // The eight shared-infantry fallback cells remain traversable.
            for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x)
                if(x<21 || x>23 || y<21 || y>23)world.map.at(x,y).terrain=Terrain::Water;
            uint64_t seed=1;for(;;++seed){Rng probe(seed);if(probe.range(0,4)==roll)break;}
            world.rng=Rng(seed);Rng expected=world.rng;expected.range(0,4);world.update();
            const auto& foot=world.ents[victim];const bool same=admitted && foot.goalX==gx+.5f && foot.goalY==gy+.5f && world.rng.s==expected.s;
            ++cases;matched+=same;observed<<heading<<','<<roll<<','<<px<<','<<py<<','<<gx<<','<<gy<<','<<foot.goalX<<','<<foot.goalY<<','<<same<<'\n';
        }
        std::printf("CRUSHING_NEARBY_FALLBACK cases=%d matched=%d\n",cases,matched);
        check(cases==80 && matched==cases && bool(observed),"actual Move warnings retain all eighty source fallback destinations when primary clearance fails");
    }
    {
        if(!fresh(true))return failures+1;
        const World base=world;
        std::ifstream input("tests/fixtures/ra2-nearby-height-cases.csv");std::string line;std::getline(input,line);
        std::ofstream observed(output/"height-warning-matrix.csv");observed<<"pattern,base,frame,expectedX,expectedY,actualX,actualY,exact\n";
        int cases=0,matched=0;bool setup=true;
        while(std::getline(input,line)) {
            std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
            int pattern,height,frame,roll,destination,gx,gy,draws;
            if(!(row>>pattern>>height>>frame>>roll>>destination>>gx>>gy>>draws)){setup=false;break;}
            world=base;world.map.bldOccRef=&world.bldOcc;
            for(int y=0;y<world.map.h;++y)for(int x=0;x<world.map.w;++x) {
                auto& cell=world.map.at(x,y);cell.terrain=Terrain::Clear;int level=height;
                if(pattern==1 && x==23 && y==23)level=height+3;
                if(pattern==2 && x>=23 && x<=25 && y>=23 && y<=25)level=height+4;
                if(pattern==3 && x>=23 && y>=23)level=14;
                if(pattern==4 && x==y && x>22)level=14;
                if(pattern==5 && x>=23 && x<=25 && y>=23 && y<=25)level=height+1;
                if(pattern==6 && x>=23 && x<=25 && y>=23 && y<=25)level=height+2;
                cell.height=(uint8_t)level;
            }
            world.tick=frame*2;
            const auto vehicle=world.spawnUnit(0,UnitType::Grizzly,21.5f,22.5f,type("MTNK"));
            const auto victim=world.spawnUnit(1,UnitType::GI,22.5f,22.5f,type("E1"));
            world.ents[vehicle].dir=world.ents[vehicle].turretDir=1;
            world.ents[vehicle].atkCd=world.ents[victim].atkCd=1000000;world.ents[victim].guard=true;
            uint64_t seed=1;for(;;++seed){Rng probe(seed);if(probe.range(0,4)==roll)break;}
            world.rng=Rng(seed);Rng expected=world.rng;expected.range(0,4);
            World::Cmd move{};move.type=World::Cmd::Move;move.ids={vehicle};move.x=24.5f;move.y=22.5f;
            world.bindCommand(move);world.applyCmd(0,move);
            const bool admitted=world.ents[vehicle].state==UState::Moving;
            const bool continuation=height==0 && pattern>=1 && pattern<=3 && (frame==0 || frame==7);
            const std::string name="height-"+std::to_string(pattern)+"-"+std::to_string(frame);
            World replay;std::ofstream trace;
            if(continuation) {
                const auto path=output/(name+"-ordered.sav");FILE* f=std::fopen(path.string().c_str(),"wb");bool saved=f && world.saveGame(f);if(f)std::fclose(f);
                f=std::fopen(path.string().c_str(),"rb");saved &= f && replay.loadGame(f);if(f)std::fclose(f);setup &= saved;
                trace.open(output/(name+".trace"));trace<<world.checksum()<<'\n';
            }
            world.update();
            const auto& foot=world.ents[victim];
            const bool same=admitted && world.valid(victim) && foot.goalX==gx+.5f && foot.goalY==gy+.5f && world.rng.s==expected.s;
            ++cases;matched+=same;observed<<pattern<<','<<height<<','<<frame<<','<<gx<<','<<gy<<','<<foot.goalX<<','<<foot.goalY<<','<<same<<'\n';
            if(continuation) {
                replay.update();bool exact=world.checksum()==replay.checksum();trace<<world.checksum()<<'\n';
                for(int step=1;step<600;++step) {
                    world.presentationEvents.clear();world.evaQueue.clear();replay.presentationEvents.clear();replay.evaQueue.clear();
                    world.update();replay.update();exact &= world.checksum()==replay.checksum();trace<<world.checksum()<<'\n';
                }
                std::ofstream(output/(name+".final"))<<world.checksum()<<'\n';
                check(exact && bool(trace),"height warning actual Move retains all600 ticks through World persistence");
            }
        }
        std::printf("CRUSHING_HEIGHT_MATRIX cases=%d matched=%d setup=%d\n",cases,matched,setup);
        check(setup && cases==1008 && bool(observed),"all height warning inputs execute through admitted real Move orders");
        check(cases==1008 && matched==cases,"height warnings match original projection tiers and one scatter draw");
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_NEARBY_HEIGHT65_INPUT_DIR")) {
        for(int pattern=1;pattern<=3;++pattern)for(int frame:{0,7}) {
            const auto name="height-"+std::to_string(pattern)+"-"+std::to_string(frame);const std::filesystem::path old(directory);
            World prior;FILE* f=std::fopen((old/(name+"-ordered.sav")).string().c_str(),"rb");bool exact=f && prior.loadGame(f);if(f)std::fclose(f);
            std::ifstream trace(old/(name+".trace")),final(old/(name+".final"));uint64_t expected=0;
            exact &= prior.nativeRulesBaseVersion==65 && bool(trace>>expected) && prior.checksum()==expected;
            for(int at=1;at<=600 && exact;++at) {
                prior.update();exact=bool(trace>>expected) && prior.checksum()==expected;prior.presentationEvents.clear();prior.evaQueue.clear();
            }
            exact &= bool(final>>expected) && prior.checksum()==expected;
            check(exact,"actual Scene65 height-warning input retains all600 original checksums");
        }
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_CRUSH_WARNING_DIR")) {
        const auto legacy=std::filesystem::path(directory);
        for(int scenario=0;scenario<8;++scenario) {
            const auto prefix="warning-"+std::to_string(scenario);World old;
            FILE* file=std::fopen((legacy/(prefix+"-ordered.sav")).string().c_str(),"rb");
            bool same=file && old.loadGame(file);if(file)std::fclose(file);
            std::ifstream final(legacy/(prefix+".final")),trace(legacy/(prefix+".trace")),schedule(legacy/(prefix+".stop"));
            uint32_t expected=0,initial=0;int stop=0;
            same &= bool(final>>expected) && bool(trace>>initial) && bool(schedule>>stop)
                && old.nativeSceneVersion==49 && !old.nativeCrushWarningRules.available && old.checksum()==initial;
            EID vehicle=INVALID_EID;for(EID id=0;id<(EID)old.ents.size();++id)
                if(old.valid(id) && old.ents[id].player==0 && !old.ents[id].isBuilding){vehicle=id;break;}
            for(int elapsed=1;elapsed<=window && same;++elapsed) {
                old.update();uint32_t checksum=0;same=bool(trace>>checksum) && old.checksum()==checksum;
                if(elapsed==stop){World::Cmd command{};command.type=World::Cmd::Stop;command.ids={vehicle};old.bindCommand(command);old.applyCmd(0,command);}
            }
            same &= old.checksum()==expected;
            std::printf("CRUSHING_WARNING_LEGACY case=%d scene=%d stop=%d exact=%d\n",scenario,old.nativeSceneVersion,stop,same);
            check(same,"an actual Scene49 warning order preserves all 601 old checksums and its recorded command schedule");
        }
    }
    for(int departure=0;departure<9;++departure) {
        if(!fresh())return failures+1;
        constexpr std::array directions{Vec2i{1,1},Vec2i{1,0},Vec2i{0,1},Vec2i{-1,1},
            Vec2i{1,-1},Vec2i{-1,0},Vec2i{0,-1},Vec2i{-1,-1}};
        const bool frozen=departure==8;const auto direction=directions[departure%8];
        const float goalX=11.5f+direction.x,goalY=17.5f+direction.y;
        for(int y=16;y<=20;++y)for(int x=9;x<=15;++x)world.map.at(x,y).terrain=Terrain::Clear;
        const EID vehicle=world.spawnUnit(0,UnitType::Grizzly,10.5f,16.5f,type("MTNK"));
        const EID infantry=world.spawnUnit(1,UnitType::GI,11.5f,17.5f,type("E1"));
        if(!world.valid(vehicle) || !world.valid(infantry))return failures+1;
        for(auto& player:world.players){player.active=true;player.defeated=false;}
        world.ents[vehicle].atkCd=world.ents[infantry].atkCd=1000000;world.ents[infantry].guard=true;
        const auto issue=[&](World& target,int player,World::Cmd::Type kind,EID actor,float x=0,float y=0) {
            World::Cmd command{};command.type=kind;command.ids={actor};command.x=x;command.y=y;
            target.bindCommand(command);target.applyCmd(player,command);
        };
        issue(world,0,World::Cmd::Move,vehicle,13.5f,19.5f);
        bool shared=false;
        for(int elapsed=0;elapsed<240 && world.valid(infantry);++elapsed) {
            world.update();const auto& mover=world.ents[vehicle];
            if(world.valid(infantry) && (int)mover.x==11 && (int)mover.y==17) {
                const float dx=mover.x-world.ents[infantry].x,dy=mover.y-world.ents[infantry].y;
                shared=dx*dx+dy*dy>=.25f;break;
            }
        }
        issue(world,0,World::Cmd::Stop,vehicle);
        const float heldX=world.ents[vehicle].x,heldY=world.ents[vehicle].y;
        const bool stopped=shared && world.ents[vehicle].state==UState::Idle;
        const bool strict=world.cellHardBlockedForMove(11,17,infantry,world.ents[infantry].navLayer);
        if(frozen)world.ents[infantry].tpSick=window+60;
        World open=world;open.map.bldOccRef=&open.bldOcc;open.kill(vehicle,false);
        open.ents[infantry].tpSick=0;
        issue(world,1,World::Cmd::Move,infantry,goalX,goalY);
        issue(open,1,World::Cmd::Move,infantry,goalX,goalY);
        const bool admitted=world.ents[infantry].state==UState::Moving && !world.ents[infantry].path.empty();
        const auto label="shared-infantry-"+std::to_string(departure);
        const auto path=output/(label+"-ordered.sav");
        FILE* file=std::fopen(path.string().c_str(),"wb");bool exact=file && world.saveGame(file);if(file)std::fclose(file);
        World resumed;file=std::fopen(path.string().c_str(),"rb");exact &= file && resumed.loadGame(file);if(file)std::fclose(file);
        exact &= resumed.checksum()==world.checksum();
        bool held=true,middleSaved=false;int reachedAt=0;
        for(int elapsed=0;elapsed<window;++elapsed) {
            world.update();open.update();
            if(exact){resumed.update();exact=world.checksum()==resumed.checksum();}
            held &= world.valid(vehicle) && world.ents[vehicle].x==heldX && world.ents[vehicle].y==heldY;
            if(!reachedAt && world.valid(infantry) && world.ents[infantry].x==goalX && world.ents[infantry].y==goalY
                && world.ents[infantry].pathIdx>=(int)world.ents[infantry].path.size())reachedAt=elapsed+1;
            if(!middleSaved && world.valid(infantry) && (int)world.ents[infantry].x==11 && (int)world.ents[infantry].y==17
                && (world.ents[infantry].x!=11.5f || world.ents[infantry].y!=17.5f)) {
                middleSaved=true;const auto midway=output/(label+"-midway.sav");
                file=std::fopen(midway.string().c_str(),"wb");bool same=file && world.saveGame(file);if(file)std::fclose(file);
                World future=world,loaded;future.map.bldOccRef=&future.bldOcc;
                file=std::fopen(midway.string().c_str(),"rb");same &= file && loaded.loadGame(file);if(file)std::fclose(file);
                same &= loaded.checksum()==world.checksum();
                for(int tick=0;tick<120 && same;++tick){future.update();loaded.update();same=future.checksum()==loaded.checksum();}
                exact &= same;
            }
        }
        const auto arrived=[&](const World& target) {
            return target.valid(infantry) && target.ents[infantry].pathIdx>=(int)target.ents[infantry].path.size()
                && target.ents[infantry].x==goalX && target.ents[infantry].y==goalY;
        };
        std::ofstream final(output/(label+".final"));final<<world.checksum()<<'\n';
        const bool protectedFoot=world.valid(infantry) && world.ents[infantry].x==11.5f && world.ents[infantry].y==17.5f
            && world.ents[infantry].immobilized();
        std::printf("CRUSHING_SHARED_EXIT scene=%d case=%d frozen=%d shared=%d stopped=%d admitted=%d held=%d alive=%d open_arrived=%d arrived=%d reached=%d state=%d middle=%d strict=%d replay=%d vehicle=%.6f,%.6f infantry=%.6f,%.6f block=%d\n",
            world.nativeSceneVersion,departure,frozen,shared,stopped,admitted,held,world.valid(infantry),arrived(open),arrived(world),reachedAt,(int)world.ents[infantry].state,middleSaved,strict,exact,
            heldX,heldY,world.ents[infantry].x,world.ents[infantry].y,world.ents[infantry].blockTick);
        check(stopped && held && exact && arrived(open) && bool(final),
            "real diagonal Move and Stop reach a shared cell outside crushing contact, and the open control route is reachable");
        check(stopped && held && strict && exact && (frozen?protectedFoot:admitted && reachedAt>0 && arrived(world) && middleSaved),
            frozen?"an immobilized infantry remains held despite the physical origin-cell exception"
                :"infantry can leave a crusher's already shared cell after the vehicle receives Stop");
        const EID outsider=world.spawnUnit(1,UnitType::GI,12.5f,17.5f,type("E1"));
        if(!world.valid(outsider))return failures+1;
        world.ents[outsider].atkCd=1000000;
        const bool newCellBlocked=world.cellHardBlockedForMove(11,17,outsider,0,true);
        issue(world,1,World::Cmd::Move,outsider,11.5f,17.5f);
        bool stayedOutside=true;
        for(int elapsed=0;elapsed<120;++elapsed) {
            world.update();stayedOutside &= world.valid(outsider)
                && ((int)world.ents[outsider].x!=11 || (int)world.ents[outsider].y!=17);
        }
        check(newCellBlocked && stayedOutside,"a separate infantry Move still cannot enter the vehicle's occupied cell");
    }
    {
        if(!fresh())return failures+1;
        world.map.bridgeCells.assign(world.map.cells.size(),{});world.map.bridgeCells[16*world.map.w+11]={0,4,0};
        const EID infantry=world.spawnUnit(1,UnitType::GI,11.5f,16.5f,type("E1"));
        const EID vehicle=world.spawnUnit(0,UnitType::ChronoMiner,11.5f,16.5f,type("CMIN"));
        if(!world.valid(infantry) || !world.valid(vehicle))return failures+1;
        world.ents[infantry].navLayer=world.ents[infantry].previousNavLayer=world.ents[infantry].goalLayer=0;
        world.ents[vehicle].navLayer=world.ents[vehicle].previousNavLayer=world.ents[vehicle].goalLayer=1;
        world.unitOccSync(infantry);world.unitOccSync(vehicle);
        check(world.cellHardBlockedForMove(11,16,infantry,1,true) && !world.cellHardBlockedForMove(11,16,infantry,0,true),
            "the infantry's own cell on another queried surface retains vehicle blocking");
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_INFANTRY_EXIT_DIR")) {
        const auto root=std::filesystem::path(directory);World old;
        FILE* file=std::fopen((root/"shared-infantry-ordered.sav").string().c_str(),"rb");
        bool same=file && old.loadGame(file);if(file)std::fclose(file);
        uint32_t expected=0;std::ifstream final(root/"shared-infantry-ordered.final");
        same &= old.nativeSceneVersion==48 && old.nativeRulesBaseVersion==48 && bool(final>>expected);
        for(int elapsed=0;elapsed<window && same;++elapsed)old.update();
        same &= old.checksum()==expected;
        std::printf("CRUSHING_SHARED_LEGACY scene=%d exact=%d\n",old.nativeSceneVersion,same);
        check(same,"an actual Scene48 admitted departure preserves its complete original 600-tick result");
    }
    for(bool drive:{false,true})for(bool differentLayer:{false,true}) {
        if(!fresh(drive))return failures+1;
        const EID vehicle=world.spawnUnit(0,drive?UnitType::WarMiner:UnitType::ChronoMiner,10.5f,16.5f,type(drive?"HARV":"CMIN"));
        const EID victim=world.spawnUnit(1,UnitType::GI,11.5f,16.5f,type("E1"));
        if(!world.valid(vehicle) || !world.valid(victim))return failures+1;
        world.ents[vehicle].atkCd=world.ents[victim].atkCd=1000000;world.ents[victim].guard=true;
        if(differentLayer) {
            // Isolate the existing surface filter with a four-level deck;
            // this mechanism geometry is not a source-map fidelity sample.
            world.map.bridgeCells.assign(world.map.cells.size(),{});
            world.map.bridgeCells[16*world.map.w+11]={0,4,0};
            world.ents[victim].navLayer=world.ents[victim].previousNavLayer=world.ents[victim].goalLayer=1;
            world.unitOccSync(victim);
        } else world.ents[victim].tpSick=window+60;
        const int hp=world.ents[victim].hp;
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={vehicle};move.x=13.5f;move.y=16.5f;
        move.b=1;world.bindCommand(move);world.applyCmd(0,move);
        const bool admitted=world.ents[vehicle].state==UState::Moving;
        bool passedUnder=false;
        for(int elapsed=0;elapsed<window;++elapsed) {
            world.update();
            if(differentLayer && world.valid(vehicle) && world.valid(victim))
                passedUnder|=(int)world.ents[vehicle].x==11 && world.ents[vehicle].navLayer==0 && world.ents[victim].navLayer==1;
        }
        std::printf("CRUSHING_GUARD layer=%d admitted=%d alive=%d hp=%d/%d mover=%.4f,%.4f surface=%d victim_surface=%d passed_below=%d frozen=%d deck=%d\n",
            differentLayer,admitted,world.valid(victim),world.ents[victim].hp,hp,
            world.ents[vehicle].x,world.ents[vehicle].y,world.ents[vehicle].navLayer,world.ents[victim].navLayer,
            passedUnder,world.ents[victim].immobilized(),world.map.hasDeck(11,16));
        check(admitted && world.valid(victim) && world.ents[victim].hp==hp
            && world.ents[victim].x==11.5f && world.ents[victim].y==16.5f
            && (differentLayer?passedUnder && world.ents[vehicle].x==13.5f:world.ents[victim].immobilized()),
            differentLayer?"a source crusher passes below infantry on another surface without harming it"
                :"a source crusher preserves a chronoshift-immobilized infantry target");
    }
    for(bool killMover:{false,true}) {
        if(!fresh())return failures+1;
        const EID vehicle=world.spawnUnit(0,UnitType::Grizzly,10.5f,16.5f,type("MTNK"));
        const EID first=world.spawnUnit(1,UnitType::GI,11.5f,16.5f,type("E1"));
        const EID later=world.spawnUnit(1,UnitType::GI,11.5f,16.5f,type("E1"));
        const EID survivor=world.spawnUnit(1,UnitType::GI,11.5f,16.5f,type("E1"));
        const auto moving=world.entityRef(vehicle);
        for(const auto id:{vehicle,later,survivor,first})world.ents[id].atkCd=1000000;
        for(const auto id:{later,survivor,first})world.ents[id].guard=true;
        const auto mod=output/(killMover?"contact-dead-mover-v2":"contact-recipient-v2");
        std::filesystem::create_directories(mod/"assets/scripts");
        std::ofstream script(mod/"assets/scripts"/(killMover?"zzzz_contact_1.lua":"zzzz_contact_0.lua"));
        script<<"local ran=false\nfunction OnUnitKilled(id)\n if ran or id~="<<first<<" then return end\n ran=true\n";
        script<<" ra2.killEntity("<<(killMover?vehicle:later)<<")\n";
        script<<" ra2.spawnUnit("<<(killMover?0:1)<<",'"<<(killMover?"Grizzly":"GI")<<"',"
            <<(killMover?10.5f:11.5f)<<",16.5)\nend\n";
        script.close();
        const bool scripted=bool(script);contentAddModPath(mod.string(),true);g_script.init(&world,0,-1);
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={vehicle};move.x=13.5f;move.y=16.5f;
        world.bindCommand(move);world.applyCmd(0,move);
        const auto previousSize=world.ents.size();
        int elapsed=0;while(world.valid(first) && ++elapsed<=window)world.update();
        g_script.shutdown();
        const bool birth=world.ents.size()==previousSize+1 && world.ents.back().alive
            && world.ents.back().hp==unitDef(world.ents.back()).hp;
        const bool safe=scripted && !world.valid(first) && birth && (killMover
            ?!world.valid(moving) && world.valid(later) && world.valid(survivor)
            :world.valid(moving) && !world.valid(later) && !world.valid(survivor));
        std::printf("CRUSHING_CALLBACK mover_killed=%d elapsed=%d birth=%d moving_alive=%d later_alive=%d survivor_alive=%d safe=%d\n",
            killMover,elapsed,birth,world.valid(moving),world.valid(later),world.valid(survivor),safe);
        check(safe,"real death callbacks cannot crush newborn recipients or continue after the moving identity dies");
    }
    if(!fresh())return failures+1;
    {
        const auto path=output/"crushing-app11.sav";
        const bool saved=saveGameFile(path.string().c_str());
        const auto before=world.checksum();
        std::ifstream input(path,std::ios::binary);
        const std::string original{std::istreambuf_iterator<char>(input),{}};
        bool atomic=saved && !original.empty() && loadGameFile(path.string().c_str()) && world.checksum()==before;
        int mutationIndex=0;
        const auto currentVersion="Version = "+std::to_string(nativeCurrentRulesSceneVersion);
        for(const auto& mutation:std::array{
            std::pair{currentVersion.c_str(),"Version = 49"},
            std::pair{"Crusher = 1","Crusher = 2"},
            std::pair{"Crushable = 1","Crushable = 2"},
            std::pair{"Crushable = 1","Crushablx = 1"},
            std::pair{"[NativeCrushWarning]","[InvalidCrushWarning]"},
            std::pair{"PlayerScatter = 0","PlayerScatter = 2"},
            std::pair{"ComputerIQ = 5","ComputerIQ = -1"},
            std::pair{"ScatterIQ = 2","ScatterIx = 2"},
            std::pair{"Fraidycat = 0","Fraidycat = 2"}}) {
            auto bytes=original;const auto at=bytes.find(mutation.first);
            if(at==std::string::npos){atomic=false;continue;}
            bytes.replace(at,std::char_traits<char>::length(mutation.first),mutation.second);
            const auto broken=output/("crushing-app11-rejected-"+std::to_string(mutationIndex++)+".sav");
            {std::ofstream out(broken,std::ios::binary);out.write(bytes.data(),bytes.size());atomic &= bool(out);}
            atomic &= !loadGameFile(broken.string().c_str()) && world.checksum()==before;
        }
        check(atomic,"missing or invalid crushing flags and forged older scene headers reject without changing the active game");
    }
    for(const auto& cohort:std::array{
        std::pair{"OPENRA2_LEGACY_CRUSH_INPUT_DIR",46},
        std::pair{"OPENRA2_LEGACY_CRUSH_CONTACT_DIR",47}})if(const char* directory=std::getenv(cohort.first)) {
        for(const auto& sample:cases) {
            const auto path=std::filesystem::path(directory)/(std::string(sample.name)+".sav");
            World old;FILE* file=std::fopen(path.string().c_str(),"rb");bool same=file && old.loadGame(file);if(file)std::fclose(file);
            uint32_t expected=0;std::ifstream final(std::filesystem::path(directory)/(std::string(sample.name)+".final"));
            same=same && old.nativeSceneVersion==cohort.second && bool(final>>expected);
            for(int elapsed=0;elapsed<window && same;++elapsed)old.update();
            same=same && old.checksum()==expected;
            std::printf("CRUSHING_LEGACY scene=%d %s exact=%d\n",cohort.second,sample.name,same);
            check(same,"an actual historical save retains its original crushing result and final checksum");
        }
    }
    std::printf("CRUSHING_RESULT failures=%d\n",failures);
    return failures;
}
