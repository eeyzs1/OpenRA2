#include "game/game.h"
#include "gfx/sprites.h"
#include "core/content.h"
#include <algorithm>
#include <cmath>

const Sprite& Game::nativeProductionIcon(int selector) const {
    static const Sprite empty{};if(!world.nativeProductionType(selector))return empty;
    const auto& definition=*world.nativeTypes[selector-NATIVE_PRODUCTION_BASE];
    if(definition.rulesOnly)return definition.isBuilding?g_sprites.iconBld(definition.building.type,world.players[localPlayer].colorId):g_sprites.iconUnit(definition.unit.type,world.players[localPlayer].colorId);
    if(nativeActorSpriteEpoch!=world.presentationEpoch){unloadNativeActors();nativeActorSpriteEpoch=world.presentationEpoch;}
    const auto& d=*world.nativeTypes[selector-NATIVE_PRODUCTION_BASE];const std::string key="cameo:"+d.id;
    if(auto it=nativeActorSprites.find(key);it!=nativeActorSprites.end())return it->second;
    PixBuf pixels;Sprite sprite;
    if(pixels.loadFromFile(contentPathFmt("assets/maps/native/%s/actors/%s_icon.png",world.map.native.assetId.c_str(),d.art.c_str()))){sprite.tex=pixels.toTexture();SetTextureFilter(sprite.tex,TEXTURE_FILTER_POINT);}
    return nativeActorSprites.emplace(key,std::move(sprite)).first->second;
}

const Sprite& Game::supportPowerIcon(SupportType type) const {
    static const Sprite empty;
    if(world.nativeProductionRules.version<4 || (unsigned)type>=SUPPORT_COUNT)return empty;
    if(nativeActorSpriteEpoch!=world.presentationEpoch){unloadNativeActors();nativeActorSpriteEpoch=world.presentationEpoch;}
    const std::string key="support:"+std::to_string((int)type);
    if(auto found=nativeActorSprites.find(key);found!=nativeActorSprites.end())return found->second;
    PixBuf pixels;Sprite sprite;const auto& icon=world.supportRule(type).icon;
    if(!icon.empty() && pixels.loadFromFile(contentResolve(icon.c_str()).c_str())) {
        sprite.tex=pixels.toTexture();SetTextureFilter(sprite.tex,TEXTURE_FILTER_POINT);
    }
    return nativeActorSprites.emplace(key,std::move(sprite)).first->second;
}

std::vector<int> Game::nativeProductionPalette() const {
    std::vector<int> types;
    for(int i=0;i<(int)world.nativeTypes.size();++i) {
        const auto& d=*world.nativeTypes[i];const int category=d.production.category;
        if(!((uiTab==0 && category==4) || (uiTab==1 && category==5) || (uiTab==2 && category==0) || (uiTab==3 && category>0 && category<4)))continue;
        if(world.nativeProductionVisible(localPlayer,NATIVE_PRODUCTION_BASE+i))types.push_back(NATIVE_PRODUCTION_BASE+i);
    }
    std::stable_sort(types.begin(),types.end(),[&](int a,int b){return world.nativeTypes[a-NATIVE_PRODUCTION_BASE]->production.order<world.nativeTypes[b-NATIVE_PRODUCTION_BASE]->production.order;});
    return types;
}

void Game::drawNativeProductionItem(int selector,Rectangle box,const Texture2D* clocks,int clockCount) {
    const int type=selector-NATIVE_PRODUCTION_BASE;const auto& d=*world.nativeTypes[type];const bool unit=!d.isBuilding;
    const int width=(int)box.width,height=(int)box.height;
    const auto& item=world.productionItem(localPlayer,unit,selector);const bool active=item.active && item.typeIdx==selector;
    DrawRectangleRec(box,Color{8,9,12,255});const auto& icon=nativeProductionIcon(selector);
    if(icon.tex.id)DrawTexturePro(icon.tex,{0,0,(float)icon.tex.width,(float)icon.tex.height},box,{0,0},0,WHITE);
    const auto reason=world.nativeProductionRejection(localPlayer,unit,selector);
    if(!active && reason!=ProductionRejectReason::None)DrawRectangleRec(box,Color{0,0,0,130});
    if(active && !item.ready) {
        if(clocks && clockCount>0){const int frame=std::clamp(item.progress+1,0,clockCount-1);const auto clock=clocks[frame];
            if(clock.id)DrawTexturePro(clock,{0,0,(float)clock.width,(float)clock.height},box,{0,0},0,WHITE);}
        else DrawRectangle((int)box.x,(int)box.y,width,(54-item.progress)*height/54,Color{0,0,0,145});
    }
    DrawRectangleLinesEx(box,active?2.f:1.f,active?Color{220,195,95,255}:Color{40,42,47,255});
    const char* name=d.isBuilding?bldName(d.building.type):unitName(d.unit.type);
    const int cost=world.nativeProductionCost(localPlayer,type);
    if(auditHud)hudItems.push_back({unit,selector,cost,box,active?(float)item.progress/54:0,name,name,14});
    const char* state=active && item.ready?TR(S::Ready):active && item.held?TR(S::Paused):nullptr;
    if(state){DrawRectangle((int)box.x,(int)(box.y+height-17),width,17,Color{0,0,0,185});drawTextS(font,state,(int)box.x+3,(int)(box.y+height-16),13,YELLOW);}
    int queued=0;if(unit)for(int q:world.players[localPlayer].unitQueue[d.production.category])queued+=q==selector;
    if(queued)drawTextS(font,TextFormat("%d",queued+(active?1:0)),(int)(box.x+width-17),(int)box.y+2,14,YELLOW);
    if(CheckCollisionPointRec(mousePos(),box)) {
        const std::string label=std::string(name)+"  $"+std::to_string(cost);
        DrawRectangle((int)box.x-160,(int)box.y,155,42,Color{10,12,16,235});drawTextS(font,label.c_str(),(int)box.x-154,(int)box.y+3,13,WHITE);
        if(reason!=ProductionRejectReason::None && !active)drawTextS(font,TR(productionRejectText(reason)),(int)box.x-154,(int)box.y+23,11,Color{255,180,120,255});
        if(mPressed(MOUSE_LEFT_BUTTON)) {
            if(active && item.ready && d.isBuilding){clearPlacement();placing=true;nativePlacementType=selector;message(TR(S::MsgPlaceBld));}
            else {World::Cmd command{};command.type=unit?World::Cmd::StartUnitProd:World::Cmd::StartBldProd;command.a=selector;issueCmd(command);}
        }
        if(mPressed(MOUSE_RIGHT_BUTTON)) {
            World::Cmd command{};command.a=selector;
            if(queued || (active && (item.held || item.ready)))command.type=unit?World::Cmd::CancelUnitProd:World::Cmd::CancelBldProd;
            else {command.type=unit?World::Cmd::HoldUnitProd:World::Cmd::HoldBldProd;command.b=1;}
            issueCmd(command);
        }
    }
}

void Game::drawNativePlacementGhost() {
    if(!world.nativeProductionType(nativePlacementType))return;
    const int type=nativePlacementType-NATIVE_PRODUCTION_BASE;const auto& d=world.nativeTypes[type];float wx,wy;const auto mouse=mousePos();
    screenToWorld((int)mouse.x,(int)mouse.y,wx,wy);int tx,ty;surfaceAt(wx,wy,tx,ty);
    const int x=tx-d->building.w+1,y=ty-d->building.h+1;const bool allowed=world.canPlaceNativeBuilding(localPlayer,nativePlacementType,x,y);
    for(int dy=0;dy<d->building.h;++dy)for(int dx=0;dx<d->building.w;++dx){int px,py;tileToScreen(x+dx,y+dy,px,py);
        if(world.map.inBounds(x+dx,y+dy))py-=heightScreenY(world.map.at(x+dx,y+dy).height);
        DrawTexture(fogBlack,px-TILE_W/2-(int)camX,py-(int)camY,allowed?Color{40,220,80,55}:Color{255,40,40,70});}
    World::Ent ghost{};ghost.isBuilding=true;ghost.player=localPlayer;ghost.x=(float)x;ghost.y=(float)y;ghost.hp=d->building.hp;ghost.nativeIdentity={type,d,"","",false};
    const auto& sprite=nativeActorSprite(ghost,0);const auto point=bldScreenPos(ghost);
    if(sprite.tex.id)DrawTexture(sprite.tex,(int)point.x-sprite.ox,(int)point.y-sprite.oy,allowed?Color{255,255,255,170}:Color{255,90,90,160});
}
