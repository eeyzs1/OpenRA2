#include "game_regression.h"
#include "game/script.h"
#include <cstdio>
#include <filesystem>
#include <limits>

int GameRegression::rulesConsistencyRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);g_script.shutdown();int failures=0;
    const auto check=[&](bool ok,const char* name){failures+=!ok;std::printf("RULES_CONSISTENCY %s: %s\n",ok?"PASS":"FAIL",name);std::fflush(stdout);};
    const auto reset=[&](int edition,Faction faction=Faction::Allies) {
        resetMatchInteraction();ais.clear();campaignMission=-1;localPlayer=0;
        phase=Phase::InGame;showMenu=paused=gameOver=netGame=false;clearPlacement();
        const char* rules=edition==0?nullptr:edition==1?"assets/rules/profiles/ra2.ini":"assets/rules/profiles/yr.ini";
        bool ok=world.init(64,64,711,2,0,{faction,Faction::Soviet},0,"maps/skirmish-reference.txt",true,false,rules);
        for(auto& cell:world.map.cells)cell=Cell{};
        world.cratesEnabled=false;world.players[0].money=10000;
        const auto power=faction==Faction::Soviet?BldType::TeslaReactor:BldType::PowerPlant;
        for(int i=0;i<5;++i)world.spawnBuilding(0,power,35+4*i,4,true);
        world.spawnBuilding(1,BldType::ConYard,50,50,true);
        return ok;
    };
    const auto snapshot=[&] {
        World restored;FILE* file=tmpfile();bool equal=file && world.saveGame(file);
        if(equal){rewind(file);equal=restored.loadGame(file);}if(file)fclose(file);
        if(equal)equal=world.checksum()==restored.checksum();
        for(int tick=0;tick<90 && equal;++tick){world.update();restored.update();equal=world.checksum()==restored.checksum();}
        return equal;
    };
    check(reset(2,Faction::Soviet),"YR rules initialize for the economic fixture");
    world.spawnBuilding(0,BldType::ConYard,5,5,true);world.spawnBuilding(0,BldType::WarFactory,10,5,true);
    const int base=world.unitProductionCost(0,UnitType::Rhino);
    const auto plant=world.spawnBuilding(0,BldType::IndustrialPlant,16,5,true);
    const bool started=world.startUnitProd(0,UnitType::Rhino);
    check(world.valid(plant) && !world.players[0].lowPower() && base==900 && started
        && world.unitProductionCost(0,UnitType::Rhino)==675 && world.players[0].unitProd[1].totalCost==675,
        "a powered source industrial plant changes both the Rhino quote and committed order from 900 to 675");
    const auto cash=world.players[0].money;
    for(int tick=0;tick<80;++tick)world.update();
    const int paid=world.players[0].unitProd[1].paid;
    world.players[0].powerSabotage=300;
    check(paid>0 && world.players[0].money==cash-paid && world.unitProductionCost(0,UnitType::Rhino)==900
        && world.players[0].unitProd[1].totalCost==675,
        "actual installments use the discounted committed price; losing power changes only new quotes");
    check(snapshot(),"an active discounted order and temporary power loss resume tick-for-tick");
    world.cancelUnitProd(0,UnitType::Rhino);
    check(world.players[0].money==cash,"canceling the discounted order refunds exactly the money paid");
    world.players[0].powerSabotage=0;
    world.ents[plant].selling=true;
    check(world.unitProductionCost(0,UnitType::Rhino)==900,"a selling industrial plant no longer discounts new orders");
    world.ents[plant].selling=false;
    world.spawnBuilding(0,BldType::Barracks,5,15,true);
    bool locked=true;
    for(auto type:{UnitType::ChronoCommando,UnitType::PsiCommando,UnitType::ChronoIvan}) {
        const int selector=world.productionSelector(0,true,(int)type);
        locked &= !world.nativeProductionVisible(0,selector) && !world.startUnitProd(0,type);
    }
    check(world.players[0].stolenTech==0 && locked,"all three stolen-technology infantry stay hidden and reject production before infiltration");
    check(snapshot(),"source prices, production and technology state survive deterministic World save/resume");
    const std::string source=world.nativeScenarioSource;
    const auto modified=[&](const char* old,const char* value) {
        auto result=source;const auto at=result.find(old);
        if(at==std::string::npos)return std::string{};result.replace(at,std::char_traits<char>::length(old),value);return result;
    };
    bool malformed=true;
    for(const auto& text:{modified("RequiredStolenTech = 0","RequiredStolenTech = 8"),
            modified("UnitsCostBonus = 1","UnitsCostBonus = nan"),
            modified("SuperweaponMask = 127","SuperweaponMask = 128")}) {
        World rejected;World::RulesSnapshot rules{text,true,true};
        malformed &= !text.empty() && !rejected.init(64,64,711,2,0,{Faction::Soviet,Faction::Allies},0,
            "maps/skirmish-reference.txt",true,false,nullptr,{},&rules);
    }
    check(malformed,"invalid stolen-technology masks, non-finite cost modifiers and unknown abilities reject before starting a match");
    const auto appSave=(std::filesystem::path(outputDir)/"economy-app.sav").string();
    world.startUnitProd(0,UnitType::Rhino);const auto saved=world.checksum();
    check(saveGameFile(appSave.c_str()) && loadGameFile(appSave.c_str()) && world.checksum()==saved
        && world.players[0].unitProd[1].totalCost==675,
        "the real application save restores a source discounted production order");
    const auto replaceField=[](std::string text,const std::string& section,const std::string& key,const char* value) {
        const auto start=text.find("["+section+"]");if(start==std::string::npos)return std::string{};
        const auto end=text.find("\n[",start+1),field=text.find(key+" = ",start);
        if(field==std::string::npos || (end!=std::string::npos && field>=end))return std::string{};
        const auto line=text.find('\n',field);text.replace(field,line==std::string::npos?line:line-field,key+" = "+value);return text;
    };
    const int plantType=world.rulesTypeIndex(0,false,(int)BldType::IndustrialPlant),labType=world.rulesTypeIndex(0,false,(int)BldType::BattleLab);
    auto expensive=replaceField(source,"NativeProduction.Type."+std::to_string(plantType),"BuildingsCostBonus","100");
    expensive=replaceField(expensive,"Type."+std::to_string(labType),"Cost","10000000");
    expensive=replaceField(expensive,"NativeProduction.Type."+std::to_string(plantType),"BuildLimit","2");
    World stress;World::RulesSnapshot expensiveRules{expensive,true,true};
    bool budget=stress.init(64,64,711,2,0,{Faction::Soviet,Faction::Allies},0,"maps/skirmish-reference.txt",true,false,nullptr,{},&expensiveRules);
    if(budget) {
        for(auto& cell:stress.map.cells)cell=Cell{};
        for(int i=0;i<5;++i)stress.spawnBuilding(0,BldType::TeslaReactor,35+4*i,4,true);
        int x=3;for(auto building:{BldType::ConYard,BldType::OreRefinery,BldType::WarFactory,BldType::Radar,BldType::IndustrialPlant}){stress.spawnBuilding(0,building,x,15,true);x+=6;}
        stress.spawnBuilding(0,BldType::IndustrialPlant,34,15,true);
        stress.spawnUnit(0,UnitType::WarMiner,25.5f,25.5f);stress.players[0].money=5000;
        auto previous=g_aiBuild[(int)Faction::Soviet];g_aiBuild[(int)Faction::Soviet].enabled=true;g_aiBuild[(int)Faction::Soviet].buildOrder={"BattleLab"};
        SkirmishAI bot;bot.reset(0);for(int turn=0;turn<15;++turn)bot.update(stress);
        budget=stress.buildingProductionCost(0,BldType::BattleLab)==std::numeric_limits<int>::max() && !stress.players[0].bldProd.active;
        if(!budget)std::printf("RULES_CONSISTENCY BUDGET price=%d active=%d type=%d power=%d/%d faction=%d\n",
            stress.buildingProductionCost(0,BldType::BattleLab),stress.players[0].bldProd.active,stress.players[0].bldProd.typeIdx,
            stress.players[0].powerMade,stress.players[0].powerUsed,(int)stress.players[0].faction);
        g_aiBuild[(int)Faction::Soviet]=std::move(previous);
    }
    check(budget,"a valid high-multiplier source quote cannot wrap the AI reserve into an affordable negative budget");

    for(int edition=1;edition<=2;++edition)for(int technology=1;technology<=(edition==2?4:2);technology*=2) {
        check(reset(edition),"the infiltration fixture initializes without stolen technology");
        world.players[1].faction=technology==1?Faction::Allies:technology==2?Faction::Soviet:Faction::Yuri;
        world.spawnBuilding(0,BldType::Barracks,5,15,true);
        const char* laboratory=technology==1?"GATECH":technology==2?"NATECH":"YATECH";int type=-1;
        for(int i=0;i<(int)world.nativeTypes.size();++i)if(world.nativeTypes[i]->id==laboratory)type=i;
        const auto target=world.spawnBuilding(1,BldType::BattleLab,20,20,true,type);
        const auto spy=world.spawnUnit(0,UnitType::Spy,19.5f,21.5f);
        world.map.reveal(0,21,21,10);World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={spy};attack.a=target;
        world.bindCommand(attack);world.applyCmd(0,attack);
        for(int tick=0;tick<180 && world.valid(spy);++tick)world.update();
        check(!world.valid(spy) && world.players[0].stolenTech==technology,
            "a real spy attack grants the corresponding original technology from an uncaptured laboratory");
        bool exact=true;int unlocked=0;
        for(auto unit:{UnitType::ChronoCommando,UnitType::PsiCommando,UnitType::ChronoIvan}) {
            const int selector=world.productionSelector(0,true,(int)unit);
            const int required=world.nativeTypes[selector-NATIVE_PRODUCTION_BASE]->production.requiredStolenTech;
            if(required==technology && world.nativeProductionVisible(0,selector)) {
                ++unlocked;exact &= world.startUnitProd(0,unit);world.cancelUnitProd(0,unit);
            } else if(required!=technology)exact &= !world.nativeProductionVisible(0,selector) && !world.startUnitProd(0,unit);
        }
        check(exact && unlocked>0,"infiltration unlocks precisely the eligible source infantry, while other technology stays locked");
        check(snapshot(),"the acquired source technology persists across deterministic save/resume");
    }

    check(reset(1),"RA2 base rules initialize for ability isolation");
    world.spawnBuilding(0,BldType::BattleLab,5,5,true);
    check(!world.swAvailable(0,SWType::ForceShield),"RA2 base does not acquire the YR Force Shield from its battle lab");
    world.spawnBuilding(0,BldType::NukeSilo,20,10,true);
    check(world.swAvailable(0,SWType::Nuke),"RA2's original MultiSpecial still grants the nuclear weapon through its MultiMissile type");
    world.spawnBuilding(0,BldType::Radar,26,10,true);
    check(!world.hasSpyPlaneSource(0),"a RA2 radar does not acquire the YR spy plane support ability");
    check(reset(2),"YR support capabilities initialize");
    world.spawnBuilding(0,BldType::Radar,20,10,true);
    check(world.hasSpyPlaneSource(0),"a captured Soviet YR radar grants its actual source spy plane ability to an Allied owner");
    for(int edition=1;edition<=2;++edition)for(int rank:{0,2}) {
        check(reset(edition),"the original charge-turret weapon fixture initializes");
        const auto prism=world.spawnUnit(0,UnitType::PrismTank,20.5f,20.5f),target=world.spawnUnit(1,UnitType::Rhino,25.5f,20.5f);
        world.ents[prism].vetRank=rank;const auto weapon=world.effWeapon(world.ents[prism],&world.ents[target]);
        check(weapon.damage>=100 && weapon.range==10 && weapon.nativeSlot==(rank?2:0),
            "a source Prism Tank resolves its normal/elite numbered weapon instead of becoming unarmed");
        const int health=world.ents[target].hp;world.map.reveal(0,23,20,10);
        World::Cmd attack{};attack.type=World::Cmd::Attack;attack.ids={prism};attack.a=target;world.bindCommand(attack);world.applyCmd(0,attack);
        for(int turn=0;turn<120 && world.valid(target) && world.ents[target].hp==health;++turn)world.update();
        check(world.combatDamage[0]>0 && (!world.valid(target) || world.ents[target].hp<health),
            "a real normal/elite source Prism attack damages an actual target without injecting damage");
        check(snapshot(),"the source charge-turret attack state resumes deterministically");
    }
    for(int edition=0;edition<3;++edition) {
        check(reset(edition),"the ability lifecycle fixture initializes");
        auto weapon=world.spawnBuilding(0,BldType::WeatherDevice,8,8,true);
        world.players[0].swReady[(int)SWType::Lightning]=true;
        world.players[0].swCharge[(int)SWType::Lightning]=swDef(SWType::Lightning).chargeTime;
        world.kill(weapon,false);
        // Rebuild in the same turn: waiting for updateSW is too late.
        weapon=world.spawnBuilding(0,BldType::WeatherDevice,8,8,true);
        check(world.valid(weapon) && !world.players[0].swReady[(int)SWType::Lightning]
            && world.players[0].swCharge[(int)SWType::Lightning]==0,
            "destroying the last source clears readiness before a same-turn rebuild");
        world.players[0].swReady[(int)SWType::Lightning]=true;
        const auto backup=world.spawnBuilding(0,BldType::WeatherDevice,16,8,true);
        world.kill(weapon,false);
        check(world.valid(backup) && world.players[0].swReady[(int)SWType::Lightning],
            "destroying one of two valid sources retains the house's charged ability");
        uiTab=1;uiScroll=0;SetTargetFPS(0);sim=SimInput{};sim.active=true;
        const Vector2 abilityButton{(float)(SCREEN_W-sidebarW+26*sidebarW/171+20),260};
        sim.pos=abilityButton;sim.pressedL=true;
        BeginTextureMode(canvas);drawHUD();EndTextureMode();sim.pressedL=false;
        check(targetingSW==SWType::Lightning,"the actual defense palette click enters ready Lightning targeting in every rules choice");
        const auto before=world.checksum();sim.pressedR=true;handleInput();sim.pressedR=false;
        check(targetingSW==SWType::COUNT && before==world.checksum(),"canceling ability targeting leaves simulation and charge unchanged");
        sim.pressedL=true;BeginTextureMode(canvas);drawHUD();EndTextureMode();sim.pressedL=false;
        camX=-600;camY=200;camZoom=1;int sx=0,sy=0;worldToScreen(0,51*(TILE_H/2.f),sx,sy);
        sim.pos={(float)sx,(float)sy};sim.pressedL=true;handleInput();sim.pressedL=false;
        check(world.players[0].stormTimer>0 && !world.players[0].swReady[(int)SWType::Lightning],
            "a palette click followed by a real ground click executes and consumes the ready ability");
        world.map.reveal(0,32,32,64);bakeTerrain();uiTab=1;shotAtDisplaySize=true;
        bool rendered=true;
        for(const auto size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
            SetWindowSize(size.x,size.y);const auto hash=world.checksum();
            const auto image=(std::filesystem::path(outputDir)/("ability-"+std::to_string(edition)+"-"+std::to_string(size.x)+".png")).string();
            shotFile=image;render();Image frame=LoadImage(image.c_str());
            rendered &= frame.data && frame.width==size.x && frame.height==size.y && hash==world.checksum();UnloadImage(frame);
        }
        check(rendered,"both physical display sizes render the shared ability palette without changing simulation");
        shotAtDisplaySize=false;
        world.kill(backup,false);world.players[0].country=Country::America;
        world.spawnBuilding(0,BldType::AirForceCmd,8,8,true);
        if(world.independentSupportEnabled)world.players[0].support[(int)SupportType::AmericanParadrop]={world.supportRule(SupportType::AmericanParadrop).rechargeTicks,true};
        else world.players[0].paradropReady=true;
        targetingSW=SWType::COUNT;targetingParadrop=false;sim.pos=abilityButton;sim.pressedL=true;
        BeginTextureMode(canvas);drawHUD();EndTextureMode();sim.pressedL=false;
        check(world.hasParadropSource(0) && targetingParadrop,"the same palette retains the ready American paradrop support entry");
    }
    sim=SimInput{};shotFile.clear();SetTargetFPS(60);
    std::printf("RULES_CONSISTENCY failures=%d\n",failures);return failures;
}
