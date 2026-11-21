#include "gfx/sprites.h"
#include "gfx/vxl.h"
#include <algorithm>

const Sprite& SpriteBank::ifvPart(const std::string& root,int turrets,int turret,int dir,int frame,int player,const Color& house) {
    const int count=VxlRt::ifvFrames(root,turret),phase=count?std::max(0,frame)%count:0;
    const auto key=root+":"+std::to_string(turrets)+":"+std::to_string(turret)+":"+std::to_string(dir&7)
        +":"+std::to_string(phase)+":"+std::to_string((uint32_t)house.r<<16|(uint32_t)house.g<<8|house.b);
    auto [entry,inserted]=ifvKeys.try_emplace(key,((uint64_t)23<<56)|(uint64_t)ifvKeys.size());
    const auto id=entry->second;if(auto found=cache.find(id);found!=cache.end())return found->second;
    PixBuf pixels;Vec2i anchor;
    if(!count || !VxlRt::renderIFV(root,turrets,turret,dir,phase,pixels,anchor,&house)) {
        TraceLog(LOG_ERROR,"Source IFV art missing: %s turret=%d",root.c_str(),turret);
        return cache.emplace(id,Sprite{}).first->second;
    }
    if(turret<0)return finishUnitSprite(id,std::move(pixels),UnitType::IFV,player,false,&anchor);
    Sprite sprite=makeSprite(std::move(pixels),anchor.x,anchor.y);
    sprite.visL=sprite.tex.width;sprite.visT=sprite.tex.height;sprite.visR=sprite.visB=-1;
    // Turrets are not independently selectable. The hull owns the hit rectangle.
    return cache.emplace(id,std::move(sprite)).first->second;
}
