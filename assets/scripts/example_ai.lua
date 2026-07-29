-- example_ai.lua
-- AI 决策 hook 演示：脚本接管玩家 1 的 AI
-- OnAiThink 返回 true 时跳过内置 AI，返回 false 时仍执行内置 AI
-- 注意：本示例仅在遭遇战模式下接管 AI，每 300 帧（10 秒）决策一次

local thinkCd = 0

function OnAiThink(player)
    -- 仅遭遇战模式，仅接管玩家 1
    if ra2.gameMode() ~= 0 or player ~= 1 then
        return false
    end

    -- 冷却：避免每帧都思考
    if thinkCd > 0 then
        thinkCd = thinkCd - 1
        return true  -- 仍在冷却，跳过内置 AI
    end
    thinkCd = 300  -- 10 秒冷却

    -- 策略：资金充足时主动进攻
    local money = ra2.money(1)
    local tankCount = ra2.countUnits(1, "Rhino") + ra2.countUnits(1, "Grizzly")

    if tankCount >= 5 and money > 1000 then
        -- 寻找敌方单位并攻击
        local eid = ra2.findEnemy(1, 64, 64, 100)
        if eid >= 0 then
            -- 命令所有坦克进攻
            for i = 0, 200 do
                local etype = ra2.entityType(i)
                if etype == "unit:Rhino" or etype == "unit:Grizzly" then
                    if ra2.entityPlayer(i) == 1 then
                        ra2.attack(i, eid)
                    end
                end
            end
        end
    end

    return true  -- 脚本接管，跳过内置 AI
end
