-- example_triggers.lua
-- 自定义触发器演示：通过战役 INI 中 Cond=Script / Act=Script 调用
-- 在 campaign.ini 中设置触发器：
--   Trigger=N  Cond=Script  C0=ambush    Act=Script  A0=ambush
-- 游戏会调用 OnTriggerCond("ambush") 判断条件，OnTrigger("ambush") 执行动作

local ambushFired = false

-- 条件检查：游戏第 1800 帧（60 秒）后且玩家有兵营时触发
function OnTriggerCond(tag)
    if tag == "ambush" then
        if ra2.tick() >= 1800 and ra2.hasBld(0, "Barracks") then
            return true
        end
    end
    return false
end

-- 动作执行：在玩家基地附近空降敌方部队
function OnTrigger(tag)
    if tag == "ambush" and not ambushFired then
        ambushFired = true
        -- 查找玩家 0 的建造厂位置
        local eid = -1
        for i = 0, 200 do
            local etype = ra2.entityType(i)
            if etype == "bld:ConYard" and ra2.entityPlayer(i) == 0 then
                eid = i
                break
            end
        end
        if eid >= 0 then
            local x, y = ra2.entityPos(eid)
            -- 在基地附近生成敌方单位
            ra2.spawnUnit(1, "Rhino", x + 8, y + 3)
            ra2.spawnUnit(1, "Rhino", x + 10, y + 5)
            ra2.spawnUnit(1, "Conscript", x + 6, y + 7)
            ra2.spawnUnit(1, "Conscript", x + 9, y + 8)
            ra2.eva(0, "警告：侦测到敌方伏击部队")
            ra2.setObjective("击退敌方伏击")
        end
    end
end
