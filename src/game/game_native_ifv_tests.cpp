#include "game_regression.h"
#include "game/script.h"
#include "game/world_priv.h"
#include "gfx/sprites.h"
#include "gfx/vxl.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>

namespace {
std::string field(std::string source,const std::string& section,const std::string& key,const std::string& value) {
    const auto start=source.find("["+section+"]"),end=source.find("\n[",start==std::string::npos?0:start+1);
    const auto pos=source.find("\n"+key+" = ",start);
    if(start==std::string::npos || pos==std::string::npos || (end!=std::string::npos && pos>=end))return {};
    const auto a=pos+key.size()+4,b=source.find('\n',a);source.replace(a,b-a,value);return source;
}
uint64_t imageHash(Image image) {
    if(!image.data)return 0;auto* pixels=LoadImageColors(image);uint64_t hash=1469598103934665603ull;
    if(pixels)for(int i=0;i<image.width*image.height;++i)for(auto c:{pixels[i].r,pixels[i].g,pixels[i].b,pixels[i].a}){hash^=c;hash*=1099511628211ull;}
    UnloadImageColors(pixels);return hash;
}
}

int GameRegression::nativeIFVRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;g_script.shutdown();
    const auto check=[&](bool ok,const std::string& label) {
        failures+=!ok;std::printf("NATIVE_IFV %s %s\n",ok?"PASS":"FAIL",label.c_str());std::fflush(stdout);
    };
    check(IsWindowHidden(),"the IFV entry starts with a hidden window");
    for(const char* edition:{"ra2","yr"}) {
        resetMatchInteraction();ais.clear();matchSetup.reset();campaignMission=-1;localPlayer=0;
        phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;
        selectRulesChoice(edition[0]=='r'?RulesChoice::RA2:RulesChoice::YR);
        cfgCountry=(int)Country::America;cfgAI=1;cfgMapSize=64;cfgMapType=0;cfgMoney=10000;cfgReferenceMap=false;cfgGameMode=0;
        auto& w=world;const std::string profile=std::string("assets/rules/profiles/")+edition+".ini";
        const auto init=[&](const World::RulesSnapshot* snapshot=nullptr) {
            return w.init(64,64,2311,1,1,{Faction::Allies,Faction::Soviet},0,nullptr,false,false,
                snapshot?nullptr:profile.c_str(),{Country::America,Country::Russia},snapshot);
        };
        const bool loaded=init();check(loaded && w.nativeWeaponCatalog && w.nativeWeaponCatalog->version==3,std::string(edition)+" loads the source gunner contract");
        if(!loaded)continue;
        const auto clear=[&] {
            for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
            for(auto& c:w.map.cells)c=Cell{};
            for(auto& p:w.players){p.isAI=false;p.defeated=false;}
            w.cratesEnabled=false;w.crates.clear();w.effects.clear();w.rebuildUnitOcc();
            w.spawnBuilding(0,BldType::ConYard,2,2,true);w.spawnBuilding(1,BldType::ConYard,55,55,true);
        };
        clear();const auto base=w;
        const auto reset=[&]{w=base;w.map.bldOccRef=&w.bldOcc;};
        const auto type=[&](const char* name){for(int i=0;i<(int)w.nativeTypes.size();++i)if(w.nativeTypes[i]->id==name)return i;return -1;};
        const auto spawn=[&](const char* name,int player,float x,float y) {
            const auto index=type(name);return index>=0?w.spawnNativeActor(index,player,x,y,256,0,"ifv-"+std::string(name),"","Guard"):INVALID_EID;
        };
        const auto vehicle=[&]{return w.spawnUnit(0,UnitType::IFV,12.5f,12.5f);};
        const auto board=[&](EID carrier,const char* name,int rank=0) {
            const auto passenger=spawn(name,0,13.5f,12.5f);if(!w.valid(carrier) || !w.valid(passenger))return false;
            const auto ref=w.entityRef(passenger);w.ents[passenger].vetRank=rank;
            if(w.orderBoard({passenger},carrier,false).accepted!=1)return false;
            for(int tick=0;tick<240 && w.valid(ref) && w.ents[carrier].cargo.empty();++tick)w.update();
            return !w.valid(ref) && w.valid(carrier) && w.ents[carrier].cargo.size()==1
                && w.ents[carrier].cargo.front().nativeIdentity.definition->id==name;
        };
        const auto path=[&](const std::string& name){return (std::filesystem::path(outputDir)/(std::string(edition)+"-"+name)).string();};
        const auto save=[&](const std::string& name,int ticks) {
            const auto raw=path(name+"-world59.sav"),app=path(name+"-app11.sav");
            FILE* file=std::fopen(raw.c_str(),"wb");bool exact=file && w.saveGame(file);if(file)std::fclose(file);
            World copy;file=std::fopen(raw.c_str(),"rb");exact &= file && copy.loadGame(file);if(file)std::fclose(file);
            auto replay=std::make_unique<Game>();exact &= saveGameFile(app.c_str()) && replay->loadGameFile(app.c_str());
            exact &= w.checksum()==copy.checksum() && w.checksum()==replay->world.checksum();const auto before=w;
            for(int i=0;i<ticks && exact;++i){w.update();copy.update();replay->world.update();exact &= w.checksum()==copy.checksum() && w.checksum()==replay->world.checksum();}
            check(exact,"actual World59 and App11 preserve "+name+" and replay every tick exactly");
            w=before;w.map.bldOccRef=&w.bldOcc;return exact;
        };
        {
            const auto fixture=(std::filesystem::path("tests/fixtures/native-ifv-v1")/(std::string(edition)+"-world58.sav")).string();
            World legacy;FILE* file=std::fopen(fixture.c_str(),"rb");bool historical=file && legacy.loadGame(file);if(file)std::fclose(file);
            EID carrier=INVALID_EID;if(historical)for(EID id=0;id<(EID)legacy.ents.size();++id)
                if(legacy.valid(id) && !legacy.ents[id].isBuilding && legacy.ents[id].utype==UnitType::IFV){carrier=id;break;}
            historical &= legacy.valid(carrier);
            if(historical){legacy.orderUnload({carrier},false);const auto gun=legacy.effWeapon(legacy.ents[carrier]);
                historical &= legacy.ents[carrier].nativeIdentity.definition->ifv.version==1 && gun.burstDelayMin==3 && gun.burstDelayMax==5;}
            check(historical,"an actual NativeIFV1 checkpoint retains its historical burst ticks after unloading");
            if(historical){w=legacy;w.map.bldOccRef=&w.bldOcc;save("legacy-ifv1",120);}reset();
        }
        EID fv=vehicle();bool empty=w.valid(fv) && w.hasNativeIFV(w.ents[fv]) && w.nativeIFVMode(w.ents[fv])==0;
        if(empty){auto gun=w.effWeapon(w.ents[fv]);empty &= gun.nativeSource && gun.nativeSource->id=="HoverMissile" && gun.burstDelayMin==6 && gun.burstDelayMax==10;
            w.ents[fv].vetRank=2;gun=w.effWeapon(w.ents[fv]);empty &= gun.nativeSource && gun.nativeSource->id=="HoverMissileE" && gun.burstDelayMin==6 && gun.burstDelayMax==10;}
        check(empty,"empty normal and elite IFVs retain their missile slots and convert actor burst frames to 6..10 ticks");
        for(int rank:{0,2}) {
            reset();fv=vehicle();const auto victim=w.spawnUnit(1,UnitType::Rhino,18,12.5f);bool spacing=w.valid(fv) && w.valid(victim);std::vector<uint64_t> shots;
            if(spacing){w.ents[fv].vetRank=rank;w.ents[victim].tpSick=1000;spacing=w.orderAttack({fv},victim,false).accepted==1;
                for(int i=0;i<80 && shots.size()<(rank?4u:2u);++i){w.presentationEvents.clear();w.update();
                    for(const auto& event:w.presentationEvents)if(event.kind==PresentationKind::Muzzle && event.entity==fv)shots.push_back(w.tick);}
                spacing &= shots.size()==(rank?4:2);for(size_t i=1;i<shots.size();++i)spacing &= shots[i]-shots[i-1]>=6 && shots[i]-shots[i-1]<=10;}
            check(spacing,rank?"an elite public attack fires four missiles with actor-source burst intervals":"a normal public attack fires two missiles with actor-source burst intervals");
        }
        struct Crew {const char* name;const char* weapon;int mode,turret;};
        std::vector<Crew> crews{{"ENGINEER","RepairBullet",1,2},{"E1","CRM60",2,1},{"FLAKT","CRFlakGuyGun",3,1},
            {"GHOST","CRMP5",4,1},{"SNIPE","AWPE",5,1},{"SHK","CRElectricBolt",6,3},{"IVAN","CRNuke",7,3},
            {"YURI","CRMindControl",8,3},{"DESO","CRRadBeamWeapon",9,3},{"CLEG","CRNeutronRifle",10,3},{"TERROR","CRTerrorBomb",11,3}};
        if(edition[0]=='y')for(const auto c:{Crew{"INIT","CRPsychicJab",13,3},Crew{"VIRUS","CRVirusGun",14,3},
            Crew{"YURIPR","CRSuperMindBlast",15,3},Crew{"GGI","CRMissileLauncher",16,3}})crews.push_back(c);
        for(const auto& crew:crews) {
            reset();fv=vehicle();const bool entered=board(fv,crew.name,2);check(entered,std::string("public boarding retains ")+crew.name+" as the gunner");
            if(!entered)continue;
            bool selected=w.nativeIFVMode(w.ents[fv])==crew.mode && w.nativeIFVTurret(w.ents[fv])==crew.turret;
            for(int rank:{0,1,2}) {
                w.ents[fv].vetRank=rank;const auto gun=w.effWeapon(w.ents[fv]);const auto& profile=w.ents[fv].nativeIdentity.definition->ifv;
                const auto& raw=w.nativeWeaponCatalog->weapons[profile.weapons[profile.modes[crew.mode].weapon][rank>=2?1:0]];
                const float boost=rank?1.1f:1.f;
                selected &= gun.nativeSource==&raw && raw.id==crew.weapon && gun.damage==(int)(raw.weapon.damage*boost);
                auto restored=gun;const int code=w.nativeWeaponReference(gun);
                selected &= code>=2000000 && w.restoreNativeWeaponReference(restored,code,true) && restored.nativeSource==gun.nativeSource;
            }
            check(selected,std::string("vehicle rank selects and scales the source mode for ")+crew.name);
            w.ents[fv].vetRank=0;
            if(crew.mode==1 || crew.mode==10 || crew.mode==16)save("crew-"+std::to_string(crew.mode),120);
            const auto release=w.orderUnload({fv},false);
            for(int i=0;i<180 && !w.ents[fv].cargo.empty() && release.pending;++i)w.update();
            bool unloaded=(release.released==1 || release.pending==1) && w.ents[fv].cargo.empty()
                && w.nativeIFVMode(w.ents[fv])==0 && w.effWeapon(w.ents[fv]).nativeSource->id=="HoverMissile";
            check(unloaded,"public unloading restores the empty missile mode");
        }
        reset();fv=vehicle();bool repairing=board(fv,"ENGINEER");
        const auto patient=w.spawnUnit(0,UnitType::Rhino,15.5f,12.5f);
        if(repairing && w.valid(patient)) {
            w.ents[patient].hp=200;for(int i=0;i<50;++i)w.update();
            check(w.ents[patient].hp==200 && !w.valid(w.ents[fv].target),"source Range1.8 does not passively repair a vehicle at distance3");
            sel={fv};sideMode=0;placing=dragging=false;camX=-450;camY=0;camZoom=1;w.map.reveal(0,32,32,64);
            auto point=unitScreenPos(w.ents[patient]);point.y-=18;const auto hoverHash=w.checksum();
            updateHoverCursor((int)point.x,(int)point.y);const bool hover=cursorKind==CursorKind::Repair && w.checksum()==hoverHash;
            issueSmartOrder((int)point.x,(int)point.y);consumePresentationEvents();
            check(hover && w.ents[fv].target==patient && msg==TR(S::MsgRepairAccepted) && !attackFeedback,
                "the real cursor, right click and localized receipt agree on vehicle repair");
            auto result=w.orderAttack({fv},patient,true);
            check(result.accepted==1 && result.healing && w.nativeRepairIntent({fv},patient),"a public allied repair order uses the shared repair admission");
            for(int i=0;i<240 && w.valid(patient) && w.ents[patient].hp==200;++i)w.update();
            check(w.valid(patient) && w.ents[patient].hp==250,"the repairer approaches and its first shot restores exactly50");
            save("repairing",170);
            const auto first=w.tick;bool unchanged=true;
            for(int i=1;i<160;++i){w.update();unchanged &= w.ents[patient].hp==250;}
            check(unchanged && w.tick-first==159,"original ROF80 keeps the next repair shot 160 logic ticks away");
            w.update();check(w.ents[patient].hp==300,"the next repair shot follows the original source cooldown");
            for(int i=0;i<400;++i)w.update();
            check(w.ents[patient].hp==400 && w.attackRejection(fv,patient)==AttackRejectReason::FullHealth,"repair caps at source strength and then rejects a full-health patient");
            sel={fv};point=unitScreenPos(w.ents[patient]);point.y-=18;updateHoverCursor((int)point.x,(int)point.y);
            issueSmartOrder((int)point.x,(int)point.y);consumePresentationEvents();
            check(cursorKind==CursorKind::NoMove && msg==TR(S::MsgRepairFullHealth) && !attackFeedback,
                "full-health cursor and receipt stop repair without calling a vehicle a building");
            const auto infantry=spawn("E1",0,17.5f,12.5f),enemy=w.spawnUnit(1,UnitType::Rhino,18.5f,12.5f);
            const auto building=w.spawnBuilding(0,BldType::ConYard,20,10,true);
            if(w.valid(infantry))w.ents[infantry].hp=1;if(w.valid(enemy))w.ents[enemy].hp=1;if(w.valid(building))w.ents[building].hp=1;
            check(w.valid(infantry) && w.valid(enemy) && w.valid(building)
                && w.attackRejection(fv,infantry)==AttackRejectReason::Ineffective
                && w.attackRejection(fv,enemy)==AttackRejectReason::InvalidTarget
                && w.attackRejection(fv,building)==AttackRejectReason::Ineffective,
                "the vehicle repair gate excludes infantry, enemies and a deployed construction yard");
            const auto drone=w.spawnUnit(1,UnitType::TerrorDrone,w.ents[patient].x,w.ents[patient].y);
            if(w.valid(drone)){w.ents[drone].parasiting=true;w.ents[drone].parasiteHost=patient;w.ents[patient].parasite=drone;w.unitOccSync(drone);w.airOccSync(drone);}
            const bool clean=w.valid(drone) && w.orderAttack({fv},patient,false).accepted==1;
            if(clean){w.ents[fv].atkCd=0;for(int i=0;i<60 && w.valid(drone);++i)w.update();}
            check(clean && !w.valid(drone) && w.ents[patient].parasite==INVALID_EID,"a repair shot removes a parasite even when the host starts at full health");
        } else check(false,"engineer and damaged-patient fixture spawns");
        {
            const auto ifvType=type("FV"),spyType=type("SPY");
            auto changed=field(base.nativeScenarioSource,"Type."+std::to_string(spyType),"IFVPassengerMode","12");
            World::RulesSnapshot cow{changed};bool mapped=init(&cow);if(mapped)clear();fv=mapped?vehicle():INVALID_EID;
            mapped &= w.valid(fv) && board(fv,"SPY");
            check(mapped && w.nativeIFVMode(w.ents[fv])==12 && w.effWeapon(w.ents[fv]).nativeSource->id=="CowShot",
                "an embedded passenger IFVMode12 selects CowShot independently of its enum type");
            changed=field(base.nativeScenarioSource,"Type."+std::to_string(ifvType),"IFVMode2Weapon","5");
            changed=field(changed,"Type."+std::to_string(ifvType),"IFVMode2Turret","3");World::RulesSnapshot remap{changed};
            mapped=init(&remap);if(mapped)clear();fv=mapped?vehicle():INVALID_EID;mapped &= w.valid(fv) && board(fv,"E1");
            check(mapped && w.nativeIFVMode(w.ents[fv])==2 && w.nativeIFVTurret(w.ents[fv])==3 && w.effWeapon(w.ents[fv]).nativeSource->id=="AWPE",
                "logical mode, numbered weapon slot and visual turret are three independent source mappings");
            if(mapped)save("remapped-crew",120);
            bool strict=true;const auto section="Type."+std::to_string(ifvType);
            for(const auto& source:{field(base.nativeScenarioSource,section,"IFVWeapons","33"),field(base.nativeScenarioSource,section,"IFVMode2Weapon","32"),
                field(base.nativeScenarioSource,section,"IFVMode2Turret","4"),field(base.nativeScenarioSource,section,"IFVWeapon1Reference","2048"),
                field(base.nativeScenarioSource,section,"IFVImage","../fv"),field(base.nativeScenarioSource,"Type."+std::to_string(spyType),"IFVPassengerMode","31"),
                field(base.nativeScenarioSource,section,"IFVBurstDelayMin","-1"),field(base.nativeScenarioSource,section,"IFVBurstDelayMax","2"),
                field(base.nativeScenarioSource,section,"NativeIFV","3")}) {
                World copy=base;copy.map.bldOccRef=&copy.bldOcc;const auto hash=copy.checksum();strict &= !source.empty() && !copy.restoreNativeDefinitions(source) && copy.checksum()==hash;
            }
            check(strict,"bad counts, weapon/turret references, image paths and passenger modes reject atomically");
        }
        reset();fv=vehicle();const bool crew=board(fv,"TERROR");const auto victim=w.spawnUnit(1,UnitType::Rhino,13.5f,12.5f);
        bool suicide=crew && w.valid(victim);if(suicide){w.ents[victim].tpSick=1000;suicide=w.orderAttack({fv},victim,false).accepted==1;
            for(int i=0;i<100 && w.valid(fv);++i)w.update();}
        check(suicide && !w.valid(fv) && w.valid(victim) && w.ents[victim].hp<400,"the terrorist IFV fires its source area weapon and consumes the firing vehicle");
        reset();fv=vehicle();const bool psychic=board(fv,"YURI");const auto infantry=spawn("E2",1,13.5f,12.5f),tank=w.spawnUnit(1,UnitType::Rhino,14.5f,12.5f);
        bool pulse=psychic && w.valid(infantry) && w.valid(tank);if(pulse){w.ents[infantry].tpSick=1000;w.ents[tank].tpSick=1000;
            pulse=w.orderAttack({fv},infantry,false).accepted==1;for(int i=0;i<100 && w.valid(infantry);++i)w.update();}
        check(pulse && !w.valid(infantry) && w.valid(tank) && w.ents[tank].hp==400 && w.ents[tank].player==1,
            "CRMindControl is an infantry-damaging psychic pulse and does not capture a vehicle");
        reset();fv=vehicle();const auto movingTarget=w.spawnUnit(1,UnitType::Rhino,18,12.5f);
        bool inFlight=w.valid(fv) && w.valid(movingTarget);if(inFlight) {
            w.ents[movingTarget].tpSick=1000;inFlight=w.orderAttack({fv},movingTarget,false).accepted==1;
            for(int i=0;i<100 && w.projs.empty();++i)w.update();
            inFlight &= !w.projs.empty() && w.ents[fv].burstRemaining>0 && board(fv,"E1");
            inFlight &= w.ents[fv].burstRemaining==0 && w.effWeapon(w.ents[fv]).nativeSource->id=="CRM60";
            for(const auto& p:w.projs)if(p.src==fv)inFlight &= p.w.nativeSource && p.w.nativeSource->id=="HoverMissile";
        }
        check(inFlight,"boarding cancels an old missile volley while the missile already in flight retains its source weapon");
        if(inFlight){save("in-flight-mode-change",120);w.kill(fv,false);save("in-flight-dead-source",120);}
        reset();fv=vehicle();bool visuals=w.valid(fv);std::set<uint64_t> turretHashes;Vec2i common{-1,-1};int width=0,height=0;
        const auto root="assets/rules/profiles/"+w.rulesProfileId+"/ifv/fv";const auto hash=w.checksum();
        for(int turret=0;turret<4;++turret)for(int dir=0;dir<8;++dir) {
            PixBuf pixels;Vec2i anchor;const auto ok=VxlRt::renderIFV(root,4,turret,dir,0,pixels,anchor,&HOUSE_COLORS[0]);
            if(turret==0 && dir==0){common=anchor;width=pixels.w;height=pixels.h;}
            visuals &= ok && anchor.x==common.x && anchor.y==common.y && pixels.w==width && pixels.h==height;
            if(dir==0){Image image{pixels.px.data(),pixels.w,pixels.h,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};turretHashes.insert(imageHash(image));
                ExportImage(image,path("turret-"+std::to_string(turret)+".png").c_str());}
        }
        check(visuals && turretHashes.size()==4 && w.checksum()==hash,"four distinct original turrets share one origin across all eight directions without changing World");
        bool pixels=true;camZoom=1;camX=-450;camY=0;w.map.reveal(0,32,32,64);bakeTerrain();shotAtDisplaySize=true;
        for(const char* crewName:{"ENGINEER","E1","YURI"}) {
            if(!w.ents[fv].cargo.empty()) {
                const auto release=w.orderUnload({fv},false);
                for(int i=0;i<180 && !w.ents[fv].cargo.empty() && release.pending;++i)w.update();
            }
            pixels &= board(fv,crewName);const int selected=w.nativeIFVTurret(w.ents[fv]);
            const auto& body=nativeIFVSprite(w.ents[fv],false);const auto& turret=nativeIFVSprite(w.ents[fv],true);
            pixels &= body.tex.id && turret.tex.id;
            for(const auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
                SetWindowSize(size.x,size.y);const auto before=w.checksum();const auto filename=path("mode-"+std::to_string(selected)+"-"+std::to_string(size.x)+".png");
                shotFile=filename;render();Image image=LoadImage(filename.c_str());
                pixels &= image.data && image.width==size.x && image.height==size.y && w.checksum()==before;
                if(size.x==1280) {
                    auto& graphic=const_cast<Sprite&>(turret);const auto texture=graphic.tex;graphic.tex={};
                    const auto absent=filename+"-without-turret.png";shotFile=absent;render();graphic.tex=texture;
                    Image without=LoadImage(absent.c_str());int changed=0;
                    if(image.data && without.data && image.width==without.width && image.height==without.height) {
                        auto* a=LoadImageColors(image);auto* b=LoadImageColors(without);
                        if(a && b)for(int i=0;i<image.width*image.height;++i)changed += a[i].r!=b[i].r || a[i].g!=b[i].g || a[i].b!=b[i].b;
                        UnloadImageColors(a);UnloadImageColors(b);
                    }
                    pixels &= changed>50 && w.checksum()==before;UnloadImage(without);
                }
                UnloadImage(image);
            }
        }
        shotAtDisplaySize=false;shotFile.clear();check(pixels,"real boarding and ordinary rendering display the selected turret at both window sizes without mutating World");
    }
    return failures;
}
