local condition = Condition(CONDITION_OUTFIT)
condition:setOutfit({lookType = 1593}) -- Avatar of Steel lookType    
local spell = Spell("instant")

local reloadDataEvent = function(cid)
    local player = Player(cid)
    if not(player) then
        return
    end

    player:reloadData()
end

function spell.onCastSpell(creature, variant)
    if not(creature) or not(creature:isPlayer()) then
        return false
    end

    local grade = creature:upgradeSpellsWORD("Avatar of Steel")
    if (grade == 0) then
        creature:sendCancelMessage("You cannot cast this spell")
        creature:getPosition():sendMagicEffect(CONST_ME_POFF)
        return false
    end

    local cooldown = 0
    if (grade >= 3) then
        cooldown = 60
    elseif (grade >= 2) then
        cooldown = 90
    elseif (grade >= 1) then
        cooldown = 120
    end

    if creature:getStorageValue(32217) >= os.time() then
        creature:sendCancelMessage("You must wait before casting this spell again.")
        creature:getPosition():sendMagicEffect(CONST_ME_POFF)
        return false
    end

    local duration = 15000
    condition:setTicks(duration)
    local conditionCooldown = Condition(CONDITION_SPELLCOOLDOWN, CONDITIONID_DEFAULT, 264)
    conditionCooldown:setTicks((cooldown * 1000 * 60))
    creature:getPosition():sendMagicEffect(CONST_ME_AVATAR_APPEAR)
    creature:addCondition(conditionCooldown)
    creature:addCondition(condition)
    creature:avatarTimer((os.time() * 1000) + duration)
    creature:setStorageValue(32217, os.time() + (cooldown))

    creature:reloadData()
    addEvent(reloadDataEvent, duration, creature:getId())
	return true
end

spell:group("support")
spell:id(264)
spell:name("Avatar of Steel")
spell:words("uteta res eq")
spell:level(1)
spell:mana(800)
spell:cooldown(1000 * 60) -- Cooldown is calculated on the casting
spell:groupCooldown(2 * 1000)
spell:vocation("knight;true", "elite knight;true")
spell:hasParams(true)
spell:isAggressive(false)
spell:register()