#include "game_regression.h"
#include <cstdio>

int GameRegression::idleYieldRegression() {
    int failures=0;
    for(int scenario=0;scenario<9;++scenario) {
        World w;w.init(64,64,20261031,2,0,{scenario==3?Faction::China:Faction::Allies,Faction::Soviet},0,"",true);
        for(int pass=0;pass<2;++pass) for(size_t id=0;id<w.ents.size();++id) if(w.ents[id].alive) w.kill((EID)id,false);
        for(auto& cell:w.map.cells) { cell=Cell{};cell.overlay=Overlay::Rock1; }
        const bool bent=scenario==1,water=scenario==3 || scenario==4;
        auto clear=[&](int x0,int y0,int x1,int y1) {
            for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) { w.map.at(x,y)=Cell{};w.map.at(x,y).terrain=water?Terrain::Water:Terrain::Clear; }
        };
        clear(2,8,12,20);
        if(bent) { clear(13,14,30,14);clear(30,14,30,40);clear(30,40,47,40);clear(48,32,61,48); }
        else { clear(13,14,48,14);if(scenario!=8) clear(49,8,61,20); }
        for(auto& player:w.players) { player.defeated=false;player.isAI=true;player.money=0; }
        w.aiAlliance=true;w.cratesEnabled=false;w.effects.clear();w.presentationEvents.clear();
        const auto type=scenario==3?UnitType::Aegis:scenario==4?UnitType::AmphTransport:UnitType::MCV;
        const int count=scenario==2?50:1;
        std::vector<EID> movers,parked;
        for(int index=0;index<count;++index) movers.push_back(w.spawnUnit(0,type,
            scenario==8?29.5f:count==1?8.5f:4.5f+index%7,count==1?14.5f:8.5f+index/7));
        for(int index=0;index<(bent?6:1);++index)
            parked.push_back(w.spawnUnit(scenario==2?1:0,scenario==5?UnitType::Grizzly:type,30.5f,bent?23.5f+index:14.5f));
        if(scenario==5) w.orderGuard(parked);
        World::Cmd command{};command.type=World::Cmd::Move;command.ids=movers;
        command.x=scenario==8?48.5f:55.5f;command.y=bent?40.5f:scenario==2?10.5f:14.5f;
        w.applyCmd(0,command);w.presentationEvents.clear();
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        bool legal=true,equal=true,budget=true,saved=false,started=false,overrideDone=false,stopped=false,smooth=true;
        int reports=0;Vec2f stoppedAt{};
        for(int frame=0;frame<6000;++frame) {
            std::vector<Vec2f> before;
            for(EID id:parked) before.push_back({w.ents[id].x,w.ents[id].y});
            w.update();replay.update();equal &= w.checksum()==replay.checksum();budget &= w.repathsThisTick<=8;
            std::vector<int> infantry(w.map.cells.size(),0),vehicles(w.map.cells.size(),0);
            for(const auto& unit:w.ents) if(unit.alive && !unit.isBuilding && !unit.parasiting && !unitDef(unit.utype).isAir()) {
                int x=(int)unit.x,y=(int)unit.y;
                if(!w.passableFor(x,y,unitDef(unit.utype).pathDomain()) || w.bldBlocked(x,y)) { legal=false;continue; }
                int cell=y*w.map.w+x;
                if(unitDef(unit.utype).isInfantry()) ++infantry[cell];else ++vehicles[cell];
                legal &= vehicles[cell]<=1 && infantry[cell]<=3 && (!vehicles[cell] || !infantry[cell]);
            }
            for(size_t index=0;index<parked.size();++index) {
                const auto& unit=w.ents[parked[index]];
                bool narrow=bent?((int)before[index].x==30 && before[index].y>=14 && before[index].y<41)
                    || ((int)before[index].y==14 && before[index].x>=13 && before[index].x<31)
                    || ((int)before[index].y==40 && before[index].x>=30 && before[index].x<48):before[index].x>=13 && before[index].x<49;
                if(narrow) smooth &= distf(before[index].x,before[index].y,unit.x,unit.y)<=1.0f/unitDef(unit.utype).speed+0.001f;
                started |= unit.state==UState::Moving && !unit.path.empty();
            }
            for(const auto& event:w.presentationEvents) if(event.kind==PresentationKind::MoveOrder && event.move.rejected) ++reports;
            w.presentationEvents.clear();replay.presentationEvents.clear();
            if(!saved && (started || frame==600)) {
                FILE* file=tmpfile();saved=file && w.saveGame(file);
                if(saved) { rewind(file);saved=replay.loadGame(file); }
                if(file) fclose(file);
                if(!saved) break;
                equal &= w.checksum()==replay.checksum();
            }
            if(scenario==6 && started && !overrideDone) {
                World::Cmd replacement{};replacement.type=World::Cmd::Move;replacement.ids=parked;replacement.x=59.5f;replacement.y=18.5f;
                w.applyCmd(0,replacement);replay.applyCmd(0,replacement);overrideDone=true;
            }
            if(scenario==7 && !stopped && w.ents[parked[0]].x>=49 && w.ents[parked[0]].state==UState::Moving) {
                World::Cmd stop{};stop.type=World::Cmd::Stop;stop.ids=movers;stop.ids.insert(stop.ids.end(),parked.begin(),parked.end());
                w.applyCmd(0,stop);replay.applyCmd(0,stop);stopped=true;stoppedAt={w.ents[parked[0]].x,w.ents[parked[0]].y};
            }
        }
        const auto requests=w.repathRequests;
        for(int frame=0;frame<300 && saved;++frame) { w.update();replay.update();equal &= w.checksum()==replay.checksum(); }
        bool outcome=true;
        if(scenario==5 || scenario==8) outcome=w.ents[movers[0]].state==UState::Idle && w.ents[movers[0]].path.empty()
            && reports==1 && w.ents[parked[0]].x==30.5f && w.ents[parked[0]].y==14.5f;
        else if(scenario==7) outcome=stopped && w.ents[parked[0]].x==stoppedAt.x && w.ents[parked[0]].y==stoppedAt.y && reports==0;
        else {
            for(EID id:movers) outcome &= w.ents[id].state==UState::Idle && !w.ents[id].path.empty()
                && w.ents[id].pathIdx==(int)w.ents[id].path.size() && w.ents[id].x>=(bent?48:49);
            for(EID id:parked) outcome &= w.ents[id].state==UState::Idle && w.ents[id].x>=(bent?48:49);
            outcome &= started && reports==0;
            if(scenario==6) outcome &= overrideDone && (int)w.ents[parked[0]].x==59 && (int)w.ents[parked[0]].y==18;
        }
        bool ok=legal && equal && budget && saved && smooth && outcome && requests==w.repathRequests;
        failures+=!ok;
        std::printf("IDLE_YIELD %s scenario=%d actors=%zu parked=%zu legal=%d replay=%d saved=%d budget=%d smooth=%d started=%d outcome=%d reports=%d bounded=%d requests=%llu\n",
            ok?"PASS":"FAIL",scenario,movers.size(),parked.size(),legal,equal,saved,budget,smooth,started,outcome,reports,requests==w.repathRequests,(unsigned long long)requests);
    }
    return failures;
}
