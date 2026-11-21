#include "game/game.h"
#include "game/skirmish_map.h"
#include "game/native_scene_contract.h"
#include "core/content.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <ctime>
#include <cstring>

namespace {
uint16_t defaultCountries(RulesChoice choice){
    uint16_t mask=0;for(int c=1;c<(int)Country::COUNT;++c)
        if(choice==RulesChoice::Fusion || (c!=(int)Country::China && (choice==RulesChoice::YR || c!=(int)Country::Yuri)))mask|=1<<c;
    return mask;
}
bool pngSize(const std::string& name,int& width,int& height){
    const auto path=contentResolve(name.c_str());if(path.empty())return false;
    std::ifstream f(path,std::ios::binary);unsigned char header[24]{},tail[12]{};
    if(!f.read((char*)header,24) || std::memcmp(header,"\x89PNG\r\n\x1a\n",8) || std::memcmp(header+12,"IHDR",4))return false;
    const auto number=[](const unsigned char* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];};
    const auto w=number(header+16),h=number(header+20);if(!w || !h || w>4096 || h>4096)return false;
    f.seekg(-12,std::ios::end);if(!f.read((char*)tail,12) || std::memcmp(tail,"\0\0\0\0IEND\xae\x42\x60\x82",12))return false;
    width=(int)w;height=(int)h;return true;
}
}

const char* Game::rulesChoiceName(RulesChoice choice) const {
    switch(choice){case RulesChoice::RA2:return TR(S::RulesRA2);case RulesChoice::YR:return TR(S::RulesYR);default:return TR(S::RulesFusion);}
}

bool Game::validateRulesSource(const World::RulesSnapshot& snapshot,RulesInfo& result,const Map* map) const {
    result={};result.checked=true;World parsed;parsed.numPlayers=2;parsed.players.resize(2);
    if(map){parsed.map=*map;parsed.map.bldOccRef=nullptr;}
    if(!parsed.restoreNativeDefinitions(snapshot.source) || !parsed.usesRulesProfile())return false;
    parsed.aircraftSpawnersEnabled=snapshot.fixedAircraft;parsed.aswEnabled=snapshot.asw;parsed.nativeFlightEnabled=snapshot.flight;parsed.nativeJumpjetEnabled=snapshot.jumpjet;
    parsed.veterancyProgressEnabled=snapshot.veterancy && parsed.nativeProductionRules.version>=4;
    parsed.independentSupportEnabled=snapshot.support && parsed.nativeProductionRules.version>=4;
    parsed.supportDeliveryEnabled=snapshot.delivery && snapshot.flight && snapshot.support && parsed.nativeProductionRules.version>=5;
    if(parsed.nativeProductionRules.version>=4)for(const auto& rule:parsed.nativeProductionRules.support)
        if(rule.available && contentResolve(rule.icon.c_str()).empty())return false;
    if(!parsed.validateAircraftState())return false;
    for(int c=1;c<(int)Country::COUNT;++c)if(parsed.rulesCountryAllowed((Country)c)){
        parsed.players[0].country=(Country)c;parsed.players[0].faction=countryFaction((Country)c);
        const int mcv=parsed.rulesTypeIndex(0,true,(int)UnitType::MCV);if(mcv<0)return false;
        const auto& transform=parsed.nativeTypes[mcv]->transformsTo;
        if(std::none_of(parsed.nativeTypes.begin(),parsed.nativeTypes.end(),[&](const auto& d){return d->id==transform && d->isBuilding && d->building.type==BldType::ConYard;}))return false;
    }
    const auto directory="assets/rules/profiles/"+parsed.rulesProfileId+"/actors/";
    for(const auto& d:parsed.nativeTypes)if(d->nativeBody || d->kind){
        const auto actorDirectory=d->rulesOnly?directory:"assets/maps/native/"+parsed.map.native.assetId+"/actors/";
        for(int frame=0;frame<d->artFrames;++frame){
            const auto name=actorDirectory+d->art+(d->isBuilding?"_":"_d")+std::to_string(frame)+".png";
            int width=0,height=0;if(!pngSize(name,width,height))return false;
            if(d->remappable){int mw=0,mh=0;
                if(!pngSize(name.substr(0,name.size()-4)+"_remap.png",mw,mh) || mw!=width || mh!=height)return false;
            }
        }
    }
    result.available=true;result.source=snapshot.source;result.id=parsed.rulesProfileId;result.countries=parsed.rulesCountryMask;return true;
}

bool Game::refreshRulesChoice(RulesChoice choice,bool force){
    if((unsigned)choice>=(unsigned)RulesChoice::COUNT)return false;
    auto& info=rulesInfo[(int)choice];if(info.checked && !force)return info.available;
    info={};info.checked=true;info.countries=defaultCountries(choice);
    if(choice==RulesChoice::Fusion){info.available=true;return true;}
    const char* name=choice==RulesChoice::RA2?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
    const auto path=contentResolve(name);if(path.empty())return false;
    std::ifstream f(path,std::ios::binary);if(!f)return false;
    const std::string source((std::istreambuf_iterator<char>(f)),{});RulesInfo staged;
    if(!validateRulesSource({source,true,true},staged))return false;
    if(!staged.id.starts_with(choice==RulesChoice::RA2?"ra2-":"yr-") || (staged.countries & ~defaultCountries(choice)))return false;
    info=std::move(staged);return true;
}

std::vector<int> Game::selectableCountries() const {
    if((unsigned)cfgRules>=(unsigned)RulesChoice::COUNT)return {};
    const auto& info=rulesInfo[(int)cfgRules];const auto mask=info.available?info.countries:defaultCountries(cfgRules);
    std::vector<int> countries;for(int c=1;c<(int)Country::COUNT;++c)if(mask&(1<<c))countries.push_back(c);
    countries.push_back((int)Country::COUNT);return countries;
}

void Game::selectRulesChoice(RulesChoice choice){
    if((unsigned)choice>=(unsigned)RulesChoice::COUNT)return;
    cfgRules=choice;refreshRulesChoice(choice,true);const auto countries=selectableCountries();
    if(choice==RulesChoice::Fusion)clearOriginalMap();
    if(countries.empty())return;
    const auto normalize=[&](int& c){if(std::find(countries.begin(),countries.end(),c)==countries.end())c=countries.front();};
    normalize(cfgCountry);for(int& c:aiCountry)normalize(c);
}

std::optional<Sha256Digest> Game::selectedRulesFingerprint(){
    const auto base=simulationRulesFingerprint();if(!base || !refreshRulesChoice(cfgRules,true))return std::nullopt;
    if(cfgRules==RulesChoice::Fusion)return base;
    Sha256 hash;hash.update("OpenRA2-source-rules-v1");hash.update(base->data(),base->size());const auto choice=(uint8_t)cfgRules;
    hash.update(&choice,1);hash.update(rulesInfo[(int)cfgRules].source);return hash.digest();
}

MatchSetup Game::currentMatchSetup(uint64_t seed) const {
    MatchSetup s;s.rules=cfgRules;s.seed=seed;s.country=cfgCountry;s.color=cfgColor;s.ai=cfgAI;s.money=cfgMoney;s.size=cfgMapSize;
    s.terrain=cfgMapType;s.mode=cfgGameMode;s.reference=cfgReferenceMap;s.originalMap=cfgOriginalMap;s.crates=cfgCrates;s.alliance=cfgAlliance;s.sharedVision=cfgSharedVision;
    s.shortGame=cfgShortGame;s.superweapons=cfgSuperweapons;s.repacks=cfgMcvRepacks;
    std::copy_n(aiColor,7,s.aiColors.begin());std::copy_n(aiCountry,7,s.aiCountries.begin());std::copy_n(aiDiff,7,s.aiDifficulty.begin());std::copy_n(aiPersonality,7,s.aiPersonality.begin());return s;
}

void Game::applyMatchSetup(const MatchSetup& s){
    cfgRules=s.rules;cfgCountry=s.country;cfgColor=s.color;cfgAI=s.ai;cfgMoney=s.money;cfgMapSize=s.size;cfgMapType=s.terrain;
    cfgGameMode=s.mode;cfgReferenceMap=s.reference;cfgOriginalMap=s.originalMap;cfgCrates=s.crates;cfgAlliance=s.alliance;cfgSharedVision=s.sharedVision;
    cfgShortGame=s.shortGame;cfgSuperweapons=s.superweapons;cfgMcvRepacks=s.repacks;previewSeed=s.seed;previewDirty=true;
    std::copy(s.aiColors.begin(),s.aiColors.end(),aiColor);std::copy(s.aiCountries.begin(),s.aiCountries.end(),aiCountry);
    std::copy(s.aiDifficulty.begin(),s.aiDifficulty.end(),aiDiff);std::copy(s.aiPersonality.begin(),s.aiPersonality.end(),aiPersonality);
}

void Game::deriveMatchSetup(){
    if(campaignMission>=0){matchSetup.reset();return;}
    MatchSetup s;s.rules=world.usesRulesProfile()?((world.rulesProfileId.starts_with("yr-") || world.rulesCountryAllowed(Country::Yuri))?RulesChoice::YR:RulesChoice::RA2):RulesChoice::Fusion;
    s.country=(int)world.players[localPlayer].country;if(s.country<=0)s.country=(int)countriesOf(world.players[localPlayer].faction).front();
    s.color=world.players[localPlayer].colorId;s.ai=std::clamp(world.numPlayers-1,0,7);s.size=std::clamp(world.map.w,32,256);
    s.mode=(int)world.skirmishMode;s.terrain=world.skirmishMode==SkirmishMode::NavalWar?1:0;s.seed=world.rng.s;
    if(world.nativeSceneVersion==nativeSkirmishMapSceneVersion)s.originalMap=world.map.native.assetId;
    s.crates=world.cratesEnabled;s.alliance=world.aiAlliance;s.sharedVision=world.sharedVision;s.shortGame=world.shortGame;s.superweapons=world.superweaponsEnabled;s.repacks=world.mcvRepacks;
    for(int i=0;i<s.ai;++i){s.aiCountries[i]=(int)world.players[i+1].country;if(!s.aiCountries[i])s.aiCountries[i]=(int)countriesOf(world.players[i+1].faction).front();s.aiColors[i]=world.players[i+1].colorId;}
    for(const auto& ai:ais)if(ai.player>0 && ai.player<=7){s.aiDifficulty[ai.player-1]=(int)ai.difficulty;s.aiPersonality[ai.player-1]=(int)ai.personality;}
    matchSetup=s;applyMatchSetup(s);
}

void Game::restartCurrentMatch(){
    if(campaignMission>=0){newCampaignGame(campaignMission);return;}
    if(!matchSetup)deriveMatchSetup();
    if(!matchSetup)return;
    const auto setup=*matchSetup;World::RulesSnapshot snapshot{world.nativeScenarioSource,world.aircraftSpawnersEnabled,world.aswEnabled,world.nativeFlightEnabled,world.nativeJumpjetEnabled,world.veterancyProgressEnabled,world.independentSupportEnabled,world.supportDeliveryEnabled};
    applyMatchSetup(setup);newGame(setup.seed,world.usesRulesProfile()?&snapshot:nullptr);
    if(phase==Phase::InGame && !world.usesRulesProfile()) {
        world.veterancyProgressEnabled=snapshot.veterancy;world.independentSupportEnabled=snapshot.support;world.supportDeliveryEnabled=snapshot.delivery;
    }
}
