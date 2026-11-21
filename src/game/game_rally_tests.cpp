#include "game_regression.h"
#include <cstdio>
#include <limits>

int GameRegression::rallyRegression() {
    cfgClassicOrders=false; // This fixture explicitly covers the modern RMB route as well as R.
    int failures=0;
    for(int scenario=0;scenario<16;++scenario) {
        World w;w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass) for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive) w.kill((EID)i,false);
        for(auto& cell:w.map.cells) cell=Cell{};
        for(auto& player:w.players) { player.defeated=false;player.money=0; }
        w.cratesEnabled=false;w.presentationEvents.clear();
        // Mechanism fixture: inject a completed, fully paid order, then use
        // the ordinary production tick rather than exposing factory internals.
        auto produce=[&](UnitType type) {
            const auto& def=unitDef(type);auto& order=w.players[0].unitProd[def.prodCat()];order=ProdItem{};
            order.active=order.isUnit=order.ready=true;order.typeIdx=(int)type;order.progress=def.buildTime;
            order.totalCost=order.paid=def.cost;const auto count=w.producedUnits[0];w.update();
            return w.producedUnits[0]==count+1;
        };
        BldType factoryType=scenario==2?BldType::OreRefinery:scenario==4 || scenario==11?BldType::AirForceCmd:scenario==5?BldType::Barracks:
            scenario==7 || scenario==15?BldType::NavalYard:BldType::WarFactory;
        if(scenario==7 || scenario==15) for(auto& cell:w.map.cells) cell.terrain=Terrain::Water;
        EID factory=w.spawnBuilding(0,factoryType,10,10,true),clone=INVALID_EID;
        if(scenario==5) clone=w.spawnBuilding(0,BldType::CloningVat,25,10,true);
        if(scenario==10) for(int y=0;y<64;++y) w.map.at(25,y).overlay=Overlay::Rock1;
        if(scenario==14) w.map.at(35,35).terrain=Terrain::Water;
        if(scenario==15) for(int y=26;y<=44;++y) for(int x=26;x<=44;++x) w.map.at(x,y).terrain=Terrain::Clear;
        if(scenario==13) w.ents[factory].selling=true;
        World::Cmd rally{};rally.type=World::Cmd::SetRally;rally.ids={factory};rally.x=35;rally.y=35;
        if(scenario!=11) w.applyCmd(0,rally);
        const int oldX=w.ents[factory].rallyX,oldY=w.ents[factory].rallyY;
        bool command=true,production=true,equal=true,feedback=true;EID unit=INVALID_EID;
        if(scenario==0 || scenario==1 || scenario==9 || scenario==12) {
            rally.x=scenario==0?std::numeric_limits<float>::infinity():scenario==1?-5.0f:scenario==12?std::numeric_limits<float>::quiet_NaN():100000.0f;
            w.applyCmd(0,rally);command=w.ents[factory].rallyX==oldX && w.ents[factory].rallyY==oldY;
        } else if(scenario==2) command=w.ents[factory].rallyX!=35 || w.ents[factory].rallyY!=35;
        else if(scenario==13) command=w.ents[factory].rallyX!=35 || w.ents[factory].rallyY!=35;
        else {
            if(scenario==3) { rally.a=1;w.applyCmd(0,rally);command=w.ents[factory].rallyX==-1 && w.ents[factory].rallyY==-1; }
            if(scenario==5) { rally.ids={clone};rally.x=45;rally.y=35;w.applyCmd(0,rally); }
            const auto type=scenario==4 || scenario==11?UnitType::Intruder:scenario==5?UnitType::GI:scenario==7 || scenario==15?UnitType::Aegis:UnitType::Grizzly;
            production=produce(type);
            EID duplicate=INVALID_EID;
            for(size_t i=0;i<w.ents.size();++i) if(w.ents[i].alive && !w.ents[i].isBuilding) {
                if(unit==INVALID_EID) unit=(EID)i;else duplicate=(EID)i;
            }
            if(!w.valid(unit)) production=false;
            else if(scenario==3) production &= w.ents[unit].state==UState::Idle && w.ents[unit].path.empty();
            else if(scenario==10 || scenario==15) production &= w.ents[unit].state==UState::Moving && !w.ents[unit].path.empty()
                && (scenario==10?w.ents[unit].path.back().x<25:
                    w.map.at(w.ents[unit].path.back().x,w.ents[unit].path.back().y).terrain==Terrain::Water);
            else if(scenario==11) production &= w.ents[unit].state==UState::Landed && w.ents[unit].occCell>=0;
            else if(scenario==4) production &= w.ents[unit].state==UState::Moving && w.ents[unit].goalX>=35 && w.ents[unit].airBucket>=0 && w.ents[unit].occCell<0;
            else production &= w.ents[unit].state==UState::Moving && !w.ents[unit].path.empty();
            if(scenario==5) {
                bool first=false,second=false;
                for(EID id:{unit,duplicate}) if(w.valid(id) && !w.ents[id].path.empty()) {
                    first |= w.ents[id].path.back()==Vec2i{35,35};second |= w.ents[id].path.back()==Vec2i{45,35};
                }
                production &= first && second;
            }
            if(scenario==6 && w.valid(unit)) {
                // Vacate the physical mouth but retain the first rally route.
                w.ents[unit].x+=1;w.unitOccSync(unit);production &= produce(type);
                Vec2i first{-1,-1};bool distinct=false;
                for(const auto& e:w.ents) if(e.alive && !e.isBuilding && !e.path.empty()) {
                    if(first.x<0) first=e.path.back();else distinct |= first!=e.path.back();
                }
                production &= distinct;
            }
        }
        if(scenario==0 || scenario==1 || scenario==2 || scenario==3 || scenario==9 || scenario>=12 || scenario==10) {
            world=w;world.map.bldOccRef=&world.bldOcc;localPlayer=0;selBuilding=factory;const auto hash=world.checksum();
            consumePresentationEvents();
            S expected=scenario==2?S::MsgRallyUnsupported:scenario==3?S::MsgRallyCleared:scenario==13?S::MsgRallySelling:
                scenario==10 || scenario==15 || scenario==14?S::MsgRallyAdjusted:S::MsgRallyInvalid;
            feedback=msg==TR(expected) && world.checksum()==hash;
        }
        FILE* file=tmpfile();World replay;bool saved=file && w.saveGame(file);
        if(saved) { rewind(file);saved=replay.loadGame(file); }
        if(file) fclose(file);
        const auto requests=w.repathRequests;
        for(int tick=0;tick<600 && saved;++tick) { w.update();replay.update();equal &= w.checksum()==replay.checksum(); }
        if((scenario>=5 && scenario<=8) || scenario==14) for(const auto& entity:w.ents) if(entity.alive && !entity.isBuilding)
            production &= !entity.path.empty() && entity.pathIdx==(int)entity.path.size() && entity.state==UState::Idle;
        if(scenario==14 && w.valid(unit)) production &= w.map.at((int)w.ents[unit].x,(int)w.ents[unit].y).terrain!=Terrain::Water;
        if(scenario==4 && w.valid(unit)) production &= distf(w.ents[unit].x,w.ents[unit].y,35.5f,35.5f)<2.1f;
        bool recovery=true;
        if(scenario==10 && saved) {
            recovery=w.repathRequests==requests && w.valid(unit) && w.ents[unit].state==UState::Idle;
            rally.x=20;rally.y=20;w.applyCmd(0,rally);replay.applyCmd(0,rally);
            World::Cmd move{};move.type=World::Cmd::Move;move.ids={unit};move.x=20.5f;move.y=20.5f;
            w.applyCmd(0,move);replay.applyCmd(0,move);
            for(int tick=0;tick<300;++tick) { w.update();replay.update();equal &= w.checksum()==replay.checksum(); }
            recovery &= w.ents[unit].x>19 && w.ents[unit].y>19 && w.ents[unit].state==UState::Idle;
        }
        bool ok=command && production && saved && equal && feedback && recovery;failures+=!ok;
        std::printf("RALLY %s scenario=%d command=%d production=%d saved=%d replay=%d feedback=%d recovery=%d\n",ok?"PASS":"FAIL",scenario,command,production,saved,equal,feedback,recovery);
        if(scenario==0) for(int corrupt=0;corrupt<3;++corrupt) {
            World bad=w;bad.map.bldOccRef=&bad.bldOcc;
            bad.ents[factory].rallyX=corrupt==0?-1:corrupt==1?100000:-2;bad.ents[factory].rallyY=corrupt==2?-2:12;
            FILE* invalid=tmpfile();bool written=invalid && bad.saveGame(invalid),accepted=false;World rejected;
            if(written) { rewind(invalid);accepted=rejected.loadGame(invalid); }
            if(invalid) fclose(invalid);bool rejects=written && !accepted;failures+=!rejects;
            std::printf("RALLY_SAVE %s corruption=%d\n",rejects?"PASS":"FAIL",corrupt);
        }
    }
    world.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
    for(int pass=0;pass<2;++pass) for(size_t i=0;i<world.ents.size();++i) if(world.ents[i].alive) world.kill((EID)i,false);
    for(auto& cell:world.map.cells) cell=Cell{};
    const EID factory=world.spawnBuilding(0,BldType::WarFactory,10,10,true);
    localPlayer=0;sel.clear();selBuilding=factory;phase=Phase::InGame;showMenu=gameOver=false;
    const int oldKey=keyBind[KA_Rally];keyBind[KA_Rally]=KEY_R;
    sim=SimInput{};sim.active=true;sim.pos={500,300};sim.keysPressed.insert(KEY_R);
    camZoom=1;camX=-500;camY=836;netGame=true;pendingCmds.clear();msg="pending rally";
    const auto before=world.checksum();handleInput();
    bool queued=pendingCmds.size()==1 && world.checksum()==before && msg=="pending rally";
    netGame=false;for(const auto& command:pendingCmds) world.applyCmd(0,command);pendingCmds.clear();consumePresentationEvents();
    bool set=world.ents[factory].rallyX==35 && world.ents[factory].rallyY==35 && msg==TR(S::MsgRallySet);
    sim.keysDown.insert(KEY_LEFT_SHIFT);handleInput();consumePresentationEvents();
    bool cleared=world.ents[factory].rallyX==-1 && world.ents[factory].rallyY==-1 && msg==TR(S::MsgRallyCleared);
    World::Cmd point{};point.type=World::Cmd::SetRally;point.ids={factory};point.x=35;point.y=35;world.applyCmd(0,point);consumePresentationEvents();
    sim.keysDown.clear();sim.keysPressed.clear();sim.pressedR=sim.downR=true;
    camX=-500;camY=(10+10+1)*16-300;handleInput();consumePresentationEvents();
    bool selfClear=world.ents[factory].rallyX==-1 && msg==TR(S::MsgRallyCleared);
    sim.pressedR=sim.downR=false;sim.pos={(float)SCREEN_W-20,300};sim.keysPressed.insert(KEY_R);msg="over sidebar";
    handleInput();bool guarded=world.ents[factory].rallyX==-1 && msg=="over sidebar";
    bool ui=queued && set && cleared && selfClear && guarded;failures+=!ui;
    std::printf("RALLY_INPUT %s queued=%d set=%d cleared=%d self_clear=%d sidebar_guard=%d\n",ui?"PASS":"FAIL",queued,set,cleared,selfClear,guarded);
    sim=SimInput{};keyBind[KA_Rally]=oldKey;netGame=false;
    return failures;
}
