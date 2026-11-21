#include "game/world.h"
#include "game/native_scene_contract.h"
#include "core/content.h"
#include <fstream>
#include <iterator>
#include <algorithm>

bool World::rulesCountryAllowed(Country country) const {
    const int value=(int)country;
    return value>0 && value<(int)Country::COUNT && (!usesRulesProfile() || (rulesCountryMask&(1<<value))!=0);
}

bool World::sourceProductionModeAllows(const NativeActorDefinition& d) const {
    if(d.isBuilding && !superweaponsEnabled && bldProvidesSW(d.building.type)!=SWType::COUNT)return false;
    if(nativeProductionRules.sourceModes)return true; // original per-type overlays were applied before binding actors
    if(skirmishMode==SkirmishMode::Megawealth && (d.isBuilding?d.building.type==BldType::OreRefinery:d.unit.canHarvet()))return false;
    if(skirmishMode==SkirmishMode::MeatGrinder && (d.isBuilding?
        d.building.type==BldType::AirForceCmd || d.building.type==BldType::NavalYard || bldProvidesSW(d.building.type)!=SWType::COUNT:
        d.unit.isAir() || d.unit.isNaval()))return false;
    return true;
}

std::string World::productionCountry(int player) const {
    if(player<0 || player>=numPlayers)return {};
    if(!usesRulesProfile())return player<(int)nativeProductionRules.countries.size()?nativeProductionRules.countries[player]:std::string{};
    Country country=players[player].country;
    if(country==Country::None)country=countriesOf(players[player].faction).front();
    return (int)country<(int)Country::COUNT?rulesCountryKeys[(int)country]:std::string{};
}

int World::rulesTypeIndex(int player,bool unit,int base) const {
    if(!usesRulesProfile())return -1;
    if(player>=numPlayers)return -1;
    const auto country=productionCountry(player);int best=-1,highest=-1000;
    for(int i=0;i<(int)nativeTypes.size();++i) {
        const auto& definition=*nativeTypes[i];
        if(definition.isBuilding==unit || (unit?(int)definition.unit.type:(int)definition.building.type)!=base)continue;
        const auto& profile=definition.production;int score=0;
        if(player<0){score=definition.isBuilding && definition.building.capturable?5:0;if(score>highest){highest=score;best=i;}continue;}
        for(const auto& owner:profile.owners)if(owner==country){score+=10;break;}
        if(!profile.required.empty() && std::find(profile.required.begin(),profile.required.end(),country)==profile.required.end())score-=100;
        if(std::find(profile.forbidden.begin(),profile.forbidden.end(),country)!=profile.forbidden.end())score-=100;
        if(definition.rulesFaction>=0)score+=definition.rulesFaction==(int)players[player].faction?20:-20;
        if(score>highest){highest=score;best=i;}
    }
    return best;
}

int World::productionSelector(int player,bool unit,int type) const {
    if(!usesRulesProfile() || nativeProductionType(type))return type;
    if(player<0 || player>=numPlayers || type<0 || type>=(unit?(int)UnitType::COUNT:(int)BldType::COUNT))return -1;
    const int index=rulesTypeIndex(player,unit,type);
    return index<0?-1:NATIVE_PRODUCTION_BASE+index;
}

int World::rulesTransformIndex(const Ent& actor) const {
    const auto& definition=actor.nativeIdentity.definition;
    if(!usesRulesProfile() || !definition || definition->transformsTo.empty())return -1;
    for(int i=0;i<(int)nativeTypes.size();++i)if(nativeTypes[i]->id==definition->transformsTo)return i;
    return -1;
}

double World::movementSpeedMultiplier(int player) const {
    if(player<0 || player>=numPlayers)return 1;
    if(!usesRulesProfile())return nativeMovementRules.houseSpeed[player];
    const auto country=players[player].country==Country::None?countriesOf(players[player].faction).front():players[player].country;
    return rulesCountrySpeed[(int)country];
}

bool World::loadRulesProfile(const char* path) {
    // Rules are immutable for the lifetime of a world. Validate before initial
    // actors are created; an invalid profile cannot alter a running match.
    if(!path || !*path || !ents.empty() || updateInProgress || entityMutationDepth>0)return false;
    const auto resolved=contentResolve(path);if(resolved.empty())return false;
    std::ifstream input(resolved,std::ios::binary);if(!input)return false;
    const std::string source((std::istreambuf_iterator<char>(input)),{});
    return loadRulesSnapshot({source,true,true});
}

bool World::loadRulesSnapshot(const RulesSnapshot& snapshot) {
    if(snapshot.source.empty() || !ents.empty() || updateInProgress || entityMutationDepth>0)return false;
    World staged=*this;staged.map.bldOccRef=&staged.bldOcc;
    if(!staged.restoreNativeDefinitions(snapshot.source) || !staged.usesRulesProfile())return false;
    // Opening a native ordinary map requires its map-bound contract. Historical
    // definition restoration may also carry native query geometry and keeps it.
    if(staged.map.native.active() && staged.nativeSceneVersion!=nativeSkirmishMapSceneVersion)return false;
    staged.aircraftSpawnersEnabled=snapshot.fixedAircraft;staged.aswEnabled=snapshot.asw;staged.nativeFlightEnabled=snapshot.flight;staged.nativeJumpjetEnabled=snapshot.jumpjet;
    staged.veterancyProgressEnabled=snapshot.veterancy && staged.nativeProductionRules.version>=4;
    staged.independentSupportEnabled=snapshot.support && staged.nativeProductionRules.version>=4;
    staged.supportDeliveryEnabled=snapshot.delivery && snapshot.flight && snapshot.support && staged.nativeProductionRules.version>=5;
    if(!staged.validateAircraftState())return false;
    for(int player=0;player<numPlayers;++player) {
        const auto country=players[player].country==Country::None?countriesOf(players[player].faction).front():players[player].country;
        if(!staged.rulesCountryAllowed(country) || staged.rulesTypeIndex(player,true,(int)UnitType::MCV)<0)return false;
    }
    *this=std::move(staged);map.bldOccRef=&bldOcc;return true;
}
