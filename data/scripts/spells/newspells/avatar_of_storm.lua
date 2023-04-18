local condition = Condition(CONDITION_OUTFIT)
condition:setOutfit({lookType = 1595}) -- Avatar of Storm lookType    
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

    local grade = creature:upgradeSpellsWORD("Avatar of Storm")
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
    local duration = 15000
    condition:setTicks(duration)
    local conditionCooldown = Condition(CONDITION_SPELLCOOLDOWN, CONDITIONID_DEFAULT, 266)
    conditionCooldown:setTicks((cooldown * 1000 * 60))
    creature:getPosition():sendMagicEffect(CONST_ME_AVATAR_APPEAR)
    creature:addCondition(conditionCooldown)
    creature:addCondition(condition)
    creature:avatarTimer((os.time() * 1000) + duration)
    creature:reloadData()
    addEvent(reloadDataEvent, duration, creature:getId())
	return true
end

spell:group("support")
spell:id(266)
spell:name("Avatar of Storm")
spell:words("uteta res ven")
spell:level(1)
spell:mana(800)
spell:isPremium(false)
spell:cooldown(1000) -- Cooldown is calculated on the casting
spell:groupCooldown(2 * 1000)
spell:vocation("sorcerer;true", "master sorcerer;true")
spell:hasParams(true)
spell:isAggressive(false)
spell:needLearn(false)
spell:register()