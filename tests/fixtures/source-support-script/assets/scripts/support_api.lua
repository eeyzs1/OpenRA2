local used = false

function OnTrigger(tag)
    if tag ~= "support_api" then return end
    assert(ra2.supportReady(0, 2))
    assert(ra2.supportReady(0, 3))
    assert(ra2.orderSupport(0, 2, 50.5, 10.5))
    used = not ra2.supportReady(0, 2) and ra2.supportReady(0, 3)
end

function OnTriggerCond(tag)
    return tag == "support_api" and used
end

function OnSave() return { used = used } end
function OnLoad(state) used = state.used end
