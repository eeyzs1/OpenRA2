#include "game_regression.h"
#include "game/script.h"
#include "game/world_priv.h"
#include "world_save_sections.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
std::vector<uint8_t> temporalBytes(const World& world) {
    std::vector<uint8_t> bytes;FILE* file=std::tmpfile();
    if(file && world.saveGame(file) && std::fseek(file,0,SEEK_END)==0) {
        const auto n=std::ftell(file);if(n>0){bytes.resize((size_t)n);std::rewind(file);if(std::fread(bytes.data(),1,bytes.size(),file)!=bytes.size())bytes.clear();}
    }
    if(file)std::fclose(file);return bytes;
}
bool temporalLoad(const std::vector<uint8_t>& bytes,World& world) {
    FILE* file=std::tmpfile();bool ok=file && std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
    if(ok){std::rewind(file);ok=world.loadGame(file);}if(file)std::fclose(file);return ok;
}
int differentPixels(Image a,Image b) {
    if(!a.data || !b.data || a.width!=b.width || a.height!=b.height)return 0;
    auto* first=LoadImageColors(a);auto* second=LoadImageColors(b);int changed=0;
    if(first && second)for(int i=0;i<a.width*a.height;++i)changed+=first[i].r!=second[i].r || first[i].g!=second[i].g || first[i].b!=second[i].b;
    UnloadImageColors(first);UnloadImageColors(second);return changed;
}
}

int GameRegression::nativeTemporalRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);g_script.shutdown();int failures=0;
    const auto check=[&](bool pass,const std::string& label){failures+=!pass;std::printf("NATIVE_TEMPORAL %s %s\n",pass?"PASS":"FAIL",label.c_str());std::fflush(stdout);};
    check(IsWindowHidden(),"the Temporal regression opens a hidden window");
    for(const char* edition:{"ra2","yr"}) {
        resetMatchInteraction();ais.clear();matchSetup.reset();campaignMission=-1;localPlayer=0;
        phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;
        selectRulesChoice(edition[0]=='r'?RulesChoice::RA2:RulesChoice::YR);
        cfgCountry=(int)Country::America;cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgReferenceMap=false;cfgGameMode=0;
        auto& w=world;const auto profile=std::string("assets/rules/profiles/")+edition+".ini";
        const bool loaded=w.init(64,64,2311,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,profile.c_str(),{Country::America,Country::Russia});
        check(loaded && w.nativeTemporalEnabled,std::string(edition)+" loads source Temporal execution");if(!loaded)continue;
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        for(auto& player:w.players){player.isAI=false;player.defeated=false;}
        w.cratesEnabled=false;w.crates.clear();w.effects.clear();w.rebuildUnitOcc();
        w.spawnBuilding(0,BldType::ConYard,2,2,true);w.spawnBuilding(1,BldType::ConYard,55,55,true);
        const auto base=w;
        const auto reset=[&]{w=base;w.map.bldOccRef=&w.bldOcc;};
        const auto path=[&](const std::string& label){return (std::filesystem::path(outputDir)/(std::string(edition)+"-"+label)).string();};
        const auto type=[&](const char* name){for(int i=0;i<(int)w.nativeTypes.size();++i)if(w.nativeTypes[i]->id==name)return i;return -1;};
        const auto crew=[&](EID carrier){const int index=type("CLEG");if(index<0 || !w.valid(carrier))return false;
            const auto definition=w.nativeTypes[index];World::Ent::GarrisonedUnit passenger;passenger.type=definition->unit.type;passenger.hp=definition->unit.hp;
            passenger.nativeIdentity={index,definition,"temporal-fixture-crew","",false};w.ents[carrier].cargo={passenger};return true;};
        const auto source=[&](bool ifv=false,float x=12.5f,float y=12.5f){const EID id=w.spawnUnit(0,ifv?UnitType::IFV:UnitType::Chrono,x,y);if(ifv)crew(id);return id;};
        const auto victim=[&]{const EID id=w.spawnUnit(1,UnitType::Rhino,15.5f,12.5f);if(w.valid(id))w.ents[id].nativeIdentity.sleeping=true;return id;};
        const auto cast=[&](EID actor,EID target) {
            if(!w.valid(actor) || !w.valid(target))return false;
            w.ents[actor].target=target;w.ents[actor].state=UState::Attacking;
            return w.startNativeTemporal(actor,target,w.effWeapon(w.ents[actor],&w.ents[target])) && w.nativeTemporalBeams.contains(actor);
        };
        const auto remaining=[&](EID target){if(!w.valid(target))return int64_t{-1};auto found=w.nativeTemporalBeams.find(w.ents[target].nativeTemporalHead);return found==w.nativeTemporalBeams.end()?int64_t{-1}:found->second.remaining;};
        const auto save=[&](const std::string& label,int ticks) {
            const auto raw=path(label+"-world60.sav"),app=path(label+"-app11.sav");const auto bytes=temporalBytes(w);
            {std::ofstream output(raw,std::ios::binary);output.write((const char*)bytes.data(),bytes.size());}
            World restored;auto replay=std::make_unique<Game>();bool exact=!bytes.empty() && temporalLoad(bytes,restored)
                && saveGameFile(app.c_str()) && replay->loadGameFile(app.c_str());const auto before=w;
            exact &= w.checksum()==restored.checksum() && w.checksum()==replay->world.checksum();
            for(int i=0;i<ticks && exact;++i){w.update();restored.update();replay->world.update();exact=w.checksum()==restored.checksum() && w.checksum()==replay->world.checksum();}
            check(exact,"versioned World and App11 resume "+label+" exactly on every tick");w=before;w.map.bldOccRef=&w.bldOcc;return exact;
        };

        for(bool ifv:{false,true}) {
            reset();const EID actor=source(ifv),target=victim();const auto label=std::string(ifv?"IFV":"CLEG");
            const bool actors=w.valid(actor) && w.valid(target);check(actors,label+" actors exist");if(!actors)continue;
            w.ents[actor].dir=w.ents[actor].turretDir=dirFromVec(3,0);
            const auto order=w.orderAttack({actor},target);check(order.accepted==1,label+" ordinary attack order is admitted");
            w.ents[target].hp=200;w.update();
            check(w.nativeTemporalBeams.contains(actor) && w.ents[actor].atkCd>0 && w.ents[target].immobilized(),label+" an actual weapon shot starts the controller");
            check(w.ents[target].hp==200 && remaining(target)==4000,label+" first hit freezes without HP damage and uses full source Strength");
            const auto frozen=w.ents[target];w.damage(target,80,0,actor);
            const auto move=w.orderMove({target},24.5f,12.5f,false);const auto attack=w.orderAttack({target},actor);
            check(w.ents[target].hp==200 && move.accepted==0 && attack.accepted==0,label+" frozen target ignores conventional damage and rejects combat/movement orders");
            {
                auto interaction=w;interaction.map.bldOccRef=&interaction.bldOcc;
                const EID tank=interaction.spawnUnit(0,UnitType::Grizzly,11.5f,12.5f),yuri=interaction.spawnUnit(0,UnitType::Yuri,12.5f,13.5f);
                check(interaction.orderAttack({tank},target).accepted==0 && interaction.orderAttack({yuri},target).accepted==0,
                    label+" frozen victims reject conventional and mind-control attack orders");
                const int owner=interaction.ents[target].player;for(int i=0;i<3;++i)interaction.update();
                check(interaction.ents[target].player==owner && interaction.ents[target].mindBy==INVALID_EID,
                    label+" a sustained mind-control attempt cannot capture a frozen victim");
            }
            const int damage=w.nativeTemporalDamage(actor);const int end=2*((4000+damage-1)/damage);
            while((int)w.tick<end-1 && w.valid(target))w.update();
            check(w.valid(target) && w.ents[target].hp==200 && w.ents[target].x==frozen.x && w.ents[target].y==frozen.y,
                label+" victim stays frozen until the original base-damage clock expires");
            save(label+"-before-erase",3);w.update();
            check(!w.valid(target) && w.tick==end && w.nativeTemporalBeams.empty() && w.ents[actor].kills==1,
                label+" erases on the exact source frame with kill credit and no lingering beam");
            check(std::none_of(w.effects.begin(),w.effects.end(),[](const Effect& effect){return effect.kind==0 || effect.kind==4 || effect.kind==10;}),
                label+" erasure emits no physical explosion or infantry death animation");
            save(label+"-after-erase",12);
        }

        reset();EID a=source(),b=source(true,12.5f,13.5f),c=source(false,13.5f,13.5f),target=victim();
        check(cast(a,target) && cast(b,target) && cast(c,target),"three sources join one target");
        check(w.ents[target].nativeTemporalHead==a && w.nativeTemporalBeams[a].prev==c && w.nativeTemporalBeams[c].prev==b,
            "new helpers are inserted immediately behind the original head");
        w.tick=2;w.updateNativeTemporalTarget(target);const auto budget=remaining(target);
        check(budget==4000-w.nativeTemporalDamage(a)-w.nativeTemporalDamage(b)-w.nativeTemporalDamage(c),"mixed CLEG/IFV helpers sum their edition-specific base damages");
        save("three-beams",35);
        w.stopNativeTemporal(c);check(remaining(target)==budget && w.nativeTemporalBeams[a].prev==b && w.nativeTemporalBeams[b].next==a,
            "removing a middle helper preserves head progress and both links");
        w.orderStop({a});check(w.ents[target].nativeTemporalHead==b && remaining(target)==budget && w.ents[target].immobilized(),
            "Stop on the head transfers exact progress to its next helper");
        save("promoted-head",15);w.orderStop({b});
        check(!w.ents[target].immobilized() && w.nativeTemporalBeams.empty(),"Stop on the final helper immediately releases the target");

        reset();a=source();b=source(false,12.5f,13.5f);target=victim();cast(a,target);cast(b,target);w.tick=2;w.updateNativeTemporalTarget(target);
        const auto dyingBudget=remaining(target);w.kill(a,false);
        check(w.ents[target].nativeTemporalHead==b && remaining(target)==dyingBudget && w.validateNativeTemporalState(),"head death promotes a helper without resetting or releasing the victim");
        save("dead-head",20);const auto oldTarget=w.entityRef(target);w.kill(target,false);
        const auto replacement=w.spawnUnit(1,UnitType::Rhino,15.5f,12.5f);
        check(!w.valid(oldTarget) && w.nativeTemporalBeams.empty() && w.valid(replacement) && w.ents[replacement].nativeTemporalHead==INVALID_EID,
            "target death and slot reuse never attach old beams to a successor");
        save("reused-target",12);

        reset();a=source();target=victim();cast(a,target);const auto move=w.orderMove({a},10.5f,12.5f,false);
        check(move.accepted==1 && !w.ents[target].immobilized() && w.nativeTemporalBeams.empty(),"an accepted movement order immediately stops the outgoing beam");
        reset();a=source();target=victim();cast(a,target);b=victim();w.orderAttack({a},b);
        check(!w.ents[target].immobilized() && w.nativeTemporalBeams.empty(),"retargeting immediately releases the previous victim");
        reset();a=source();target=victim();cast(a,target);w.ents[a].x=2.5f;w.unitOccSync(a);w.update();
        check(!w.ents[target].immobilized() && w.nativeTemporalBeams.empty(),"loss of source range releases the victim at the update boundary");
        reset();a=source();b=source(true,13.5f,12.5f);target=victim();cast(b,target);cast(a,b);
        check(w.ents[b].immobilized() && !w.ents[target].immobilized() && !w.nativeTemporalBeams.contains(b),"freezing a caster cancels that caster's outgoing beam immediately");
        check(w.orderUnload({b}).released==0 && w.ents[b].cargo.size()==1,"a frozen IFV cannot release its anonymous crew");
        save("incoming-cancels-outgoing",10);

        reset();a=source(true);target=victim();cast(a,target);const auto released=w.orderUnload({a});
        if(released.pending) {
            check(released.pending==1 && w.ents[a].cargo.size()==1 && w.ents[target].immobilized() && w.nativeTemporalBeams.contains(a),
                "a ground IFV keeps its crew and beam until the queued unload mission starts");
            save("crew-unload-pending",20);
            for(int i=0;i<180 && !w.ents[a].cargo.empty();++i)w.update();
        }
        check((released.released==1 || released.pending==1) && w.ents[a].cargo.empty() && !w.ents[target].immobilized() && !w.nativeTemporalBeams.contains(a),
            "actual IFV unloading cancels its crew's Temporal beam");

        reset();a=source();target=victim();w.ents[target].invuln=100;
        check(w.attackRejection(a,target)==AttackRejectReason::Ineffective && !cast(a,target) && !w.ents[target].immobilized(),"Iron Curtain rejects both the order and direct Temporal dispatch");
        w.ents[target].invuln=0;auto protectedType=std::make_shared<NativeActorDefinition>(*w.ents[target].nativeIdentity.definition);
        protectedType->warpable=false;w.ents[target].nativeIdentity.definition=protectedType;
        check(w.attackRejection(a,target)==AttackRejectReason::Ineffective && !cast(a,target),"source Warpable=no blocks erasure independently of ordinary armor verses");

        reset();a=source();target=victim();w.ents[a].vetRank=1;w.ents[a].crateDmgBoost=5000;cast(a,target);w.tick=2;w.updateNativeTemporalTarget(target);
        check(w.effWeapon(w.ents[a]).damage>8 && remaining(target)==3992,"veteran FIREPOWER and crates never accelerate a Temporal clock");
        w.ents[a].vetRank=2;w.updateNativeTemporalTarget(target);
        check(remaining(target)==3976,"promotion switches to the original elite weapon's base damage");

        reset();a=source();target=w.spawnUnit(1,UnitType::GI,15.5f,12.5f);cast(a,target);w.nativeTemporalBeams[a].remaining=8;w.tick=2;w.effects.clear();
        w.updateNativeTemporalTarget(target);
        check(!w.valid(target) && std::any_of(w.effects.begin(),w.effects.end(),[](const Effect& effect){return effect.kind==9;})
            && std::none_of(w.effects.begin(),w.effects.end(),[](const Effect& effect){return effect.kind==0 || effect.kind==10;}),
            "infantry erasure uses the source erase animation without an ordinary death sequence");
        reset();a=source();target=w.spawnBuilding(1,BldType::NuclearReactor,15,11,true);cast(a,target);w.nativeTemporalBeams[a].remaining=8;w.tick=2;w.effects.clear();
        const int sourceHP=w.ents[a].hp;w.updateNativeTemporalTarget(target);
        check(!w.valid(target) && w.bldAt(15,11)==INVALID_EID && w.ents[a].hp==sourceHP && w.timedBombs.empty()
            && std::none_of(w.effects.begin(),w.effects.end(),[](const Effect& effect){return effect.kind==0 || effect.kind==4 || effect.kind==12;}),
            "reactor erasure removes its foundation and power without a meltdown or collateral damage");

        reset();target=victim();std::vector<EID> helpers;
        for(int i=0;i<62;++i){const EID actor=source();helpers.push_back(actor);cast(actor,target);}
        w.tick=2;w.updateNativeTemporalTarget(target);
        check(remaining(target)==4000-52*8 && w.nativeTemporalBeams.size()==62 && w.validateNativeTemporalState(),
            "the isolated RA2 helper boundary is head plus 51 contributors without dropping later links");
        save("helper-cap",3);

        reset();a=source();target=w.spawnBuilding(1,BldType::WarFactory,15,11,true);cast(a,target);
        const auto facilities=w.productionFacilities(1,true,(int)UnitType::Rhino);
        check(w.ents[target].immobilized() && !w.productionFacilityOperational(w.ents[target]) && facilities.frozen==1,
            "a frozen factory is reported as present and frozen but cannot produce");
        const EID engineer=w.spawnUnit(0,UnitType::Engineer,13.5f,11.5f);
        check(w.engineerRejection(engineer,target,EngineerIntent::Capture)!=EngineerRejectReason::None,
            "engineers cannot capture a building while it is being erased");
        w.ents[target].hp-=50;const auto funds=w.players[1].money;w.sellBuilding(target);
        check(!w.ents[target].selling && !w.repairBuilding(target) && w.players[1].money==funds,"frozen buildings reject selling and repair without spending or refunding money");
        save("frozen-factory",21);w.orderStop({a});
        check(w.productionFacilityOperational(w.ents[target]),"releasing a factory restores operational admission");

        reset();a=source();target=w.spawnBuilding(1,BldType::NukeSilo,15,11,true);w.players[1].swCharge[(int)SWType::Nuke]=321;
        const bool hasNuke=w.swAvailable(1,SWType::Nuke);cast(a,target);w.syncSuperweaponSources();const auto charge=w.players[1].swCharge[(int)SWType::Nuke];
        for(int i=0;i<5;++i)w.update();
        check(hasNuke && charge==321 && w.players[1].swCharge[(int)SWType::Nuke]==321 && !w.swAvailable(1,SWType::Nuke),
            "freezing the sole superweapon source suspends availability while preserving charge");
        save("frozen-superweapon",15);w.orderStop({a});
        check(w.swAvailable(1,SWType::Nuke) && w.players[1].swCharge[(int)SWType::Nuke]==321,"unfreezing restores the same superweapon charge");

        reset();a=source();const int airport=type("CAAIRP");target=airport<0?INVALID_EID:w.spawnNativeActor(airport,1,15,11,256,0,"temporal-airport","","Guard");
        w.spawnBuilding(1,BldType::PowerPlant,40,40,true);w.players[1].support[0].ticks=321;cast(a,target);
        for(int i=0;i<5;++i)w.update();
        check(w.valid(target) && w.hasSupportSource(1,SupportType::Paradrop) && w.players[1].support[0].ticks==321 && !w.supportReady(1,SupportType::Paradrop),
            "a frozen airport remains a support source but its recharge is suspended");
        save("frozen-support",11);w.orderStop({a});const int oldCharge=w.players[1].support[0].ticks;
        w.ents[a].nativeIdentity.sleeping=true;w.update();
        check(w.players[1].support[0].ticks==oldCharge+1,"support recharge resumes from its retained value after unfreezing");

        reset();a=source();target=victim();cast(a,target);b=source(false,12.5f,13.5f);cast(b,target);const auto bytes=temporalBytes(w);
        const auto section=locateWorldSaveSection(bytes,WorldSaveSection::NativeTemporal);
        const auto* expectedWorld=WorldSaveFormat::forRevision(WorldSaveFormat::ordinaryRevision(w.nativeProductionRules.version));
        check(section && expectedWorld && worldSaveHeaderMatches(bytes,expectedWorld->magic.data()),
            "current ordinary saves use their declared World revision and contain a bounded Temporal suffix");
        if(section) {
            const auto mark=section->offset;const auto hash=w.checksum();
            const auto rejects=[](const std::vector<uint8_t>& data){World discarded;return !temporalLoad(data,discarded);};
            auto bad=bytes;const uint32_t huge=0xffffffffu;std::memcpy(bad.data()+mark+9,&huge,4);
            check(rejects(bad),"an oversized controller count is rejected before allocation");
            bad=bytes;bad[mark+17]^=1;check(rejects(bad),"a stale source generation cannot bind to a live entity");
            bad=bytes;bad.pop_back();check(rejects(bad),"a truncated Temporal suffix is rejected");
            bad=bytes;bad[6]='5';bad[7]='8';bad.resize(mark);
            check(rejects(bad),"a Scene35 file cannot discard its controller suffix and masquerade as World58");
            const auto app=path("transaction-app11.sav");bool transactional=saveGameFile(app.c_str());
            std::ifstream input(app,std::ios::binary);std::vector<uint8_t> appBytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
            const auto at=embeddedWorldSaveOffset(appBytes);
            transactional &= at.has_value();
            if(transactional) {
                const auto offset=*at;appBytes[offset+mark+17]^=1;
                const auto rejected=path("stale-controller.appbad");{std::ofstream output(rejected,std::ios::binary);output.write((const char*)appBytes.data(),appBytes.size());}
                transactional=!loadGameFile(rejected.c_str()) && w.checksum()==hash;
            }
            check(transactional,"application loading rejects stale Temporal identities without changing the running game");
        }
        auto malformed=w;malformed.map.bldOccRef=&malformed.bldOcc;malformed.nativeTemporalBeams[a].prev=a;
        check(!malformed.validateNativeTemporalState() && temporalBytes(malformed).empty(),"a cyclic collaboration graph cannot be saved");

        {
            auto reused=w;reused.map.bldOccRef=&reused.bldOcc;World fresh;
            const auto loadOld=[](World& out){FILE* input=std::fopen("tests/fixtures/native-temporal-v58/fusion-world58.sav","rb");
                const bool ok=input && out.loadGame(input);if(input)std::fclose(input);return ok;};
            const bool freshLoaded=loadOld(fresh),reusedLoaded=loadOld(reused);
            check(freshLoaded && reusedLoaded && !reused.nativeTemporalEnabled && reused.nativeTemporalBeams.empty()
                && reused.checksum()==fresh.checksum(),"a live Source35 World can load an actual World58 fusion save without retaining new controllers");
            check(reusedLoaded && std::any_of(reused.ents.begin(),reused.ents.end(),[](const auto& e){return e.alive && e.chrono==17;})
                && std::all_of(reused.ents.begin(),reused.ents.end(),[](const auto& e){return e.nativeTemporalHead==INVALID_EID;}),
                "the actual old fixture preserves its separate historical chrono progress");
            if(reusedLoaded){const auto file=path("fusion-migrated-world59.sav");const auto migrated=temporalBytes(reused);
                std::ofstream output(file,std::ios::binary);output.write((const char*)migrated.data(),migrated.size());}
        }

        reset();a=source(true);target=victim();cast(a,target);w.map.reveal(0,32,32,64);w.effects.clear();
        camZoom=1;camX=-450;camY=0;bakeTerrain();shotAtDisplaySize=true;bool visuals=true;
        for(const auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);const auto hash=w.checksum();const auto filename=path("beam-"+std::to_string(size.x)+".png");shotFile=filename;render();
            Image beam=LoadImage(filename.c_str());const auto graph=w.nativeTemporalBeams;w.nativeTemporalBeams.clear();
            const auto absent=path("beam-"+std::to_string(size.x)+"-without.png");shotFile=absent;render();w.nativeTemporalBeams=graph;
            Image without=LoadImage(absent.c_str());const int changed=differentPixels(beam,without);
            std::printf("NATIVE_TEMPORAL PIXELS %s %d %d\n",edition,size.x,changed);
            visuals &= changed>20 && w.checksum()==hash;UnloadImage(beam);UnloadImage(without);
        }
        shotAtDisplaySize=false;shotFile.clear();check(visuals,"ordinary rendering shows the sustained beam at both window sizes without changing World");
        save("rendered-active-beam",30);
    }
    return failures;
}
