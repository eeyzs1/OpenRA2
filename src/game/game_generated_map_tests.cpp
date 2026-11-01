#include "game/game.h"
#include <array>
#include <queue>
#include <cstdio>
#include <filesystem>

int Game::generatedMapRegression(bool health,const char* outputDir) {
    int failures=0,cases=0;
    constexpr std::array<uint64_t,3> seeds{20260722,20261031,305419896};
    if(!health) {
        for(int size:{48,64,96,128,160,200,256}) for(int players:{2,4,8}) for(int type=0;type<7;++type) for(uint64_t seed:seeds) {
            Map map;std::vector<Vec2i> starts;map.generate(size,size,seed,players,starts,type);++cases;bool legal=starts.size()==players,resources=true,connected=true;
            std::vector<int> component(map.cells.size(),-1);int label=0;
            for(int sy=0;sy<size;++sy)for(int sx=0;sx<size;++sx)if(map.passable(sx,sy) && component[sy*size+sx]<0){
                std::queue<int> todo;todo.push(sy*size+sx);component[sy*size+sx]=label;
                while(!todo.empty()){int cell=todo.front();todo.pop();int x=cell%size,y=cell/size;
                    for(auto d:{Vec2i{1,0},Vec2i{-1,0},Vec2i{0,1},Vec2i{0,-1}}){int nx=x+d.x,ny=y+d.y;if(!map.passable(nx,ny) || component[ny*size+nx]>=0)continue;component[ny*size+nx]=label;todo.push(ny*size+nx);}
                }++label;
            }
            int first=-1;
            for(int player=0;player<(int)starts.size();++player){auto sp=starts[player];int id=component[sp.y*size+sp.x];if(first<0)first=id;
                if(type!=1 && type!=3)connected &= id==first;
                for(int y=sp.y-1;y<=sp.y+2;++y)for(int x=sp.x-1;x<=sp.x+2;++x)legal &= map.passable(x,y) && map.at(x,y).terrain!=Terrain::Bridge;
                int reserve=0;
                for(int y=std::max(0,sp.y-24);y<std::min(size,sp.y+25);++y)for(int x=std::max(0,sp.x-24);x<std::min(size,sp.x+25);++x){
                    if(component[y*size+x]!=id || map.at(x,y).ore<=0)continue;
                    int owner=0,best=1000000;for(int other=0;other<players;++other){int dx=x-starts[other].x,dy=y-starts[other].y;int score=dx*dx+dy*dy;if(score<best){best=score;owner=other;}}
                    if(owner==player)reserve+=map.at(x,y).ore;
                }
                resources &= reserve>=6000;
                if(reserve<6000)std::printf("MAP_RESOURCE size=%d players=%d map=%d seed=%llu player=%d reserve=%d\n",size,players,type,(unsigned long long)seed,player,reserve);
            }
            bool ok=legal && resources && connected;failures+=!ok;
            if(!ok)std::printf("GENERATED_MAP FAIL size=%d players=%d map=%d seed=%llu legal=%d resources=%d connected=%d\n",size,players,type,(unsigned long long)seed,legal,resources,connected);
        }
        for(int type=0;type<7;++type) for(uint64_t seed:seeds) {
            World sample;sample.init(64,64,seed,0,4,{Faction::Allies,Faction::Soviet,Faction::China,Faction::Yuri},type);
            bool legal=true;int mcvs=0;for(EID id=0;id<(EID)sample.ents.size();++id)if(sample.ents[id].alive && !sample.ents[id].isBuilding && sample.ents[id].utype==UnitType::MCV){++mcvs;legal &= sample.canDeployMcv(id);}
            failures+=!legal || mcvs!=4;std::printf("GENERATED_START %s map=%d seed=%llu mcvs=%d\n",legal && mcvs==4?"PASS":"FAIL",type,(unsigned long long)seed,mcvs);
        }
        std::filesystem::create_directories(outputDir);SetTargetFPS(0);cfgReferenceMap=false;cfgMapSize=64;cfgAI=1;cfgGameMode=(int)SkirmishMode::Battle;
        sim=SimInput{};sim.active=true;sim.pos={-100,-100};netGame=false;shotAtDisplaySize=true;
        auto screenshot=[&](int type,int width,const char* stage) {
            SetWindowSize(width,width==1280?720:1080);const bool match=phase==Phase::InGame;const auto hash=match?world.checksum():0;
            const auto file=(std::filesystem::path(outputDir)/(std::string("map_")+std::to_string(type)+"_"+stage+"_"+std::to_string(width)+".png")).string();
            shotFile=file;render();Image image=LoadImage(file.c_str());bool ok=image.data && image.width==width && image.height==(width==1280?720:1080) && (!match || hash==world.checksum());
            failures+=!ok;UnloadImage(image);
        };
        for(int type:{4,5}){
            phase=Phase::Setup;cfgMapType=type;previewSeed=20261031;previewDirty=true;refreshMapPreview();
            for(int width:{1280,1920})screenshot(type,width,"setup");
            const auto preview=previewMap.cells;newGame(previewSeed);bool same=preview.size()==world.map.cells.size();
            for(size_t i=0;same && i<preview.size();++i){const auto& a=preview[i];const auto& b=world.map.cells[i];same=a.terrain==b.terrain && a.overlay==b.overlay && a.variant==b.variant && a.height==b.height && a.ore==b.ore && a.oreMax==b.oreMax;}
            failures+=!same;std::printf("GENERATED_PREVIEW %s map=%d actual start has identical terrain/resources to setup\n",same?"PASS":"FAIL",type);
            for(int width:{1280,1920})screenshot(type,width,"opening");
        }
        sim=SimInput{};shotAtDisplaySize=false;
        std::printf("GENERATED_MAP cases=%d failures=%d\n",cases,failures);return failures;
    }
    for(int type=0;type<7;++type)for(int faction=0;faction<4;++faction)for(uint64_t seed:seeds){
        World sample;sample.init(64,64,seed,0,2,{(Faction)faction,(Faction)faction},type);SkirmishAI bots[2];bots[0].reset(0);bots[1].reset(1);
        bool built[2]{},income[2]{},produced[2]{};bool replay=true;World restored;SkirmishAI resumed[2];int replayLeft=0;
        for(int tick=0;tick<9000;++tick){
            sample.update();for(auto& ai:bots)ai.update(sample);
            if(replayLeft>0){restored.update();for(auto& ai:resumed)ai.update(restored);replay &= sample.checksum()==restored.checksum();--replayLeft;}
            if(tick==1200){if(FILE* file=tmpfile()){bool saved=sample.saveGame(file);rewind(file);replay &= saved && restored.loadGame(file);fclose(file);}else replay=false;resumed[0]=bots[0];resumed[1]=bots[1];replayLeft=replay?60:0;}
            for(int player=0;player<2;++player){built[player] |= sample.hasBld(player,BldType::ConYard);income[player] |= sample.harvestedIncome[player]>0;produced[player] |= sample.producedUnits[player]>0;}
            sample.presentationEvents.clear();sample.evaQueue.clear();restored.presentationEvents.clear();restored.evaQueue.clear();
        }
        bool ok=replay;for(int player=0;player<2;++player){ok &= built[player] && income[player] && produced[player];std::printf("GENERATED_ECONOMY %s map=%d faction=%d seed=%llu player=%d built=%d income=%llu produced=%llu damage=%llu money=%lld\n",built[player]&&income[player]&&produced[player]?"PASS":"FAIL",type,faction,(unsigned long long)seed,player,built[player],(unsigned long long)sample.harvestedIncome[player],(unsigned long long)sample.producedUnits[player],(unsigned long long)sample.combatDamage[player],(long long)sample.players[player].money);}
        ++cases;failures+=!ok;
    }
    std::printf("GENERATED_HEALTH cases=%d failures=%d (natural 5-minute openings; not full-match proof)\n",cases,failures);return failures;
}
