local mType = Game.createMonsterType("The Nightmare Beast")
local monster = {}

monster.description = "a The Nightmare Beast"
monster.experience = 255000
monster.outfit = {
	lookType = 1144,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0
}

monster.health = 300000
monster.maxHealth = 300000
monster.race = "blood"
monster.corpse = 34797
monster.speed = 300
monster.manaCost = 0
monster.maxSummons = 0

monster.changeTarget = {
	interval = 2000,
	chance = 50
}

monster.strategiesTarget = {
	nearest = 70,
	health = 10,
	damage = 10,
	random = 10,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = true,
	illusionable = false,
	canPushItems = true,
	canPushCreatures = true,
	staticAttackChance = 90,
	targetDistance = 1,
	runHealth = 0,
	healthHidden = false,
	isBlockable = false,
	canWalkOnEnergy = false,
	canWalkOnFire = false,
	canWalkOnPoison = false,
	pet = false
}

monster.light = {
	level = 0,
	color = 0
}

monster.voices = {
	interval = 5000,
	chance = 30,
}

monster.loot = {
	{name = "platinum coin", chance = 100000, maxCount = 5},
	{name = "piggy bank", chance = 100000},
	{name = "mysterious remains", chance = 100000},
	{name = "energy bar", chance = 100000},
	{name = "silver token", chance = 97120, maxCount = 4},
	{name = "gold token", chance = 76980, maxCount = 3},
	{name = "ultimate spirit potion", chance = 63310, maxCount = 14},
	{name = "supreme health potion", chance = 53240, maxCount = 6},
	{name = "ultimate spirit potion", chance = 47480, maxCount = 20},
	{name = "huge chunk of crude iron", chance = 40290},
	{name = "red gem", chance = 32370},
	{name = "yellow gem", chance = 28780},
	{name = "royal star", chance = 25900, maxCount = 100},
	{name = "berserk potion", chance = 24460, maxCount = 10},
	{name = "blue gem", chance = 18710},
	{name = "mastermind potion", chance = 17990, maxCount = 10},
	{name = "green gem", chance = 17270},	
	{name = "skull staff", chance = 16550},
	{name = "bullseye potion", chance = 13670, maxCount = 10},		
	{name = "gold ingot", chance = 1950},	
	{name = "ring of the sky", chance = 1630},
	{id = 26199, chance = 1910},
	{name = "beast's nightmare-cushion", chance = 1470},
	{name = "violet gem", chance = 6470},
	{name = "magic sulphur", chance = 6470},
	{name = "purple tendril lantern", chance = 160},
	{id = 26185, chance = 1040},
	{id = 26189, chance = 1040},
	{name = "soul stone", chance = 100},
	{name = "dragon figurine", chance =1040},
	{name = "giant sapphire", chance = 1320},
	{name = "giant emerald", chance = 1320},
	{name = "turquoise tendril lantern", chance = 180},
	{name = "dark whispers", chance = 200},
	{name = "arcane staff", chance = 880},
	{name = "giant ruby", chance = 880},
	{name = "abyss hammer", chance = 160},	
	{name = "unicorn figurine", chance = 400}
}
-- ALTEREI O VALOR DE CHANCE. AUMENTEI DE 20 PARA 30. ADICIONEI MANA DRAIN, AUMENTEI O VALOR DE CHANCE DE 15 PARA 50
monster.attacks = {
	{name ="melee", interval = 2000, chance = 100, minDamage = -1000, maxDamage = -2700},
	{name ="combat", interval = 2000, chance = 30, type = COMBAT_DEATHDAMAGE, minDamage = -800, maxDamage = -2200, range = 15, shootEffect = CONST_ANI_DEATH, effect = CONST_ME_MORTAREA, target = true},	
	{name ="combat", interval = 2000, chance = 30, type = COMBAT_DEATHDAMAGE, minDamage = -800, maxDamage = -2400, range = 15, shootEffect = CONST_ANI_DEATH, effect = CONST_ME_MORTAREA, target = true},	
	{name ="combat", interval = 2000, chance = 30, type = COMBAT_DEATHDAMAGE, minDamage = -800, maxDamage = -1500, radius = 40, effect = CONST_ME_MORTAREA, target = false},
	{name ="combat", interval = 2000, chance = 50, type = COMBAT_MANADRAIN, minDamage = -40, maxDamage = -90, radius = 4, shootEffect = CONST_ANI_ONYXARROW, effect = CONST_ME_MAGIC_RED, target = true},	
}

monster.defenses = {
	defense = 20,
	armor = 20
}

monster.elements = {
	{type = COMBAT_PHYSICALDAMAGE, percent = 10},
	{type = COMBAT_ENERGYDAMAGE, percent = 0},
	{type = COMBAT_EARTHDAMAGE, percent = 0},
	{type = COMBAT_FIREDAMAGE, percent = 20},
	{type = COMBAT_LIFEDRAIN, percent = 0},
	{type = COMBAT_MANADRAIN, percent = 0},
	{type = COMBAT_DROWNDAMAGE, percent = 0},
	{type = COMBAT_ICEDAMAGE, percent = 0},
	{type = COMBAT_HOLYDAMAGE , percent = 10},
	{type = COMBAT_DEATHDAMAGE , percent = 0}
}

monster.immunities = {
	{type = "paralyze", condition = true},
	{type = "outfit", condition = false},
	{type = "invisible", condition = true},
	{type = "bleed", condition = false}
}

mType.onThink = function(monster, interval)
end

mType.onAppear = function(monster, creature)
	if monster:getType():isRewardBoss() then
		monster:setReward(true)
	end
end

mType.onDisappear = function(monster, creature)
end

mType.onMove = function(monster, creature, fromPosition, toPosition)
end

mType.onSay = function(monster, creature, type, message)
end

mType:register(monster)
