#include "game/world.h"
#include "game/native_scene_contract.h"
#include <cmath>
#include <algorithm>

void World::updateFog(int player) {
    if(!nativeEvents.active || !nativeEventDefinition || nativeEventDefinition->fogOfWar)map.clearVisible(player);
    // 间谍渗透雷达 或 间谍卫星（RA2 原作：建成后全图常亮，被毁后恢复战争迷雾）
    if (players[player].revealTimer > 0 || hasBld(player, BldType::SpySat)) {
        for (int y = 0; y < map.h; y++)
            for (int x = 0; x < map.w; x++)
                map.reveal(player, x, y, 0);
        return;
    }
    revealLocalSight(player,player);
}

void World::revealLocalSight(int player,int owner) {
    for (const Ent& e : ents) {
        if (!e.alive || e.player != owner) continue;
        int sight;
        int cx, cy;
        if (e.isBuilding) {
            sight = bldDef(e).sight;
            cx = (int)e.x + bldDef(e).w / 2;
            cy = (int)e.y + bldDef(e).h / 2;
        } else {
            sight = unitDef(e).sight;
            cx = (int)e.x; cy = (int)e.y;
        }
        map.reveal(player, cx, cy, sight);
    }
    if(supportDeliveryEnabled)for(const auto& mission:supportMissions)
        if(mission.type==SupportType::SpyPlane && valid(mission.aircraft)) {
            const auto& e=ents[mission.aircraft.id];const auto& rules=supportDeliveryRules();
            if(e.player==owner && distf(e.x,e.y,mission.x,mission.y)<=rules.spyRange)
                map.reveal(player,(int)e.x,(int)e.y,rules.spyRadius);
        }
}

// 裂缝产生器（RA2 原作）：黑幕半径内敌军迷雾打回不可见（间谍卫星也无法穿透；低电时失效）
void World::applyGapShroud() {
    const int R = 10;
    const bool localSight=nativeRulesBaseVersion>=nativeLocalGapSightSceneVersion;
    const bool sourceArea=nativeRulesBaseVersion>=nativeGapAreaSceneVersion;
    const bool groundProjection=nativeRulesBaseVersion>=nativeGapGroundHeightSceneVersion;
    const bool groundSlope=nativeRulesBaseVersion>=nativeGapGroundSlopeSceneVersion && map.native.active();
    const auto queryHeight=[&](size_t index) {
        const int height=map.cells[index].height;
        if(!groundSlope)return height;
        // At the cell center, source slopes9..16 add a full104-lepton
        // level. Half-level slopes truncate to the saved base level.
        const int ramp=map.native.tiles[index].ramp;
        return height+(ramp>=9 && ramp<=16?1:0);
    };
    std::vector<uint8_t> affected(localSight?numPlayers:0,0);
    std::vector<std::vector<uint8_t>> covered(groundProjection?numPlayers:0);
    int minimumFloor=0,maximumCeil=0;bool heightsKnown=false;
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.btype != BldType::GapGenerator || e.player < 0) continue;
        if (players[e.player].lowPower()) continue;
        int cx = (int)e.x, cy = (int)e.y;
        for (int p = 0; p < numPlayers; p++) {
            if (!isEnemy(p, e.player)) continue;
            if(localSight)affected[p]=1;
            auto& f = map.fog[p];
            if(groundProjection) {
                if(!heightsKnown) {
                    int low=255,high=0;
                    for(size_t i=0;i<map.cells.size();++i){const int height=queryHeight(i);low=std::min(low,height);high=std::max(high,height);}
                    minimumFloor=low/2;maximumCeil=(high+1)/2;heightsKnown=true;
                }
                // The source counter field is on the projected plane. Ground
                // queries use one cell at even levels, both diagonal cells at
                // odd levels. Accumulate every generator before resolving them.
                const int level=map.inBounds(cx,cy)?map.at(cx,cy).height:0;
                const int centerX=cx-level/2,centerY=cy-level/2;
                auto& mask=covered[p];if(mask.empty())mask.resize(map.cells.size(),0);
                const int x0=std::max(0,centerX-R+minimumFloor),x1=std::min(map.w-1,centerX+R+maximumCeil);
                const int y0=std::max(0,centerY-R+minimumFloor),y1=std::min(map.h-1,centerY+R+maximumCeil);
                for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
                    const size_t index=(size_t)y*map.w+x;const int height=queryHeight(index);
                    const int dx=x-height/2-centerX,dy=y-height/2-centerY;
                    const bool floorCovered=dx*dx+dy*dy<(R+1)*(R+1);
                    const bool ceilCovered=(height&1)?(dx-1)*(dx-1)+(dy-1)*(dy-1)<(R+1)*(R+1):floorCovered;
                    mask[index]|=(floorCovered?1:0)|(ceilCovered?2:0);
                }
                continue;
            }
            for (int dy = -R; dy <= R; dy++)
                for (int dx = -R; dx <= R; dx++) {
                    const int squared=dx*dx+dy*dy;
                    // The original flat-cell loop includes distances strictly
                    // below (R+1)^2; older scenes retain their saved mask.
                    if(sourceArea?squared>=(R+1)*(R+1):squared>R*R)continue;
                    int x = cx + dx, y = cy + dy;
                    if (!map.inBounds(x, y)) continue;
                    f[(size_t)y * map.w + x] = FOG_UNSEEN;
                }
        }
    }
    if(groundProjection)for(int p=0;p<numPlayers;++p)
        for(size_t i=0;i<covered[p].size();++i)if(covered[p][i]==3)map.fog[p][i]=FOG_UNSEEN;
    // Satellite illumination remains shrouded. Nearby physical observations
    // survive every overlapping gap and expire when their observers leave.
    if(localSight)for(int player=0;player<numPlayers;++player)if(affected[player])
        for(int owner=0;owner<numPlayers;++owner)
            if(owner==player || (sharedVision && isAllied(player,owner)))revealLocalSight(player,owner);
}

