#include "gfx/sprites.h"
#include "gfx/assets.h"
#include "gfx/vxl.h"
#include "gfx/terrain_material.h"
#include "core/content.h"
#include "core/ini.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

const Color HOUSE_COLORS[MAX_PLAYERS] = {
    {255, 200, 40, 255},  // 金
    {220, 40, 40, 255},   // 红
    {60, 110, 230, 255},  // 蓝
    {60, 180, 80, 255},   // 绿
    {240, 130, 30, 255},  // 橙
    {80, 200, 220, 255},  // 青
    {170, 80, 200, 255},  // 紫
    {230, 130, 170, 255}, // 粉
};

SpriteBank g_sprites;



// ---------------- 缓存 ----------------
static uint64_t keyOf(int cat, int a, int b, int c, int player) {
    return ((uint64_t)cat << 56) | ((uint64_t)(a & 0xFFFF) << 40) |
           ((uint64_t)(b & 0xFF) << 32) | ((uint64_t)(c & 0xFFFF) << 16) | (uint64_t)(player & 0xFF);
}

// ---------------- 外部素材覆盖（assets/sprites/*.png，命名约定见 gfx/assets.h） ----------------
static bool loadSpr(PixBuf& out, const char* fmt, ...) {
    char path[192];
    va_list ap; va_start(ap, fmt);
    vsnprintf(path, sizeof(path), fmt, ap);
    va_end(ap);
    std::string resolved = contentResolve(path);
    return out.loadFromFile(resolved.empty() ? path : resolved.c_str());
}

// 外部 PNG 清洗：去黑/灰 fringe、半透明白边；弱化底部烘焙地皮；剔除孤立伪影色斑（如蓝斑）
// ---------------- RA2 补全：图形别名（已全量绘制专属图形，仅保留兜底入口） ----------------
static UnitType spriteAliasUnit(UnitType t) {
    return t; // 全部单位均有专属图形（外部素材仍可按别名前的原始名覆盖）
}
static BldType spriteAliasBld(BldType t) {
    return t; // 全部建筑均有专属图形
}

// 缺素材：禁止程序生成回退。累计计数，返回 1×1 品红占位；启动时 missingCount()>0 则拒绝进游戏。
static int g_spriteMissingCount = 0;
static PixBuf missingAssetPix(const char* kind, const char* name) {
    g_spriteMissingCount++;
    TraceLog(LOG_ERROR, "SPRITE-MISSING %s=%s (procedural fallback disabled; refuse start)", kind, name);
    fprintf(stderr, "SPRITE-MISSING %s=%s (procedural fallback disabled; refuse start)\n", kind, name);
    PixBuf pb(1, 1);
    pb.set(0, 0, Color{255, 0, 255, 255});
    return pb;
}
int SpriteBank::missingCount() const { return g_spriteMissingCount; }

static bool applyHouseMask(PixBuf& pixels,const std::string& imagePath,Color house) {
    std::filesystem::path maskPath(imagePath);
    maskPath.replace_filename(maskPath.stem().string()+"_remap.png");
    std::string image=contentResolve(imagePath.c_str()),mask=contentResolve(maskPath.generic_string().c_str());
    // A mod replacing a PNG without its mask must not inherit an unrelated base mask.
    if(image.empty() || mask.empty() || std::filesystem::path(image).parent_path()!=std::filesystem::path(mask).parent_path()) return false;
    PixBuf channel;
    if(!channel.loadFromFile(mask.c_str())) return false;
    if(channel.w!=pixels.w || channel.h!=pixels.h) {
        missingAssetPix("remap-mask-size",mask.c_str());return false;
    }
    for(size_t i=0;i<pixels.px.size();++i) if(channel.px[i].a && pixels.px[i].a) {
        float shade=channel.px[i].r/255.0f;
        pixels.px[i].r=(uint8_t)(house.r*shade);
        pixels.px[i].g=(uint8_t)(house.g*shade);
        pixels.px[i].b=(uint8_t)(house.b*shade);
    }
    return true;
}

Sprite SpriteBank::makeSprite(PixBuf&& pb, int ox, int oy) {
    Sprite s;
    s.tex = pb.toTexture();
    s.ox = ox; s.oy = oy;
    return s;
}

// ---------------- RA2 风格地面投影 ----------------
// 在画布内烘烧软椭圆投影（下偏右，RA2 标志性视觉锚点）
static void bakeShadow(PixBuf& pb, int cx, int cy, int rx, int ry) {
    if(rx<=0 || ry<=0)return;
    for (int y = cy - ry; y <= cy + ry; y++)
        for (int x = cx - rx; x <= cx + rx; x++) {
            float ddx = (x - cx) / (float)rx, ddy = (y - cy) / (float)ry;
            float d = ddx * ddx + ddy * ddy;
            if (d > 1.0f) continue;
            uint8_t a = (uint8_t)(86.0f * (1.0f - d));
            pb.blend(x, y, Color{8, 10, 6, a});
        }
}
// 扩充画布（内容偏移到 left,top），用于容纳投影

bool SpriteBank::hasTurret(UnitType t) const {
    return unitHasTurret(spriteAliasUnit(t));
}

// ---------------- 建筑 ----------------

// ---------------- 对外获取（文件素材 / VXL；禁止程序生成回退） ----------------
static void softenResourceEdges(PixBuf& pb) {
    if (pb.w > 0 && pb.h > 0) {
        float cx = (pb.w - 1) * 0.5f, cy = (pb.h - 1) * 0.5f;
        float hw = pb.w * 0.5f, hh = pb.h * 0.5f;
        for (int y = 0; y < pb.h; y++)
            for (int x = 0; x < pb.w; x++) {
                Color c = pb.get(x, y);
                if (c.a < 8) continue;
                float md = fabsf(x - cx) / hw + fabsf(y - cy) / hh;
                if (md > 0.82f && md <= 1.05f) {
                    int a = (int)((1.05f - md) / 0.23f * c.a);
                    c.a = (uint8_t)(a < 0 ? 0 : (a > 255 ? 255 : a));
                    pb.set(x, y, c);
                }
            }
    }
}
const Sprite& SpriteBank::tile(Terrain t, int variant) {
    uint64_t k = keyOf(1, (int)t, variant, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/tile_%s_%d.png", terrainAssetName(t), variant))
        pb = missingAssetPix("tile", terrainAssetName(t));
    if(t==Terrain::Ore || t==Terrain::Gems) softenResourceEdges(pb);
    Sprite s = makeSprite(std::move(pb), TILE_W / 2, 0);
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::resourceTile(Terrain t,int variant,int amount,int capacity) {
    if(t!=Terrain::Ore && t!=Terrain::Gems) return tile(t,variant);
    variant&=7;const int level=resourceVisualLevel(amount,capacity);
    if(level==RESOURCE_VISUAL_LEVELS) return tile(t,variant);
    const auto key=keyOf(1,(int)t,variant,level+1,0);
    if(auto found=cache.find(key);found!=cache.end()) return found->second;
    PixBuf original;
    if(!loadSpr(original,"assets/sprites/tile_%s_%d.png",terrainAssetName(t),variant)) return tile(t,variant);
    softenResourceEdges(original);
    // Keep whole authored mineral clusters; removing random pixels would turn
    // crystals into noise. One deterministic ordering serves all density levels.
    std::vector<int> labels(original.px.size(),-1),pending;
    std::vector<uint32_t> ranks;
    int maxAlpha=0;for(const auto color:original.px) maxAlpha=std::max(maxAlpha,(int)color.a);
    const int coreAlpha=std::max(1,maxAlpha/2);
    for(int y=0;y<original.h;++y) for(int x=0;x<original.w;++x) {
        const int start=y*original.w+x;
        if(original.px[start].a<coreAlpha || labels[start]>=0) continue;
        const int group=(int)ranks.size();ranks.push_back(terrainVisualHash(x,y,variant+97*(int)t));
        pending={start};labels[start]=group;
        while(!pending.empty()) {
            const int pixel=pending.back();pending.pop_back();
            const int px=pixel%original.w,py=pixel/original.w;
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                const int nx=px+dx,ny=py+dy;
                if(nx<0 || ny<0 || nx>=original.w || ny>=original.h) continue;
                const int next=ny*original.w+nx;
                if(original.px[next].a>=coreAlpha && labels[next]<0) { labels[next]=group;pending.push_back(next); }
            }
        }
    }
    // Antialiased fringes can connect otherwise separate crystals. Attach
    // them to the nearest visible core instead of merging through alpha=1.
    for(int y=0;y<original.h;++y) for(int x=0;x<original.w;++x) {
        const int pixel=y*original.w+x;
        if(!original.px[pixel].a || labels[pixel]>=0) continue;
        int best=original.w*original.w+original.h*original.h+1;
        for(int cy=0;cy<original.h;++cy) for(int cx=0;cx<original.w;++cx) {
            const int core=cy*original.w+cx;
            if(original.px[core].a<coreAlpha || labels[core]<0) continue;
            const int distance=(cx-x)*(cx-x)+(cy-y)*(cy-y);
            if(distance<best) { best=distance;labels[pixel]=labels[core]; }
        }
    }
    std::vector<int> order(ranks.size()),position(ranks.size());
    for(size_t i=0;i<order.size();++i) order[i]=(int)i;
    std::sort(order.begin(),order.end(),[&](int a,int b) { return ranks[a]!=ranks[b]?ranks[a]<ranks[b]:a<b; });
    for(size_t i=0;i<order.size();++i) position[order[i]]=(int)i;
    for(int stage=0;stage<RESOURCE_VISUAL_LEVELS;++stage) {
        PixBuf pixels=original;
        const int keep=(stage*(int)order.size()+RESOURCE_VISUAL_LEVELS-1)/RESOURCE_VISUAL_LEVELS;
        for(size_t i=0;i<labels.size();++i) if(labels[i]<0 || position[labels[i]]>=keep) pixels.px[i].a=0;
        cache.emplace(keyOf(1,(int)t,variant,stage+1,0),makeSprite(std::move(pixels),TILE_W/2,0));
    }
    return cache.at(key);
}

const Sprite& SpriteBank::overlaySpr(Overlay o) {
    uint64_t k = keyOf(2, (int)o, 0, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/overlay_%s.png", overlayAssetName(o)))
        pb = missingAssetPix("overlay", overlayAssetName(o));
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height - 1;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::crateSpr() {
    uint64_t k = keyOf(2, 100, 0, 0, 0); // 与 overlay 同槽族，id=100 专供箱子
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/overlay_crate.png"))
        pb = missingAssetPix("overlay", "crate");
    Sprite s = makeSprite(std::move(pb), 0, 0);
    // theater SHP 60×60：南触点约在内容底缘中心
    s.ox = s.tex.width / 2;
    s.oy = s.tex.height - 6;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::finishUnitSprite(uint64_t k, PixBuf&& pb, UnitType t, int player, bool remapToHouse,const Vec2i* groundAnchor,bool grounded) {
    // RA2 风格地面投影：仅地面单位（空军/海军不烘投影）
    const UnitDef& ud = unitDef(t);
    // 恐怖机器人 SHP 画布偏大：缩到约步兵量级
    if (t == UnitType::TerrorDrone || t == UnitType::ChaosDrone) {
        int nw = std::max(1, pb.w * 55 / 100);
        int nh = std::max(1, pb.h * 55 / 100);
        pb = pb.scale(nw, nh);
    }
    int padX=0,padY=0;
    if (grounded || (!ud.isAir() && !ud.isNaval())) {
        int ow = pb.w, oh = pb.h;
        bool inf = ud.isInfantry() || t == UnitType::TerrorDrone || t == UnitType::ChaosDrone;
        const int shadowX=(groundAnchor?groundAnchor->x:ow/2)+3;
        const int shadowY=groundAnchor?groundAnchor->y:(inf?oh-2:(int)(oh*0.72f));
        const int radiusX=inf?7:(int)(ow*0.30f),radiusY=inf?3:(int)(oh*0.10f);
        padX=6;padY=4;int right=6,bottom=4;
        if(groundAnchor) {
            padX=std::max(padX,2-shadowX+radiusX);right=std::max(right,shadowX+radiusX+3-ow);
            padY=std::max(padY,2-shadowY+radiusY);bottom=std::max(bottom,shadowY+radiusY+3-oh);
        }
        PixBuf canvas(ow+padX+right,oh+padY+bottom);
        bakeShadow(canvas,padX+shadowX,padY+shadowY,radiusX,radiusY);
        canvas.blit(pb,padX,padY);
        pb = std::move(canvas);
    }
    if(remapToHouse) pb.remap(Pal::REMAP, HOUSE_COLORS[player]);
    // vis* 仅供选中/点选收紧；ox/oy 保持与 gen_assets / 既有核对一致（整幅中心+0.72h）
    int visL = pb.w, visT = pb.h, visR = -1, visB = -1;
    for (int y = 0; y < pb.h; y++)
        for (int x = 0; x < pb.w; x++)
            if (pb.get(x, y).a > 30) {
                if (x < visL) visL = x;
                if (x > visR) visR = x;
                if (y < visT) visT = y;
                if (y > visB) visB = y;
            }
    if (visR < 0) { visL = 0; visT = 0; visR = pb.w - 1; visB = pb.h - 1; }
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.visL = visL; s.visT = visT; s.visR = visR; s.visB = visB;
    s.ox = groundAnchor?groundAnchor->x+padX:s.tex.width/2;
    // 地面载具：锚点与 gen_assets 南触点 0.72h 一致；步兵/空/海仍用中心+4
    if(groundAnchor)s.oy=groundAnchor->y+padY;
    else if (!ud.isAir() && !ud.isNaval() && !ud.isInfantry()
        && t != UnitType::TerrorDrone && t != UnitType::ChaosDrone)
        s.oy = (int)(s.tex.height * 0.72f);
    else
        s.oy = s.tex.height / 2 + 4;
    return cache.emplace(k, s).first->second;
}

int SpriteBank::unitBodyFrames(UnitType t) const { return VxlRt::bodyFrames(spriteAliasUnit(t)); }
int SpriteBank::unitTurretFrames(UnitType t) const { return VxlRt::turretFrames(spriteAliasUnit(t)); }

const Sprite& SpriteBank::unitBody(UnitType t, int dir, int frame, int player) {
    UnitType orig = t;
    t = spriteAliasUnit(t);
    dir &= 7;
    // 满载采矿车用 frame=1（只对载具有效；步兵/恐怖机器人 frame 为行走帧）
    bool isMiner = (t == UnitType::Harvester || t == UnitType::ChronoMiner
                || t == UnitType::WarMiner || t == UnitType::SlaveMiner);
    bool walker = unitDef(t).isInfantry() || t == UnitType::TerrorDrone;
    const int nativeFrames=unitBodyFrames(t);
    int fKey = isMiner ? (frame ? 1 : 0) : (walker ? (frame & 1) : std::max(0,frame)%std::max(1,nativeFrames));
    uint64_t k = keyOf(3, (int)t, dir, fKey, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    if(nativeFrames<1)return finishUnitSprite(k,missingAssetPix("voxel animation",unitAssetName(orig)),t,player);
    PixBuf pb;
    // 优先运行时 VXL；无 VXL 再 PNG（SHP 步兵/机器人）；缺失则报错拒绝开局
    Vec2i originAnchor;
    const bool fromVoxel=VxlRt::renderBody(t, dir, fKey, pb, &HOUSE_COLORS[player],t==UnitType::SiegeChopper?&originAnchor:nullptr);
    bool ext=fromVoxel;
    if (!ext) {
        ext = loadSpr(pb, "assets/sprites/unit_%s_d%d_f%d.png", unitAssetName(orig), dir, fKey)
            || (fKey != 0 && loadSpr(pb, "assets/sprites/unit_%s_d%d_f0.png", unitAssetName(orig), dir))
            || loadSpr(pb, "assets/sprites/unit_%s_d%d.png", unitAssetName(orig), dir)
            || (orig != t && (loadSpr(pb, "assets/sprites/unit_%s_d%d_f%d.png", unitAssetName(t), dir, fKey)
                           || (fKey != 0 && loadSpr(pb, "assets/sprites/unit_%s_d%d_f0.png", unitAssetName(t), dir))
                           || loadSpr(pb, "assets/sprites/unit_%s_d%d.png", unitAssetName(t), dir)));
    }
    if (!ext) {
        pb = missingAssetPix("unit", unitAssetName(orig));
    }
    // 采矿车满载：VXL 无独立帧，货舱区域略提亮偏黄
    if (ext && isMiner && fKey == 1 && VxlRt::hasBody(t)) {
        for (int y = 0; y < pb.h; y++)
            for (int x = 0; x < pb.w; x++) {
                Color c = pb.get(x, y);
                if (c.a < 128) continue;
                if (c.r > 150 && c.g < 90 && c.b < 90) continue;
                if (x < pb.w * 35 / 100) {
                    c.r = (uint8_t)std::min(255, (int)(c.r * 1.12f + 18));
                    c.g = (uint8_t)std::min(255, (int)(c.g * 1.08f + 10));
                    c.b = (uint8_t)std::min(255, (int)(c.b * 0.92f));
                    pb.set(x, y, c);
                }
            }
    }
    const bool masked=!fromVoxel && applyHouseMask(pb,TextFormat("assets/sprites/unit_%s_d%d_f%d.png",unitAssetName(orig),dir,fKey),HOUSE_COLORS[player]);
    return finishUnitSprite(k, std::move(pb), t, player, !fromVoxel && !masked,fromVoxel && t==UnitType::SiegeChopper?&originAnchor:nullptr);
}

const Sprite& SpriteBank::unitDeployed(UnitType t,int bodyDir,int turretDir,int player) {
    if(t!=UnitType::SiegeChopper)return unitBody(t,bodyDir,0,player);
    bodyDir&=7;turretDir&=7;
    const uint64_t key=keyOf(20,(int)t,(bodyDir<<3)|turretDir,0,player);
    auto found=cache.find(key);if(found!=cache.end())return found->second;
    PixBuf pixels;Vec2i anchor;
    if(!VxlRt::renderDeployed(t,bodyDir,turretDir,pixels,anchor,&HOUSE_COLORS[player])) {
        pixels=missingAssetPix("deployed unit","schd + schdtur");anchor={pixels.w/2,pixels.h/2};
    }
    return finishUnitSprite(key,std::move(pixels),t,player,false,&anchor,true);
}

const Sprite& SpriteBank::unitUnload(UnitType t, int dir, int player) {
    UnitType orig = t;
    t = spriteAliasUnit(t);
    dir &= 7;
    uint64_t k = keyOf(15, (int)t, dir, 0, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    const bool fromVoxel=VxlRt::renderUnload(t, dir, pb, &HOUSE_COLORS[player]);
    bool ext=fromVoxel;
    if (!ext) {
        ext = loadSpr(pb, "assets/sprites/unit_%s_unload_d%d_f0.png", unitAssetName(orig), dir)
            || (orig != t && loadSpr(pb, "assets/sprites/unit_%s_unload_d%d_f0.png", unitAssetName(t), dir));
    }
    if (!ext) return unitBody(orig, dir, 0, player);
    return finishUnitSprite(k, std::move(pb), t, player, !fromVoxel);
}

const Sprite& SpriteBank::chopperTransform(int phase,int player) {
    phase=std::clamp(phase,0,chopperFrames_-1);
    const uint64_t key=keyOf(21,(int)UnitType::SiegeChopper,phase,0,player);
    if(auto found=cache.find(key);found!=cache.end())return found->second;
    PixBuf pixels;
    const auto name=std::string(TextFormat("assets/sprites/unit_siegechopper_transform_f%d.png",phase));
    if(!loadSpr(pixels,"%s",name.c_str()) || pixels.w!=chopperWidth_ || pixels.h!=chopperHeight_
        || !applyHouseMask(pixels,name,HOUSE_COLORS[player]))pixels=missingAssetPix("chopper-transform",name.c_str());
    return finishUnitSprite(key,std::move(pixels),UnitType::SiegeChopper,player,false,&chopperOrigin_);
}

void SpriteBank::loadChopperTransform() {
    Ini metadata;const auto path=contentResolve("assets/chopper_deploy.ini");
    if(path.empty() || !metadata.load(path.c_str())){missingAssetPix("chopper-transform-metadata","assets/chopper_deploy.ini");return;}
    auto integer=[&](const char* key,int minimum,int maximum,int& value) {
        const char* raw=metadata.get("SiegeChopper",key);char* end=nullptr;const long parsed=raw?strtol(raw,&end,10):0;
        if(!raw || end==raw || *end || parsed<minimum || parsed>maximum){missingAssetPix("chopper-transform-metadata",key);return false;}
        value=(int)parsed;return true;
    };
    bool valid=integer("Frames",1,256,chopperFrames_);
    valid=integer("Width",1,512,chopperWidth_) && valid;valid=integer("Height",1,512,chopperHeight_) && valid;
    valid=integer("OriginX",0,chopperWidth_-1,chopperOrigin_.x) && valid;valid=integer("OriginY",0,chopperHeight_-1,chopperOrigin_.y) && valid;
    valid=integer("Facing",0,7,chopperFacing_) && valid;
    if(valid)for(int phase=0;phase<chopperFrames_;++phase)chopperTransform(phase,0);
}

const Sprite& SpriteBank::unitTurret(UnitType t, int dir, int player, int frame) {
    UnitType orig = t;
    t = spriteAliasUnit(t);
    dir &= 7;
    const int frames=unitTurretFrames(t),phase=std::max(0,frame)%std::max(1,frames);
    uint64_t k = keyOf(4, (int)t, dir, phase, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    if(frames<1)return cache.emplace(k,makeSprite(missingAssetPix("voxel animation",unitAssetName(orig)),0,0)).first->second;
    PixBuf pb;
    const bool fromVoxel=VxlRt::renderTurret(t, dir, pb, &HOUSE_COLORS[player],phase);
    bool ext=fromVoxel;
    if (!ext) {
        ext = loadSpr(pb, "assets/sprites/turret_%s_d%d.png", unitAssetName(orig), dir)
            || (orig != t && loadSpr(pb, "assets/sprites/turret_%s_d%d.png", unitAssetName(t), dir));
    }
    if (!ext) pb = missingAssetPix("turret", unitAssetName(orig));
    if(!fromVoxel && !applyHouseMask(pb,TextFormat("assets/sprites/turret_%s_d%d.png",unitAssetName(orig),dir),HOUSE_COLORS[player]))
        pb.remap(Pal::REMAP, HOUSE_COLORS[player]);
    int visL = pb.w, visT = pb.h, visR = -1, visB = -1;
    for (int y = 0; y < pb.h; y++)
        for (int x = 0; x < pb.w; x++)
            if (pb.get(x, y).a > 30) {
                if (x < visL) visL = x;
                if (x > visR) visR = x;
                if (y < visT) visT = y;
                if (y > visB) visB = y;
            }
    if (visR < 0) { visL = 0; visT = 0; visR = pb.w - 1; visB = pb.h - 1; }
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.visL = visL; s.visT = visT; s.visR = visR; s.visB = visB;
    // 与车体同一锚点规则：叠绘时 body.ox/oy 对齐才不漂
    const UnitDef& ud = unitDef(t);
    s.ox = s.tex.width / 2;
    if (!ud.isAir() && !ud.isNaval() && !ud.isInfantry())
        s.oy = (int)(s.tex.height * 0.72f);
    else
        s.oy = s.tex.height / 2 + 4;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::finishBldSprite(uint64_t k, PixBuf&& pb, int groundY, int player,
                                          bool withShadow, int footW, int footH) {
    // RA2 风格地面投影（底部偏右椭圆）；统一锚点
    int ow = pb.w, oh = pb.h;
    // 油井等 SHP 南角仅 1–2 像素细尖：阴影若钉在尖端，主体白地基会像悬空。
    // 锚点仍用南角（与占地菱形一致）；阴影钉在「够宽」的地基行。
    int shadowY = groundY;
    const int minSolid = std::max(8, ow / 20);
    while (shadowY > 0) {
        int n = 0;
        for (int x = 0; x < ow; x++) if (pb.get(x, shadowY).a > 60) n++;
        if (n >= minSolid) break;
        shadowY--;
    }
    // 南尖 ox：西木 SHP 约定「地基北尖在画布水平中心」(PPM SHP Builder)。
    // S = N + ((fw-fh)*TW/2, (fw+fh)*TH/2) → ox = cw/2 + (fw-fh)*TILE_W/2。
    // 错误用 TILE_W/4 或不透明质心会让 3×2 等矩形建筑相对虚线笼左右偏。
    int contentOx;
    if (footW > 0 && footH > 0) {
        contentOx = ow / 2 + (footW - footH) * (TILE_W / 2);
        if (contentOx < 0) contentOx = 0;
        if (contentOx >= ow) contentOx = ow - 1;
    } else {
        long long tipSum = 0;
        int tipN = 0;
        int tipY = groundY;
        if (tipY < 0) tipY = 0;
        if (tipY >= oh) tipY = oh - 1;
        for (int x = 0; x < ow; x++)
            if (pb.get(x, tipY).a > 60) { tipSum += x; tipN++; }
        contentOx = tipN > 0 ? (int)(tipSum / tipN) : ow / 2;
    }
    // 可见包围盒（内容坐标）— 虚线笼用，剔除透明顶/侧边
    int visL = ow, visT = oh, visR = -1, visB = -1;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++)
            if (pb.get(x, y).a > 60) {
                if (x < visL) visL = x;
                if (x > visR) visR = x;
                if (y < visT) visT = y;
                if (y > visB) visB = y;
            }
    if (visR < 0) { visL = 0; visT = 0; visR = ow - 1; visB = oh - 1; }
    PixBuf canvas(ow + 14, oh + 10);
    if (withShadow) {
        // 阴影居中在南角锚点正下方（略偏南），勿再右偏，否则与占地菱形错位
        bakeShadow(canvas, 6 + contentOx, 4 + shadowY + 2, (int)(ow * 0.38f), 5);
    }
    canvas.blit(pb, 6, 4);
    pb = std::move(canvas);
    // Remapable=no 的科技建筑（油田/医院等）：跳过 house remap，保留原 SHP 色
    // player==-2 哨兵：显式无 remap；中立(-1)仍可灰显可俘建筑
    const bool skipRemap = (player == -2);
    if (!skipRemap)
        pb.remap(Pal::REMAP, player >= 0 ? HOUSE_COLORS[player] : Color{150, 150, 155, 255});
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = contentOx + 6; // blit 左偏移 6 — 南角对齐占地菱形南尖
    s.oy = groundY + 4;   // blit 上偏移 4
    s.visL = visL + 6;
    s.visT = visT + 4;
    s.visR = visR + 6;
    s.visB = visB + 4;
    return cache.emplace(k, s).first->second;
}

const char* SpriteBank::bldSpriteStem(BldType t, Country country) {
    // Prefer faction/country art when PNG exists (matches cage review cards).
    static thread_local char chosen[64];
    const char* base = bldAssetName(t);
    char usa[64], sov[64], yuri[64];
    const char* cands[4];
    int n = 0;
    Faction f = (country == Country::None) ? Faction::Allies : countryFaction(country);
    if (country == Country::America) {
        snprintf(usa, sizeof(usa), "%s_usa", base);
        cands[n++] = usa;
    }
    if (f == Faction::Soviet || f == Faction::China) {
        snprintf(sov, sizeof(sov), "%s_sov", base);
        cands[n++] = sov;
    }
    if (f == Faction::Yuri) {
        snprintf(yuri, sizeof(yuri), "%s_yuri", base);
        cands[n++] = yuri;
    }
    cands[n++] = base;
    for (int i = 0; i < n; i++) {
        if (FileExists(contentPathFmt("assets/sprites/bld_%s.png", cands[i]))) {
            snprintf(chosen, sizeof(chosen), "%s", cands[i]);
            return chosen;
        }
    }
    snprintf(chosen, sizeof(chosen), "%s", base);
    return chosen;
}

const Sprite& SpriteBank::building(BldType t, int player, bool constructing, Country country) {
    BldType orig = t;
    t = spriteAliasBld(t);
    const char* stem = bldSpriteStem(orig, country);
    // key 含 country：同色不同阵营贴图不撞缓存
    uint64_t k = keyOf(5, (int)t, constructing ? 1 : 0, (int)country, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    const char* mid = constructing ? "_scaffold" : "";
    bool ext = loadSpr(pb, "assets/sprites/bld_%s%s.png", stem, mid);
    if (!ext && orig != t) {
        const char* aliasStem = bldSpriteStem(t, country);
        ext = loadSpr(pb, "assets/sprites/bld_%s%s.png", aliasStem, mid);
    }
    // 建造中无独立 scaffold PNG：用 mk 首帧（地基）或成品（均为 MIX 提取），禁止程序脚手架
    if (!ext && constructing) {
        ext = loadSpr(pb, "assets/sprites/bld_%s_mk_f0.png", stem)
            || loadSpr(pb, "assets/sprites/bld_%s.png", stem);
        if (!ext && orig != t) {
            const char* aliasStem = bldSpriteStem(t, country);
            ext = loadSpr(pb, "assets/sprites/bld_%s_mk_f0.png", aliasStem)
                || loadSpr(pb, "assets/sprites/bld_%s.png", aliasStem);
        }
    }
    int groundY; // 内容画布中"占地菱形南角"的 y 坐标（绘制锚点契约，同 bldScreenPos）
    if (!ext) {
        pb = missingAssetPix(constructing ? "bld_scaffold" : "bld", stem);
        groundY = 0;
    } else {
        // 外部 SHP 素材：落地点 = 最低不透明行（地基南角），勿用固定 h-4（透明底边会让建筑浮空/切脚）
        groundY = pb.h - 1;
        while (groundY > 0) {
            bool solid = false;
            for (int x = 0; x < pb.w && !solid; x++) solid = pb.get(x, groundY).a > 60;
            if (solid) break;
            groundY--;
        }
    }
    const BldDef& bd = bldDef(orig);
    return finishBldSprite(k, std::move(pb), groundY, player, true, bd.w, bd.h);
}

const Sprite& SpriteBank::buildingGhost(BldType t, int player, Country country) {
    BldType orig = t;
    t = spriteAliasBld(t);
    const char* stem = bldSpriteStem(orig, country);
    // key 槽位 14：无烘焙投影的放置幽灵（与成品 key 5 分开缓存）
    uint64_t k = keyOf(14, (int)t, (int)country, 0, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    bool ext = loadSpr(pb, "assets/sprites/bld_%s.png", stem)
            || (orig != t && loadSpr(pb, "assets/sprites/bld_%s.png", bldSpriteStem(t, country)));
    int groundY;
    if (!ext) {
        pb = missingAssetPix("bld_ghost", stem);
        groundY = 0;
    } else {
        groundY = pb.h - 1;
        while (groundY > 0) {
            bool solid = false;
            for (int x = 0; x < pb.w && !solid; x++) solid = pb.get(x, groundY).a > 60;
            if (solid) break;
            groundY--;
        }
    }
    const BldDef& bd = bldDef(orig);
    return finishBldSprite(k, std::move(pb), groundY, player, false, bd.w, bd.h);
}

// ===================== 动画系统（art.ini 序列 + mk 建造动画） =====================
void SpriteBank::loadAnimsIni() {
    FILE* f = fopen(contentPathFmt("assets/sprites/anims.ini"), "rb");
    if (!f) {
        std::string alt = contentResolve("assets/sprites/anims.ini");
        if (!alt.empty()) f = fopen(alt.c_str(), "rb");
    }
    if (!f) return;
    char line[256];
    char sec[64] = "";
    auto findUnit = [](const char* nm) -> int {
        for (int i = 0; i < (int)UnitType::COUNT; i++)
            if (strcmp(unitAssetName((UnitType)i), nm) == 0) return i;
        return -1;
    };
    auto findBld = [](const char* nm) -> int {
        for (int i = 0; i < (int)BldType::COUNT; i++)
            if (strcmp(bldAssetName((BldType)i), nm) == 0) return i;
        return -1;
    };
    int curUnit = -1, curBld = -1;
    while (fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '\r' || *p == '\n' || !*p) continue;
        if (*p == '[') {
            char* e = strchr(p, ']');
            if (!e) continue;
            *e = 0;
            strncpy(sec, p + 1, sizeof(sec) - 1); sec[sizeof(sec) - 1] = 0;
            curUnit = -1; curBld = -1;
            if (strncmp(sec, "bld_", 4) == 0) curBld = findBld(sec + 4);
            else curUnit = findUnit(sec);
            continue;
        }
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = p, * val = eq + 1;
        // 修剪空白
        auto trim = [](char* s) {
            char* e = s + strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
            while (*s == ' ' || *s == '\t') s++;
            return s;
        };
        key = trim(key); val = trim(val);
        int iv = atoi(val);
        if (curUnit >= 0) {
            const bool frameCount=!strcmp(key,"walk") || !strcmp(key,"fire") || !strcmp(key,"die") || !strcmp(key,"depfire");
            if(frameCount && (iv<0 || iv>256)) {
                missingAssetPix("animation-metadata",sec);iv=0;
            }
            UnitAnimInfo& ai = uanims[curUnit];
            if (!strcmp(key, "walk")) ai.walk = iv;
            else if (!strcmp(key, "walkrate")) ai.walkRate = iv > 0 ? iv : 4;
            else if (!strcmp(key, "fire")) ai.fire = iv;
            else if (!strcmp(key, "firerate")) ai.fireRate = iv > 0 ? iv : 4;
            else if (!strcmp(key, "die")) ai.die = iv;
            else if (!strcmp(key, "depfire")) ai.depFire = iv;
            else if (!strcmp(key, "dep")) ai.dep = iv != 0;
        } else if (curBld >= 0) {
            if (!strcmp(key, "mk")) banims[curBld] = iv;
        }
    }
    fclose(f);
    TraceLog(LOG_INFO, "anims.ini: %zu units, %zu blds", uanims.size(), banims.size());
}

const UnitAnimInfo& SpriteBank::animInfo(UnitType t) const {
    static const UnitAnimInfo EMPTY;
    auto it = uanims.find((int)t);
    return it != uanims.end() ? it->second : EMPTY;
}

int SpriteBank::bldMkFrames(BldType t) const {
    auto it = banims.find((int)t);
    return it != banims.end() ? it->second : 0;
}

const Sprite& SpriteBank::unitAnim(UnitType t, UAnim a, int dir, int phase, int player) {
    dir &= 7;
    // 站立或无动画素材的单位：直接回退 unitBody（自身带缓存）
    if (a == UAnim::Stand) return unitBody(t, dir, 0, player);
    const auto& info=animInfo(t);
    const int count=a==UAnim::Walk?info.walk:a==UAnim::Fire?info.fire:a==UAnim::DepFire?info.depFire:a==UAnim::Die?info.die:a==UAnim::Dep?(info.dep?1:0):0;
    if(count==0) return unitBody(t,dir,0,player); // explicitly undeclared sequence
    if(phase<0 || phase>=count) phase=std::clamp(phase,0,count-1);
    uint64_t k = keyOf(12, (int)t, ((int)a << 4) | dir, phase & 0xFF, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    const char* nm = unitAssetName(t);
    PixBuf pb;
    bool ok = false;
    switch (a) {
        case UAnim::Walk: ok = loadSpr(pb, "assets/sprites/unit_%s_walk_d%d_f%d.png", nm, dir, phase); break;
        case UAnim::Fire: ok = loadSpr(pb, "assets/sprites/unit_%s_fire_d%d_f%d.png", nm, dir, phase); break;
        case UAnim::Die:  ok = loadSpr(pb, "assets/sprites/unit_%s_die_f%d.png", nm, phase); break;
        case UAnim::Dep:  ok = loadSpr(pb, "assets/sprites/unit_%s_dep_d%d.png", nm, dir); break;
        case UAnim::DepFire: ok = loadSpr(pb, "assets/sprites/unit_%s_depfire_d%d_f%d.png", nm, dir, phase); break;
        default: break;
    }
    if (!ok) pb=missingAssetPix("animation",TextFormat("%s anim=%d dir=%d frame=%d",nm,(int)a,dir,phase));
    return finishUnitSprite(k, std::move(pb), t, player);
}

const Sprite& SpriteBank::buildingMk(BldType t, int frame, int player, Country country) {
    uint64_t k = keyOf(13, (int)t, frame & 0xFF, (int)country, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    const char* stem = bldSpriteStem(t, country);
    if (!loadSpr(pb, "assets/sprites/bld_%s_mk_f%d.png", stem, frame))
        return building(t, player, false, country); // 帧缺失：回退成品
    // 与成品共用地面锚点，避免建造动画逐帧跳动/切脚
    const Sprite& fin = building(t, player, false, country);
    int groundY = fin.oy - 4; // finishBldSprite 把 oy = groundY + 4
    if (groundY < 0 || groundY >= pb.h) {
        groundY = pb.h - 1;
        while (groundY > 0) {
            bool solid = false;
            for (int x = 0; x < pb.w && !solid; x++) solid = pb.get(x, groundY).a > 60;
            if (solid) break;
            groundY--;
        }
    }
    // 若 mk 画布宽高与成品不同，先垫到成品内容尺寸，保证 ox 与放置预览一致
    int finOw = fin.tex.width - 14, finOh = fin.tex.height - 10;
    if (finOw > 0 && finOh > 0 && (pb.w != finOw || pb.h != finOh)) {
        PixBuf padded(finOw, finOh);
        int ox = (finOw - pb.w) / 2, oy = finOh - pb.h; // 底对齐（地基）
        if (oy < 0) oy = 0;
        padded.blit(pb, ox, oy);
        pb = std::move(padded);
        groundY = fin.oy - 4;
        if (groundY < 0) groundY = 0;
        if (groundY >= pb.h) groundY = pb.h - 1;
    }
    const Sprite& mk = finishBldSprite(k, std::move(pb), groundY, player);
    // 与成品同地面锚点（避免建造逐帧跳脚）；可见包围盒保留本帧真实内容，
    // 否则选中笼/血条按成品尺寸包住半成品，会出现「大空笼子里浮着一小块」
    const_cast<Sprite&>(mk).ox = fin.ox;
    const_cast<Sprite&>(mk).oy = fin.oy;
    return mk;
}

const Sprite& SpriteBank::explosion(int frame) {
    frame = clampi(frame, 0, EXPLOSION_FRAMES - 1);
    uint64_t k = keyOf(6, frame, 0, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/fx_explosion_%d.png", frame)) {
        char nm[32]; snprintf(nm, sizeof(nm), "%d", frame);
        pb = missingAssetPix("fx_explosion", nm);
    }
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::muzzle() {
    uint64_t k = keyOf(7, 0, 0, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/fx_muzzle.png"))
        pb = missingAssetPix("fx", "muzzle");
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::projectile(int kind, int dir) {
    uint64_t k = keyOf(8, kind, dir & 7, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/fx_proj_%d_d%d.png", kind, dir & 7)) {
        char nm[32]; snprintf(nm, sizeof(nm), "%d_d%d", kind, dir & 7);
        pb = missingAssetPix("fx_proj", nm);
    }
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2;
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::smoke(int frame) {
    frame = clampi(frame, 0, SMOKE_FRAMES - 1);
    uint64_t k = keyOf(9, frame, 0, 0, 0);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf pb;
    if (!loadSpr(pb, "assets/sprites/fx_smoke_%d.png", frame)) {
        char nm[32]; snprintf(nm, sizeof(nm), "%d", frame);
        pb = missingAssetPix("fx_smoke", nm);
    }
    Sprite s = makeSprite(std::move(pb), 0, 0);
    s.ox = s.tex.width / 2; s.oy = s.tex.height / 2;
    return cache.emplace(k, s).first->second;
}

void SpriteBank::loadWarpAnims() {
    Ini metadata;const auto path=contentResolve("assets/chrono_fx.ini");
    if(path.empty() || !metadata.load(path.c_str())) { missingAssetPix("chrono-metadata","assets/chrono_fx.ini");return; }
    const char* sections[]={"Arrival","Departure","Erase"};
    for(int visual=0;visual<3;++visual) {
        const char* text=metadata.get(sections[visual],"Frames");char* end=nullptr;
        const long count=text?strtol(text,&end,10):0;
        if(!text || end==text || *end || count<1 || count>256) missingAssetPix("chrono-metadata",sections[visual]);
        else warpFrames_[visual]=(int)count;
    }
}
int SpriteBank::warpFrames(int visual) const { return warpFrames_[std::clamp(visual,0,2)]; }
const Sprite& SpriteBank::warpEffect(int visual,int frame) {
    visual=std::clamp(visual,0,2);frame=std::clamp(frame,0,warpFrames(visual)-1);
    const uint64_t key=keyOf(16,visual,frame,0,0);auto found=cache.find(key);if(found!=cache.end()) return found->second;
    const char* names[]={"warpin","warpout","warpaway"};PixBuf pixels;
    if(!loadSpr(pixels,"assets/sprites/fx_%s_%d.png",names[visual],frame)) pixels=missingAssetPix("chrono",names[visual]);
    Sprite sprite=makeSprite(std::move(pixels),0,0);sprite.ox=sprite.tex.width/2;sprite.oy=sprite.tex.height/2;
    return cache.emplace(key,sprite).first->second;
}

// ---------------- 图标 ----------------
// RA2 原作 cameo = 带天空/地面的小场景照（非深色底）：上部钢青天空渐变+云斑，
// 地平线 ~63% 处，下部棕灰土地+噪点；建筑按地面锚点立于地面，底部软椭圆投影。
const Sprite& SpriteBank::iconUnit(UnitType t, int player) {
    uint64_t k = keyOf(10, (int)t, 0, 0, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf ext;
    // cameo.pal 图标勿 house-remap（会把天空/涂装染坏）
    const char* stem = unitAssetName(t);
    if (loadSpr(ext, "assets/sprites/icon_unit_%s.png", stem)) {
        Sprite s = makeSprite(std::move(ext), 0, 0);
        return cache.emplace(k, s).first->second;
    }
    PixBuf miss = missingAssetPix("icon_unit", stem);
    Sprite s = makeSprite(std::move(miss), 0, 0);
    return cache.emplace(k, s).first->second;
}

const Sprite& SpriteBank::iconBld(BldType t, int player) {
    uint64_t k = keyOf(11, (int)t, 0, 0, player);
    auto it = cache.find(k);
    if (it != cache.end()) return it->second;
    PixBuf ext;
    if (loadSpr(ext, "assets/sprites/icon_bld_%s.png", bldAssetName(t))) {
        // cameo 已是成品色，勿 remap
        Sprite s = makeSprite(std::move(ext), 0, 0);
        return cache.emplace(k, s).first->second;
    }
    PixBuf miss = missingAssetPix("icon_bld", bldAssetName(t));
    Sprite s = makeSprite(std::move(miss), 0, 0);
    return cache.emplace(k, s).first->second;
}

void SpriteBank::init() {
    inited = true;
    g_spriteMissingCount = 0;
    VxlRt::init(); // 运行时 VXL（调色板 / VPL）
    loadWarpAnims();
    loadChopperTransform();
    loadAnimsIni(); // 动画元数据（walk/fire/die/mk 帧数）
    // Validate declared frames once, before Game decides whether startup is allowed.
    for(const auto& entry:uanims) {
        UnitType type=(UnitType)entry.first;const auto& info=entry.second;
        for(int direction=0;direction<8;++direction) {
            for(int phase=0;phase<info.walk;++phase) unitAnim(type,UAnim::Walk,direction,phase,0);
            for(int phase=0;phase<info.fire;++phase) unitAnim(type,UAnim::Fire,direction,phase,0);
            for(int phase=0;phase<info.depFire;++phase) unitAnim(type,UAnim::DepFire,direction,phase,0);
            if(info.dep) unitAnim(type,UAnim::Dep,direction,0,0);
        }
        for(int phase=0;phase<info.die;++phase) unitAnim(type,UAnim::Die,0,phase,0);
    }
    // 预载必需地形/装饰/特效（缺失计入 missingCount，启动侧拒绝进游戏）
    for (int t = 0; t <= (int)Terrain::Bridge; t++)
        for (int v = 0; v < 8; v++) tile((Terrain)t, v);
    for (int o = 1; o <= (int)Overlay::Rock2; o++) overlaySpr((Overlay)o);
    crateSpr();
    for (int f = 0; f < EXPLOSION_FRAMES; f++) explosion(f);
    for (int f = 0; f < SMOKE_FRAMES; f++) smoke(f);
    muzzle();
    for(int visual=0;visual<3;++visual) for(int frame=0;frame<warpFrames(visual);++frame) warpEffect(visual,frame);
}

// 开局预载（见头文件注释）；仅本地玩家全量 + 中立建筑，AI 玩家单位仍懒加载
// （其新类型首次进入视野时仅 8 方向≈6ms，不可感知），避免开局等待过长
void SpriteBank::preloadMatch(int localPlayer) {
    double t0 = GetTime();
    size_t n0 = cache.size();
    for(Terrain type:{Terrain::Ore,Terrain::Gems}) for(int variant=0;variant<8;++variant) {
        tile(type,variant);resourceTile(type,variant,1,RESOURCE_VISUAL_LEVELS);
    }
    for (int i = 0; i < (int)UnitType::COUNT; i++) {
        UnitType t = (UnitType)i;
        bool isMiner = (t == UnitType::Harvester || t == UnitType::ChronoMiner
                || t == UnitType::WarMiner || t == UnitType::SlaveMiner);
        int frames = (isMiner || unitDef(t).isInfantry() || t == UnitType::TerrorDrone) ? 2 : std::max(1,unitBodyFrames(t));
        for (int d = 0; d < 8; d++)
            for (int f = 0; f < frames; f++) unitBody(t, d, f, localPlayer);
        if(t==UnitType::SiegeChopper)for(int d=0;d<8;++d)unitDeployed(t,d,d,localPlayer);
        if (hasTurret(t))
            for (int d = 0; d < 8; d++)for(int f=0;f<std::max(1,unitTurretFrames(t));++f)unitTurret(t,d,localPlayer,f);
    }
    for (int i = 0; i < (int)BldType::COUNT; i++) {
        BldType t = (BldType)i;
        building(t, localPlayer, false, Country::None);
        building(t, localPlayer, true, Country::None);
        building(t, -1, false, Country::None); // 中立（灰色 remap）预置建筑
    }
    for (int i = 0; i < (int)UnitType::COUNT; i++) iconUnit((UnitType)i, localPlayer);
    for (int i = 0; i < (int)BldType::COUNT; i++) iconBld((BldType)i, localPlayer);
    for (int kind = 0; kind < 2; kind++)
        for (int d = 0; d < 8; d++) projectile(kind, d);
    TraceLog(LOG_INFO, "preloadMatch: +%zu sprites in %.0f ms", cache.size() - n0, (GetTime() - t0) * 1000.0);
}

// ===================== 离线素材生成（--gen-assets） =====================
// 管线：程序绘制 → PNG 文件（assets/sprites/）；游戏运行时直接加载文件，程序生成仅作缺失兜底

// Unit frame keys distinguish miner load, SHP walk, and native HVA phase.

bool SpriteBank::genAssets(const char* /*outDir*/) {
    TraceLog(LOG_ERROR, "genAssets disabled: use tools/ra2pack/gen_assets.py");
    fprintf(stderr, "genAssets disabled: use tools/ra2pack/gen_assets.py\n");
    return false;
}
