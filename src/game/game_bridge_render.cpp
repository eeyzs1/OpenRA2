#include "game/game.h"
#include "gfx/sprites.h"
#include <queue>

void Game::orderBridgeDrawItems(std::vector<WorldDrawItem>& items) const {
    if(!world.map.hasBridgeDecks())return;
    const int count=(int)items.size();std::vector<std::vector<int>> after(count);std::vector<int> incoming(count,0);
    for(int bridge=0;bridge<count;++bridge) {
        if(items[bridge].kind!=1)continue;
        const auto& actor=world.ents[items[bridge].id];const auto& spec=actor.nativeIdentity.definition;
        if(!spec || spec->kind!=3)continue;
        const auto& sprite=nativeActorSprite(actor);if(!sprite.tex.id)continue;
        auto origin=bldScreenPos(actor);const int frame=nativeActorFrame(actor);if(frame>=9 && frame<=17)origin.y-=TILE_H/2;
        const Rectangle box{origin.x-sprite.ox,origin.y-sprite.oy,(float)sprite.tex.width,(float)sprite.tex.height};
        const int deckHeight=world.map.at((int)actor.x,(int)actor.y).height+4;
        for(int unit=0;unit<count;++unit) {
            if(items[unit].kind!=0)continue;const auto& mobile=world.ents[items[unit].id];
            if(mobile.airborne() || mobile.magneticHeight>0 || !CheckCollisionRecs(box,unitScreenRect(mobile)))continue;
            const int height=world.actorSurfaceHeight(mobile);
            int first=-1,last=-1;
            if(height<deckHeight){first=unit;last=bridge;}
            else if(mobile.navLayer==1 && height>=deckHeight){first=bridge;last=unit;}
            if(first>=0){after[first].push_back(last);++incoming[last];}
        }
    }
    // Stable topological order preserves the ordinary footprint order wherever
    // a bridge adds no vertical occlusion constraint.
    std::priority_queue<int,std::vector<int>,std::greater<int>> ready;
    for(int i=0;i<count;++i)if(!incoming[i])ready.push(i);
    std::vector<WorldDrawItem> sorted;sorted.reserve(items.size());
    while(!ready.empty()) {
        const int index=ready.top();ready.pop();sorted.push_back(items[index]);
        for(int next:after[index])if(--incoming[next]==0)ready.push(next);
    }
    if(sorted.size()==items.size())items=std::move(sorted);
}
