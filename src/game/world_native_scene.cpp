#include "game/world.h"
#include "game/native_scene_contract.h"
#include "core/binary_state_io.h"
#include "core/ini.h"
#include "core/content.h"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>
#include <cstring>
#include <limits>
#include <set>
#include <cctype>

namespace {
std::string lower(std::string value){for(char& c:value)c=(char)std::tolower((unsigned char)c);return value;}
bool parseScene(const std::string& source,Ini& ini) {
    if(source.empty() || source.size()>4*1024*1024 || source.find('\0')!=std::string::npos)return false;
    std::istringstream input(source);std::string line;Ini::Section* current=nullptr;
    auto trim=[](std::string s){const auto a=s.find_first_not_of(" \t\r");return a==std::string::npos?std::string{}:s.substr(a,s.find_last_not_of(" \t\r")-a+1);};
    while(std::getline(input,line)) {
        line=trim(line);if(line.empty() || line[0]==';' || line[0]=='#')continue;
        if(line.front()=='[' && line.back()==']') {
            const std::string name=line.substr(1,line.size()-2);if(ini.find(name.c_str()))return false;
            ini.sections.push_back({name,{}});current=&ini.sections.back();
        } else {
            const size_t equal=line.find('=');if(!current || equal==std::string::npos)return false;
            auto key=trim(line.substr(0,equal)),value=trim(line.substr(equal+1));
            if(key.empty() || current->get(key.c_str()))return false;current->kv.emplace_back(std::move(key),std::move(value));
        }
    }
    return true;
}
int integer(const Ini::Section* s,const char* key,int low,int high,bool& ok,int fallback=std::numeric_limits<int>::min()) {
    const char* text=s?s->get(key):nullptr;
    if(!text){if(fallback==std::numeric_limits<int>::min()){ok=false;return 0;}return fallback;}
    int value=0;const auto result=std::from_chars(text,text+strlen(text),value);
    if(result.ec!=std::errc{} || *result.ptr || value<low || value>high){ok=false;return fallback;}return value;
}
std::string value(const Ini::Section* s,const char* key) {const char* v=s?s->get(key):nullptr;return v?v:"";}
bool idValid(const std::string& id) {
    return !id.empty() && id.size()<=64 && std::all_of(id.begin(),id.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';});
}
struct RulesMapActor {int type,x,y,health,facing;std::string key,tag,mission;};
bool rulesMapActors(const Ini& ini,const Map& map,int players,
    const std::vector<std::shared_ptr<const NativeActorDefinition>>& definitions,
    std::vector<Vec2i>& starts,std::vector<RulesMapActor>& actors) {
    bool ok=true;const auto* scene=ini.find("Scene");
    const int count=integer(scene,"MapStarts",players,MAX_PLAYERS,ok),placed=integer(scene,"MapActors",0,65536,ok);
    std::set<std::pair<int,int>> locations;std::set<std::string> keys;
    for(int i=0;i<count && ok;++i){const auto* row=ini.find(("MapStart."+std::to_string(i)).c_str());
        Vec2i p{integer(row,"X",0,map.w-1,ok),integer(row,"Y",0,map.h-1,ok)};
        if(!ok || !map.passable(p.x,p.y) || !locations.emplace(p.x,p.y).second)return false;starts.push_back(p);}
    for(int i=0;i<placed && ok;++i){const auto* row=ini.find(("MapActor."+std::to_string(i)).c_str());RulesMapActor a;
        a.type=integer(row,"Type",0,(int)definitions.size()-1,ok);a.x=integer(row,"X",0,map.w-1,ok);a.y=integer(row,"Y",0,map.h-1,ok);
        a.health=integer(row,"Health",1,256,ok);a.facing=integer(row,"Facing",0,255,ok);
        a.key=value(row,"Key");a.tag=value(row,"Tag");a.mission=value(row,"Mission");
        if(!ok || a.key.empty() || a.key.size()>128 || a.tag.size()>128 || a.mission.size()>128 || !keys.insert(a.key).second)return false;
        const auto& d=*definitions[a.type];if(d.kind<1 || d.kind>2 || !d.isBuilding || d.selectable || d.significant)return false;
        for(int y=0;y<d.building.h;++y)for(int x=0;x<d.building.w;++x)if(!map.inBounds(a.x+x,a.y+y))return false;
        actors.push_back(std::move(a));
    }
    return ok;
}
}

bool World::restoreNativeDefinitions(const std::string& source) {
    Ini ini;if(!parseScene(source,ini))return false;const auto* scene=ini.find("Scene");bool ok=scene!=nullptr;
    if(!scene)return false;
    const int declaredVersion=integer(scene,"Version",1,maximumNativeSceneVersion,ok);
    const bool rulesMap=declaredVersion==nativeSkirmishMapSceneVersion;
    const int rulesBase=rulesMap?integer(scene,"RulesBaseVersion",nativeSkirmishRulesSemantics,nativeTravelSceneVersion,ok,nativeSkirmishRulesSemantics):declaredVersion;
    if((rulesMap && !nativeOrdinaryRulesScene(rulesBase))
        || (!rulesMap && scene->get("RulesBaseVersion")))return false;
    const int version=nativeOrdinaryRulesScene(rulesBase)?nativeSkirmishRulesSemantics:rulesBase;
    if(version>=35) {if(integer(scene,"TemporalVersion",1,1,ok)!=1)return false;}
    else if(scene && scene->get("TemporalVersion"))return false;
    const int crewVersion=version>=37?2:version>=36?1:0;
    if(version>=36) {if(integer(scene,"CrewVersion",crewVersion,crewVersion,ok)!=crewVersion)return false;}
    else if(scene && scene->get("CrewVersion"))return false;
    NativeCrewRules crewRules;if(!parseNativeCrewRules(ini,version>=36,crewRules) || crewRules.version!=crewVersion)return false;
    const bool rulesOnly=version>=17 && value(scene,"Mode")=="Rules";
    struct RulesDiagnostic {
        bool enabled,done=false;std::string stage="header";
        ~RulesDiagnostic(){if(enabled && !done)std::fprintf(stderr,"Rules profile rejected at %s\n",stage.c_str());}
    } diagnostic{rulesOnly};
    if(version<1 || (version>=17 && !rulesOnly))return false;
    if(rulesMap) {
        const auto hash=value(scene,"MapAssetSHA256");
        if(!map.native.active() || value(scene,"Map")!=map.native.assetId || value(scene,"MapSourceSHA256")!=map.native.sourceHash
            || hash.size()!=64 || !std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))return false;
    } else if(rulesOnly && (scene->get("MapStarts") || scene->get("MapActors") || scene->get("MapAssetSHA256") || scene->get("MapSourceSHA256")))return false;
    if(!rulesOnly && (value(scene,"Map")!=map.native.assetId || value(scene,"SourceSHA256")!=map.native.sourceHash))return false;
    const int count=integer(scene,"Types",1,512,ok),houses=rulesOnly?numPlayers:integer(scene,"Houses",1,MAX_PLAYERS,ok);
    const int ruleTypes=rulesMap?integer(scene,"MapRuleTypes",1,count-1,ok):count;
    if(rulesMap && skirmishMode!=SkirmishMode::Battle && skirmishMode!=SkirmishMode::FreeForAll)return false;
    std::string profileId,profileName;uint16_t countryMask=0;
    std::array<std::string,(int)Country::COUNT> countryKeys;
    std::array<double,(int)Country::COUNT> countrySpeed;countrySpeed.fill(1);
    if(rulesOnly) {
        profileId=value(scene,"RulesId");profileName=value(scene,"RulesName");const auto hash=value(scene,"SourceSHA256");
        if(profileId.empty() || profileId.size()>64 || !std::all_of(profileId.begin(),profileId.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-';})
            || profileName.empty() || profileName.size()>256 || (!rulesMap && !value(scene,"Map").empty())
            || hash.size()!=64 || !std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})
            || integer(scene,"Houses",0,0,ok)!=0 || integer(scene,"Actors",0,0,ok)!=0)return false;
        countryMask=(uint16_t)integer(scene,"CountryMask",1,(1<<(int)Country::COUNT)-1,ok);
        const auto* keys=ini.find("Rules.Countries");const auto* speeds=ini.find("Rules.CountrySpeed");
        for(int c=1;c<(int)Country::COUNT;++c)if(countryMask&(1<<c)) {
            countryKeys[c]=value(keys,std::to_string(c).c_str());float speed=1;
            if(!idValid(countryKeys[c]) || !nativeSourceNumber(speeds?speeds->get(std::to_string(c).c_str()):nullptr,speed,0,100))return false;
            countrySpeed[c]=speed;for(char& ch:countryKeys[c])if(ch>='a'&&ch<='z')ch-=32;
        }
    }
    if(!ok || houses!=numPlayers)return false;
    std::vector<std::shared_ptr<const NativeActorDefinition>> definitions;uint8_t allies[MAX_PLAYERS]{};uint32_t colors[MAX_PLAYERS]{};
    std::vector<NativeRocketDefinition> rockets;
    std::vector<NativeAircraftDefinition> aircraft;
    std::shared_ptr<const NativeWeaponCatalog> weapons;
    if(!parseNativeWeaponCatalog(ini,version>=32,weapons))return false;
    if(weapons && weapons->version!=(version>=34?3:version>=33?2:1))return false;
    if(!parseNativeAircraftDefinitions(ini,count,aircraft) || (version<19 && !aircraft.empty()))return false;
    if(version==19 && !aircraft.empty() && value(ini.find("NativeAircraft"),"Version")!="1")return false;
    NativeProductionRules production;
    NativeMovementRules movement;
    diagnostic.stage="movement rules";
    if(!parseNativeMovementRules(ini,houses,version>=16,movement))return false;
    diagnostic.stage="production rules";
    if(!parseNativeProductionRules(ini,houses,production) || (version>=11 && !production.available))return false;
    const int ordinaryProduction=rulesBase==nativeTravelSceneVersion?9:rulesBase==nativeGroundSceneVersion?8
        :rulesBase==nativeHarvestSceneVersion?7:rulesBase==nativeEconomySceneVersion?6:0;
    if(ordinaryProduction?production.version!=ordinaryProduction:production.version>=6)return false;
    if(rulesOnly && (version>=27)!=production.economy)return false;
    if(rulesOnly && (version>=29)!=(production.version>=3))return false;
    if(rulesOnly && (version>=30)!=(production.version>=4))return false;
    if(rulesOnly && (version>=31)!=(production.version>=5))return false;
    static_assert((int)SkirmishMode::COUNT==7);
    if(version>=24 && !prepareNativeSourceMode(ini,ruleTypes,(int)skirmishMode,production))return false;
    if(version<24 && ini.find("Rules.Modes"))return false;
    diagnostic.stage="rocket rules";
    if(!parseNativeRocketDefinitions(ini,count,rockets) || (version>=6 && version<8 && rockets.empty()))return false;
    for(int i=0;i<count && ok;++i) {
        const auto* s=ini.find(("Type."+std::to_string(i)).c_str());if(!s)return false;
        auto d=std::make_shared<NativeActorDefinition>();d->rulesOnly=rulesOnly;d->id=value(s,"Id");d->name=value(s,"Name");d->art=value(s,"Art");
        diagnostic.stage="type "+std::to_string(i)+" ("+d->id+")";
        if(!idValid(d->id) || !idValid(d->art) || d->name.empty() || d->name.size()>256)return false;
        for(const auto& previous:definitions)if(previous->id==d->id)return false;
        d->isBuilding=integer(s,"Building",0,1,ok)!=0;d->selectable=integer(s,"Selectable",0,1,ok,1)!=0;
        if(rulesOnly){d->transformsTo=value(s,"TransformsTo");if(!d->transformsTo.empty() && !idValid(d->transformsTo))return false;
            const int side=integer(s,"OriginalPlanningSide",-1,2,ok,-1);d->rulesFaction=side==2?(int)Faction::Yuri:side;}
        d->significant=integer(s,"Significant",0,1,ok,1)!=0;d->blocksMovement=integer(s,"Blocks",0,1,ok,1)!=0;
        d->remappable=integer(s,"Remappable",0,1,ok,0)!=0;
        d->nativeBody=integer(s,"NativeBody",0,1,ok,value(s,"Base")=="NativeVehicle"?1:0)!=0;
        d->nativeRocket=integer(s,"NativeRocket",0,1,ok,0)!=0;
        d->canPassiveAcquire=integer(s,"CanPassiveAcquire",0,1,ok,1)!=0;
        if(version>=23 && d->isBuilding)d->airObstacleHeight=integer(s,"AirObstacleHeight",0,100000,ok);
        if(version>=20){d->sourceNavalTargeting=true;
            d->underwater=integer(s,"Underwater",0,1,ok)!=0;
            d->navalTargeting=integer(s,"NavalTargeting",0,7,ok);
            d->landTargeting=integer(s,"LandTargeting",0,2,ok);
        }
        d->kind=integer(s,"Kind",0,3,ok,0);d->immune=integer(s,"Immune",0,1,ok,d->id=="CABHUT"?1:0)!=0;
        if(rulesMap && (i<ruleTypes)!=(d->kind==0))return false;
        if(version>=35 && d->kind==0)d->warpable=integer(s,"Warpable",0,1,ok)!=0;
        if(!parseNativeCrewProfile(s,d->kind==0?crewVersion:0,weapons.get(),d->crew)
            || (d->isBuilding && (d->crew.openTopped || d->crew.occupier)))return false;
        if(!parseNativeIFV(s,version>=34 && d->kind==0,weapons.get(),d->ifv) || (d->isBuilding && d->ifv.gunner))return false;
        if(version>=34) {
            d->vehicleRepairable=integer(s,"VehicleRepairable",0,1,ok)!=0;
            d->immuneToPsionicWeapons=integer(s,"ImmuneToPsionicWeapons",0,1,ok)!=0;
            d->immuneToRadiation=integer(s,"ImmuneToRadiation",0,1,ok)!=0;
            d->immuneToPoison=integer(s,"ImmuneToPoison",0,1,ok)!=0;
        }
        if(rulesOnly && d->kind) {
            if(!rulesMap || d->kind>2 || !d->isBuilding || d->selectable || d->significant)return false;
            d->rulesOnly=false; // scenery uses map art and neutral actor lifecycle
        }
        if(production.available && d->kind==0 && (!parseNativeProductionProfile(ini.find(("NativeProduction.Type."+std::to_string(i)).c_str()),d->production,production.version)
            || d->isBuilding!=(d->production.category>=4)))return false;
        if(version>=12 && d->kind==0 && d->production.infantryExit<0)return false;
        if(production.economy && (d->production.superweaponMask&~production.superweaponMask))return false;
        if(version>=7 && d->kind==0 && !parseNativeTargetProfile(s,d->targeting))return false;
        d->bridgeRepairHut=integer(s,"BridgeRepairHut",0,1,ok,d->id=="CABHUT"?1:0)!=0;
        d->gate=integer(s,"Gate",0,1,ok,0)!=0;
        if(d->gate) {
            d->gateStages=integer(s,"GateStages",1,255,ok);
            d->gateOpenTicks=integer(s,"GateOpenFrames",0,900000,ok)*LOGIC_FPS/15;
            d->gateCloseTicks=integer(s,"GateCloseFrames",0,900000,ok)*LOGIC_FPS/15;
        }
        d->wall=integer(s,"Wall",0,1,ok,0)!=0;d->overlay=integer(s,"Overlay",-1,254,ok,-1);
        d->trainable=integer(s,"Trainable",0,1,ok,version>=26?std::numeric_limits<int>::min():1)!=0;
        d->artFrames=integer(s,"ArtFrames",1,512,ok,2);d->damageLevels=integer(s,"DamageLevels",1,16,ok,1);
        d->radarRGB=(uint32_t)integer(s,"RadarRGB",0,0xffffff,ok,0x5c5c5c);
        if(d->gate && (!d->isBuilding || !d->blocksMovement || d->kind || d->artFrames<2*d->gateStages+1))return false;
        if(d->kind && (!d->isBuilding || (d->kind>1 && d->overlay<0)))return false;
        d->armor=(Armor)integer(s,"Armor",0,(int)Armor::COUNT-1,ok);
        d->originX=integer(s,"OriginX",0,4096,ok,0);d->originY=integer(s,"OriginY",0,4096,ok,0);
        const int hp=integer(s,"Hp",1,1000000,ok),sight=integer(s,"Sight",0,128,ok),cost=integer(s,"Cost",0,10000000,ok);
        const std::string base=value(s,"Base");
        if(d->isBuilding) {
            BldType type;if(!bldTypeByName(base.c_str(),type))return false;
            d->building=bldDef(type);d->building.name=d->name.c_str();d->building.hp=hp;d->building.cost=cost;d->building.sight=sight;
            d->building.w=integer(s,"Width",1,32,ok);d->building.h=integer(s,"Height",1,32,ok);
            d->building.power=integer(s,"Power",-100000,100000,ok);d->building.garrisonCap=integer(s,"Garrison",0,64,ok);
            if(production.version>=3 && (d->production.infantryAbsorb || d->production.unitAbsorb || d->production.bunker))
                d->building.garrisonCap=d->production.bunker?1:d->production.passengers;
            d->building.capturable=integer(s,"Capturable",0,1,ok)!=0;
            if(d->production.available)d->building.buildTime=54;
            // Occupant weapons are not an autonomous building weapon.
            if(type==BldType::CivHouse)d->building.weapon.damage=0;
        } else {
            UnitType type;if(!unitTypeByName(base.c_str(),type))return false;
            d->unit=unitDef(type);d->unit.name=d->name.c_str();d->unit.hp=hp;d->unit.cost=cost;d->unit.sight=sight;d->unit.armor=d->armor;
            auto locomotor=value(s,"OriginalLocomotor");
            std::transform(locomotor.begin(),locomotor.end(),locomotor.begin(),[](unsigned char c){return (char)std::tolower(c);});
            const bool walks=locomotor=="{4a582744-9839-11d1-b709-00a024ddafd1}";
            if(version>=15 && d->unit.isInfantry() && (!rulesOnly || walks)) {
                if(!walks)return false;
                const int originalSpeed=integer(s,"OriginalSpeed",-1,1000000,ok);
                d->walkSpeedLeptons=std::min(255,std::clamp(originalSpeed,0,100)*256/100);
                float veteranSpeed=1;
                if(!nativeSourceNumber(s->get("OriginalVeteranSpeed"),veteranSpeed,0,100))return false;
                if(integer(s,"VeteranFaster",0,1,ok))d->walkVeteranSpeed[1]=veteranSpeed;
                if(integer(s,"EliteFaster",0,1,ok))d->walkVeteranSpeed[2]=veteranSpeed;
            }
            if(version>=16 && !d->unit.isInfantry() && !d->nativeRocket && !(d->unit.isAir() && (rulesOnly || version>=22))
               && !parseNativeMovementProfile(ini,i,d->movement))return false;
            if(version>=21 && !parseNativeFlightProfile(ini,i,d->flight))return false;
            if(d->flight.available && (!d->unit.isAir() || d->nativeRocket))return false;
            if(version>=22 && !parseNativeJumpjetProfile(ini,i,d->jumpjet))return false;
            if(d->jumpjet.available && d->jumpjet.version>=2 && version<23)return false;
            if(d->jumpjet.available && (!d->unit.isAir() || d->nativeRocket || d->flight.available))return false;
            if(rulesOnly)d->unit.ammo=std::max(0,integer(s,"SourceAmmo",-1,1024,ok,0));
            if(d->production.available)d->unit.buildTime=54;
            d->unit.bridgeClearance=(int8_t)(1-integer(s,"TooBigToFitUnderBridge",0,1,ok,d->unit.fitsUnderBridge()?0:1));
            d->unit.cargoCap=integer(s,"TransportCapacity",0,64,ok,d->unit.cargoCap);
            if(rulesOnly && type==UnitType::AircraftCarrier)d->unit.cargoCap=integer(s,"SourceSpawns",0,64,ok,0);
            d->unit.passengerSize=integer(s,"PassengerSize",1,64,ok,d->unit.passengerSize);
            d->unit.cargoSizeLimit=integer(s,"PassengerLimit",1,64,ok,d->unit.cargoSizeLimit);
            if(d->nativeRocket){d->unit.move=MoveType::Air;d->unit.weapon.damage=0;d->unit.elite=nullptr;d->unit.ammo=1;}
        }
        d->hasNativeWeapon=integer(s,"NativeWeapon",0,1,ok,0)!=0;
        if(d->hasNativeWeapon) {
            auto& weapon=d->isBuilding?d->building.weapon:d->unit.weapon;
            weapon.damage=integer(s,"WeaponDamage",version>=34?-1000000:0,1000000,ok);weapon.cooldown=integer(s,"WeaponROF",1,900000,ok)*LOGIC_FPS/15;
            weapon.antiAir=integer(s,"WeaponAA",0,1,ok)!=0;weapon.antiGround=integer(s,"WeaponAG",0,1,ok)!=0;
            weapon.speedLeptons=std::min(255,integer(s,"WeaponSpeed",0,100,ok)*256/100);weapon.burst=1;
            if(!nativeSourceNumber(s->get("WeaponRange"),weapon.range,0,128)
                || !nativeSourceNumber(s->get("WeaponSpread"),weapon.cellSpread,0,11)
                || !nativeSourceNumber(s->get("WeaponPercentAtMax"),weapon.percentAtMax,0,1)
                || !nativeSourceVerses(s->get("WeaponVerses"),d->weaponVerses))return false;
            d->weaponProjectile=value(s,"WeaponProjectile");d->weaponReport=value(s,"WeaponReport");
            if(!idValid(d->weaponProjectile) || d->weaponReport.size()>128)return false;
            weapon.projectile=d->weaponProjectile.c_str();weapon.report=d->weaponReport.c_str();
            weapon.nativeVerses=d->weaponVerses.data();weapon.nativeType=i;
        }
        for(const auto& rocket:rockets)if(rocket.launcher==i) {
            if(d->isBuilding || d->nativeRocket)return false;d->hasNativeWeapon=true;
            auto& weapon=d->unit.weapon;weapon.damage=rocket.damage;weapon.range=rocket.range;weapon.cooldown=rocket.rof*LOGIC_FPS/15;
            weapon.burst=1;weapon.antiAir=false;weapon.antiGround=true;weapon.nativeType=i;
            d->weaponVerses=rocket.normal.verses;weapon.nativeVerses=d->weaponVerses.data();
            d->nativeElite=weapon;d->nativeElite.damage=rocket.eliteDamage;d->eliteVerses=rocket.elite.verses;
            d->nativeElite.nativeVerses=d->eliteVerses.data();d->unit.elite=&d->nativeElite;
        }
        if(version>=8 && d->kind==0) {
            if(!parseNativeGuns(s,i,d->guns,weapons.get()))return false;
            d->weaponCatalog=weapons;
            if(d->guns.staged() && version<25)return false;
            if(d->guns.direct()) {
                WeaponDef weapon{};weapon.damage=0;weapon.range=0;
                if(d->guns.slots[0].present)weapon=d->guns.slots[0].weapon;
                if(d->isBuilding)d->building.weapon=weapon;
                else {
                    d->unit.weapon=weapon;d->unit.elite=d->guns.slots[2].present?&d->guns.slots[2].weapon:nullptr;
                    d->unit.hasAirWeapon=d->guns.slots[1].present && d->guns.slots[1].weapon.antiAir;
                    if(d->unit.hasAirWeapon)d->unit.airWeapon=d->guns.slots[1].weapon;
                }
            }
        }
        definitions.push_back(std::move(d));
    }
    if(!ok)return false;
    if(rulesMap){std::vector<Vec2i> starts;std::vector<RulesMapActor> placed;
        if(!rulesMapActors(ini,map,numPlayers,definitions,starts,placed))return false;}
    for(const auto& gunner:definitions)if(gunner->ifv.gunner)
        for(const auto& passenger:definitions)if(!passenger->isBuilding && passenger->kind==0
            && passenger->ifv.passengerMode>=(int)gunner->ifv.modes.size())return false;
    for(const auto& rocket:rockets)if(definitions[rocket.launcher]->isBuilding || definitions[rocket.launcher]->nativeRocket
        || definitions[rocket.missile]->isBuilding || !definitions[rocket.missile]->nativeRocket)return false;
    for(const auto& d:aircraft) {
        const auto& owner=*definitions[d.launcher];const auto& child=*definitions[d.aircraft];
        if(owner.isBuilding || owner.nativeRocket || child.isBuilding || child.nativeRocket || !child.unit.isAir())return false;
        if(owner.unit.type==UnitType::AircraftCarrier) {
            if(owner.unit.cargoCap!=d.capacity || child.unit.type!=UnitType::Hornet || d.weaponSlot!=0 || d.underwaterOnly)return false;
        } else if(version>=20 && owner.unit.type==UnitType::Destroyer) {
            if(child.unit.type!=UnitType::ASW || owner.unit.cargoCap || d.weaponSlot!=1 || !d.underwaterOnly
                || owner.navalTargeting!=1 || child.navalTargeting!=2 || child.landTargeting!=1 || !child.unit.ammo
                || !owner.guns.direct() || !owner.guns.slots[1].present || owner.guns.slots[1].weapon.range!=-2)return false;
        } else return false;
        for(const auto& rocket:rockets)if(rocket.launcher==d.launcher)return false;
    }
    if(version>=19)for(int type=0;type<count;++type)if(!definitions[type]->isBuilding
        && definitions[type]->unit.type==UnitType::AircraftCarrier && definitions[type]->unit.cargoCap>0
        && std::none_of(aircraft.begin(),aircraft.end(),[&](const auto& d){return d.launcher==type;}))return false;
    if(version>=20)for(int type=0;type<count;++type)if(!definitions[type]->isBuilding && definitions[type]->unit.type==UnitType::Destroyer
        && std::none_of(aircraft.begin(),aircraft.end(),[&](const auto& d){return d.launcher==type;}))return false;
    for(int i=0;!rulesOnly && i<houses && ok;++i) {
        const auto* house=ini.find(("House."+std::to_string(i)).c_str());if(!house)return false;
        allies[i]=(uint8_t)integer(house,"Allies",0,(1<<houses)-1,ok);if(!(allies[i]&(1<<i)))return false;
        colors[i]=(uint32_t)integer(house,"RGB",0,0xffffff,ok);
    }
    std::vector<NativeBridgeBlueprint> bridges;std::set<std::string> bridgeKeys;std::set<std::pair<int,int>> locations;
    const int actors=integer(scene,"Actors",0,65536,ok);
    for(int i=0;i<actors && ok;++i) {
        const auto* row=ini.find(("Actor."+std::to_string(i)).c_str());
        const int type=integer(row,"Type",0,count-1,ok);if(!ok)return false;
        if(definitions[type]->kind!=3)continue;
        NativeBridgeBlueprint bridge;bridge.type=type;bridge.object=value(row,"Key");bridge.tag=value(row,"Tag");
        bridge.x=integer(row,"X",0,map.w-1,ok);bridge.y=integer(row,"Y",0,map.h-1,ok);
        bridge.house=integer(row,"House",-1,houses-1,ok);bridge.facing=integer(row,"Facing",0,255,ok);
        bridge.frame=integer(row,"OriginalFrame",0,definitions[type]->artFrames-1,ok);
        if(!ok || !map.inBounds(bridge.x,bridge.y) || bridge.object.empty() || bridge.object.size()>128 || bridge.tag.size()>128
            || !bridgeKeys.insert(bridge.object).second || !locations.emplace(bridge.x,bridge.y).second)return false;
        bridge.height=integer(row,"OriginalHeight",0,14,ok,version>=3?std::numeric_limits<int>::min():map.at(bridge.x,bridge.y).height);
        bridges.push_back(std::move(bridge));
    }
    const bool destroyable=integer(scene,"DestroyableBridges",0,1,ok,1)!=0;
    std::shared_ptr<const NativeEventDefinition> eventDefinition;
    if(!ok || ((!rulesOnly || version>=38) && (!parseNativeEventDefinitions(ini,map.w,map.h,numPlayers,eventDefinition) || (version>=5 && !eventDefinition))))return false;
    NativeAIRules aiRules;
    if((!rulesOnly || version>=38) && (!parseNativeAIRules(ini,eventDefinition.get(),rulesOnly?numPlayers:houses,aiRules) || (version>=12 && !aiRules.available)))return false;
    if(version>=38 && (!rulesOnly || !eventDefinition || !eventDefinition->aiOnly || !aiRules.isGlobal()))return false;
    if(!nativeGlobalAIFeaturesMatch(nativeGlobalAIContractForScene(version),aiRules.hasOriginalTeamControl(),
        aiRules.hasOriginalNearbyGeometry(),aiRules.hasOriginalSourceCoordinates()))return false;
    if(aiRules.hasOriginalNearbyGeometry()) {
        for(const auto& [id,speed]:aiRules.nearbySpeeds)if(std::none_of(definitions.begin(),definitions.end(),[&](const auto& d){return !d->isBuilding && lower(d->id)==id;}))return false;
        for(const auto& d:definitions)if(!d->isBuilding && !d->kind && !aiRules.nearbySpeeds.contains(lower(d->id)))return false;
    }
    if(aiRules.hasOriginalTeamControl()) {
        for(const auto& [id,rating]:aiRules.leadership)if(std::none_of(definitions.begin(),definitions.end(),[&](const auto& d){return !d->isBuilding && lower(d->id)==id;}))return false;
        for(const auto& d:definitions)if(!d->isBuilding && !d->kind && !aiRules.leadership.contains(lower(d->id)))return false;
        for(const auto& id:aiRules.baseBuildings)if(std::none_of(definitions.begin(),definitions.end(),[&](const auto& d){return d->isBuilding && lower(d->id)==id;}))return false;
    }
    if(rulesOnly && version<38 && (ini.find("NativeEvents") || ini.find("NativeAI")))return false;
    nativeAIRules=std::move(aiRules);
    NativeCrateRules crateRules;
    if(!rulesOnly && (!parseNativeCrateRules(ini,crateRules) || (version>=13 && (!crateRules.available || !eventDefinition->orderedActions))))return false;
    if(rulesOnly && version>=26){if(!parseNativeCrateRules(ini,crateRules) || !crateRules.randomCrates)return false;
        const auto present=[&](const std::string& id,bool building){return std::any_of(definitions.begin(),definitions.end(),[&](const auto& d){return d->id==id && d->isBuilding==building && !d->kind;});};
        for(const auto& id:crateRules.baseUnits)if(!present(id,false))return false;
        for(const auto& id:crateRules.constructionYards)if(!present(id,true))return false;
        for(const auto& id:crateRules.harvesters)if(!present(id,false))return false;
        if(!crateRules.unitCrateType.empty() && !present(crateRules.unitCrateType,false))return false;
    }
    if(rulesOnly && version<26 && ini.find("NativeCrates"))return false;
    if(!rulesOnly && version>=14 && (!eventDefinition || !eventDefinition->extendedConditions))return false;
    if(production.version>=4) {
        unsigned supportMask=0;
        for(int i=0;i<SUPPORT_COUNT;++i)if(production.support[i].available) {
            supportMask|=1u<<i;
            for(int side=0;side<3;++side)if(production.support[i].count[side]>0) {
                if(std::none_of(definitions.begin(),definitions.end(),[&](const auto& d){return !d->isBuilding
                    && d->unit.isInfantry() && d->id==production.support[i].infantry[side];}))return false;
            }
            if(i<2 && (countryMask&(1<<(int)Country::Yuri)) && !production.support[i].count[2])return false;
        }
        for(const auto& d:definitions)if(d->production.supportMask&~supportMask)return false;
    }
    if(production.version>=5)for(int i=0;i<3;++i)if(production.support[i].available) {
        const auto type=i==2?UnitType::SpyPlane:UnitType::CargoPlane;
        if(std::none_of(definitions.begin(),definitions.end(),[&](const auto& d){return !d->isBuilding && d->unit.type==type
            && d->flight.available && !d->selectable && !d->canPassiveAcquire;}))return false;
    }
    nativeCrateRules=std::move(crateRules);
    nativeEventDefinition=std::move(eventDefinition);
    nativeProductionRules=std::move(production);
    if(nativeProductionRules.version<4)veterancyProgressEnabled=independentSupportEnabled=false;
    if(nativeProductionRules.version<5)supportDeliveryEnabled=false;
    nativeMovementRules=std::move(movement);
    nativeRocketDefinitions=std::move(rockets);nativeAircraftDefinitions=std::move(aircraft);
    nativeScenarioSource=source;nativeTypes=std::move(definitions);nativeBridgeBlueprints=std::move(bridges);nativeWeaponCatalog=std::move(weapons);
    nativeSceneVersion=declaredVersion;nativeCrewRules=crewRules;nextCombatActor=1;nativeCrewBursts.clear();
    nativeTemporalEnabled=version>=35;nativeTemporalBeams.clear();for(auto& actor:ents)actor.nativeTemporalHead=INVALID_EID;
    rulesProfileId=std::move(profileId);rulesProfileName=std::move(profileName);rulesCountryMask=countryMask;
    rulesCountryKeys=std::move(countryKeys);rulesCountrySpeed=countrySpeed;
    nativeBridgesDestroyable=destroyable;
    rebuildNativeBridgeGroups();
    std::copy_n(allies,MAX_PLAYERS,nativeAllies);std::copy_n(colors,MAX_PLAYERS,nativeColors);diagnostic.done=true;return true;
}

bool World::initializeRulesMapStarts(std::vector<Vec2i>& starts) {
    if(nativeSceneVersion!=nativeSkirmishMapSceneVersion || !map.native.active() || !ents.empty())return false;
    Ini ini;std::vector<Vec2i> parsedStarts;std::vector<RulesMapActor> placed;
    if(!parseScene(nativeScenarioSource,ini) || !rulesMapActors(ini,map,numPlayers,nativeTypes,parsedStarts,placed))return false;
    for(const auto& a:placed)if(spawnNativeActor(a.type,-1,(float)a.x,(float)a.y,a.health,a.facing,a.key,a.tag,a.mission)==INVALID_EID)return false;
    starts=std::move(parsedStarts);return true;
}

EID World::spawnNativeActor(int type,int player,float x,float y,int health,int facing,
                            const std::string& object,const std::string& tag,const std::string& mission) {
    if(type<0 || type>=(int)nativeTypes.size() || player < -1 || player>=numPlayers || health<1 || health>256 || facing<0 || facing>255
        || object.size()>128 || tag.size()>128 || !std::isfinite(x) || !std::isfinite(y)
        || x<0 || y<0 || x>=map.w || y>=map.h || !map.inBounds((int)x,(int)y))return INVALID_EID;
    const auto definition=nativeTypes[type];const bool building=definition->isBuilding;
    if(player<0 && !definition->kind)return INVALID_EID;
    const auto& b=definition->building;
    if(building && definition->blocksMovement)
        for(int dy=0;dy<b.h;++dy)for(int dx=0;dx<b.w;++dx)
            if(!map.inBounds((int)x+dx,(int)y+dy))return INVALID_EID;
    if(definition->rulesOnly) {
        const EID id=building?spawnBuilding(player,b.type,(int)x,(int)y,true,type):spawnUnit(player,definition->unit.type,x,y,type);
        if(!valid(id))return INVALID_EID;
        auto& e=ents[id];e.nativeIdentity={type,definition,object,tag,mission=="Sleep"};
        e.hp=std::max(1,(building?b.hp:definition->unit.hp)*health/256);
        e.dir=e.turretDir=((facing+16)/32+6)&7;
        if(usesNativeJumpjet(e)){e.nativeJumpjet={};initializeNativeJumpjet(e);}return id;
    }
    const EID id=allocEnt();if(id==INVALID_EID)return INVALID_EID;auto& e=ents[id];e.isBuilding=building;e.player=player;e.x=e.px=x;e.y=e.py=y;
    e.nativeIdentity={type,definition,object,tag,mission=="Sleep"};e.btype=building?b.type:BldType::ConYard;
    e.utype=building?UnitType::GI:definition->unit.type;e.hp=std::max(1,(building?b.hp:definition->unit.hp)*health/256);
    e.dir=e.turretDir=((facing+16)/32+6)&7;
    if(building) {
        e.rallyX=e.rallyY=-1;
        if(definition->blocksMovement)for(int dy=0;dy<b.h;++dy)for(int dx=0;dx<b.w;++dx)bldOcc[cellIdx((int)x+dx,(int)y+dy)]=id+1;
    } else {e.goalX=x;e.goalY=y;initializeMovementSurface(e);initializeNativeJumpjet(e);unitOccSync(id);airOccSync(id);}
    if(definition->gate)rebuildNativeGates();
    initializeNativeLauncher(id);
    return id;
}

bool World::loadNativeScene(const char* path) {
    std::ifstream input(path,std::ios::binary);if(!input)return false;
    const std::string source((std::istreambuf_iterator<char>(input)),{});Ini ini;if(!parseScene(source,ini))return false;
    World staged;staged.map=map;staged.map.bldOccRef=nullptr;
    const auto* header=ini.find("Scene");bool ok=header!=nullptr;
    const int houses=integer(header,"Houses",1,MAX_PLAYERS,ok),actors=integer(header,"Actors",0,65536,ok);
    if(!ok || !map.native.active())return false;
    staged.numPlayers=houses;staged.players.assign(houses,Player{});staged.rng=rng;
    staged.bldOcc.assign(map.cells.size(),-1);staged.unitOccHead.assign(map.cells.size(),INVALID_EID);
    staged.airBucketW=(map.w+AIR_BUCKET-1)/AIR_BUCKET;
    staged.airOccHead.assign((size_t)staged.airBucketW*((map.h+AIR_BUCKET-1)/AIR_BUCKET),INVALID_EID);
    staged.map.bldOccRef=&staged.bldOcc;staged.map.initFog(houses);
    if(!staged.restoreNativeDefinitions(source))return false;
    for(int i=0;i<houses;++i) {
        const auto* s=ini.find(("House."+std::to_string(i)).c_str());auto& p=staged.players[i];
        p.active=true;p.name=value(s,"Name");p.faction=value(s,"Faction")=="Soviet"?Faction::Soviet:Faction::Allies;
        if(!countryByName(value(s,"Country").c_str(),p.country))return false;p.isAI=integer(s,"PlayerControl",0,1,ok)==0;
        p.colorId=integer(s,"Color",0,7,ok);p.money=integer(s,"Money",0,10000000,ok);
    }
    for(int i=0;i<actors && ok;++i) {
        const auto* s=ini.find(("Actor."+std::to_string(i)).c_str());if(!s)return false;
        const int type=integer(s,"Type",0,(int)staged.nativeTypes.size()-1,ok),house=integer(s,"House",-1,houses-1,ok);
        const int x=integer(s,"X",0,map.w-1,ok),y=integer(s,"Y",0,map.h-1,ok);
        const int health=integer(s,"Health",1,256,ok),facing=integer(s,"Facing",0,255,ok);
        if(!ok || staged.spawnNativeActor(type,house,x+(staged.nativeTypes[type]->isBuilding?0:.5f),
                y+(staged.nativeTypes[type]->isBuilding?0:.5f),health,facing,value(s,"Key"),value(s,"Tag"),value(s,"Mission"))==INVALID_EID)return false;
    }
    if(!ok)return false;
    staged.rebuildNativeBridges();
    staged.rebuildNativeGates();
    for(auto& actor:staged.ents)if(actor.alive && !actor.isBuilding && !unitDef(actor).isAir()
        && !staged.map.surfacePassable((int)actor.x,(int)actor.y,unitDef(actor).pathDomain(),0,false)
        && staged.map.hasDeck((int)actor.x,(int)actor.y))staged.initializeMovementSurface(actor);
    staged.recomputePower();for(int i=0;i<houses;++i)staged.updateFog(i);
    staged.presentationEpoch=nextPresentationEpoch();*this=std::move(staged);map.bldOccRef=&bldOcc;return true;
}

bool World::writeNativeActors(FILE* file) const {
    BinaryStateStream s{file};const uint8_t active=!nativeScenarioSource.empty();s.write(active);
    if(!active) {
        auto empty=[](const NativeActorIdentity& n){return n.type==-1 && !n.definition && n.object.empty() && n.tag.empty() && !n.sleeping;};
        auto passenger=[&](auto&& self,const Ent::GarrisonedUnit& p)->bool{if(!empty(p.nativeIdentity))return false;for(const auto& child:p.cargo)if(!self(self,child))return false;return true;};
        for(const auto& e:ents){if(!empty(e.nativeIdentity) || (e.alive && !e.isBuilding && e.utype==UnitType::NativeVehicle))return false;for(const auto& p:e.cargo)if(!passenger(passenger,p))return false;for(const auto& p:e.garrison)if(!passenger(passenger,p))return false;}
        return s.ok && nativeTypes.empty() && std::all_of(nativeAllies,nativeAllies+MAX_PLAYERS,[](uint8_t n){return n==0;});
    }
    s.writeText(nativeScenarioSource);for(uint8_t row:nativeAllies){if(row>=(1u<<numPlayers))return false;s.write(row);}
    s.write((uint32_t)ents.size());
    auto identity=[&](const NativeActorIdentity& n) {
        if(n.type < -1 || n.type>=(int)nativeTypes.size() || n.object.size()>128 || n.tag.size()>128
            || (n.type>=0 && n.definition!=nativeTypes[n.type])){s.ok=false;return;}
        s.write(n.type);s.writeText(n.object);s.writeText(n.tag);s.write((uint8_t)n.sleeping);
    };
    auto passenger=[&](auto&& self,const Ent::GarrisonedUnit& p)->void{identity(p.nativeIdentity);for(const auto& child:p.cargo)self(self,child);};
    for(const auto& e:ents){identity(e.nativeIdentity);for(const auto& p:e.cargo)passenger(passenger,p);for(const auto& p:e.garrison)passenger(passenger,p);}
    return s.ok;
}

bool World::readNativeActors(FILE* file) {
    BinaryStateStream s{file};uint8_t active=0;s.read(active);if(!s.ok || active>1)return false;
    nativeScenarioSource.clear();nativeTypes.clear();nativeWeaponCatalog.reset();nativeProductionRules={};nativeProductionSerial=0;std::fill_n(nativeAllies,MAX_PLAYERS,0);
    rulesProfileId.clear();rulesProfileName.clear();rulesCountryMask=0;rulesCountryKeys={};rulesCountrySpeed.fill(1);
    resetNativeEvents();
    resetNativeRockets();
    resetNativeBridgeScenario();
    std::fill_n(nativeColors,MAX_PLAYERS,0);
    if(!active)return true;
    std::string source;s.readText(source,4*1024*1024);if(!s.ok || !restoreNativeDefinitions(source))return false;
    for(auto& row:nativeAllies){s.read(row);if(row>=(1u<<numPlayers))return false;}
    uint32_t count=0;s.read(count);if(!s.ok || count!=ents.size())return false;
    auto identity=[&](NativeActorIdentity& n,bool building,UnitType unit,BldType bld) {
        s.read(n.type);s.readText(n.object,128);s.readText(n.tag,128);uint8_t sleep=0;s.read(sleep);n.sleeping=sleep!=0;
        if(!s.ok || sleep>1 || n.type < -1 || n.type>=(int)nativeTypes.size()){s.ok=false;return;}
        if(n.type>=0) {
            n.definition=nativeTypes[n.type];const auto& d=*n.definition;
            if(d.isBuilding!=building || (building?d.building.type!=bld:d.unit.type!=unit))s.ok=false;
        }
    };
    auto passenger=[&](auto&& self,Ent::GarrisonedUnit& p)->void{identity(p.nativeIdentity,false,p.type,BldType::ConYard);for(auto& child:p.cargo)self(self,child);};
    for(auto& e:ents){identity(e.nativeIdentity,e.isBuilding,e.utype,e.btype);for(auto& p:e.cargo)passenger(passenger,p);for(auto& p:e.garrison)passenger(passenger,p);}
    return s.ok;
}
