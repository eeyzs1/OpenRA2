#include "game/map.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

int Map::surfaceHeight(int x,int y,int layer) const {
    if(!inBounds(x,y))return 0;
    return layer==1 && hasDeck(x,y)?bridgeCells[(size_t)y*w+x].height:at(x,y).height;
}

bool Map::surfacePassable(int x,int y,int domain,int layer,bool buildings,bool underBridge,uint8_t gateAccess) const {
    if(!inBounds(x,y) || domain<0 || domain>2 || layer<0 || layer>1)return false;
    if(layer==1)return domain!=1 && hasDeck(x,y);
    if(!underBridge && hasDeck(x,y))return false;
    if(buildings && cellBlocked(x,y,gateAccess))return false;
    const auto& c=at(x,y);if(c.blocked)return false;
    if(domain==1)return c.terrain==Terrain::Water;
    return c.passable() || (domain==2 && c.terrain==Terrain::Water);
}

int Map::preferredLayer(int x,int y,int domain) const {
    return domain!=1 && hasDeck(x,y)?1:0;
}

int Map::stepLayer(int ax,int ay,int layer,int bx,int by,int domain,bool corners,bool underBridge,uint8_t gateAccess) const {
    if(!inBounds(ax,ay) || !inBounds(bx,by) || layer<0 || layer>1 || domain<0 || domain>2
        || std::abs(ax-bx)>1 || std::abs(ay-by)>1)return -1;
    if(ax==bx && ay==by)return surfacePassable(bx,by,domain,layer,true,underBridge,gateAccess)?layer:-1;
    if(!hasBridgeDecks()) {
        // The common single-surface case has no layer choices or railing
        // transitions. Check its four diagonal edges directly instead of
        // recursively entering the general two-layer solver for every edge.
        if(layer!=0 || !surfacePassable(bx,by,domain,0,true,underBridge,gateAccess))return -1;
        const int from=at(ax,ay).height,to=at(bx,by).height;
        if(domain!=1 && std::abs(from-to)>MAP_MAX_CLIMB)return -1;
        if(corners && ax!=bx && ay!=by) {
            if(!surfacePassable(bx,ay,domain,0,true,underBridge,gateAccess)
                || !surfacePassable(ax,by,domain,0,true,underBridge,gateAccess))return -1;
            if(domain!=1) {
                const int first=at(bx,ay).height,second=at(ax,by).height;
                if(std::abs(from-first)>MAP_MAX_CLIMB || std::abs(from-second)>MAP_MAX_CLIMB
                    || std::abs(first-to)>MAP_MAX_CLIMB || std::abs(second-to)>MAP_MAX_CLIMB)return -1;
            }
        }
        return 0;
    }
    if(layer==1 && !hasDeck(ax,ay))return -1;
    int result=-1;
    for(int target=0;target<2;++target) {
        if(!surfacePassable(bx,by,domain,target,true,underBridge,gateAccess))continue;
        if(domain!=1 && std::abs(surfaceHeight(ax,ay,layer)-surfaceHeight(bx,by,target))>MAP_MAX_CLIMB)continue;
        if(layer!=target) {
            const int deckX=target?bx:ax,deckY=target?by:ay;
            const int landX=target?ax:bx,landY=target?ay:by;
            if(hasDeck(landX,landY))continue;
            const auto& deck=bridgeCells[(size_t)deckY*w+deckX];
            // Railings prohibit side entry/exit. Only the two longitudinal
            // ends connect to a matching-height, unbridged bank.
            if((deck.axis==0 && ax==bx) || (deck.axis==1 && ay==by))continue;
        }
        if(corners && ax!=bx && ay!=by) {
            const int first=stepLayer(ax,ay,layer,bx,ay,domain,false,underBridge,gateAccess),second=stepLayer(ax,ay,layer,ax,by,domain,false,underBridge,gateAccess);
            if(first<0 || second<0 || stepLayer(bx,ay,first,bx,by,domain,false,underBridge,gateAccess)!=target
                || stepLayer(ax,by,second,bx,by,domain,false,underBridge,gateAccess)!=target)continue;
        }
        if(result>=0)return -1; // Ambiguous surface geometry is never guessed.
        result=target;
    }
    return result;
}

bool Map::findLayeredPath(int sx,int sy,int startLayer,int tx,int ty,int goalLayer,float goalX,float goalY,float range,
                          std::vector<Vec2i>& out,int maxNodes,int domain,const std::vector<uint8_t>* costs,int goalHeight,bool underBridge,uint8_t gateAccess) const {
    out.clear();if(!inBounds(sx,sy) || !inBounds(tx,ty) || domain<0 || domain>2 || maxNodes<=0)return false;
    if(startLayer<0)startLayer=preferredLayer(sx,sy,domain);
    if(startLayer>1 || goalLayer>1 || goalLayer < -1 || (startLayer==1 && !hasDeck(sx,sy)))return false;
    const int area=w*h,total=area*2,start=startLayer*area+sy*w+sx;
    auto blocked=[&](int x,int y,int layer) {
        if(!surfacePassable(x,y,domain,layer,true,underBridge,gateAccess))return true;
        if(!costs || (costs->size()!=cells.size() && costs->size()!=(size_t)total))return false;
        const int index=y*w+x+(costs->size()==(size_t)total?layer*area:0);
        return (*costs)[index]==255 && (x!=sx || y!=sy || layer!=startLayer);
    };
    if(range<=0 && blocked(tx,ty,goalLayer)) {
        bool found=false;
        for(int r=1;r<=3 && !found;++r)for(int y=-r;y<=r && !found;++y)for(int x=-r;x<=r;++x)
            if(!blocked(tx+x,ty+y,goalLayer)){tx+=x;ty+=y;goalX=tx+.5f;goalY=ty+.5f;found=true;break;}
        if(!found)return false;
    }
    const auto reached=[&](int x,int y,int layer) {
        if(goalLayer>=0 && layer!=goalLayer)return false;
        if(goalHeight>=0 && std::abs(surfaceHeight(x,y,layer)-goalHeight)>MAP_MAX_CLIMB)return false;
        if(range<=0)return x==tx && y==ty;
        const float dx=x+.5f-goalX,dy=y+.5f-goalY;return dx*dx+dy*dy<=range*range;
    };
    auto heuristic=[&](int x,int y)->int64_t {
        if(range>0)return (int64_t)std::max(0.0,std::floor((std::hypot(x+.5-goalX,y+.5-goalY)-range)*10000));
        const int dx=std::abs(x-tx),dy=std::abs(y-ty);return 10000LL*std::max(dx,dy)+4142LL*std::min(dx,dy);
    };
    struct Node{int id,x,y,layer;int64_t g,f;};
    auto compare=[&](const Node& a,const Node& b) {
        if(a.f!=b.f)return a.f>b.f;
        const auto ca=std::abs((int64_t)(a.x-sx)*(ty-sy)-(int64_t)(a.y-sy)*(tx-sx));
        const auto cb=std::abs((int64_t)(b.x-sx)*(ty-sy)-(int64_t)(b.y-sy)*(tx-sx));
        if(ca!=cb)return ca>cb;if(a.g!=b.g)return a.g<b.g;return a.id>b.id;
    };
    std::priority_queue<Node,std::vector<Node>,decltype(compare)> open(compare);
    std::vector<int64_t> distance(total,std::numeric_limits<int64_t>::max());std::vector<int> parent(total,-1);
    std::vector<uint8_t> closed(total,0);distance[start]=0;open.push({start,sx,sy,startLayer,0,heuristic(sx,sy)});
    constexpr int dx[]={1,-1,0,0,1,1,-1,-1},dy[]={0,0,1,-1,1,-1,1,-1};int expanded=0;
    while(!open.empty() && expanded++<maxNodes) {
        const auto cur=open.top();open.pop();if(closed[cur.id])continue;closed[cur.id]=1;
        if(reached(cur.x,cur.y,cur.layer)) {
            for(int id=cur.id;id>=0 && id!=start;id=parent[id])out.push_back({id%area%w,id%area/w});
            std::reverse(out.begin(),out.end());return true;
        }
        for(int direction=0;direction<8;++direction) {
            const int x=cur.x+dx[direction],y=cur.y+dy[direction];
            const int layer=stepLayer(cur.x,cur.y,cur.layer,x,y,domain,true,underBridge,gateAccess);
            if(layer<0 || blocked(x,y,layer))continue;
            const int id=layer*area+y*w+x;if(closed[id])continue;
            int64_t cost=cur.g+(direction<4?10000:14142);
            if(costs && (costs->size()==cells.size() || costs->size()==(size_t)total))
                cost+=10000LL*(*costs)[y*w+x+(costs->size()==(size_t)total?layer*area:0)];
            if(cost>=distance[id])continue;distance[id]=cost;parent[id]=cur.id;
            open.push({id,x,y,layer,cost,cost+heuristic(x,y)});
        }
    }
    return false;
}
