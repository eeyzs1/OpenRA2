#pragma once
#include "game/world.h"

// 遭遇战 AI：建造顺序 + 持续暴兵 + 波次进攻
class SkirmishAI {
public:
    int player = -1;
    int thinkTimer = 0;
    int attackWave = 0;
    int attackTimer = 0;
    int difficulty = 1;         // 0 简单 1 普通 2 困难（思考间隔/进攻阈值/特殊兵频率）
    int8_t hasWater = -1; // 地图是否有水域（-1 未检测）
    int8_t navalPlaceable = -1; // 当前基地半径内是否有可建船厂的水域（缓存）
    int navalCheckCd = 0;   // 船厂选址缓存冷却（思考次数）
    int navalFail = 0;      // 船厂放置连续失败次数

    void reset(int p) { player = p; thinkTimer = 0; attackWave = 0; attackTimer = 0; hasWater = -1;
                        navalPlaceable = -1; navalCheckCd = 0; navalFail = 0; }
    void update(World& w);

private:
    // 建造序列推进
    void doBuildOrder(World& w);
    void doProduction(World& w);
    void doAttack(World& w);
    void doSuperWeapon(World& w);
    void doEngineers(World& w); // 工程师：占领中立科技建筑 + 修复己方受损建筑
    bool tryPlaceBld(World& w, BldType t);
    bool detectWater(World& w); // 全图扫描水域（结果缓存）
    bool navalSiteAvailable(World& w); // 基地半径内存在可放置船厂的水域
    Vec2i findArmyCenter(World& w);
    int countArmy(World& w);
};
