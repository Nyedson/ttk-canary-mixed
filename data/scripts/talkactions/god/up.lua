local up = TalkAction("/up")

function up.onSay(player, words, param)
	if not player:getGroup():getAccess() or player:getAccountType() < 4 then
		return true
	end

	local position = player:getPosition()
	position.z = position.z - 1
	player:teleportTo(position)
	return false
end

up:separator(" ")
up:register()
