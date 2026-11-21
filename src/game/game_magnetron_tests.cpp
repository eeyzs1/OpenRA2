#include "game_regression.h"
#include <cstdio>
#include <filesystem>

int GameRegression::magnetronRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    int failures=0;
    for(int scenario=0;scenario<2;++scenario) {
        World w;w.init(64,64,20261031,2,0,{Faction::Yuri,Faction::Allies},0,"",true);
        // Retiring starting MCVs can spawn crew into already visited slots.
        for(int pass=0;pass<2;++pass)
            for(size_t id=0;id<w.ents.size();++id) if(w.ents[id].alive) w.kill((EID)id,false);
        for(const auto& e:w.ents) failures+=e.alive;
        w.effects.clear();w.projs.clear();w.presentationEvents.clear();w.evaQueue.clear();
        for(auto& cell:w.map.cells) { cell=Cell{};cell.terrain=Terrain::Clear; }
        for(auto& player:w.players) { player.defeated=false;player.money=0; }
        w.cratesEnabled=false;
        EID actor=w.spawnUnit(0,UnitType::Magnetron,10.5f,10.5f);
        EID target=w.spawnBuilding(1,BldType::ConYard,scenario?20:17,10,true);
        const auto result=w.orderAttack({actor},target);
        const bool approach=w.ents[actor].state==(scenario?UState::Chasing:UState::Attacking);
        const int initial=w.ents[target].hp;
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        bool equal=true,saved=false,inRange=true;std::vector<uint64_t> hits;
        for(int frame=0;frame<360;++frame) {
            w.update();replay.update();equal &= w.checksum()==replay.checksum();
            for(const auto& event:w.presentationEvents) if(event.kind==PresentationKind::Impact && event.entity==target) {
                hits.push_back(event.tick);
                if(scenario==0 && hits.size()==1) {
                    world=w;world.map.bldOccRef=&world.bldOcc;
                    world.map.reveal(0,16,14,30);phase=Phase::InGame;localPlayer=0;
                    gameOver=false;showMenu=false;paused=false;camZoom=1;interpAlpha=1;uiTab=0;sel={actor};
                    camX=112-(SCREEN_W-sidebarW)/2.0f;camY=56;
                    bakeTerrain();minimapTimer=0;
                    shotAtDisplaySize=true;
                    const auto hash=world.checksum();
                    for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
                        SetWindowSize(size.x,size.y);render();
                        const auto path=(std::filesystem::path(outputDir)/("building_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();
                        shotFile=path;render();
                        Image frameImage=LoadImage(path.c_str());const auto dst=letterboxDst();
                        int lx=0,ly=0;
                        for(const auto& beam:world.effects) if(beam.kind==3 && beam.aux==1) {
                            const float x=(beam.x+beam.x2)/2,y=(beam.y+beam.y2)/2;
                            worldToScreen((x-y)*(TILE_W/2.0f),(x+y)*(TILE_H/2.0f),lx,ly);ly-=13;break;
                        }
                        const int px=(int)(dst.x+lx*dst.width/SCREEN_W);
                        const int py=(int)(dst.y+ly*dst.height/SCREEN_H);
                        int pink=0;
                        for(int y=py-8;y<=py+8;++y) for(int x=px-8;x<=px+8;++x) {
                            if(x<0 || y<0 || x>=frameImage.width || y>=frameImage.height) continue;
                            Color c=GetImageColor(frameImage,x,y);pink+=c.r>150 && c.b>100 && c.g<110;
                        }
                        bool dimensions=frameImage.width==size.x && frameImage.height==size.y;
                        UnloadImage(frameImage);
                        bool visible=dimensions && pink>=2 && world.checksum()==hash;
                        failures+=!visible;
                        std::printf("MAGNETRON_VISUAL %s size=%dx%d pink=%d simulation_unchanged=%d\n",visible?"PASS":"FAIL",size.x,size.y,pink,world.checksum()==hash);
                    }
                    shotAtDisplaySize=false;
                }
                inRange &= event.damage==80 && distf(w.ents[actor].x,w.ents[actor].y,
                    w.ents[target].x+bldDef(w.ents[target].btype).w/2.0f,
                    w.ents[target].y+bldDef(w.ents[target].btype).h/2.0f)<=10.001f;
            }
            w.presentationEvents.clear();replay.presentationEvents.clear();
            if(!saved && !hits.empty()) {
                FILE* file=tmpfile();saved=file && w.saveGame(file);
                if(saved) { rewind(file);saved=replay.loadGame(file); }
                if(file) fclose(file);
                if(!saved) break;
                equal &= w.checksum()==replay.checksum();
                bool beam=false;
                for(const auto& effect:replay.effects) beam |= effect.kind==3 && effect.aux==1;
                saved &= beam;
            }
        }
        bool cadence=hits.size()>=3;
        for(size_t index=1;index<hits.size();++index) cadence &= hits[index]-hits[index-1]==110;
        bool ok=approach && result.accepted==1 && initial-w.ents[target].hp==(int)hits.size()*80
            && cadence && inRange && equal && saved;
        failures+=!ok;
        std::printf("MAGNETRON %s scenario=%d hits=%zu damage=%d range=%d cadence=%d replay=%d saved=%d\n",
            ok?"PASS":"FAIL",scenario,hits.size(),initial-w.ents[target].hp,inRange,cadence,equal,saved);
        EID vehicle=w.spawnUnit(1,UnitType::MCV,w.ents[actor].x+3,w.ents[actor].y+2);
        const int vehicleHp=w.ents[vehicle].hp;
        auto lift=w.orderAttack({actor},vehicle);
        for(int frame=0;frame<150;++frame) w.update();
        bool lifted=lift.accepted==1 && w.ents[vehicle].magneticBy==actor && w.ents[vehicle].hp==vehicleHp;
        EID infantry=w.spawnUnit(1,UnitType::GI,30.5f,30.5f);
        const auto hash=w.checksum();auto reject=w.orderAttack({actor},infantry);
        bool preserved=reject.reason==AttackRejectReason::Unsupported && w.checksum()==hash;
        w.presentationEvents.clear();World::Cmd rejected{};rejected.type=World::Cmd::Attack;rejected.ids={actor};rejected.a=infantry;
        w.applyCmd(0,rejected);
        preserved &= w.checksum()==hash && !w.presentationEvents.empty()
            && w.presentationEvents.back().kind==PresentationKind::AttackOrder
            && w.presentationEvents.back().attack.reason==AttackRejectReason::Unsupported;
        failures+=!lifted || !preserved;
        std::printf("MAGNETRON_SWITCH %s lift=%d reject_preserves=%d\n",lifted&&preserved?"PASS":"FAIL",lifted,preserved);
        if(scenario==1) {
            auto& definition=const_cast<UnitDef&>(unitDef(UnitType::Magnetron));
            const auto oldElite=definition.elite;WeaponDef elite=definition.weapon;elite.damage=9999;elite.range=2;
            definition.elite=&elite;w.ents[actor].vetRank=2;
            const auto eliteBuilding=w.effWeapon(w.ents[actor],&w.ents[target]);
            const auto eliteVehicle=w.effWeapon(w.ents[actor],&w.ents[vehicle]);
            bool selected=eliteBuilding.range==10 && eliteBuilding.damage>=80 && eliteBuilding.damage<1000
                && eliteVehicle.range==2 && eliteVehicle.damage>=9999;
            definition.elite=oldElite;w.ents[actor].vetRank=0;
            failures+=!selected;
            std::printf("MAGNETRON_ELITE %s\n",selected?"PASS":"FAIL");
            auto& secondary=const_cast<WeaponDef&>(magnetronBuildingWeapon());
            const auto original=secondary;
            const auto path=(std::filesystem::path(outputDir)/"secondary_override.ini").string();
            bool loaded=false;
            if(FILE* file=fopen(path.c_str(),"wb")) {
                fputs("[SecondaryWeapon.Magnetron]\nDamage=37\nRange=7\nCooldown=90\n",file);loaded=fclose(file)==0;
            }
            if(loaded) loadRules(path.c_str());
            auto custom=w.effWeapon(w.ents[actor],&w.ents[target]);
            auto ordered=w.orderAttack({actor},target);w.presentationEvents.clear();
            int count=0;bool damage=true;uint64_t previous=0;
            for(int frame=0;frame<400;++frame) {
                w.update();
                for(const auto& event:w.presentationEvents) if(event.kind==PresentationKind::Impact && event.entity==target) {
                    damage &= event.damage==37 && (!previous || event.tick-previous==90);
                    previous=event.tick;++count;
                }
                w.presentationEvents.clear();
            }
            bool mod=loaded && custom.damage==37 && custom.range==7 && custom.cooldown==90 && ordered.accepted==1 && count>=2 && damage;
            secondary.damage=0;w.orderStop({actor});w.kill(vehicle,false);
            const auto primary=w.effWeapon(w.ents[actor]);
            bool disabled=w.attackRejection(actor,target)==AttackRejectReason::NoWeapon
                && w.findNearestEnemy(0,w.ents[actor].x,w.ents[actor].y,20,true,&primary,UnitType::Magnetron,actor)==INVALID_EID;
            secondary=original;
            failures+=!mod || !disabled;
            std::printf("MAGNETRON_RULES %s hits=%d disabled_acquisition=%d\n",mod&&disabled?"PASS":"FAIL",count,disabled);
        }
    }
    return failures;
}
