#include "game_regression.h"
#include "game/script.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <fstream>
#include <iterator>

namespace {
std::string edit(std::string source,const std::string& section,const std::string& key,const std::string& value) {
    const auto start=source.find("["+section+"]"),end=source.find("\n[",start==std::string::npos?0:start+1);
    const auto field=source.find("\n"+key+" = ",start);
    if(start==std::string::npos || field==std::string::npos || (end!=std::string::npos && field>=end))return {};
    const auto a=field+key.size()+4,b=source.find('\n',a);source.replace(a,b-a,value);return source;
}
}

int GameRegression::nativeWeaponActionsRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;g_script.shutdown();
    const auto check=[&](bool ok,const char* label){failures+=!ok;std::printf("NATIVE_WEAPON_ACTION %s %s\n",ok?"PASS":"FAIL",label);std::fflush(stdout);};
    for(int edition=0;edition<2;++edition) {
        resetMatchInteraction();ais.clear();matchSetup.reset();campaignMission=-1;localPlayer=0;
        phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;selectRulesChoice(edition?RulesChoice::YR:RulesChoice::RA2);
        cfgCountry=(int)Country::America;cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgReferenceMap=false;cfgGameMode=0;
        const char* profile=edition?"assets/rules/profiles/yr.ini":"assets/rules/profiles/ra2.ini";
        bool loaded=world.init(64,64,1919,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,profile,{Country::America,Country::Russia});
        // Exercise the historical Scene32/v1 contract explicitly; Scene33's
        // cell selection has its own independent shrapnel regression.
        if(loaded) {
            const auto fixture=std::filesystem::path("tests/fixtures/native-weapon-actions")/(edition?"yr-scene32.ini":"ra2-scene32.ini");
            std::ifstream input(fixture,std::ios::binary);
            World::RulesSnapshot legacy{std::string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>())};
            loaded=world.init(64,64,1919,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,nullptr,{Country::America,Country::Russia},&legacy);
        }
        check(loaded && world.nativeWeaponCatalog,"both editions retain the historical Scene32 recursive weapon contract");if(!loaded || !world.nativeWeaponCatalog)continue;
        const auto catalog=world.nativeWeaponCatalog;
        const auto named=[&](const char* id)->const NativeWeaponData* {
            for(const auto& w:catalog->weapons)if(w.id==id)return &w;return nullptr;
        };
        const auto* comet=named("Comet");const auto* elite=named("SuperComet");const auto* fragment=named("CometFragment");
        const auto* superFragment=named("SuperCometFragment");const auto* sonic=named("SonicZap");const auto* chrono=named("NeutronRifle");
        check(comet && elite && fragment && superFragment && comet->laser && elite->laser && fragment->weapon.damage==30
            && superFragment->weapon.damage==50 && comet->projectile->shrapnelCount==5 && superFragment->projectile->shrapnelCount==3,
            "the non-elite and two-level elite prism graphs retain independent source damage and counts");
        check(sonic && sonic->sonic && sonic->ambientDamage==10 && chrono && catalog->warheads[chrono->warheadIndex].temporal,
            "sonic ambient damage and Temporal flags are retained as data, independently of their pending controllers");
        bool refs=true;int slots=0;
        for(const auto& type:world.nativeTypes)if(type->guns.direct())for(const auto& gun:type->guns.slots)if(gun.present) {
            ++slots;const int code=world.nativeWeaponReference(gun.weapon);auto restored=gun.weapon;restored.nativeSource=nullptr;
            refs &= gun.weapon.nativeSource && code>=0 && world.restoreNativeWeaponReference(restored,code,true)
                && restored.nativeSource==gun.weapon.nativeSource && &projectileForWeapon(restored)==&projectileForWeapon(gun.weapon);
            auto invalid=gun.weapon;invalid.nativeSource=nullptr;refs &= world.nativeWeaponReference(invalid)==-2;
        }
        check(refs && slots>100,"every direct source gun restores its actual catalog projectile and rejects a lost source binding");
        for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
        for(auto& c:world.map.cells)c=Cell{};
        for(auto& player:world.players){player.isAI=false;player.defeated=false;}
        world.cratesEnabled=false;world.crates.clear();world.rebuildUnitOcc();world.rebuildAirOcc();
        world.spawnBuilding(0,BldType::ConYard,2,2,true);world.spawnBuilding(1,BldType::ConYard,55,55,true);
        const auto base=world;
        const auto reset=[&]{world=base;world.map.bldOccRef=&world.bldOcc;world.effects.clear();world.projs.clear();};
        const auto target=[&](int owner,float x,float y){const EID id=world.spawnUnit(owner,UnitType::Rhino,x,y);if(world.valid(id))world.ents[id].tpSick=500;return id;};
        const auto attack=[&](EID actor,EID victim) {
            if(!world.valid(actor) || !world.valid(victim))return false;
            const int hp=world.ents[victim].hp;if(world.orderAttack({actor},victim,false).accepted!=1)return false;
            for(int i=0;i<250 && world.valid(victim) && world.ents[victim].hp==hp;++i)world.update();
            return !world.valid(victim) || world.ents[victim].hp<hp;
        };
        const auto snapshot=[&](const std::string& name,int ticks) {
            const auto prefix=std::filesystem::path(outputDir)/(std::to_string(edition)+"-"+name);
            const auto raw=prefix.string()+"-world59.sav",app=prefix.string()+"-app11.sav";
            FILE* f=std::fopen(raw.c_str(),"wb");bool exact=f && world.saveGame(f);if(f)std::fclose(f);
            World copy;f=std::fopen(raw.c_str(),"rb");exact &= f && copy.loadGame(f);if(f)std::fclose(f);
            auto replay=std::make_unique<Game>();exact &= saveGameFile(app.c_str()) && replay->loadGameFile(app.c_str());
            exact &= world.checksum()==copy.checksum() && world.checksum()==replay->world.checksum();
            for(int i=0;i<ticks && exact;++i){world.update();copy.update();replay->world.update();
                exact &= world.checksum()==copy.checksum() && world.checksum()==replay->world.checksum();}
            check(exact,"actual World59/App11 saves preserve source references and resume every tick exactly");return exact;
        };
        const EID prism=world.spawnUnit(0,UnitType::PrismTank,10.5f,10.5f),primary=target(1,15.5f,10.5f);
        std::vector<EID> nearby;for(const auto p:{Vec2i{16,10},Vec2i{16,11},Vec2i{15,11},Vec2i{14,11},Vec2i{14,10},Vec2i{15,9}})nearby.push_back(target(1,p.x+.5f,p.y+.5f));
        bool legal=world.valid(prism) && world.valid(primary);for(auto id:nearby)legal &= world.valid(id);
        check(legal,"normal-stat prism and seven distinct enemy tanks spawn on a legal level surface");if(!legal)continue;
        const bool fired=attack(prism,primary);int hits=0;bool amounts=true;
        for(auto id:nearby){const int loss=unitDef(world.ents[id]).hp-world.ents[id].hp;hits+=loss>0;amounts &= loss==0 || loss==15;}
        check(fired && world.ents[primary].hp==350 && hits==5 && amounts && world.projs.empty(),
            "a public attack deals Comet's 50-versus-heavy hit and five independent 15-point fragments");
        check(std::count_if(world.effects.begin(),world.effects.end(),[](const auto& e){return e.alive && e.kind==3 && e.aux==2 && e.maxAge==30;})==6,
            "the primary and five fragments emit source-duration laser effects instead of gun muzzle flashes");
        const auto beamState=world;
        snapshot("normal-beams",120);
        world=beamState;world.map.bldOccRef=&world.bldOcc;
        camZoom=1;camX=-450;camY=0;shotAtDisplaySize=true;bakeTerrain();bool visible=true;
        for(const auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);const auto path=(std::filesystem::path(outputDir)/(std::to_string(edition)+"-laser-"+std::to_string(size.x)+".png")).string();
            world.map.reveal(0,32,32,64);const auto hash=world.checksum();shotFile=path;render();Image drawn=LoadImage(path.c_str());
            const auto beams=world.effects;
            world.effects.erase(std::remove_if(world.effects.begin(),world.effects.end(),[](const auto& e){return e.kind==3 && e.aux==2;}),world.effects.end());
            const auto without=path+"-without.png";shotFile=without;render();Image empty=LoadImage(without.c_str());world.effects=beams;
            int changed=0;if(drawn.data && empty.data && drawn.width==empty.width && drawn.height==empty.height) {
                auto* a=LoadImageColors(drawn);auto* b=LoadImageColors(empty);
                if(a && b)for(int i=0;i<drawn.width*drawn.height;++i)changed += a[i].r!=b[i].r || a[i].g!=b[i].g || a[i].b!=b[i].b;
                UnloadImageColors(a);UnloadImageColors(b);
            }
            visible &= drawn.data && drawn.width==size.x && drawn.height==size.y && changed>100 && world.checksum()==hash;
            UnloadImage(drawn);UnloadImage(empty);
        }
        shotAtDisplaySize=false;shotFile.clear();check(visible,"both display sizes draw a positive laser contribution while rendering leaves simulation unchanged");
        reset();const EID upgraded=world.spawnUnit(0,UnitType::PrismTank,10.5f,10.5f),first=target(1,15.5f,10.5f),second=target(1,16.5f,10.5f);
        if(world.valid(upgraded))world.ents[upgraded].vetRank=2;
        check(attack(upgraded,first) && world.ents[first].hp==303 && world.ents[second].hp==375 && world.projs.empty(),
            "elite SuperComet uses its own 165-point gun plus 50-point and recursive 30-point fragments");
        snapshot("elite-beams",120);
        reset();const EID buildingPrism=world.spawnUnit(0,UnitType::PrismTank,10.5f,10.5f),building=world.spawnBuilding(1,BldType::SentryGun,15,10,true),neighbor=target(1,16.5f,11.5f);
        const int hp=world.valid(building)?world.ents[building].hp:0;
        check(attack(buildingPrism,building) && world.ents[building].hp==hp-200 && world.ents[neighbor].hp==400,
            "an original prism hit on a building does not generate the infantry/vehicle shrapnel branch");
        reset();const EID guarded=world.spawnUnit(0,UnitType::PrismTank,10.5f,10.5f),enemy=target(1,15.5f,10.5f),friendly=target(0,16.5f,10.5f),far=target(1,19.5f,10.5f);
        check(attack(guarded,enemy) && world.ents[friendly].hp==400 && world.ents[far].hp==400,
            "fragment selection excludes allies and enemies outside the fragment weapon's source range");
        const auto flak=world.spawnUnit(1,UnitType::FlakTrooper,25.5f,25.5f),ground=world.spawnUnit(0,UnitType::GI,27.5f,25.5f);
        bool trajectory=world.valid(flak) && world.valid(ground);
        if(trajectory){const auto gun=world.effWeapon(world.ents[flak],&world.ents[ground]);const auto& p=projectileForWeapon(gun);
            trajectory=std::string(p.name)=="FlakTProj" && p.arcing && p.inaccurate && !p.inviso;}
        check(trajectory,"native ground flak resolves its own arcing inaccurate projectile without the fusion fallback");
        bool launched=world.valid(flak) && world.valid(ground);
        if(launched){world.ents[ground].tpSick=500;launched=world.orderAttack({flak},ground,false).accepted==1;
            bool found=false;for(int i=0;i<120 && !found;++i){world.update();for(const auto& p:world.projs)if(p.src==flak){found=true;launched &= p.arcing && p.rot==0 && p.speed==10;}}
            launched &= found;}
        check(launched,"the public flak attack actually launches a source arcing shell at the original arcing speed");
        bool strict=comet && fragment && superFragment;
        if(strict) {
            const auto projectile="NativeProjectile."+std::to_string(comet->projectileIndex);
            std::vector<std::string> malformed{
                edit(base.nativeScenarioSource,projectile,"ShrapnelWeapon",std::to_string(comet->index)),
                edit(base.nativeScenarioSource,projectile,"ShrapnelWeapon","2048"),
                edit(base.nativeScenarioSource,"NativeWeapon."+std::to_string(comet->index),"Range","nan"),
                edit(base.nativeScenarioSource,"NativeWeapon."+std::to_string(comet->index),"Damage","101"),
                edit(base.nativeScenarioSource,"NativeWeapons","Weapons","2049"),
                edit(base.nativeScenarioSource,"NativeWeapon."+std::to_string(fragment->index),"Id","comet"),
                edit(base.nativeScenarioSource,"Scene","Version","31")};
            auto excessive=edit(base.nativeScenarioSource,"NativeProjectile."+std::to_string(elite->projectileIndex),"ShrapnelCount","256");
            excessive=edit(excessive,"NativeProjectile."+std::to_string(superFragment->projectileIndex),"ShrapnelCount","256");malformed.push_back(excessive);
            for(const auto& source:malformed){World staged=base;staged.map.bldOccRef=&staged.bldOcc;const auto hash=staged.checksum();
                strict &= !source.empty() && !staged.restoreNativeDefinitions(source) && staged.checksum()==hash;}
        }
        check(strict,"cycles, invalid references, nonfinite values, inconsistent gun data and excessive graphs reject atomically");
        if(fragment) {
            auto source=edit(base.nativeScenarioSource,"NativeWeapon."+std::to_string(fragment->index),"Laser","0");
            source=edit(source,"NativeProjectile."+std::to_string(fragment->projectileIndex),"Inviso","0");World::RulesSnapshot mod{source};
            const bool modded=!source.empty() && world.init(64,64,1927,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,nullptr,{Country::America,Country::Russia},&mod);
            check(modded,"a source-only fragment projectile override parses without rewriting ordinary gun slots");
            if(modded){for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
                for(auto& c:world.map.cells)c=Cell{};world.rebuildUnitOcc();world.cratesEnabled=false;
                world.spawnBuilding(0,BldType::ConYard,2,2,true);world.spawnBuilding(1,BldType::ConYard,55,55,true);
                const auto actor=world.spawnUnit(0,UnitType::PrismTank,10.5f,10.5f),a=target(1,15.5f,10.5f),b=target(1,16.5f,10.5f);
                bool flying=attack(actor,a) && !world.projs.empty();for(const auto& p:world.projs)flying &= p.w.nativeType==-2 && world.nativeWeaponReference(p.w)>=2000000;
                check(flying,"real split projectiles use standalone catalog identities beyond the launcher's four gun slots");
                world.kill(actor,false);snapshot("flying-fragment",60);
                check(world.valid(b) && world.ents[b].hp==385,"a launched fragment survives its launcher's death and deals the same saved source damage");
            }
        }
        World isolated;isolated.numPlayers=2;isolated.players.resize(2);
        bool lifetime=isolated.restoreNativeDefinitions(base.nativeScenarioSource);
        std::shared_ptr<const NativeActorDefinition> retained;
        if(lifetime)for(const auto& d:isolated.nativeTypes)if(!d->isBuilding && d->unit.type==UnitType::PrismTank){retained=d;break;}
        std::weak_ptr<const NativeWeaponCatalog> weak=isolated.nativeWeaponCatalog;isolated=World{};
        lifetime &= retained && !weak.expired() && retained->guns.slots[0].weapon.nativeSource
            && std::string(projectileForWeapon(retained->guns.slots[0].weapon).name)=="LargeCometP";
        retained.reset();lifetime &= weak.expired();check(lifetime,
            "actor definitions keep their immutable catalog alive independently of a World's lifetime");
    }
    return failures;
}
