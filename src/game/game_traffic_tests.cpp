#include "game/game.h"
#include <cstdio>
#include <cstring>

int Game::trafficRegression(int scenarioFilter) {
    if(scenarioFilter < -1 || scenarioFilter > 3) return 1;
    int failures=0;
    for(int scenario=0;scenario<4;++scenario) {
        if(scenarioFilter>=0 && scenario!=scenarioFilter) continue;
        World w;
        w.init(96,96,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id) if(w.ents[id].alive) w.kill((EID)id,false);
        for(auto& cell:w.map.cells) { cell=Cell{};cell.terrain=Terrain::Water; }
        auto clear=[&](int x0,int y0,int x1,int y1) {
            for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) w.map.at(x,y).terrain=Terrain::Clear;
        };
        clear(4,32,25,65);clear(65,48,91,84);
        if(scenario==1) {
            clear(26,48,40,48);clear(40,48,40,64);clear(40,64,55,64);
            clear(55,54,55,64);clear(55,54,64,54);
        } else {
            clear(26,48,45,48);clear(45,48,45,64);clear(45,64,64,64);
        }
        w.cratesEnabled=false;w.aiAlliance=true;
        for(auto& player:w.players) { player.isAI=true;player.defeated=false; }
        World replay=w;replay.map.bldOccRef=&replay.bldOcc;
        const int waves=scenario==2?3:1;
        int spawned=0,finished=-1;bool legal=true,equal=true,budget=true,saved=false,integrity=true;
        std::vector<EID> units;std::vector<bool> toRight;
        auto spawn=[&](int wave) {
            for(int side=0;side<2;++side) {
                for(int index=0;index<25;++index) {
                    float x=(side?78.5f:10.5f)+index%5;
                    float y=(side?74.5f-wave*5:35.5f+wave*5)+index/5;
                    if(w.unitAtCell((int)x,(int)y)!=INVALID_EID) return false;
                }
            }
            for(int side=0;side<2;++side) {
                std::vector<EID> group;
                for(int index=0;index<25;++index) {
                    float x=(side?78.5f:10.5f)+index%5;
                    float y=(side?74.5f-wave*5:35.5f+wave*5)+index/5;
                    if(scenario==3 && index==0) { x=45.5f;y=side?60.5f:52.5f; }
                    int owner=scenario==0?0:side;
                    EID id=w.spawnUnit(owner,UnitType::Grizzly,x,y);
                    EID other=replay.spawnUnit(owner,UnitType::Grizzly,x,y);
                    equal &= id==other;units.push_back(id);toRight.push_back(!side);group.push_back(id);
                }
                World::Cmd command{};command.type=World::Cmd::Move;command.ids=group;
                command.x=side?15:75;command.y=side?55:56;
                w.applyCmd(scenario==0?0:side,command);replay.applyCmd(scenario==0?0:side,command);
            }
            return true;
        };
        for(int frame=0;frame<(scenario==2?16000:8000);++frame) {
            if(spawned<waves && frame>=spawned*600 && spawn(spawned)) ++spawned;
            w.update();replay.update();budget &= w.repathsThisTick<=8;
            equal &= w.checksum()==replay.checksum();
            w.presentationEvents.clear();replay.presentationEvents.clear();
            if(finished<0 && spawned==waves) {
                bool complete=true;
                for(size_t index=0;index<units.size();++index) {
                    const auto& unit=w.ents[units[index]];
                    complete &= unit.state==UState::Idle && !unit.path.empty() && unit.pathIdx==(int)unit.path.size()
                        && (toRight[index]?unit.x>=65:unit.x<26);
                }
                if(complete) finished=frame+1;
            }
            if(scenario==0 && frame==400) {
                std::vector<uint8_t> bytes;
                if(FILE* file=tmpfile()) {
                    if(w.saveGame(file) && !fseek(file,0,SEEK_END)) {
                        const long size=ftell(file);
                        if(size>=25 && !fseek(file,0,SEEK_SET)) {
                            bytes.resize((size_t)size);
                            if(fread(bytes.data(),1,bytes.size(),file)!=bytes.size()) bytes.clear();
                        }
                    }
                    fclose(file);
                }
                uint32_t count=0;
                if(bytes.size()>=25) std::memcpy(&count,bytes.data()+bytes.size()-25,4);
                integrity &= count==1;
                auto loads=[](const std::vector<uint8_t>& data) {
                    FILE* file=tmpfile();World restored;bool ok=false;
                    if(file) { if(fwrite(data.data(),1,data.size(),file)==data.size()) { rewind(file);ok=restored.loadGame(file); } fclose(file); }
                    return ok;
                };
                if(count==1) {
                    integrity &= loads(bytes);
                    const size_t record=bytes.size()-21;
                    auto rejects=[&](size_t offset,const auto& value) {
                        auto corrupt=bytes;std::memcpy(corrupt.data()+record+offset,&value,sizeof(value));
                        integrity &= !loads(corrupt);
                    };
                    rejects(0,int32_t{3});rejects(4,int32_t{96*96});rejects(8,int32_t{-1});
                    rejects(12,uint64_t{w.tick+1000001});rejects(20,uint8_t{2});
                    auto duplicate=bytes;uint32_t two=2;std::memcpy(duplicate.data()+record-4,&two,4);
                    duplicate.insert(duplicate.end(),bytes.begin()+record,bytes.end());integrity &= !loads(duplicate);
                    auto trailing=bytes;trailing.push_back(0);integrity &= !loads(trailing);
                    auto oldTag=bytes;oldTag[7]='M';integrity &= !loads(oldTag);
                }
            }
            std::vector<EID> occupied(w.map.cells.size(),INVALID_EID);
            for(EID id:units) {
                const auto& unit=w.ents[id];int x=(int)unit.x,y=(int)unit.y;
                if(!w.valid(id) || !w.map.inBounds(x,y) || !w.map.passable(x,y) || w.bldBlocked(x,y)) {
                    if(legal) std::printf("TRAFFIC_ILLEGAL scenario=%d frame=%d id=%d cell=%d,%d\n",scenario,frame,id,x,y);
                    legal=false;continue;
                }
                auto cell=(size_t)y*w.map.w+x;
                if(occupied[cell]!=INVALID_EID) {
                    if(legal) std::printf("TRAFFIC_OVERLAP scenario=%d frame=%d id=%d other=%d cell=%d,%d\n",scenario,frame,id,occupied[cell],x,y);
                    legal=false;
                }
                occupied[cell]=id;
            }
            if(frame==1500) {
                FILE* file=tmpfile();saved=file && w.saveGame(file);
                if(saved) { rewind(file);saved=replay.loadGame(file); }
                if(file) fclose(file);
                if(!saved) { equal=false;break; }
                equal &= saved && w.checksum()==replay.checksum();
            }
        }
        int arrived=0;
        for(size_t index=0;index<units.size();++index) {
            const auto& unit=w.ents[units[index]];
            const auto goal=unit.path.empty()?Vec2i{-100,-100}:unit.path.back();
            bool atBank=toRight[index]?unit.x>=65 && unit.x<92 && unit.y>=48 && unit.y<85:
                unit.x>=4 && unit.x<26 && unit.y>=32 && unit.y<66;
            // Occupied destinations are explicitly adjusted; idle allies can
            // also be nudged aside after completion. Require a completed route
            // on the opposite bank, still within six tiles of its endpoint.
            arrived+=atBank && unit.state==UState::Idle && !unit.path.empty()
                && unit.pathIdx==(int)unit.path.size()
                && std::abs(unit.x-goal.x-0.5f)<=6 && std::abs(unit.y-goal.y-0.5f)<=6;
        }
        bool ok=spawned==waves && arrived==waves*50 && legal && equal && budget && saved && integrity;
        failures+=!ok;
        std::printf("TRAFFIC %s scenario=%d spawned=%zu arrived=%d finished=%d legal=%d replay=%d saved=%d integrity=%d budget=%d repaths=%llu\n",
            ok?"PASS":"FAIL",scenario,units.size(),arrived,finished,legal,equal,saved,integrity,budget,(unsigned long long)w.repathRequests);
        if(!ok) for(EID id:units) {
            const auto& unit=w.ents[id];
            auto next=unit.pathIdx<(int)unit.path.size()?unit.path[unit.pathIdx]:Vec2i{-1,-1};
            auto goal=unit.path.empty()?Vec2i{-1,-1}:unit.path.back();
            std::printf("TRAFFIC_UNIT id=%d pos=%.2f,%.2f state=%d block=%d path=%d/%zu next=%d,%d goal=%d,%d\n",id,unit.x,unit.y,(int)unit.state,unit.blockTick,unit.pathIdx,unit.path.size(),next.x,next.y,goal.x,goal.y);
        }
    }
    for(int owner:{1,-1}) {
        World w;w.init(64,64,20261031,2,0,{Faction::Allies,Faction::Soviet},0,"",true);
        for(size_t id=0;id<w.ents.size();++id) if(w.ents[id].alive) w.kill((EID)id,false);
        for(auto& cell:w.map.cells) { cell=Cell{};cell.terrain=Terrain::Clear; }
        w.aiAlliance=true;w.cratesEnabled=false;
        for(auto& player:w.players) player.isAI=true;
        const EID tank=w.spawnUnit(0,UnitType::Grizzly,10.5f,10.5f);
        const EID infantry=w.spawnUnit(owner,UnitType::GI,11.5f,10.5f);
        auto result=w.orderMove({tank},14.5f,10.5f,false);
        for(int frame=0;frame<240;++frame) w.update();
        bool ok=result.accepted==1 && w.valid(tank) && w.ents[tank].state==UState::Idle
            && (int)w.ents[tank].x==14 && (int)w.ents[tank].y==10
            && (owner<0?!w.valid(infantry):w.valid(infantry) && w.ents[infantry].hp==unitDef(UnitType::GI).hp);
        failures+=!ok;
        std::printf("TRAFFIC_CRUSH %s owner=%d\n",ok?"PASS":"FAIL",owner);
    }
    return failures;
}
