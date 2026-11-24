#include "game_regression.h"
#include "game/script.h"
#include "game/world_priv.h"
#include "world_save_sections.h"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <memory>

namespace {
std::vector<uint8_t> crewBytes(const World& world) {
    FILE* file=std::tmpfile();std::vector<uint8_t> result;
    if(file && world.saveGame(file) && !std::fseek(file,0,SEEK_END)) {
        const auto length=std::ftell(file);if(length>0){result.resize((size_t)length);std::rewind(file);if(std::fread(result.data(),1,result.size(),file)!=result.size())result.clear();}
    }
    if(file)std::fclose(file);return result;
}
bool crewLoad(const std::vector<uint8_t>& bytes,World& world) {
    FILE* file=std::tmpfile();bool ok=file && !bytes.empty() && std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
    if(ok){std::rewind(file);ok=world.loadGame(file);}if(file)std::fclose(file);return ok;
}
void crewWrite(const std::string& path,const std::vector<uint8_t>& bytes) {
    std::ofstream output(path,std::ios::binary);output.write((const char*)bytes.data(),bytes.size());
}
}

int GameRegression::nativeCrewRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);g_script.shutdown();int failures=0;
    const auto check=[&](bool pass,const std::string& label){failures+=!pass;std::printf("NATIVE_CREW %s %s\n",pass?"PASS":"FAIL",label.c_str());std::fflush(stdout);};
    check(IsWindowHidden(),"crew regression uses a hidden window");
    for(const char* edition:{"ra2","yr"}) {
        const bool yr=edition[0]=='y';resetMatchInteraction();ais.clear();matchSetup.reset();campaignMission=-1;localPlayer=0;phase=Phase::InGame;
        selectRulesChoice(yr?RulesChoice::YR:RulesChoice::RA2);cfgCountry=(int)Country::America;cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgReferenceMap=false;cfgGameMode=0;
        auto& w=world;const auto profile=std::string("assets/rules/profiles/")+edition+".ini";
        const bool loaded=w.init(64,64,2311,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,profile.c_str());
        check(loaded && w.nativeCrewRules.version==2,std::string(edition)+" loads explicit Scene43/Crew2 contract");if(!loaded)continue;
        for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
        for(auto& cell:w.map.cells)cell=Cell{};for(auto& p:w.players){p.isAI=false;p.defeated=false;}
        w.cratesEnabled=false;w.crates.clear();w.effects.clear();w.rebuildUnitOcc();w.spawnBuilding(0,BldType::ConYard,2,2,true);w.spawnBuilding(1,BldType::ConYard,55,55,true);
        const auto base=w;const auto reset=[&]{w=base;w.map.bldOccRef=&w.bldOcc;};
        const auto path=[&](const std::string& name){return (std::filesystem::path(outputDir)/(std::string(edition)+"-"+name)).string();};
        const auto remaining=[&](EID target){const auto node=w.nativeTemporalBeams.find(w.ents[target].nativeTemporalHead);return node==w.nativeTemporalBeams.end()?int64_t{-1}:node->second.remaining;};
        const auto victim=[&](float x=18.5f){const EID id=w.spawnUnit(1,UnitType::Rhino,x,12.5f);w.ents[id].nativeIdentity.sleeping=true;return id;};
        const auto board=[&](EID carrier,UnitType type,int count) {
            std::vector<EID> actors;for(int i=0;i<count;++i)actors.push_back(w.spawnUnit(0,type,12.5f+i*.25f,13.5f));
            const auto ordered=w.orderBoard(actors,carrier);check(ordered.accepted==count,"real passenger boarding orders accepted");
            for(int i=0;i<20;++i){bool pending=false;for(EID id:actors)pending|=w.valid(id);if(!pending)break;w.update();}
            bool consumed=true;for(EID id:actors)consumed&=!w.valid(id);check(consumed,"boarding stores passengers and retires old entity handles");
        };
        const auto save=[&](const std::string& label,int ticks=30) {
            const auto bytes=crewBytes(w);crewWrite(path(label+"-world60.sav"),bytes);
            World restored;auto app=std::make_unique<Game>();const auto snapshot=w;
            bool exact=!bytes.empty() && crewLoad(bytes,restored) && saveGameFile(path(label+"-app11.sav").c_str()) && app->loadGameFile(path(label+"-app11.sav").c_str());
            exact &= w.checksum()==restored.checksum() && w.checksum()==app->world.checksum();
            for(int i=0;i<ticks && exact;++i){w.update();restored.update();app->world.update();exact=w.checksum()==restored.checksum() && w.checksum()==app->world.checksum();}
            check(exact,"World60 and actual App11 resume "+label+" on every tick");
            if(exact)crewWrite(path(label+"-resumed-world60.sav"),crewBytes(w));w=snapshot;w.map.bldOccRef=&w.bldOcc;return exact;
        };
        check(w.validateNativeCrewState(),"new entity identities are unique and saveable");
        check(w.nativeCrewRules.passengerOccupy==yr,"RA2 building guns and YR occupant weapons remain distinct");
        if(yr)check(w.nativeCrewRules.openRange==2 && w.nativeCrewRules.warpDistance==7 && w.nativeCrewRules.openDamage==1.2f,"original open-topped range, damage and warp distance imported");

        reset();int buildingType=-1;for(int i=0;i<(int)w.nativeTypes.size();++i)if(w.nativeTypes[i]->isBuilding && w.nativeTypes[i]->building.type==BldType::CivHouse && w.nativeTypes[i]->building.garrisonCap>0){buildingType=i;break;}
        const EID house=buildingType<0?INVALID_EID:w.spawnNativeActor(buildingType,0,12,12,256,0,"crew-house","","Guard");
        check(w.valid(house),"source urban building exists");
        if(w.valid(house)) {
            EID gi=w.spawnUnit(0,UnitType::GI,10.5f,13.5f),chrono=w.spawnUnit(0,UnitType::Chrono,10.5f,14.5f);
            check(w.entryRejection(chrono,house,EntryKind::Garrison)==EntryRejectReason::Incompatible,"CLEG without Occupier cannot garrison an urban building");
            check(w.orderGarrison({gi},house).accepted==1,"source Occupier GI enters urban building");
            for(int i=0;i<30 && w.valid(gi);++i)w.update();
            check(w.ents[house].garrison.size()==1,"real garrison preserves a single passenger");
            if(!w.ents[house].garrison.empty()) {
                auto& p=w.ents[house].garrison[0];const uint64_t identity=p.combatActor;
                const auto weapon=w.nativeCrewWeapon(w.ents[house],p);
                check(identity!=0 && weapon.nativeSource && weapon.nativeSource->id==(yr?"UCPara":"AlliedOccupyW"),"urban fire selects this edition's source weapon");
                check(weapon.damage==(yr?36:30) && weapon.cooldown==(yr?36:30),"urban base damage and ROF use source multipliers once");
                const EID target=victim(18.5f);const int hp=w.ents[target].hp;p.atkCd=0;w.presentationEvents.clear();w.effects.clear();w.updateNativeCrew(house);
                check(w.ents[target].hp<hp && p.atkCd==weapon.cooldown,"actual urban shot uses passenger cooldown");
                check(std::any_of(w.effects.begin(),w.effects.end(),[](const auto& effect){return effect.kind==5;})
                    && std::any_of(w.presentationEvents.begin(),w.presentationEvents.end(),[&](const auto& event){return event.kind==PresentationKind::Sound && event.sound==Sfx::Shot;}),"urban fire retains basic muzzle flash and weapon sound");
                save("urban");const auto released=w.orderUngarrison({house});
                check(released.released==1,"actual ungarrison releases the shooter");
                EID restored=INVALID_EID;for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].combatActor==identity)restored=id;
                check(w.valid(restored) && w.ents[restored].atkCd==weapon.cooldown,"unloaded passenger retains identity and weapon cooldown");
                save("unloaded-urban");
            }
        }
        reset();const EID transport=w.spawnUnit(0,UnitType::AmphTransport,12.5f,13.5f);
        const EID tank=w.spawnUnit(0,UnitType::Grizzly,12.5f,12.5f);const uint64_t tankIdentity=w.ents[tank].combatActor;
        const EID tankTarget=victim(16.5f);w.ents[tankTarget].hp=1;
        // Keep the hull aligned with the carrier while the independent
        // turret fires east, so the real projectile remains in flight on entry.
        w.ents[tank].dir=dirFromVec(0,1);w.ents[tank].turretDir=dirFromVec(1,0);
        check(w.orderAttack({tank},tankTarget).accepted==1,"actual tank attack starts before boarding");w.update();
        check(w.valid(tankTarget) && std::any_of(w.projs.begin(),w.projs.end(),[&](const auto& p){return p.alive && p.sourceActor==tankIdentity;}),"actual delayed projectile captures its physical shooter's durable identity");
        check(w.orderBoard({tank},transport).accepted==1,"firing tank can board an amphibious transport");
        for(int i=0;i<20 && w.valid(tank);++i)w.update();
        check(!w.valid(tank) && w.findCrewPassenger(transport,tankIdentity) && w.valid(tankTarget)
            && std::any_of(w.projs.begin(),w.projs.end(),[&](const auto& p){return p.alive && p.sourceActor==tankIdentity;}),
            "boarding transfers the actual tank identity to cargo before its delayed impact");
        save("real-projectile-boarding",80);
        for(int i=0;i<80 && w.valid(tankTarget);++i)w.update();
        const auto* credited=w.findCrewPassenger(transport,tankIdentity);
        check(!w.valid(tankTarget) && credited && credited->kills==1 && w.ents[transport].kills==0,"real delayed impact credits stored shooter, not transport or reused entity slot");
        if(!yr)continue;

        reset();EID bf=w.spawnUnit(0,UnitType::BattleFortress,12.5f,12.5f);board(bf,UnitType::Chrono,2);w.ents[bf].atkCd=100000;
        if(w.ents[bf].cargo.size()!=2)continue;
        const uint64_t first=w.ents[bf].cargo[0].combatActor,second=w.ents[bf].cargo[1].combatActor;
        check(first && second && first!=second,"two CLEG passengers have distinct durable identities");
        EID target=victim();const int hp=w.ents[target].hp;w.updateNativeCrew(bf);
        check(w.nativeTemporalBeams.size()==2 && w.ents[target].immobilized() && w.ents[target].hp==hp,"two actual BF passenger shots freeze without HP damage");
        check(remaining(target)==4000 && w.validateNativeTemporalState(),"passenger head initializes full source Strength budget");
        w.ents[bf].cargo[1].vetRank=2;w.ents[bf].cargo[0].crateDmgBoost=500;
        w.tick=2;w.updateNativeTemporalTarget(target);const auto budget=remaining(target);
        check(budget==4000-8-16,"helper clock uses each passenger's current raw normal or elite damage");
        save("two-cleg");
        const EID physical=w.spawnUnit(0,UnitType::Chrono,14.5f,13.5f);w.ents[physical].target=target;w.ents[physical].state=UState::Attacking;
        w.startNativeTemporal(physical,target,w.effWeapon(w.ents[physical],&w.ents[target]));
        check(w.nativeTemporalBeams.size()==3 && w.validateNativeTemporalState(),"physical and contained casters share one collaboration chain");
        save("mixed-three");
        std::swap(w.ents[bf].cargo[0],w.ents[bf].cargo[1]);
        check(w.nativeTemporalDamage(World::nativeCrewTemporalKey(first))==8 && w.nativeTemporalDamage(World::nativeCrewTemporalKey(second))==16
            && w.validateNativeTemporalState(),"reordering passenger records preserves both controller identities and weapon ranks");save("swapped-seats");
        const auto key=World::nativeCrewTemporalKey(first);w.consumeCrewPassenger(bf,first);
        check(!w.nativeTemporalBeams.contains(key) && w.ents[target].nativeTemporalHead==physical && remaining(target)==budget,"consuming the head passenger promotes newest helper without reset");
        check(w.findCrewPassenger(bf,second) && w.nativeTemporalDamage(World::nativeCrewTemporalKey(second))==16,"shifting passenger seats never changes the helper identity");
        w.orderStop({physical});check(w.ents[target].nativeTemporalHead==World::nativeCrewTemporalKey(second) && remaining(target)==budget,"last passenger inherits the retained head clock");
        save("passenger-relay");
        const auto moved=w.orderMove({bf},13,12,false);check(moved.accepted==1 && w.ents[target].immobilized(),"moving BF retains a passenger beam inside warp distance");
        w.ents[bf].x=2.5f;w.updateNativeTemporalSource(bf);check(!w.ents[target].immobilized() && w.nativeTemporalBeams.empty(),"warp distance releases passenger beams after carrier movement");

        reset();bf=w.spawnUnit(0,UnitType::BattleFortress,12.5f,12.5f);board(bf,UnitType::Chrono,2);w.ents[bf].atkCd=100000;target=victim();w.updateNativeCrew(bf);
        auto saved=crewBytes(w);const auto crewSection=locateWorldSaveSection(saved,WorldSaveSection::NativeCrew),temporal=locateWorldSaveSection(saved,WorldSaveSection::NativeTemporal);
        check(crewSection && temporal,"World60 contains bounded crew and extended Temporal blocks");
        const auto rejects=[&](const std::vector<uint8_t>& bytes){World unused;return !crewLoad(bytes,unused);};
        if(crewSection && temporal) {
            auto bad=saved;bad[crewSection->offset+9]=0;for(int i=10;i<17;++i)bad[crewSection->offset+i]=0;check(rejects(bad),"zero identity sequence is rejected");
            bad=saved;const uint32_t huge=0xffffffffu;std::memcpy(bad.data()+crewSection->offset+17,&huge,4);check(rejects(bad),"oversized identity count rejected before allocation");
            bad=saved;const uint64_t stale=0;std::memcpy(bad.data()+temporal->offset+25,&stale,8);check(rejects(bad),"passenger beam cannot masquerade as a carrier beam");
            bad=saved;bad[temporal->offset+17]^=1;check(rejects(bad),"stale carrier generation cannot own a passenger beam");
            bad=saved;bad.pop_back();check(rejects(bad),"truncated crew state is rejected");
            const uint64_t passengerA=w.ents[bf].cargo[0].combatActor,passengerB=w.ents[bf].cargo[1].combatActor;
            bad=saved;uint32_t identities=0;std::memcpy(&identities,bad.data()+crewSection->offset+17,4);bool changed=false;
            for(uint32_t i=0;i<identities;++i){uint64_t value=0;const auto at=crewSection->offset+25+8*i;std::memcpy(&value,bad.data()+at,8);
                if(value==passengerB){std::memcpy(bad.data()+at,&passengerA,8);changed=true;break;}}
            check(changed && rejects(bad),"loader rejects duplicate passenger identities in an actual serialized block");
            auto malformed=w;malformed.map.bldOccRef=&malformed.bldOcc;malformed.ents[bf].cargo[1].combatActor=malformed.ents[bf].cargo[0].combatActor;
            check(!malformed.validateNativeCrewState() && crewBytes(malformed).empty(),"duplicate passenger identities cannot be saved");
            const auto before=w.checksum();const auto appPath=path("transaction.app");bool transactional=saveGameFile(appPath.c_str());
            std::ifstream input(appPath,std::ios::binary);std::vector<uint8_t> app{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
            const std::string magic="RA2WLD60";auto at=std::search(app.begin(),app.end(),magic.begin(),magic.end());transactional &= at!=app.end();
            if(transactional){const auto offset=(size_t)(at-app.begin());app[offset+crewSection->offset+9]=0;for(int i=10;i<17;++i)app[offset+crewSection->offset+i]=0;
                crewWrite(path("transaction-bad.app"),app);transactional=!loadGameFile(path("transaction-bad.app").c_str()) && w.checksum()==before;}
            check(transactional,"actual application rejects corrupt crew state without replacing the running match");
        }
        const uint64_t releasedIdentity=w.ents[bf].cargo.back().combatActor;
        const auto unload=w.orderUnload({bf});check(unload.released==2 && !w.ents[target].immobilized() && w.nativeTemporalBeams.empty(),"actual unload detaches both independent beams immediately");
        EID soldier=INVALID_EID;for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].combatActor==releasedIdentity)soldier=id;
        check(w.valid(soldier),"real unloaded CLEG retains its original identity");
        save("unloaded-cleg");

        reset();bf=w.spawnUnit(0,UnitType::BattleFortress,12.5f,12.5f);board(bf,UnitType::Chrono,1);w.ents[bf].atkCd=100000;target=victim();w.updateNativeCrew(bf);
        EID attacker=w.spawnUnit(1,UnitType::Chrono,14.5f,13.5f);w.ents[attacker].target=bf;w.ents[attacker].state=UState::Attacking;w.startNativeTemporal(attacker,bf,w.effWeapon(w.ents[attacker],&w.ents[bf]));
        check(w.ents[bf].immobilized() && !w.ents[target].immobilized() && w.nativeTemporalBeams.size()==1,"freezing a carrier cancels all of its outgoing passenger beams");
        const auto cooldown=w.ents[bf].cargo[0].atkCd;w.update();check(w.ents[bf].cargo[0].atkCd==cooldown,"frozen carrier pauses passenger timers");save("frozen-carrier");
        w.kill(bf,false);check(w.nativeTemporalBeams.empty() && w.validateNativeCrewState(),"carrier death removes incoming and outgoing links and passenger identities");

        reset();bf=w.spawnUnit(0,UnitType::BattleFortress,12.5f,12.5f);board(bf,UnitType::Chrono,1);target=victim();w.updateNativeCrew(bf);
        {auto corrupt=crewBytes(w);const auto section=locateWorldSaveSection(corrupt,WorldSaveSection::NativeTemporal);
            if(section){const uint64_t zero=0;std::memcpy(corrupt.data()+section->offset+25,&zero,8);}
            check(section && rejects(corrupt),"a single passenger beam cannot masquerade as the non-Temporal carrier");}
        reset();bf=w.spawnUnit(0,UnitType::BattleFortress,12.5f,12.5f);board(bf,UnitType::FlakTrooper,1);w.ents[bf].atkCd=100000;target=victim(16.5f);
        auto& gi=w.ents[bf].cargo[0];gi.vetRank=2;const auto weapon=w.nativeCrewWeapon(w.ents[bf],gi,&w.ents[target]);w.updateNativeCrew(bf);
        check(weapon.burst>1 && w.nativeCrewBursts.size()==1 && gi.atkCd==weapon.cooldown,"elite infantry creates an independent source burst and ROF");
        save("active-burst",40);
        {auto bad=w;bad.map.bldOccRef=&bad.bldOcc;if(!bad.nativeCrewBursts.empty())bad.nativeCrewBursts.begin()->second.remaining=64;
            check(!bad.validateNativeCrewState() && crewBytes(bad).empty(),"oversized pending passenger bursts cannot be saved");}
        const auto burstIdentity=gi.combatActor;w.orderUnload({bf});check(!w.nativeCrewBursts.contains(burstIdentity),"unloading cancels only that passenger's pending burst");

        reset();bf=w.spawnUnit(0,UnitType::BattleFortress,12.5f,12.5f);board(bf,UnitType::GI,1);w.ents[bf].atkCd=100000;
        const auto shooter=w.ents[bf].cargo[0].combatActor;target=victim(17.5f);w.ents[target].hp=1;
        // This delayed-projectile fixture isolates identity after unloading;
        // real GGI boarding and weapon selection run in the companion suite.
        const auto missile=std::find_if(w.nativeWeaponCatalog->weapons.begin(),w.nativeWeaponCatalog->weapons.end(),[](const auto& d){return d.id=="MissileLauncher";});
        check(missile!=w.nativeWeaponCatalog->weapons.end(),"actual source missile exists for delayed identity fixture");
        if(missile!=w.nativeWeaponCatalog->weapons.end()) {
            const auto& pd=*missile->projectile;Projectile p;p.w=missile->weapon;p.kind=ProjKind::Missile;p.player=0;p.x=p.ox=12.5f;p.y=p.oy=12.5f;p.tx=17.5f;p.ty=12.5f;
            p.target=target;p.src=bf;p.sourceActor=shooter;p.rot=pd.motion.rot;p.speed=10;w.projs.push_back(p);
            w.orderUnload({bf});w.kill(bf,false);check(w.projs[0].src==INVALID_EID && w.projs[0].sourceActor==shooter,"carrier retirement preserves projectile's durable shooter");
            EID returned=INVALID_EID;for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].combatActor==shooter){returned=id;w.ents[id].nativeIdentity.sleeping=true;}
            save("delayed-projectile",90);
            for(int i=0;i<90 && w.valid(target);++i)w.update();
            check(!w.valid(target) && w.valid(returned) && w.ents[returned].kills==1,"delayed impact awards the actual unloaded shooter after carrier death");
        }
    }
    for(const char* name:{"ra2-three-beams-world59.sav","yr-three-beams-world59.sav","yr-two-cleg-bf-world59.sav"}) {
        std::ifstream input(std::string("tests/fixtures/native-crew-v59/")+name,std::ios::binary);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        World old;const bool loaded=crewLoad(bytes,old);check(loaded && !old.nativeCrewRules.enabled,std::string("actual World59 fixture retains old crew contract: ")+name);
        if(loaded){
            auto reused=world;reused.map.bldOccRef=&reused.bldOcc;
            check(crewLoad(bytes,reused) && reused.checksum()==old.checksum() && reused.validateNativeCrewState(),std::string("reused new world clears crew identities when loading old fixture: ")+name);
            const auto upgraded=crewBytes(old);
            auto normalized=upgraded;const auto extension=locateWorldSaveSection(normalized,WorldSaveSection::NativeCrew);
            if(extension){normalized.resize(extension->offset);normalized[6]='5';normalized[7]='9';}
            check(extension && normalized==bytes,std::string("legacy payload stays byte-identical after removing only disabled CREW suffix: ")+name);
            World fresh;bool exact=crewLoad(upgraded,fresh) && old.checksum()==fresh.checksum();
            for(int i=0;i<60 && exact;++i){old.update();fresh.update();exact=old.checksum()==fresh.checksum();}check(exact,std::string("old world migrates and resumes exactly: ")+name);}
    }
    return failures+nativeCrewWeaponRegression(outputDir);
}
