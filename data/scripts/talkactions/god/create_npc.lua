local createNpc = TalkAction("/n")

function createNpc.onSay(player, words, param)
	if not player:getGroup():getAccess() or player:getAccountType() < 4 then
		return true
	end

	if param == "" then
		player:sendCancelMessage("Command param required.")
		return false
	end

	local position = player:getPosition()
	local npc = Game.createNpc(param, position)
	if npc then
		npc:setMasterPos(position)
		position:sendMagicEffect(CONST_ME_MAGIC_RED)
	else
		player:sendCancelMessage("There is not enough room.")
		position:sendMagicEffect(CONST_ME_POFF)
	end
	return false
end

createNpc:separator(" ")
createNpc:register()
