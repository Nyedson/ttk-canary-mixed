local ghost = TalkAction("/ghost")

function ghost.onSay(player, words, param)
	if not player:getGroup():getAccess() or player:getAccountType() < 4 then
		return true
	end

	local position = player:getPosition()
	local isGhost = not player:isInGhostMode()

	player:setGhostMode(isGhost)
	if isGhost then
		player:sendTextMessage(MESSAGE_HOTKEY_PRESSED, "You are now invisible.")
		position:sendMagicEffect(CONST_ME_YALAHARIGHOST)
	else
		player:sendTextMessage(MESSAGE_HOTKEY_PRESSED, "You are visible again.")
		position.x = position.x + 1
		position:sendMagicEffect(CONST_ME_SMOKE)
	end
	return false
end

ghost:separator(" ")
ghost:register()
