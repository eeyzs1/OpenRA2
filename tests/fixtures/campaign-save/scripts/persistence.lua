-- PERSISTENCE_FIXTURE_SCRIPT: mechanism coverage, not shipped campaign content.
local counter, startCalls, saveMode = 0, 0, "normal"
local savedLoad
local flags = { spawned = false, exactInteger = 9007199254740993 }
flags.self = flags
local binary = "left\0right"
local bootRandom = math.random()
-- The regression intentionally enters this branch to verify restore preflight.
if ra2.money(0) == 777777 then ra2.giveMoney(0, 5) end

function OnGameStart()
    startCalls = startCalls + 1
    math.randomseed(1234, 999)
    ra2.giveMoney(0, 1234)
end
function OnAiThink(player) return true end
function OnTick(tick)
    counter = counter + 1
    flags.lastRandom, flags.lastBits = math.random(), math.random(0)
    if counter % 37 == 0 then ra2.giveMoney(0, math.random(1, 7)) end
end
function OnTriggerCond(tag)
    return tag == "later" and counter >= 300 and flags.spawned
end
function OnTrigger(tag)
    if tag == "spawnOnce" and not flags.spawned then
        flags.spawned = true
        ra2.spawnUnit(1, "Conscript", 45.5, 45.5)
        ra2.giveMoney(0, 89)
    elseif tag == "missing_load" then
        savedLoad, OnLoad = OnLoad, nil
    elseif tag == "invalid_save" or tag == "write_save" or tag == "reject_load" or tag == "normal" then
        saveMode = tag
        if tag == "normal" and savedLoad then OnLoad, savedLoad = savedLoad, nil end
    end
end
function OnSave()
    if saveMode == "invalid_save" then return { invalid = function() end } end
    if saveMode == "write_save" then ra2.giveMoney(0, 99999) end
    return { counter = counter, startCalls = startCalls, flags = flags, alias = flags,
             binary = binary, bootRandom = bootRandom, contract = saveMode == "reject_load" and "restore_no" or "restore_ok", saveMode = saveMode }
end
function OnLoad(saved)
    if saved.contract == "write_cash" then ra2.giveMoney(0, 99999) end
    if saved.contract == "write_game" then ra2.win() end
    assert(saved.contract == "restore_ok", "invalid declared state")
    assert(saved.flags == saved.alias and saved.flags.self == saved.flags)
    assert(saved.flags.exactInteger == 9007199254740993 and saved.binary == "left\0right")
    assert(saved.startCalls == 1)
    counter, startCalls, flags, binary, saveMode = saved.counter, saved.startCalls, saved.flags, saved.binary, saved.saveMode
    bootRandom = saved.bootRandom
end
