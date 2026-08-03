#include "game/world.h"
#include <cmath>
#include <algorithm>

void World::updateFog(int player) {
    map.clearVisible(player);
    // 间谍渗透雷达 或 间谍卫星（RA2 原作：建成后全图常亮，被毁后恢复战争迷雾）
    if (players[player].revealTimer > 0 || hasBld(player, BldType::SpySat)) {
        for (int y = 0; y < map.h; y++)
            for (int x = 0; x < map.w; x++)
                map.reveal(player, x, y, 0);
        return;
    }
    for (const Ent& e : ents) {
        if (!e.alive || e.player != player) continue;
        int sight;
        int cx, cy;
        if (e.isBuilding) {
            sight = bldDef(e.btype).sight;
            cx = (int)e.x + bldDef(e.btype).w / 2;
            cy = (int)e.y + bldDef(e.btype).h / 2;
        } else {
            sight = unitDef(e.utype).sight;
            cx = (int)e.x; cy = (int)e.y;
        }
        map.reveal(player, cx, cy, sight);
    }
}

// 裂缝产生器（RA2 原作）：黑幕半径内敌军迷雾打回不可见（间谍卫星也无法穿透；低电时失效）
void World::applyGapShroud() {
    const int R = 10;
    for (const Ent& e : ents) {
        if (!e.alive || !e.isBuilding || e.btype != BldType::GapGenerator || e.player < 0) continue;
        if (players[e.player].lowPower()) continue;
        int cx = (int)e.x, cy = (int)e.y;
        for (int p = 0; p < numPlayers; p++) {
            if (!isEnemy(p, e.player)) continue;
            auto& f = map.fog[p];
            for (int dy = -R; dy <= R; dy++)
                for (int dx = -R; dx <= R; dx++) {
                    if (dx * dx + dy * dy > R * R) continue;
                    int x = cx + dx, y = cy + dy;
                    if (!map.inBounds(x, y)) continue;
                    f[(size_t)y * map.w + x] = FOG_UNSEEN;
                }
        }
    }
}

