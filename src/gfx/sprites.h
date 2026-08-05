#pragma once
#include "gfx/pixel.h"
#include "game/data.h"
#include "game/map.h"
#include <unordered_map>
#include <array>

// 8 个玩家阵营色（RA2 风格）
static constexpr int MAX_PLAYERS = 8;
extern const Color HOUSE_COLORS[MAX_PLAYERS];

// 单位动画种类（素材由 tools/ra2pack/gen_assets.py 从 art.ini 序列提取）
enum class UAnim : uint8_t { Stand, Walk, Fire, Die, Dep };

// anims.ini 元数据：walk/fire/die=帧数 walkrate/firerate=帧间隔(tick) dep=有部署站姿
struct UnitAnimInfo {
    int walk = 0, walkRate = 4;
    int fire = 0, fireRate = 4;
    int die = 0;
    bool dep = false;
};

// 程序化素材库：启动时用代码生成全部像素素材
class SpriteBank {
public:
    void init(); // 需在 InitWindow 之后调用

    // 地形与装饰
    const Sprite& tile(Terrain t, int variant);
    const Sprite& overlaySpr(Overlay o);
    const Sprite& crateSpr(); // 补给箱（RA2 crate.tem）


    // 单位：dir 0..7（0=东，顺时针），frame 步兵行走帧 0..1
    const Sprite& unitBody(UnitType t, int dir, int frame, int player);
    const Sprite& unitUnload(UnitType t, int dir, int player);
    const Sprite& unitTurret(UnitType t, int dir, int player);
    bool hasTurret(UnitType t) const;

    // 单位动画（art.ini 序列）：phase 0..frames-1；无素材时回退站立帧
    const Sprite& unitAnim(UnitType t, UAnim a, int dir, int phase, int player);
    const UnitAnimInfo& animInfo(UnitType t) const;

    // 建筑（constructing=true 返回脚手架）
    const Sprite& building(BldType t, int player, bool constructing);
    // 放置预览：成品外观但不烘焙地面投影（绿格已表示占地，避免双重阴影）
    const Sprite& buildingGhost(BldType t, int player);
    // 建筑建造动画帧（mk 关键帧）：frame 0..mkFrames-1，无素材回退成品
    const Sprite& buildingMk(BldType t, int frame, int player);
    int bldMkFrames(BldType t) const;

    // 特效
    const Sprite& explosion(int frame);   // 0..11
    const Sprite& muzzle();
    const Sprite& projectile(int kind, int dir); // kind: 0 shell,1 missile
    const Sprite& smoke(int frame);       // 0..5

    // UI 图标
    const Sprite& iconUnit(UnitType t, int player);
    const Sprite& iconBld(BldType t, int player);

    // 开局预载：本地玩家全单位/炮塔/建筑/图标 + 中立建筑 + 抛射体，
    // 避免游戏中首次遭遇新类型时 PNG 解码+纹理上传造成的掉帧毛刺
    void preloadMatch(int localPlayer);

    // 基地车展开/打包等状态图标用
    static constexpr int EXPLOSION_FRAMES = 12;
    static constexpr int SMOKE_FRAMES = 6;

private:
    // 基础（红色占位）像素缓存
    PixBuf baseTile(Terrain t, int variant);
    PixBuf baseOverlay(Overlay o);
    PixBuf baseUnitBody(UnitType t, int dir, int frame);
    PixBuf baseUnitTurret(UnitType t, int dir);
    PixBuf baseBuilding(BldType t, bool constructing);
    PixBuf baseExplosion(int frame);
    PixBuf baseMuzzle();
    PixBuf baseProjectile(int kind, int dir);
    PixBuf baseSmoke(int frame);

    // 内容图（基础绘制 + RA2 风格化后处理：棱边光/轮廓，不含地面投影）
    PixBuf unitContentPix(UnitType t, int dir, int fKey);
    PixBuf turretContentPix(UnitType t, int dir);
    PixBuf bldContentPix(BldType t, bool constructing);
    // bldContentPix 输出：内容画布中"占地菱形南角"的 y 坐标（建筑锚点垂直基准）
    int bldGroundY_ = 0;

public:
    // 离线素材生成（--gen-assets）：全量导出 PNG 到 assets/sprites/ + 审核预览图；无需 InitWindow
    bool genAssets(const char* dir);
private:

    // 通用缓存：key -> Sprite
    std::unordered_map<uint64_t, Sprite> cache;
    const Sprite& get(uint64_t key, PixBuf (SpriteBank::*gen)(), Color remapTo, bool doRemap);
    // 生成辅助
    PixBuf genBaseRaw(uint64_t key, bool& ok);

    Sprite makeSprite(PixBuf&& pb, int ox, int oy);
    bool inited = false;

    // 动画元数据（anims.ini，init 时解析）
    std::unordered_map<int, UnitAnimInfo> uanims;  // UnitType -> info
    std::unordered_map<int, int> banims;           // BldType -> mk 帧数
    void loadAnimsIni();
    // 单位身体精灵的公共收尾：地面投影烘焙 + 阵营色 remap + 锚点
    const Sprite& finishUnitSprite(uint64_t k, PixBuf&& pb, UnitType t, int player);
    // 建筑精灵的公共收尾：地面投影烘焙 + 阵营色 remap + 锚点（groundY=内容画布地面y）
    const Sprite& finishBldSprite(uint64_t k, PixBuf&& pb, int groundY, int player, bool withShadow = true);
};

extern SpriteBank g_sprites;

// 方向工具：从移动向量求 8 方向（0=东 顺时针）
int dirFromVec(float dx, float dy);
