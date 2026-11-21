#include "game_regression.h"
#include "gfx/sprites.h"
#include "gfx/vxl.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>

int GameRegression::voxelAnimationRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);
    int failures=0;
    auto check=[&](bool ok,const char* name){failures+=!ok;std::printf("VOXEL_ANIMATION %s %s\n",ok?"PASS":"FAIL",name);};
    auto hash=[](const Sprite& sprite) {
        Image image=LoadImageFromTexture(sprite.tex);Color* pixels=LoadImageColors(image);uint32_t result=2166136261u;
        for(int i=0;pixels && i<image.width*image.height;++i)for(uint8_t c:{pixels[i].r,pixels[i].g,pixels[i].b,pixels[i].a})result=(result^c)*16777619u;
        if(pixels)UnloadImageColors(pixels);UnloadImage(image);return result;
    };
    Image sheet=GenImageColor(1536,576,Color{22,28,28,255});
    for(int row=0;row<3;++row) {
        const UnitType type=row==0?UnitType::Nighthawk:row==1?UnitType::SiegeChopper:UnitType::GatlingTank;
        check((row<2?g_sprites.unitBodyFrames(type):g_sprites.unitTurretFrames(type))==2,"native two-frame animation declared");
        for(int direction=0;direction<8;++direction) {
            auto sprite=[&](int frame,int player)->const Sprite& {
                return row<2?g_sprites.unitBody(type,direction,frame,player):g_sprites.unitTurret(type,direction,player,frame);
            };
            const auto& first=sprite(0,0);const auto& next=sprite(1,0);
            bool ok=hash(first)!=hash(next) && hash(first)==hash(sprite(2,0)) && hash(next)==hash(sprite(3,0))
                && first.tex.width==next.tex.width && first.tex.height==next.tex.height && first.ox==next.ox && first.oy==next.oy
                && next.visL>0 && next.visT>0 && next.visR<next.tex.width-1 && next.visB<next.tex.height-1
                && hash(next)!=hash(sprite(1,1));
            failures+=!ok;std::printf("VOXEL_VARIANT %s type=%d direction=%d first=%08x next=%08x anchor=%d,%d size=%dx%d\n",
                ok?"PASS":"FAIL",(int)type,direction,hash(first),hash(next),first.ox,first.oy,first.tex.width,first.tex.height);
            for(int frame=0;frame<2;++frame) {
                const auto& pose=sprite(frame,0);Image image=LoadImageFromTexture(pose.tex);
                ImageDraw(&sheet,image,{0,0,(float)image.width,(float)image.height},
                    {(float)(direction*192+96-pose.ox),(float)(row*192+frame*88+64-pose.oy),(float)image.width,(float)image.height},WHITE);
                UnloadImage(image);
            }
        }
    }
    ExportImage(sheet,(std::filesystem::path(outputDir)/"native_frames.png").string().c_str());UnloadImage(sheet);
    check(hash(g_sprites.unitBody(UnitType::Rhino,0,0,0))==hash(g_sprites.unitBody(UnitType::Rhino,0,19,0)),"single-frame vehicle stays static");
    check(hash(g_sprites.unitBody(UnitType::WarMiner,0,0,0))!=hash(g_sprites.unitBody(UnitType::WarMiner,0,1,0)),"miner load appearance remains separate from animation");
    check(hash(g_sprites.unitBody(UnitType::GI,0,0,0))!=hash(g_sprites.unitBody(UnitType::GI,0,1,0)),"SHP walk frame remains available");

    world.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"",true);
    for(int pass=0;pass<2;++pass)for(size_t id=0;id<world.ents.size();++id)if(world.ents[id].alive)world.kill((EID)id,false);
    for(auto& cell:world.map.cells)cell=Cell{};
    for(auto& player:world.players){player.money=0;player.defeated=false;}
    world.cratesEnabled=false;world.effects.clear();world.projs.clear();world.presentationEvents.clear();
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
    localPlayer=0;netGame=false;phase=Phase::InGame;showMenu=false;gameOver=false;campaignMission=-1;
    sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;
    sel.clear();selBuilding=INVALID_EID;clearPlacement();msg.clear();msgTimer=0;moveFeedbackTimer=0;
    camX=-550;camY=306;fogMaskTick=-1;shotAtDisplaySize=true;SetTargetFPS(0);bakeTerrain();
    for(UnitType type:{UnitType::Nighthawk,UnitType::SiegeChopper,UnitType::GatlingTank}) {
        EID actor=world.spawnUnit(0,type,20.5f,20.5f);world.ents[actor].dir=0;world.ents[actor].turretDir=0;
        for(int active=0;active<2;++active)for(int frame=0;frame<2;++frame)for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            world.tick=frame*2;
            if(type==UnitType::GatlingTank)world.ents[actor].fireAnim=active?g_gameRules.fireAnimationTicks:0;
            else world.ents[actor].state=active?UState::Idle:UState::Landed;
            const int expectedPhase=active?frame:0;
            const auto& expected=type==UnitType::GatlingTank?g_sprites.unitTurret(type,0,0,expectedPhase):g_sprites.unitBody(type,0,expectedPhase,0);
            SetWindowSize(size.x,size.y);const auto before=world.checksum();
            const auto path=(std::filesystem::path(outputDir)/(std::to_string((int)type)+"_active"+std::to_string(active)+"_frame"+std::to_string(frame)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();
            shotFile=path;render();Image actual=LoadImage(path.c_str()),source=LoadImageFromTexture(expected.tex);
            const auto& staticPose=type==UnitType::GatlingTank?g_sprites.unitTurret(type,0,0,0):g_sprites.unitBody(type,0,0,0);
            Image rest=LoadImageFromTexture(staticPose.tex);Color* restPixels=LoadImageColors(rest);
            Color* pixels=LoadImageColors(actual);Color* reference=LoadImageColors(source);int samples=0,hits=0,sensitive=0,sensitiveHits=0;
            Vector2 at=unitScreenPos(world.ents[actor]);at.y-=unitRenderLift(world.ents[actor]);const auto viewport=letterboxDst();
            for(int y=0;reference && pixels && y<source.height;++y)for(int x=0;x<source.width;++x) {
                const Color color=reference[y*source.width+x];if(color.a<245)continue;
                const int sx=(int)(viewport.x+(at.x-expected.ox+x+0.5f)*viewport.width/SCREEN_W);
                const int sy=(int)(viewport.y+(at.y-expected.oy+y+0.5f)*viewport.height/SCREEN_H);
                if(sx<0 || sy<0 || sx>=actual.width || sy>=actual.height)continue;
                const Color drawn=pixels[sy*actual.width+sx];++samples;
                const bool matches=std::abs((int)drawn.r-color.r)<24 && std::abs((int)drawn.g-color.g)<24 && std::abs((int)drawn.b-color.b)<24;
                hits+=matches;
                if(expectedPhase && restPixels && rest.width==source.width && rest.height==source.height) {
                    const Color old=restPixels[y*rest.width+x];
                    if(old.a<245 || std::abs((int)old.r-color.r)>30 || std::abs((int)old.g-color.g)>30 || std::abs((int)old.b-color.b)>30) {
                        ++sensitive;sensitiveHits+=matches;
                    }
                }
            }
            const Rectangle box=unitScreenRect(world.ents[actor]);
            const bool picked=pickUnit((int)(box.x+box.width/2),(int)(box.y+box.height/2))==actor;
            const bool phaseVisible=!expectedPhase || (sensitive>10 && sensitiveHits*100>=sensitive*85);
            const bool ok=samples>20 && hits*100>=samples*85 && phaseVisible && picked && before==world.checksum();failures+=!ok;
            std::printf("VOXEL_FRAME %s type=%d active=%d frame=%d size=%dx%d samples=%d hits=%d sensitive=%d sensitive_hits=%d picked=%d isolated=%d\n",
                ok?"PASS":"FAIL",(int)type,active,frame,size.x,size.y,samples,hits,sensitive,sensitiveHits,picked,before==world.checksum());
            if(restPixels)UnloadImageColors(restPixels);UnloadImage(rest);
            if(pixels)UnloadImageColors(pixels);if(reference)UnloadImageColors(reference);UnloadImage(actual);UnloadImage(source);
        }
        world.kill(actor,false);
    }
    // A real move changes the logical heading by one octant. Freeze only the
    // fixture's translation so these captures isolate the displayed rotation.
    SetWindowSize(1440,810);
    const EID tank=world.spawnUnit(0,UnitType::Grizzly,20.5f,20.5f);
    world.ents[tank].dir=world.ents[tank].turretDir=0;
    world.orderMove({tank},26.5f,20.5f,false);
    world.update();
    auto& turning=world.ents[tank];turning.px=turning.x;turning.py=turning.y;
    check(turning.dir==1,"actual move changes tank heading by one octant");
    std::vector<uint32_t> poses;
    for(int turnPhase=0;turnPhase<=4;++turnPhase) {
        interpAlpha=turnPhase/4.f;
        const auto before=world.checksum();
        const auto path=(std::filesystem::path(outputDir)/("turn_phase_"+std::to_string(turnPhase)+".png")).string();
        shotFile=path;render();
        Image image=LoadImage(path.c_str());
        const Vector2 at=unitScreenPos(turning);
        ImageCrop(&image,Rectangle{at.x-90,at.y-90,180,180});
        Color* pixels=LoadImageColors(image);uint32_t value=2166136261u;
        for(int i=0;pixels && i<image.width*image.height;++i)for(uint8_t c:{pixels[i].r,pixels[i].g,pixels[i].b,pixels[i].a})value=(value^c)*16777619u;
        if(pixels)UnloadImageColors(pixels);UnloadImage(image);
        if(std::find(poses.begin(),poses.end(),value)==poses.end())poses.push_back(value);
        const Rectangle box=unitScreenRect(turning);
        check(pickUnit((int)(box.x+box.width/2),(int)(box.y+box.height/2))==tank,"turning tank remains selectable");
        check(before==world.checksum(),"turn rendering leaves simulation unchanged");
    }
    std::printf("VEHICLE_TURN distinct_rendered_poses=%zu expected=5\n",poses.size());
    check(poses.size()==5,"tank heading uses intermediate rendered poses");
    for(UnitType type:{UnitType::Grizzly,UnitType::Rhino,UnitType::Apocalypse,UnitType::ChronoMiner,UnitType::WarMiner,
            UnitType::PrismTank,UnitType::MirageTank,UnitType::TeslaTank,UnitType::TankDestroyer}) {
        std::vector<uint32_t> facings;
        Vec2i origin{};int width=0,height=0;
        bool aligned=true,inside=true;
        Image strip=GenImageColor(32*96,112,Color{22,28,28,255});
        for(int facing=0;facing<32;++facing) {
            const auto& body=g_sprites.unitBodyFacing(type,facing,0,0);
            const auto value=hash(body);if(std::find(facings.begin(),facings.end(),value)==facings.end())facings.push_back(value);
            PixBuf pixels;Vec2i anchor;
            const bool rendered=VxlRt::renderBodyFacing(type,facing,0,pixels,anchor,&HOUSE_COLORS[0]);
            if(!facing){origin=anchor;width=pixels.w;height=pixels.h;}
            aligned &= rendered && anchor.x==origin.x && anchor.y==origin.y && pixels.w==width && pixels.h==height;
            inside &= body.visL>0 && body.visT>0 && body.visR<body.tex.width-1 && body.visB<body.tex.height-1;
            Image image=LoadImageFromTexture(body.tex);
            ImageDraw(&strip,image,Rectangle{0,0,(float)image.width,(float)image.height},Rectangle{(float)(facing*96+48-body.ox),(float)(88-body.oy),(float)image.width,(float)image.height},WHITE);UnloadImage(image);
            if(g_sprites.hasTurret(type)) {
                const int turretFacing=(facing+7)&31;
                PixBuf turret;Vec2i turretAnchor;
                aligned &= VxlRt::renderTurretFacing(type,turretFacing,0,turret,turretAnchor,&HOUSE_COLORS[0])
                    && turretAnchor.x==origin.x && turretAnchor.y==origin.y && turret.w==width && turret.h==height;
                const auto& part=g_sprites.unitTurretFacing(type,turretFacing,0);
                image=LoadImageFromTexture(part.tex);
                ImageDraw(&strip,image,Rectangle{0,0,(float)image.width,(float)image.height},Rectangle{(float)(facing*96+48-part.ox),(float)(88-part.oy),(float)image.width,(float)image.height},WHITE);UnloadImage(image);
            }
        }
        ExportImage(strip,(std::filesystem::path(outputDir)/("facings_"+std::to_string((int)type)+".png")).string().c_str());UnloadImage(strip);
        const bool colors=hash(g_sprites.unitBodyFacing(type,3,0,0))!=hash(g_sprites.unitBodyFacing(type,3,0,1));
        check(aligned && inside && facings.size()==32 && colors,"32 vehicle headings retain origin, bounds and house color");
        std::printf("VEHICLE_FACINGS type=%d unique=%zu aligned=%d inside=%d colors=%d\n",(int)type,facings.size(),aligned,inside,colors);
        if(type==UnitType::WarMiner || type==UnitType::ChronoMiner) {
            bool stableOrigin=true,unclipped=true,distinct=true,remapped=true;
            for(int dir=0;dir<8;++dir) {
                const auto& travel=g_sprites.unitBody(type,dir,0,0);
                const auto& unloading=g_sprites.unitUnload(type,dir,0);
                stableOrigin &= unloading.ox==travel.ox && unloading.oy==travel.oy
                    && unloading.tex.width==travel.tex.width && unloading.tex.height==travel.tex.height;
                unclipped &= unloading.visL>0 && unloading.visT>0
                    && unloading.visR<unloading.tex.width-1 && unloading.visB<unloading.tex.height-1;
                const auto dump=hash(unloading);
                distinct &= dump!=hash(travel);
                remapped &= dump!=hash(g_sprites.unitUnload(type,dir,1));
            }
            std::printf("MINER_VOXEL_UNLOAD type=%d aligned=%d inside=%d distinct=%d colors=%d\n",
                (int)type,stableOrigin,unclipped,distinct,remapped);
            check(stableOrigin && unclipped && distinct && remapped,
                "a distinct unload shape keeps the ground origin, full bounds and house color from all eight directions");
        }
    }
    turning.previousDir=7;turning.dir=0;turning.previousTurretDir=1;turning.turretDir=0;interpAlpha=.5f;
    check(unitBodyFacing(turning)==30 && unitTurretFacing(turning)==2,"body and turret wrap independently along shortest turn");
    const auto stable=world.checksum();turning.previousDir=5;turning.previousTurretDir=4;
    check(stable==world.checksum(),"derived headings do not participate in simulation checksum");
    FILE* saved=tmpfile();
    if(saved) {
        const bool written=world.saveGame(saved);rewind(saved);World resumed;
        const bool read=written && resumed.loadGame(saved);fclose(saved);
        check(read && stable==resumed.checksum() && resumed.ents[tank].previousDir==resumed.ents[tank].dir
            && resumed.ents[tank].previousTurretDir==resumed.ents[tank].turretDir,"loaded vehicle begins at its current logical heading");
    }else check(false,"create current vehicle save");
    const EID fresh=world.spawnUnit(0,UnitType::Rhino,24.5f,24.5f);
    check(world.ents[fresh].previousDir==world.ents[fresh].dir && world.ents[fresh].previousTurretDir==world.ents[fresh].turretDir,"new vehicle begins at its own heading");

    const bool loaded=world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true,true,
        "assets/rules/profiles/ra2.ini",{Country::Germany,Country::Russia});
    check(loaded,"the native tank turn fixture loads its map and rules");
    if(!loaded)return failures;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id))world.kill(id,false);
    for(auto& cell:world.map.cells)cell=Cell{};
    for(auto& player:world.players)player.isAI=false;
    world.cratesEnabled=false;world.crates.clear();world.effects.clear();
    world.spawnBuilding(0,BldType::ConYard,3,3,true);
    world.spawnBuilding(1,BldType::ConYard,50,50,true);
    fogMaskTick=-1;bakeTerrain();
    for(UnitType type:{UnitType::PrismTank,UnitType::MirageTank,UnitType::TeslaTank,UnitType::TankDestroyer}) {
        const auto definition=std::find_if(world.nativeTypes.begin(),world.nativeTypes.end(),[&](const auto& candidate) {
            return candidate->rulesOnly && !candidate->isBuilding && candidate->unit.type==type;
        });
        if(definition==world.nativeTypes.end())return 1;
        localPlayer=type==UnitType::TeslaTank?1:0;
        const auto id=world.spawnUnit(localPlayer,type,20.5f,20.5f,(int)(definition-world.nativeTypes.begin()));
        if(!world.valid(id))return 1;
        auto& unit=world.ents[id];
        check(world.usesNativeGround(unit) && !unit.nativeIdentity.definition->nativeBody,"the ordinary tank draws its timed native ground heading through the shared sprite bank");
        unit.dir=unit.previousDir=7;
        unit.turretDir=unit.previousTurretDir=3;
        World::Cmd move{};move.type=World::Cmd::Move;move.ids={id};move.x=26.5f;move.y=20.5f;
        world.bindCommand(move);world.applyCmd(localPlayer,move);
        const bool admitted=unit.state==UState::Moving;
        int previousBody=28,previousTurret=12,bodyStep=0,turretStep=0,middlePose=-1;
        std::array<bool,32> bodyPoses{},turretPoses{};
        const bool turreted=g_sprites.hasTurret(type);
        bool arrivedFacing=false,captured=false;
        for(int t=0;t<160 && !arrivedFacing;++t) {
            world.update();
            const auto hull=world.nativeHullHeading(unit,world.tick);
            for(int sample=0;sample<=4;++sample) {
                interpAlpha=sample/4.f;
                const int body=unitBodyFacing(unit),turret=unitTurretFacing(unit);
                bodyStep=std::max(bodyStep,std::abs(((body-previousBody+16)&31)-16));
                turretStep=std::max(turretStep,std::abs(((turret-previousTurret+16)&31)-16));
                bodyPoses[body]=true;turretPoses[turret]=true;
                previousBody=body;previousTurret=turret;
                const auto raw=uint16_t(unit.previousHullHeading+std::lround(NativeGroundMath::delta(unit.previousHullHeading,hull)*interpAlpha));
                if(middlePose<0 && NativeGroundMath::facing32(raw)==31) {
                    middlePose=body;
                    const auto before=world.checksum();
                    const auto path=(std::filesystem::path(outputDir)/("native-turn-"+std::to_string((int)type)+".png")).string();
                    shotFile=path;render();shotFile.clear();
                    const auto box=unitScreenRect(unit);
                    captured=FileExists(path.c_str()) && before==world.checksum()
                        && pickUnit((int)(box.x+box.width/2),(int)(box.y+box.height/2))==id;
                }
            }
            arrivedFacing=hull==16384 && (!turreted || world.nativeTurretHeading(unit,world.tick)==16384) && unit.x>20.5f;
        }
        const auto bodies=std::count(bodyPoses.begin(),bodyPoses.end(),true);
        const auto turrets=std::count(turretPoses.begin(),turretPoses.end(),true);
        std::printf("NATIVE_TANK_POSES type=%d hull=%d turret=%d hull_step=%d turret_step=%d midpoint=%d screenshot=%d\n",
            (int)type,(int)bodies,turreted?(int)turrets:0,bodyStep,turreted?turretStep:0,middlePose,captured);
        check(admitted && arrivedFacing && bodyStep==1 && bodies==9 && (!turreted || (turretStep==1 && turrets==9))
            && middlePose==31 && captured,"native ninety-degree tank turns render every intermediate hull and turret pose and remain selectable");
        world.kill(id,false);
    }
    interpAlpha=1;
    check(g_sprites.assetsOk(),"native voxel animations have no missing assets");
    return failures;
}
