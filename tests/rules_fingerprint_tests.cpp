#include "game/data.h"
#include "core/content.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>

int main(int argc,char** argv) {
    contentInit({});loadRulesFromContent();loadProjectilesFromContent();
    int failures=0;const auto check=[&](bool ok,const char* s){std::printf("RULES_HASH %s %s\n",ok?"PASS":"FAIL",s);failures+=!ok;};
    const auto baseline=simulationRulesFingerprint();check(baseline.has_value(),"installed effective rules have a finite canonical digest");if(!baseline)return 1;
    const auto changed=[&](){const auto value=simulationRulesFingerprint();return value && value!=baseline;};
    auto& yard=const_cast<BldDef&>(bldDef(BldType::ConYard));const auto yardSaved=yard;
    ++yard.hp;check(changed(),"a single construction-yard HP override changes compatibility");yard=yardSaved;
    auto& tank=const_cast<UnitDef&>(unitDef(UnitType::Apocalypse));const auto tankSaved=tank;
    ++tank.airWeapon.damage;check(changed(),"a later-built anti-air weapon changes compatibility");tank=tankSaved;
    if(tank.elite){auto elite=*tank.elite;tank.elite=&elite;
        check(simulationRulesFingerprint()==baseline,"equivalent elite weapon values at a different address remain compatible");
        ++elite.damage;check(changed(),"an elite-only weapon override is included");tank=tankSaved;}
    ++tank.cargoSizeLimit;check(changed(),"transport admission changes compatibility");tank=tankSaved;
    tank.name="different display name";tank.weapon.report="different audio report";
    check(simulationRulesFingerprint()==baseline,"unit display names and sound reports do not change simulation compatibility");tank=tankSaved;
    const auto savedRules=g_gameRules;
    ++g_gameRules.carrierRegenerateTicks;check(changed(),"carrier loss regeneration changes compatibility");g_gameRules=savedRules;
    ++g_gameRules.carrierReloadTicks;check(changed(),"carrier reloading changes compatibility");g_gameRules=savedRules;
    ++g_gameRules.carrierLaunchDelayTicks;check(changed(),"carrier launch cadence changes compatibility");g_gameRules=savedRules;
    ++g_gameRules.destroyerRegenerateTicks;check(changed(),"ASW loss regeneration changes compatibility");g_gameRules=savedRules;
    ++g_gameRules.destroyerReloadTicks;check(changed(),"ASW reloading changes compatibility");g_gameRules=savedRules;
    ++g_gameRules.destroyerLaunchDelayTicks;check(changed(),"ASW launch cadence changes compatibility");g_gameRules=savedRules;
    g_gameRules.lowPowerSpeedFactor*=.5f;check(changed(),"a future low-power production change is included");g_gameRules=savedRules;
    ++g_gameRules.oreRegrowRate;check(changed(),"ore regrowth changes compatibility");g_gameRules=savedRules;
    g_gameRules.warheadArea[(int)WeaponDef::Warhead::AP].cellSpread=.75f;check(changed(),"warhead area behavior changes compatibility");g_gameRules=savedRules;
    g_gameRules.veteranRatio=std::numeric_limits<float>::quiet_NaN();check(!simulationRulesFingerprint(),"nonfinite rules are rejected before agreement");g_gameRules=savedRules;
    auto& basic=const_cast<UnitDef&>(unitDef(UnitType::Grizzly));const auto basicSaved=basic;
    basic.weapon.splash=0;const auto positiveZero=simulationRulesFingerprint();basic.weapon.splash=-0.f;
    check(simulationRulesFingerprint()==positiveZero,"equivalent signed zero parses canonically");basic=basicSaved;
    const auto snapshot=projectileRulesSnapshot();const auto dir=std::filesystem::path(argc>1?argv[1]:"review_out/rules-fingerprint");std::filesystem::create_directories(dir);
    const auto write=[&](const char* name,bool reverse,bool mutate){const auto file=dir/name;std::ofstream out(file);
        out<<"; Formatting and Image are presentation-only.\n";
        for(size_t j=0;j<snapshot.definitions.size();++j){const size_t i=reverse?snapshot.definitions.size()-1-j:j;
            const auto& [key,p]=snapshot.definitions[i];out<<"[Projectile."<<key<<"]\nImage=another-image\nArcing="<<p.arcing<<"\nInviso="<<p.inviso
                <<"\nVertical="<<p.vertical<<"\nInaccurate="<<(mutate && !i?!p.inaccurate:p.inaccurate)<<"\nProximity="<<p.proximity
                <<"\nROT="<<p.rot<<"\nAA="<<p.aa<<"\nAG="<<p.ag<<"\nSpeedLeptons="<<p.speedLeptons<<"\n";}
        for(size_t j=0;j<snapshot.bridges.size();++j){const auto& [key,value]=snapshot.bridges[reverse?snapshot.bridges.size()-1-j:j];out<<"[ProjectileBridge."<<key<<"]\nProjectile="<<value<<"\n";}
        out.close();return file.string();};
    const auto equivalent=write("reordered.ini",true,false);loadProjectiles(equivalent.c_str());
    check(simulationRulesFingerprint()==baseline,"reordered parsed projectile tables and changed images remain compatible");
    const auto modified=write("different-projectile.ini",false,true);loadProjectiles(modified.c_str());
    check(changed(),"a projectile-only behavior change is detected independently of initial entity state");loadProjectilesFromContent();
    check(simulationRulesFingerprint()==baseline,"restoring the installed rules restores the exact digest");
    basic.weapon.projectile="Psychic";const auto psychic=simulationRulesFingerprint();
    basic.weapon.projectile="Invisible";const auto invisible=simulationRulesFingerprint();basic=basicSaved;
    check(psychic && invisible && psychic!=invisible,
        "selected projectile type identifiers distinguish stock numerically identical profiles with different effect/burst dispatch");
    std::printf("RULES_HASH digest=%s failures=%d\n",digestHex(*baseline).c_str(),failures);return failures;
}
