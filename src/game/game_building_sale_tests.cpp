#include "game_regression.h"
#include "game/native_scene_contract.h"
#include "game/script.h"
#include "scoped_user_file.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
World saleFixture(const char* profile="assets/rules/profiles/ra2.ini",bool sovietFirst=false) {
    const std::vector<Faction> sides=sovietFirst?std::vector<Faction>{Faction::Soviet,Faction::Allies}:std::vector<Faction>{Faction::Allies,Faction::Soviet};
    const std::vector<Country> countries=sovietFirst?std::vector<Country>{Country::Russia,Country::America}:std::vector<Country>{Country::America,Country::Russia};
    World w;w.init(64,64,20261124,2,0,sides,0,nullptr,false,false,profile,countries);
    w.cratesEnabled=false;w.crates.clear();
    for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id))w.kill(id,false);
    for(auto& cell:w.map.cells)cell=Cell{};
    for(auto& p:w.players){p.money=0;p.defeated=false;p.isAI=false;}
    w.spawnBuilding(0,BldType::ConYard,48,48,true);w.spawnBuilding(1,BldType::ConYard,2,48,true);
    return w;
}
int saleType(const World& w,const char* name) {
    for(int i=0;i<(int)w.nativeTypes.size();++i)if(w.nativeTypes[i]->id==name)return i;
    return -1;
}
void clearSaleFeedback(World& w){w.presentationEvents.clear();w.evaQueue.clear();}
void saleCommand(World& w,int player,EID id) {
    World::Cmd c{};c.type=World::Cmd::SellBuilding;c.ids={id};w.bindCommand(c);w.applyCmd(player,c);
}
bool saleSave(World& w,World& copy,const std::filesystem::path& path) {
    FILE* f=std::fopen(path.string().c_str(),"w+b");bool ok=f && w.saveGame(f);
    if(ok){std::rewind(f);ok=copy.loadGame(f) && w.checksum()==copy.checksum();}
    if(f)std::fclose(f);return ok;
}
struct SaleCase {const char* name;const char* type;int owner;bool ai,damaged;int refund;int repacks=-1,initialOwner=-1;};
constexpr SaleCase saleCases[]{
    {"allied-power","GAPOWR",0,false,false,400},
    {"allied-power-damaged","GAPOWR",0,false,true,400},
    {"soviet-power","NAPOWR",1,false,false,300},
    {"allied-refinery","GAREFN",0,false,false,300},
    {"allied-refinery-damaged","GAREFN",0,false,true,300},
    {"soviet-refinery","NAREFN",1,false,false,300},
    {"purifier-soylent","GAOREP",0,false,false,1250},
    {"prism","ATESLA",0,false,false,750},
    {"ai-power","GAPOWR",0,true,false,800},
    {"ai-refinery","GAREFN",0,true,false,300},
    {"allied-conyard","GACNST",0,false,false,1500,0},
    {"soviet-conyard","NACNST",1,false,false,1500,0},
    {"allied-conyard-repacks","GACNST",0,false,false,1500,1},
    {"soviet-conyard-repacks","NACNST",1,false,false,1500,1},
    {"allied-conyard-damaged","GACNST",0,false,true,1500,0},
    {"ai-allied-conyard","GACNST",0,true,false,3000,1},
    {"ai-soviet-conyard","NACNST",1,true,false,3000,0},
    {"captured-soviet-conyard","NACNST",0,false,false,1500,1,1},
};
}

int GameRegression::buildingSaleRegression(const char* outputDir) {
    ScopedUserFile settings("settings.ini");if(!settings.ready())return 1;
    const std::filesystem::path out(outputDir);std::filesystem::create_directories(out);int failures=0;
    const auto check=[&](bool ok,const char* kind,const char* name){failures+=!ok;std::printf("BUILDING_SALE %s %s %s\n",ok?"PASS":"FAIL",kind,name);std::fflush(stdout);};
    selectRulesChoice(RulesChoice::RA2);
    for(const auto& c:saleCases) {
        World w=saleFixture();w.map.bldOccRef=&w.bldOcc;const int type=saleType(w,c.type);
        if(type<0){check(false,"SETUP",c.name);continue;}
        const auto& def=*w.nativeTypes[type];const EID id=w.spawnBuilding(c.initialOwner<0?c.owner:c.initialOwner,def.building.type,20,20,true,type);
        if(!w.valid(id)){check(false,"SETUP",c.name);continue;}
        if(c.initialOwner>=0 && c.initialOwner!=c.owner){w.changeActorOwner(id,c.owner);w.synchronizeBuildingOwners();}
        w.players[c.owner].isAI=c.ai;w.players[c.owner].money=10000;
        if(c.repacks>=0)w.mcvRepacks=c.repacks!=0;
        if(c.damaged)w.ents[id].hp=std::max(1,w.ents[id].hp/2);
        const auto ref=w.entityRef(id);saleCommand(w,c.owner,id);
        const bool admitted=w.valid(ref) && w.ents[id].selling && !w.presentationEvents.empty() && w.presentationEvents.back().building.accepted;
        const int duration=g_gameRules.buildingAnimationTicks+1;
        bool behavior=w.players[c.owner].money==10000;
        World copy,middle;bool exact=saleSave(w,copy,out/(std::string(c.name)+"-ordered.sav")),mid=true,midActive=false;
        std::ofstream trace(out/(std::string(c.name)+".trace"));trace<<w.checksum()<<'\n';std::ofstream(out/(std::string(c.name)+".case"))<<id<<'\n';
        int retired=-1,credited=w.players[c.owner].money!=10000?0:-1;bool savedFinal=false;
        for(int at=1;at<=600;++at) {
            w.update();if(exact){copy.update();exact=w.checksum()==copy.checksum();}
            if(midActive && mid){middle.update();mid=w.checksum()==middle.checksum();}
            if(!w.valid(ref) && retired<0)retired=at;
            if(w.players[c.owner].money!=10000 && credited<0)credited=at;
            behavior &= w.players[c.owner].money==10000+(at>=duration?c.refund:0);
            trace<<w.checksum()<<'\n';
            if(at==10){mid=saleSave(w,middle,out/(std::string(c.name)+"-midway.sav"));midActive=true;}
            if(!savedFinal && !w.valid(ref)){World final;savedFinal=saleSave(w,final,out/(std::string(c.name)+"-sold.sav"));}
            clearSaleFeedback(w);clearSaleFeedback(copy);clearSaleFeedback(middle);
        }
        if(!savedFinal){World final;savedFinal=saleSave(w,final,out/(std::string(c.name)+"-active.sav"));}
        std::ofstream(out/(std::string(c.name)+".final"))<<w.checksum()<<'\n';
        std::printf("BUILDING_SALE OBSERVED name=%s scene=%d admitted=%d credited=%d retired=%d duration=%d gained=%lld expected=%d replay=%d mid=%d\n",
            c.name,w.nativeRulesBaseVersion,admitted,credited,retired,duration,(long long)(w.players[c.owner].money-10000),c.refund,exact,mid);
        check(admitted && behavior && retired==duration && credited==duration,"COMMAND",c.name);
        check(exact && mid && savedFinal,"REPLAY",c.name);
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_BUILDING_SALE61_INPUT_DIR"))for(size_t index=0;index<10;++index) {
        const auto& c=saleCases[index];
        const std::filesystem::path old(directory);World w;
        FILE* f=std::fopen((old/(std::string(c.name)+"-ordered.sav")).string().c_str(),"rb");bool exact=f && w.loadGame(f);if(f)std::fclose(f);
        std::ifstream trace(old/(std::string(c.name)+".trace")),final(old/(std::string(c.name)+".final"));uint64_t expected=0;
        exact &= w.nativeRulesBaseVersion==61 && bool(trace>>expected) && w.checksum()==expected;
        for(int at=1;at<=600 && exact;++at){w.update();exact=bool(trace>>expected) && w.checksum()==expected;clearSaleFeedback(w);}
        exact &= bool(final>>expected) && w.checksum()==expected;check(exact,"LEGACY61",c.name);
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_CONYARD62_INPUT_DIR")) {
        const auto replay=[&](const std::string& name) {
            const std::filesystem::path old(directory);World w;
            FILE* f=std::fopen((old/(name+"-ordered.sav")).string().c_str(),"rb");bool exact=f && w.loadGame(f);if(f)std::fclose(f);
            std::ifstream trace(old/(name+".trace")),final(old/(name+".final"));uint64_t expected=0;
            exact &= w.nativeRulesBaseVersion==62 && bool(trace>>expected) && w.checksum()==expected;
            for(int at=1;at<=600 && exact;++at){w.update();exact=bool(trace>>expected) && w.checksum()==expected;clearSaleFeedback(w);}
            exact &= bool(final>>expected) && w.checksum()==expected;check(exact,"LEGACY62",name.c_str());
        };
        for(const auto& c:saleCases)replay(c.name);
        for(const char* type:{"GACNST","NACNST"})for(bool repacks:{false,true})for(bool hotkey:{false,true})
            replay(std::string("ui-")+type+(repacks?"-repacks":"-fixed")+(hotkey?"-key":"-dollar"));
    }
    {
        World w=saleFixture();w.map.bldOccRef=&w.bldOcc;w.players[0].money=10000;
        const EID id=w.spawnBuilding(0,BldType::PowerPlant,20,20,true,saleType(w,"GAPOWR"));
        w.ents[id].hp=700;w.ents[id].repairing=true;saleCommand(w,1,id);
        bool correct=!w.ents[id].selling && w.presentationEvents.back().building.reason==BuildingRejectReason::WrongOwner;
        saleCommand(w,0,id);correct &= w.ents[id].selling && !w.ents[id].repairing;
        for(int at=0;at<10;++at)w.update();const int remaining=w.ents[id].constructAnim;
        saleCommand(w,0,id);correct &= w.presentationEvents.back().building.reason==BuildingRejectReason::Selling;
        World::Cmd repair{};repair.type=World::Cmd::RepairBuilding;repair.ids={id};w.bindCommand(repair);w.applyCmd(0,repair);
        correct &= w.presentationEvents.back().building.reason==BuildingRejectReason::Selling && !w.ents[id].repairing
            && w.ents[id].constructAnim==remaining && w.players[0].money==10000;
        for(int at=0;at<=remaining;++at)w.update();
        correct &= !w.valid(id) && w.players[0].money==10400;
        World saved;correct &= saleSave(w,saved,out/"control-repeated.sav");
        check(correct,"CONTROL","foreign-repeat-repair-and-one-credit");
    }
    {
        World w=saleFixture();w.map.bldOccRef=&w.bldOcc;w.players[0].money=10000;
        const EID id=w.spawnBuilding(0,BldType::PowerPlant,20,20,true,saleType(w,"GAPOWR"));
        const auto old=w.entityRef(id);World::Cmd stale{};stale.type=World::Cmd::SellBuilding;stale.ids={id};w.bindCommand(stale);
        saleCommand(w,0,id);for(int at=0;at<10;++at)w.update();w.kill(id,false);
        const EID replacement=w.spawnBuilding(0,BldType::PowerPlant,20,20,true,saleType(w,"GAPOWR"));
        w.applyCmd(0,stale);bool correct=!w.valid(old) && w.valid(replacement) && !w.ents[replacement].selling;
        for(int at=0;at<600;++at){w.update();clearSaleFeedback(w);}
        correct &= w.valid(replacement) && w.players[0].money==10000;
        World saved;correct &= saleSave(w,saved,out/"control-destroyed.sav");
        check(correct,"CONTROL","destroyed-sale-and-stale-command");
    }
    for(int owner:{1,-1}) {
        World w=saleFixture();w.map.bldOccRef=&w.bldOcc;w.players[0].money=w.players[1].money=10000;
        const EID id=w.spawnBuilding(0,BldType::PowerPlant,20,20,true,saleType(w,"GAPOWR"));
        saleCommand(w,0,id);for(int at=0;at<10;++at)w.update();
        w.changeActorOwner(id,owner);w.synchronizeBuildingOwners();w.players[1].isAI=true;
        const auto name=owner==1?"current-owner":"neutral-owner";World saved;
        bool exact=saleSave(w,saved,out/(std::string("control-")+name+".sav"));
        std::ofstream trace(out/(std::string("control-")+name+".trace"));trace<<w.checksum()<<'\n';
        for(int at=0;at<600;++at){w.update();saved.update();exact &= w.checksum()==saved.checksum();trace<<w.checksum()<<'\n';clearSaleFeedback(w);clearSaleFeedback(saved);}
        check(exact && !w.valid(id) && w.players[0].money==10000 && w.players[1].money==10000+(owner==1?800:0),"CONTROL",name);
    }
    for(bool saturate:{false,true}) {
        World w=saleFixture();w.map.bldOccRef=&w.bldOcc;
        const Credits initial=saturate?MAX_CREDIT_BALANCE-100:Credits(g_gameRules.maxMoney);
        w.players[0].money=initial;const EID id=w.spawnBuilding(0,BldType::PowerPlant,20,20,true,saleType(w,"GAPOWR"));
        saleCommand(w,0,id);bool correct=w.players[0].money==initial;
        for(int at=0;at<600;++at){w.update();clearSaleFeedback(w);}
        const auto name=saturate?"balance-saturation":"protected-refund-above-income-cap";
        World saved;correct &= !w.valid(id) && w.players[0].money==initial+(saturate?100:400)
            && saleSave(w,saved,out/(std::string("control-")+name+".sav"));
        check(correct,"CONTROL",name);
    }
    {
        World w=saleFixture();w.map.bldOccRef=&w.bldOcc;w.players[0].money=10000;
        const EID id=w.spawnBuilding(0,BldType::PowerPlant,20,20,true,saleType(w,"GAPOWR"));saleCommand(w,0,id);
        for(int at=0;at<g_gameRules.buildingAnimationTicks;++at)w.update();
        bool exact=w.valid(id) && w.ents[id].selling && w.ents[id].constructAnim==0 && w.players[0].money==10000;
        World saved;exact &= saleSave(w,saved,out/"completion-pending.sav");
        world=std::move(w);world.map.bldOccRef=&world.bldOcc;ais.clear();
        const auto before=world.checksum();const auto app=out/"sale-app11.sav";auto copy=std::make_unique<Game>();
        exact &= saveGameFile(app.string().c_str()) && copy->loadGameFile(app.string().c_str()) && copy->world.checksum()==before;
        std::ifstream in(app,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(in),{}};int rejected=0;
        const auto declared="Version = "+std::to_string(nativeCurrentRulesSceneVersion);
        const std::pair<std::string,std::string> invalid[]{
            {declared,"Version = "+std::to_string(nativeCurrentRulesSceneVersion+1)},{declared,"Version = 61"},
            {"BuildingSaleHuman = 400","BuildingSaleHuman = -01"},{"BuildingSaleOther = 800","BuildingSaleOther = -01"},
            {"BuildingSaleHuman = 400","BuildingSaleHuman = xxx"},{"BuildingSaleOther = 800","XuildingSaleOther = 800"},
            {"BuildingSaleHuman = 0","BuildingSaleHuman = 1"},{"BuildingSaleOther = 0","BuildingSaleOther = 1"}};
        for(const auto& [from,to]:invalid) {
            auto broken=bytes;const auto at=broken.find(from);bool atomic=at!=std::string::npos;
            if(atomic)broken.replace(at,from.size(),to);
            const auto file=out/("sale-rejected-"+std::to_string(rejected)+".sav");
            {std::ofstream output(file,std::ios::binary);output.write(broken.data(),broken.size());atomic &= bool(output);}
            atomic &= !loadGameFile(file.string().c_str()) && world.checksum()==before;exact &= atomic;rejected+=atomic;
        }
        std::ofstream trace(out/"sale-app.trace");trace<<world.checksum()<<'\n';
        for(int at=0;at<600 && exact;++at){world.update();copy->world.update();saved.update();exact=world.checksum()==copy->world.checksum() && world.checksum()==saved.checksum();trace<<world.checksum()<<'\n';clearSaleFeedback(world);clearSaleFeedback(copy->world);clearSaleFeedback(saved);}
        std::ofstream(out/"sale-app.final")<<world.checksum()<<'\n';
        check(exact && rejected==8 && !world.valid(id) && world.players[0].money==10400,"APP","completion-boundary-and-atomic-rejections");
    }
    for(const auto& source:{std::pair{"GAREFN","CMIN"},std::pair{"NAREFN","HARV"}}) {
        const int player=source.first[0]=='G'?0:1;World w=saleFixture();w.map.bldOccRef=&w.bldOcc;
        const int power=saleType(w,player==0?"GAPOWR":"NAPOWR");
        if(power<0){check(false,"SETUP",source.first);continue;}
        const EID powerId=w.spawnBuilding(player,w.nativeTypes[power]->building.type,player==0?48:2,43,true,power);
        w.synchronizeBuildingOwners();w.players[player].money=10000;
        const int selector=w.nativeProductionCode(source.first);
        if(selector<NATIVE_PRODUCTION_BASE){check(false,"SETUP",source.first);continue;}
        const auto rejection=w.productionStartRejection(player,false,selector);
        bool correct=w.valid(powerId) && rejection==ProductionRejectReason::None;
        std::printf("BUILDING_SALE PRODUCTION_START type=%s power=%s valid=%d rejection=%d\n",source.first,w.nativeTypes[power]->id.c_str(),w.valid(powerId),(int)rejection);
        World::Cmd start{};start.type=World::Cmd::StartBldProd;start.a=selector;w.applyCmd(player,start);
        int productionTicks=0;for(;productionTicks<6000 && !w.players[player].bldProd.ready;++productionTicks){w.update();clearSaleFeedback(w);}
        correct &= w.players[player].bldProd.ready && w.players[player].bldProd.paid==2000 && w.players[player].money==8000;
        const int paid=w.players[player].bldProd.paid;
        Vec2i site{-1,-1};const auto& def=*w.nativeTypes[selector-NATIVE_PRODUCTION_BASE];
        for(int y=0;y<w.map.h && site.x<0;++y)for(int x=0;x<w.map.w && site.x<0;++x)
            if(w.canPlaceNativeBuilding(player,selector,x,y) && w.map.inBounds(x+def.building.w/2,y+def.building.h)
                && w.unitAtCell(x+def.building.w/2,y+def.building.h)==INVALID_EID)site={x,y};
        correct &= site.x>=0;
        if(site.x>=0){World::Cmd place{};place.type=World::Cmd::PlaceBuilding;place.a=selector;place.x=(float)site.x;place.y=(float)site.y;w.applyCmd(player,place);}
        const EID id=site.x>=0?w.bldAt(site.x,site.y):INVALID_EID;
        const auto miners=[&](){int count=0;for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.player==player && e.nativeIdentity.definition && e.nativeIdentity.definition->id==source.second)++count;return count;};
        correct &= w.valid(id) && miners()==1 && !w.players[player].bldProd.active;
        if(w.valid(id)) {
            for(int at=0;at<g_gameRules.buildingAnimationTicks+1;++at)w.update();
            saleCommand(w,player,id);correct &= w.players[player].money==8000;
            World copy;bool exact=saleSave(w,copy,out/(std::string("paid-")+source.first+"-ordered.sav"));
            std::ofstream trace(out/(std::string("paid-")+source.first+".trace"));trace<<w.checksum()<<'\n';
            for(int at=0;at<600;++at){w.update();copy.update();exact &= w.checksum()==copy.checksum();trace<<w.checksum()<<'\n';clearSaleFeedback(w);clearSaleFeedback(copy);}
            correct &= exact && !w.valid(id) && w.players[player].money==8300 && miners()==1;
            World sold;correct &= saleSave(w,sold,out/(std::string("paid-")+source.first+"-sold.sav"));
        }
        std::printf("BUILDING_SALE PRODUCTION type=%s ticks=%d paid=%d cash=%lld miners=%d site=%d,%d\n",source.first,productionTicks,paid,(long long)w.players[player].money,miners(),site.x,site.y);
        check(correct,"JOURNEY",source.first);
    }
    for(auto choice:{RulesChoice::YR,RulesChoice::Fusion}) {
        selectRulesChoice(choice);World w=saleFixture(choice==RulesChoice::YR?"assets/rules/profiles/yr.ini":nullptr);w.map.bldOccRef=&w.bldOcc;
        EID yard=INVALID_EID;for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].isBuilding && w.ents[id].player==0 && w.ents[id].btype==BldType::ConYard){yard=id;break;}
        saleCommand(w,0,yard);check(w.valid(yard) && !w.ents[yard].selling && !w.constructionYardSaleEnabled()
            && w.presentationEvents.back().building.reason==BuildingRejectReason::ConstructionYard,"LEGACY_YARD",choice==RulesChoice::YR?"yr43-denied":"fusion-denied");
        w.players[0].money=10000;const EID id=w.spawnBuilding(0,BldType::OreRefinery,20,20,true);const int refund=bldDef(w.ents[id]).cost/2;
        saleCommand(w,0,id);bool correct=w.players[0].money==10000+refund;
        for(int at=0;at<600;++at){w.update();correct &= w.players[0].money==10000+refund;clearSaleFeedback(w);}
        check(correct && !w.valid(id),"LEGACY",choice==RulesChoice::YR?"yr43-immediate":"fusion-immediate");
    }
    {
        selectRulesChoice(RulesChoice::RA2);World w=saleFixture();w.map.bldOccRef=&w.bldOcc;w.players[0].money=10000;
        w.spawnBuilding(0,BldType::PowerPlant,20,20,true,saleType(w,"GAPOWR"));
        EID yard=INVALID_EID;for(EID id=0;id<(EID)w.ents.size();++id)if(w.valid(id) && w.ents[id].isBuilding && w.ents[id].player==0 && w.ents[id].btype==BldType::ConYard){yard=id;break;}
        const int selector=w.nativeProductionCode("GAPOWR");World::Cmd start{};start.type=World::Cmd::StartBldProd;start.a=selector;w.applyCmd(0,start);
        for(int at=0;at<6000 && !w.players[0].bldProd.paid;++at){w.update();clearSaleFeedback(w);}
        const auto paid=w.players[0].bldProd;bool correct=w.valid(yard) && paid.active && paid.paid>0 && paid.progress<54;
        saleCommand(w,0,yard);correct &= w.valid(yard) && w.ents[yard].selling
            && w.productionStartRejection(0,false,selector)==ProductionRejectReason::MissingFactory;
        World copy;bool exact=saleSave(w,copy,out/"last-yard-paid-order.sav");
        std::ofstream trace(out/"last-yard-paid-order.trace");trace<<w.checksum()<<'\n';
        for(int at=1;at<=600;++at) {
            if(at==100){World::Cmd cancel{};cancel.type=World::Cmd::CancelBldProd;cancel.a=selector;w.applyCmd(0,cancel);copy.applyCmd(0,cancel);}
            w.update();copy.update();exact &= w.checksum()==copy.checksum();trace<<w.checksum()<<'\n';
            const auto& order=w.players[0].bldProd;
            if(at<100)correct &= order.active && order.progress==paid.progress && order.paid==paid.paid && order.workRemainder==paid.workRemainder;
            else correct &= !order.active;
            correct &= w.players[0].money==10000-(at<100?paid.paid:0)+(at>g_gameRules.buildingAnimationTicks?1500:0);
            clearSaleFeedback(w);clearSaleFeedback(copy);
        }
        World final;exact &= saleSave(w,final,out/"last-yard-paid-order-final.sav");
        check(correct && exact && !w.valid(yard) && w.countUnits(0,UnitType::MCV)==0,"YARD_CONTROL","last-factory-pauses-paid-work-and-cancel-refunds-once");
    }
    for(const char* name:{"GACNST","NACNST"})for(bool repacks:{false,true}) {
        selectRulesChoice(RulesChoice::RA2);World w=saleFixture();w.map.bldOccRef=&w.bldOcc;w.mcvRepacks=repacks;w.players[0].money=10000;
        const int type=saleType(w,name);const EID id=w.spawnBuilding(0,BldType::ConYard,20,20,true,type);const auto before=w.entityRef(id);
        World::Cmd pack{};pack.type=World::Cmd::Deploy;pack.ids={id};w.bindCommand(pack);w.applyCmd(0,pack);
        int matching=0;for(const auto& e:w.ents)if(e.alive && !e.isBuilding && e.player==0 && e.nativeIdentity.definition
            && e.nativeIdentity.definition->id==(name[0]=='G'?"AMCV":"SMCV"))++matching;
        bool correct=w.players[0].money==10000 && (repacks?!w.valid(before) && matching==1:w.valid(before) && matching==0);
        World copy;const auto key=std::string("deploy-")+name+(repacks?"-enabled":"-disabled");
        bool exact=saleSave(w,copy,out/(key+".sav"));
        for(int at=0;at<600 && exact;++at){w.update();copy.update();exact=w.checksum()==copy.checksum();clearSaleFeedback(w);clearSaleFeedback(copy);}
        check(correct && exact,"PACK",key.c_str());
    }
    selectRulesChoice(RulesChoice::RA2);g_script.shutdown();SetTargetFPS(0);SetWindowSize(1280,720);
    const int savedSellKey=keyBind[KA_Sell];keyBind[KA_Sell]=KEY_DELETE;
    const auto prepareUI=[&]() {
        resetMatchInteraction();ais.clear();localPlayer=0;campaignMission=-1;phase=Phase::InGame;
        paused=showMenu=gameOver=false;netGame=true;pendingCmds.clear();camZoom=1;camEdgeLock=10000;
        world.map.reveal(0,32,32,100);bakeTerrain();int sx,sy;tileToScreen(21,21,sx,sy);
        camX=sx-(SCREEN_W-sidebarW)*.5f;camY=sy-SCREEN_H*.4f;
        const Vector2 point{(sx-camX)*camZoom,(sy+TILE_H/2-camY)*camZoom};
        sim={};sim.active=true;sim.pos={-100,-100};render();return point;
    };
    const auto inputSale=[&](EID id,bool hotkey,Vector2 point) {
        if(hotkey){selBuilding=id;sim.keysPressed.insert(KEY_DELETE);handleInput();sim.keysPressed.clear();return true;}
        sim.pos={(SCREEN_W-sidebarW)+(1311-1195)*(sidebarW/171.f),178*(SCREEN_H/768.f)};
        sim.pressedL=true;render();sim.pressedL=false;const bool setup=sideMode==2;
        sim.pos=point;sim.pressedL=true;handleInput();sim.pressedL=false;return setup;
    };
    for(const char* type:{"GACNST","NACNST"})for(bool repacks:{false,true})for(bool hotkey:{false,true}) {
        const std::string name=std::string("ui-")+type+(repacks?"-repacks":"-fixed")+(hotkey?"-key":"-dollar");
        world=saleFixture("assets/rules/profiles/ra2.ini",type[0]=='N');world.map.bldOccRef=&world.bldOcc;world.mcvRepacks=repacks;world.players[0].money=10000;
        const int index=saleType(world,type);const EID id=world.spawnBuilding(0,BldType::ConYard,20,20,true,index);
        const auto point=prepareUI();
        bool setup=world.valid(id) && pickBuilding((int)point.x,(int)point.y)==id;
        const auto ref=world.entityRef(id);World before;setup &= saleSave(world,before,out/(name+"-before.sav"));
        std::ofstream(out/(name+".case"))<<id<<'\n';const auto beforeInput=world.checksum();
        setup &= inputSale(id,hotkey,point);
        const int command=pendingCmds.empty()?-1:(int)pendingCmds.front().type;
        bool correct=setup && world.checksum()==beforeInput && pendingCmds.size()==1
            && pendingCmds.front().type==World::Cmd::SellBuilding;
        for(const auto& c:pendingCmds)world.applyCmd(0,c);pendingCmds.clear();netGame=false;
        correct &= world.valid(ref) && world.ents[id].selling && world.players[0].money==10000;
        if(!hotkey && repacks){const auto sum=world.checksum();shotAtDisplaySize=true;shotFile=(out/(name+"-selling.png")).string();render();correct &= world.checksum()==sum;}
        World copy;bool exact=saleSave(world,copy,out/(name+"-ordered.sav"));
        std::ofstream trace(out/(name+".trace"));trace<<world.checksum()<<'\n';
        for(int at=1;at<=600;++at){world.update();copy.update();exact &= world.checksum()==copy.checksum();
            correct &= world.players[0].money==10000+(at>g_gameRules.buildingAnimationTicks?1500:0);
            trace<<world.checksum()<<'\n';clearSaleFeedback(world);clearSaleFeedback(copy);}
        std::ofstream(out/(name+".final"))<<world.checksum()<<'\n';
        World final;exact &= saleSave(world,final,out/(name+"-final.sav"));
        correct &= !world.valid(ref) && world.countUnits(0,UnitType::MCV)==0 && (!hotkey?sideMode==2:true);
        if(!hotkey && repacks){const auto sum=world.checksum();shotFile=(out/(name+"-sold.png")).string();render();correct &= world.checksum()==sum;}
        std::printf("BUILDING_SALE UI_OBSERVED name=%s scene=%d setup=%d command=%d expected=%d cash=%lld mcv=%d replay=%d\n",
            name.c_str(),world.nativeRulesBaseVersion,setup,command,(int)World::Cmd::SellBuilding,(long long)world.players[0].money,world.countUnits(0,UnitType::MCV),exact);
        check(setup,"UI_SETUP",name.c_str());check(correct,"UI",name.c_str());check(exact,"UI_REPLAY",name.c_str());
    }
    if(const char* directory=std::getenv("OPENRA2_LEGACY_CONYARD62_INPUT_DIR"))
    for(const char* type:{"GACNST","NACNST"})for(bool repacks:{false,true})for(bool hotkey:{false,true}) {
        const std::string name=std::string("ui-")+type+(repacks?"-repacks":"-fixed")+(hotkey?"-key":"-dollar");
        const std::filesystem::path old(directory);World before,expected;EID id=INVALID_EID;
        std::ifstream(old/(name+".case"))>>id;
        FILE* f=std::fopen((old/(name+"-before.sav")).string().c_str(),"rb");bool exact=f && before.loadGame(f);if(f)std::fclose(f);
        f=std::fopen((old/(name+"-ordered.sav")).string().c_str(),"rb");exact &= f && expected.loadGame(f);if(f)std::fclose(f);
        if(!exact){check(false,"LEGACY62_INPUT",name.c_str());continue;}
        world=std::move(before);world.map.bldOccRef=&world.bldOcc;const auto point=prepareUI();
        const auto sum=world.checksum();exact &= inputSale(id,hotkey,point) && world.checksum()==sum;
        const size_t count=hotkey && !repacks?0:1;
        exact &= world.nativeRulesBaseVersion==62 && pendingCmds.size()==count;
        if(count==1)exact &= !pendingCmds.empty() && pendingCmds.front().type==(repacks?World::Cmd::Deploy:World::Cmd::SellBuilding);
        for(const auto& c:pendingCmds)world.applyCmd(0,c);pendingCmds.clear();netGame=false;
        exact &= world.checksum()==expected.checksum();World saved;exact &= saleSave(world,saved,out/(name+"-legacy62-command.sav"));
        check(exact,"LEGACY62_INPUT",name.c_str());
    }
    keyBind[KA_Sell]=savedSellKey;sim={};netGame=false;shotAtDisplaySize=false;
    return failures;
}
