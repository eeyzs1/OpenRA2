-- ============================================================
-- OpenRA2 自定义 AI 脚本示例
-- ============================================================
-- 本文件演示如何用 Lua 完全接管 AI 逻辑。
-- 在 assets/scripts/ 目录下放置 .lua 文件，游戏启动时自动加载。
--
-- 核心 hook: OnAiThink(player)
--   每 AI 思考帧调用。返回 true = 跳过内置 AI（脚本完全接管）。
--   返回 false/nil = 内置 AI 正常执行（脚本仅辅助）。
--
-- 全部 API 见 script.h 注释。关键 API:
--   ra2.tick()                    当前逻辑帧（30帧/秒）
--   ra2.money(p)                  玩家资金
--   ra2.power(p)                  返回 powerMade, powerUsed
--   ra2.lowPower(p)               是否电力不足
--   ra2.numPlayers()              总玩家数
--   ra2.isEnemy(a, b)             a 和 b 是否敌对
--   ra2.playerFaction(p)          玩家阵营（0盟 1苏 2中 3尤）
--   ra2.playerDefeated(p)         玩家是否被歼灭
--   ra2.hasBld(p, name)           是否有某建筑
--   ra2.countUnits(p, name)       某单位数量
--   ra2.countBlds(p, name)        某建筑数量
--   ra2.startBldProd(p, name)     开始建造建筑
--   ra2.startUnitProd(p, name)    开始生产单位
--   ra2.bldProdActive(p)          建筑生产队列是否活跃
--   ra2.bldProdReady(p)           建筑是否就绪可放置
--   ra2.findBldPos(p, name)       寻找可放置位置 → x, y（-1 = 无）
--   ra2.tryPlaceBld(p, name, x, y) 尝试放置建筑
--   ra2.prereqMet(p, name)        前置条件是否满足
--   ra2.findIdleUnits(p, name)    查找空闲单位（name="*"=全部）
--   ra2.findUnits(p, name, x, y, r) 查找区域内单位
--   ra2.findBlds(p, name, x, y, r)  查找区域内建筑
--   ra2.findNearestBld(p, name, x, y) 最近建筑
--   ra2.findEnemy(p, x, y, r)     最近敌方实体
--   ra2.entityPos(eid)            实体位置 → x, y
--   ra2.entityHp(eid)             实体血量
--   ra2.orderMove(ids, x, y, attackMove)  批量移动
--   ra2.orderAttack(ids, targetEid)       批量攻击
--   ra2.orderStop(ids)            批量停止
--   ra2.orderScatter(ids)         批量散布
--   ra2.orderGuard(ids)           批量警戒
--   ra2.orderDeploy(eid)          展开（MCV/部署单位）
--   ra2.orderCapture(ids, bldEid) 工程师占领
--   ra2.orderRepair(ids, bldEid)  工程师修复
--   ra2.orderService(ids, depotEid) 车辆送维修厂
--   ra2.orderGarrison(ids, bldEid)  进驻建筑
--   ra2.orderUngarrison(ids)      撤出驻军
--   ra2.setRally(bldEid, x, y)    设集结点
--   ra2.launchSW(p, name, x, y)   释放超武
--   ra2.swReady(p, name)          超武是否就绪
--   ra2.paradropReady(p)          伞兵是否就绪
--   ra2.orderParadrop(p, x, y)    空降伞兵
--   ra2.mapSize()                 地图尺寸 → w, h
--   ra2.dist(x1, y1, x2, y2)      两点距离
-- ============================================================

-- 每个玩家独立的思考冷却（避免每帧都思考）
local thinkCooldown = {}

-- 建造序列模板：按阵营选择
local function getBuildOrder(faction)
    if faction == 0 then -- 盟军
        return {"PowerPlant", "OreRefinery", "Barracks", "WarFactory",
                "OreRefinery", "Radar", "AirForceCmd", "BattleLab",
                "Pillbox", "PrismTower", "WeatherDevice", "ChronoSphere"}
    elseif faction == 1 then -- 苏军
        return {"TeslaReactor", "OreRefinery", "Barracks", "WarFactory",
                "OreRefinery", "Radar", "AirForceCmd", "BattleLab",
                "SentryGun", "TeslaCoil", "NuclearReactor", "NukeSilo", "IronCurtain"}
    elseif faction == 3 then -- 尤里
        return {"BioReactor", "OreRefinery", "Barracks", "WarFactory",
                "OreRefinery", "Radar", "BattleLab",
                "GatlingCannon", "PsychicTower", "PsychicDominator", "GeneticMutator"}
    else -- 中国（苏系建筑）
        return {"TeslaReactor", "OreRefinery", "Barracks", "WarFactory",
                "OreRefinery", "Radar", "AirForceCmd", "BattleLab",
                "SentryGun", "TeslaCoil", "NuclearReactor", "NukeSilo", "IronCurtain"}
    end
end

-- 主战单位按阵营选择
local function getMainTank(faction)
    if faction == 0 then return "PrismTank" end
    if faction == 1 then return "Apocalypse" end
    if faction == 3 then return "Magnetron" end
    return "Type99"
end

local function getBasicTank(faction)
    if faction == 0 then return "Grizzly" end
    if faction == 1 then return "Rhino" end
    if faction == 3 then return "LasherTank" end
    return "Type99"
end

local function getInfantry(faction)
    if faction == 0 then return "GI" end
    if faction == 1 then return "Conscript" end
    if faction == 3 then return "Initiate" end
    return "PLA"
end

local function getAAUnit(faction)
    if faction == 0 then return "IFV" end
    if faction == 3 then return "GatlingTank" end
    return "FlakTrack"
end

-- 自定义 AI 主入口
function OnAiThink(player)
    -- 只接管 AI 玩家（player > 0），不接管人类玩家
    if player == 0 then return false end

    -- 冷却：每 15 逻辑帧思考一次（约 0.5 秒）
    thinkCooldown[player] = (thinkCooldown[player] or 0) - 1
    if thinkCooldown[player] > 0 then return true end -- 已接管但不思考
    thinkCooldown[player] = 15

    local faction = ra2.playerFaction(player)
    local money = ra2.money(player)
    local pMade, pUsed = ra2.power(player)
    local lowP = ra2.lowPower(player)

    -- ---- 1. 建造序列 ----
    if not ra2.bldProdActive(player) then
        local order = getBuildOrder(faction)
        for _, name in ipairs(order) do
            if ra2.prereqMet(player, name) and ra2.countBlds(player, name) == 0 then
                -- 电力不足时优先补电
                if lowP and (name == "PowerPlant" or name == "TeslaReactor"
                             or name == "BioReactor" or name == "NuclearReactor") then
                    ra2.startBldProd(player, name)
                    break
                end
                -- 资金够就造
                if money > 1000 then
                    ra2.startBldProd(player, name)
                    break
                end
            end
        end
        -- 第二个矿厂
        if ra2.countBlds(player, "OreRefinery") < 2 and ra2.prereqMet(player, "OreRefinery") and money > 2000 then
            ra2.startBldProd(player, "OreRefinery")
        end
    end
    -- 建筑就绪 → 自动放置
    if ra2.bldProdReady(player) then
        -- 需要知道正在造什么：遍历建造序列找缺失的
        local order = getBuildOrder(faction)
        for _, name in ipairs(order) do
            if ra2.countBlds(player, name) == 0 and ra2.prereqMet(player, name) then
                local x, y = ra2.findBldPos(player, name)
                if x >= 0 then ra2.tryPlaceBld(player, name, x, y); break end
            end
        end
    end

    -- ---- 2. 单位生产 ----
    if ra2.hasBld(player, "WarFactory") then
        -- 采矿车维持 3 辆
        local harvName = (faction == 0) and "ChronoMiner" or (faction == 1) and "WarMiner" or "Harvester"
        if ra2.countUnits(player, harvName) < 3 and ra2.prereqMet(player, harvName) then
            ra2.startUnitProd(player, harvName)
        end
        -- 主战坦克
        if ra2.hasBld(player, "BattleLab") then
            local tank = getMainTank(faction)
            if ra2.prereqMet(player, tank) and ra2.unitQueued(player, 1) < 2 then
                ra2.startUnitProd(player, tank)
            end
            -- AA 护航：每 5 辆坦克配 1 辆防空
            local aa = getAAUnit(faction)
            local tankCount = ra2.countUnits(player, getMainTank(faction))
            local aaCount = ra2.countUnits(player, aa)
            if aaCount < tankCount / 5 and ra2.prereqMet(player, aa) then
                ra2.startUnitProd(player, aa)
            end
        else
            local tank = getBasicTank(faction)
            if ra2.prereqMet(player, tank) and ra2.unitQueued(player, 1) < 2 then
                ra2.startUnitProd(player, tank)
            end
        end
    end
    -- 步兵生产
    if ra2.hasBld(player, "Barracks") and ra2.unitQueued(player, 0) < 2 then
        local inf = getInfantry(faction)
        if ra2.countUnits(player, inf) < 10 then
            ra2.startUnitProd(player, inf)
        end
    end

    -- ---- 3. 进攻逻辑 ----
    local army = ra2.findIdleUnits(player, "*")
    if #army >= 10 then
        -- 找最近敌方建筑
        local bld = ra2.findNearestBld(player, "ConYard", -1, -1) -- player=-1 means any player
        if bld < 0 then
            -- 找任意敌方单位
            local x, y = ra2.entityPos(army[1])
            bld = ra2.findEnemy(player, x, y, 100)
        end
        if bld >= 0 then
            ra2.orderAttack(army, bld)
        end
    end

    -- ---- 4. 超武使用 ----
    local swNames = (faction == 0) and {"Lightning", "ChronoShift"}
                   or (faction == 1) and {"Nuke", "IronCurtain"}
                   or (faction == 3) and {"GeneticMutator", "PsychicDominator"}
                   or {"Nuke", "IronCurtain"}
    for _, sw in ipairs(swNames) do
        if ra2.swReady(player, sw) then
            -- 找敌方建造厂
            local target = ra2.findNearestBld(player, "ConYard", -1, -1)
            if target >= 0 then
                local tx, ty = ra2.entityPos(target)
                ra2.launchSW(player, sw, tx, ty)
            end
        end
    end

    -- ---- 5. 伞兵 ----
    if ra2.paradropReady(player) then
        local target = ra2.findNearestBld(player, "ConYard", -1, -1)
        if target >= 0 then
            local tx, ty = ra2.entityPos(target)
            ra2.orderParadrop(player, tx + 3, ty + 3)
        end
    end

    -- ---- 6. 防御反应：敌人靠近基地时回防 ----
    local conYard = ra2.findNearestBld(player, "ConYard", 0, 0)
    if conYard >= 0 then
        local cx, cy = ra2.entityPos(conYard)
        local enemies = ra2.findUnits(-1, "*", cx, cy, 10) -- -1 = any enemy player
        if #enemies > 0 then
            local idle = ra2.findIdleUnits(player, "*")
            if #idle > 0 then
                ra2.orderAttack(idle, enemies[1])
            end
        end
    end

    -- 返回 true = 完全接管，内置 AI 不执行
    return true
end

-- ============================================================
-- 事件 hook（可选）
-- ============================================================

function OnGameStart()
    -- 游戏开始时调用
end

function OnUnitKilled(eid, player, unitType)
    -- 单位死亡
end

function OnBuildingDestroyed(eid, player, bldType)
    -- 建筑被毁
end

function OnPlayerDefeated(player)
    -- 玩家被歼灭
    thinkCooldown[player] = nil
end
