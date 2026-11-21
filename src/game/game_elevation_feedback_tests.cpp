#include "game_regression.h"
#include "game/lang.h"
#include "gfx/sprites.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>

int GameRegression::elevationFeedbackRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    struct Restore { GameRules rules;int language;~Restore(){g_gameRules=rules;g_lang=language;} } restore{g_gameRules,g_lang};
    g_gameRules.chopperLandingTicks=40;g_gameRules.chopperDeployTicks=20;
    struct Case { int scenario,kind;bool delayed; }; // kind: deploy, attack, impact, release, entry, muzzle
    std::vector<Case> cases;
    for(int scenario=0;scenario<5;++scenario)for(int kind=0;kind<3;++kind)cases.push_back({scenario,kind,false});
    for(int kind=0;kind<3;++kind)cases.push_back({0,kind,true});
    cases.push_back({4,0,true});cases.push_back({4,2,true});
    for(int kind:{3,4})for(bool delayed:{false,true})cases.push_back({0,kind,delayed});
    for(int scenario:{5,6,7})for(bool delayed:{false,true})cases.push_back({scenario,scenario==5?2:5,delayed});
    for(const auto test:cases) {
        const int scenario=test.scenario,kind=test.kind;g_lang=(scenario+kind)&1;
        world.init(64,64,20261031,2,0,{Faction::Soviet,Faction::Allies},0,"",true);
        for(int pass=0;pass<2;++pass)for(size_t id=0;id<world.ents.size();++id)if(world.ents[id].alive)world.kill((EID)id,false);
        for(auto& cell:world.map.cells){cell=Cell{};cell.height=3;}
        for(auto& player:world.players){player.money=0;player.defeated=false;}
        world.cratesEnabled=false;world.effects.clear();world.projs.clear();world.presentationEvents.clear();combatCues.clear();
        localPlayer=0;netGame=false;phase=Phase::InGame;showMenu=false;gameOver=false;campaignMission=-1;
        sim=SimInput{};sim.active=true;sim.pos={-100,-100};interpAlpha=1;camZoom=1;
        sel.clear();selBuilding=INVALID_EID;clearPlacement();msg.clear();msgTimer=0;moveFeedbackTimer=0;
        camX=-500;camY=356;fogMaskTick=-1;shotAtDisplaySize=true;SetTargetFPS(0);
        const int owner=kind==1?1:0;
        EID actor=scenario==5?world.spawnBuilding(owner,BldType::PowerPlant,20,20,true)
            :world.spawnUnit(owner,scenario==4 || scenario==6?UnitType::Rhino:scenario==7?UnitType::Nighthawk:UnitType::SiegeChopper,20.5f,20.5f);
        world.spawnUnit(1-owner,UnitType::MCV,50.5f,50.5f);
        auto step=[&](int count){for(int i=0;i<count;++i)world.update();};
        auto deploy=[&]{World::Cmd c{};c.type=World::Cmd::Deploy;c.ids={actor};world.applyCmd(owner,c);};
        if(scenario<4) {
            deploy();
            if(scenario<2)step(scenario==0?30:10);
            else {step(60);deploy();step(20+(scenario==2?10:30));}
        } else if(scenario==4)world.ents[actor].magneticHeight=6;
        world.presentationEvents.clear();
        PresentationKind wanted=kind==0?PresentationKind::DeployOrder:kind==1?PresentationKind::AttackOrder:kind==2?PresentationKind::Impact:
            kind==3?PresentationKind::ReleaseOrder:kind==4?PresentationKind::EntryOrder:PresentationKind::Muzzle;
        if(kind==0)deploy();
        else if(kind==1) {
            EID shooter=world.spawnUnit(0,UnitType::Grizzly,24.5f,20.5f);
            World::Cmd c{};c.type=World::Cmd::Attack;c.ids={shooter};c.a=actor;world.applyCmd(0,c);
        } else if(kind==2)world.damage(actor,8,1-owner);
        else if(kind==3) {
            World::Cmd c{};c.type=World::Cmd::Unload;c.ids={actor};world.applyCmd(0,c);
        } else if(kind==4) {
            EID passenger=world.spawnUnit(0,UnitType::Engineer,22.5f,20.5f);
            World::Cmd c{};c.type=World::Cmd::Board;c.ids={passenger};c.a=actor;world.applyCmd(0,c);
        } else {
            EID target=world.spawnUnit(1,UnitType::MCV,24.5f,20.5f);world.ents[target].hp=10000;
            world.orderAttack({actor},target);
            for(int tick=0;tick<600;++tick) {
                world.update();
                if(std::any_of(world.presentationEvents.begin(),world.presentationEvents.end(),[&](const auto& event){return event.kind==wanted && event.entity==actor;}))break;
            }
        }
        auto found=std::find_if(world.presentationEvents.begin(),world.presentationEvents.end(),[&](const auto& event){return event.kind==wanted;});
        if(found==world.presentationEvents.end()){++failures;std::printf("ELEVATION_EVENT FAIL scenario=%d kind=%d missing\n",scenario,kind);continue;}
        const auto event=*found;
        const float fraction=scenario==0 || scenario==2?0.25f:scenario==1 || scenario==3?0.75f:scenario==7?1.f:0.f;
        const float lift=scenario==4?18.f:fraction*AIR_ALT;
        const bool captured=std::abs(event.flightFraction-fraction)<0.001f && event.magneticHeight==(scenario==4?6:0) && event.terrainHeight==3;
        bool reused=true;
        if(test.delayed) {
            world.kill(actor,false);const EID replacement=world.spawnUnit(owner,UnitType::Kirov,40.5f,40.5f);reused=replacement==actor;
            world.map.at((int)event.x,(int)event.y).height=0;
        }
        // Deliver the saved receipt alone; unrelated retirement receipts are separate events.
        world.presentationEvents.clear();world.presentationEvents.push_back(event);
        for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
        const auto checksum=world.checksum();consumePresentationEvents();msgTimer=0;
        const bool isolated=checksum==world.checksum();
        const bool heightCorrect=kind==2 || kind==5 || std::abs(feedbackLift-lift)<0.001f;
        bakeTerrain();fogMaskTick=-1;
        for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            if(test.delayed && size.x==1920)continue;
            SetWindowSize(size.x,size.y);
            const auto name="case"+std::to_string(scenario)+"_kind"+std::to_string(kind)+"_delayed"+std::to_string(test.delayed)+"_"+std::to_string(size.x)+"x"+std::to_string(size.y);
            const auto withPath=(std::filesystem::path(outputDir)/(name+".png")).string();shotFile=withPath;render();Image with=LoadImage(withPath.c_str());
            const float timer=moveFeedbackTimer;auto cues=std::move(combatCues);combatCues.clear();moveFeedbackTimer=0;
            const auto withoutPath=(std::filesystem::path(outputDir)/(name+"_without.png")).string();shotFile=withoutPath;render();Image without=LoadImage(withoutPath.c_str());combatCues=std::move(cues);moveFeedbackTimer=timer;
            float x=(event.x-event.y)*32-camX,y=(event.x+event.y)*16-camY-48-lift;
            const bool cue=kind==2 || kind==5;
            if(cue)y-=scenario==5?28:10;
            if(kind==5) {
                const float dx=(event.targetX-event.targetY-event.x+event.y)*32,dy=(event.targetX+event.targetY-event.x-event.y)*16;
                const float distance=std::sqrt(dx*dx+dy*dy);if(distance>0){x+=dx/distance*17;y+=dy/distance*17;}
            }
            const auto viewport=letterboxDst();const float scale=viewport.width/SCREEN_W;
            const int cx=(int)(viewport.x+x*scale),cy=(int)(viewport.y+y*scale),radius=(int)((cue?12:36)*scale);
            Color* a=LoadImageColors(with);Color* b=LoadImageColors(without);int pixels=0,minY=with.height,maxY=-1;
            if(a && b && with.width==without.width && with.height==without.height)
                for(int py=std::max(0,cy-radius);py<std::min(with.height,cy+radius+1);++py)
                    for(int px=std::max(0,cx-radius);px<std::min(with.width,cx+radius+1);++px) {
                        const auto first=a[py*with.width+px],second=b[py*with.width+px];
                        bool changed=std::abs((int)first.r-second.r)+std::abs((int)first.g-second.g)+std::abs((int)first.b-second.b)>30;
                        const bool colored=cue?first.r>150 && first.g>70:first.r>170 && first.g<150 && first.b<150;
                        if(changed && colored){++pixels;minY=std::min(minY,py);maxY=std::max(maxY,py);}
                    }
            const bool centered=kind==5 || (maxY>=0 && std::abs((minY+maxY)*0.5f-cy)<=2.0f*scale);
            const bool ok=captured && reused && isolated && heightCorrect && pixels>=6 && centered && checksum==world.checksum();failures+=!ok;
            std::printf("ELEVATION_FRAME %s scenario=%d kind=%d delayed=%d size=%dx%d captured=%d reused=%d isolated=%d height_correct=%d pixels=%d centered=%d lift=%.2f\n",
                ok?"PASS":"FAIL",scenario,kind,test.delayed,size.x,size.y,captured,reused,isolated,heightCorrect,pixels,centered,lift);
            if(a)UnloadImageColors(a);if(b)UnloadImageColors(b);UnloadImage(with);UnloadImage(without);
        }
    }
    for (UnitType type : {UnitType::Grizzly, UnitType::Rhino, UnitType::MirageTank}) {
        for (bool west : {false, true}) {
            const int owner = type == UnitType::Rhino ? 1 : 0;
            const float sign = west ? -1.f : 1.f;
            if (!world.init(64, 64, 20261031, 2, 0, {Faction::Allies, Faction::Soviet}, 0,
                    "maps/skirmish-reference.txt", true, true, "assets/rules/profiles/ra2.ini",
                    {Country::America, Country::Russia})) return failures + 1;
            for (EID id = 0; id < (EID)world.ents.size(); ++id) if (world.valid(id)) world.kill(id, false);
            for (auto& cell : world.map.cells) { cell = Cell{}; cell.height = 3; }
            for (auto& player : world.players) player.isAI = false;
            world.cratesEnabled = false;
            world.crates.clear();
            world.spawnBuilding(0, BldType::ConYard, 3, 3, true);
            world.spawnBuilding(1, BldType::ConYard, 50, 50, true);
            const EID actor = world.spawnUnit(owner, type, 20.5f, 20.5f);
            const EID target = world.spawnUnit(1 - owner, UnitType::MCV, 20.5f + sign * 4.f, 20.5f - sign * 2.f);
            if (!world.valid(actor) || !world.valid(target) || !world.usesNativeGround(world.ents[actor])) {
                std::printf("TANK_MUZZLE FAIL type=%d west=%d missing timed tank or target\n", (int)type, west);
                ++failures;
                continue;
            }
            auto& initial = world.ents[actor];
            initial.dir = initial.previousDir = west ? 4 : 0;
            initial.turretDir = initial.previousTurretDir = west ? 3 : 7;
            World::Cmd move{};
            move.type = World::Cmd::Move;
            move.ids = {actor};
            move.x = 20.5f + sign * 10.f;
            move.y = 20.5f - sign * 10.f;
            move.attackMove = true;
            world.bindCommand(move);
            world.applyCmd(owner, move);
            const bool admitted = world.ents[actor].state == UState::AttackMoving;

            PresentationEvent shot;
            for (int step = 0; step < 60 && shot.kind != PresentationKind::Muzzle; ++step) {
                world.presentationEvents.clear();
                world.update();
                for (const auto& event : world.presentationEvents) {
                    if (event.kind == PresentationKind::Muzzle && event.entity == actor) shot = event;
                }
            }
            const auto& gunner = world.ents[actor];
            const auto heading = world.nativeTurretHeading(gunner, world.tick);
            const int facing = NativeGroundMath::facing32(heading);
            const auto targetHeading = NativeGroundMath::vector(shot.targetX - shot.x, shot.targetY - shot.y);
            const float aimError = std::abs(NativeGroundMath::delta(heading, targetHeading)) * (360.f / 65536.f);
            const bool rolling = std::hypot(gunner.x - shot.x, gunner.y - shot.y) > .001f;
            if (shot.kind != PresentationKind::Muzzle || !admitted || !rolling || aimError < 15.f || aimError > 45.f
                    || (west ? (facing < 13 || facing > 19) : (facing > 3 && facing < 29))) {
                std::printf("TANK_MUZZLE FAIL type=%d west=%d shot=%d admitted=%d rolling=%d facing=%d aim_error=%.2f\n",
                    (int)type, west, shot.kind == PresentationKind::Muzzle, admitted, rolling, facing, aimError);
                ++failures;
                continue;
            }

            // Near a horizontal pose the exposed end is the turret image's outermost two columns.
            const auto& turret = g_sprites.unitTurretFacing(type, facing, world.players[owner].colorId, unitTurretFrame(gunner));
            Image silhouette = LoadImageFromTexture(turret.tex);
            Color* shape = LoadImageColors(silhouette);
            const int edge = west ? turret.visL : turret.visR;
            Vector2 tip{};
            int capPixels = 0;
            if (shape && edge > 0 && edge + 1 < silhouette.width) {
                for (int x : {edge, edge + (west ? 1 : -1)}) {
                    for (int y = 0; y < silhouette.height; ++y) {
                        if (shape[y * silhouette.width + x].a == 0) continue;
                        tip.x += x + .5f - turret.ox;
                        tip.y += y + .5f - turret.oy;
                        ++capPixels;
                    }
                }
            }
            if (shape) UnloadImageColors(shape);
            UnloadImage(silhouette);
            if (capPixels == 0) { ++failures; continue; }
            tip.x /= capPixels;
            tip.y /= capPixels;

            localPlayer = owner;
            shotAtDisplaySize = false;
            camX = (shot.x - shot.y) * 32 - 500;
            camY = (shot.x + shot.y) * 16 - 3 * 16 - 340;
            world.projs.clear();
            world.effects.clear();
            for (bool delayed : {false, true}) {
                bool reused = true;
                if (delayed) {
                    world.kill(actor, false);
                    reused = world.spawnUnit(owner, UnitType::Kirov, 40.5f, 40.5f) == actor;
                    world.map.at((int)shot.x, (int)shot.y).height = 0;
                    world.update();
                }
                for (auto& fog : world.map.fog) std::fill(fog.begin(), fog.end(), FOG_VISIBLE);
                fogMaskTick = -1;
                bakeTerrain();
                world.presentationEvents.clear();
                world.presentationEvents.push_back(shot);
                combatCues.clear();
                const auto checksum = world.checksum();
                consumePresentationEvents();
                const auto name = "tank_muzzle_" + std::to_string((int)type)
                    + (west ? "_west" : "_east") + (delayed ? "_delayed" : "");
                const auto withPath = (std::filesystem::path(outputDir) / (name + ".png")).string();
                shotFile = withPath;
                render();
                combatCues.clear();
                const auto withoutPath = (std::filesystem::path(outputDir) / (name + "_without.png")).string();
                shotFile = withoutPath;
                render();
                Image with = LoadImage(withPath.c_str()), without = LoadImage(withoutPath.c_str());
                Color* a = LoadImageColors(with), *b = LoadImageColors(without);
                Vector2 center{};
                int pixels = 0;
                if (a && b && with.width == without.width && with.height == without.height
                        && with.width >= 570 && with.height >= 400) {
                    for (int y = 280; y < 400; ++y) {
                        for (int x = 430; x < 570; ++x) {
                            const auto first = a[y * with.width + x], second = b[y * with.width + x];
                            if (first.r + first.g + first.b <= second.r + second.g + second.b + 50) continue;
                            center.x += x + .5f;
                            center.y += y + .5f;
                            ++pixels;
                        }
                    }
                }
                if (a) UnloadImageColors(a);
                if (b) UnloadImageColors(b);
                UnloadImage(with);
                UnloadImage(without);
                if (pixels > 0) { center.x /= pixels; center.y /= pixels; }
                const float gap = std::hypot(center.x - (500.f + tip.x), center.y - (340.f + tip.y));
                const bool isolated = checksum == world.checksum();
                const bool ok = pixels >= 6 && gap <= 3.f && isolated && reused;
                failures += !ok;
                std::printf("TANK_MUZZLE %s type=%d west=%d delayed=%d tick=%llu facing=%d aim_error=%.2f "
                    "pixels=%d tip=%.2f,%.2f center=%.2f,%.2f gap=%.2f isolated=%d reused=%d\n",
                    ok ? "PASS" : "FAIL", (int)type, west, delayed, (unsigned long long)shot.tick, facing, aimError,
                    pixels, 500.f + tip.x, 340.f + tip.y, center.x, center.y, gap, isolated, reused);
            }
        }
    }
    return failures;
}
