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

// 外部精灵库：仅加载 assets/sprites（及运行时 VXL），缺失则记错并拒绝启动
class SpriteBank {
public:
    void init(); // 需在 InitWindow 之后调用
    int missingCount() const; // init/加载过程中累计的缺失数
    bool assetsOk() const { return missingCount() == 0; }

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

    // 建筑贴图 stem：有阵营变体 PNG 时用 bld_X_sov/_yuri/_usa，否则 bldAssetName
    static const char* bldSpriteStem(BldType t, Country country);

    // 建筑（constructing=true 返回脚手架）；country 决定阵营贴图变体
    const Sprite& building(BldType t, int player, bool constructing, Country country = Country::None);
    // 放置预览：成品外观但不烘焙地面投影（绿格已表示占地，避免双重阴影）
    const Sprite& buildingGhost(BldType t, int player, Country country = Country::None);
    // 建筑建造动画帧（mk 逐帧）：frame 0..mkFrames-1，无素材回退成品
    const Sprite& buildingMk(BldType t, int frame, int player, Country country = Country::None);
    int bldMkFrames(BldType t) const;

    // 特效
    const Sprite& explosion(int frame);   // 0..11
    const Sprite& muzzle();
    const Sprite& projectile(int kind, int dir); // kind: 0 shell,1 missile
    const Sprite& smoke(int frame);       // 0..5

    // UI 图标
    const Sprite& iconUnit(UnitType t, int player);
    const Sprite& iconBld(BldType t, int player);

    // 开局预载：本地玩家全单位/炮塔/建筑/图标 + 中立建筑 + 抛射体
    void preloadMatch(int localPlayer);

    static constexpr int EXPLOSION_FRAMES = 12;
    static constexpr int SMOKE_FRAMES = 6;

    // 已禁用：禁止程序生成 PNG（请用 tools/ra2pack/gen_assets.py 从 MIX 提取）
    bool genAssets(const char* dir);

private:
    std::unordered_map<uint64_t, Sprite> cache;
    Sprite makeSprite(PixBuf&& pb, int ox, int oy);
    bool inited = false;

    std::unordered_map<int, UnitAnimInfo> uanims;  // UnitType -> info
    std::unordered_map<int, int> banims;           // BldType -> mk 帧数
    void loadAnimsIni();

    const Sprite& finishUnitSprite(uint64_t k, PixBuf&& pb, UnitType t, int player);
    const Sprite& finishBldSprite(uint64_t k, PixBuf&& pb, int groundY, int player,
                                  bool withShadow = true, int footW = 0, int footH = 0);
};

extern SpriteBank g_sprites;

// 方向工具：从移动向量求 8 方向（0=东 顺时针）
int dirFromVec(float dx, float dy);
