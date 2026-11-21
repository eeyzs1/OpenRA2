#include "game_regression.h"
#include <cstdio>
#include <filesystem>
#include <limits>

int GameRegression::refundRegression(const char* outputDir) {
    std::filesystem::create_directories(outputDir);int failures=0;
    auto check=[&](bool ok,const char* name) { failures+=!ok;std::printf("REFUND %s %s\n",ok?"PASS":"FAIL",name); };
    World w;w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"maps/skirmish-reference.txt",true);
    w.players[0].country=Country::America;
    w.spawnBuilding(0,BldType::ConYard,10,10,true);w.spawnBuilding(0,BldType::PowerPlant,15,10,true);
    w.spawnBuilding(0,BldType::Barracks,10,20,true);w.cratesEnabled=false;
    w.startUnitProd(0,UnitType::GI);w.startBldProd(0,BldType::PowerPlant);w.startBldProd(0,BldType::Wall);
    for(int i=0;i<20;++i) w.update();
    auto& p=w.players[0];const auto paid=p.unitProd[0].paid+p.bldProd.paid+p.defProd.paid;
    check(paid>0,"real production has paid work before the cash-cap boundary fixture");
    p.money=g_gameRules.maxMoney;
    w.cancelUnitProd(0,UnitType::GI);w.cancelBldProd(0,BldType::PowerPlant);w.cancelBldProd(0,BldType::Wall);
    check(p.money==(int64_t)g_gameRules.maxMoney+paid,"all paid unit, building and defense work is refunded above the income cap");
    Credits balance=p.money;
    check(w.creditIncome(0,1000)==0 && p.money==balance,"ordinary income never clamps an existing refunded surplus downward");
    EID plant=INVALID_EID;for(EID id=0;id<(EID)w.ents.size();++id) if(w.ents[id].alive && w.ents[id].isBuilding &&
        w.ents[id].player==0 && w.ents[id].btype==BldType::PowerPlant) { plant=id;break; }
    w.sellBuilding(plant);const Credits sale=p.money-balance;
    check(sale==bldDef(BldType::PowerPlant).cost/2,"selling an owned asset returns its value even above the income cap");
    balance=p.money;w.sellBuilding(plant);check(p.money==balance,"repeated sale cannot credit the same asset twice");
    EID refinery=w.spawnBuilding(0,BldType::OreRefinery,30,30,true);
    EID miner=w.spawnUnit(0,UnitType::ChronoMiner,28.5f,34.5f);w.ents[miner].oreLoad=4;
    const auto incomeBefore=w.harvestedIncome[0];w.orderReturnToRefinery({miner},refinery);
    for(int i=0;i<1800 && w.ents[miner].oreLoad>0;++i) w.update();
    check(w.ents[miner].oreLoad==0 && p.money==balance && w.harvestedIncome[0]==incomeBefore,
          "actual refinery unloading preserves a surplus and counts only credited mining income");
    p.money=Credits(std::numeric_limits<int32_t>::max())+1000;
    check(w.startUnitProd(0,UnitType::GI),"production can spend a balance wider than signed 32-bit");
    for(int i=0;i<20;++i) w.update();
    const int widePaid=p.unitProd[0].paid;const Credits initialWide=Credits(std::numeric_limits<int32_t>::max())+1000;
    w.changeMoney(0,10000);w.cancelUnitProd(0,UnitType::GI);
    check(widePaid>0 && p.money==initialWide+10000,"incremental payment and exact refund retain all high balance bits");
    p.money=MAX_CREDIT_BALANCE-500;w.startBldProd(0,BldType::PowerPlant);
    for(int i=0;i<20;++i) w.update();
    const Credits reserved=w.reservedProductionFunds(0);
    w.changeMoney(0,std::numeric_limits<Credits>::max());
    check(reserved>0 && p.money==MAX_CREDIT_BALANCE-reserved,
          "explicit grants reserve numerical headroom for every paid production refund");
    w.cancelBldProd(0,BldType::PowerPlant);
    check(p.money==MAX_CREDIT_BALANCE,"refund is still exact at the validated ledger boundary");
    const Credits total=p.money+w.players[1].money;
    const Credits transferred=w.transferMoney(0,1,p.money*2/5);
    check(transferred>std::numeric_limits<int32_t>::max() && p.money+w.players[1].money==total && p.money>=0,
          "large transfers conserve credited balances without 32-bit truncation");
    w.changeMoney(0,std::numeric_limits<Credits>::min());check(p.money==0,"maximum negative explicit grant cannot underflow cash");
    p.money=999999;auto lowHash=w.checksum();p.money+=(Credits{1}<<32);
    check(w.checksum()!=lowHash,"simulation checksum includes balance bits above 32-bit");
    FILE* file=tmpfile();World resumed;bool roundtrip=file && w.saveGame(file);
    if(roundtrip) { rewind(file);roundtrip=resumed.loadGame(file); }if(file) fclose(file);
    check(roundtrip && w.checksum()==resumed.checksum() && resumed.players[0].money==p.money,
          "current-version save restores wide balances exactly");
    for(int i=0;i<180 && roundtrip;++i) { w.update();resumed.update();roundtrip=w.checksum()==resumed.checksum(); }
    check(roundtrip,"wide-balance current save continues with identical tick checksums");
    world=std::move(w);world.map.bldOccRef=&world.bldOcc;ais.clear();localPlayer=0;campaignMission=-1;
    phase=Phase::InGame;showMenu=false;paused=false;gameOver=false;netGame=false;clearPlacement();
    const auto appPath=(std::filesystem::path(outputDir)/"cash.sav").string();const auto expected=world.checksum();
    bool app=saveGameFile(appPath.c_str());world.players[0].money=0;app=app && loadGameFile(appPath.c_str());
    check(app && world.checksum()==expected,"current application save restores wide cash without a stale preview");
    SetTargetFPS(0);auditHud=true;shotAtDisplaySize=true;sim=SimInput{};sim.active=true;sim.pos={-100,-100};
    msgTimer=0;sel.clear();selBuilding=INVALID_EID;camX=-500;camY=100;camZoom=1;bakeTerrain();
    bool display=true;
    for(Vec2i size:{Vec2i{1280,720},Vec2i{1920,1080}}) {
        SetWindowSize(size.x,size.y);Image low{};
        for(int scenario=0;scenario<3;++scenario) {
            const Credits value=scenario==0?999999:scenario==1?999999+(Credits{1}<<32):MAX_CREDIT_BALANCE;
            world.players[0].money=value;const auto hash=world.checksum();render();
            const auto path=(std::filesystem::path(outputDir)/("cash"+std::to_string(scenario)+"_"+
                std::to_string(size.x)+"x"+std::to_string(size.y)+".png")).string();shotFile=path;render();
            display &= world.checksum()==hash && hudMoney.value==value && hudMoney.text==std::to_string(value) &&
                hudMoney.bounds.x>=SCREEN_W-sidebarW+4 && hudMoney.bounds.x+hudMoney.bounds.width<=SCREEN_W-4 &&
                hudMoney.bounds.y>=0 && hudMoney.bounds.y+hudMoney.bounds.height<=27;
            if(scenario==0) low=LoadImage(path.c_str());
            if(scenario==1) {
                Image high=LoadImage(path.c_str());Color* a=LoadImageColors(low);Color* b=LoadImageColors(high);int differences=0;
                const int left=(SCREEN_W-sidebarW+4)*size.x/SCREEN_W,right=(SCREEN_W-4)*size.x/SCREEN_W;
                for(int y=0;y<27*size.y/SCREEN_H;++y) for(int x=left;x<right;++x) {
                    int index=y*size.x+x;if(a && b && (a[index].r!=b[index].r || a[index].g!=b[index].g || a[index].b!=b[index].b)) ++differences;
                }
                display &= a && b && differences>20;
                if(a) UnloadImageColors(a);if(b) UnloadImageColors(b);UnloadImage(high);
            }
        }
        UnloadImage(low);
    }
    check(display,"wide money display fits both resolutions and differs from its truncated low-32-bit value");
    sim=SimInput{};
    std::printf("REFUND failures=%d\n",failures);return failures;
}
