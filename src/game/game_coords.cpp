#include "game/game.h"
#include "gfx/sprites.h"
#include <cmath>

void Game::worldToScreen(float wx, float wy, int& sx, int& sy) const {
    sx = (int)((wx - camX) * camZoom);
    sy = (int)((wy - camY) * camZoom);
}
void Game::screenToWorld(int sx, int sy, float& wx, float& wy) const {
    wx = (float)sx / camZoom + camX;
    wy = (float)sy / camZoom + camY;
}

Vector2 Game::unitScreenPos(const World::Ent& e) const {
    // 瓦片浮点坐标 → 世界像素：sx=(x-y)*32, sy=(x+y)*16
    // 渲染插值：逻辑 30Hz、渲染 60fps，在两帧逻辑位置间线性插值消除跳动感
    // 注意：返回「未乘 zoom」的视口坐标；绘制包在 rlScalef(camZoom) 内
    float ex = e.px + (e.x - e.px) * interpAlpha;
    float ey = e.py + (e.y - e.py) * interpAlpha;
    float wx = (ex - ey) * (TILE_W / 2.0f);
    float wy = (ex + ey) * (TILE_H / 2.0f);
    return {wx - camX, wy - camY};
}

Rectangle Game::unitScreenRect(const World::Ent& e) const {
    Vector2 p = unitScreenPos(e);
    const UnitDef& ud = unitDef(e.utype);
    if (ud.isAir() && e.state != UState::Landed) p.y -= AIR_ALT;
    int cid = (e.player >= 0 && e.player < world.numPlayers)
            ? world.players[e.player].colorId : 0;
    int frame = 0;
    if (ud.canHarvet())
        frame = (e.oreLoad >= World::harvesterCapacity(e.utype)) ? 1 : 0;
    const Sprite& body = g_sprites.unitBody(e.utype, e.dir & 7, frame, cid);
    Rectangle r{
        p.x - (float)body.ox,
        p.y - (float)body.oy,
        (float)body.tex.width,
        (float)body.tex.height
    };
    if (!ud.isAir() && !ud.isNaval()) {
        float ox = r.x, oy = r.y, ow = r.width, oh = r.height;
        r.x += 6; r.y += 4; r.width -= 12; r.height -= 8;
        if (r.width < 8) { r.x = ox; r.width = ow; }
        if (r.height < 8) { r.y = oy; r.height = oh; }
    }
    return r;
}

Vector2 Game::bldScreenPos(const World::Ent& e) const {
    const BldDef& d = bldDef(e.btype);
    // 锚点 = 建筑占地东南角瓦片的南角点
    int px, py;
    tileToScreen((int)e.x + d.w - 1, (int)e.y + d.h - 1, px, py);
    return {(float)px - camX, (float)py + TILE_H - camY};
}

