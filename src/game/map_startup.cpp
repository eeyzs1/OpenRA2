#include "game/map.h"
#include <algorithm>
#include <limits>
#include <queue>
#include <tuple>

void Map::finalizeSkirmishStarts(const std::vector<Vec2i>& starts,int mapType) {
    if(starts.empty())return;
    const int count=w*h;
    constexpr Vec2i directions[]={{1,0},{-1,0},{0,1},{0,-1}};
    auto flood=[&](int start,std::vector<int>& labels,int label) {
        std::queue<int> todo;todo.push(start);labels[start]=label;
        while(!todo.empty()) { const int cell=todo.front();todo.pop();const int x=cell%w,y=cell/w;
            for(auto direction:directions){const int nx=x+direction.x,ny=y+direction.y;
                if(!passable(nx,ny) || labels[ny*w+nx]>=0)continue;
                labels[ny*w+nx]=label;todo.push(ny*w+nx);
            }
        }
    };
    // Land map families require a real surface route. Island families retain their sea separation.
    if(mapType!=1 && mapType!=3) {
        const int anchor=starts[0].y*w+starts[0].x;
        std::vector<int> connected(count,-1);flood(anchor,connected,0);
        for(const auto start:starts) {
            const int target=start.y*w+start.x;if(connected[target]>=0)continue;
            constexpr int infinity=std::numeric_limits<int>::max()/4;
            std::vector<int> cost(count,infinity),parent(count,-1);
            std::priority_queue<std::pair<int,int>,std::vector<std::pair<int,int>>,std::greater<std::pair<int,int>>> frontier;
            cost[anchor]=0;frontier.push({0,anchor});
            while(!frontier.empty()) {
                const auto [distance,cell]=frontier.top();frontier.pop();if(distance!=cost[cell])continue;if(cell==target)break;
                const int x=cell%w,y=cell/w;
                for(auto direction:directions){const int nx=x+direction.x,ny=y+direction.y;if(!inBounds(nx,ny))continue;
                    const int next=ny*w+nx;const int penalty=at(nx,ny).terrain==Terrain::Water?20:passable(nx,ny)?1:12;
                    if(distance+penalty>=cost[next])continue;cost[next]=distance+penalty;parent[next]=cell;frontier.push({cost[next],next});
                }
            }
            for(int cell=target;cell!=anchor && cell>=0;cell=parent[cell]) {
                auto& terrain=cells[cell];
                if(terrain.terrain==Terrain::Water){terrain.terrain=Terrain::Bridge;terrain.ore=terrain.oreMax=0;terrain.overlay=Overlay::None;}
                else if(!terrain.passable())terrain.overlay=Overlay::None;
            }
            std::fill(connected.begin(),connected.end(),-1);flood(anchor,connected,0);
        }
    }
    // Resource ownership here is a generation-only nearest-start partition, not a simulation field.
    std::vector<int> labels(count,-1),owner(count,0);int label=0;
    for(int cell=0;cell<count;++cell)if(labels[cell]<0 && passable(cell%w,cell/w))flood(cell,labels,label++);
    for(int cell=0;cell<count;++cell){const int x=cell%w,y=cell/w;int best=std::numeric_limits<int>::max();
        for(int player=0;player<(int)starts.size();++player){const int dx=x-starts[player].x,dy=y-starts[player].y;const int score=dx*dx+dy*dy;
            if(score<best){best=score;owner[cell]=player;}
        }
    }
    auto reserved=[&](int x,int y){
        for(auto start:starts){const int dx=x-start.x,dy=y-start.y;
            if(dx>=-1 && dx<=2 && dy>=-1 && dy<=2)return true;
            if((dx==3 && dy==1)||(dx==-3 && dy==1)||(dx==1 && dy==-3)||(dx==1 && dy==3))return true;
        }return false;
    };
    for(int player=0;player<(int)starts.size();++player) {
        const auto start=starts[player];const int region=labels[start.y*w+start.x];int reserve=0;
        std::vector<std::pair<int,int>> candidates;
        const int directionX=start.x<w/2?1:-1,directionY=start.y<h/2?1:-1;
        for(int y=std::max(0,start.y-24);y<std::min(h,start.y+25);++y)for(int x=std::max(0,start.x-24);x<std::min(w,start.x+25);++x){
            const int cell=y*w+x;if(owner[cell]!=player || labels[cell]!=region)continue;
            const auto& terrain=cells[cell];reserve+=terrain.ore;
            const int dx=x-start.x,dy=y-start.y;
            if(terrain.ore>0 || terrain.terrain==Terrain::Water || terrain.terrain==Terrain::Bridge || !terrain.passable()
                || reserved(x,y) || std::max(std::abs(dx),std::abs(dy))<4)continue;
            const int score=(dx*dx+dy*dy)*64-(dx*directionX+dy*directionY)*8;
            candidates.push_back({score,cell});
        }
        std::sort(candidates.begin(),candidates.end());
        // Twenty standard ore cells of stock on the player's reachable local territory.
        for(const auto [score,cell]:candidates) {
            if(reserve>=6000)break;auto& terrain=cells[cell];terrain.terrain=Terrain::Ore;terrain.overlay=Overlay::None;
            terrain.ore=terrain.oreMax=300;reserve+=300;
        }
    }
}
