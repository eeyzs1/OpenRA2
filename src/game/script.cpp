#include "game/script.h"
#include "game/data.h"
#include "gfx/assets.h"
#include "core/util.h"
#include <raylib.h>
#include <cstring>
#include <cstdio>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// Game 层注册的回调（setObjective/win/lose 需要 Game 状态）
static std::function<void(const std::string&)> g_setObjectiveCb;
static std::function<void()> g_winCb;
static std::function<void()> g_loseCb;

void scriptSetObjectiveCb(std::function<void(const std::string&)> cb) { g_setObjectiveCb = cb; }
void scriptSetWinCb(std::function<void()> cb) { g_winCb = cb; }
void scriptSetLoseCb(std::function<void()> cb) { g_loseCb = cb; }

ScriptEngine g_script;

ScriptEngine::ScriptEngine() = default;
ScriptEngine::~ScriptEngine() { shutdown(); }

// ---- API 绑定辅助 ----
static World* W(lua_State* L) {
    lua_getglobal(L, "__world");
    World* w = (World*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return w;
}

static int l_tick(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)W(L)->tick);
    return 1;
}
static int l_money(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    if (p < 0 || p >= W(L)->numPlayers) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, W(L)->players[p].money);
    return 1;
}
static int l_giveMoney(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    int amt = (int)luaL_checkinteger(L, 2);
    if (p >= 0 && p < W(L)->numPlayers) W(L)->players[p].money += amt;
    return 0;
}
static int l_spawnUnit(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    float x = (float)luaL_checknumber(L, 3);
    float y = (float)luaL_checknumber(L, 4);
    UnitType ut;
    if (unitTypeByName(name, ut)) {
        EID id = W(L)->spawnUnit(p, ut, x, y);
        lua_pushinteger(L, id);
        return 1;
    }
    // 自定义变体：spawnUnit(base) + 数值覆盖
    const UnitVariant* var = findVariant(name);
    if (var) {
        EID id = W(L)->spawnUnit(p, var->base, x, y);
        if (id >= 0) {
            World::Ent& e = W(L)->ents[id];
            if (var->hp > 0) e.hp = var->hp;
            // cost/speed/weapon 在 unitDef 中读取，变体仅影响 spawn 时 HP
            // （完整覆盖需要修改 unitDef，此处保持简单：HP 覆盖已满足多数变体需求）
        }
        lua_pushinteger(L, id);
        return 1;
    }
    lua_pushinteger(L, -1);
    return 1;
}
static int l_spawnBuilding(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    int x = (int)luaL_checkinteger(L, 3);
    int y = (int)luaL_checkinteger(L, 4);
    BldType bt;
    if (!bldTypeByName(name, bt)) { lua_pushinteger(L, -1); return 1; }
    EID id = W(L)->spawnBuilding(p, bt, x, y, true);
    lua_pushinteger(L, id);
    return 1;
}
static int l_killEntity(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    W(L)->kill(id);
    return 0;
}
static int l_damageEntity(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    int dmg = (int)luaL_checkinteger(L, 2);
    int byP = (int)luaL_optinteger(L, 3, -1);
    W(L)->damage(id, dmg, byP);
    return 0;
}
static int l_countUnits(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    UnitType ut;
    if (!unitTypeByName(name, ut)) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, W(L)->countUnits(p, ut));
    return 1;
}
static int l_countBlds(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    BldType bt;
    if (!bldTypeByName(name, bt)) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, W(L)->countBlds(p, bt));
    return 1;
}
static int l_hasBld(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    BldType bt;
    if (!bldTypeByName(name, bt)) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, W(L)->hasBld(p, bt));
    return 1;
}
static int l_eva(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* text = luaL_checkstring(L, 2);
    W(L)->eva(p, text);
    return 0;
}
static int l_evaAll(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    W(L)->evaAll(text);
    return 0;
}
static int l_setObjective(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    if (g_setObjectiveCb) g_setObjectiveCb(text);
    return 0;
}
static int l_win(lua_State* L) {
    if (g_winCb) g_winCb();
    return 0;
}
static int l_lose(lua_State* L) {
    if (g_loseCb) g_loseCb();
    return 0;
}
static int l_revealMap(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    int r = (int)luaL_checkinteger(L, 4);
    // 简化实现：以圆心为中心，半径内格子设为可见
    World* w = W(L);
    if (p < 0 || p >= w->numPlayers) return 0;
    // 直接用 updateFog 的可见性：临时全图可见
    // 更精确：标记圆内为已探索（RA2 原作 RevealMap 动作）
    // 这里用 revealTimer 机制：临时全图可见 30 秒
    w->players[p].revealTimer = 900; // 30 帧/秒 × 30 秒
    (void)x; (void)y; (void)r;
    return 0;
}
static int l_entityPos(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (!w->valid(id)) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, w->ents[id].x);
    lua_pushnumber(L, w->ents[id].y);
    return 2;
}
static int l_entityHp(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (!w->valid(id)) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, w->ents[id].hp);
    return 1;
}
static int l_entityPlayer(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (!w->valid(id)) { lua_pushinteger(L, -1); return 1; }
    lua_pushinteger(L, w->ents[id].player);
    return 1;
}
static int l_entityType(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (!w->valid(id)) { lua_pushnil(L); return 1; }
    const auto& e = w->ents[id];
    char buf[64];
    if (e.isBuilding) {
        snprintf(buf, sizeof(buf), "bld:%s", bldAssetName(e.btype));
    } else {
        snprintf(buf, sizeof(buf), "unit:%s", unitAssetName(e.utype));
    }
    lua_pushstring(L, buf);
    return 1;
}
static int l_findEnemy(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float r = (float)luaL_checknumber(L, 4);
    EID id = W(L)->findNearestEnemy(p, x, y, r);
    lua_pushinteger(L, id);
    return 1;
}
static int l_moveTo(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    if (W(L)->valid(id)) W(L)->orderMove({id}, x, y, false);
    return 0;
}
static int l_attack(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    EID tgt = (EID)luaL_checkinteger(L, 2);
    if (W(L)->valid(id) && W(L)->valid(tgt)) W(L)->orderAttack({id}, tgt);
    return 0;
}
static int l_stop(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    if (W(L)->valid(id)) W(L)->orderStop({id});
    return 0;
}
static int l_gameMode(lua_State* L) {
    lua_getglobal(L, "__gameMode");
    int m = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, m);
    return 1;
}
static int l_missionId(lua_State* L) {
    lua_getglobal(L, "__missionId");
    int m = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_pushinteger(L, m);
    return 1;
}

// ---- AI 控制 API ----
// 辅助：从 Lua table（数组 of eid）提取 std::vector<EID>
static std::vector<EID> luaToIds(lua_State* L, int idx) {
    std::vector<EID> ids;
    luaL_checktype(L, idx, LUA_TTABLE);
    int n = (int)lua_rawlen(L, idx);
    ids.reserve(n);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, idx, i);
        ids.push_back((EID)lua_tointeger(L, -1));
        lua_pop(L, 1);
    }
    return ids;
}

static int l_power(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (p < 0 || p >= w->numPlayers) { lua_pushinteger(L, 0); lua_pushinteger(L, 0); return 2; }
    lua_pushinteger(L, w->players[p].powerMade);
    lua_pushinteger(L, w->players[p].powerUsed);
    return 2;
}
static int l_numPlayers(lua_State* L) { lua_pushinteger(L, W(L)->numPlayers); return 1; }
static int l_isEnemy(lua_State* L) {
    int a = (int)luaL_checkinteger(L, 1), b = (int)luaL_checkinteger(L, 2);
    lua_pushboolean(L, W(L)->isEnemy(a, b));
    return 1;
}
static int l_playerDefeated(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    World* w = W(L);
    lua_pushboolean(L, p >= 0 && p < w->numPlayers && w->players[p].defeated);
    return 1;
}
static int l_playerFaction(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (p < 0 || p >= w->numPlayers) { lua_pushinteger(L, -1); return 1; }
    lua_pushinteger(L, (int)w->players[p].faction);
    return 1;
}
static int l_playerCountry(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (p < 0 || p >= w->numPlayers) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, (int)w->players[p].country);
    return 1;
}
static int l_mapSize(lua_State* L) {
    lua_pushinteger(L, W(L)->map.w);
    lua_pushinteger(L, W(L)->map.h);
    return 2;
}
static int l_terrainAt(lua_State* L) {
    int x = (int)luaL_checkinteger(L, 1), y = (int)luaL_checkinteger(L, 2);
    World* w = W(L);
    if (!w->map.inBounds(x, y)) { lua_pushinteger(L, -1); return 1; }
    lua_pushinteger(L, (int)w->map.at(x, y).terrain);
    return 1;
}
static int l_startBldProd(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    BldType bt;
    if (!bldTypeByName(name, bt)) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, W(L)->startBldProd(p, bt));
    return 1;
}
static int l_startUnitProd(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    UnitType ut;
    if (!unitTypeByName(name, ut)) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, W(L)->startUnitProd(p, ut));
    return 1;
}
static int l_bldProdActive(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (p < 0 || p >= w->numPlayers) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, w->players[p].bldProd.active);
    return 1;
}
static int l_bldProdReady(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (p < 0 || p >= w->numPlayers) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, w->players[p].bldProd.ready);
    return 1;
}
static int l_tryPlaceBld(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    int x = (int)luaL_checkinteger(L, 3), y = (int)luaL_checkinteger(L, 4);
    BldType bt;
    if (!bldTypeByName(name, bt)) { lua_pushboolean(L, 0); return 1; }
    World* w = W(L);
    if (w->canPlace(bt, x, y, p)) { lua_pushboolean(L, w->placeBuilding(p, bt, x, y)); return 1; }
    lua_pushboolean(L, 0);
    return 1;
}
static int l_findBldPos(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    BldType bt;
    if (!bldTypeByName(name, bt)) { lua_pushinteger(L, -1); lua_pushinteger(L, -1); return 2; }
    World* w = W(L);
    // 围绕建造厂螺旋搜索
    int ccx = -1, ccy = -1;
    for (const auto& e : w->ents)
        if (e.alive && e.isBuilding && e.player == p && e.btype == BldType::ConYard) { ccx = (int)e.x; ccy = (int)e.y; break; }
    if (ccx < 0) { lua_pushinteger(L, -1); lua_pushinteger(L, -1); return 2; }
    const BldDef& d = bldDef(bt);
    for (int r = 1; r <= 14; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(abs(dx), abs(dy)) != r) continue;
                int bx = ccx + dx, by = ccy + dy;
                if (w->canPlace(bt, bx, by, p)) { lua_pushinteger(L, bx); lua_pushinteger(L, by); return 2; }
            }
    lua_pushinteger(L, -1); lua_pushinteger(L, -1);
    return 2;
}
static int l_cancelProd(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    bool isUnit = lua_toboolean(L, 2);
    W(L)->cancelProd(p, isUnit);
    return 0;
}
static int l_unitQueued(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    int cat = (int)luaL_checkinteger(L, 2);
    lua_pushinteger(L, W(L)->unitQueuedCount(p, cat));
    return 1;
}
static int l_prereqMet(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    BldType bt;
    if (bldTypeByName(name, bt)) { lua_pushboolean(L, W(L)->prereqMet(p, bldDef(bt))); return 1; }
    UnitType ut;
    if (unitTypeByName(name, ut)) { lua_pushboolean(L, W(L)->unitPrereqMet(p, unitDef(ut))); return 1; }
    lua_pushboolean(L, 0);
    return 1;
}
static int l_orderMove(lua_State* L) {
    auto ids = luaToIds(L, 1);
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    bool am = lua_toboolean(L, 4);
    W(L)->orderMove(ids, x, y, am);
    return 0;
}
static int l_orderAttack(lua_State* L) {
    auto ids = luaToIds(L, 1);
    EID tgt = (EID)luaL_checkinteger(L, 2);
    W(L)->orderAttack(ids, tgt);
    return 0;
}
static int l_orderStop(lua_State* L) {
    auto ids = luaToIds(L, 1);
    W(L)->orderStop(ids);
    return 0;
}
static int l_orderScatter(lua_State* L) {
    auto ids = luaToIds(L, 1);
    W(L)->orderScatter(ids);
    return 0;
}
static int l_orderGuard(lua_State* L) {
    auto ids = luaToIds(L, 1);
    W(L)->orderGuard(ids);
    return 0;
}
static int l_orderUnload(lua_State* L) {
    auto ids = luaToIds(L, 1);
    W(L)->orderUnload(ids);
    return 0;
}
static int l_orderDeploy(lua_State* L) {
    EID id = (EID)luaL_checkinteger(L, 1);
    W(L)->orderDeploy(id);
    return 0;
}
static int l_orderCapture(lua_State* L) {
    auto ids = luaToIds(L, 1);
    EID tgt = (EID)luaL_checkinteger(L, 2);
    W(L)->orderCapture(ids, tgt);
    return 0;
}
static int l_orderRepair(lua_State* L) {
    auto ids = luaToIds(L, 1);
    EID tgt = (EID)luaL_checkinteger(L, 2);
    W(L)->orderRepair(ids, tgt);
    return 0;
}
static int l_orderService(lua_State* L) {
    auto ids = luaToIds(L, 1);
    EID tgt = (EID)luaL_checkinteger(L, 2);
    W(L)->orderService(ids, tgt);
    return 0;
}
static int l_orderGarrison(lua_State* L) {
    auto ids = luaToIds(L, 1);
    EID tgt = (EID)luaL_checkinteger(L, 2);
    W(L)->orderGarrison(ids, tgt);
    return 0;
}
static int l_orderUngarrison(lua_State* L) {
    auto ids = luaToIds(L, 1);
    W(L)->orderUngarrison(ids);
    return 0;
}
static int l_setRally(lua_State* L) {
    EID bld = (EID)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    World* w = W(L);
    if (w->valid(bld) && w->ents[bld].isBuilding) {
        w->ents[bld].rallyX = (int)x; w->ents[bld].rallyY = (int)y;
    }
    return 0;
}
static int l_launchSW(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    float x = (float)luaL_checknumber(L, 3), y = (float)luaL_checknumber(L, 4);
    SWType sw;
    if (!swTypeByName(name, sw)) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, W(L)->launchSW(p, sw, x, y));
    return 1;
}
static int l_swReady(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    SWType sw;
    if (!swTypeByName(name, sw)) { lua_pushboolean(L, 0); return 1; }
    World* w = W(L);
    if (p < 0 || p >= w->numPlayers) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, w->players[p].swReady[(int)sw]);
    return 1;
}
static int l_paradropReady(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (p < 0 || p >= w->numPlayers) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, w->players[p].paradropReady);
    return 1;
}
static int l_orderParadrop(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    float x = (float)luaL_checknumber(L, 2), y = (float)luaL_checknumber(L, 3);
    W(L)->orderParadrop(p, x, y);
    return 0;
}
static int l_findNearestBld(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    float x = (float)luaL_checknumber(L, 3), y = (float)luaL_checknumber(L, 4);
    BldType bt;
    if (!bldTypeByName(name, bt)) { lua_pushinteger(L, -1); return 1; }
    World* w = W(L);
    EID best = INVALID_EID; float bd = 1e9f;
    for (size_t i = 0; i < w->ents.size(); i++) {
        const auto& e = w->ents[i];
        if (!e.alive || !e.isBuilding || e.btype != bt) continue;
        if (p >= 0 && e.player != p) continue;
        float d = distf(x, y, e.x, e.y);
        if (d < bd) { bd = d; best = (int)i; }
    }
    lua_pushinteger(L, best);
    return 1;
}
static int l_findUnits(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    float x = (float)luaL_checknumber(L, 3), y = (float)luaL_checknumber(L, 4);
    float r = (float)luaL_checknumber(L, 5);
    UnitType ut = UnitType::COUNT;
    if (strlen(name) > 0 && strcmp(name, "*") != 0) {
        if (!unitTypeByName(name, ut)) { lua_newtable(L); return 1; }
    }
    World* w = W(L);
    lua_newtable(L);
    int idx = 1;
    for (size_t i = 0; i < w->ents.size(); i++) {
        const auto& e = w->ents[i];
        if (!e.alive || e.isBuilding) continue;
        if (p >= 0 && e.player != p) continue;
        if (ut != UnitType::COUNT && e.utype != ut) continue;
        if (r > 0 && distf(x, y, e.x, e.y) > r) continue;
        lua_pushinteger(L, (lua_Integer)i);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}
static int l_findBlds(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    float x = (float)luaL_checknumber(L, 3), y = (float)luaL_checknumber(L, 4);
    float r = (float)luaL_checknumber(L, 5);
    BldType bt = BldType::COUNT;
    if (strlen(name) > 0 && strcmp(name, "*") != 0) {
        if (!bldTypeByName(name, bt)) { lua_newtable(L); return 1; }
    }
    World* w = W(L);
    lua_newtable(L);
    int idx = 1;
    for (size_t i = 0; i < w->ents.size(); i++) {
        const auto& e = w->ents[i];
        if (!e.alive || !e.isBuilding) continue;
        if (p >= 0 && e.player != p) continue;
        if (bt != BldType::COUNT && e.btype != bt) continue;
        if (r > 0 && distf(x, y, e.x, e.y) > r) continue;
        lua_pushinteger(L, (lua_Integer)i);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}
static int l_findIdleUnits(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    const char* name = luaL_checkstring(L, 2);
    UnitType ut = UnitType::COUNT;
    if (strlen(name) > 0 && strcmp(name, "*") != 0) {
        if (!unitTypeByName(name, ut)) { lua_newtable(L); return 1; }
    }
    World* w = W(L);
    lua_newtable(L);
    int idx = 1;
    for (size_t i = 0; i < w->ents.size(); i++) {
        const auto& e = w->ents[i];
        if (!e.alive || e.isBuilding || e.player != p) continue;
        if (ut != UnitType::COUNT && e.utype != ut) continue;
        if (e.state != UState::Idle) continue;
        lua_pushinteger(L, (lua_Integer)i);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}
static int l_lowPower(lua_State* L) {
    int p = (int)luaL_checkinteger(L, 1);
    World* w = W(L);
    if (p < 0 || p >= w->numPlayers) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, w->players[p].lowPower());
    return 1;
}
static int l_dist(lua_State* L) {
    float x1 = (float)luaL_checknumber(L, 1), y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3), y2 = (float)luaL_checknumber(L, 4);
    lua_pushnumber(L, distf(x1, y1, x2, y2));
    return 1;
}

void ScriptEngine::registerApi() {
    lua_State* Ls = (lua_State*)L;
    lua_newtable(Ls);
    lua_setglobal(Ls, "ra2");
    lua_getglobal(Ls, "ra2");
    struct { const char* name; lua_CFunction fn; } fns[] = {
        {"tick", l_tick}, {"money", l_money}, {"giveMoney", l_giveMoney},
        {"spawnUnit", l_spawnUnit}, {"spawnBuilding", l_spawnBuilding},
        {"killEntity", l_killEntity}, {"damageEntity", l_damageEntity},
        {"countUnits", l_countUnits}, {"countBlds", l_countBlds},
        {"hasBld", l_hasBld}, {"eva", l_eva}, {"evaAll", l_evaAll},
        {"setObjective", l_setObjective}, {"win", l_win}, {"lose", l_lose},
        {"revealMap", l_revealMap}, {"entityPos", l_entityPos},
        {"entityHp", l_entityHp}, {"entityPlayer", l_entityPlayer},
        {"entityType", l_entityType}, {"findEnemy", l_findEnemy},
        {"moveTo", l_moveTo}, {"attack", l_attack}, {"stop", l_stop},
        {"gameMode", l_gameMode}, {"missionId", l_missionId},
        // AI 控制 API
        {"power", l_power}, {"numPlayers", l_numPlayers}, {"isEnemy", l_isEnemy},
        {"playerDefeated", l_playerDefeated}, {"playerFaction", l_playerFaction},
        {"playerCountry", l_playerCountry}, {"mapSize", l_mapSize},
        {"terrainAt", l_terrainAt}, {"lowPower", l_lowPower}, {"dist", l_dist},
        {"startBldProd", l_startBldProd}, {"startUnitProd", l_startUnitProd},
        {"bldProdActive", l_bldProdActive}, {"bldProdReady", l_bldProdReady},
        {"tryPlaceBld", l_tryPlaceBld}, {"findBldPos", l_findBldPos},
        {"cancelProd", l_cancelProd}, {"unitQueued", l_unitQueued},
        {"prereqMet", l_prereqMet},
        {"orderMove", l_orderMove}, {"orderAttack", l_orderAttack},
        {"orderStop", l_orderStop}, {"orderScatter", l_orderScatter},
        {"orderGuard", l_orderGuard}, {"orderUnload", l_orderUnload},
        {"orderDeploy", l_orderDeploy}, {"orderCapture", l_orderCapture},
        {"orderRepair", l_orderRepair}, {"orderService", l_orderService},
        {"orderGarrison", l_orderGarrison}, {"orderUngarrison", l_orderUngarrison},
        {"setRally", l_setRally}, {"launchSW", l_launchSW}, {"swReady", l_swReady},
        {"paradropReady", l_paradropReady}, {"orderParadrop", l_orderParadrop},
        {"findNearestBld", l_findNearestBld}, {"findUnits", l_findUnits},
        {"findBlds", l_findBlds}, {"findIdleUnits", l_findIdleUnits},
    };
    for (auto& f : fns) {
        lua_pushcfunction(Ls, f.fn);
        lua_setfield(Ls, -2, f.name);
    }
    lua_pop(Ls, 1);
    // 存储 world 指针到 registry
    lua_pushlightuserdata(Ls, (void*)world);
    lua_setglobal(Ls, "__world");
    lua_pushinteger(Ls, gameMode);
    lua_setglobal(Ls, "__gameMode");
    lua_pushinteger(Ls, missionId);
    lua_setglobal(Ls, "__missionId");
}

bool ScriptEngine::loadScripts() {
    if (!DirectoryExists("assets/scripts")) return false;
    FilePathList files = LoadDirectoryFiles("assets/scripts");
    lua_State* Ls = (lua_State*)L;
    int loaded = 0;
    for (unsigned i = 0; i < files.count; i++) {
        const char* path = files.paths[i];
        const char* ext = strrchr(path, '.');
        if (!ext || strcmp(ext, ".lua") != 0) continue;
        if (luaL_dofile(Ls, path) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua script error in %s: %s", path, err ? err : "?");
            lua_pop(Ls, 1);
        } else {
            loaded++;
        }
    }
    UnloadDirectoryFiles(files);
    if (loaded > 0) TraceLog(LOG_INFO, "Lua: %d script(s) loaded from assets/scripts/", loaded);
    return loaded > 0;
}

void ScriptEngine::init(World* w, int mode, int mission) {
    world = w;
    gameMode = mode;
    missionId = mission;
    L = luaL_newstate();
    if (!L) return;
    luaL_openlibs((lua_State*)L);
    registerApi();
    loadScripts();
}

void ScriptEngine::shutdown() {
    if (L) { lua_close((lua_State*)L); L = nullptr; }
    world = nullptr;
}

bool ScriptEngine::callHook(const char* name, int nargs, int nresults) {
    if (!L) return false;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, name);
    if (!lua_isfunction(Ls, -1)) { lua_pop(Ls, 1 + nargs); return false; }
    // 参数已在栈顶（nargs 个）
    if (lua_pcall(Ls, nargs, nresults, 0) != LUA_OK) {
        const char* err = lua_tostring(Ls, -1);
        TraceLog(LOG_WARNING, "Lua hook %s error: %s", name, err ? err : "?");
        lua_pop(Ls, 1);
        return false;
    }
    return true;
}

void ScriptEngine::onGameStart() {
    if (!L) return;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnGameStart");
    if (lua_isfunction(Ls, -1)) {
        if (lua_pcall(Ls, 0, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnGameStart: %s", err ? err : "?");
            lua_pop(Ls, 1);
        }
    } else lua_pop(Ls, 1);
}

void ScriptEngine::onTick(uint64_t t) {
    if (!L) return;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnTick");
    if (lua_isfunction(Ls, -1)) {
        lua_pushinteger(Ls, (lua_Integer)t);
        if (lua_pcall(Ls, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnTick: %s", err ? err : "?");
            lua_pop(Ls, 1);
        }
    } else lua_pop(Ls, 1);
}

void ScriptEngine::onUnitKilled(EID eid, int player, UnitType utype) {
    if (!L) return;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnUnitKilled");
    if (lua_isfunction(Ls, -1)) {
        lua_pushinteger(Ls, eid);
        lua_pushinteger(Ls, player);
        lua_pushstring(Ls, unitAssetName(utype));
        if (lua_pcall(Ls, 3, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnUnitKilled: %s", err ? err : "?");
            lua_pop(Ls, 1);
        }
    } else lua_pop(Ls, 1);
}

void ScriptEngine::onBuildingComplete(EID eid, int player, BldType btype) {
    if (!L) return;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnBuildingComplete");
    if (lua_isfunction(Ls, -1)) {
        lua_pushinteger(Ls, eid);
        lua_pushinteger(Ls, player);
        lua_pushstring(Ls, bldAssetName(btype));
        if (lua_pcall(Ls, 3, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnBuildingComplete: %s", err ? err : "?");
            lua_pop(Ls, 1);
        }
    } else lua_pop(Ls, 1);
}

void ScriptEngine::onBuildingDestroyed(EID eid, int player, BldType btype) {
    if (!L) return;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnBuildingDestroyed");
    if (lua_isfunction(Ls, -1)) {
        lua_pushinteger(Ls, eid);
        lua_pushinteger(Ls, player);
        lua_pushstring(Ls, bldAssetName(btype));
        if (lua_pcall(Ls, 3, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnBuildingDestroyed: %s", err ? err : "?");
            lua_pop(Ls, 1);
        }
    } else lua_pop(Ls, 1);
}

void ScriptEngine::onSuperWeapon(int player, SWType sw, float x, float y) {
    if (!L) return;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnSuperWeapon");
    if (lua_isfunction(Ls, -1)) {
        lua_pushinteger(Ls, player);
        const char* name = "Nuke";
        switch (sw) { case SWType::Nuke: name="Nuke"; break; case SWType::Lightning: name="Lightning"; break;
                      case SWType::IronCurtain: name="IronCurtain"; break; case SWType::ChronoShift: name="ChronoShift"; break; default: name="?"; }
        lua_pushstring(Ls, name);
        lua_pushnumber(Ls, x);
        lua_pushnumber(Ls, y);
        if (lua_pcall(Ls, 4, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnSuperWeapon: %s", err ? err : "?");
            lua_pop(Ls, 1);
        }
    } else lua_pop(Ls, 1);
}

void ScriptEngine::onPlayerDefeated(int player) {
    if (!L) return;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnPlayerDefeated");
    if (lua_isfunction(Ls, -1)) {
        lua_pushinteger(Ls, player);
        if (lua_pcall(Ls, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnPlayerDefeated: %s", err ? err : "?");
            lua_pop(Ls, 1);
        }
    } else lua_pop(Ls, 1);
}

void ScriptEngine::onTrigger(const std::string& tag) {
    if (!L) return;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnTrigger");
    if (lua_isfunction(Ls, -1)) {
        lua_pushstring(Ls, tag.c_str());
        if (lua_pcall(Ls, 1, 0, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnTrigger: %s", err ? err : "?");
            lua_pop(Ls, 1);
        }
    } else lua_pop(Ls, 1);
}

bool ScriptEngine::onTriggerCond(const std::string& tag) {
    if (!L) return false;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnTriggerCond");
    if (lua_isfunction(Ls, -1)) {
        lua_pushstring(Ls, tag.c_str());
        if (lua_pcall(Ls, 1, 1, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnTriggerCond: %s", err ? err : "?");
            lua_pop(Ls, 1);
            return false;
        }
        bool r = lua_toboolean(Ls, -1) != 0;
        lua_pop(Ls, 1);
        return r;
    }
    lua_pop(Ls, 1);
    return false;
}

bool ScriptEngine::onAiThink(int player) {
    if (!L) return false;
    lua_State* Ls = (lua_State*)L;
    lua_getglobal(Ls, "OnAiThink");
    if (lua_isfunction(Ls, -1)) {
        lua_pushinteger(Ls, player);
        if (lua_pcall(Ls, 1, 1, 0) != LUA_OK) {
            const char* err = lua_tostring(Ls, -1);
            TraceLog(LOG_WARNING, "Lua OnAiThink: %s", err ? err : "?");
            lua_pop(Ls, 1);
            return false;
        }
        bool r = lua_toboolean(Ls, -1) != 0;
        lua_pop(Ls, 1);
        return r;
    }
    lua_pop(Ls, 1);
    return false;
}
