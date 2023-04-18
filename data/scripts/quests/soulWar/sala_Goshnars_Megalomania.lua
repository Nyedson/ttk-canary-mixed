-- Goshnar's Malice

local config = {				
			
	requiredLevel = 400,
	daily = true,
	roomCenterPosition = Position(34093, 31030, 11),
	playerPositions = {
		Position(34059, 31030, 11),
		Position(34060, 31030, 11),
		Position(34061, 31030, 11),
		Position(34062, 31030, 11),
		Position(34063, 31030, 11),
	},
	teleportPosition = Position(34091, 31037, 11),
	bossPosition = Position(34094, 31023, 11)
}

local leverboss = Action()

function leverboss.onUse(player, item, fromPosition, target, toPosition, isHotkey)
	if item.itemid == 9825 then		
		-- Check if the player that pulled the lever is on the correct position
		if player:getPosition() ~= config.playerPositions[1] then
			player:sendTextMessage(MESSAGE_STATUS_SMALL, "You can\'t start the battle.")
			return true
		end
		
		local team, participant = {}

		for i = 1, #config.playerPositions do
			participant = Tile(config.playerPositions[i]):getTopCreature()
			
			-- Check there is a participant player
			if participant and participant:isPlayer() then
				-- Check participant level
				if participant:getLevel() < config.requiredLevel then
					player:sendTextMessage(MESSAGE_STATUS_SMALL,
						"All the players need to be level ".. config.requiredLevel .." or higher.")
					return true
				end

				-- Check participant boss timer
				if config.daily and participant:getStorageValue(Storage.Boos.GoshnarsMegalomania) > os.time() then
					player:getPosition():sendMagicEffect(CONST_ME_POFF)
					player:sendCancelMessage("Não deu o tempo para o boss.")
					return true
				end

				team[#team + 1] = participant
			end
		end

		-- Check if a team currently inside the boss room
		local specs, spec = Game.getSpectators(config.roomCenterPosition, false, false, 14, 14, 13, 13)
		for i = 1, #specs do
			spec = specs[i]
			if spec:isPlayer() then
				player:sendTextMessage(MESSAGE_STATUS_SMALL, "A team is already inside the boss room.")
				return true
			end			
		end
		
		-- Check if a team currently inside the boss room
		local specs, spec = Game.getSpectators(config.roomCenterPosition, false, false, 14, 14, 13, 13)
		for i = 1, #specs do
			spec = specs[i]			
			spec:remove()
		end

		-- Spawn boss
		Game.createMonster("Goshnar's Megalomania", config.bossPosition)
		Game.createMonster("Hateful Soul", Position(34132, 30996, 11))
		Game.createMonster("Malicious Soul", Position(34088, 30998, 11))
		Game.createMonster("Hazardous Phantom", Position(34055, 31062, 11))
				
				
		-- Teleport team participants
		for i = 1, #team do
			team[i]:getPosition():sendMagicEffect(CONST_ME_POFF)
			team[i]:teleportTo(config.teleportPosition)
			-- Assign boss timer
			team[i]:setStorageValue(Storage.Boos.GoshnarsMegalomania, os.time() + 20*60*60) -- 20 hours
		end		

		config.teleportPosition:sendMagicEffect(CONST_ME_ENERGYAREA)		
	end

	item:transform(9825)
	return true
end
 
leverboss:uid(60112)
leverboss:register()