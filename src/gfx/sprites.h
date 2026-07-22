#pragma once
#include "gfx/pixel.h"
#include "game/data.h"
#include "game/map.h"
#include <unordered_map>
#include <array>

// 8 个玩家阵营色（RA2 风格）
static constexpr int MAX_PLAYERS = 8;
extern const Color HOUSE_COLORS[MAX_PLAYERS];

// 程序化素材库：启动时用代码生成全部像素素材
class SpriteBank {
public:
    void init(); // 需在 InitWindow 之后调用

    // 地形与装饰
    const Sprite& tile(Terrain t, int variant);
    const Sprite& overlaySpr(Overlay o);

    // 单位：dir 0..7（0=东，顺时针），frame 步兵行走帧 0..1
    const Sprite& unitBody(UnitType t, int dir, int frame, int player);
    const Sprite& unitTurret(UnitType t, int dir, int player);
    bool hasTurret(UnitType t) const;

    // 建筑（constructing=true 返回脚手架）
    const Sprite& building(BldType t, int player, bool constructing);

    // 特效
    const Sprite& explosion(int frame);   // 0..11
    const Sprite& muzzle();
    const Sprite& projectile(int kind, int dir); // kind: 0 shell,1 missile
    const Sprite& smoke(int frame);       // 0..5

    // UI 图标
    const Sprite& iconUnit(UnitType t, int player);
    const Sprite& iconBld(BldType t, int player);

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

    // 通用缓存：key -> Sprite
    std::unordered_map<uint64_t, Sprite> cache;
    const Sprite& get(uint64_t key, PixBuf (SpriteBank::*gen)(), Color remapTo, bool doRemap);
    // 生成辅助
    PixBuf genBaseRaw(uint64_t key, bool& ok);

    Sprite makeSprite(PixBuf&& pb, int ox, int oy);
    bool inited = false;
};

extern SpriteBank g_sprites;

// 方向工具：从移动向量求 8 方向（0=东 顺时针）
int dirFromVec(float dx, float dy);
