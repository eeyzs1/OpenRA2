#include "game_regression.h"
#include "game/world_priv.h"
#include "game/script.h"
#include "core/content.h"
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <limits>

int GameRegression::splashRegression() {
    int failures=0;
    auto check=[&](bool ok,const char* label){failures+=!ok;std::printf("SPLASH %s %s\n",ok?"PASS":"FAIL",label);};
    auto reset=[](World& w) {
        w.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"",true);
        for(size_t id=0;id<w.ents.size();++id)if(w.ents[id].alive)w.kill((EID)id,false);
        for(auto& cell:w.map.cells)cell=Cell{};
        w.crates.clear();w.cratesEnabled=false;w.aiAlliance=false;
        for(auto& p:w.players){p.defeated=false;p.isAI=false;}
        w.map.reveal(0,32,32,64);w.map.reveal(1,32,32,64);
    };
    auto resolved=[](WeaponDef weapon) {
        const auto& area=g_gameRules.warheadArea[(int)weapon.warhead];
        weapon.cellSpread=area.cellSpread;weapon.percentAtMax=area.percentAtMax;return weapon;
    };
    const auto& definition=unitDef(UnitType::Apocalypse);
    check(g_gameRules.warheadArea[(int)WeaponDef::Warhead::ApocAP].cellSpread==.3f
        && g_gameRules.warheadArea[(int)WeaponDef::Warhead::MammothHE].cellSpread==.5f,"calibrated warhead radii load from rules");
    const int expected[3][4]={{25,18,12,0},{100,75,50,0},{35,25,17,0}};
    for(int mode=0;mode<3;++mode)for(int sample=0;sample<4;++sample) {
        World w;reset(w);
        const auto weapon=resolved(mode==0?definition.weapon:mode==1?*definition.elite:definition.airWeapon);
        const int radius=mode==2?128:76;
        const int offset=sample==0?0:sample==1?radius/2:sample==2?radius:radius+1;
        const EID target=w.spawnUnit(1,mode==2?UnitType::Intruder:UnitType::GI,10.5f+offset/256.f,10.5f);
        const int hp=w.ents[target].hp;
        w.applyCellSpreadDamage(weapon,0,10.5f,10.5f,INVALID_EID);
        check(hp-w.ents[target].hp==expected[mode][sample],"center half-radius edge and outside damage use lepton distance");
        std::printf("SPLASH_DISTANCE mode=%d sample=%d leptons=%d damage=%d expected=%d\n",mode,sample,offset,hp-w.ents[target].hp,expected[mode][sample]);
    }
    World w;reset(w);auto ground=resolved(definition.weapon),air=resolved(definition.airWeapon);
    EID infantry=w.spawnUnit(1,UnitType::GI,10.5f,10.5f),plane=w.spawnUnit(1,UnitType::Intruder,10.5f,10.5f);
    const int infHp=w.ents[infantry].hp,airHp=w.ents[plane].hp;
    w.applyCellSpreadDamage(ground,0,10.5f,10.5f,INVALID_EID);
    check(w.ents[infantry].hp==infHp-25 && w.ents[plane].hp==airHp,"ground blast excludes airborne targets");
    w.applyCellSpreadDamage(air,0,10.5f,10.5f,INVALID_EID);
    check(w.ents[infantry].hp==infHp-25 && w.ents[plane].hp==airHp-35,"anti-air blast excludes ground targets");
    reset(w);EID friendUnit=w.spawnUnit(0,UnitType::GI,10.5f,10.5f);
    EID neutral=w.spawnUnit(-1,UnitType::GI,10.5f+76/256.f,10.5f);
    EID protectedUnit=w.spawnUnit(1,UnitType::GI,10.5f+38/256.f,10.5f);w.ents[protectedUnit].invuln=1;
    const int friendHp=w.ents[friendUnit].hp,neutralHp=w.ents[neutral].hp,protectedHp=w.ents[protectedUnit].hp;
    w.applyCellSpreadDamage(ground,0,10.5f,10.5f,INVALID_EID);
    check(w.ents[friendUnit].hp==friendHp-25 && w.ents[neutral].hp==neutralHp-12 && w.ents[protectedUnit].hp==protectedHp,
        "friendly neutral and invulnerable recipients are handled explicitly");
    reset(w);const EID source=w.spawnUnit(0,UnitType::Apocalypse,10.5f,10.5f);const int sourceHp=w.ents[source].hp;
    w.applyCellSpreadDamage(ground,0,10.5f,10.5f,source);
    check(w.ents[source].hp==sourceHp,"firer is excluded from its own calibrated blast");
    reset(w);EID host=w.spawnUnit(1,UnitType::MCV,10.5f,10.5f),parasite=w.spawnUnit(1,UnitType::TerrorDrone,10.5f,10.5f);
    w.ents[host].parasite=parasite;w.ents[parasite].parasiteHost=host;w.ents[parasite].parasiting=true;w.unitOccSync(parasite);
    int parasiteHp=w.ents[parasite].hp;w.applyCellSpreadDamage(ground,0,10.5f,10.5f,INVALID_EID);
    check(w.ents[parasite].hp==parasiteHp,"hidden attached terror drone is not a separate area recipient");
    reset(w);w.map.at(10,10).terrain=Terrain::Water;host=w.spawnUnit(1,UnitType::Destroyer,10.5f,10.5f);
    parasite=w.spawnUnit(1,UnitType::Squid,10.5f,10.5f);w.ents[host].parasite=parasite;
    w.ents[parasite].parasiteHost=host;w.ents[parasite].parasiting=true;w.unitOccSync(parasite);parasiteHp=w.ents[parasite].hp;
    w.applyCellSpreadDamage(ground,0,10.5f,10.5f,INVALID_EID);
    check(w.ents[parasite].hp==parasiteHp-75,"exposed attached squid remains vulnerable to area damage");
    reset(w);EID building=w.spawnBuilding(1,BldType::ConYard,10,10,true);int before=w.ents[building].hp;
    w.applyCellSpreadDamage(ground,0,10.125f,10.125f,INVALID_EID);
    check(before-w.ents[building].hp==70,"impact inside a foundation cell hits it without a second direct hit");
    WeaponDef area=ground;area.warhead=WeaponDef::Warhead::Legacy;area.vsBuilding=1;area.cellSpread=1;area.percentAtMax=.5f;
    before=w.ents[building].hp;w.applyCellSpreadDamage(area,0,9.75f,10.5f,INVALID_EID);
    check(before-w.ents[building].hp==62,"outside impact measures the nearby foundation instead of building center");
    area.cellSpread=11;area.percentAtMax=1;area.damage=10;
    before=w.ents[building].hp;w.applyCellSpreadDamage(area,0,11.5f,11.5f,INVALID_EID);
    check(before-w.ents[building].hp==bldDef(BldType::ConYard).w*bldDef(BldType::ConYard).h*10,
        "multiple affected foundation cells contribute once each");

    reset(w);const EID actor=w.spawnUnit(0,UnitType::Apocalypse,10.5f,20.5f);
    EID target=w.spawnUnit(1,UnitType::Conscript,14.5f,20.5f);
    EID neighbor=w.spawnUnit(1,UnitType::Conscript,14.5f+38/256.f,20.5f);
    w.ents[target].tpSick=w.ents[neighbor].tpSick=200;w.ents[actor].turretDir=dirFromVec(4,0);
    const int targetHp=w.ents[target].hp,neighborHp=w.ents[neighbor].hp;
    World replay=w;replay.map.bldOccRef=&replay.bldOcc;
    World::Cmd command{};command.type=World::Cmd::Attack;command.ids={actor};command.a=target;
    w.applyCmd(0,command);replay.applyCmd(0,command);bool equal=w.checksum()==replay.checksum(),saved=false,hashed=false,launched=false;
    auto& liveArea=g_gameRules.warheadArea[(int)WeaponDef::Warhead::ApocAP];const auto originalArea=liveArea;
    for(int frame=0;frame<60;++frame) {
        w.update();replay.update();equal &= w.checksum()==replay.checksum();
        if(frame==19 && !w.projs.empty()) {
            launched=w.projs.front().w.cellSpread==.3f && w.projs.front().w.percentAtMax==.5f;
            const auto hash=w.checksum();w.projs.front().w.percentAtMax=.6f;hashed=w.checksum()!=hash;w.projs.front().w.percentAtMax=.5f;
            FILE* file=std::tmpfile();
            if(file){saved=w.saveGame(file) && std::fseek(file,0,SEEK_SET)==0 && replay.loadGame(file)
                && hash==w.checksum() && hash==replay.checksum();std::fclose(file);}
            liveArea.cellSpread=0; // Only launched snapshots determine the two pending impacts.
        }
    }
    liveArea=originalArea;
    check(equal && saved && hashed && launched && targetHp-w.ents[target].hp==50 && neighborHp-w.ents[neighbor].hp==36,
        "commanded double-shell impacts replay through save with launch-time area snapshots");
    std::printf("SPLASH_FLIGHT target=%d neighbor=%d replay=%d saved=%d hashed=%d launched=%d\n",
        targetHp-w.ents[target].hp,neighborHp-w.ents[neighbor].hp,equal,saved,hashed,launched);
    reset(w);EID gun=w.spawnUnit(0,UnitType::Apocalypse,10.5f,20.5f);
    target=w.spawnUnit(1,UnitType::Conscript,14.5f,20.5f);w.ents[target].tpSick=200;w.ents[gun].turretDir=dirFromVec(4,0);
    const int missedHp=w.ents[target].hp;command.ids={gun};command.a=target;w.applyCmd(0,command);w.update();
    World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids={gun};w.applyCmd(0,stop);
    w.ents[target].x+=1;w.unitOccSync(target);
    for(int frame=0;frame<59;++frame)w.update();
    check(w.ents[target].hp==missedHp,"a target one cell away from the old shell landing point is missed");
    reset(w);gun=w.spawnUnit(0,UnitType::Apocalypse,10.5f,20.5f);
    target=w.spawnUnit(1,UnitType::Conscript,14.5f,20.5f);neighbor=w.spawnUnit(1,UnitType::Conscript,14.5f+38/256.f,20.5f);
    w.ents[target].tpSick=w.ents[neighbor].tpSick=200;w.ents[gun].turretDir=dirFromVec(4,0);
    auto& primary=const_cast<UnitDef&>(unitDef(UnitType::Apocalypse)).weapon;const auto originalPrimary=primary;
    primary.projectile="InvisibleLow";command.ids={gun};command.a=target;
    w.applyCmd(0,command);w.update();primary=originalPrimary;
    check(w.ents[target].hp==unitDef(UnitType::Conscript).hp-25 && w.ents[neighbor].hp==unitDef(UnitType::Conscript).hp-18,
        "instant projectile dispatch uses the same single area pass");
    for(int invalid=0;invalid<4;++invalid) {
        World bad=w;bad.map.bldOccRef=&bad.bldOcc;Projectile p{};p.player=0;p.x=p.tx=10.5f;p.y=p.ty=20.5f;p.w=ground;
        if(invalid==0)p.w.cellSpread=std::numeric_limits<float>::quiet_NaN();
        if(invalid==1)p.w.percentAtMax=std::numeric_limits<float>::quiet_NaN();
        if(invalid==2)p.w.cellSpread=12;
        if(invalid==3)p.w.percentAtMax=-1;
        bad.projs.push_back(p);World loaded;FILE* file=std::tmpfile();bool rejected=false;
        if(file){rejected=bad.saveGame(file) && std::fseek(file,0,SEEK_SET)==0 && !loaded.loadGame(file);std::fclose(file);}
        check(rejected,"snapshot rejects malformed flight area parameters");
    }
    std::filesystem::create_directories("review_out");exportRules("review_out/splash-roundtrip.ini");
    liveArea.cellSpread=-1;liveArea.percentAtMax=1;loadRules("review_out/splash-roundtrip.ini");
    check(liveArea.cellSpread==.3f && liveArea.percentAtMax==.5f,"warhead area survives rules export and reload");
    FILE* patch=std::fopen("review_out/splash-key-only.ini","wb");
    if(patch){std::fputs("[Warhead.ApocAP]\nCellSpread=0.75\nPercentAtMax=25%\n",patch);std::fclose(patch);loadRules("review_out/splash-key-only.ini");}
    check(patch && liveArea.cellSpread==.75f && liveArea.percentAtMax==.25f,"key-only warhead patches and percentage notation are accepted");
    patch=std::fopen("review_out/splash-invalid-rule.ini","wb");
    if(patch){std::fputs("[Warhead.ApocAP]\nCellSpread=nan\nPercentAtMax=inf\n",patch);std::fclose(patch);loadRules("review_out/splash-invalid-rule.ini");}
    check(patch && liveArea.cellSpread==.75f && liveArea.percentAtMax==.25f,"nonfinite rule values preserve the last valid area settings");
    liveArea=originalArea;

    reset(w);EID house=w.spawnBuilding(1,BldType::CivHouse,10,10,true);
    w.ents[house].hp=bldDef(BldType::CivHouse).hp/2+1;
    w.ents[house].garrison={{UnitType::GI,67,0,0,0},{UnitType::GI,68,0,0,0}};
    area.damage=100;area.cellSpread=11;area.percentAtMax=1;area.vsBuilding=1;
    w.applyCellSpreadDamage(area,0,10.5f,10.5f,INVALID_EID);
    int evacuated=0;bool crewHealthy=true;
    for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.utype==UnitType::GI && e.player==1) {
        ++evacuated;crewHealthy &= e.hp==67 || e.hp==68;
    }
    check(!w.valid(house) && evacuated==2 && crewHealthy,
        "foundation hits preserve mid-damage evacuation and do not hit the escaped crew again");
    std::printf("SPLASH_EVAC house_alive=%d crew=%d healthy=%d\n",w.valid(house),evacuated,crewHealthy);

    // Actual Lua destruction callbacks kill a later recipient and spawn a new
    // object in its place. Reuse must wait until the whole damage batch ends.
    reset(w);w.freeList.clear();EID first=w.spawnUnit(1,UnitType::GI,10.5f,10.5f),later=w.spawnUnit(1,UnitType::GI,11.5f,10.5f);
    w.ents[first].hp=w.ents[later].hp=1;
    const auto originalSlots=w.ents.size();
    const char* root="review_out/splash-callback";std::filesystem::create_directories(std::string(root)+"/assets/scripts");
    FILE* script=std::fopen("review_out/splash-callback/assets/scripts/zzzz_splash_callback.lua","wb");
    if(script){std::fputs("local ran=false\nlocal first=ra2.findUnits(1,'GI',10.5,10.5,0.1)[1]\nlocal later=ra2.findUnits(1,'GI',11.5,10.5,0.1)[1]\nfunction OnUnitKilled(id)\n if not ran and id==first then\n ran=true\n ra2.killEntity(later)\n ra2.spawnUnit(1,'GI',11.5,10.5)\n end\nend\n",script);std::fclose(script);}
    contentAddModPath(root,true);g_script.init(&w,0,-1);
    area.damage=100;area.cellSpread=1.5f;area.percentAtMax=1;area.vsInfantry=1;
    w.applyCellSpreadDamage(area,0,10.5f,10.5f,INVALID_EID);g_script.shutdown();
    check(script && !w.valid(first) && !w.valid(later) && w.ents.size()==originalSlots+1
        && w.ents.back().alive && w.ents.back().utype==UnitType::GI && w.ents.back().hp==unitDef(UnitType::GI).hp,
        "callback births are outside the old recipient set and cannot reuse a pending victim slot");
    FILE* file=std::tmpfile();World loaded;bool resumed=false;
    if(file){resumed=w.saveGame(file) && std::fseek(file,0,SEEK_SET)==0 && loaded.loadGame(file) && w.checksum()==loaded.checksum();std::fclose(file);}
    check(resumed,"callback quarantine drains before the next snapshot boundary");
    return failures;
}
