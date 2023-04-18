local mType = Game.createMonsterType("Carnivostrich")
local monster = {}

monster.description = "a Carnivostrich"
monster.experience = 8000
monster.outfit = {
	lookType = 1605,
	lookHead = 0,
	lookBody = 0,
	lookLegs = 0,
	lookFeet = 0,
	lookAddons = 0,
	lookMount = 0
}

monster.raceId = 2341
monster.Bestiary = {
	class = "Bird",
	race = BESTY_RACE_BIRD,
	toKill = 2500,
	FirstUnlock = 50,
	SecondUnlock = 500,
	CharmsPoints = 50,
	Stars = 1,
	Occurrence = 0,
	Locations = "Sabrehaven, Rookgaard, The McRonalds Farm in Thais, Northport, \z
		Fibula, Carlin (killable but unreachable), Greenshore, Krimhorn, Orc Fortress, \z
		Factory Quarter, also theres one in the farm near Edron."
	}

monster.health = 9200
monster.maxHealth = 9200
monster.race = "blood"
monster.corpse = 42226
monster.speed = 186
monster.manaCost = 0

monster.changeTarget = {
	interval = 4000,
	chance = 10
}

monster.strategiesTarget = {
	nearest = 100,
}

monster.flags = {
	summonable = false,
	attackable = true,
	hostile = true,
	convinceable = false,
	pushable = false,
	rewardBoss = false,
	illusionable = true,
	canPushItems = true,
	canPushCreatures = true,
	staticAttackChance = 90,
	targetDistance = 2,
	runHealth = 0,
	healthHidden = false,
	isBlockable = false,
	canWalkOnEnergy = false,
	canWalkOnFire = false,
	canWalkOnPoison = true
}

monster.light = {
	level = 0,
	color = 0
}

monster.voices = {
	interval = 5000,
	chance = 10,
	{text = "Shriiiek", yell = true}
}

monster.loot = {
	{name = "Platinum Coin", chance = 100000, maxCount = 10},
	{name = "Strong Mana Potion", chance = 12540, maxCount = 4},
	{name = "Small Ruby", chance = 5000},
	{name = "Small Emerald", chance = 5810},
--	{name = "Carnivostrich Feather", chance = 3400},
	{name = "Boots of Haste", chance = 30500},
	{name = "Spellbook of Mind Control", chance = 24410},
}

monster.attacks = {
	{name ="melee", interval = 2000, chance = 100, minDamage = 0, maxDamage = -498},
	{name ="combat", interval = 2000, chance = 40, type = COMBAT_PHYSICALDAMAGE, minDamage = 386, maxDamage = -480, range = 7, shootEffect = CONST_ANI_LARGEROCK, target = true},
	{name ="combat", interval = 2000, chance = 30, type = COMBAT_DEATHDAMAGE, minDamage = -350, maxDamage = -495, length = 7, spread = 0, effect = CONST_ME_BLACKSMOKE, target = false},
	{name ="combat", interval = 2000, chance = 20, type = COMBAT_DEATHDAMAGE, minDamage = -311, maxDamage = -400, length = 8, spread = 3, effect = CONST_ME_BLACKSMOKE, target = false},
}

monster.defenses = {
	defense = 25,
	armor = 25,
	{name ="combat", interval = 2000, chance = 15, type = COMBAT_HEALING, minDamage = 45, maxDamage = 65, effect = CONST_ME_MAGIC_BLUE, target = false},
	{name ="speed", interval = 2000, chance = 15, speedChange = 300, effect = CONST_ME_MAGIC_GREEN, target = false, duration = 5000}
}

monster.elements = {
	{type = COMBAT_PHYSICALDAMAGE, percent = 0},
	{type = COMBAT_ENERGYDAMAGE, percent = 20},
	{type = COMBAT_EARTHDAMAGE, percent = 100},
	{type = COMBAT_FIREDAMAGE, percent = 0},
	{type = COMBAT_LIFEDRAIN, percent = 0},
	{type = COMBAT_MANADRAIN, percent = 0},
	{type = COMBAT_DROWNDAMAGE, percent = 0},
	{type = COMBAT_ICEDAMAGE, percent = 10},
	{type = COMBAT_HOLYDAMAGE , percent = 0},
	{type = COMBAT_DEATHDAMAGE , percent = 0}
}

monster.immunities = {
	{type = "paralyze", condition = false},
	{type = "outfit", condition = false},
	{type = "invisible", condition = true},
	{type = "bleed", condition = false}
}

mType:register(monster)
