-- example_events.lua
-- 事件 hook 演示：开局奖励、单位阵亡通报、超武预警
-- 此文件在游戏开始时自动加载，删除此文件即可恢复默认行为

-- 开局：给玩家 0 额外资金（遭遇战模式）
function OnGameStart()
    if ra2.gameMode() == 0 then
        ra2.giveMoney(0, 2000)
        ra2.eva(0, "脚本引擎：开局奖励 $2000")
    end
end

-- 每帧：检查玩家 0 资金，低于 500 时提示（每 600 帧=20 秒最多一次）
local lowMoneyCd = 0
function OnTick(tick)
    if lowMoneyCd > 0 then lowMoneyCd = lowMoneyCd - 1 end
    if ra2.gameMode() == 0 and lowMoneyCd == 0 then
        if ra2.money(0) < 500 then
            ra2.eva(0, "警告：资金不足 500")
            lowMoneyCd = 900  -- 30 秒冷却
        end
    end
end

-- 单位阵亡：通报重要单位损失
function OnUnitKilled(eid, player, unitType)
    -- 仅通报玩家 0 的单位
    if player ~= 0 then return end
    -- 重要单位：采矿车、基地车
    if unitType == "Harvester" then
        ra2.eva(0, "采矿车损失，经济受影响")
    elseif unitType == "MCV" then
        ra2.eva(0, "基地车被毁，无法展开新基地")
    end
end

-- 建筑被毁：基地受攻击警告
function OnBuildingDestroyed(eid, player, bldType)
    if player ~= 0 then return end
    ra2.eva(0, "建筑被毁：" .. bldType)
end

-- 超武释放：全图预警
function OnSuperWeapon(player, swType, x, y)
    -- 敌方超武释放时提示玩家 0
    if player ~= 0 then
        ra2.eva(0, "敌方超武释放：" .. swType)
    end
end

-- 玩家被歼灭
function OnPlayerDefeated(player)
    if player ~= 0 then
        ra2.eva(0, "敌方玩家 " .. player .. " 已被歼灭")
    end
end
