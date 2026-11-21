#include "game/game.h"
#include "gfx/sprites.h"
#include "core/content.h"
#include <cmath>

void Game::unloadNativeActors() const {
    for(auto& [key,sprite]:nativeActorSprites)if(sprite.tex.id)UnloadTexture(sprite.tex);
    nativeActorSprites.clear();nativeActorSpriteEpoch=0;
}

int Game::nativeActorFrame(const World::Ent& e) const {
    const auto& d=e.nativeIdentity.definition;if(!d)return 0;
    if(!e.isBuilding) {
        if(d->nativeRocket){const int poses=std::max(1,d->artFrames/8);
            return (e.dir&7)+8*std::clamp((int)std::lround((e.nativePitch/3.14159265358979323846f+.5f)*(poses-1)),0,poses-1);}
        for(const auto& launcher:world.nativeLaunchers)if(world.valid(launcher.actor) && &world.ents[launcher.actor]==&e
            && world.nativeRocketDefinitions[launcher.definition].noSpawnAlt && world.nativeDockedSpawns(launcher.actor)==0)return (e.dir&7)+8;
        for(const auto& spawner:world.aircraftSpawners)if(world.valid(spawner.owner) && &world.ents[spawner.owner.id]==&e
            && spawner.definition>=0 && world.nativeAircraftDefinitions[spawner.definition].noSpawnAlt
            && world.dockedAircraft(spawner.owner.id)==0)return (e.dir&7)+8;
        return e.dir&7;
    }
    if(d->gate) {
        const int stage=d->gateOpenTicks?e.nativeGateProgress*(d->gateStages-1)/d->gateOpenTicks
            :(e.nativeGatePhase==NativeGatePhase::Open?d->gateStages-1:0);
        return std::clamp(stage,0,d->gateStages-1)+(e.hp*2<=d->building.hp?d->gateStages+1:0);
    }
    if(d->kind==1)return 0;
    if(d->kind>=2 && world.map.inBounds((int)e.x,(int)e.y)) {
        const auto& tile=world.map.native.tiles[(size_t)(int)e.y*world.map.w+(int)e.x];
        int frame=tile.overlayData;
        if(d->wall) {
            const int damage=std::clamp((d->building.hp-e.hp)*d->damageLevels/d->building.hp,0,d->damageLevels-1);
            frame=(frame&15)+16*damage;
        }
        return std::clamp(frame,0,d->artFrames-1);
    }
    return e.hp*2<=d->building.hp?1:0;
}

void Game::drawNativeRockets() const {
    const float width=(SCREEN_W-sidebarW)/camZoom,height=SCREEN_H/camZoom;
    for(const auto& rocket:world.nativeRockets) {
        if(!world.valid(rocket.actor))continue;const auto& e=world.ents[rocket.actor];
        // Ground fog must not cover an airborne sprite at its projected height.
        // Enemy visibility still comes from its actual ground cell.
        if(e.player!=localPlayer && world.map.fogAt(localPlayer,(int)e.x,(int)e.y)!=FOG_VISIBLE)continue;
        auto point=unitScreenPos(e);point.y-=unitRenderLift(e);
        const auto& sprite=nativeActorSprite(e);if(!sprite.tex.id)continue;
        const float left=point.x-sprite.ox,top=point.y-sprite.oy;
        if(left+sprite.tex.width<0 || top+sprite.tex.height<0 || left>width || top>height)continue;
        DrawTexture(sprite.tex,(int)left,(int)top,WHITE);
    }
}

const Sprite& Game::nativeActorSprite(const World::Ent& e,int frame) const {
    static const Sprite empty{};if(!e.nativeIdentity.definition)return empty;
    if(e.nativeIdentity.definition->rulesOnly && !e.nativeIdentity.definition->nativeBody) {
        const int color=e.player>=0 && e.player<world.numPlayers?world.players[e.player].colorId:0;
        const auto country=e.player>=0 && e.player<world.numPlayers?world.players[e.player].country:Country::None;
        return e.isBuilding?g_sprites.buildingGhost(bldDef(e).type,color,country):g_sprites.unitBody(unitDef(e).type,e.dir&7,0,color);
    }
    if(nativeActorSpriteEpoch!=world.presentationEpoch){unloadNativeActors();nativeActorSpriteEpoch=world.presentationEpoch;}
    const auto& definition=*e.nativeIdentity.definition;
    const int state=frame>=0?frame:nativeActorFrame(e);
    uint32_t rgb=e.player>=0?world.nativeColors[e.player]:0xaaaaaa;
    if(definition.rulesOnly && e.player>=0 && e.player<world.numPlayers){const auto color=HOUSE_COLORS[world.players[e.player].colorId];rgb=(uint32_t)color.r<<16|(uint32_t)color.g<<8|color.b;}
    const std::string key=definition.art+":"+std::to_string(state)+":"+std::to_string(rgb);
    if(const auto found=nativeActorSprites.find(key);found!=nativeActorSprites.end())return found->second;
    const std::string filename=definition.art+(e.isBuilding?"_":"_d")+std::to_string(state)+".png";
    PixBuf pixels;Sprite sprite;
    const std::string directory=definition.rulesOnly?"assets/rules/profiles/"+world.rulesProfileId+"/actors/":"assets/maps/native/"+world.map.native.assetId+"/actors/";
    if(pixels.loadFromFile(contentPathFmt("%s",(directory+filename).c_str()))) {
        PixBuf remap;
        if(definition.remappable && (!remap.loadFromFile(contentPathFmt("%s",(directory+filename.substr(0,filename.size()-4)+"_remap.png").c_str()))
            || remap.w!=pixels.w || remap.h!=pixels.h)) {
            TraceLog(LOG_ERROR,"Native actor remap mask missing: %s",filename.c_str());return nativeActorSprites.emplace(key,Sprite{}).first->second;
        }
        const Color house{(uint8_t)(rgb>>16),(uint8_t)(rgb>>8),(uint8_t)rgb,255};
        sprite.visL=pixels.w;sprite.visT=pixels.h;sprite.visR=sprite.visB=-1;
        for(int y=0;y<pixels.h;++y)for(int x=0;x<pixels.w;++x) {
            auto& c=pixels.px[(size_t)y*pixels.w+x];
            const uint8_t intensity=definition.remappable?remap.px[(size_t)y*pixels.w+x].r:0;
            if(c.a && intensity){const float shade=intensity/255.f;c.r=(uint8_t)(house.r*shade);c.g=(uint8_t)(house.g*shade);c.b=(uint8_t)(house.b*shade);}
            if(c.a>=200){sprite.visL=std::min(sprite.visL,x);sprite.visT=std::min(sprite.visT,y);sprite.visR=std::max(sprite.visR,x);sprite.visB=std::max(sprite.visB,y);}
        }
        sprite.ox=definition.originX;sprite.oy=definition.originY;sprite.tex=pixels.toTexture();SetTextureFilter(sprite.tex,TEXTURE_FILTER_POINT);
    } else TraceLog(LOG_ERROR,"Native actor art missing: %s",filename.c_str());
    return nativeActorSprites.emplace(key,std::move(sprite)).first->second;
}

bool Game::drawNativeBuilding(const World::Ent& e,EID id,bool remembered,int frame) const {
    if(!e.isBuilding || !e.nativeIdentity.definition || e.nativeIdentity.definition->rulesOnly)return false;
    const auto& d=bldDef(e);const auto& sprite=nativeActorSprite(e,frame);if(!sprite.tex.id)return true;
    Vector2 p=bldScreenPos(e);
    const int selectedFrame=frame>=0?frame:nativeActorFrame(e);
    if(e.nativeIdentity.definition->kind==3 && selectedFrame>=9 && selectedFrame<=17)p.y-=TILE_H/2;
    DrawTexture(sprite.tex,(int)p.x-sprite.ox,(int)p.y-sprite.oy,remembered?Color{120,120,120,255}:WHITE);
    const bool selected=!remembered && id==selBuilding;
    if(!remembered && selected && e.nativeIdentity.definition->selectable && sprite.visR>=sprite.visL) {
        const int x=(int)p.x-sprite.ox+sprite.visL,y=(int)p.y-sprite.oy+sprite.visT;
        const int width=std::clamp(sprite.visW(),28,220);DrawRectangle(x,y-7,width,4,Color{20,20,20,220});
        DrawRectangle(x,y-7,(int)(width*std::clamp((float)e.hp/d.hp,0.f,1.f)),3,Color{40,220,70,255});
        if(selected)DrawRectangleLines(x-2,y-2,sprite.visW()+4,sprite.visH()+4,WHITE);
    }
    return true;
}
