#include "game/game.h"
#include "gfx/sprites.h"
#include <algorithm>
#include <cmath>

void Game::drawWarpEffect(const Effect& effect) {
    const int visual=std::clamp(effect.aux,0,2);
    const int frame=std::clamp((int)((int64_t)effect.age*g_sprites.warpFrames(visual)/effect.maxAge),0,g_sprites.warpFrames(visual)-1);
    const Sprite& sprite=g_sprites.warpEffect(visual,frame);
    const float x=(effect.x-effect.y)*(TILE_W/2.0f)-camX;
    float y=(effect.x+effect.y)*(TILE_H/2.0f)-camY;
    const int tx=(int)std::floor(effect.x),ty=(int)std::floor(effect.y);
    if(world.map.inBounds(tx,ty)) y-=heightScreenY(world.map.at(tx,ty).height);
    // Original SHP canvases use a 60-pixel cell; fit the existing 64-pixel world grid.
    const float scale=TILE_W/60.0f;
    DrawTextureEx(sprite.tex,{x-sprite.ox*scale,y-sprite.oy*scale},0,scale,Color{255,255,255,170});
}
