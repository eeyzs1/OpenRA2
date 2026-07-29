#pragma once
#include "game/world.h"
#include <string>
#include <functional>

// Lua 用户脚本引擎：加载 assets/scripts/*.lua，提供事件 hook 与 API
// 用户可定义以下函数（均可选，缺失则跳过）：
//   OnGameStart()                                  开局调用
//   OnTick(tick)                                   每逻辑帧调用
//   OnUnitKilled(eid, player, unitType)            单位死亡
//   OnBuildingComplete(eid, player, bldType)       建筑建造完成
//   OnBuildingDestroyed(eid, player, bldType)      建筑被毁
//   OnSuperWeapon(player, swType, x, y)            超武释放
//   OnPlayerDefeated(player)                       玩家被歼灭
//   OnTrigger(tag)                                 触发器 Act=Script 调用（tag 为触发器标识）
//   OnAiThink(player)                              AI 决策 hook（返回 true 则跳过内置 AI）
// 触发器条件 Cond=Script 时调用：OnTriggerCond(tag) 返回 bool
//
// API（C++ → Lua 全局表 ra2）：
//   ra2.tick()                    当前逻辑帧
//   ra2.money(player)             玩家资金
//   ra2.giveMoney(player, amount) 增减资金
//   ra2.spawnUnit(player, name, x, y)        返回 eid(失败 -1)
//   ra2.spawnBuilding(player, name, x, y)    返回 eid(失败 -1)
//   ra2.killEntity(eid)
//   ra2.damageEntity(eid, dmg, byPlayer)
//   ra2.countUnits(player, name)
//   ra2.countBlds(player, name)
//   ra2.hasBld(player, name)     返回 bool
//   ra2.eva(player, text)
//   ra2.evaAll(text)
//   ra2.setObjective(text)
//   ra2.win()
//   ra2.lose()
//   ra2.revealMap(player, x, y, radius)
//   ra2.entityPos(eid)            返回 x,y（无效 nil）
//   ra2.entityHp(eid)
//   ra2.entityPlayer(eid)
//   ra2.entityType(eid)           返回 "unit:Grizzly"/"bld:WarFactory"/nil
//   ra2.findEnemy(player, x, y, maxR)  返回 eid(无 -1)
//   ra2.moveTo(eid, x, y)        指令移动
//   ra2.attack(eid, targetEid)   指令攻击
//   ra2.stop(eid)
//   ra2.gameMode()               0 遭遇战 1 战役
//   ra2.missionId()              当前战役关卡序号（0-based，遭遇战返回 -1）

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    // 初始化 Lua 状态并加载 assets/scripts/*.lua；无脚本目录时安全空跑
    void init(World* world, int gameMode, int missionId);

    // 释放 Lua 状态
    void shutdown();

    // ---- 事件分发（World/Game 在对应时机调用）----
    void onGameStart();
    void onTick(uint64_t tick);
    void onUnitKilled(EID eid, int player, UnitType utype);
    void onBuildingComplete(EID eid, int player, BldType btype);
    void onBuildingDestroyed(EID eid, int player, BldType btype);
    void onSuperWeapon(int player, SWType sw, float x, float y);
    void onPlayerDefeated(int player);

    // 触发器 Act=Script：调用 Lua OnTrigger(tag)
    void onTrigger(const std::string& tag);

    // 触发器 Cond=Script：调用 Lua OnTriggerCond(tag)，返回 bool
    bool onTriggerCond(const std::string& tag);

    // AI hook：调用 Lua OnAiThink(player)，返回 true 表示脚本接管（跳过内置 AI）
    bool onAiThink(int player);

    bool enabled() const { return L != nullptr; }

private:
    World* world = nullptr;
    int gameMode = 0;
    int missionId = -1;
    void* L = nullptr;  // lua_State*（void* 避免 header 污染）

    void registerApi();
    bool callHook(const char* name, int nargs, int nresults);
    bool loadScripts();
};

// 全局实例（Game 持有，World 通过 Game 间接调用）
extern ScriptEngine g_script;

// Game 层注册的回调（setObjective/win/lose 需要 Game 状态）
void scriptSetObjectiveCb(std::function<void(const std::string&)> cb);
void scriptSetWinCb(std::function<void()> cb);
void scriptSetLoseCb(std::function<void()> cb);
