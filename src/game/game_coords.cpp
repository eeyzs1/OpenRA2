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
    // 瓦片浮点坐标 → 世界像素：sx=(x-y)*32, sy=(x+y)*16 − height*16
    float ex = e.px + (e.x - e.px) * interpAlpha;
    float ey = e.py + (e.y - e.py) * interpAlpha;
    float wx = (ex - ey) * (TILE_W / 2.0f);
    float wy = (ex + ey) * (TILE_H / 2.0f);
    int cx = (int)floorf(ex), cy = (int)floorf(ey);
    if (world.map.inBounds(cx, cy)) {
        if(world.map.hasBridgeDecks()) {
            const float before=(float)world.map.surfaceHeight((int)std::floor(e.px),(int)std::floor(e.py),e.previousNavLayer);
            const float after=(float)world.actorSurfaceHeight(e);
            wy-=(before+(after-before)*interpAlpha)*(TILE_H/2.f);
        } else wy-=(float)heightScreenY(world.map.at(cx,cy).height);
    }
    return {wx - camX, wy - camY};
}

float Game::unitRenderLift(const World::Ent& e) const {
    if(e.supportAir || (e.nativeIdentity.definition && (e.nativeIdentity.definition->nativeRocket || e.nativeJumpjet.motion.started))) {
        const float altitude=e.previousNativeAltitude+(e.nativeAltitude-e.previousNativeAltitude)*interpAlpha;
        const float before=world.map.surfaceHeight((int)e.px,(int)e.py,e.previousNavLayer),after=world.actorSurfaceHeight(e);
        return (altitude/104.f-(before+(after-before)*interpAlpha))*(TILE_H/2.f);
    }
    return e.airborne()?AIR_ALT*e.flightFraction():e.magneticHeight*3.0f;
}

float Game::eventRenderLift(const PresentationEvent& event) const {
    if(event.nativeAltitudePixels>=0)return event.nativeAltitudePixels;
    return event.visualTarget==VisualTarget::Aircraft?AIR_ALT*std::clamp(event.flightFraction,0.f,1.f)
        :3.f*std::clamp(event.magneticHeight,0,12);
}

int Game::unitBodyFrame(const World::Ent& e) const {
    if(e.utype==UnitType::Destroyer)for(const auto& s:world.aircraftSpawners)
        if(world.valid(s.owner) && &world.ents[s.owner.id]==&e)return world.dockedAircraft(s.owner.id)==0?1:0;
    if(unitDef(e).canHarvet())return e.oreLoad>=World::harvesterCapacity(e.utype)?1:0;
    if(e.airborne())return (int)((world.tick/2)%std::max(1,g_sprites.unitBodyFrames(e.utype)));
    return 0;
}

int Game::unitTurretFrame(const World::Ent& e) const {
    return e.fireAnim>0?(int)((world.tick/2)%std::max(1,g_sprites.unitTurretFrames(e.utype))):0;
}

int Game::chopperTransformFrame(const World::Ent& e) const {
    if(e.isBuilding || e.utype!=UnitType::SiegeChopper || e.dir!=g_sprites.chopperTransformFacing()
        || (e.state!=UState::DeployUnfolding && e.state!=UState::DeployFolding))return -1;
    const int count=g_sprites.chopperTransformFrames(),duration=std::max(1,g_gameRules.chopperDeployTicks);
    const int phase=std::clamp((duration-e.deployAnim)*count/duration,0,count-1);
    return e.state==UState::DeployFolding?count-1-phase:phase;
}

Rectangle Game::unitScreenRect(const World::Ent& e) const {
    Vector2 p = unitScreenPos(e);
    const UnitDef& ud = unitDef(e);
    p.y -= unitRenderLift(e);
    int cid = (e.player >= 0 && e.player < world.numPlayers)
            ? world.players[e.player].colorId : 0;
    int frame = unitBodyFrame(e);
    const int transform=chopperTransformFrame(e);
    const Sprite* water=swimmingSprite(e,e.utype,cid);
    const Sprite& body = ud.canHarvet() && world.harvesterUnloading(e)?g_sprites.unitUnload(e.utype,e.dir,cid):water?*water:world.hasNativeIFV(e)?nativeIFVSprite(e,false,frame):e.nativeIdentity.definition && e.nativeIdentity.definition->nativeBody?nativeActorSprite(e):transform>=0?g_sprites.chopperTransform(transform,cid):e.utype==UnitType::SiegeChopper && e.deployed
        ? g_sprites.unitDeployed(e.utype,e.dir,e.turretDir,cid)
        : g_sprites.unitBodyFacing(e.utype,unitBodyFacing(e),frame,cid);
    // 优先用不透明包围盒：与选中角标/点选一致，避免透明画布把命中区甩到左上
    if (body.visR > body.visL && body.visB > body.visT) {
        Rectangle r{
            p.x - (float)body.ox + (float)body.visL,
            p.y - (float)body.oy + (float)body.visT,
            (float)body.visW(),
            (float)body.visH()
        };
        // 点选略放宽，避免深色描边落在包围盒外
        r.x -= 4.f; r.y -= 4.f; r.width += 8.f; r.height += 8.f;
        return r;
    }
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
    if(e.nativeIdentity.definition && !e.nativeIdentity.definition->rulesOnly) {
        int px,py;tileToScreen((int)e.x,(int)e.y,px,py);
        return {(float)px-camX,(float)py+TILE_H/2-heightScreenY(world.map.at((int)e.x,(int)e.y).height)-camY};
    }
    const BldDef& d = bldDef(e);
    // 锚点 = 建筑占地东南角瓦片的南角点（含格高度抬升）
    int px, py;
    int tx = (int)e.x + d.w - 1, ty = (int)e.y + d.h - 1;
    tileToScreen(tx, ty, px, py);
    if (world.map.inBounds(tx, ty))
        py -= heightScreenY(world.map.at(tx, ty).height);
    return {(float)px - camX, (float)py + TILE_H - camY};
}

