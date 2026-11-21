-- Isolated mechanism fixture. Entity handles must survive an actual Lua snapshot.
local state = {}
local lastKilled = -1
local function contains(values, expected)
    for _, value in ipairs(values) do if value == expected then return true end end
    return false
end
function OnUnitKilled(id)
    lastKilled = id
    if state.legacyUnit then state.legacyCallback = id == state.legacyUnit end
end
function OnBuildingComplete(id)
    state.lastBuildingHp = ra2.entityHp(id)
    if state.reentrant then
        state.reentrant = false
        ra2.killEntity(state.laterMcv)
        state.newborn = ra2.spawnUnit(0, "GI", 30.5, 30.5)
    end
end
function OnGameStart()
    state.oldActor = ra2.spawnUnit(0, "Grizzly", 5.5, 5.5)
    ra2.killEntity(state.oldActor)
    state.deathIdentity = lastKilled == state.oldActor
    state.unit = ra2.spawnUnit(0, "GI", 5.5, 6.5)
    state.oldTarget = ra2.spawnUnit(1, "Conscript", 10.5, 5.5)
    ra2.killEntity(state.oldTarget)
    state.enemy = ra2.spawnUnit(1, "Rhino", 10.5, 6.5)
    state.building = ra2.spawnBuilding(0, "PowerPlant", 15, 15)
    state.distinct = state.oldActor ~= state.unit and state.oldTarget ~= state.enemy
    state.lookups = contains(ra2.findUnits(0, "GI", 0, 0, 0), state.unit)
        and contains(ra2.findBlds(0, "PowerPlant", 0, 0, 0), state.building)
        and ra2.findNearestBld(0, "PowerPlant", 15, 15) == state.building
end
function OnTrigger(tag)
    if tag == "stale" then
        ra2.moveTo(state.oldActor, 30.5, 5.5)
        ra2.orderMove({ state.oldActor }, 30.5, 5.5, false)
        ra2.attack(state.unit, state.oldTarget)
        ra2.orderAttack({ state.unit }, state.oldTarget)
        ra2.damageEntity(state.oldActor, 100000, 1)
        ra2.killEntity(state.oldActor)
        -- Legacy raw slots cannot acquire a later incarnation either.
        ra2.moveTo(state.oldActor & 0xffffffff, 30.5, 5.5)
        state.safe = ra2.entityHp(state.oldActor) == 0 and ra2.entityHp(state.unit) > 0
            and ra2.entityHp(state.oldTarget) == 0 and ra2.entityHp(state.enemy) > 0
            and contains(ra2.findIdleUnits(0, "GI"), state.unit)
    elseif tag == "current" then
        ra2.moveTo(state.unit, 30.5, 5.5)
    elseif tag == "reentrant_prepare" then
        state.firstMcv = ra2.spawnUnit(0, "MCV", 20.5, 20.5)
        state.laterMcv = ra2.spawnUnit(0, "MCV", 30.5, 30.5)
        state.reentrant = true
    elseif tag == "legacy_live_prepare" then
        state.legacyUnit = ra2.spawnUnit(0, "GI", 35.5, 35.5)
    elseif tag == "legacy_live_kill" then
        ra2.killEntity(state.legacyUnit)
    elseif tag == "legacy_live_successor" then
        state.successor = ra2.spawnUnit(0, "GI", 35.5, 35.5)
        state.legacyProtected = ra2.entityHp(state.legacyUnit) == 0 and ra2.entityHp(state.successor) > 0
    end
end
function OnTriggerCond(tag)
    if tag == "conversion_health" then return state.lastBuildingHp == 333 end
    if tag == "safe" then return state.distinct and state.lookups and state.deathIdentity and state.safe end
    if tag == "legacy_callback" then return state.legacyCallback == true end
    if tag == "legacy_protected" then return state.legacyProtected == true end
    return false
end
function OnSave() return { state = state, lastKilled = lastKilled } end
function OnLoad(saved) state, lastKilled = saved.state, saved.lastKilled end
