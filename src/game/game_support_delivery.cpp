#include "game/game.h"
#include "gfx/sprites.h"

void Game::drawSupportActors() const {
    const float width=(SCREEN_W-sidebarW)/camZoom,height=SCREEN_H/camZoom;
    for(const auto& e:world.ents) {
        if(!e.alive || !e.supportAir || (e.player!=localPlayer && world.map.fogAt(localPlayer,(int)e.x,(int)e.y)!=FOG_VISIBLE))continue;
        auto point=unitScreenPos(e);const auto ground=point;point.y-=unitRenderLift(e);
        const int color=world.players[e.player].colorId;
        const auto& body=e.nativeIdentity.definition && e.nativeIdentity.definition->nativeBody?nativeActorSprite(e):g_sprites.unitBody(e.utype,e.dir,0,color);
        const float left=point.x-body.ox,top=point.y-body.oy;
        if(left+body.tex.width<0 || top+body.tex.height<0 || left>width || top>height)continue;
        DrawEllipse((int)ground.x,(int)ground.y+4,10,4,Color{0,0,0,30});
        DrawTexture(body.tex,(int)left,(int)top,WHITE);
        if(e.supportAir==2) {
            const auto elapsed=world.tick-e.supportSince;const int phase=(int)(elapsed*400/512);
            const auto& canopy=g_sprites.parachute(phase<20?phase:20+(phase-20)%20);
            DrawTexture(canopy.tex,(int)point.x-canopy.ox,(int)point.y-canopy.oy-12,WHITE);
        }
    }
}
