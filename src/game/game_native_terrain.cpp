#include "game/game.h"
#include "core/content.h"
#include "gfx/pixel.h"
#include <array>
#include <algorithm>
#include <cmath>

bool Game::bakeNativeTerrain() {
    nativeFogOwners.clear();
    nativeFogOffsets.clear();
    if(terrainTex.id)UnloadTexture(terrainTex);terrainTex={};
    const auto& native=world.map.native;
    if(!native.active() || !validateNativeMap(world.map))return false;
    const char* path=contentPathFmt("assets/maps/native/%s/terrain.png",native.assetId.c_str());
    Image image=LoadImage(path);
    if(!image.data || image.width!=terrainW || image.height!=terrainH) {
        if(image.data)UnloadImage(image);
        TraceLog(LOG_ERROR,"Native terrain image missing or invalid: %s",path);return false;
    }
    const std::string ownerPath=contentPathFmt("assets/maps/native/%s/terrain-owners.png",native.assetId.c_str());
    Image owners=LoadImage(ownerPath.c_str());
    if(!owners.data || owners.width!=terrainW || owners.height!=terrainH) {
        if(owners.data)UnloadImage(owners);UnloadImage(image);
        TraceLog(LOG_ERROR,"Native terrain ownership missing or invalid: %s",ownerPath.c_str());return false;
    }
    const int fw=terrainW/8+2,fh=terrainH/8+2;
    nativeFogOffsets.reserve((size_t)fw*fh+1);nativeFogOffsets.push_back(0);
    ImageFormat(&owners,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    const Color* colors=(const Color*)owners.data;
    bool valid=true;
    for(int y=0;y<fh;++y)for(int x=0;x<fw;++x) {
        std::array<uint32_t,64> ids{};int count=0;
        // Every source pixel contributes. Sampling only the center can reveal
        // an unknown neighboring cliff through a visible coarse mask pixel.
        for(int py=y*8;py<std::min(y*8+8,owners.height);++py)
            for(int px=x*8;px<std::min(x*8+8,owners.width);++px) {
                const Color color=colors[(size_t)py*owners.width+px];
                const uint32_t id=color.a?(uint32_t)color.r|((uint32_t)color.g<<8)|((uint32_t)color.b<<16):0;
                if(id>world.map.cells.size() || (id && !world.map.cells[id-1].present)){valid=false;continue;}
                if(id && std::find(ids.begin(),ids.begin()+count,id)==ids.begin()+count)ids[count++]=id;
            }
        nativeFogOwners.insert(nativeFogOwners.end(),ids.begin(),ids.begin()+count);
        nativeFogOffsets.push_back((uint32_t)nativeFogOwners.size());
    }
    UnloadImage(owners);
    if(!valid){UnloadImage(image);nativeFogOwners.clear();nativeFogOffsets.clear();return false;}
    const int width=(int)std::ceil(terrainW*terrainSC),height=(int)std::ceil(terrainH*terrainSC);
    if(image.width!=width || image.height!=height)ImageResizeNN(&image,width,height);
    terrainTex=LoadTextureFromImage(image);UnloadImage(image);
    if(!terrainTex.id)return false;
    SetTextureFilter(terrainTex,TEXTURE_FILTER_POINT);fogMaskTick=-1;return true;
}

bool Game::bakeNativeFogMask(PixBuf& pixels,int width,int height) const {
    if(!world.map.native.active() || nativeFogOffsets.size()!=(size_t)width*height+1)return false;
    for(size_t i=0;i+1<nativeFogOffsets.size();++i) {
        if(nativeFogOffsets[i]==nativeFogOffsets[i+1])continue;
        uint8_t alpha=0;
        for(uint32_t j=nativeFogOffsets[i];j<nativeFogOffsets[i+1];++j) {
            const int index=(int)nativeFogOwners[j]-1;
            const FogState state=world.map.fogAt(localPlayer,index%world.map.w,index/world.map.w);
            alpha=std::max(alpha,(uint8_t)(state==FOG_VISIBLE?0:state==FOG_SEEN?170:255));
        }
        pixels.px[i]=Color{0,0,0,alpha};
    }
    return true;
}
