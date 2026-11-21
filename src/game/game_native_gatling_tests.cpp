#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>

int GameRegression::nativeGatlingRegression(const char* outputDir) {
    g_script.shutdown();std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_GATLING %s %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    for(bool building:{false,true}){
        World w;const bool opened=w.init(64,64,1991,1,1,{Faction::Yuri,Faction::Soviet},0,nullptr,false,false,"assets/rules/profiles/yr.ini");
        w.ents.clear();w.freeList.clear();std::fill(w.bldOcc.begin(),w.bldOcc.end(),0);
        for(auto& c:w.map.cells){c.height=0;c.ore=c.oreMax=0;c.blocked=false;c.overlay=Overlay::None;c.terrain=Terrain::Clear;}
        for(auto& p:w.players){p.isAI=false;p.money=100000;p.defeated=false;}
        for(auto& fog:w.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);w.cratesEnabled=false;w.rebuildUnitOcc();w.rebuildAirOcc();
        w.spawnBuilding(0,BldType::BioReactor,4,4,true);w.spawnBuilding(0,BldType::BioReactor,8,4,true);
        w.spawnBuilding(0,BldType::ConYard,24,10,true);
        const EID actor=building?w.spawnBuilding(0,BldType::GatlingCannon,12,12,true):w.spawnUnit(0,UnitType::GatlingTank,12.5f,12.5f);
        const EID air=w.spawnUnit(1,UnitType::Kirov,19.5f,12.5f),ground=w.spawnUnit(1,UnitType::Engineer,16.5f,18.5f);
        if(w.valid(air)){w.ents[air].guard=false;w.ents[air].hp=100000;}if(w.valid(ground)){w.ents[ground].guard=false;w.ents[ground].hp=100000;}
        const bool staged=opened && w.valid(actor) && w.hasNativeGatling(w.ents[actor]);
        std::printf("NATIVE_GATLING CASE building=%d actor=%d\n",building,actor);
        check(staged,"the source gatling actor binds all six normal and six elite weapon entries instead of an empty Primary slot");if(!staged)continue;
        const auto& p=w.ents[actor].nativeIdentity.definition->guns;
        check(p.rateUp==1 && p.rateDown==50 && p.thresholds[0]==std::array<int,3>{200,400,600}
            && p.thresholds[1]==std::array<int,3>{100,200,300},"source gatling rate and normal/elite stage thresholds retain the original fields");
        for(int frame=0;frame<160;++frame)w.update();
        check(w.valid(actor) && w.valid(air) && w.ents[air].airborne(),"the air-target fixture takes off through its real source flight controller");
        bool selected=true,refs=true;
        for(int rank:{0,2})for(int stage=0;stage<3;++stage)for(bool airborne:{false,true}){
            auto& e=w.ents[actor];e.vetRank=rank;e.gatlingStage=stage;const int slot=stage*4+(rank==2?2:0)+(airborne?1:0);
            const auto weapon=w.effWeapon(e,&w.ents[airborne?air:ground]);
            selected &= weapon.nativeSlot==slot && weapon.damage>0 && (airborne?weapon.antiAir:weapon.antiGround);
            const int code=w.nativeWeaponReference(weapon);auto restored=weapon;restored.nativeVerses=nullptr;
            refs &= code>=0 && w.restoreNativeWeaponReference(restored,code,true) && restored.nativeType==weapon.nativeType
                && restored.nativeSlot==slot && restored.nativeVerses==weapon.nativeVerses;
        }
        check(selected,"all three stages select distinct normal and elite AG/AA slots for actual ground and airborne targets");
        check(refs,"all twelve gun references round-trip, including references beyond the historical four-slot encoding");
        auto& e=w.ents[actor];e.vetRank=0;e.gatlingHeat=e.gatlingStage=0;e.cancelBurst();e.atkCd=0;
        if(!building)w.orderAttack({actor},air);
        const int hp=w.ents[air].hp;
        for(int frame=0;frame<900;++frame){w.update();w.presentationEvents.clear();w.evaQueue.clear();}
        check(w.ents[air].hp<hp && w.ents[actor].gatlingStage==2 && w.ents[actor].gatlingHeat>=400,
            "actual air combat deals source weapon damage and advances through the native heat stages");
        bool burst=false;
        for(int frame=0;frame<120 && !burst;++frame){w.update();const auto& a=w.ents[actor];burst=a.burstRemaining>0 && w.valid(a.target) && w.effWeapon(a,&w.ents[a.target]).nativeSlot>=4;}
        check(burst,"actual staged combat leaves a pending source burst whose gun index exceeds three");
        const std::string name=building?"cannon":"tank";const auto path=(std::filesystem::path(outputDir)/(name+"-world56.sav")).string();
        FILE* file=std::fopen(path.c_str(),"wb");bool same=file && w.saveGame(file);if(file)std::fclose(file);World copy;
        file=std::fopen(path.c_str(),"rb");same &= file && copy.loadGame(file);if(file)std::fclose(file);
        same &= w.checksum()==copy.checksum();
        for(int frame=0;frame<180 && same;++frame){w.update();copy.update();same &= w.checksum()==copy.checksum();}
        check(same,"a real World55 snapshot restores the selected high-stage guns, heat and pending bursts exactly");
        Game saved;saved.world=w;saved.world.map.bldOccRef=&saved.world.bldOcc;saved.cfgRules=RulesChoice::YR;saved.cfgAI=1;
        saved.cfgCountry=(int)Country::COUNT;saved.cfgMapSize=64;saved.cfgMapType=0;saved.cfgMoney=100000;saved.phase=Phase::InGame;
        const auto app=(std::filesystem::path(outputDir)/(name+"-app11.sav")).string();Game resumed;
        same=saved.saveGameFile(app.c_str()) && resumed.loadGameFile(app.c_str()) && resumed.world.checksum()==w.checksum();
        for(int frame=0;frame<180 && same;++frame){w.update();resumed.world.update();same &= w.checksum()==resumed.world.checksum();}
        check(same,"a real App11 checkpoint resumes the complete source gatling actor and its combat");
        World captured=w;captured.map.bldOccRef=&captured.bldOcc;captured.ents[air].player=0;captured.projs.clear();
        const int friendly=captured.ents[air].hp;for(int frame=0;frame<90;++frame)captured.update();
        check(captured.ents[air].hp==friendly && captured.ents[actor].target!=air,
            "automatic gatling fire and a pending burst stop targeting an aircraft after it changes to the gun owner's side");
        auto& cooled=w.ents[actor];cooled.target=INVALID_EID;cooled.state=UState::Idle;
        w.ents[air].x=54.5f;w.ents[air].y=54.5f;w.ents[ground].x=54.5f;w.ents[ground].y=50.5f;w.rebuildUnitOcc();w.rebuildAirOcc();
        for(int frame=0;frame<40;++frame)w.update();
        check(w.ents[actor].gatlingHeat==0 && w.ents[actor].gatlingStage==0,"leaving combat cools the source gun to its first stage using the imported rate");
    }
    return failures;
}
