#include "game_regression.h"
#include "world_save_sections.h"
#include "game/script.h"
#include "core/content.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>

int GameRegression::nativeCrateRegression(const char* outputDir){
    const auto scene=contentResolve("assets/maps/native/all01t/scene.ini");
    if(scene.empty() || !FileExists(scene.c_str())){std::printf("NATIVE_CRATE SKIP: export original scene\n");return 77;}
    std::filesystem::create_directories(outputDir);int failures=0;
    const auto check=[&](bool ok,const char* text){std::printf("NATIVE_CRATE %s: %s\n",ok?"PASS":"FAIL",text);std::fflush(stdout);failures+=!ok;};
    const auto resume=[&](World& a,int ticks){FILE* f=tmpfile();World b;bool exact=f && a.saveGame(f);
        if(exact){rewind(f);exact=b.loadGame(f);}if(f)fclose(f);exact &= a.checksum()==b.checksum();
        for(int i=0;i<ticks && exact;++i){a.update();b.update();exact &= a.checksum()==b.checksum();}return exact;};
    g_script.shutdown();ais.clear();phase=Phase::InGame;localPlayer=0;gameOver=victory=paused=showMenu=netGame=false;campaignMission=-1;clearPlacement();
    const char* legacy="review_out/native-campaign/legacy-native44-crates.sav";
    if(FileExists(legacy)){const bool loaded=loadGameFile(legacy);check(loaded && world.nativeAI.active && !world.nativeCrateRules.available
        && !world.nativeEventDefinition->orderedActions && resume(world,120),
        "the actual World44/Scene12 active AI save retains its old action/crate boundary and exact continuation");}
    if(!world.init(64,64,37,1,0,{Faction::Allies},0,"assets/maps/native/all01t/terrain.nmap",true,true)
        || !world.loadNativeScene(scene.c_str()) || !world.startNativeEvents()){check(false,"the authored crate scene starts");return failures;}
    EID tanya=INVALID_EID;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0
        && world.ents[id].nativeIdentity.definition && world.ents[id].nativeIdentity.definition->id=="TANY")tanya=id;
    if(!world.valid(tanya)){check(false,"the original Tanya exists");return failures;}
    const auto hero=world.ents[tanya].nativeIdentity.object;
    for(int i=0;i<35;++i)world.update();
    check(world.nativeCrateRules.available && world.nativeEventDefinition->orderedActions && world.crates.size()==4
        && std::count_if(world.crates.begin(),world.crates.end(),[&](const auto& c){return world.nativeCrateType(c.kind);})==4,
        "the original one-second trigger creates all four authored fixed-kind crates without random replacement units");
    const World initial=world;const auto sourceRules=world.nativeCrateRules;
    const auto move=[&](World& a,EID id,Vec2i point){World::Cmd c{};c.type=World::Cmd::Move;c.ids={id};c.x=point.x+.5f;c.y=point.y+.5f;a.applyCmd(0,c);};
    const auto productionDir=(std::filesystem::path(outputDir)/"paid-army").string();
    const int production=nativeProductionRegression(productionDir.c_str());if(production){check(false,"the actual paid engineer/bridge preparation succeeds");return failures+production;}
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.object==hero)tanya=id;
    const int gi=world.nativeProductionCode("E1");EID barracks=INVALID_EID;
    for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0 && world.ents[id].nativeIdentity.definition
        && world.ents[id].nativeIdentity.definition->id=="GAPILE")barracks=id;
    World::Cmd rally{};rally.type=World::Cmd::SetRally;rally.ids={barracks};rally.x=78.5f;rally.y=44.5f;world.applyCmd(0,rally);
    for(int i=0;i<24;++i){World::Cmd train{};train.type=World::Cmd::StartUnitProd;train.a=gi;world.applyCmd(0,train);}
    const int kinds[]={11,9,10,2};int next=0;std::array<bool,4> consumed{},playerCollected{};std::vector<EID> army;uint64_t trained=world.producedUnits[0];
    const auto originalCrate=[&](int slot)->const World::Crate&{return *std::find_if(initial.crates.begin(),initial.crates.end(),[&](const auto& c){return c.kind==NATIVE_CRATE_BASE+kinds[slot];});};
    const auto present=[&](int slot){const auto& original=originalCrate(slot);
        return std::any_of(world.crates.begin(),world.crates.end(),[&](const auto& c){return c.kind==original.kind && c.x==original.x && c.y==original.y;});};
    const auto attack=[&](EID id,EID target){World::Cmd c{};c.type=World::Cmd::Attack;c.ids={id};c.a=target;world.applyCmd(0,c);};
    const auto visible=[&](const World::Ent& e){const int w=e.isBuilding?bldDef(e).w:1,h=e.isBuilding?bldDef(e).h:1;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(world.map.fogAt(0,(int)e.x+x,(int)e.y+y)==FOG_VISIBLE)return true;return false;};
    for(int tick=0;tick<20000 && next<4 && world.valid(tanya);++tick){
        const auto& original=originalCrate(next);std::array<bool,4> before{};for(int j=0;j<4;++j)before[j]=present(j);
        if(!before[next])break;
        if(tick%15==0){
            army.clear();for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0
                && world.ents[id].nativeIdentity.type==gi-NATIVE_PRODUCTION_BASE && world.ents[id].nativeIdentity.object.rfind("production:0:",0)==0)army.push_back(id);
            if(next>=2)for(EID id:army){const auto& actor=world.ents[id];EID target=INVALID_EID;float best=1e9f,distance=1e9f;
                for(EID e=0;e<(EID)world.ents.size();++e){const auto& enemy=world.ents[e];
                    if(!enemy.alive || !world.isEnemy(0,enemy.player) || !visible(enemy) || !enemy.nativeIdentity.definition || enemy.nativeIdentity.definition->kind
                        || (enemy.isBuilding && world.effWeapon(enemy).damage<=0) || world.attackRejection(id,e)!=AttackRejectReason::None)continue;
                    const float d=distf(actor.x,actor.y,enemy.x+(enemy.isBuilding?bldDef(enemy).w*.5f:0),enemy.y+(enemy.isBuilding?bldDef(enemy).h*.5f:0));
                    const float score=d-(enemy.isBuilding?4:0);if(score<best){best=score;distance=d;target=e;}}
                if((actor.deployed && (target<0 || distance>6)) || (!actor.deployed && target>=0 && distance<5.8f)){
                    World::Cmd deploy{};deploy.type=World::Cmd::Deploy;deploy.ids={id};world.applyCmd(0,deploy);}
                if(target>=0){if(world.ents[id].target!=target)attack(id,target);}
                else if(!world.ents[id].deployed && world.ents[id].state!=UState::Moving)move(world,id,{88,59});
            }
            EID threat=INVALID_EID;float nearest=6;
            for(EID id=0;id<(EID)world.ents.size();++id){const auto& enemy=world.ents[id];
                if(!enemy.alive || enemy.isBuilding || !world.isEnemy(0,enemy.player) || !visible(enemy) || unitDef(enemy).isNaval() || unitDef(enemy).isAir()
                    || world.attackRejection(tanya,id)!=AttackRejectReason::None)continue;
                const float d=distf(world.ents[tanya].x,world.ents[tanya].y,enemy.x,enemy.y);if(d<nearest){nearest=d;threat=id;}}
            if(threat>=0){if(world.ents[tanya].target!=threat)attack(tanya,threat);}
            else if(world.ents[tanya].state!=UState::Moving)move(world,tanya,{original.x,original.y});
        }
        world.update();
        for(int j=0;j<4;++j)if(before[j] && !present(j)){consumed[j]=true;const auto& crate=originalCrate(j);std::string collector;
            for(const auto& actor:world.ents)if(actor.alive && !actor.isBuilding && actor.player==0 && actor.nativeIdentity.definition
                && std::abs(actor.x-crate.x-.5f)<.01f && std::abs(actor.y-crate.y-.5f)<.01f){playerCollected[j]=true;collector=actor.nativeIdentity.object;break;}
            std::printf("NATIVE_CRATE actual kind=%d tick=%llu collector=%s hero_alive=%d hp=%d paid_GI=%zu\n",kinds[j],
                (unsigned long long)world.tick,collector.c_str(),world.valid(tanya),world.valid(tanya)?world.ents[tanya].hp:0,army.size());}
        while(next<4 && consumed[next])++next;
    }
    const bool collected=next==4 && std::all_of(playerCollected.begin(),playerCollected.end(),[](bool v){return v;});
    check(world.producedUnits[0]>=trained+12,"the escort is trained through the actual native paid queue with the original base and economy");
    check(collected && world.valid(tanya) && world.nativeEvents.outcome!=NativeMissionOutcome::Defeat && world.ents[tanya].nativeIdentity.object==hero
        && world.nativeCrateMultiplier(world.ents[tanya].nativeIdentity,9)==sourceRules.data[9]
        && world.nativeCrateMultiplier(world.ents[tanya].nativeIdentity,10)==sourceRules.data[10]
        && world.nativeCrateMultiplier(world.ents[tanya].nativeIdentity,11)==sourceRules.data[11],
        "the actual authored Tanya collects speed, healing, armor and firepower through real Move commands and remains alive");
    if(!collected)return failures;
    check(world.ents[tanya].crateDmgBoost==0 && world.ents[tanya].crateArmorBoost==0 && world.ents[tanya].crateSpeedBoost==0 && resume(world,120),
        "source permanent multipliers use no prototype 45-second timers and survive exact World45 continuation");
    const auto saved=(std::filesystem::path(outputDir)/"native-crates-collected.sav").string();const auto before=world.checksum();
    check(saveGameFile(saved.c_str()) && loadGameFile(saved.c_str()) && before==world.checksum(),
        "the application restores the real collected-source state");
    const World natural=world;
    {
        World faster=natural,slower=natural;faster.map.bldOccRef=&faster.bldOcc;slower.map.bldOccRef=&slower.bldOcc;
        slower.nativeCrateBonuses[hero].speed=1;int fastAt=-1,slowAt=-1;bool exactStep=false;Vec2i goal{-1,-1};
        for(int radius=3;radius<=6 && goal.x<0;++radius)for(int y=-radius;y<=radius && goal.x<0;++y)for(int x=-radius;x<=radius;++x){
            const int tx=(int)faster.ents[tanya].x+x,ty=(int)faster.ents[tanya].y+y;
            if(std::max(std::abs(x),std::abs(y))!=radius || !faster.map.inBounds(tx,ty) || !faster.map.passable(tx,ty)
                || faster.bldBlocked(tx,ty) || faster.unitAtCell(tx,ty)!=INVALID_EID)continue;
            if(faster.orderMove({tanya},tx+.5f,ty+.5f,false).accepted && slower.orderMove({tanya},tx+.5f,ty+.5f,false).accepted){
                goal={(int)faster.ents[tanya].goalX,(int)faster.ents[tanya].goalY};break;}}
        for(int i=0;i<300 && goal.x>=0 && (fastAt<0 || slowAt<0);++i){const auto a=faster.ents[tanya],b=slower.ents[tanya];faster.update();slower.update();
            if(i==0){const float fastStep=distf(a.x,a.y,faster.ents[tanya].x,faster.ents[tanya].y),slowStep=distf(b.x,b.y,slower.ents[tanya].x,slower.ents[tanya].y);
                exactStep=slowStep>0 && std::abs(fastStep/slowStep-sourceRules.data[10])<.001f;}
            if(fastAt<0 && faster.valid(tanya) && distf(faster.ents[tanya].x,faster.ents[tanya].y,goal.x+.5f,goal.y+.5f)<.2f)fastAt=i;
            if(slowAt<0 && slower.valid(tanya) && distf(slower.ents[tanya].x,slower.ents[tanya].y,goal.x+.5f,goal.y+.5f)<.2f)slowAt=i;}
        std::printf("NATIVE_CRATE speed controlled boosted=%d baseline=%d\n",fastAt,slowAt);
        check(exactStep && fastAt>=0 && slowAt>fastAt,"source speed scales the actual first movement step by 1.2 and reaches the destination before the explicit no-speed counterbranch");
    }
    // All following branches are controlled mechanisms, separate from the real Tanya journey.
    world=initial;world.map.bldOccRef=&world.bldOcc;
    int thug=-1;for(int i=0;i<(int)world.nativeEvents.triggers.size();++i)
        if(world.nativeEventDefinition->triggers[world.nativeEvents.triggers[i].definition].id=="0b3c062c")thug=i;
    for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
    const auto teams=world.nativeEvents.teams.size();const auto point=world.nativeWaypoint(5);
    if(thug>=0)world.offerNativeTagEvent(world.nativeEvents.triggers[thug].tag,1,tanya);
    check(thug>=0 && world.nativeEvents.triggers[thug].firings==1 && world.nativeEvents.triggers[thug].blockedAction==-1
        && world.nativeEvents.teams.size()==teams+2 && world.map.fogAt(0,point.x,point.y)==FOG_UNSEEN
        && world.map.fogAt(1,point.x,point.y)==FOG_VISIBLE && resume(world,10),
        "the actual Create Thugs action list creates its two source teams, enables later triggers and reshrouds only the source player");
    world=natural;world.map.bldOccRef=&world.bldOcc;
    const int e1=world.nativeUnitIndex("E1"),e2=world.nativeUnitIndex("E2");
    Vec2i anchor{-1,-1};
    for(int y=1;y<world.map.h-1 && anchor.x<0;++y)for(int x=1;x<world.map.w-1;++x){bool clear=true;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)clear &= world.map.passable(x+dx,y+dy)
            && world.map.at(x+dx,y+dy).height==world.map.at(x,y).height && !world.bldBlocked(x+dx,y+dy) && world.unitAtCell(x+dx,y+dy)==INVALID_EID;
        if(clear){anchor={x,y};break;}}
    bool range=anchor.x>=0 && world.createNativeCrate(9,anchor);EID recipient=INVALID_EID,nearby=INVALID_EID,distant=INVALID_EID;
    if(range)for(int radius=1;radius<=5;++radius)for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x){
        if(std::max(std::abs(x),std::abs(y))!=radius)continue;const int tx=anchor.x+x,ty=anchor.y+y;
        if(!world.map.inBounds(tx,ty) || !world.map.passable(tx,ty) || world.bldBlocked(tx,ty) || world.unitAtCell(tx,ty)!=INVALID_EID)continue;
        if(nearby<0 && std::hypot((float)x,(float)y)<sourceRules.radius)nearby=world.spawnNativeActor(e2,1,tx+.5f,ty+.5f,256,0,"crate-counter:nearby-enemy","","");
        else if(distant<0 && std::hypot((float)x,(float)y)>sourceRules.radius)distant=world.spawnNativeActor(e2,1,tx+.5f,ty+.5f,256,0,"crate-counter:distant-enemy","","");
    }
    if(range){Vec2i start{-1,-1};for(int y=-1;y<=1 && start.x<0;++y)for(int x=-1;x<=1;++x)
            if((x || y) && world.unitAtCell(anchor.x+x,anchor.y+y)==INVALID_EID){start={anchor.x+x,anchor.y+y};break;}
        if(start.x>=0)recipient=world.spawnNativeActor(e1,0,start.x+.5f,start.y+.5f,256,0,"crate-counter:recipient","","");
        range=world.valid(recipient);if(range){move(world,recipient,anchor);
            for(int i=0;i<120 && world.valid(recipient) && world.nativeCrateMultiplier(world.ents[recipient].nativeIdentity,9)==1;++i)world.update();
            const int hp=world.ents[recipient].hp;
            world.damage(recipient,60,-1);range &= world.ents[recipient].hp==hp-40
                && world.nativeCrateMultiplier(world.ents[recipient].nativeIdentity,9)==1.5f && world.valid(nearby) && world.valid(distant)
                && world.nativeCrateMultiplier(world.ents[nearby].nativeIdentity,9)==1.5f && world.nativeCrateMultiplier(world.ents[distant].nativeIdentity,9)==1;}}
    check(range,"source radius upgrades a nearby enemy as well as the collector, excludes the distant unit, and divides actual received damage by 1.5");
    bool fire=false,heal=false,money=false,invulnerable=false;
    if(world.valid(recipient)){
        const int baseline=world.effWeapon(world.ents[recipient]).damage;World::Crate crate;crate.x=anchor.x;crate.y=anchor.y;crate.kind=NATIVE_CRATE_BASE+11;
        fire=world.pickupNativeCrate(world.ents[recipient],crate) && world.effWeapon(world.ents[recipient]).damage==baseline*2;
        world.pickupNativeCrate(world.ents[recipient],crate);fire &= world.effWeapon(world.ents[recipient]).damage==baseline*2;
        world.ents[recipient].vetRank=1;const int rankHealth=world.ents[recipient].hp;world.damage(recipient,7,-1);
        check(world.ents[recipient].hp==rankHealth-3 && world.effWeapon(world.ents[recipient]).damage==33,
            "source veterancy and crate factors combine before rounding: seven incoming damage becomes three, and 15 base firepower becomes 33");
        EID building=INVALID_EID;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].player==0 && world.ents[id].isBuilding
            && world.ents[id].nativeIdentity.definition && !world.ents[id].nativeIdentity.definition->kind && !world.ents[id].nativeIdentity.definition->immune){building=id;break;}
        if(world.valid(building)){world.damage(building,10,-1);crate.kind=NATIVE_CRATE_BASE+2;
            heal=world.ents[building].hp<bldDef(world.ents[building]).hp && world.pickupNativeCrate(world.ents[recipient],crate)
                && world.ents[recipient].hp==unitDef(world.ents[recipient]).hp
                && world.ents[building].hp==bldDef(world.ents[building]).hp;}
        const auto cash=world.players[0].money;crate.kind=NATIVE_CRATE_BASE;
        money=world.pickupNativeCrate(world.ents[recipient],crate) && world.players[0].money==cash+sourceRules.soloMoney;
        crate.kind=NATIVE_CRATE_BASE+13;invulnerable=world.pickupNativeCrate(world.ents[recipient],crate);const int hp=world.ents[recipient].hp;
        world.damage(recipient,60,-1);invulnerable &= world.ents[recipient].hp==hp && world.ents[recipient].invuln==60*LOGIC_FPS;
    }
    check(fire,"source firepower doubles the actual native weapon damage and does not stack on an already upgraded object");
    check(heal,"source HealBase restores both the owner's actual source infantry and building");
    check(money,"the campaign money crate grants the original SoloCrateMoney amount");
    check(invulnerable,"the authored invulnerability kind protects actual damage using its source duration data");
    check(resume(world,20),"controlled source powerups retain exact save/resume state");
    {
        World::Crate speed;speed.x=anchor.x;speed.y=anchor.y;speed.kind=NATIVE_CRATE_BASE+10;
        bool cargo=world.valid(recipient) && world.pickupNativeCrate(world.ents[recipient],speed);EID carrier=INVALID_EID;
        for(int radius=1;radius<=3 && carrier<0;++radius)for(int y=-radius;y<=radius && carrier<0;++y)for(int x=-radius;x<=radius;++x){
            const int tx=anchor.x+x,ty=anchor.y+y;if(!world.map.inBounds(tx,ty) || !world.map.passable(tx,ty)
                || world.bldBlocked(tx,ty) || world.unitAtCell(tx,ty)!=INVALID_EID)continue;
            carrier=world.spawnUnit(0,UnitType::AmphTransport,tx+.5f,ty+.5f);break;}
        cargo &= world.valid(carrier) && world.orderBoard({recipient},carrier).accepted!=0;
        for(int i=0;i<300 && cargo && world.valid(recipient);++i)world.update();
        cargo &= world.valid(carrier) && !world.ents[carrier].cargo.empty();
        if(cargo){cargo &= world.nativeCrateMultiplier(world.ents[carrier].cargo.front().nativeIdentity,9)==1.5f
                && world.nativeCrateMultiplier(world.ents[carrier].cargo.front().nativeIdentity,10)==1.2f && resume(world,20);
            world.orderUnload({carrier});for(int i=0;i<60 && !world.ents[carrier].cargo.empty();++i)world.update();
            EID restored=INVALID_EID;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.object=="crate-counter:recipient")restored=id;
            cargo &= world.valid(restored) && world.nativeCrateMultiplier(world.ents[restored].nativeIdentity,9)==1.5f
                && world.nativeCrateMultiplier(world.ents[restored].nativeIdentity,10)==1.2f && world.nativeCrateMultiplier(world.ents[restored].nativeIdentity,11)==2;}
        check(cargo,"per-object source upgrades survive real boarding, passenger save/resume and connected unloading");
        EID oldActor=INVALID_EID;for(EID id=0;id<(EID)world.ents.size();++id)if(world.valid(id) && world.ents[id].nativeIdentity.object=="crate-counter:recipient")oldActor=id;
        bool reused=world.valid(oldActor);if(reused){world.kill(oldActor,false);const auto replacement=world.spawnNativeActor(e1,0,anchor.x+.5f,anchor.y+.5f,256,0,"crate-counter:replacement","","");
            reused=replacement==oldActor && world.nativeCrateMultiplier(world.ents[replacement].nativeIdentity,9)==1
                && world.nativeCrateMultiplier(world.ents[replacement].nativeIdentity,10)==1 && world.nativeCrateMultiplier(world.ents[replacement].nativeIdentity,11)==1;}
        check(reused,"a reused entity slot cannot inherit another source object's permanent upgrades");
    }
    auto bonus=world.nativeCrateBonuses;world.nativeCrateBonuses[hero].armor=std::numeric_limits<float>::quiet_NaN();
    check(!world.validateNativeCrateState(),"nonfinite source bonus state is rejected before saving");world.nativeCrateBonuses=bonus;
    world.nativeCrateBonuses[""].speed=1;check(!world.validateNativeCrateState(),"an empty bonus identity is rejected");world.nativeCrateBonuses=bonus;
    check(!world.createNativeCrate(std::numeric_limits<int>::max(),anchor) && !world.nativeCrateType(std::numeric_limits<int>::min()),
        "invalid crate selectors are rejected without overflow");
    {
        FILE* file=tmpfile();FILE* extension=tmpfile();bool saved=file && extension && world.saveGame(file) && world.writeNativeCrateState(extension);
        const long crateEnd=extension?ftell(extension):0;if(saved)saved=world.writeNativeConditionState(extension);
        const auto bytes=[](FILE* input){std::vector<uint8_t> data;if(!input)return data;const long n=ftell(input);if(n<0)return data;
            rewind(input);data.resize((size_t)n);if(fread(data.data(),1,data.size(),input)!=data.size())data.clear();return data;};
        const auto full=bytes(file),tail=bytes(extension);if(file)fclose(file);if(extension)fclose(extension);
        const auto modern=locateWorldSaveSection(full,WorldSaveSection::AttackMove);const size_t ending=modern?modern->offset:0;
        saved &= modern && tail.size()>5 && tail.size()<=ending && std::equal(tail.begin(),tail.end(),full.begin()+ending-tail.size());
        const auto rejected=[&](const std::vector<uint8_t>& data){FILE* input=tmpfile();World loaded;bool accepted=false;
            if(input){if(fwrite(data.data(),1,data.size(),input)==data.size()){rewind(input);accepted=loaded.loadGame(input);}fclose(input);}return !accepted;};
        bool bad=saved;if(saved){const size_t start=ending-tail.size();auto data=full;data[start]=2;bad &= rejected(data);
            data=full;const uint32_t count=0xffffffff;memcpy(data.data()+start+1,&count,4);bad &= rejected(data);
            data=full;const uint32_t length=129;memcpy(data.data()+start+5,&length,4);bad &= rejected(data);
            data=full;const float nan=std::numeric_limits<float>::quiet_NaN();memcpy(data.data()+start+crateEnd-4,&nan,4);bad &= rejected(data);
            data=full;data.pop_back();bad &= rejected(data);data=full;data.push_back(0);bad &= rejected(data);
        }
        check(bad,"invalid bonus headers, sizes, keys, nonfinite binary values and truncated/trailing crate saves are rejected");
    }
    world=initial;world.map.bldOccRef=&world.bldOcc;for(auto& fog:world.map.fog)std::fill(fog.begin(),fog.end(),FOG_VISIBLE);
    syncFogKnowledge();clearPlacement();sel.clear();selBuilding=-1;camZoom=1;interpAlpha=1;bakeTerrain();
    const auto crate=world.crates.front();
    for(auto size:{Vec2i{1280,720},Vec2i{1920,1080}}){SetWindowSize(size.x,size.y);render();int x,y;tileToScreen(crate.x,crate.y,x,y);
        camX=x-(SCREEN_W-sidebarW)/2.f;camY=y-SCREEN_H/2.f;shotAtDisplaySize=true;
        shotFile=(std::filesystem::path(outputDir)/("native-crates-"+std::to_string(size.x)+".png")).string();
        const auto checksum=world.checksum();render();check(checksum==world.checksum(),"authored crates render at both display sizes without simulation mutation");}
    world=natural;world.map.bldOccRef=&world.bldOcc;
    std::printf("NATIVE_CRATE failures=%d\n",failures);return failures;
}
