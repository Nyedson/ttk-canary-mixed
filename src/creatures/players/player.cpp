/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2019 Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include <bitset>

#include "creatures/players/player.h"
#include "items/bed.h"
#include "creatures/interactions/chat.h"
#include "creatures/combat/combat.h"
#include "config/configmanager.h"
#include "lua/creature/creatureevent.h"
#include "lua/creature/events.h"
#include "game/game.h"
#include "io/iologindata.h"
#include "creatures/monsters/monster.h"
#include "creatures/monsters/monsters.h"
#include "lua/creature/movement.h"
#include "game/scheduling/scheduler.h"
#include "items/weapons/weapons.h"
#include "io/iobestiary.h"
#include "creatures/combat/spells.h"
#include "game/exaltedforge.h"

extern ConfigManager g_config;
extern Game g_game;
extern Chat* g_chat;
extern Vocations g_vocations;
extern MoveEvents* g_moveEvents;
extern Weapons* g_weapons;
extern CreatureEvents* g_creatureEvents;
extern Events* g_events;
extern Imbuements* g_imbuements;
extern Monsters g_monsters;
extern Spells* g_spells;
extern IOPrey g_prey;
extern Forge g_forge;

MuteCountMap Player::muteCountMap;

uint32_t Player::playerAutoID = 0x10010000;

Player::Player(ProtocolGame_ptr p) :
                                    Creature(),
                                    lastPing(OTSYS_TIME()),
                                    lastPong(lastPing),
                                    inbox(new Inbox(ITEM_INBOX)),
                                    client(std::move(p)) {
  inbox->incrementReferenceCounter();
}

Player::~Player()
{
	for (Item* item : inventory) {
		if (item) {
			item->setParent(nullptr);
			item->stopDecaying();
			item->decrementReferenceCounter();
		}
	}

	for (const auto& it : depotLockerMap) {
		it.second->removeInbox(inbox);
		it.second->stopDecaying();
		it.second->decrementReferenceCounter();
	}

	for (const auto& it : rewardMap) {
		it.second->decrementReferenceCounter();
	}

	for (const auto& it : quickLootContainers) {
		it.second->decrementReferenceCounter();
	}

	for (PreySlot* slot : preys) {
		if (slot) {
			delete slot;
		}
	}

	for (TaskHuntingSlot* slot : taskHunting) {
		if (slot) {
			delete slot;
		}
	}

	inbox->stopDecaying();
	inbox->decrementReferenceCounter();

	setWriteItem(nullptr);
	setEditHouse(nullptr);
	logged = false;
}

bool Player::setVocation(uint16_t vocId)
{
	Vocation* voc = g_vocations.getVocation(vocId);
	if (!voc) {
		return false;
	}
	vocation = voc;

	Condition* condition = getCondition(CONDITION_REGENERATION, CONDITIONID_DEFAULT);
	if (condition) {
		condition->setParam(CONDITION_PARAM_HEALTHGAIN, vocation->getHealthGainAmount());
		condition->setParam(CONDITION_PARAM_HEALTHTICKS, vocation->getHealthGainTicks() * 1000);
		condition->setParam(CONDITION_PARAM_MANAGAIN, vocation->getManaGainAmount());
		condition->setParam(CONDITION_PARAM_MANATICKS, vocation->getManaGainTicks() * 1000);
	}
	g_game.addPlayerVocation(this);
	return true;
}

bool Player::isPushable() const
{
	if (hasFlag(PlayerFlag_CannotBePushed)) {
		return false;
	}
	return Creature::isPushable();
}

std::string Player::getDescription(int32_t lookDistance) const
{
	std::ostringstream s;

	if (lookDistance == -1) {
		s << "yourself.";

		if (group->access) {
			s << " You are " << group->name << '.';
		} else if (vocation->getId() != VOCATION_NONE) {
			s << " You are " << vocation->getVocDescription() << '.';
		} else {
			s << " You have no vocation.";
		}
	} else {
		s << name;
		if (!group->access) {
			s << " (Level " << level << ')';
		}
		s << '.';

		if (sex == PLAYERSEX_FEMALE) {
			s << " She";
		} else {
			s << " He";
		}

		if (group->access) {
			s << " is " << group->name << '.';
		} else if (vocation->getId() != VOCATION_NONE) {
			s << " is " << vocation->getVocDescription() << '.';
		} else {
			s << " has no vocation.";
		}
	}

	if (party) {
		if (lookDistance == -1) {
			s << " Your party has ";
		} else if (sex == PLAYERSEX_FEMALE) {
			s << " She is in a party with ";
		} else {
			s << " He is in a party with ";
		}

		size_t memberCount = party->getMemberCount() + 1;
		if (memberCount == 1) {
			s << "1 member and ";
		} else {
			s << memberCount << " members and ";
		}

		size_t invitationCount = party->getInvitationCount();
		if (invitationCount == 1) {
			s << "1 pending invitation.";
		} else {
			s << invitationCount << " pending invitations.";
		}
	}

	if (guild && guildRank) {
		size_t memberCount = guild->getMemberCount();
		if (memberCount >= 1000) {
			s << "";
			return s.str();
		}

		if (lookDistance == -1) {
			s << " You are ";
		} else if (sex == PLAYERSEX_FEMALE) {
			s << " She is ";
		} else {
			s << " He is ";
		}

		s << guildRank->name << " of the " << guild->getName();
		if (!guildNick.empty()) {
			s << " (" << guildNick << ')';
		}

		if (memberCount == 1) {
			s << ", which has 1 member, " << guild->getMembersOnline().size() << " of them online.";
		} else {
			s << ", which has " << memberCount << " members, " << guild->getMembersOnline().size() << " of them online.";
		}
	}
	return s.str();
}

Item* Player::getInventoryItem(slots_t slot) const
{
	if (slot < CONST_SLOT_FIRST || slot > CONST_SLOT_LAST) {
		return nullptr;
	}
	return inventory[slot];
}

void Player::addConditionSuppressions(uint32_t addConditions)
{
	conditionSuppressions |= addConditions;
}

void Player::removeConditionSuppressions(uint32_t removeConditions)
{
	conditionSuppressions &= ~removeConditions;
}

Item* Player::getWeapon(slots_t slot, bool ignoreAmmo) const
{
	Item* item = inventory[slot];
	if (!item) {
		return nullptr;
	}

	WeaponType_t weaponType = item->getWeaponType();
	if (weaponType == WEAPON_NONE || weaponType == WEAPON_SHIELD || weaponType == WEAPON_AMMO) {
		return nullptr;
	}

  if (!ignoreAmmo && weaponType == WEAPON_DISTANCE) {
    const ItemType& it = Item::items[item->getID()];
    if (it.ammoType != AMMO_NONE) {
      Item* quiver = inventory[CONST_SLOT_RIGHT];
      if (!quiver || quiver->getWeaponType() != WEAPON_QUIVER)
        return nullptr;
      Container* container = quiver->getContainer();
      if (!container)
        return nullptr;
      bool found = false;
      for (Item* ammoItem : container->getItemList()) {
        if (ammoItem->getAmmoType() == it.ammoType) {
          item = ammoItem;
          found = true;
          break;
        }
      }
      if (!found)
        return nullptr;
    }
  }
	return item;
}

Item* Player::getWeapon(bool ignoreAmmo/* = false*/) const
{
	Item* item = getWeapon(CONST_SLOT_LEFT, ignoreAmmo);
	if (item) {
		return item;
	}

	item = getWeapon(CONST_SLOT_RIGHT, ignoreAmmo);
	if (item) {
		return item;
	}
	return nullptr;
}

WeaponType_t Player::getWeaponType() const
{
	Item* item = getWeapon();
	if (!item) {
		return WEAPON_NONE;
	}
	return item->getWeaponType();
}

int32_t Player::getWeaponSkill(const Item* item) const
{
	if (!item) {
		return getSkillLevel(SKILL_FIST);
	}

	int32_t attackSkill;

	WeaponType_t weaponType = item->getWeaponType();
	switch (weaponType) {
		case WEAPON_SWORD: {
			attackSkill = getSkillLevel(SKILL_SWORD);
			break;
		}

		case WEAPON_CLUB: {
			attackSkill = getSkillLevel(SKILL_CLUB);
			break;
		}

		case WEAPON_AXE: {
			attackSkill = getSkillLevel(SKILL_AXE);
			break;
		}

		case WEAPON_DISTANCE: {
			attackSkill = getSkillLevel(SKILL_DISTANCE);
			break;
		}

		default: {
			attackSkill = 0;
			break;
		}
	}
	return attackSkill;
}

int32_t Player::getArmor() const
{
	int32_t armor = 0;

	static const slots_t armorSlots[] = {CONST_SLOT_HEAD, CONST_SLOT_NECKLACE, CONST_SLOT_ARMOR, CONST_SLOT_LEGS, CONST_SLOT_FEET, CONST_SLOT_RING};
	for (slots_t slot : armorSlots) {
		Item* inventoryItem = inventory[slot];
		if (inventoryItem) {
			armor += inventoryItem->getArmor();
		}
	}
	return static_cast<int32_t>(armor * vocation->armorMultiplier);
}

void Player::getShieldAndWeapon(const Item*& shield, const Item*& weapon) const
{
	shield = nullptr;
	weapon = nullptr;

	for (uint32_t slot = CONST_SLOT_RIGHT; slot <= CONST_SLOT_LEFT; slot++) {
		Item* item = inventory[slot];
		if (!item) {
			continue;
		}

		switch (item->getWeaponType()) {
			case WEAPON_NONE:
				break;

			case WEAPON_SHIELD: {
				if (!shield || (shield && item->getDefense() > shield->getDefense())) {
					shield = item;
				}
				break;
			}

			default: { // weapons that are not shields
				weapon = item;
				break;
			}
		}
	}
}

float Player::getMitigation() const
{
	int32_t skill = getSkillLevel(SKILL_SHIELD);
	int32_t defenseValue = 0;
	const Item* weapon = inventory[CONST_SLOT_LEFT];
	const Item* shield = inventory[CONST_SLOT_RIGHT];

	float fightFactor = 1.0f;
	float shieldFactor = 1.0f;
	float distanceFactor = 1.0f;
	switch (fightMode) {
		case FIGHTMODE_ATTACK: {
			fightFactor = 0.67f;
			break;
		}
		case FIGHTMODE_BALANCED: {
			fightFactor = 0.84f;
			break;
		}
		case FIGHTMODE_DEFENSE: {
			fightFactor = 1.0f;
			break;
		}
		default:
			break;
	}

	if (shield) {
		if (shield->isSpellBook() || shield->getWeaponType() == WEAPON_QUIVER) {
			distanceFactor = vocation->mitigationSecondaryShield;
		} else {
			shieldFactor = vocation->mitigationPrimaryShield;
		}
		defenseValue = shield->getDefense();
		// Wheel of destiny
		if (shield->getDefense() > 0) {
			defenseValue += getWheelOfDestinyMajorStatConditional("Combat Mastery", WHEEL_OF_DESTINY_MAJOR_DEFENSE);
		}
	}

	if (weapon) {
		if (weapon->getAmmoType() == AMMO_BOLT || weapon->getAmmoType() == AMMO_ARROW) {
			distanceFactor = vocation->mitigationSecondaryShield;
		} else if (weapon->getSlotPosition() & SLOTP_TWO_HAND) {
			defenseValue = weapon->getDefense() + weapon->getExtraDefense();
			shieldFactor = vocation->mitigationSecondaryShield;
		} else {
			defenseValue += weapon->getExtraDefense();
			shieldFactor = vocation->mitigationPrimaryShield;
		}
	}

	float mitigation = std::ceil((((((skill * vocation->mitigationFactor) + (shieldFactor * defenseValue))/100.0)) * fightFactor * distanceFactor) * 100.0)/100.0;
	mitigation += (mitigation * getMitigationMultiplier()) / 100.;
	return mitigation;
}

int32_t Player::getDefense() const
{
	int32_t defenseSkill = getSkillLevel(SKILL_FIST);
	int32_t defenseValue = 7;
	const Item* weapon;
	const Item* shield;
	try {
		getShieldAndWeapon(shield, weapon);
	}
	catch (const std::exception &e) {
		SPDLOG_ERROR("{} got exception {}", getName(), e.what());
	}

	if (weapon) {
		defenseValue = weapon->getDefense() + weapon->getExtraDefense();
		defenseSkill = getWeaponSkill(weapon);
	}

	if (shield) {
		defenseValue = weapon != nullptr ? shield->getDefense() + weapon->getExtraDefense() : shield->getDefense();
		// Wheel of destiny
		if (defenseValue > 0) {
			defenseValue += getWheelOfDestinyMajorStatConditional("Combat Mastery", WHEEL_OF_DESTINY_MAJOR_DEFENSE);
		}
		defenseSkill = getSkillLevel(SKILL_SHIELD);
	}

	if (defenseSkill == 0) {
		switch (fightMode) {
			case FIGHTMODE_ATTACK:
			case FIGHTMODE_BALANCED:
				return 1;

			case FIGHTMODE_DEFENSE:
				return 2;
		}
	}

	return (defenseSkill / 4. + 2.23) * defenseValue * 0.15 * getDefenseFactor() * vocation->defenseMultiplier;
}

float Player::getAttackFactor() const
{
	switch (fightMode) {
		case FIGHTMODE_ATTACK: return 1.0f;
		case FIGHTMODE_BALANCED: return 0.75f;
		case FIGHTMODE_DEFENSE: return 0.5f;
		default: return 1.0f;
	}
}

float Player::getDefenseFactor() const
{
	switch (fightMode) {
		case FIGHTMODE_ATTACK: return (OTSYS_TIME() - lastAttack) < getAttackSpeed() ? 0.5f : 1.0f;
		case FIGHTMODE_BALANCED: return (OTSYS_TIME() - lastAttack) < getAttackSpeed() ? 0.75f : 1.0f;
		case FIGHTMODE_DEFENSE: return 1.0f;
		default: return 1.0f;
	}
}

uint32_t Player::getClientIcons() const
{
	uint32_t icons = 0;
	for (Condition* condition : conditions) {
		if (!isSuppress(condition->getType())) {
			icons |= condition->getIcons();
		}
	}

	if (pzLocked) {
		icons |= ICON_REDSWORDS;
	}

	if (tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
		icons |= ICON_PIGEON;
		client->sendRestingStatus(1);

		// Don't show ICON_SWORDS if player is in protection zone.
		if (hasBitSet(ICON_SWORDS, icons)) {
			icons &= ~ICON_SWORDS;
		}
	} else {
		client->sendRestingStatus(0);
	}

	// Game client debugs with 10 or more icons
	// so let's prevent that from happening.
	std::bitset<32> icon_bitset(static_cast<uint64_t>(icons));
	for (size_t pos = 0, bits_set = icon_bitset.count(); bits_set >= 10; ++pos) {
		if (icon_bitset[pos]) {
			icon_bitset.reset(pos);
			--bits_set;
		}
	}
	return icon_bitset.to_ulong();
}

void Player::updateInventoryWeight()
{
	if (hasFlag(PlayerFlag_HasInfiniteCapacity)) {
		return;
	}

	inventoryWeight = 0;
	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		const Item* item = inventory[i];
		if (item) {
			inventoryWeight += item->getWeight();
		}
	}
}

void Player::setTraining(bool value) {
	for (const auto& it : g_game.getPlayers()) {
		if (!this->isInGhostMode() || it.second->isAccessPlayer()) {
			it.second->notifyStatusChange(this, value ? VIPSTATUS_TRAINING : VIPSTATUS_ONLINE, false);
		}
	}
	this->statusVipList = VIPSTATUS_TRAINING;
	setExerciseTraining(value);
}

void Player::addSkillAdvance(skills_t skill, uint64_t count)
{
	uint64_t currReqTries = vocation->getReqSkillTries(skill, skills[skill].level);
	uint64_t nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);
	if (currReqTries >= nextReqTries) {
		//player has reached max skill
		return;
	}

	g_events->eventPlayerOnGainSkillTries(this, skill, count);
	if (count == 0) {
		return;
	}

	bool sendUpdateSkills = false;
	while ((skills[skill].tries + count) >= nextReqTries) {
		count -= nextReqTries - skills[skill].tries;
		skills[skill].level++;
		skills[skill].tries = 0;
		skills[skill].percent = 0;

		std::ostringstream ss;
		ss << "You advanced to " << getSkillName(skill) << " level " << skills[skill].level << '.';
		sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());

		g_creatureEvents->playerAdvance(this, skill, (skills[skill].level - 1), skills[skill].level);

		sendUpdateSkills = true;
		currReqTries = nextReqTries;
		nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);
		if (currReqTries >= nextReqTries) {
			count = 0;
			break;
		}
	}

	skills[skill].tries += count;

	uint32_t newPercent;
	if (nextReqTries > currReqTries) {
		newPercent = Player::getPercentLevel(skills[skill].tries, nextReqTries);
	} else {
		newPercent = 0;
	}

	if (skills[skill].percent != newPercent) {
		skills[skill].percent = newPercent;
		sendUpdateSkills = true;
	}

	if (sendUpdateSkills) {
		sendSkills();
		sendStats();
	}
}

void Player::setVarStats(stats_t stat, int32_t modifier)
{
	varStats[stat] += modifier;

	switch (stat) {
		case STAT_MAXHITPOINTS: {
			if (getHealth() > getMaxHealth()) {
				Creature::changeHealth(getMaxHealth() - getHealth());
			} else {
				g_game.addCreatureHealth(this);
			}
			break;
		}

		case STAT_MAXMANAPOINTS: {
			if (getMana() > getMaxMana()) {
				Creature::changeMana(getMaxMana() - getMana());
			}
			else {
				g_game.addPlayerMana(this);
			}
			break;
		}

		default: {
			break;
		}
	}
}

int64_t Player::getDefaultStats(stats_t stat) const
{
	switch (stat) {
		case STAT_MAXHITPOINTS: return healthMax;
		case STAT_MAXMANAPOINTS: return manaMax;
		case STAT_MAGICPOINTS: return getBaseMagicLevel();
		default: return 0;
	}
}

void Player::addContainer(uint8_t cid, Container* container)
{
	if (cid > 0xF) {
		return;
	}

	if (!container) {
		return;
	}

	if (container->getID() == ITEM_BROWSEFIELD) {
		container->incrementReferenceCounter();
	}

	auto it = openContainers.find(cid);
	if (it != openContainers.end()) {
		OpenContainer& openContainer = it->second;
		Container* oldContainer = openContainer.container;
		if (oldContainer->getID() == ITEM_BROWSEFIELD) {
			oldContainer->decrementReferenceCounter();
		}

		openContainer.container = container;
		openContainer.index = 0;
	} else {
		OpenContainer openContainer;
		openContainer.container = container;
		openContainer.index = 0;
		openContainers[cid] = openContainer;
	}
}

void Player::closeContainer(uint8_t cid)
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return;
	}

	OpenContainer openContainer = it->second;
	Container* container = openContainer.container;
	openContainers.erase(it);

	if (container && container->getID() == ITEM_BROWSEFIELD) {
		container->decrementReferenceCounter();
	}
}

void Player::setContainerIndex(uint8_t cid, uint16_t index)
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return;
	}
	it->second.index = index;
}

Container* Player::getContainerByID(uint8_t cid)
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return nullptr;
	}
	return it->second.container;
}

int8_t Player::getContainerID(const Container* container) const
{
	for (const auto& it : openContainers) {
		if (it.second.container == container) {
			return it.first;
		}
	}
	return -1;
}

uint16_t Player::getContainerIndex(uint8_t cid) const
{
	auto it = openContainers.find(cid);
	if (it == openContainers.end()) {
		return 0;
	}
	return it->second.index;
}

bool Player::canOpenCorpse(uint32_t ownerId) const
{
	return getID() == ownerId || (party && party->canOpenCorpse(ownerId));
}

uint16_t Player::getLookCorpse() const
{
	if (sex == PLAYERSEX_FEMALE) {
		return ITEM_FEMALE_CORPSE;
	} else {
		return ITEM_MALE_CORPSE;
	}
}

void Player::addStorageValue(const uint32_t key, const int32_t value, const bool isLogin/* = false*/)
{
	if (IS_IN_KEYRANGE(key, RESERVED_RANGE)) {
		if (IS_IN_KEYRANGE(key, OUTFITS_RANGE)) {
			outfits.emplace_back(
				value >> 16,
				value & 0xFF
			);
			return;
		} else if (IS_IN_KEYRANGE(key, MOUNTS_RANGE)) {
			// do nothing
		} else if (IS_IN_KEYRANGE(key, FAMILIARS_RANGE)) {
			familiars.emplace_back(
				value >> 16);
			return;
		} else {
			SPDLOG_WARN("Unknown reserved key: {} for player: {}", key, getName());
			return;
		}
	}

	if (value != -1) {
		int32_t oldValue;
		getStorageValue(key, oldValue);

		storageMap[key] = value;

		if (!isLogin) {
			auto currentFrameTime = g_dispatcher.getDispatcherCycle();
			g_events->eventOnStorageUpdate(this, key, value, oldValue, currentFrameTime);
		}
	} else {
		storageMap.erase(key);
	}
}

bool Player::getStorageValue(const uint32_t key, int32_t& value) const
{
	auto it = storageMap.find(key);
	if (it == storageMap.end()) {
		value = -1;
		return false;
	}

	value = it->second;
	return true;
}

bool Player::canSee(const Position& pos) const
{
	if (!client) {
		return false;
	}
	return client->canSee(pos);
}

bool Player::canSeeCreature(const Creature* creature) const
{
	if (creature == this) {
		return true;
	}

	if (creature->isInGhostMode() && !group->access) {
		return false;
	}

	if (!creature->getPlayer() && !canSeeInvisibility() && creature->isInvisible()) {
		return false;
	}
	return true;
}

bool Player::canWalkthrough(const Creature* creature) const
{
	if (group->access || creature->isInGhostMode()) {
		return true;
	}

	const Player* player = creature->getPlayer();
	const Monster* monster = creature->getMonster();
	const Npc* npc = creature->getNpc();
	if (monster) {
		if (!monster->isPet()) {
			return false;
		}
		return true;
	}

	if (player) {
		const Tile* playerTile = player->getTile();
		if (!playerTile) {
			return false;
		}

		if (g_game.getWorldType() == WORLD_TYPE_NO_PVP && isInWar(player)) {
			return false;
		}

		if (!playerTile->hasFlag(TILESTATE_NOPVPZONE) && !playerTile->hasFlag(TILESTATE_PROTECTIONZONE) && player->getLevel() > static_cast<uint32_t>(g_config.getNumber(ConfigManager::PROTECTION_LEVEL)) && g_game.getWorldType() != WORLD_TYPE_NO_PVP) {
			return false;
		}

		const Item* playerTileGround = playerTile->getGround();
		if (!playerTileGround || !playerTileGround->hasWalkStack()) {
			return false;
		}

		Player* thisPlayer = const_cast<Player*>(this);
		if ((OTSYS_TIME() - lastWalkthroughAttempt) > 2000) {
			thisPlayer->setLastWalkthroughAttempt(OTSYS_TIME());
			return false;
		}

		if (creature->getPosition() != lastWalkthroughPosition) {
			thisPlayer->setLastWalkthroughPosition(creature->getPosition());
			return false;
		}

		thisPlayer->setLastWalkthroughPosition(creature->getPosition());
		return true;
	} else if (npc) {
		const Tile* tile = npc->getTile();
		const HouseTile* houseTile = dynamic_cast<const HouseTile*>(tile);
		return (houseTile != nullptr);
	}

	return false;
}

bool Player::canWalkthroughEx(const Creature* creature) const
{
	if (group->access) {
		return true;
	}

	const Monster* monster = creature->getMonster();
	if (monster) {
		if (!monster->isPet()) {
			return false;
		}
		return true;
	}

	const Player* player = creature->getPlayer();
	const Npc* npc = creature->getNpc();
	if (player) {
		const Tile* playerTile = player->getTile();
		return playerTile && (playerTile->hasFlag(TILESTATE_NOPVPZONE) || playerTile->hasFlag(TILESTATE_PROTECTIONZONE) || player->getLevel() <= static_cast<uint32_t>(g_config.getNumber(ConfigManager::PROTECTION_LEVEL)) || g_game.getWorldType() == WORLD_TYPE_NO_PVP);
	} else if (npc) {
		const Tile* tile = npc->getTile();
		const HouseTile* houseTile = dynamic_cast<const HouseTile*>(tile);
		return (houseTile != nullptr);
	} else {
		return false;
	}

}

void Player::onReceiveMail() const
{
	if (isNearDepotBox()) {
		sendTextMessage(MESSAGE_EVENT_ADVANCE, "New mail has arrived.");
	}
}

Container* Player::setLootContainer(ObjectCategory_t category, Container* container, bool loading /* = false*/)
{
	Container* previousContainer = nullptr;
	auto it = quickLootContainers.find(category);
	if (it != quickLootContainers.end() && !loading) {
		previousContainer = (*it).second;
		uint32_t flags = previousContainer->getIntAttr(ITEM_ATTRIBUTE_QUICKLOOTCONTAINER);
		flags &= ~(1 << category);
		if (flags == 0) {
			previousContainer->removeAttribute(ITEM_ATTRIBUTE_QUICKLOOTCONTAINER);
		} else {
			previousContainer->setIntAttr(ITEM_ATTRIBUTE_QUICKLOOTCONTAINER, flags);
		}

		previousContainer->decrementReferenceCounter();
		quickLootContainers.erase(it);
	}

	if (container) {
		previousContainer = container;
		quickLootContainers[category] = container;

		container->incrementReferenceCounter();
		if (!loading) {
			uint32_t flags = container->getIntAttr(ITEM_ATTRIBUTE_QUICKLOOTCONTAINER);
			container->setIntAttr(ITEM_ATTRIBUTE_QUICKLOOTCONTAINER, flags | static_cast<uint32_t>(1 << category));
		}
	}

	return previousContainer;
}

Container* Player::getLootContainer(ObjectCategory_t category) const
{
	if (category != OBJECTCATEGORY_DEFAULT && !isPremium()) {
		category = OBJECTCATEGORY_DEFAULT;
	}

	auto it = quickLootContainers.find(category);
	if (it != quickLootContainers.end()) {
		return (*it).second;
	}

	if (category != OBJECTCATEGORY_DEFAULT) {
		// firstly, fallback to default
		return getLootContainer(OBJECTCATEGORY_DEFAULT);
	}

	return nullptr;
}

void Player::checkLootContainers(const Item* item)
{
  if (!item) {
    return;
  }

	const Container* container = item->getContainer();
	if (!container) {
		return;
	}

	bool shouldSend = false;

	auto it = quickLootContainers.begin();
	while (it != quickLootContainers.end()) {
		Container* lootContainer = (*it).second;

		bool remove = false;
		if (item->getHoldingPlayer() != this && (item == lootContainer || container->isHoldingItem(lootContainer))) {
			remove = true;
		}

		if (remove) {
			shouldSend = true;
			it = quickLootContainers.erase(it);
			lootContainer->decrementReferenceCounter();
			lootContainer->removeAttribute(ITEM_ATTRIBUTE_QUICKLOOTCONTAINER);
		} else {
			++it;
		}
	}

	if (shouldSend) {
		sendLootContainers();
	}
}

bool Player::isNearDepotBox() const
{
	const Position& pos = getPosition();
	for (int32_t cx = -1; cx <= 1; ++cx) {
		for (int32_t cy = -1; cy <= 1; ++cy) {
			Tile* posTile = g_game.map.getTile(pos.x + cx, pos.y + cy, pos.z);
			if (!posTile) {
				continue;
			}

			if (posTile->hasFlag(TILESTATE_DEPOT)) {
				return true;
			}
		}
	}
	return false;
}

DepotChest* Player::getDepotBox()
{
	DepotChest* depotBoxs = new DepotChest(ITEM_DEPOT);
	depotBoxs->incrementReferenceCounter();
	depotBoxs->setMaxDepotItems(getMaxDepotItems());
	for (uint32_t index = 1; index <= 18; ++index) {
		depotBoxs->internalAddThing(getDepotChest(19 - index, true));
	}
	return depotBoxs;
}

DepotChest* Player::getDepotChest(uint32_t depotId, bool autoCreate)
{
	auto it = depotChests.find(depotId);
	if (it != depotChests.end()) {
		return it->second;
	}

	if (!autoCreate) {
		return nullptr;
	}

	DepotChest* depotChest;
	if (depotId > 0 && depotId < 18) {
		depotChest = new DepotChest(ITEM_DEPOT_NULL + depotId);
	}
	else {
		depotChest = new DepotChest(ITEM_DEPOT_XVIII);
	}

	depotChest->incrementReferenceCounter();
	depotChests[depotId] = depotChest;
	return depotChest;
}

DepotLocker* Player::getDepotLocker(uint32_t depotId)
{
	auto it = depotLockerMap.find(depotId);
	if (it != depotLockerMap.end()) {
		inbox->setParent(it->second);
		for (uint8_t i = g_config.getNumber(ConfigManager::DEPOT_BOXES); i > 0; i--) {
			if (DepotChest* depotBox = getDepotChest(i, false)) {
				depotBox->setParent(it->second->getItemByIndex(0)->getContainer());
 			}
		}
		return it->second;
	}

	DepotLocker* depotLocker = new DepotLocker(ITEM_LOCKER1);
	depotLocker->setDepotId(depotId);
	depotLocker->internalAddThing(Item::CreateItem(ITEM_MARKET));
	depotLocker->internalAddThing(inbox);
	depotLocker->internalAddThing(Item::CreateItem(ITEM_SUPPLY_STASH));
	Container* depotChest = Item::CreateItemAsContainer(ITEM_DEPOT, g_config.getNumber(ConfigManager::DEPOT_BOXES));
	for (uint8_t i = g_config.getNumber(ConfigManager::DEPOT_BOXES); i > 0; i--) {
		DepotChest* depotBox = getDepotChest(i, true);
		depotChest->internalAddThing(depotBox);
		depotBox->setParent(depotChest);
	}
	depotLocker->internalAddThing(depotChest);
	depotLockerMap[depotId] = depotLocker;
	return depotLocker;
}

RewardChest* Player::getRewardChest()
{
	if (rewardChest != nullptr) {
		return rewardChest;
	}

	rewardChest = new RewardChest(ITEM_REWARD_CHEST);
	return rewardChest;
}

Reward* Player::getReward(uint32_t rewardId, bool autoCreate)
{
	auto it = rewardMap.find(rewardId);
	if (it != rewardMap.end()) {
		return it->second;
	}

	if (!autoCreate) {
		return nullptr;
	}

	Reward* reward = new Reward();
	reward->incrementReferenceCounter();
	reward->setIntAttr(ITEM_ATTRIBUTE_DATE, rewardId);
	rewardMap[rewardId] = reward;

	g_game.internalAddItem(getRewardChest(), reward, INDEX_WHEREEVER, FLAG_NOLIMIT);

	return reward;
}

void Player::removeReward(uint32_t rewardId) {
	rewardMap.erase(rewardId);
}

void Player::getRewardList(std::vector<uint32_t>& rewards) {
	rewards.reserve(rewardMap.size());
	for (auto& it : rewardMap) {
		rewards.push_back(it.first);
	}
}

void Player::sendCancelMessage(ReturnValue message) const
{
	sendCancelMessage(getReturnMessage(message));
}

void Player::sendStats()
{
	if (client) {
		client->sendStats();
		lastStatsTrainingTime = getOfflineTrainingTime() / 60 / 1000;
	}
}

void Player::sendPing()
{
	int64_t timeNow = OTSYS_TIME();

	bool hasLostConnection = false;
	if ((timeNow - lastPing) >= 5000) {
		lastPing = timeNow;
		if (client) {
			client->sendPing();
		} else {
			hasLostConnection = true;
		}
	}

	int64_t noPongTime = timeNow - lastPong;
	if ((hasLostConnection || noPongTime >= 7000) && attackedCreature && attackedCreature->getPlayer()) {
		setAttackedCreature(nullptr);
	}

	if (noPongTime >= 60000 && canLogout()) {
		if (g_creatureEvents->playerLogout(this)) {
			if (client) {
				client->logout(true, true);
			} else {
				g_game.removeCreature(this, true);
			}
		}
	}
}

Item* Player::getWriteItem(uint32_t& retWindowTextId, uint16_t& retMaxWriteLen)
{
	retWindowTextId = this->windowTextId;
	retMaxWriteLen = this->maxWriteLen;
	return writeItem;
}

void Player::inImbuing(Item* item)
{
	if (imbuing) {
		imbuing->decrementReferenceCounter();
	}

	if (item) {
		imbuing = item;
		imbuing->incrementReferenceCounter();
	} else {
		imbuing = nullptr;
	}
}

void Player::setWriteItem(Item* item, uint16_t maxWriteLength /*= 0*/)
{
	windowTextId++;

	if (writeItem) {
		writeItem->decrementReferenceCounter();
	}

	if (item) {
		writeItem = item;
		this->maxWriteLen = maxWriteLength;
		writeItem->incrementReferenceCounter();
	} else {
		writeItem = nullptr;
		this->maxWriteLen = 0;
	}
}

House* Player::getEditHouse(uint32_t& retWindowTextId, uint32_t& retListId)
{
	retWindowTextId = this->windowTextId;
	retListId = this->editListId;
	return editHouse;
}

void Player::setEditHouse(House* house, uint32_t listId /*= 0*/)
{
	windowTextId++;
	editHouse = house;
	editListId = listId;
}

void Player::sendHouseWindow(House* house, uint32_t listId) const
{
	if (!client) {
		return;
	}

	std::string text;
	if (house->getAccessList(listId, text)) {
		client->sendHouseWindow(windowTextId, text);
	}
}

void Player::sendImbuementWindow(Item* item)
{
	if (!client || !item) {
		return;
	}

	if (item->getTopParent() != this) {
		this->sendTextMessage(MESSAGE_FAILURE,
			"You have to pick up the item to imbue it.");
		return;
	}

	const ItemType& it = Item::items[item->getID()];
	uint8_t slot = it.imbuingSlots;
	if (slot <= 0 ) {
		this->sendTextMessage(MESSAGE_FAILURE, "This item is not imbuable.");
		return;
	}

	client->sendImbuementWindow(item);
}

void Player::sendMarketEnter(uint32_t depotId)
{
	if (client && depotId && this->getLastDepotId() != -1) {
		client->sendMarketEnter(depotId);
	}
}

//container
void Player::sendAddContainerItem(const Container* container, const Item* item)
{
	if (!client) {
		return;
	}

	if (!container) {
		return;
	}

	for (const auto& it : openContainers) {
		const OpenContainer& openContainer = it.second;
		if (openContainer.container != container) {
			continue;
		}

		uint16_t slot = openContainer.index;
		if (container->getID() == ITEM_BROWSEFIELD) {
			uint16_t containerSize = container->size() - 1;
			uint16_t pageEnd = openContainer.index + container->capacity() - 1;
			if (containerSize > pageEnd) {
				slot = pageEnd;
				item = container->getItemByIndex(pageEnd);
			} else {
				slot = containerSize;
			}
		} else if (openContainer.index >= container->capacity()) {
			item = container->getItemByIndex(openContainer.index - 1);
		}
		client->sendAddContainerItem(it.first, slot, item);
	}
}

void Player::sendUpdateContainerItem(const Container* container, uint16_t slot, const Item* newItem)
{
	if (!client) {
		return;
	}

	for (const auto& it : openContainers) {
		const OpenContainer& openContainer = it.second;
		if (openContainer.container != container) {
			continue;
		}

		if (slot < openContainer.index) {
			continue;
		}

		uint16_t pageEnd = openContainer.index + container->capacity();
		if (slot >= pageEnd) {
			continue;
		}

		client->sendUpdateContainerItem(it.first, slot, newItem);
	}
}

void Player::sendRemoveContainerItem(const Container* container, uint16_t slot)
{
	if (!client) {
		return;
	}

	if (!container) {
		return;
	}

	for (auto& it : openContainers) {
		OpenContainer& openContainer = it.second;
		if (openContainer.container != container) {
			continue;
		}

		uint16_t& firstIndex = openContainer.index;
		if (firstIndex > 0 && firstIndex >= container->size() - 1) {
			firstIndex -= container->capacity();
			sendContainer(it.first, container, false, firstIndex);
		}

		client->sendRemoveContainerItem(it.first, std::max<uint16_t>(slot, firstIndex), container->getItemByIndex(container->capacity() + firstIndex));
	}
}

void Player::openPlayerContainers()
{
	std::vector<std::pair<uint8_t, Container*>> openContainersList;

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		Container* itemContainer = item->getContainer();
		if (itemContainer) {
			uint8_t cid = item->getIntAttr(ITEM_ATTRIBUTE_OPENCONTAINER);
			if (cid > 0) {
				openContainersList.emplace_back(std::make_pair(cid, itemContainer));
			}
			for (ContainerIterator it = itemContainer->iterator(); it.hasNext(); it.advance()) {
				Container* subContainer = (*it)->getContainer();
				if (subContainer) {
					uint8_t subcid = (*it)->getIntAttr(ITEM_ATTRIBUTE_OPENCONTAINER);
					if (subcid > 0) {
						openContainersList.emplace_back(std::make_pair(subcid, subContainer));
					}
				}
			}
		}
	}

	std::sort(openContainersList.begin(), openContainersList.end(), [](const std::pair<uint8_t, Container*>& left, const std::pair<uint8_t, Container*>& right) {
		return left.first < right.first;
	});

	for (auto& it : openContainersList) {
		addContainer(it.first - 1, it.second);
		onSendContainer(it.second);
	}
}

void Player::onUpdateTileItem(const Tile* updateTile, const Position& pos, const Item* oldItem,
							  const ItemType& oldType, const Item* newItem, const ItemType& newType)
{
	Creature::onUpdateTileItem(updateTile, pos, oldItem, oldType, newItem, newType);

	if (oldItem != newItem) {
		onRemoveTileItem(updateTile, pos, oldType, oldItem);
	}

	if (tradeState != TRADE_TRANSFER) {
		if (tradeItem && oldItem == tradeItem) {
			g_game.internalCloseTrade(this);
		}
	}
}

void Player::onRemoveTileItem(const Tile* fromTile, const Position& pos, const ItemType& iType,
							  const Item* item)
{
	Creature::onRemoveTileItem(fromTile, pos, iType, item);

	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(item);

		if (tradeItem) {
			const Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem)) {
				g_game.internalCloseTrade(this);
			}
		}
	}

  checkLootContainers(item);
}

void Player::onCreatureAppear(Creature* creature, bool isLogin)
{
	Creature::onCreatureAppear(creature, isLogin);

	if (isLogin && creature == this) {
		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			Item* item = inventory[slot];
			if (item) {
				item->startDecaying();
				g_moveEvents->onPlayerEquip(this, item, static_cast<slots_t>(slot), false);
			}
		}

		for (Condition* condition : storedConditionList) {
			addCondition(condition);
		}
		storedConditionList.clear();

		BedItem* bed = g_game.getBedBySleeper(guid);
		if (bed) {
			bed->wakeUp(this);
		}

		if (guild) {
			guild->addMember(this);
		}

		int32_t offlineTime;
		if (getLastLogout() != 0) {
			// Not counting more than 21 days to prevent overflow when multiplying with 1000 (for milliseconds).
			offlineTime = std::min<int32_t>(time(nullptr) - getLastLogout(), 86400 * 21);
		} else {
			offlineTime = 0;
		}

		for (Condition* condition : getMuteConditions()) {
			condition->setTicks(condition->getTicks() - (offlineTime * 1000));
			if (condition->getTicks() <= 0) {
				removeCondition(condition);
			}
		}

		// Reload bestiary tracker
		refreshBestiaryTracker(getBestiaryTrackerList());

		g_game.checkPlayersRecord();
		IOLoginData::updateOnlineStatus(guid, true);
	}
}

void Player::onAttackedCreatureDisappear(bool isLogout)
{
	sendCancelTarget();

	if (!isLogout) {
		sendTextMessage(MESSAGE_FAILURE, "Target lost.");
	}
}

void Player::onFollowCreatureDisappear(bool isLogout)
{
	sendCancelTarget();

	if (!isLogout) {
		sendTextMessage(MESSAGE_FAILURE, "Target lost.");
	}
}

void Player::onChangeZone(ZoneType_t zone)
{
	if (zone == ZONE_PROTECTION) {
		if (attackedCreature && !hasFlag(PlayerFlag_IgnoreProtectionZone)) {
			setAttackedCreature(nullptr);
			onAttackedCreatureDisappear(false);
		}

		if (!group->access && isMounted()) {
			dismount();
			g_game.internalCreatureChangeOutfit(this, defaultOutfit);
			wasMounted = true;
		}
	} else {
		if (wasMounted) {
			toggleMount(true);
			wasMounted = false;
		}
	}
	
	onThinkWheelOfDestiny(true);
	sendWheelOfDestinyGiftOfLifeCooldown();

	g_game.updateCreatureWalkthrough(this);
	sendIcons();
	g_events->eventPlayerOnChangeZone(this, zone);
}

void Player::onAttackedCreatureChangeZone(ZoneType_t zone)
{
	if (zone == ZONE_PROTECTION) {
		if (!hasFlag(PlayerFlag_IgnoreProtectionZone)) {
			setAttackedCreature(nullptr);
			onAttackedCreatureDisappear(false);
		}
	} else if (zone == ZONE_NOPVP) {
		if (attackedCreature->getPlayer()) {
			if (!hasFlag(PlayerFlag_IgnoreProtectionZone)) {
				setAttackedCreature(nullptr);
				onAttackedCreatureDisappear(false);
			}
		}
	} else if (zone == ZONE_NORMAL) {
		//attackedCreature can leave a pvp zone if not pzlocked
		if (g_game.getWorldType() == WORLD_TYPE_NO_PVP) {
			if (attackedCreature->getPlayer()) {
				setAttackedCreature(nullptr);
				onAttackedCreatureDisappear(false);
			}
		}
	}
}

void Player::onRemoveCreature(Creature* creature, bool isLogout)
{
	Creature::onRemoveCreature(creature, isLogout);

	if (creature == this) {
		uint64_t savingTime = OTSYS_TIME();
		if (isLogout) {
			loginPosition = getPosition();
		}

		lastLogout = time(nullptr);

		if (eventWalk != 0) {
			setFollowCreature(nullptr);
		}

		if (tradePartner) {
			g_game.internalCloseTrade(this);
		}

		closeShopWindow();

		clearPartyInvitations();

		if (party) {
			party->leaveParty(this);
		}

		g_chat->removeUserFromAllChannels(*this);


		if (guild) {
			guild->removeMember(this);
		}

		IOLoginData::updateOnlineStatus(guid, false);

		bool saved = false;
		for (uint32_t tries = 0; tries < 3; ++tries) {
			if (IOLoginData::savePlayer(this)) {
				saved = true;
				break;
			}
		}

		if (!saved) {
			SPDLOG_WARN("Error while saving player: {}", getName());
		}
		if (isLogout) {
			SPDLOG_INFO("{} has logged out. (Saved in {}ms)", getName(), OTSYS_TIME() - savingTime);
		}
	}
}

void Player::openShopWindow(Npc* npc, const std::vector<ShopInfo>& shop)
{
	shopItemList = std::move(shop);
  std::map<uint32_t, uint32_t> tempInventoryMap;
  getAllItemTypeCountAndSubtype(tempInventoryMap);

  sendShop(npc);
  sendSaleItemList(tempInventoryMap);
}

bool Player::closeShopWindow(bool sendCloseShopWindow /*= true*/)
{
	//unreference callbacks
	int32_t onBuy;
	int32_t onSell;

	Npc* npc = getShopOwner(onBuy, onSell);
	if (!npc) {
		shopItemList.clear();
		return false;
	}

	setShopOwner(nullptr, -1, -1);
	npc->onPlayerEndTrade(this, onBuy, onSell);

	if (sendCloseShopWindow) {
		sendCloseShop();
	}

	shopItemList.clear();
	return true;
}

void Player::onWalk(Direction& dir)
{
	Creature::onWalk(dir);
	setNextActionTask(nullptr);
	setNextAction(OTSYS_TIME() + getStepDuration(dir));
}

void Player::onCreatureMove(Creature* creature, const Tile* newTile, const Position& newPos,
							const Tile* oldTile, const Position& oldPos, bool teleport)
{
	Creature::onCreatureMove(creature, newTile, newPos, oldTile, oldPos, teleport);

	if (hasFollowPath && (creature == followCreature || (creature == this && followCreature))) {
		isUpdatingPath = false;
		g_game.addToCheckFollow(this);
	}

	if (creature != this) {
		return;
	}

	if (tradeState != TRADE_TRANSFER) {
		//check if we should close trade
		if (tradeItem && !Position::areInRange<1, 1, 0>(tradeItem->getPosition(), getPosition())) {
			g_game.internalCloseTrade(this);
		}

		if (tradePartner && !Position::areInRange<2, 2, 0>(tradePartner->getPosition(), getPosition())) {
			g_game.internalCloseTrade(this);
		}
	}

	// close modal windows
	if (!modalWindows.empty()) {
		// TODO: This shouldn't be hardcoded
		for (uint32_t modalWindowId : modalWindows) {
			if (modalWindowId == std::numeric_limits<uint32_t>::max()) {
				sendTextMessage(MESSAGE_EVENT_ADVANCE, "Offline training aborted.");
				break;
			}
		}
		modalWindows.clear();
	}

	// leave market
	if (inMarket) {
		inMarket = false;
	}

	if (party) {
		party->updateSharedExperience();
		party->updatePlayerStatus(this, oldPos, newPos);
	}

	if (teleport || oldPos.z != newPos.z) {
		int32_t ticks = g_config.getNumber(ConfigManager::STAIRHOP_DELAY);
		if (ticks > 0) {
			if (Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_PACIFIED, ticks, 0)) {
				addCondition(condition);
			}
		}
	}
}

//container
void Player::onAddContainerItem(const Item* item)
{
	checkTradeState(item);
}

void Player::onUpdateContainerItem(const Container* container, const Item* oldItem, const Item* newItem)
{
	if (oldItem != newItem) {
		onRemoveContainerItem(container, oldItem);
	}

	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(oldItem);
	}
}

void Player::onRemoveContainerItem(const Container* container, const Item* item)
{
	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(item);

		if (tradeItem) {
			if (tradeItem->getParent() != container && container->isHoldingItem(tradeItem)) {
				g_game.internalCloseTrade(this);
			}
		}
	}

  checkLootContainers(item);
}

void Player::onCloseContainer(const Container* container)
{
	if (!client) {
		return;
	}

	for (const auto& it : openContainers) {
		if (it.second.container == container) {
			client->sendCloseContainer(it.first);
		}
	}
}

void Player::onSendContainer(const Container* container)
{
	if (!client) {
		return;
	}

	bool hasParent = container->hasParent();
	for (const auto& it : openContainers) {
		const OpenContainer& openContainer = it.second;
		if (openContainer.container == container) {
			client->sendContainer(it.first, container, hasParent, openContainer.index);
		}
	}
}

//inventory
void Player::onUpdateInventoryItem(Item* oldItem, Item* newItem)
{
	if (oldItem != newItem) {
		onRemoveInventoryItem(oldItem);
	}

	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(oldItem);
	}
}

void Player::onRemoveInventoryItem(Item* item)
{
	if (tradeState != TRADE_TRANSFER) {
		checkTradeState(item);

		if (tradeItem) {
			const Container* container = item->getContainer();
			if (container && container->isHoldingItem(tradeItem)) {
				g_game.internalCloseTrade(this);
			}
		}
	}

  checkLootContainers(item);
}

void Player::checkTradeState(const Item* item)
{
	if (!tradeItem || tradeState == TRADE_TRANSFER) {
		return;
	}

	if (tradeItem == item) {
		g_game.internalCloseTrade(this);
	} else {
		const Container* container = dynamic_cast<const Container*>(item->getParent());
		while (container) {
			if (container == tradeItem) {
				g_game.internalCloseTrade(this);
				break;
			}

			container = dynamic_cast<const Container*>(container->getParent());
		}
	}
}

void Player::setNextWalkActionTask(SchedulerTask* task)
{
	if (walkTaskEvent != 0) {
		g_scheduler.stopEvent(walkTaskEvent);
		walkTaskEvent = 0;
	}

	delete walkTask;
	walkTask = task;
}

void Player::setNextWalkTask(SchedulerTask* task)
{
	if (nextStepEvent != 0) {
		g_scheduler.stopEvent(nextStepEvent);
		nextStepEvent = 0;
	}

	if (task) {
		nextStepEvent = g_scheduler.addEvent(task);
		resetIdleTime();
	}
}

void Player::setNextActionTask(SchedulerTask* task, bool resetIdleTime /*= true */)
{
	if (actionTaskEvent != 0) {
		g_scheduler.stopEvent(actionTaskEvent);
		actionTaskEvent = 0;
	}

	if (!inEventMovePush)
		cancelPush();

	if (task) {
		actionTaskEvent = g_scheduler.addEvent(task);
		if (resetIdleTime) {
			this->resetIdleTime();
		}
	}
}

void Player::setNextActionPushTask(SchedulerTask* task)
{
	if (actionTaskEventPush != 0) {
		g_scheduler.stopEvent(actionTaskEventPush);
		actionTaskEventPush = 0;
	}

	if (task) {
		actionTaskEventPush = g_scheduler.addEvent(task);
	}
}

void Player::setNextPotionActionTask(SchedulerTask* task)
{
	if (actionPotionTaskEvent != 0) {
		g_scheduler.stopEvent(actionPotionTaskEvent);
		actionPotionTaskEvent = 0;
	}

	cancelPush();

	if (task) {
		actionPotionTaskEvent = g_scheduler.addEvent(task);
		//resetIdleTime();
	}
}

uint32_t Player::getNextActionTime() const
{
	return std::max<int64_t>(SCHEDULER_MINTICKS, nextAction - OTSYS_TIME());
}

uint32_t Player::getNextPotionActionTime() const
{
	return std::max<int64_t>(SCHEDULER_MINTICKS, nextPotionAction - OTSYS_TIME());
}

void Player::cancelPush()
{
	if (actionTaskEventPush !=  0) {
		g_scheduler.stopEvent(actionTaskEventPush);
		actionTaskEventPush = 0;
		inEventMovePush = false;
	}
}

void Player::onThink(uint32_t interval)
{
	Creature::onThink(interval);

	sendPing();

	MessageBufferTicks += interval;
	if (MessageBufferTicks >= 1500) {
		MessageBufferTicks = 0;
		addMessageBuffer();
	}

	if (!getTile()->hasFlag(TILESTATE_NOLOGOUT) && !isAccessPlayer() && !isExerciseTraining()) {
		idleTime += interval;
		const int32_t kickAfterMinutes = g_config.getNumber(ConfigManager::KICK_AFTER_MINUTES);
		if (idleTime > (kickAfterMinutes * 60000) + 60000) {
			kickPlayer(true);
		} else if (client && idleTime == 60000 * kickAfterMinutes) {
			std::ostringstream ss;
			ss << "There was no variation in your behaviour for " << kickAfterMinutes << " minutes. You will be disconnected in one minute if there is no change in your actions until then.";
			client->sendTextMessage(TextMessage(MESSAGE_ADMINISTRADOR, ss.str()));
		}
	}

	if (g_game.getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		checkSkullTicks(interval / 1000);
	}

	addOfflineTrainingTime(interval);
	if (lastStatsTrainingTime != getOfflineTrainingTime() / 60 / 1000) {
		sendStats();
	}

	// Momentum
	if (getCondition(CONDITION_INFIGHT)) {
		if (varSpecial[SPECIAL_MOMENTUM] != 0) {
			lastMomentumTime += interval;
			if (lastMomentumTime >= 2000) {
				double chance = uniform_double_random();
				if (chance <= varSpecial[SPECIAL_MOMENTUM]) {
					reduceSpellCooldown(2000);
				}

				lastMomentumTime = 0;
			}
		}
	}
	// Wheel of destiny major spells
	onThinkWheelOfDestiny();
}

uint32_t Player::isMuted() const
{
	if (hasFlag(PlayerFlag_CannotBeMuted)) {
		return 0;
	}

	int32_t muteTicks = 0;
	for (Condition* condition : conditions) {
		if (condition->getType() == CONDITION_MUTED && condition->getTicks() > muteTicks) {
			muteTicks = condition->getTicks();
		}
	}
	return static_cast<uint32_t>(muteTicks) / 1000;
}

void Player::addMessageBuffer()
{
	if (MessageBufferCount > 0 && g_config.getNumber(ConfigManager::MAX_MESSAGEBUFFER) != 0 && !hasFlag(PlayerFlag_CannotBeMuted)) {
		--MessageBufferCount;
	}
}

void Player::removeMessageBuffer()
{
	if (hasFlag(PlayerFlag_CannotBeMuted)) {
		return;
	}

	const int32_t maxMessageBuffer = g_config.getNumber(ConfigManager::MAX_MESSAGEBUFFER);
	if (maxMessageBuffer != 0 && MessageBufferCount <= maxMessageBuffer + 1) {
		if (++MessageBufferCount > maxMessageBuffer) {
			uint32_t muteCount = 1;
			auto it = muteCountMap.find(guid);
			if (it != muteCountMap.end()) {
				muteCount = it->second;
			}

			uint32_t muteTime = 5 * muteCount * muteCount;
			muteCountMap[guid] = muteCount + 1;
			Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_MUTED, muteTime * 1000, 0);
			addCondition(condition);

			std::ostringstream ss;
			ss << "You are muted for " << muteTime << " seconds.";
			sendTextMessage(MESSAGE_FAILURE, ss.str());
		}
	}
}

void Player::drainHealth(Creature* attacker, int64_t damage)
{
	Creature::drainHealth(attacker, damage);
	sendStats();
}

void Player::drainMana(Creature* attacker, int64_t manaLoss)
{
	Creature::drainMana(attacker, manaLoss);
	sendStats();
}

void Player::addManaSpent(uint64_t amount)
{
	if (hasFlag(PlayerFlag_NotGainMana)) {
		return;
	}

	uint64_t currReqMana = vocation->getReqMana(magLevel);
	uint64_t nextReqMana = vocation->getReqMana(magLevel + 1);
	if (currReqMana >= nextReqMana) {
		//player has reached max magic level
		return;
	}

	g_events->eventPlayerOnGainSkillTries(this, SKILL_MAGLEVEL, amount);
	if (amount == 0) {
		return;
	}

	bool sendUpdateStats = false;
	while ((manaSpent + amount) >= nextReqMana) {
		amount -= nextReqMana - manaSpent;

		magLevel++;
		manaSpent = 0;

		std::ostringstream ss;
		ss << "You advanced to magic level " << magLevel << '.';
		sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());

		g_creatureEvents->playerAdvance(this, SKILL_MAGLEVEL, magLevel - 1, magLevel);

		sendUpdateStats = true;
		currReqMana = nextReqMana;
		nextReqMana = vocation->getReqMana(magLevel + 1);
		if (currReqMana >= nextReqMana) {
			return;
		}
	}

	manaSpent += amount;

	uint8_t oldPercent = magLevelPercent;
	if (nextReqMana > currReqMana) {
		magLevelPercent = Player::getPercentLevel(manaSpent, nextReqMana);
	} else {
		magLevelPercent = 0;
	}

	if (oldPercent != magLevelPercent) {
		sendUpdateStats = true;
	}

	if (sendUpdateStats) {
		sendStats();
		sendSkills();
	}
}

void Player::addExperience(Creature* source, uint64_t exp, bool sendText/* = false*/)
{
	uint64_t currLevelExp = Player::getExpForLevel(level);
	uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
	uint64_t rawExp = exp;
	if (currLevelExp >= nextLevelExp) {
		//player has reached max level
		levelPercent = 0;
		sendStats();
		return;
	}

	g_events->eventPlayerOnGainExperience(this, source, exp, rawExp);
	if (exp == 0) {
		return;
	}

	experience += exp;

	if (sendText) {
		std::string expString = std::to_string(exp) + (exp != 1 ? " experience points." : " experience point.");

		TextMessage message(MESSAGE_EXPERIENCE, "You gained " + expString);
		message.position = position;
		message.primary.value = exp;
		message.primary.color = TEXTCOLOR_WHITE_EXP;
		sendTextMessage(message);

		SpectatorHashSet spectators;
		g_game.map.getSpectators(spectators, position, false, true);
		spectators.erase(this);
		if (!spectators.empty()) {
			message.type = MESSAGE_EXPERIENCE_OTHERS;
			message.text = getName() + " gained " + expString;
			for (Creature* spectator : spectators) {
				spectator->getPlayer()->sendTextMessage(message);
			}
		}
	}

	uint32_t prevLevel = level;
	while (experience >= nextLevelExp) {
		++level;
		// Player stats gain for vocations level <= 8
		if (vocation->getId() != VOCATION_NONE && level <= 8) {
			Vocation* noneVocation = g_vocations.getVocation(VOCATION_NONE);
			healthMax += noneVocation->getHPGain();
			health += noneVocation->getHPGain();
			manaMax += noneVocation->getManaGain();
			mana += noneVocation->getManaGain();
			capacity += noneVocation->getCapGain();
		} else {
			healthMax += vocation->getHPGain();
			health += vocation->getHPGain();
			manaMax += vocation->getManaGain();
			mana += vocation->getManaGain();
			capacity += vocation->getCapGain();
		}

		currLevelExp = nextLevelExp;
		nextLevelExp = Player::getExpForLevel(level + 1);
		if (currLevelExp >= nextLevelExp) {
			//player has reached max level
			break;
		}
	}

	if (prevLevel != level) {
		health = healthMax;
		mana = manaMax;

		updateBaseSpeed();
		setBaseSpeed(getBaseSpeed());
		g_game.changeSpeed(this, 0);
		g_game.addCreatureHealth(this);
		g_game.addPlayerMana(this);

		if (party) {
			party->updateSharedExperience();
		}

		g_creatureEvents->playerAdvance(this, SKILL_LEVEL, prevLevel, level);

		std::ostringstream ss;
		ss << "You advanced from Level " << prevLevel << " to Level " << level << '.';
		sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
	}

	if (nextLevelExp > currLevelExp) {
		levelPercent = Player::getPercentLevel(experience - currLevelExp, nextLevelExp - currLevelExp);
	} else {
		levelPercent = 0;
	}
	sendStats();
	sendExperienceInfo(rawExp, exp);
}

void Player::removeExperience(uint64_t exp, bool sendText/* = false*/)
{
	if (experience == 0 || exp == 0) {
		return;
	}

	g_events->eventPlayerOnLoseExperience(this, exp);
	if (exp == 0) {
		return;
	}

	uint64_t lostExp = experience;
	experience = std::max<int64_t>(0, experience - exp);

	if (sendText) {
		lostExp -= experience;

		std::string expString = std::to_string(lostExp) + (lostExp != 1 ? " experience points." : " experience point.");

		TextMessage message(MESSAGE_EXPERIENCE, "You lost " + expString);
		message.position = position;
		message.primary.value = static_cast<int64_t>(lostExp);
		message.primary.color = TEXTCOLOR_RED;
		sendTextMessage(message);

		SpectatorHashSet spectators;
		g_game.map.getSpectators(spectators, position, false, true);
		spectators.erase(this);
		if (!spectators.empty()) {
			message.type = MESSAGE_EXPERIENCE_OTHERS;
			message.text = getName() + " lost " + expString;
			for (Creature* spectator : spectators) {
				spectator->getPlayer()->sendTextMessage(message);
			}
		}
	}

	uint32_t oldLevel = level;
	uint64_t currLevelExp = Player::getExpForLevel(level);

	while (level > 1 && experience < currLevelExp) {
		--level;
		// Player stats loss for vocations level <= 8
		if (vocation->getId() != VOCATION_NONE && level <= 8) {
			Vocation* noneVocation = g_vocations.getVocation(VOCATION_NONE);
			healthMax = std::max<int64_t>(0, healthMax - noneVocation->getHPGain());
			manaMax = std::max<int64_t>(0, manaMax - noneVocation->getManaGain());
			capacity = std::max<int32_t>(0, capacity - noneVocation->getCapGain());
		} else {
			healthMax = std::max<int64_t>(0, healthMax - vocation->getHPGain());
			manaMax = std::max<int64_t>(0, manaMax - vocation->getManaGain());
			capacity = std::max<int32_t>(0, capacity - vocation->getCapGain());
		}
		currLevelExp = Player::getExpForLevel(level);
	}

	if (oldLevel != level) {
		health = healthMax;
		mana = manaMax;

		updateBaseSpeed();
		setBaseSpeed(getBaseSpeed());

		g_game.changeSpeed(this, 0);
		g_game.addCreatureHealth(this);
		g_game.addPlayerMana(this);

		if (party) {
			party->updateSharedExperience();
		}

		std::ostringstream ss;
		ss << "You were downgraded from Level " << oldLevel << " to Level " << level << '.';
		sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
	}

	uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
	if (nextLevelExp > currLevelExp) {
		levelPercent = Player::getPercentLevel(experience - currLevelExp, nextLevelExp - currLevelExp);
	} else {
		levelPercent = 0;
	}
	sendStats();
}

double_t Player::getPercentLevel(uint64_t count, uint64_t nextLevelCount)
{
	if (nextLevelCount == 0) {
		return 0;
	}

  double_t result = round( ((count * 100.) / nextLevelCount) * 100.) / 100.;
	if (result > 100) {
		return 0;
	}
	return result;
}

void Player::onBlockHit()
{
	if (shieldBlockCount > 0) {
		--shieldBlockCount;

		if (hasShield()) {
			addSkillAdvance(SKILL_SHIELD, 1);
		}
	}
}

void Player::onAttackedCreatureBlockHit(BlockType_t blockType)
{
	lastAttackBlockType = blockType;

	switch (blockType) {
		case BLOCK_NONE: {
			addAttackSkillPoint = true;
			bloodHitCount = 30;
			shieldBlockCount = 30;
			break;
		}

		case BLOCK_DEFENSE:
		case BLOCK_ARMOR: {
			//need to draw blood every 30 hits
			if (bloodHitCount > 0) {
				addAttackSkillPoint = true;
				--bloodHitCount;
			} else {
				addAttackSkillPoint = false;
			}
			break;
		}

		default: {
			addAttackSkillPoint = false;
			break;
		}
	}
}

bool Player::hasShield() const
{
	Item* item = inventory[CONST_SLOT_LEFT];
	if (item && item->getWeaponType() == WEAPON_SHIELD) {
		return true;
	}

	item = inventory[CONST_SLOT_RIGHT];
	if (item && item->getWeaponType() == WEAPON_SHIELD) {
		return true;
	}
	return false;
}

BlockType_t Player::blockHit(Creature* attacker, CombatType_t combatType, int64_t& damage,
							 bool checkDefense /* = false*/, bool checkArmor /* = false*/, bool field /* = false*/)
{
	BlockType_t blockType = Creature::blockHit(attacker, combatType, damage, checkDefense, checkArmor, field);

	if (attacker) {
		sendCreatureSquare(attacker, SQ_COLOR_BLACK);
	}

	if (blockType != BLOCK_NONE) {
		return blockType;
	}

	if (damage > 0) {
		for (int32_t slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_LAST; ++slot) {
			if (!isItemAbilityEnabled(static_cast<slots_t>(slot))) {
				continue;
			}

			Item* item = inventory[slot];
			if (!item) {
				continue;
			}

			const ItemType& it = Item::items[item->getID()];
			if (it.abilities) {
				const int16_t& absorbPercent = it.abilities->absorbPercent[combatTypeToIndex(combatType)];
				if (absorbPercent != 0) {
					damage -= std::round(damage * (absorbPercent / 100.));

					uint16_t charges = item->getCharges();
					if (charges != 0) {
						g_game.transformItem(item, item->getID(), charges - 1);
					}
				}

				if (field) {
					const int16_t& fieldAbsorbPercent = it.abilities->fieldAbsorbPercent[combatTypeToIndex(combatType)];
					if (fieldAbsorbPercent != 0) {
						damage -= std::round(damage * (fieldAbsorbPercent / 100.));

						uint16_t charges = item->getCharges();
						if (charges != 0) {
							g_game.transformItem(item, item->getID(), charges - 1);
						}
					}
				}
				/*if (attacker) {
					const int16_t& reflectPercent = it.abilities->reflectPercent[combatTypeToIndex(combatType)];
					if (reflectPercent != 0) {
						CombatParams params;
						params.combatType = combatType;
						params.impactEffect = CONST_ME_MAGIC_BLUE;

						CombatDamage reflectDamage;
						reflectDamage.origin = ORIGIN_SPELL;
						reflectDamage.primary.type = combatType;
						reflectDamage.primary.value = std::round(-damage * (reflectPercent / 100.));

						Combat::doCombatHealth(this, attacker, reflectDamage, params);
					}
				}*/
			}

			uint8_t slots = Item::items[item->getID()].imbuingSlots;
			for (uint8_t i = 0; i < slots; i++) {
				uint32_t info = item->getImbuement(i);
				if (info >> 8) {
					Imbuement* ib = g_imbuements->getImbuement(info & 0xFF);
					const int16_t& absorbPercent2 = ib->absorbPercent[combatTypeToIndex(combatType)];

					if (absorbPercent2 != 0) {
						damage -= std::ceil(damage * (absorbPercent2 / 100.));
					}
				}
			}

		}

		if (damage > 0) {
			// Defense Potions
			Condition* protection = getCondition(CONDITION_SPECIALPOTION_EFFECT, CONDITIONID_DEFAULT, combatType);
			if (protection) {
				damage -= std::ceil(damage * (8 / 100.));
			}

			// Attack Potions
			if (attacker && attacker->getPlayer()) {
				Condition* damageBoost = attacker->getCondition(CONDITION_SPECIALPOTION_EFFECT, CONDITIONID_COMBAT, combatType);
				if (damageBoost) {
					damage += std::ceil(damage * (8 / 100.));
				}
			}
		}
		
		// Wheel of destiny
		int32_t wheelOfDestinyElementAbsorb = getWheelOfDestinyResistance(combatType);
		if (wheelOfDestinyElementAbsorb > 0) {
			damage -= std::ceil((damage * wheelOfDestinyElementAbsorb) / 10000.);
		}

		damage -= std::ceil((damage * checkWheelOfDestinyAvatarSkill(WHEEL_OF_DESTINY_AVATAR_SKILL_DAMAGE_REDUCTION)) / 100.);

		if (damage <= 0) {
			damage = 0;
			blockType = BLOCK_ARMOR;
		}
	}
	return blockType;
}

uint32_t Player::getIP() const
{
	if (client) {
		return client->getIP();
	}

	return 0;
}

void Player::resetSpellsCooldown()
{
	auto it = conditions.begin(); auto end = conditions.end();
	while (it != end) {
		ConditionType_t type = (*it)->getType();
		uint32_t spellId = (*it)->getSubId();
		int32_t ticks = (*it)->getTicks();
		int32_t newTicks = (ticks <= 2000) ? 0 : ticks - 2000;
		if (type == CONDITION_SPELLCOOLDOWN || (type == CONDITION_SPELLGROUPCOOLDOWN)) {
			(*it)->setTicks(newTicks);
			type == CONDITION_SPELLGROUPCOOLDOWN ? sendSpellGroupCooldown(static_cast<SpellGroup_t>(spellId), newTicks) : sendSpellCooldown(static_cast<uint8_t>(spellId), newTicks);
		}
		++it;
	}
}

void Player::death(Creature* lastHitCreature)
{
	loginPosition = town->getTemplePosition();

	g_game.sendSingleSoundEffect(this->getPosition(), sex == PLAYERSEX_FEMALE ? SOUND_EFFECT_TYPE_HUMAN_FEMALE_DEATH : SOUND_EFFECT_TYPE_HUMAN_MALE_DEATH, this);

	if (skillLoss) {
		uint8_t unfairFightReduction = 100;
		int playerDmg = 0;
		int othersDmg = 0;
		uint32_t sumLevels = 0;
		uint32_t inFightTicks = 5 * 60 * 1000;
		for (const auto& it : damageMap) {
			CountBlock_t cb = it.second;
			if ((OTSYS_TIME() - cb.ticks) <= inFightTicks) {
				Player* damageDealer = g_game.getPlayerByID(it.first);
				if (damageDealer) {
					playerDmg += cb.total;
					sumLevels += damageDealer->getLevel();
				}
				else{
					othersDmg += cb.total;
				}
			}
		}
		bool pvpDeath = false;
		if(playerDmg > 0 || othersDmg > 0){
			pvpDeath = (Player::lastHitIsPlayer(lastHitCreature) || playerDmg / (playerDmg + static_cast<double>(othersDmg)) >= 0.05);
		}
		if (pvpDeath && sumLevels > level) {
			double reduce = level / static_cast<double>(sumLevels);
			unfairFightReduction = std::max<uint8_t>(20, std::floor((reduce * 100) + 0.5));
		}

		//Magic level loss
		uint64_t sumMana = 0;
		uint64_t lostMana = 0;

		//sum up all the mana
		for (uint32_t i = 1; i <= magLevel; ++i) {
			sumMana += vocation->getReqMana(i);
		}

		sumMana += manaSpent;

		double deathLossPercent = getLostPercent() * (unfairFightReduction / 100.);

		// Charm bless bestiary
		if (lastHitCreature && lastHitCreature->getMonster()) {
			if (charmRuneBless != 0) {
				MonsterType* mType = g_monsters.getMonsterType(lastHitCreature->getName());
				if (mType && mType->info.raceid == charmRuneBless) {
				  deathLossPercent = (deathLossPercent * 90) / 100;
				}
			}
		}

		lostMana = static_cast<uint64_t>(sumMana * deathLossPercent);

		while (lostMana > manaSpent && magLevel > 0) {
			lostMana -= manaSpent;
			manaSpent = vocation->getReqMana(magLevel);
			magLevel--;
		}

		manaSpent -= lostMana;

		uint64_t nextReqMana = vocation->getReqMana(magLevel + 1);
		if (nextReqMana > vocation->getReqMana(magLevel)) {
			magLevelPercent = Player::getPercentLevel(manaSpent, nextReqMana);
		} else {
			magLevelPercent = 0;
		}

		//Skill loss
		for (uint8_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) { //for each skill
			uint64_t sumSkillTries = 0;
			for (uint16_t c = 11; c <= skills[i].level; ++c) { //sum up all required tries for all skill levels
				sumSkillTries += vocation->getReqSkillTries(i, c);
			}

			sumSkillTries += skills[i].tries;

			uint32_t lostSkillTries = static_cast<uint32_t>(sumSkillTries * deathLossPercent);
			while (lostSkillTries > skills[i].tries) {
				lostSkillTries -= skills[i].tries;

				if (skills[i].level <= 10) {
					skills[i].level = 10;
					skills[i].tries = 0;
					lostSkillTries = 0;
					break;
				}

				skills[i].tries = vocation->getReqSkillTries(i, skills[i].level);
				skills[i].level--;
			}

			skills[i].tries = std::max<int32_t>(0, skills[i].tries - lostSkillTries);
			skills[i].percent = Player::getPercentLevel(skills[i].tries, vocation->getReqSkillTries(i, skills[i].level));
		}

		//Level loss
		uint64_t expLoss = static_cast<uint64_t>(experience * deathLossPercent);
		g_events->eventPlayerOnLoseExperience(this, expLoss);

		if (expLoss != 0) {
			uint32_t oldLevel = level;

			if (vocation->getId() == VOCATION_NONE || level > 7) {
				experience -= expLoss;
			}

			while (level > 1 && experience < Player::getExpForLevel(level)) {
				--level;
				healthMax = std::max<int64_t>(0, healthMax - vocation->getHPGain());
				manaMax = std::max<int64_t>(0, manaMax - vocation->getManaGain());
				capacity = std::max<int32_t>(0, capacity - vocation->getCapGain());
			}

			if (oldLevel != level) {
				std::ostringstream ss;
				ss << "You were downgraded from Level " << oldLevel << " to Level " << level << '.';
				sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
			}

			uint64_t currLevelExp = Player::getExpForLevel(level);
			uint64_t nextLevelExp = Player::getExpForLevel(level + 1);
			if (nextLevelExp > currLevelExp) {
				levelPercent = Player::getPercentLevel(experience - currLevelExp, nextLevelExp - currLevelExp);
			} else {
				levelPercent = 0;
			}
		}

		//Make player lose bless
		uint8_t maxBlessing = 8;
		if (pvpDeath && hasBlessing(1)) {
			removeBlessing(1, 1); //Remove TOF only
		} else {
			for (int i = 2; i <= maxBlessing; i++) {
				removeBlessing(i, 1);
			}
		}

		sendStats();
		sendSkills();
		sendReLoginWindow(unfairFightReduction);
		sendBlessStatus();
		if (getSkull() == SKULL_BLACK) {
			health = 40;
			mana = 0;
		} else {
			health = healthMax;
			mana = manaMax;
		}

		auto it = conditions.begin(), end = conditions.end();
		while (it != end) {
			Condition* condition = *it;
			if (condition->isPersistent()) {
				it = conditions.erase(it);

				condition->endCondition(this);
				onEndCondition(condition->getType());
				delete condition;
			} else {
				++it;
			}
		}
	} else {
		setSkillLoss(true);

		auto it = conditions.begin(), end = conditions.end();
		while (it != end) {
			Condition* condition = *it;
			if (condition->isPersistent()) {
				it = conditions.erase(it);

				condition->endCondition(this);
				onEndCondition(condition->getType());
				delete condition;
			} else {
				++it;
			}
		}

		health = healthMax;
		g_game.internalTeleport(this, getTemplePosition(), true);
		g_game.addCreatureHealth(this);
		g_game.addPlayerMana(this);
		onThink(EVENT_CREATURE_THINK_INTERVAL);
		onIdleStatus();
		sendStats();
	}
}

bool Player::dropCorpse(Creature* lastHitCreature, Creature* mostDamageCreature, bool lastHitUnjustified, bool mostDamageUnjustified)
{
	if (getZone() != ZONE_PVP || !Player::lastHitIsPlayer(lastHitCreature)) {
		return Creature::dropCorpse(lastHitCreature, mostDamageCreature, lastHitUnjustified, mostDamageUnjustified);
	}

	setDropLoot(true);
	return false;
}

Item* Player::getCorpse(Creature* lastHitCreature, Creature* mostDamageCreature)
{
	Item* corpse = Creature::getCorpse(lastHitCreature, mostDamageCreature);
	if (corpse && corpse->getContainer()) {
		std::ostringstream ss;
		if (lastHitCreature) {
			ss << "You recognize " << getNameDescription() << ". " << (getSex() == PLAYERSEX_FEMALE ? "She" : "He") << " was killed by " << lastHitCreature->getNameDescription() << '.';
		} else {
			ss << "You recognize " << getNameDescription() << '.';
		}

		corpse->setSpecialDescription(ss.str());
	}
	return corpse;
}

void Player::addInFightTicks(bool pzlock /*= false*/)
{
	// Wheel of destiny
	bool reloadClient = false;
	if (getWheelOfDestinyInstant("Battle Instinct") && getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_BATTLE_INSTINCT) < OTSYS_TIME()) {
		if (checkWheelOfDestinyBattleInstinct()) {
			reloadClient = true;
		}
	}
	if (getWheelOfDestinyInstant("Positional Tatics") && getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_POSITIONAL_TATICS) < OTSYS_TIME()) {
		if (checkWheelOfDestinyPositionalTatics()) {
			reloadClient = true;
		}
	}
	if (getWheelOfDestinyInstant("Ballistic Mastery") && getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_BALLISTIC_MASTERY) < OTSYS_TIME()) {
		if (checkWheelOfDestinyBallisticMastery()) {
			reloadClient = true;
		}
	}
	if (reloadClient) {
		sendSkills();
		sendStats();
		//g_game.reloadCreature(this);
	}
	
	if (hasFlag(PlayerFlag_NotGainInFight)) {
		return;
	}

	if (pzlock) {
		pzLocked = true;
		sendIcons();
	}

	Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, g_config.getNumber(ConfigManager::PZ_LOCKED), 0);
	addCondition(condition);
}

void Player::removeList()
{
	g_game.removePlayer(this);

	for (const auto& it : g_game.getPlayers()) {
		it.second->notifyStatusChange(this, VIPSTATUS_OFFLINE);
	}
}

void Player::addList()
{
	for (const auto& it : g_game.getPlayers()) {
		it.second->notifyStatusChange(this, this->statusVipList);
	}

	g_game.addPlayer(this);
}

void Player::kickPlayer(bool displayEffect)
{
	g_creatureEvents->playerLogout(this);
	if (client) {
		client->logout(displayEffect, true);
	} else {
		g_game.removeCreature(this);
	}
}

void Player::notifyStatusChange(Player* loginPlayer, VipStatus_t status, bool message)
{
	if (!client) {
		return;
	}

	auto it = VIPList.find(loginPlayer->guid);
	if (it == VIPList.end()) {
		return;
	}

	client->sendUpdatedVIPStatus(loginPlayer->guid, status);

	if (message) {
		if (status == VIPSTATUS_ONLINE) {
			client->sendTextMessage(TextMessage(MESSAGE_FAILURE, loginPlayer->getName() + " has logged in."));
		} else if (status == VIPSTATUS_OFFLINE) {
			client->sendTextMessage(TextMessage(MESSAGE_FAILURE, loginPlayer->getName() + " has logged out."));
		}
	}
}

bool Player::removeVIP(uint32_t vipGuid)
{
	if (VIPList.erase(vipGuid) == 0) {
		return false;
	}

	IOLoginData::removeVIPEntry(accountNumber, vipGuid);
	return true;
}

bool Player::addVIP(uint32_t vipGuid, const std::string& vipName, VipStatus_t status)
{
	if (VIPList.size() >= getMaxVIPEntries() || VIPList.size() == 200) { // max number of buddies is 200 in 9.53
		sendTextMessage(MESSAGE_FAILURE, "You cannot add more buddies.");
		return false;
	}

	auto result = VIPList.insert(vipGuid);
	if (!result.second) {
		sendTextMessage(MESSAGE_FAILURE, "This player is already in your list.");
		return false;
	}

	IOLoginData::addVIPEntry(accountNumber, vipGuid, "", 0, false);
	if (client) {
		client->sendVIP(vipGuid, vipName, "", 0, false, status);
	}
	return true;
}

bool Player::addVIPInternal(uint32_t vipGuid)
{
	if (VIPList.size() >= getMaxVIPEntries() || VIPList.size() == 200) { // max number of buddies is 200 in 9.53
		return false;
	}

	return VIPList.insert(vipGuid).second;
}

bool Player::editVIP(uint32_t vipGuid, const std::string& description, uint32_t icon, bool notify)
{
	auto it = VIPList.find(vipGuid);
	if (it == VIPList.end()) {
		return false; // player is not in VIP
	}

	IOLoginData::editVIPEntry(accountNumber, vipGuid, description, icon, notify);
	return true;
}

//close container and its child containers
void Player::autoCloseContainers(const Container* container)
{
	std::vector<uint32_t> closeList;
	for (const auto& it : openContainers) {
		Container* tmpContainer = it.second.container;
		while (tmpContainer) {
			if (tmpContainer->isRemoved() || tmpContainer == container) {
				closeList.push_back(it.first);
				break;
			}

			tmpContainer = dynamic_cast<Container*>(tmpContainer->getParent());
		}
	}

	for (uint32_t containerId : closeList) {
		closeContainer(containerId);
		if (client) {
			client->sendCloseContainer(containerId);
		}
	}
}

bool Player::hasCapacity(const Item* item, uint32_t count) const
{
	if (hasFlag(PlayerFlag_CannotPickupItem)) {
		return false;
	}

	if (hasFlag(PlayerFlag_HasInfiniteCapacity) || item->getTopParent() == this) {
		return true;
	}

	uint32_t itemWeight = item->getContainer() != nullptr ? item->getWeight() : item->getBaseWeight();
	if (item->isStackable()) {
		itemWeight *= count;
	}
	return itemWeight <= getFreeCapacity();
}

ReturnValue Player::queryAdd(int32_t index, const Thing& thing, uint32_t count, uint32_t flags, Creature*) const
{
	const Item* item = thing.getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	bool childIsOwner = hasBitSet(FLAG_CHILDISOWNER, flags);
	if (childIsOwner) {
		//a child container is querying the player, just check if enough capacity
		bool skipLimit = hasBitSet(FLAG_NOLIMIT, flags);
		if (skipLimit || hasCapacity(item, count)) {
			return RETURNVALUE_NOERROR;
		}
		return RETURNVALUE_NOTENOUGHCAPACITY;
	}

	if (!item->isPickupable()) {
		return RETURNVALUE_CANNOTPICKUP;
	}

	ReturnValue ret = RETURNVALUE_NOERROR;

	const int32_t& slotPosition = item->getSlotPosition();
	if ((slotPosition & SLOTP_HEAD) || (slotPosition & SLOTP_NECKLACE) ||
			(slotPosition & SLOTP_BACKPACK) || (slotPosition & SLOTP_ARMOR) ||
			(slotPosition & SLOTP_LEGS) || (slotPosition & SLOTP_FEET) ||
			(slotPosition & SLOTP_RING)) {
		ret = RETURNVALUE_CANNOTBEDRESSED;
	} else if (slotPosition & SLOTP_TWO_HAND) {
		ret = RETURNVALUE_PUTTHISOBJECTINBOTHHANDS;
	} else if ((slotPosition & SLOTP_RIGHT) || (slotPosition & SLOTP_LEFT)) {
		if (!g_config.getBoolean(ConfigManager::CLASSIC_EQUIPMENT_SLOTS)) {
			ret = RETURNVALUE_CANNOTBEDRESSED;
		} else {
			ret = RETURNVALUE_PUTTHISOBJECTINYOURHAND;
		}
	}

	switch (index) {
		case CONST_SLOT_HEAD: {
			if (slotPosition & SLOTP_HEAD) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_NECKLACE: {
			if (slotPosition & SLOTP_NECKLACE) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_BACKPACK: {
			if (slotPosition & SLOTP_BACKPACK) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_ARMOR: {
			if (slotPosition & SLOTP_ARMOR) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_RIGHT: {
			if (slotPosition & SLOTP_RIGHT) {
				if (!g_config.getBoolean(ConfigManager::CLASSIC_EQUIPMENT_SLOTS)) {
          if (item->getWeaponType() != WEAPON_SHIELD && item->getWeaponType() != WEAPON_QUIVER) {
            ret = RETURNVALUE_CANNOTBEDRESSED;
          }
          else {
            const Item* leftItem = inventory[CONST_SLOT_LEFT];
            if (leftItem) {
              if ((leftItem->getSlotPosition() | slotPosition) & SLOTP_TWO_HAND) {
                if (item->getWeaponType() == WEAPON_QUIVER && leftItem->getWeaponType() == WEAPON_DISTANCE)
                  ret = RETURNVALUE_NOERROR;
                else
                  ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
              }
              else {
                ret = RETURNVALUE_NOERROR;
              }
            }
            else {
              ret = RETURNVALUE_NOERROR;
            }
          }
				} else if (slotPosition & SLOTP_TWO_HAND) {
					if (inventory[CONST_SLOT_LEFT] && inventory[CONST_SLOT_LEFT] != item) {
						ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
					} else {
						ret = RETURNVALUE_NOERROR;
					}
				} else if (inventory[CONST_SLOT_LEFT]) {
					const Item* leftItem = inventory[CONST_SLOT_LEFT];
					WeaponType_t type = item->getWeaponType(), leftType = leftItem->getWeaponType();

					if (leftItem->getSlotPosition() & SLOTP_TWO_HAND) {
						ret = RETURNVALUE_DROPTWOHANDEDITEM;
					} else if (item == leftItem && count == item->getItemCount()) {
						ret = RETURNVALUE_NOERROR;
					} else if (leftType == WEAPON_SHIELD && type == WEAPON_SHIELD) {
						ret = RETURNVALUE_CANONLYUSEONESHIELD;
					} else if (leftType == WEAPON_NONE || type == WEAPON_NONE ||
							   leftType == WEAPON_SHIELD || leftType == WEAPON_AMMO
							   || type == WEAPON_SHIELD || type == WEAPON_AMMO) {
						ret = RETURNVALUE_NOERROR;
					} else {
						ret = RETURNVALUE_CANONLYUSEONEWEAPON;
					}
				} else {
					ret = RETURNVALUE_NOERROR;
				}
			}
			break;
		}

		case CONST_SLOT_LEFT: {
			if (slotPosition & SLOTP_LEFT) {
				if (!g_config.getBoolean(ConfigManager::CLASSIC_EQUIPMENT_SLOTS)) {
					WeaponType_t type = item->getWeaponType();
					if (type == WEAPON_NONE || type == WEAPON_SHIELD || type == WEAPON_AMMO) {
						ret = RETURNVALUE_CANNOTBEDRESSED;
					} else if (inventory[CONST_SLOT_RIGHT] && (slotPosition & SLOTP_TWO_HAND)) {
						if (type == WEAPON_DISTANCE && inventory[CONST_SLOT_RIGHT]->getWeaponType() == WEAPON_QUIVER) {
							ret = RETURNVALUE_NOERROR;
						}
						else {
							ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
						}
					} else {
						ret = RETURNVALUE_NOERROR;
					}
				} else if (slotPosition & SLOTP_TWO_HAND) {
					if (inventory[CONST_SLOT_RIGHT] && inventory[CONST_SLOT_RIGHT] != item) {
						ret = RETURNVALUE_BOTHHANDSNEEDTOBEFREE;
					} else {
						ret = RETURNVALUE_NOERROR;
					}
				} else if (inventory[CONST_SLOT_RIGHT]) {
					const Item* rightItem = inventory[CONST_SLOT_RIGHT];
					WeaponType_t type = item->getWeaponType(), rightType = rightItem->getWeaponType();

					if (rightItem->getSlotPosition() & SLOTP_TWO_HAND) {
						ret = RETURNVALUE_DROPTWOHANDEDITEM;
					} else if (item == rightItem && count == item->getItemCount()) {
						ret = RETURNVALUE_NOERROR;
					} else if (rightType == WEAPON_SHIELD && type == WEAPON_SHIELD) {
						ret = RETURNVALUE_CANONLYUSEONESHIELD;
					} else if (rightType == WEAPON_NONE || type == WEAPON_NONE ||
							   rightType == WEAPON_SHIELD || rightType == WEAPON_AMMO
							   || type == WEAPON_SHIELD || type == WEAPON_AMMO) {
						ret = RETURNVALUE_NOERROR;
					} else {
						ret = RETURNVALUE_CANONLYUSEONEWEAPON;
					}
				} else {
					ret = RETURNVALUE_NOERROR;
				}
			}
			break;
		}

		case CONST_SLOT_LEGS: {
			if (slotPosition & SLOTP_LEGS) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_FEET: {
			if (slotPosition & SLOTP_FEET) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_RING: {
			if (slotPosition & SLOTP_RING) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_AMMO: {
			if ((slotPosition & SLOTP_AMMO) || g_config.getBoolean(ConfigManager::CLASSIC_EQUIPMENT_SLOTS)) {
				ret = RETURNVALUE_NOERROR;
			}
			break;
		}

		case CONST_SLOT_WHEREEVER:
		case -1:
			ret = RETURNVALUE_NOTENOUGHROOM;
			break;

		default:
			ret = RETURNVALUE_NOTPOSSIBLE;
			break;
	}

	if (ret == RETURNVALUE_NOERROR || ret == RETURNVALUE_NOTENOUGHROOM) {
		//need an exchange with source?
		const Item* inventoryItem = getInventoryItem(static_cast<slots_t>(index));
		if (inventoryItem && (!inventoryItem->isStackable() || inventoryItem->getID() != item->getID())) {
			return RETURNVALUE_NEEDEXCHANGE;
		}

		//check if enough capacity
		if (!hasCapacity(item, count)) {
			return RETURNVALUE_NOTENOUGHCAPACITY;
		}

		if (!g_moveEvents->onPlayerEquip(const_cast<Player*>(this), const_cast<Item*>(item), static_cast<slots_t>(index), true)) {
			return RETURNVALUE_CANNOTBEDRESSED;
		}
	}

	return ret;
}

ReturnValue Player::queryMaxCount(int32_t index, const Thing& thing, uint32_t count, uint32_t& maxQueryCount,
		uint32_t flags) const
{
	const Item* item = thing.getItem();
	if (item == nullptr) {
		maxQueryCount = 0;
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (index == INDEX_WHEREEVER) {
		uint32_t n = 0;
		for (int32_t slotIndex = CONST_SLOT_FIRST; slotIndex <= CONST_SLOT_LAST; ++slotIndex) {
			Item* inventoryItem = inventory[slotIndex];
			if (inventoryItem) {
				if (Container* subContainer = inventoryItem->getContainer()) {
					uint32_t queryCount = 0;
					subContainer->queryMaxCount(INDEX_WHEREEVER, *item, item->getItemCount(), queryCount, flags);
					n += queryCount;

					//iterate through all items, including sub-containers (deep search)
					for (ContainerIterator it = subContainer->iterator(); it.hasNext(); it.advance()) {
						if (Container* tmpContainer = (*it)->getContainer()) {
							queryCount = 0;
							tmpContainer->queryMaxCount(INDEX_WHEREEVER, *item, item->getItemCount(), queryCount, flags);
							n += queryCount;
						}
					}
				} else if (inventoryItem->isStackable() && item->equals(inventoryItem) && inventoryItem->getItemCount() < 100) {
					uint32_t remainder = (100 - inventoryItem->getItemCount());

					if (queryAdd(slotIndex, *item, remainder, flags) == RETURNVALUE_NOERROR) {
						n += remainder;
					}
				}
			} else if (queryAdd(slotIndex, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) { //empty slot
				if (item->isStackable()) {
					n += 100;
				} else {
					++n;
				}
			}
		}

		maxQueryCount = n;
	} else {
		const Item* destItem = nullptr;

		const Thing* destThing = getThing(index);
		if (destThing) {
			destItem = destThing->getItem();
		}

		if (destItem) {
			if (destItem->isStackable() && item->equals(destItem) && destItem->getItemCount() < 100) {
				maxQueryCount = 100 - destItem->getItemCount();
			} else {
				maxQueryCount = 0;
			}
		} else if (queryAdd(index, *item, count, flags) == RETURNVALUE_NOERROR) { //empty slot
			if (item->isStackable()) {
				maxQueryCount = 100;
			} else {
				maxQueryCount = 1;
			}

			return RETURNVALUE_NOERROR;
		}
	}

	if (maxQueryCount < count) {
		return RETURNVALUE_NOTENOUGHROOM;
	} else {
		return RETURNVALUE_NOERROR;
	}
}

ReturnValue Player::queryRemove(const Thing& thing, uint32_t count, uint32_t flags, Creature* /*= nullptr*/) const
{
	int32_t index = getThingIndex(&thing);
	if (index == -1) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	const Item* item = thing.getItem();
	if (item == nullptr) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (count == 0 || (item->isStackable() && count > item->getItemCount())) {
		return RETURNVALUE_NOTPOSSIBLE;
	}

	if (!item->isMoveable() && !hasBitSet(FLAG_IGNORENOTMOVEABLE, flags)) {
		return RETURNVALUE_NOTMOVEABLE;
	}

	return RETURNVALUE_NOERROR;
}

Cylinder* Player::queryDestination(int32_t& index, const Thing& thing, Item** destItem,
		uint32_t& flags)
{
	if (index == 0 /*drop to capacity window*/ || index == INDEX_WHEREEVER) {
		*destItem = nullptr;

		const Item* item = thing.getItem();
		if (item == nullptr) {
			return this;
		}

		bool autoStack = !((flags & FLAG_IGNOREAUTOSTACK) == FLAG_IGNOREAUTOSTACK);
		bool isStackable = item->isStackable();

		std::vector<Container*> containers;

		for (uint32_t slotIndex = CONST_SLOT_FIRST; slotIndex <= CONST_SLOT_AMMO; ++slotIndex) {
			Item* inventoryItem = inventory[slotIndex];
			if (inventoryItem) {
				if (inventoryItem == tradeItem) {
					continue;
				}

				if (inventoryItem == item) {
					continue;
				}

				if (autoStack && isStackable) {
					//try find an already existing item to stack with
					if (queryAdd(slotIndex, *item, item->getItemCount(), 0) == RETURNVALUE_NOERROR) {
						if (inventoryItem->equals(item) && inventoryItem->getItemCount() < 100) {
							index = slotIndex;
							*destItem = inventoryItem;
							return this;
						}
					}

					if (Container* subContainer = inventoryItem->getContainer()) {
						containers.push_back(subContainer);
					}
				} else if (Container* subContainer = inventoryItem->getContainer()) {
					containers.push_back(subContainer);
				}
			} else if (queryAdd(slotIndex, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) { //empty slot
				index = slotIndex;
				*destItem = nullptr;
				return this;
			}
		}

		size_t i = 0;
		while (i < containers.size()) {
			Container* tmpContainer = containers[i++];
			if (!autoStack || !isStackable) {
				//we need to find first empty container as fast as we can for non-stackable items
				uint32_t n = tmpContainer->capacity() - tmpContainer->size();
				while (n) {
					if (tmpContainer->queryAdd(tmpContainer->capacity() - n, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) {
						index = tmpContainer->capacity() - n;
						*destItem = nullptr;
						return tmpContainer;
					}

					n--;
				}

				for (Item* tmpContainerItem : tmpContainer->getItemList()) {
					if (Container* subContainer = tmpContainerItem->getContainer()) {
						containers.push_back(subContainer);
					}
				}

				continue;
			}

			uint32_t n = 0;

			for (Item* tmpItem : tmpContainer->getItemList()) {
				if (tmpItem == tradeItem) {
					continue;
				}

				if (tmpItem == item) {
					continue;
				}

				//try find an already existing item to stack with
				if (tmpItem->equals(item) && tmpItem->getItemCount() < 100) {
					index = n;
					*destItem = tmpItem;
					return tmpContainer;
				}

				if (Container* subContainer = tmpItem->getContainer()) {
					containers.push_back(subContainer);
				}

				n++;
			}

			if (n < tmpContainer->capacity() && tmpContainer->queryAdd(n, *item, item->getItemCount(), flags) == RETURNVALUE_NOERROR) {
				index = n;
				*destItem = nullptr;
				return tmpContainer;
			}
		}

		return this;
	}

	Thing* destThing = getThing(index);
	if (destThing) {
		*destItem = destThing->getItem();
	}

	Cylinder* subCylinder = dynamic_cast<Cylinder*>(destThing);
	if (subCylinder) {
		index = INDEX_WHEREEVER;
		*destItem = nullptr;
		return subCylinder;
	} else {
		return this;
	}
}

void Player::addThing(int32_t index, Thing* thing)
{
	if (index < CONST_SLOT_FIRST || index > CONST_SLOT_LAST) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	item->setParent(this);
	inventory[index] = item;

	//send to client
	sendInventoryItem(static_cast<slots_t>(index), item);
}

void Player::updateThing(Thing* thing, uint16_t itemId, uint32_t count)
{
	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	item->setID(itemId);
	item->setSubType(count);

	//send to client
	sendInventoryItem(static_cast<slots_t>(index), item);

	//event methods
	onUpdateInventoryItem(item, item);
}

void Player::replaceThing(uint32_t index, Thing* thing)
{
	if (index > CONST_SLOT_LAST) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* oldItem = getInventoryItem(static_cast<slots_t>(index));
	if (!oldItem) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	//send to client
	sendInventoryItem(static_cast<slots_t>(index), item);

	//event methods
	onUpdateInventoryItem(oldItem, item);

	item->setParent(this);

	inventory[index] = item;
}

void Player::removeThing(Thing* thing, uint32_t count)
{
	Item* item = thing->getItem();
	if (!item) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	int32_t index = getThingIndex(thing);
	if (index == -1) {
		return /*RETURNVALUE_NOTPOSSIBLE*/;
	}

	if (item->isStackable()) {
		if (count == item->getItemCount()) {
			//send change to client
			sendInventoryItem(static_cast<slots_t>(index), nullptr);

			//event methods
			onRemoveInventoryItem(item);

			item->setParent(nullptr);
			inventory[index] = nullptr;
		} else {
			uint8_t newCount = static_cast<uint8_t>(std::max<int32_t>(0, item->getItemCount() - count));
			item->setItemCount(newCount);

			//send change to client
			sendInventoryItem(static_cast<slots_t>(index), item);

			//event methods
			onUpdateInventoryItem(item, item);
		}
	} else {
		//send change to client
		sendInventoryItem(static_cast<slots_t>(index), nullptr);

		//event methods
		onRemoveInventoryItem(item);

		item->setParent(nullptr);
		inventory[index] = nullptr;
	}
}

int32_t Player::getThingIndex(const Thing* thing) const
{
	for (int i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		if (inventory[i] == thing) {
			return i;
		}
	}
	return -1;
}

size_t Player::getFirstIndex() const
{
	return CONST_SLOT_FIRST;
}

size_t Player::getLastIndex() const
{
	return CONST_SLOT_LAST + 1;
}

uint32_t Player::getItemTypeCount(uint16_t itemId, int32_t subType /*= -1*/) const
{
	uint32_t count = 0;
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		if (item->getID() == itemId) {
			count += Item::countByType(item, subType);
		}

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				if ((*it)->getID() == itemId) {
					count += Item::countByType(*it, subType);
				}
			}
		}
	}
	return count;
}

bool Player::isStashExhausted() const {
	uint32_t exhaust_time = 1500;
	return (OTSYS_TIME() - lastStashInteraction < exhaust_time);
}

void Player::stashContainer(StashContainerList itemDict)
{
	StashItemList stashItemDict; // ClientID - Count
	for (auto it_dict : itemDict) {
		stashItemDict[(it_dict.first)->getClientID()] = it_dict.second;
	}

	for (auto it : stashItems) {
		if(!stashItemDict[it.first]) {
			stashItemDict[it.first] = it.second;
		} else {
			stashItemDict[it.first] += it.second;
		}
	}

	if (getStashSize(stashItemDict) > g_config.getNumber(ConfigManager::STASH_ITEMS)) {
		sendCancelMessage("You don't have capacity in the Supply Stash to stow all this item.");
		return;
	}

	uint32_t totalStowed = 0;
	std::ostringstream retString;
	for (auto stashIterator : itemDict) {
		uint16_t iteratorCID = (stashIterator.first)->getClientID();
		if (g_game.internalRemoveItem(stashIterator.first, stashIterator.second) == RETURNVALUE_NOERROR) {
			addItemOnStash(iteratorCID, stashIterator.second);
			totalStowed += stashIterator.second;
		}
	}

	if (totalStowed == 0) {
		sendCancelMessage("Sorry, not possible.");
		return;
	}

	retString << "Stowed " << totalStowed << " object" << (totalStowed > 1 ? "s." : ".");
	sendTextMessage(MESSAGE_STATUS, retString.str());
}

bool Player::removeItemOfType(uint16_t itemId, uint32_t amount, int32_t subType, bool ignoreEquipped/* = false*/) const
{
	if (amount == 0) {
		return true;
	}

	std::vector<Item*> itemList;

	uint32_t count = 0;
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		if (!ignoreEquipped && item->getID() == itemId) {
			uint32_t itemCount = Item::countByType(item, subType);
			if (itemCount == 0) {
				continue;
			}

			itemList.push_back(item);

			count += itemCount;
			if (count >= amount) {
				g_game.internalRemoveItems(std::move(itemList), amount, Item::items[itemId].stackable);
				return true;
			}
		} else if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				Item* containerItem = *it;
				if (containerItem->getID() == itemId) {
					uint32_t itemCount = Item::countByType(containerItem, subType);
					if (itemCount == 0) {
						continue;
					}

					itemList.push_back(containerItem);

					count += itemCount;
					if (count >= amount) {
						g_game.internalRemoveItems(std::move(itemList), amount, Item::items[itemId].stackable);
						return true;
					}
				}
			}
		}
	}
	return false;
}

std::map<uint32_t, uint32_t>& Player::getAllItemTypeCount(std::map<uint32_t, uint32_t>& countMap) const
{
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		countMap[item->getID()] += Item::countByType(item, -1);

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				countMap[(*it)->getID()] += Item::countByType(*it, -1);
			}
		}
	}
	return countMap;
}

void Player::getAllItemType(std::multimap<uint16_t, uint8_t>& countMap) const
{
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		auto invetoryFind = countMap.find(item->getID());
		if (invetoryFind != countMap.end()) {
			if ((*invetoryFind).second != item->getBoost()) {
				if (item->getFluidType() != 0) {
					countMap.emplace(item->getID(), item->getFluidType());
				} else {
					countMap.emplace(item->getID(), item->getBoost());
				}
			}
		} else {
			if (item->getFluidType() != 0) {
				countMap.emplace(item->getID(), item->getFluidType());
			} else {
				countMap.emplace(item->getID(), item->getBoost());
			}
		}

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				item = (*it);

				auto backpackFind = countMap.find(item->getID());
				if (backpackFind != countMap.end()) {
					if ((*backpackFind).second != item->getBoost()) {
						if (item->getFluidType() != 0) {
							countMap.emplace(item->getID(), item->getFluidType());
						} else {
							countMap.emplace(item->getID(), item->getBoost());
						}
					}
				} else {
					if (item->getFluidType() != 0) {
						countMap.emplace(item->getID(), item->getFluidType());
					} else {
						countMap.emplace(item->getID(), item->getBoost());
					}
				}
			}
		}
	}
}

Item* Player::getItemByClientId(uint16_t clientId) const
{
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		if (item->getClientID() == clientId) {
			return item;
		}

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				if ((*it)->getClientID() == clientId) {
					return (*it);
				}
			}
		}
	}
	return nullptr;
}

std::map<uint16_t, uint16_t> Player::getInventoryClientIds() const
{
	std::map<uint16_t, uint16_t> itemMap;
	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		auto rootSearch = itemMap.find(item->getClientID());
		if (rootSearch != itemMap.end()) {
			itemMap[item->getClientID()] = itemMap[item->getClientID()] + Item::countByType(item, -1);
		}
		else
		{
			itemMap.emplace(item->getClientID(), Item::countByType(item, -1));
		}

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				auto containerSearch = itemMap.find((*it)->getClientID());
				if (containerSearch != itemMap.end()) {
					itemMap[(*it)->getClientID()] = itemMap[(*it)->getClientID()] + Item::countByType(*it, -1);
				}
				else
				{
					itemMap.emplace((*it)->getClientID(), Item::countByType(*it, -1));
				}
				itemMap.emplace((*it)->getClientID(), Item::countByType(*it, -1));
			}
		}
	}
	return itemMap;
}

void Player::getAllItemTypeCountAndSubtype(std::map<uint32_t, uint32_t>& countMap) const
{
  for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
    Item* item = inventory[i];
    if (!item) {
      continue;
    }

    uint16_t itemId = item->getID();
    if (Item::items[itemId].isFluidContainer()) {
      countMap[static_cast<uint32_t>(itemId) | (static_cast<uint32_t>(item->getFluidType()) << 16)] += item->getItemCount();
    } else {
      countMap[static_cast<uint32_t>(itemId)] += item->getItemCount();
    }

    if (Container* container = item->getContainer()) {
      for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
        item = (*it);

        itemId = item->getID();
        if (Item::items[itemId].isFluidContainer()) {
          countMap[static_cast<uint32_t>(itemId) | (static_cast<uint32_t>(item->getFluidType()) << 16)] += item->getItemCount();
        } else {
          countMap[static_cast<uint32_t>(itemId)] += item->getItemCount();
        }
      }
    }
  }
}

Thing* Player::getThing(size_t index) const
{
	if (index >= CONST_SLOT_FIRST && index <= CONST_SLOT_LAST) {
		return inventory[index];
	}
	return nullptr;
}

void Player::postAddNotification(Thing* thing, const Cylinder* oldParent, int32_t index, cylinderlink_t link /*= LINK_OWNER*/)
{
	if (link == LINK_OWNER) {
		//calling movement scripts
		g_moveEvents->onPlayerEquip(this, thing->getItem(), static_cast<slots_t>(index), false);
	}

	bool requireListUpdate = true;

	if (link == LINK_OWNER || link == LINK_TOPPARENT) {
		const Item* i = (oldParent ? oldParent->getItem() : nullptr);

		// Check if we owned the old container too, so we don't need to do anything,
		// as the list was updated in postRemoveNotification
		assert(i ? i->getContainer() != nullptr : true);

		if (i) {
			requireListUpdate = i->getContainer()->getHoldingPlayer() != this;
		} else {
			requireListUpdate = oldParent != this;
		}

		updateInventoryWeight();
		updateItemsLight();
		sendInvetoryItems();
		sendStats();
	}

	if (const Item* item = thing->getItem()) {
		if (const Container* container = item->getContainer()) {
			onSendContainer(container);
		}

		if (shopOwner && !scheduledSaleUpdate && requireListUpdate) {
			updateSaleShopList(item);
		}
	} else if (const Creature* creature = thing->getCreature()) {
		if (creature == this) {
			//check containers
			std::vector<Container*> containers;

			for (const auto& it : openContainers) {
				Container* container = it.second.container;
				if (!Position::areInRange<1, 1, 0>(container->getPosition(), getPosition())) {
					containers.push_back(container);
				}
			}

			for (const Container* container : containers) {
				autoCloseContainers(container);
			}
		}
	}
}

void Player::postRemoveNotification(Thing* thing, const Cylinder* newParent, int32_t index, cylinderlink_t link /*= LINK_OWNER*/)
{
	if (link == LINK_OWNER) {
		//calling movement scripts
		g_moveEvents->onPlayerDeEquip(this, thing->getItem(), static_cast<slots_t>(index));
	}

	bool requireListUpdate = true;

	if (link == LINK_OWNER || link == LINK_TOPPARENT) {
		const Item* i = (newParent ? newParent->getItem() : nullptr);

		// Check if we owned the old container too, so we don't need to do anything,
		// as the list was updated in postRemoveNotification
		assert(i ? i->getContainer() != nullptr : true);

		if (i) {
			requireListUpdate = i->getContainer()->getHoldingPlayer() != this;
		} else {
			requireListUpdate = newParent != this;
		}

		updateInventoryWeight();
		updateItemsLight();
		sendInvetoryItems();
		sendStats();
	}

	if (const Item* item = thing->getItem()) {
		if (const Container* container = item->getContainer()) {
      checkLootContainers(container);

			if (container->isRemoved() || !Position::areInRange<1, 1, 0>(getPosition(), container->getPosition())) {
				autoCloseContainers(container);
			} else if (container->getTopParent() == this) {
				onSendContainer(container);
			} else if (const Container* topContainer = dynamic_cast<const Container*>(container->getTopParent())) {
				if (const DepotChest* depotChest = dynamic_cast<const DepotChest*>(topContainer)) {
					bool isOwner = false;

					for (const auto& it : depotChests) {
						if (it.second == depotChest) {
							isOwner = true;
							it.second->stopDecaying();
							onSendContainer(container);
						}
					}

					if (!isOwner) {
						autoCloseContainers(container);
					}
				} else {
					onSendContainer(container);
				}
			} else {
				autoCloseContainers(container);
			}
		}

		if (shopOwner && !scheduledSaleUpdate && requireListUpdate) {
			updateSaleShopList(item);
		}
	}
}

// i will keep this function so it can be reviewed
bool Player::updateSaleShopList(const Item* item)
{
	uint16_t currency = shopOwner ? shopOwner->getCurrency() : ITEM_GOLD_COIN;
	uint16_t itemId = item->getID();
	if ((currency == ITEM_GOLD_COIN && itemId != ITEM_GOLD_COIN && itemId != ITEM_PLATINUM_COIN && itemId != ITEM_CRYSTAL_COIN) || (currency != ITEM_GOLD_COIN && itemId != currency)) {
		auto it = std::find_if(shopItemList.begin(), shopItemList.end(), [itemId](const ShopInfo& shopInfo) { return shopInfo.itemId == itemId && shopInfo.sellPrice != 0; });
		if (it == shopItemList.end()) {
			const Container* container = item->getContainer();
			if (!container) {
				return false;
			}

			const auto& items = container->getItemList();
			return std::any_of(items.begin(), items.end(), [this](const Item* containerItem) {
				return updateSaleShopList(containerItem);
			});
		}
	}

	g_dispatcher.addTask(createTask(std::bind(&Game::updatePlayerSaleItems, &g_game, getID())));
	scheduledSaleUpdate = true;
	return true;
}

bool Player::hasShopItemForSale(uint32_t itemId, uint8_t subType) const
{
	const ItemType& itemType = Item::items[itemId];
	return std::any_of(shopItemList.begin(), shopItemList.end(), [&](const ShopInfo& shopInfo) {
		return shopInfo.itemId == itemId && shopInfo.buyPrice != 0 && (!itemType.isFluidContainer() || shopInfo.subType == subType);
	});
}

void Player::internalAddThing(Thing* thing)
{
	internalAddThing(0, thing);
}

void Player::internalAddThing(uint32_t index, Thing* thing)
{
	Item* item = thing->getItem();
	if (!item) {
		return;
	}

	//index == 0 means we should equip this item at the most appropiate slot (no action required here)
	if (index >= CONST_SLOT_FIRST && index <= CONST_SLOT_LAST) {
		if (inventory[index]) {
			return;
		}

		inventory[index] = item;
		item->setParent(this);
	}
}

bool Player::setFollowCreature(Creature* creature)
{
	if (!Creature::setFollowCreature(creature)) {
		setFollowCreature(nullptr);
		setAttackedCreature(nullptr);

		sendCancelMessage(RETURNVALUE_THEREISNOWAY);
		sendCancelTarget();
		stopWalk();
		return false;
	}
	return true;
}

bool Player::setAttackedCreature(Creature* creature)
{
	if (!Creature::setAttackedCreature(creature)) {
		sendCancelTarget();
		return false;
	}

	if (chaseMode && creature) {
		if (followCreature != creature) {
			//chase opponent
			setFollowCreature(creature);
		}
	} else if (followCreature) {
		setFollowCreature(nullptr);
	}

	if (creature) {
		g_dispatcher.addTask(createTask(std::bind(&Game::checkCreatureAttack, &g_game, getID())));
	}
	return true;
}

void Player::goToFollowCreature()
{
	if (!walkTask) {
		if ((OTSYS_TIME() - lastFailedFollow) < 2000) {
			return;
		}

		Creature::goToFollowCreature();

		if (followCreature && !hasFollowPath) {
			lastFailedFollow = OTSYS_TIME();
		}
	}
}

void Player::getPathSearchParams(const Creature* creature, FindPathParams& fpp) const
{
	Creature::getPathSearchParams(creature, fpp);
	fpp.fullPathSearch = true;
}

void Player::doAttacking(uint32_t)
{
	if (lastAttack == 0) {
		lastAttack = OTSYS_TIME() - getAttackSpeed() - 1;
	}

	if (hasCondition(CONDITION_PACIFIED)) {
		return;
	}

	if ((OTSYS_TIME() - lastAttack) >= getAttackSpeed()) {
		bool result = false;

		Item* tool = getWeapon();
		const Weapon* weapon = g_weapons->getWeapon(tool);
		uint32_t delay = getAttackSpeed();
		bool classicSpeed = g_config.getBoolean(ConfigManager::CLASSIC_ATTACK_SPEED);

		if (weapon) {
			if (!weapon->interruptSwing()) {
				result = weapon->useWeapon(this, tool, attackedCreature);
			} else if (!classicSpeed && !canDoAction()) {
				delay = getNextActionTime();
			} else {
				result = weapon->useWeapon(this, tool, attackedCreature);
			}
		} else {
			result = Weapon::useFist(this, attackedCreature);
		}

		SchedulerTask* task = createSchedulerTask(std::max<uint32_t>(SCHEDULER_MINTICKS, delay), std::bind(&Game::checkCreatureAttack, &g_game, getID()));
		if (!classicSpeed) {
			setNextActionTask(task, false);
		} else {
			g_scheduler.addEvent(task);
		}

		if (result) {
			lastAttack = OTSYS_TIME();
		}
	}
}

uint64_t Player::getGainedExperience(Creature* attacker) const
{
	if (g_config.getBoolean(ConfigManager::EXPERIENCE_FROM_PLAYERS)) {
		Player* attackerPlayer = attacker->getPlayer();
		if (attackerPlayer && attackerPlayer != this && skillLoss && std::abs(static_cast<int32_t>(attackerPlayer->getLevel() - level)) <= g_config.getNumber(ConfigManager::EXP_FROM_PLAYERS_LEVEL_RANGE)) {
			return std::max<uint64_t>(0, std::floor(getLostExperience() * getDamageRatio(attacker) * 0.75));
		}
	}
	return 0;
}

void Player::onFollowCreature(const Creature* creature)
{
	if (!creature) {
		stopWalk();
	}
}

void Player::setChaseMode(bool mode)
{
	bool prevChaseMode = chaseMode;
	chaseMode = mode;

	if (prevChaseMode != chaseMode) {
		if (chaseMode) {
			if (!followCreature && attackedCreature) {
				//chase opponent
				setFollowCreature(attackedCreature);
			}
		} else if (attackedCreature) {
			setFollowCreature(nullptr);
			cancelNextWalk = true;
		}
	}
}

void Player::onWalkAborted()
{
	setNextWalkActionTask(nullptr);
	sendCancelWalk();
}

void Player::onWalkComplete()
{
	if (walkTask) {
		walkTaskEvent = g_scheduler.addEvent(walkTask);
		walkTask = nullptr;
	}
}

void Player::stopWalk()
{
	cancelNextWalk = true;
}

LightInfo Player::getCreatureLight() const
{
	if (internalLight.level > itemsLight.level) {
		return internalLight;
	}
	return itemsLight;
}

void Player::updateItemsLight(bool internal /*=false*/)
{
	LightInfo maxLight;

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		Item* item = inventory[i];
		if (item) {
			LightInfo curLight = item->getLightInfo();

			if (curLight.level > maxLight.level) {
				maxLight = std::move(curLight);
			}
		}
	}

	if (itemsLight.level != maxLight.level || itemsLight.color != maxLight.color) {
		itemsLight = maxLight;

		if (!internal) {
			g_game.changeLight(this);
		}
	}
}

void Player::onAddCondition(ConditionType_t type)
{
	Creature::onAddCondition(type);

	if (type == CONDITION_OUTFIT && isMounted()) {
		dismount();
	}

	sendIcons();
}

void Player::onAddCombatCondition(ConditionType_t type)
{
	switch (type) {
		case CONDITION_POISON:
			sendTextMessage(MESSAGE_FAILURE, "You are poisoned.");
			break;

		case CONDITION_DROWN:
			sendTextMessage(MESSAGE_FAILURE, "You are drowning.");
			break;

		case CONDITION_PARALYZE:
			sendTextMessage(MESSAGE_FAILURE, "You are paralyzed.");
			break;

		case CONDITION_DRUNK:
			sendTextMessage(MESSAGE_FAILURE, "You are drunk.");
			break;

		case CONDITION_ROOTED:
			sendTextMessage(MESSAGE_FAILURE, "You are rooted.");
			break;

		case CONDITION_CURSED:
			sendTextMessage(MESSAGE_FAILURE, "You are cursed.");
			break;

		case CONDITION_FREEZING:
			sendTextMessage(MESSAGE_FAILURE, "You are freezing.");
			break;

		case CONDITION_DAZZLED:
			sendTextMessage(MESSAGE_FAILURE, "You are dazzled.");
			break;

		case CONDITION_BLEEDING:
			sendTextMessage(MESSAGE_FAILURE, "You are bleeding.");
			break;

		default:
			break;
	}
}

void Player::onEndCondition(ConditionType_t type)
{
	Creature::onEndCondition(type);

	if (type == CONDITION_INFIGHT) {
		onIdleStatus();
		pzLocked = false;
		clearAttacked();

		if (getSkull() != SKULL_RED && getSkull() != SKULL_BLACK) {
			setSkull(SKULL_NONE);
		}
	}

	sendIcons();
}

void Player::onCombatRemoveCondition(Condition* condition)
{
	//Creature::onCombatRemoveCondition(condition);
	if (condition->getId() > 0) {
		//Means the condition is from an item, id == slot
		if (g_game.getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
			Item* item = getInventoryItem(static_cast<slots_t>(condition->getId()));
			if (item) {
				//25% chance to destroy the item
				if (25 >= uniform_random(1, 100)) {
					g_game.internalRemoveItem(item);
				}
			}
		}
	} else {
		if (!canDoAction()) {
			const uint32_t delay = getNextActionTime();
			const int32_t ticks = delay - (delay % EVENT_CREATURE_THINK_INTERVAL);
			if (ticks < 0) {
				removeCondition(condition);
			} else {
				condition->setTicks(ticks);
			}
		} else {
			removeCondition(condition);
		}
	}
}

void Player::onAttackedCreature(Creature* target)
{
	Creature::onAttackedCreature(target);

	if (target->getZone() == ZONE_PVP) {
		return;
	}

	if (target == this) {
		addInFightTicks();
		return;
	}

	if (hasFlag(PlayerFlag_NotGainInFight)) {
		return;
	}

	Player* targetPlayer = target->getPlayer();
	if (targetPlayer && !isPartner(targetPlayer) && !isGuildMate(targetPlayer)) {
		if (!pzLocked && g_game.getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
			pzLocked = true;
			sendIcons();
		}

		if (getSkull() == SKULL_NONE && getSkullClient(targetPlayer) == SKULL_YELLOW) {
			addAttacked(targetPlayer);
			targetPlayer->sendCreatureSkull(this);
		} else if (!targetPlayer->hasAttacked(this)) {
			if (!pzLocked) {
				pzLocked = true;
				sendIcons();
			}

			if (!Combat::isInPvpZone(this, targetPlayer) && !isInWar(targetPlayer)) {
				addAttacked(targetPlayer);

				if (targetPlayer->getSkull() == SKULL_NONE && getSkull() == SKULL_NONE && !targetPlayer->hasKilled(this)) {
					setSkull(SKULL_WHITE);
				}

				if (getSkull() == SKULL_NONE) {
					targetPlayer->sendCreatureSkull(this);
				}
			}
		}
	}

	addInFightTicks();
}

void Player::onAttacked()
{
	Creature::onAttacked();

	addInFightTicks();
}

void Player::onIdleStatus()
{
	Creature::onIdleStatus();

	if (party) {
		party->clearPlayerPoints(this);
	}
}

void Player::onPlacedCreature()
{
	//scripting event - onLogin
	if (!g_creatureEvents->playerLogin(this)) {
		kickPlayer(true);
	}

	sendUnjustifiedPoints();
}

void Player::onAttackedCreatureDrainHealth(Creature* target, int64_t points)
{
	Creature::onAttackedCreatureDrainHealth(target, points);

	if (target) {
		if (party && !Combat::isPlayerCombat(target)) {
			Monster* tmpMonster = target->getMonster();
			if (tmpMonster && tmpMonster->isHostile()) {
				//We have fulfilled a requirement for shared experience
				party->updatePlayerTicks(this, points);
			}
		}
	}
}

void Player::onTargetCreatureGainHealth(Creature* target, int64_t points)
{
	if (target && party) {
		Player* tmpPlayer = nullptr;

		if (isPartner(tmpPlayer) && (tmpPlayer != this)) {
			tmpPlayer = target->getPlayer();
		} else if (Creature* targetMaster = target->getMaster()) {
			if (Player* targetMasterPlayer = targetMaster->getPlayer()) {
				tmpPlayer = targetMasterPlayer;
			}
		}

		if (isPartner(tmpPlayer)) {
			party->updatePlayerTicks(this, points);
		}
	}
}

bool Player::onKilledCreature(Creature* target, bool lastHit/* = true*/)
{
	bool unjustified = false;

	if (hasFlag(PlayerFlag_NotGenerateLoot)) {
		target->setDropLoot(false);
	}

	Creature::onKilledCreature(target, lastHit);

	if (Player* targetPlayer = target->getPlayer()) {
		if (targetPlayer && targetPlayer->getZone() == ZONE_PVP) {
			targetPlayer->setDropLoot(false);
			targetPlayer->setSkillLoss(false);
		} else if (!hasFlag(PlayerFlag_NotGainInFight) && !isPartner(targetPlayer)) {
			if (!Combat::isInPvpZone(this, targetPlayer) && hasAttacked(targetPlayer) && !targetPlayer->hasAttacked(this) && !isGuildMate(targetPlayer) && targetPlayer != this) {
				if (targetPlayer->hasKilled(this)) {
					for (auto& kill : targetPlayer->unjustifiedKills) {
						if (kill.target == getGUID() && kill.unavenged) {
							kill.unavenged = false;
							auto it = attackedSet.find(targetPlayer->guid);
							attackedSet.erase(it);
							break;
						}
					}
				} else if (targetPlayer->getSkull() == SKULL_NONE && !isInWar(targetPlayer)) {
					unjustified = true;
					addUnjustifiedDead(targetPlayer);
				}

				if (lastHit && hasCondition(CONDITION_INFIGHT)) {
					pzLocked = true;
					Condition* condition = Condition::createCondition(CONDITIONID_DEFAULT, CONDITION_INFIGHT, g_config.getNumber(ConfigManager::WHITE_SKULL_TIME), 0);
					addCondition(condition);
				}
			}
		}
	} else if (const Monster* monster = target->getMonster();
		TaskHuntingSlot* taskSlot = getTaskHuntingWithCreature(monster->getRaceId())) {
		if (const TaskHuntingOption* option = g_prey.GetTaskRewardOption(taskSlot)) {
			taskSlot->currentKills += 1;
			if ((taskSlot->upgrade && taskSlot->currentKills >= option->secondKills) ||
				(!taskSlot->upgrade && taskSlot->currentKills >= option->firstKills)) {
				taskSlot->state = PreyTaskDataState_Completed;
				sendTextMessage(MESSAGE_STATUS, "You succesfully finished your hunting task. Your reward is ready to be claimed!");
			}
			reloadTaskSlot(taskSlot->id);
		}	
	}

	return unjustified;
}

void Player::gainExperience(uint64_t gainExp, Creature* source)
{
	if (hasFlag(PlayerFlag_NotGainExperience) || gainExp == 0 || staminaMinutes == 0) {
		return;
	}

	addExperience(source, gainExp, true);
}

void Player::onGainExperience(uint64_t gainExp, Creature* target)
{
	if (hasFlag(PlayerFlag_NotGainExperience)) {
		return;
	}

	if (target && !target->getPlayer() && party && party->isSharedExperienceActive() && party->isSharedExperienceEnabled()) {
		party->shareExperience(gainExp, target);
		//We will get a share of the experience through the sharing mechanism
		return;
	}

	Creature::onGainExperience(gainExp, target);
	gainExperience(gainExp, target);
}

void Player::onGainSharedExperience(uint64_t gainExp, Creature* source)
{
	gainExperience(gainExp, source);
}

bool Player::isImmune(CombatType_t type) const
{
	if (hasFlag(PlayerFlag_CannotBeAttacked)) {
		return true;
	}
	return Creature::isImmune(type);
}

bool Player::isImmune(ConditionType_t type) const
{
	if (hasFlag(PlayerFlag_CannotBeAttacked)) {
		return true;
	}
	return Creature::isImmune(type);
}

bool Player::isAttackable() const
{
	return !hasFlag(PlayerFlag_CannotBeAttacked);
}

bool Player::lastHitIsPlayer(Creature* lastHitCreature)
{
	if (!lastHitCreature) {
		return false;
	}

	if (lastHitCreature->getPlayer()) {
		return true;
	}

	Creature* lastHitMaster = lastHitCreature->getMaster();
	return lastHitMaster && lastHitMaster->getPlayer();
}

void Player::changeHealth(int64_t healthChange, bool sendHealthChange/* = true*/)
{
	if (PLAYER_SOUND_HEALTH_CHANGE >= static_cast<uint32_t>(uniform_random(1, 100))) {
		g_game.sendSingleSoundEffect(this->getPosition(), sex == PLAYERSEX_FEMALE ? SOUND_EFFECT_TYPE_HUMAN_FEMALE_BARK : SOUND_EFFECT_TYPE_HUMAN_MALE_BARK, this);
	}

	Creature::changeHealth(healthChange, sendHealthChange);
	sendStats();
}

void Player::changeMana(int64_t manaChange)
{
	if (!hasFlag(PlayerFlag_HasInfiniteMana)) {
		Creature::changeMana(manaChange);
	}
	g_game.addPlayerMana(this);
	sendStats();
}

void Player::changeSoul(int32_t soulChange)
{
	if (soulChange > 0) {
		soul += std::min<int32_t>(soulChange, vocation->getSoulMax() - soul);
	} else {
		soul = std::max<int32_t>(0, soul + soulChange);
	}

	sendStats();
}

bool Player::canWear(uint32_t lookType, uint8_t addons) const
{
	if (group->access) {
		return true;
	}

	const Outfit* outfit = Outfits::getInstance().getOutfitByLookType(sex, lookType);
	if (!outfit) {
		return false;
	}

	if (outfit->premium && !isPremium()) {
		return false;
	}

	if (outfit->unlocked && addons == 0) {
		return true;
	}

	for (const OutfitEntry& outfitEntry : outfits) {
		if (outfitEntry.lookType != lookType) {
			continue;
		}
		return (outfitEntry.addons & addons) == addons;
	}
	return false;
}

bool Player::canLogout()
{
	if (isConnecting) {
		return false;
	}

	if (getTile()->hasFlag(TILESTATE_NOLOGOUT)) {
		return false;
	}

	if (getTile()->hasFlag(TILESTATE_PROTECTIONZONE)) {
		return true;
	}

	return !isPzLocked() && !hasCondition(CONDITION_INFIGHT);
}

void Player::genReservedStorageRange()
{
	// generate outfits range
	uint32_t outfits_key = PSTRG_OUTFITS_RANGE_START;
	for (const OutfitEntry& entry : outfits) {
		storageMap[++outfits_key] = (entry.lookType << 16) | entry.addons;
	}
	// generate familiars range
	uint32_t familiar_key = PSTRG_FAMILIARS_RANGE_START;
	for (const FamiliarEntry& entry : familiars) {
		storageMap[++familiar_key] = (entry.lookType << 16);
	}
}

void Player::addOutfit(uint16_t lookType, uint8_t addons)
{
	for (OutfitEntry& outfitEntry : outfits) {
		if (outfitEntry.lookType == lookType) {
			outfitEntry.addons |= addons;
			return;
		}
	}
	outfits.emplace_back(lookType, addons);
}

bool Player::removeOutfit(uint16_t lookType)
{
	for (auto it = outfits.begin(), end = outfits.end(); it != end; ++it) {
		OutfitEntry& entry = *it;
		if (entry.lookType == lookType) {
			outfits.erase(it);
			return true;
		}
	}
	return false;
}

bool Player::removeOutfitAddon(uint16_t lookType, uint8_t addons)
{
	for (OutfitEntry& outfitEntry : outfits) {
		if (outfitEntry.lookType == lookType) {
			outfitEntry.addons &= ~addons;
			return true;
		}
	}
	return false;
}

bool Player::getOutfitAddons(const Outfit& outfit, uint8_t& addons) const
{
	if (group->access) {
		addons = 3;
		return true;
	}

	if (outfit.premium && !isPremium()) {
		return false;
	}

	for (const OutfitEntry& outfitEntry : outfits) {
		if (outfitEntry.lookType != outfit.lookType) {
			continue;
		}

		addons = outfitEntry.addons;
		return true;
	}

	if (!outfit.unlocked) {
		return false;
	}

	addons = 0;
	return true;
}

bool Player::canFamiliar(uint32_t lookType) const {
	if (group->access) {
		return true;
	}

	const Familiar* familiar = Familiars::getInstance().getFamiliarByLookType(getVocationId(), lookType);
	if (!familiar) {
		return false;
	}

	if (familiar->premium && !isPremium()) {
		return false;
	}

	if (familiar->unlocked) {
		return true;
	}

	for (const FamiliarEntry& familiarEntry : familiars) {
		if (familiarEntry.lookType != lookType) {
			continue;
		}
	}
	return false;
}

void Player::addFamiliar(uint16_t lookType) {
	for (FamiliarEntry& familiarEntry : familiars) {
		if (familiarEntry.lookType == lookType) {
			return;
		}
	}
	familiars.emplace_back(lookType);
}

bool Player::removeFamiliar(uint16_t lookType) {
	for (auto it = familiars.begin(), end = familiars.end(); it != end; ++it) {
		FamiliarEntry& entry = *it;
		if (entry.lookType == lookType) {
			familiars.erase(it);
			return true;
		}
	}
	return false;
}

bool Player::getFamiliar(const Familiar& familiar) const {
	if (group->access) {
		return true;
	}

	if (familiar.premium && !isPremium()) {
		return false;
	}

	for (const FamiliarEntry& familiarEntry : familiars) {
		if (familiarEntry.lookType != familiar.lookType) {
			continue;
		}

		return true;
	}

	if (!familiar.unlocked) {
		return false;
	}

	return true;
}

void Player::setSex(PlayerSex_t newSex)
{
	sex = newSex;
}

Skulls_t Player::getSkull() const
{
	if (hasFlag(PlayerFlag_NotGainInFight)) {
		return SKULL_NONE;
	}
	return skull;
}

Skulls_t Player::getSkullClient(const Creature* creature) const
{
	if (!creature || g_game.getWorldType() != WORLD_TYPE_PVP) {
		return SKULL_NONE;
	}

	const Player* player = creature->getPlayer();
	if (player && player->getSkull() == SKULL_NONE) {
		if (player == this) {
			for (const auto& kill : unjustifiedKills) {
				if (kill.unavenged && (time(nullptr) - kill.time) < g_config.getNumber(ConfigManager::ORANGE_SKULL_DURATION) * 24 * 60 * 60) {
					return SKULL_ORANGE;
				}
			}
		}

		if (player->hasKilled(this)) {
			return SKULL_ORANGE;
		}

		if (player->hasAttacked(this)) {
			return SKULL_YELLOW;
		}

		if (isPartner(player)) {
			return SKULL_GREEN;
		}
	}
	return Creature::getSkullClient(creature);
}

bool Player::hasKilled(const Player* player) const
{
	for (const auto& kill : unjustifiedKills) {
		if (kill.target == player->getGUID() && (time(nullptr) - kill.time) < g_config.getNumber(ConfigManager::ORANGE_SKULL_DURATION) * 24 * 60 * 60 && kill.unavenged) {
			return true;
		}
	}

	return false;
}

bool Player::hasAttacked(const Player* attacked) const
{
	if (hasFlag(PlayerFlag_NotGainInFight) || !attacked) {
		return false;
	}

	return attackedSet.find(attacked->guid) != attackedSet.end();
}

void Player::addAttacked(const Player* attacked)
{
	if (hasFlag(PlayerFlag_NotGainInFight) || !attacked || attacked == this) {
		return;
	}

	attackedSet.insert(attacked->guid);
}

void Player::removeAttacked(const Player* attacked)
{
	if (!attacked || attacked == this) {
		return;
	}

	auto it = attackedSet.find(attacked->guid);
	if (it != attackedSet.end()) {
		attackedSet.erase(it);
	}
}

void Player::clearAttacked()
{
	attackedSet.clear();
}

void Player::addUnjustifiedDead(const Player* attacked)
{
	if (hasFlag(PlayerFlag_NotGainInFight) || attacked == this || g_game.getWorldType() == WORLD_TYPE_PVP_ENFORCED) {
		return;
	}

	sendTextMessage(MESSAGE_EVENT_ADVANCE, "Warning! The murder of " + attacked->getName() + " was not justified.");

	unjustifiedKills.emplace_back(attacked->getGUID(), time(nullptr), true);

	uint8_t dayKills = 0;
	uint8_t weekKills = 0;
	uint8_t monthKills = 0;

	for (const auto& kill : unjustifiedKills) {
		const auto diff = time(nullptr) - kill.time;
		if (diff <= 4 * 60 * 60) {
			dayKills += 1;
		}
		if (diff <= 7 * 24 * 60 * 60) {
			weekKills += 1;
		}
		if (diff <= 30 * 24 * 60 * 60) {
			monthKills += 1;
		}
	}

	if (getSkull() != SKULL_BLACK) {
		if (dayKills >= 2 * g_config.getNumber(ConfigManager::DAY_KILLS_TO_RED) || weekKills >= 2 * g_config.getNumber(ConfigManager::WEEK_KILLS_TO_RED) || monthKills >= 2 * g_config.getNumber(ConfigManager::MONTH_KILLS_TO_RED)) {
			setSkull(SKULL_BLACK);
			//start black skull time
			skullTicks = static_cast<int64_t>(g_config.getNumber(ConfigManager::BLACK_SKULL_DURATION)) * 24 * 60 * 60 * 1000;
		} else if (dayKills >= g_config.getNumber(ConfigManager::DAY_KILLS_TO_RED) || weekKills >= g_config.getNumber(ConfigManager::WEEK_KILLS_TO_RED) || monthKills >= g_config.getNumber(ConfigManager::MONTH_KILLS_TO_RED)) {
			setSkull(SKULL_RED);
			//reset red skull time
			skullTicks = static_cast<int64_t>(g_config.getNumber(ConfigManager::RED_SKULL_DURATION)) * 24 * 60 * 60 * 1000;
		}
	}

	sendUnjustifiedPoints();
}

void Player::checkSkullTicks(int64_t ticks)
{
	int64_t newTicks = skullTicks - ticks;
	if (newTicks < 0) {
		skullTicks = 0;
	} else {
		skullTicks = newTicks;
	}

	if ((skull == SKULL_RED || skull == SKULL_BLACK) && skullTicks < 1 && !hasCondition(CONDITION_INFIGHT)) {
		setSkull(SKULL_NONE);
	}
}

bool Player::isPromoted() const
{
	uint16_t promotedVocation = g_vocations.getPromotedVocation(vocation->getId());
	return promotedVocation == VOCATION_NONE && vocation->getId() != promotedVocation;
}

double Player::getLostPercent() const
{
	int32_t blessingCount = 0;
	uint8_t maxBlessing = (operatingSystem == CLIENTOS_NEW_WINDOWS || operatingSystem == CLIENTOS_NEW_MAC) ? 8 : 6;
	for (int i = 2; i <= maxBlessing; i++) {
		if (hasBlessing(i)) {
			blessingCount++;
		}
	}

	int32_t deathLosePercent = g_config.getNumber(ConfigManager::DEATH_LOSE_PERCENT);
	if (deathLosePercent != -1) {
		if (isPromoted()) {
			deathLosePercent -= 3;
		}

		deathLosePercent -= blessingCount;
		return std::max<int32_t>(0, deathLosePercent) / 100.;
	}

	double lossPercent;
	if (level >= 24) {
		double tmpLevel = level + (levelPercent / 100.);
		lossPercent = ((tmpLevel + 50) * 50 * ((tmpLevel * tmpLevel) - (5 * tmpLevel) + 8)) / experience;
	} else {
		lossPercent = 5;
	}

	double percentReduction = 0;
	if (isPromoted()) {
		percentReduction += 30;
	}

	percentReduction += blessingCount * 8;
	return lossPercent * (1 - (percentReduction / 100.)) / 100.;
}

void Player::learnInstantSpell(const std::string& spellName)
{
	if (!hasLearnedInstantSpell(spellName)) {
		learnedInstantSpellList.push_front(spellName);
	}
}

void Player::forgetInstantSpell(const std::string& spellName)
{
	learnedInstantSpellList.remove(spellName);
}

bool Player::hasLearnedInstantSpell(const std::string& spellName) const
{
	if (hasFlag(PlayerFlag_CannotUseSpells)) {
		return false;
	}

	if (hasFlag(PlayerFlag_IgnoreSpellCheck)) {
		return true;
	}

	for (const auto& learnedSpellName : learnedInstantSpellList) {
		if (strcasecmp(learnedSpellName.c_str(), spellName.c_str()) == 0) {
			return true;
		}
	}
	return false;
}

bool Player::isInWar(const Player* player) const
{
	if (!player || !guild) {
		return false;
	}

	const Guild* playerGuild = player->getGuild();
	if (!playerGuild) {
		return false;
	}

	return isInWarList(playerGuild->getId()) && player->isInWarList(guild->getId());
}

bool Player::isInWarList(uint32_t guildId) const
{
	return std::find(guildWarVector.begin(), guildWarVector.end(), guildId) != guildWarVector.end();
}

bool Player::isPremium() const
{
	if (g_config.getBoolean(ConfigManager::FREE_PREMIUM) || hasFlag(PlayerFlag_IsAlwaysPremium)) {
		return true;
	}

	return premiumDays > 0;
}

void Player::setPremiumDays(int32_t v)
{
	premiumDays = v;
	sendBasicData();
}

void Player::setTibiaCoins(int32_t v)
{
	coinBalance = v;
}

PartyShields_t Player::getPartyShield(const Player* player) const
{
	if (!player) {
		return SHIELD_NONE;
	}

	if (party) {
		if (party->getLeader() == player) {
			if (party->isSharedExperienceActive()) {
				if (party->isSharedExperienceEnabled()) {
					return SHIELD_YELLOW_SHAREDEXP;
				}

				if (party->canUseSharedExperience(player)) {
					return SHIELD_YELLOW_NOSHAREDEXP;
				}

				return SHIELD_YELLOW_NOSHAREDEXP_BLINK;
			}

			return SHIELD_YELLOW;
		}

		if (player->party == party) {
			if (party->isSharedExperienceActive()) {
				if (party->isSharedExperienceEnabled()) {
					return SHIELD_BLUE_SHAREDEXP;
				}

				if (party->canUseSharedExperience(player)) {
					return SHIELD_BLUE_NOSHAREDEXP;
				}

				return SHIELD_BLUE_NOSHAREDEXP_BLINK;
			}

			return SHIELD_BLUE;
		}

		if (isInviting(player)) {
			return SHIELD_WHITEBLUE;
		}
	}

	if (player->isInviting(this)) {
		return SHIELD_WHITEYELLOW;
	}

	if (player->party) {
		return SHIELD_GRAY;
	}

	return SHIELD_NONE;
}

bool Player::isInviting(const Player* player) const
{
	if (!player || !party || party->getLeader() != this) {
		return false;
	}
	return party->isPlayerInvited(player);
}

bool Player::isPartner(const Player* player) const
{
	if (!player || !party || player == this) {
		return false;
	}
	return party == player->party;
}

bool Player::isGuildMate(const Player* player) const
{
	if (!player || !guild) {
		return false;
	}
	return guild == player->guild;
}

void Player::sendPlayerPartyIcons(Player* player)
{
	sendPartyCreatureShield(player);
	sendPartyCreatureSkull(player);
}

bool Player::addPartyInvitation(Party* newParty)
{
	auto it = std::find(invitePartyList.begin(), invitePartyList.end(), newParty);
	if (it != invitePartyList.end()) {
		return false;
	}

	invitePartyList.push_front(newParty);
	return true;
}

void Player::removePartyInvitation(Party* remParty)
{
	invitePartyList.remove(remParty);
}

void Player::clearPartyInvitations()
{
	for (Party* invitingParty : invitePartyList) {
		invitingParty->removeInvite(*this, false);
	}
	invitePartyList.clear();
}

GuildEmblems_t Player::getGuildEmblem(const Player* player) const
{
	if (!player) {
		return GUILDEMBLEM_NONE;
	}

	const Guild* playerGuild = player->getGuild();
	if (!playerGuild) {
		return GUILDEMBLEM_NONE;
	}

	if (player->getGuildWarVector().empty()) {
		if (guild == playerGuild) {
			return GUILDEMBLEM_MEMBER;
		} else {
			return GUILDEMBLEM_OTHER;
		}
	} else if (guild == playerGuild) {
		return GUILDEMBLEM_ALLY;
	} else if (isInWar(player)) {
		return GUILDEMBLEM_ENEMY;
	}

	return GUILDEMBLEM_NEUTRAL;
}

void Player::sendUnjustifiedPoints()
{
	if (client) {
		double dayKills = 0;
		double weekKills = 0;
		double monthKills = 0;

		for (const auto& kill : unjustifiedKills) {
			const auto diff = time(nullptr) - kill.time;
			if (diff <= 24 * 60 * 60) {
				dayKills += 1;
			}
			if (diff <= 7 * 24 * 60 * 60) {
				weekKills += 1;
			}
			if (diff <= 30 * 24 * 60 * 60) {
				monthKills += 1;
			}
		}

		bool isRed = getSkull() == SKULL_RED;

		auto dayMax = ((isRed ? 2 : 1) * g_config.getNumber(ConfigManager::DAY_KILLS_TO_RED));
		auto weekMax = ((isRed ? 2 : 1) * g_config.getNumber(ConfigManager::WEEK_KILLS_TO_RED));
		auto monthMax = ((isRed ? 2 : 1) * g_config.getNumber(ConfigManager::MONTH_KILLS_TO_RED));

		uint8_t dayProgress = std::min(std::round(dayKills / dayMax * 100), 100.0);
		uint8_t weekProgress = std::min(std::round(weekKills / weekMax * 100), 100.0);
		uint8_t monthProgress = std::min(std::round(monthKills / monthMax * 100), 100.0);
		uint8_t skullDuration = 0;
		if (skullTicks != 0) {
			skullDuration = std::floor<uint8_t>(skullTicks / (24 * 60 * 60 * 1000));
		}
		client->sendUnjustifiedPoints(dayProgress, std::max(dayMax - dayKills, 0.0), weekProgress, std::max(weekMax - weekKills, 0.0), monthProgress, std::max(monthMax - monthKills, 0.0), skullDuration);
	}
}

uint8_t Player::getCurrentMount() const
{
	int32_t value;
	if (getStorageValue(PSTRG_MOUNTS_CURRENTMOUNT, value)) {
		return value;
	}
	return 0;
}

void Player::setCurrentMount(uint8_t mount)
{
	addStorageValue(PSTRG_MOUNTS_CURRENTMOUNT, mount);
}

bool Player::toggleMount(bool mount)
{
	if ((OTSYS_TIME() - lastToggleMount) < 3000 && !wasMounted) {
		sendCancelMessage(RETURNVALUE_YOUAREEXHAUSTED);
		return false;
	}

	if (mount) {
		if (isMounted()) {
			return false;
		}

		if (!group->access && tile->hasFlag(TILESTATE_PROTECTIONZONE)) {
			sendCancelMessage(RETURNVALUE_ACTIONNOTPERMITTEDINPROTECTIONZONE);
			return false;
		}

		const Outfit* playerOutfit = Outfits::getInstance().getOutfitByLookType(getSex(), defaultOutfit.lookType);
		if (!playerOutfit) {
			return false;
		}

		uint8_t currentMountId = getCurrentMount();
		if (currentMountId == 0) {
			sendOutfitWindow();
			return false;
		}

		Mount* currentMount = g_game.mounts.getMountByID(currentMountId);
		if (!currentMount) {
			return false;
		}

		if (!hasMount(currentMount)) {
			setCurrentMount(0);
			sendOutfitWindow();
			return false;
		}

		if (currentMount->premium && !isPremium()) {
			sendCancelMessage(RETURNVALUE_YOUNEEDPREMIUMACCOUNT);
			return false;
		}

		if (hasCondition(CONDITION_OUTFIT)) {
			sendCancelMessage(RETURNVALUE_NOTPOSSIBLE);
			return false;
		}

		defaultOutfit.lookMount = currentMount->clientId;

		if (currentMount->speed != 0) {
			g_game.changeSpeed(this, currentMount->speed);
		}
	} else {
		if (!isMounted()) {
			return false;
		}

		dismount();
	}

	g_game.internalCreatureChangeOutfit(this, defaultOutfit);
	lastToggleMount = OTSYS_TIME();
	return true;
}

bool Player::tameMount(uint8_t mountId)
{
	if (!g_game.mounts.getMountByID(mountId)) {
		return false;
	}

	const uint8_t tmpMountId = mountId - 1;
	const uint32_t key = PSTRG_MOUNTS_RANGE_START + (tmpMountId / 31);

	int32_t value;
	if (getStorageValue(key, value)) {
		value |= (1 << (tmpMountId % 31));
	} else {
		value = (1 << (tmpMountId % 31));
	}

	addStorageValue(key, value);
	return true;
}

bool Player::untameMount(uint8_t mountId)
{
	if (!g_game.mounts.getMountByID(mountId)) {
		return false;
	}

	const uint8_t tmpMountId = mountId - 1;
	const uint32_t key = PSTRG_MOUNTS_RANGE_START + (tmpMountId / 31);

	int32_t value;
	if (!getStorageValue(key, value)) {
		return true;
	}

	value &= ~(1 << (tmpMountId % 31));
	addStorageValue(key, value);

	if (getCurrentMount() == mountId) {
		if (isMounted()) {
			dismount();
			g_game.internalCreatureChangeOutfit(this, defaultOutfit);
		}

		setCurrentMount(0);
	}

	return true;
}

bool Player::hasMount(const Mount* mount) const
{
	if (isAccessPlayer()) {
		return true;
	}

	if (mount->premium && !isPremium()) {
		return false;
	}

	const uint8_t tmpMountId = mount->id - 1;

	int32_t value;
	if (!getStorageValue(PSTRG_MOUNTS_RANGE_START + (tmpMountId / 31), value)) {
		return false;
	}

	return ((1 << (tmpMountId % 31)) & value) != 0;
}

void Player::dismount()
{
	Mount* mount = g_game.mounts.getMountByID(getCurrentMount());
	if (mount && mount->speed > 0) {
		g_game.changeSpeed(this, -mount->speed);
	}

	defaultOutfit.lookMount = 0;
}

bool Player::addOfflineTrainingTries(skills_t skill, uint64_t tries)
{
	if (tries == 0 || skill == SKILL_LEVEL) {
		return false;
	}

	bool sendUpdate = false;
	uint32_t oldSkillValue, newSkillValue;
	long double oldPercentToNextLevel, newPercentToNextLevel;

	if (skill == SKILL_MAGLEVEL) {
		uint64_t currReqMana = vocation->getReqMana(magLevel);
		uint64_t nextReqMana = vocation->getReqMana(magLevel + 1);

		if (currReqMana >= nextReqMana) {
			return false;
		}

		oldSkillValue = magLevel;
		oldPercentToNextLevel = static_cast<long double>(manaSpent * 100) / nextReqMana;

		g_events->eventPlayerOnGainSkillTries(this, SKILL_MAGLEVEL, tries);
		uint32_t currMagLevel = magLevel;

		while ((manaSpent + tries) >= nextReqMana) {
			tries -= nextReqMana - manaSpent;

			magLevel++;
			manaSpent = 0;

			g_creatureEvents->playerAdvance(this, SKILL_MAGLEVEL, magLevel - 1, magLevel);

			sendUpdate = true;
			currReqMana = nextReqMana;
			nextReqMana = vocation->getReqMana(magLevel + 1);

			if (currReqMana >= nextReqMana) {
				tries = 0;
				break;
			}
		}

		manaSpent += tries;

		if (magLevel != currMagLevel) {
			std::ostringstream ss;
			ss << "You advanced to magic level " << magLevel << '.';
			sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
		}

		uint8_t newPercent;
		if (nextReqMana > currReqMana) {
			newPercent = Player::getPercentLevel(manaSpent, nextReqMana);
			newPercentToNextLevel = static_cast<long double>(manaSpent * 100) / nextReqMana;
		} else {
			newPercent = 0;
			newPercentToNextLevel = 0;
		}

		if (newPercent != magLevelPercent) {
			magLevelPercent = newPercent;
			sendUpdate = true;
		}

		newSkillValue = magLevel;
	} else {
		uint64_t currReqTries = vocation->getReqSkillTries(skill, skills[skill].level);
		uint64_t nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);
		if (currReqTries >= nextReqTries) {
			return false;
		}

		oldSkillValue = skills[skill].level;
		oldPercentToNextLevel = static_cast<long double>(skills[skill].tries * 100) / nextReqTries;

		g_events->eventPlayerOnGainSkillTries(this, skill, tries);
		uint32_t currSkillLevel = skills[skill].level;

		while ((skills[skill].tries + tries) >= nextReqTries) {
			tries -= nextReqTries - skills[skill].tries;

			skills[skill].level++;
			skills[skill].tries = 0;
			skills[skill].percent = 0;

			g_creatureEvents->playerAdvance(this, skill, (skills[skill].level - 1), skills[skill].level);

			sendUpdate = true;
			currReqTries = nextReqTries;
			nextReqTries = vocation->getReqSkillTries(skill, skills[skill].level + 1);

			if (currReqTries >= nextReqTries) {
				tries = 0;
				break;
			}
		}

		skills[skill].tries += tries;

		if (currSkillLevel != skills[skill].level) {
			std::ostringstream ss;
			ss << "You advanced to " << getSkillName(skill) << " level " << skills[skill].level << '.';
			sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
		}

		uint8_t newPercent;
		if (nextReqTries > currReqTries) {
			newPercent = Player::getPercentLevel(skills[skill].tries, nextReqTries);
			newPercentToNextLevel = static_cast<long double>(skills[skill].tries * 100) / nextReqTries;
		} else {
			newPercent = 0;
			newPercentToNextLevel = 0;
		}

		if (skills[skill].percent != newPercent) {
			skills[skill].percent = newPercent;
			sendUpdate = true;
		}

		newSkillValue = skills[skill].level;
	}

	if (sendUpdate) {
		sendSkills();
		sendStats();
	}

	std::ostringstream ss;
	ss << std::fixed << std::setprecision(2) << "Your " << ucwords(getSkillName(skill)) << " skill changed from level " << oldSkillValue << " (with " << oldPercentToNextLevel << "% progress towards level " << (oldSkillValue + 1) << ") to level " << newSkillValue << " (with " << newPercentToNextLevel << "% progress towards level " << (newSkillValue + 1) << ')';
	sendTextMessage(MESSAGE_EVENT_ADVANCE, ss.str());
	return sendUpdate;
}

bool Player::hasModalWindowOpen(uint32_t modalWindowId) const
{
	return find(modalWindows.begin(), modalWindows.end(), modalWindowId) != modalWindows.end();
}

void Player::onModalWindowHandled(uint32_t modalWindowId)
{
	modalWindows.remove(modalWindowId);
}

void Player::sendModalWindow(const ModalWindow& modalWindow)
{
	if (!client) {
		return;
	}

	modalWindows.push_front(modalWindow.id);
	client->sendModalWindow(modalWindow);
}

void Player::clearModalWindows()
{
	modalWindows.clear();
}

uint16_t Player::getHelpers() const
{
	uint16_t helpers;

	if (guild && party) {
		std::unordered_set<Player*> helperSet;

		const auto& guildMembers = guild->getMembersOnline();
		helperSet.insert(guildMembers.begin(), guildMembers.end());

		const auto& partyMembers = party->getMembers();
		helperSet.insert(partyMembers.begin(), partyMembers.end());

		const auto& partyInvitees = party->getInvitees();
		helperSet.insert(partyInvitees.begin(), partyInvitees.end());

		helperSet.insert(party->getLeader());

		helpers = helperSet.size();
	} else if (guild) {
		helpers = guild->getMembersOnline().size();
	} else if (party) {
		helpers = party->getMemberCount() + party->getInvitationCount() + 1;
	} else {
		helpers = 0;
	}

	return helpers;
}

void Player::sendClosePrivate(uint16_t channelId)
{
	if (channelId == CHANNEL_GUILD || channelId == CHANNEL_PARTY) {
		g_chat->removeUserFromChannel(*this, channelId);
	}

	if (client) {
		client->sendClosePrivate(channelId);
	}
}

uint64_t Player::getMoney() const
{
	std::vector<const Container*> containers;
	uint64_t moneyCount = 0;

	for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; ++i) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}

		const Container* container = item->getContainer();
		if (container) {
			containers.push_back(container);
		} else {
			moneyCount += item->getWorth();
		}
	}

	size_t i = 0;
	while (i < containers.size()) {
		const Container* container = containers[i++];
		for (const Item* item : container->getItemList()) {
			const Container* tmpContainer = item->getContainer();
			if (tmpContainer) {
				containers.push_back(tmpContainer);
			} else {
				moneyCount += item->getWorth();
			}
		}
	}
	return moneyCount;
}

size_t Player::getMaxVIPEntries() const
{
	if (group->maxVipEntries != 0) {
		return group->maxVipEntries;
	} else if (isPremium()) {
		return 100;
	}
	return 20;
}

size_t Player::getMaxDepotItems() const
{
	if (group->maxDepotItems != 0) {
		return group->maxDepotItems;
	} else if (isPremium()) {
		return g_config.getNumber(ConfigManager::PREMIUM_DEPOT_LIMIT);
	}
	return g_config.getNumber(ConfigManager::FREE_DEPOT_LIMIT);
}

std::forward_list<Condition*> Player::getMuteConditions() const
{
	std::forward_list<Condition*> muteConditions;
	for (Condition* condition : conditions) {
		if (condition->getTicks() <= 0) {
			continue;
		}

		ConditionType_t type = condition->getType();
		if (type != CONDITION_MUTED && type != CONDITION_CHANNELMUTEDTICKS && type != CONDITION_YELLTICKS) {
			continue;
		}

		muteConditions.push_front(condition);
	}
	return muteConditions;
}

void Player::setGuild(Guild* newGuild)
{
	if (newGuild == this->guild) {
		return;
	}

	Guild* oldGuild = this->guild;

	this->guildNick.clear();
	this->guild = nullptr;
	this->guildRank = nullptr;

	if (newGuild) {
		GuildRank_ptr rank = newGuild->getRankByLevel(1);
		if (!rank) {
			return;
		}

		this->guild = newGuild;
		this->guildRank = rank;
		newGuild->addMember(this);
	}

	if (oldGuild) {
		oldGuild->removeMember(this);
	}
}

//Autoloot
void Player::addAutoLootItem(uint16_t itemId, uint16_t bpId)
{
	autoLootMap[itemId] = bpId;
}

void Player::removeAutoLootItem(uint16_t itemId)
{
	autoLootMap.erase(itemId);
}

int32_t Player::getAutoLootItem(const uint16_t itemId)
{
	auto it = autoLootMap.find(itemId);
	if (it != autoLootMap.end()) {
		return it->second;
	}
	return -1;
}

//Custom: Anti bug of market
bool Player::isMarketExhausted() const {
	uint32_t exhaust_time = 3000; // half second 500
	return (OTSYS_TIME() - lastMarketInteraction < exhaust_time);
}

// Player talk with npc exhausted
bool Player::isNpcExhausted() const {
	// One second = 1000
	uint32_t exhaustionTime = 500;
	return (OTSYS_TIME() - lastNpcInteraction < exhaustionTime);
}

void Player::updateNpcExhausted() {
	lastNpcInteraction = OTSYS_TIME();
}

uint16_t Player::getFreeBackpackSlots() const
{
	Thing* thing = getThing(CONST_SLOT_BACKPACK);
	if (!thing) {
		return 0;
	}

	Container* backpack = thing->getContainer();
	if (!backpack) {
		return 0;
	}

	uint16_t counter = std::max<uint16_t>(0, backpack->getFreeSlots());

	return counter;
}

void Player::onEquipImbueItem(Imbuement* imbuement)
{
	// check skills
	bool requestUpdate = false;

	for (int32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		if (imbuement->skills[i]) {
			requestUpdate = true;
			setVarSkill(static_cast<skills_t>(i), imbuement->skills[i]);
		}
	}

	if (requestUpdate) {
		sendSkills();
		requestUpdate = false;
	}

	// check magpoint
	for (int32_t s = STAT_FIRST; s <= STAT_LAST; ++s) {
		if (imbuement->stats[s]) {
			requestUpdate = true;
			setVarStats(static_cast<stats_t>(s), imbuement->stats[s]);
		}
	}

	// speed
	if (imbuement->speed != 0) {
		g_game.changeSpeed(this, imbuement->speed);
	}

	// capacity
	if (imbuement->capacity != 0) {
		requestUpdate = true;
		bonusCapacity = (capacity * imbuement->capacity)/100;
	}

	if (requestUpdate) {
		sendStats();
		sendSkills();
	}

	return;
}

void Player::onDeEquipImbueItem(Imbuement* imbuement)
{
	// check skills
	bool requestUpdate = false;

	for (int32_t i = SKILL_FIRST; i <= SKILL_LAST; ++i) {
		if (imbuement->skills[i]) {
			requestUpdate = true;
			setVarSkill(static_cast<skills_t>(i), -imbuement->skills[i]);
		}
	}

	if (requestUpdate) {
		sendSkills();
		requestUpdate = false;
	}

	// check magpoint
	for (int32_t s = STAT_FIRST; s <= STAT_LAST; ++s) {
		if (imbuement->stats[s]) {
			requestUpdate = true;
			setVarStats(static_cast<stats_t>(s), -imbuement->stats[s]);
		}
	}

	// speed
	if (imbuement->speed != 0) {
		g_game.changeSpeed(this, -imbuement->speed);
	}

	// capacity
	if (imbuement->capacity != 0) {
		requestUpdate = true;
		bonusCapacity = 0;
	}

	if (requestUpdate) {
		sendStats();
		sendSkills();
	}

	return;
}

bool Player::addItemFromStash(uint16_t itemId, uint32_t itemCount) {
	uint32_t stackCount = 100u;

	while (itemCount > 0) {
		auto addValue = itemCount > stackCount ? stackCount : itemCount;
		itemCount -= addValue;
		Item* newItem = Item::CreateItem(itemId, addValue);

		if (g_game.internalQuickLootItem(this, newItem, OBJECTCATEGORY_STASHRETRIEVE) != RETURNVALUE_NOERROR) {
			g_game.internalPlayerAddItem(this, newItem, true);
		}
	}

	sendOpenStash();
	return true;
}

void Player::stowItem(Item* item, uint32_t count, bool allItems) {
	if (!item || !item->isItemStorable()) {
		sendCancelMessage("This item cannot be stowed here.");
		return;
	}

	StashContainerList itemDict;
	if (allItems) {
		for (int32_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
			Item* inventoryItem = inventory[i];
			if (!inventoryItem) {
				continue;
			}

			if (inventoryItem->getClientID() == item->getClientID()) {
				itemDict.push_back(std::pair<Item*, uint32_t>(inventoryItem, inventoryItem->getItemCount()));
			}

			if (Container* container = inventoryItem->getContainer()) {
				for (auto stowable_it : container->getStowableItems()) {
					if ((stowable_it.first)->getClientID() == item->getClientID()) {
						itemDict.push_back(stowable_it);
					}
				}
			}
		}
	} else if (item->getContainer()) {
		itemDict = item->getContainer()->getStowableItems();
	} else {
		itemDict.push_back(std::pair<Item*, uint32_t>(item, count));
	}

	if (itemDict.size() == 0) {
		sendCancelMessage("There is no stowable items on this container.");
		return;
	}

	stashContainer(itemDict);
}

void Player::clearSpells()
{
	std::list<uint16_t> spellList = g_spells->getSpellsByVocation(getVocationId());

	for (auto spellId : spellList) {
		if (Condition* spellCondition = getCondition(CONDITION_SPELLCOOLDOWN, CONDITIONID_DEFAULT, spellId)) {
			removeCondition(spellCondition, true);
			sendSpellCooldown(spellId, 0);

			SpellGroup_t group = g_spells->getInstantSpellById(spellId)->getGroup();
			Condition* groupCondition = getCondition(CONDITION_SPELLGROUPCOOLDOWN, CONDITIONID_DEFAULT, group);
			if (groupCondition) {
				removeCondition(groupCondition, true);
				sendSpellGroupCooldown(group, 0);
			}

			group = g_spells->getInstantSpellById(spellId)->getSecondaryGroup();
			groupCondition = getCondition(CONDITION_SPELLGROUPCOOLDOWN, CONDITIONID_DEFAULT, group);
			if (groupCondition) {
				removeCondition(groupCondition, true);
				sendSpellGroupCooldown(group, 0);
			}
		}
	}

	sendTextMessage(MESSAGE_EVENT_ADVANCE, "Your cooldowns have been cleared!");
}

std::unordered_map<uint16_t, uint32_t> Player::getInventoryItems() const
{
	std::unordered_map<uint16_t, uint32_t> inventory;
	for (std::underlying_type<slots_t>::type slot = CONST_SLOT_FIRST; slot <= CONST_SLOT_AMMO; slot++) {
		Item* inventoryItem = getInventoryItem(static_cast<slots_t>(slot));
		if (inventoryItem) {
			inventory[inventoryItem->getClientID()] += inventoryItem->getItemCount();
			if (Container* container = inventoryItem->getContainer()) {
				for (auto containerItem : container->getItemList()) {
					inventory[containerItem->getClientID()] += containerItem->getItemCount();
				}

				ContainerIterator ite = container->iterator();
				while (ite.hasNext()) {
					Item* cur = *ite;
					Container* subContainer = cur ? cur->getContainer() : nullptr;
					ite.advance();
					if (subContainer) {
						for (auto subContainerItem : subContainer->getItemList()) {
							inventory[subContainerItem->getClientID()] += subContainerItem->getItemCount();
						}
					}
				}
			}
		}
	}

	return inventory;
}

std::unordered_map<uint16_t, uint32_t> Player::getStoreInboxItems() const
{
	std::unordered_map<uint16_t, uint32_t> storeItems;
	Item* thing = getInventoryItem(CONST_SLOT_STORE_INBOX);
	if (Container* storeInbox = thing->getContainer()) {
		for (auto storeItem : storeInbox->getItemList()) {
			storeItems[storeItem->getClientID()] += storeItem->getItemCount();
		}

		ContainerIterator ite = storeInbox->iterator();
		while (ite.hasNext()) {
			Item* cur = *ite;
			Container* subContainer = cur ? cur->getContainer() : nullptr;
			ite.advance();
			if (subContainer) {
				for (auto subContainerItem : subContainer->getItemList()) {
					storeItems[subContainerItem->getClientID()] += subContainerItem->getItemCount();
				}
			}
		}
	}

	return storeItems;
}

std::unordered_map<uint16_t, uint32_t> Player::getDepotItems() const
{
	std::unordered_map<uint16_t, uint32_t> depotItems;
	for (const auto& it : depotChests) {
		DepotChest* depotChest = it.second;
		for (auto item : depotChest->getItemList()) {
			depotItems[item->getClientID()] += item->getItemCount();
		}
	}

	return depotItems;
}

std::unordered_map<uint16_t, uint32_t> Player::getDepotInboxItems() const
{
	std::unordered_map<uint16_t, uint32_t> inboxItems;
	for (auto item : getInbox()->getItemList()) {
		inboxItems[item->getClientID()] += item->getItemCount();
	}

	return inboxItems;
}

void Player::initializePrey()
{
	if (preys.empty()) {
		for (uint8_t slotId = PreySlot_First; slotId <= PreySlot_Last; slotId++) {
			auto slot = new PreySlot(static_cast<PreySlot_t>(slotId));
			if (!g_config.getBoolean(ConfigManager::PREY_ENABLED)) {
				slot->state = PreyDataState_Inactive;
			} else if (slot->id == PreySlot_Three && !g_config.getBoolean(ConfigManager::PREY_FREE_THIRD_SLOT)) {
				slot->state = PreyDataState_Locked;
			} else {
				slot->state = PreyDataState_Selection;
				slot->reloadMonsterGrid(getPreyBlackList(), getLevel());
			}

			if (!setPreySlotClass(slot)) {
				delete slot;
			}
		}
	}
}

void Player::initializeTaskHunting()
{
	if (taskHunting.empty()) {
		for (uint8_t slotId = PreySlot_First; slotId <= PreySlot_Last; slotId++) {
			auto slot = new TaskHuntingSlot(static_cast<PreySlot_t>(slotId));
			if (!g_config.getBoolean(ConfigManager::TASK_HUNTING_ENABLED)) {
				slot->state = PreyTaskDataState_Inactive;
			} else if (slot->id == PreySlot_Three && !g_config.getBoolean(ConfigManager::TASK_HUNTING_FREE_THIRD_SLOT)) {
				slot->state = PreyTaskDataState_Locked;
			} else {
				slot->state = PreyTaskDataState_Selection;
				slot->reloadMonsterGrid(getTaskHuntingBlackList(), getLevel());
			}

			if (!setTaskHuntingSlotClass(slot)) {
				delete slot;
			}
		}
	}

	if (client && g_config.getBoolean(ConfigManager::TASK_HUNTING_ENABLED)) {
		client->writeToOutputBuffer(g_prey.GetTaskHuntingBaseDate());
	}
}

bool Player::isCreatureUnlockedOnTaskHunting(const MonsterType* mtype) const {
	if (!mtype) {
		return false;
	}

	return getBestiaryKillCount(mtype->info.raceid) >= mtype->info.bestiaryToUnlock;
}

Item* Player::getItemTypeByTier(uint16_t itemId, int8_t tier)
{
	Item* item = inventory[CONST_SLOT_BACKPACK];
	if (!item) {
		return nullptr;
	}

	Item* targetItem = nullptr;
	if (Container* container = item->getContainer()) {
		for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
			if ((*it)->getID() == itemId && (*it)->getBoost() == tier) {
				targetItem = (*it);
				break;
			}
		}
	}
	return targetItem;
}

uint32_t Player::getItemTypeCountByTier(uint16_t itemId, int8_t tier, bool equiped /* = false*/) const
{
	uint32_t count = 0;
	for (uint8_t i = CONST_SLOT_FIRST; i <= CONST_SLOT_LAST; i++) {
		Item* item = inventory[i];
		if (!item) {
			continue;
		}


		if (equiped) {
			if (item->getID() == itemId && item->getBoost() == tier) {
				count += Item::countByType(item, -1);
			}
		}

		if (Container* container = item->getContainer()) {
			for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
				if ((*it)->getID() == itemId && (*it)->getBoost() == tier) {
					count += Item::countByType(*it, -1);
				}
			}
		}
	}
	return count;
}

uint32_t Player::getDepotItemCountByTier(uint16_t itemId, int8_t tier)
{
	DepotLocker* depotLocker = getDepotLocker(lastDepotId);
	if (!depotLocker) {
		return 0;
	}

	size_t row = 0;
	uint32_t count = 0;
	std::map<uint16_t, uint16_t> countItem;
	std::vector<Container*> containers{ depotLocker };

	do {
		Container* container = containers[row++];

		for (Item* item : container->getItemList()) {
			Container* c = item->getContainer();
			if (c && !c->empty()) {
				containers.push_back(c);
				continue;
			}

			const ItemType& itemType = Item::items[item->getID()];
			if (itemType.wareId == 0) {
				continue;
			}

			if (c && (!itemType.isContainer() || c->capacity() != itemType.maxItems)) {
				continue;
			}

			if (!item->hasMarketAttributes()) {
				continue;
			}

			if (item->getBoost() == tier && item->getID() == itemId) {
				if (item->isStackable()) {
					count += item->getItemCount();
				} else {
					count++;
				}
			}
		}
	} while (row < containers.size());

	return count;
}

std::multimap<uint16_t, uint8_t> Player::getFusedItems() const
{
	std::multimap<uint16_t, uint8_t> itemList;
	Item* inventoryItem = inventory[CONST_SLOT_BACKPACK];
	if (!inventoryItem) {
		return itemList;
	}

	Container* container = inventoryItem->getContainer();
	if (!container) {
		return itemList;
	}

	for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
		Item* item = (*it);
		if (item && item->getTier() > 0 && item->hasMarketAttributes()) {
			uint8_t itemBoost = item->getBoost();
			uint32_t itemCount = getItemTypeCountByTier(item->getID(), itemBoost);
			if (itemCount >= 2 && g_forge.itemCanBeFused(item->getTier(), item->getBoost())) {
				bool itemFound = false;
				for (auto ite : itemList) {
					if (ite.first == (*it)->getClientID() && ite.second == itemBoost) {
						itemFound = true;
						break;
					}
				}

				if (!itemFound) {
					itemList.emplace((*it)->getClientID(), itemBoost);
				}
			}
		}
	}

	return itemList;
}

std::map<Item*, std::map<uint16_t, uint16_t>> Player::getTransferItems() const
{
	std::map<Item*, std::map<uint16_t, uint16_t>> transferList;
	Item* inventoryItem = inventory[CONST_SLOT_BACKPACK];
	if (!inventoryItem) {
		return transferList;
	}

	Container* container = inventoryItem->getContainer();
	if (!container) {
		return transferList;
	}

	std::vector<Item*> transferItems;
	for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
		Item* item = (*it);
		if (item && item->getTier() > 0 && item->getBoost() >= 2 && item->hasMarketAttributes()) {
			bool itemFound = false;
			for (auto ite : transferList) {
				Item* itemCheck = ite.first;
				if (itemCheck->getBoost() == item->getBoost()) {
					itemFound = true;
					break;
				}
			}

			if (!itemFound) {
				transferItems.emplace_back(item);
			}
		}
	}

	for (auto tItem : transferItems) {
		std::map<uint16_t, uint16_t> list;
		for (ContainerIterator it = container->iterator(); it.hasNext(); it.advance()) {
			Item* item = (*it);
			if (tItem->getTier() == item->getTier()) {
				if (item->hasMarketAttributes() && item->getBoost() < 1) {
					list[item->getClientID()] = getItemTypeCountByTier(item->getID(), 0);
				}
			}
		}
		transferList[tItem] = list;
	}

	return transferList;
}

void Player::reduceSpellCooldown(int32_t seconds)
{
	std::list<uint16_t> spellList = g_spells->getSpellsByVocation(getVocationId());

	bool isReduced = false;
	for (auto spellId : spellList) {
		if (Condition* spellCondition = getCondition(CONDITION_SPELLCOOLDOWN, CONDITIONID_DEFAULT, spellId)) {
			if (spellCondition->getTicks() >= seconds) {
				spellCondition->setTicks(spellCondition->getTicks() - seconds);
				sendSpellCooldown(spellId, spellCondition->getTicks());

				isReduced = true;
			}
		}
	}

	if (isReduced) {
		g_game.addMagicEffect(getPosition(), CONST_ME_MOMENTUM);
		std::ostringstream text;
		text << "Your cooldown's  are reduced by " << (seconds / 1000) << " seconds. (Momentum)";

		TextMessage message;
		message.position = getPosition();
		message.type = MESSAGE_DAMAGE_DEALT;
		message.text = std::move(text.str());
		sendTextMessage(message);
	}
}

void Player::setHotkeyItemMap(std::multimap<uint16_t, uint8_t>& items)
{
	if (hotkeyItems.size() == 0 && items.size() == 0) {
		return;
	}

	hotkeyItems.clear();
	hotkeyItems = items;

	sendHotkeyItemMap();
}

void Player::sendInvetoryItems()
{
	std::multimap<uint16_t, uint8_t> tempInventoryMap;
	getAllItemType(tempInventoryMap);
	sendItems(tempInventoryMap);
}

SoundEffect_t Player::getHitSoundEffect()
{
	// Distance sound effects
	if (Item* tool = getWeapon()) {
		const ItemType& it = Item::items[tool->getID()];
		if (it.weaponType == WEAPON_AMMO) {
			if (it.ammoType == AMMO_BOLT) {
				return SOUND_EFFECT_TYPE_DIST_ATK_CROSSBOW_SHOT;
			}
			else if (it.ammoType == AMMO_ARROW) {
				if (it.shootType == CONST_ANI_BURSTARROW) {
					return SOUND_EFFECT_TYPE_BURST_ARROW_EFFECT;
				}
				else if (it.shootType == CONST_ANI_DIAMONDARROW) {
					return SOUND_EFFECT_TYPE_DIAMOND_ARROW_EFFECT;
				}
			}
			else {
				return SOUND_EFFECT_TYPE_DIST_ATK_THROW_SHOT;
			}
		}
		else if (it.weaponType == WEAPON_DISTANCE) {
			if (tool->getAmmoType() == AMMO_BOLT) {
				return SOUND_EFFECT_TYPE_DIST_ATK_CROSSBOW_SHOT;
			}
			else if (tool->getAmmoType() == AMMO_ARROW) {
				return SOUND_EFFECT_TYPE_DIST_ATK_BOW_SHOT;
			}
			else {
				return SOUND_EFFECT_TYPE_DIST_ATK_THROW_SHOT;
			}
		}
		else if (it.weaponType == WEAPON_WAND) {
			// Separate between wand and rod here
			//return SOUND_EFFECT_TYPE_DIST_ATK_ROD_SHOT;
			return SOUND_EFFECT_TYPE_DIST_ATK_WAND_SHOT;
		}
	}

	return SOUND_EFFECT_TYPE_SILENCE;
}

SoundEffect_t Player::getAttackSoundEffect()
{
	Item* tool = getWeapon();
	if (!tool) {
		return SOUND_EFFECT_TYPE_HUMAN_CLOSE_ATK_FIST;
	}

	const ItemType& it = Item::items[tool->getID()];
	if (it.weaponType == WEAPON_NONE || it.weaponType == WEAPON_SHIELD) {
		return SOUND_EFFECT_TYPE_HUMAN_CLOSE_ATK_FIST;
	}

	switch (it.weaponType) {
	case WEAPON_AXE: {
		return SOUND_EFFECT_TYPE_MELEE_ATK_AXE;
	}
	case WEAPON_SWORD: {
		return SOUND_EFFECT_TYPE_MELEE_ATK_SWORD;
	}
	case WEAPON_CLUB: {
		return SOUND_EFFECT_TYPE_MELEE_ATK_CLUB;
	}
	case WEAPON_AMMO:
	case WEAPON_DISTANCE: {
		if (tool->getAmmoType() == AMMO_BOLT) {
			return SOUND_EFFECT_TYPE_DIST_ATK_CROSSBOW;
		}
		else if (tool->getAmmoType() == AMMO_ARROW) {
			return SOUND_EFFECT_TYPE_DIST_ATK_BOW;
		}
		else {
			return SOUND_EFFECT_TYPE_DIST_ATK_THROW;
		}

		break;
	}
	case WEAPON_WAND: {
		return SOUND_EFFECT_TYPE_MAGICAL_RANGE_ATK;
	}
	default: {
		return SOUND_EFFECT_TYPE_SILENCE;
	}
	}

	return SOUND_EFFECT_TYPE_SILENCE;
}

// Wheel of destiny
bool Player::checkWheelOfDestinyBattleInstinct()
{
	setWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_BATTLE_INSTINCT, OTSYS_TIME() + 2000);
	bool updateClient = false;
	wheelOfDestinyCreaturesNearby = 0;
	uint16_t creaturesNearby = 0;
	for (int offsetX = -1; offsetX <= 1; offsetX++) {
		if (creaturesNearby >= 8) {
			break;
		}
		for (int offsetY = -1; offsetY <= 1; offsetY++) {
			if (creaturesNearby >= 8) {
				break;
			}
			Tile* tile = g_game.map.getTile(getPosition().x + offsetX, getPosition().y + offsetY, getPosition().z);
			if (!tile) {
				continue;
			}

			const Creature* creature = tile->getTopVisibleCreature(this);
			if (!creature || creature == this || (creature->getMaster() && creature->getMaster()->getPlayer() == this)) {
				continue;
			}

			creaturesNearby++;
		}
	}

	if (creaturesNearby >= 5) {
		wheelOfDestinyCreaturesNearby = creaturesNearby;
		creaturesNearby -= 4;
		uint16_t meleeSkill = 1 * creaturesNearby;
		uint16_t shieldSkill = 6 * creaturesNearby;
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_MELEE) != meleeSkill || getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_SHIELD) != shieldSkill) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_MELEE, meleeSkill);
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_SHIELD, shieldSkill);
			updateClient = true;
		}
	} else if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_MELEE) != 0 || getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_SHIELD) != 0) {
		setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_MELEE, 0);
		setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_SHIELD, 0);
		updateClient = true;
	}

	return updateClient;
}

bool Player::checkWheelOfDestinyPositionalTatics()
{
	setWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_POSITIONAL_TATICS, OTSYS_TIME() + 2000);
	wheelOfDestinyCreaturesNearby = 0;
	bool updateClient = false;
	uint16_t creaturesNearby = 0;
	for (int offsetX = -1; offsetX <= 1; offsetX++) {
		if (creaturesNearby > 0) {
			break;
		}
		for (int offsetY = -1; offsetY <= 1; offsetY++) {
			Tile* tile = g_game.map.getTile(getPosition().x + offsetX, getPosition().y + offsetY, getPosition().z);
			if (!tile) {
				continue;
			}

			const Creature* creature = tile->getTopVisibleCreature(this);
			if (!creature || creature == this || !creature->getMonster() || (creature->getMaster() && creature->getMaster()->getPlayer())) {
				continue;
			}

			creaturesNearby++;
			break;
		}
	}
	uint16_t magicSkill = 3;
	uint16_t distanceSkill = 3;
	if (creaturesNearby == 0) {
		wheelOfDestinyCreaturesNearby = creaturesNearby;
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DISTANCE) != distanceSkill) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DISTANCE, distanceSkill);
			updateClient = true;
		}
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_MAGIC) != 0) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_MAGIC, 0);
			updateClient = true;
		}
	} else {
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DISTANCE) != 0) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DISTANCE, 0);
			updateClient = true;
		}
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_MAGIC) != magicSkill) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_MAGIC, magicSkill);
			updateClient = true;
		}
	}

	return updateClient;
}

bool Player::checkWheelOfDestinyBallisticMastery()
{
	setWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_BALLISTIC_MASTERY, OTSYS_TIME() + 2000);
	bool updateClient = false;
	Item* item = getWeapon();
	uint16_t newCritical = 10;
	uint16_t newHolyBonus = 2; // 2%
	uint16_t newPhysicalBonus = 2; // 2%
	if (item && item->getAmmoType() == AMMO_BOLT) {
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG) != newCritical) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG, newCritical);
			updateClient = true;
		}
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_PHYSICAL_DMG) != 0 || getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_HOLY_DMG) != 0) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_PHYSICAL_DMG, 0);
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_HOLY_DMG, 0);
			updateClient = true;
		}
	} else if (item && item->getAmmoType() == AMMO_ARROW) {
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG) != 0) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG, 0);
			updateClient = true;
		}
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_PHYSICAL_DMG) != newPhysicalBonus || getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_HOLY_DMG) != newHolyBonus) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_PHYSICAL_DMG, newPhysicalBonus);
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_HOLY_DMG, newHolyBonus);
			updateClient = true;
		}
	} else {
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG) != 0) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG, 0);
			updateClient = true;
		}
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_PHYSICAL_DMG) != 0 || getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_HOLY_DMG) != 0) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_PHYSICAL_DMG, 0);
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_HOLY_DMG, 0);
			updateClient = true;
		}
	}

	return updateClient;
}

bool Player::checkWheelOfDestinyCombatMastery()
{
	setWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_COMBAT_MASTERY, OTSYS_TIME() + 2000);
	bool updateClient = false;
	Item* item = getWeapon();
	uint8_t stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_COMBAT_MASTERY);
	if (item && item->getSlotPosition() & SLOTP_TWO_HAND) {
		int32_t criticalSkill = 0;
		if (stage >= 3) {
			criticalSkill = 12;
		} else if (stage >= 2) {
			criticalSkill = 8;
		} else if (stage >= 1) {
			criticalSkill = 4;
		}

		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG_2) != criticalSkill) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG_2, criticalSkill);
			updateClient = true;
		}
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DEFENSE) != 0) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DEFENSE, 0);
			updateClient = true;
		}
	} else {
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG_2) != 0) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_CRITICAL_DMG_2, 0);
			updateClient = true;
		}
		if (getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DEFENSE) == 0) {
			int32_t shieldSkill = 0;
			if (stage >= 3) {
				shieldSkill = 30;
			} else if (stage >= 2) {
				shieldSkill = 20;
			} else if (stage >= 1) {
				shieldSkill = 10;
			}
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DEFENSE, shieldSkill);
			updateClient = true;
		}
	}

	return updateClient;
}

bool Player::checkWheelOfDestinyDivineEmpowerment()
{
	bool updateClient = false;
	setWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_DIVINE_EMPOWERMENT, OTSYS_TIME() + 2000);
	Tile* tile = getTile();
	if (tile && tile->getItemTypeCount(ITEM_DIVINE_EMPOWERMENT_WOD) > 0) {
		int32_t damageBonus = 0;
		uint8_t stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_DIVINE_EMPOWERMENT);
		if (stage >= 3) {
			damageBonus = 12;
		} else if (stage >= 2) {
			damageBonus = 10;
		} else if (stage >= 1) {
			damageBonus = 8;
		}

		if (damageBonus != getWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DAMAGE)) {
			setWheelOfDestinyMajorStat(WHEEL_OF_DESTINY_MAJOR_DAMAGE, damageBonus);
			updateClient = true;
		}
	}

	return updateClient;
}

void Player::checkWheelOfDestinyGiftOfLife()
{
	// Healing
	CombatDamage giftDamage;
	giftDamage.primary.value = (getMaxHealth() * getWheelOfDestinyGiftOfLifeHeal()) / 100;
	giftDamage.primary.type = COMBAT_HEALING;
	sendTextMessage(MESSAGE_EVENT_ADVANCE, "That was close! Fortunately, your were saved by the Gift of Life.");
	g_game.addMagicEffect(getPosition(), CONST_ME_WATER_DROP);
	g_game.combatChangeHealth(this, this, giftDamage);
	// Condition cooldown reduction
	uint16_t reductionTimer = 60000;
	reduceAllSpellsCooldownTimer(reductionTimer);

	// Set cooldown
	setWheelOfDestinyGiftOfCooldown(getWheelOfDestinyGiftOfLifeTotalCooldown(), false);
	sendWheelOfDestinyGiftOfLifeCooldown();
}

int32_t Player::checkWheelOfDestinyBlessingGroveHealingByTarget(Creature* target)
{
	if (!target || target == this) {
		return 0;
	}

	int32_t healingBonus = 0;
	uint8_t stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_BLESSING_OF_THE_GROVE);
	int32_t healthPercent = std::round((static_cast<double>(target->getHealth()) * 100)/static_cast<double>(target->getMaxHealth()));
	if (healthPercent <= 30) {
		if (stage >= 3) {
			healingBonus = 24;
		} else if (stage >= 2) {
			healingBonus = 18;
		} else if (stage >= 1) {
			healingBonus = 12;
		}
	} else if (healthPercent <= 60) {
		if (stage >= 3) {
			healingBonus = 12;
		} else if (stage >= 2) {
			healingBonus = 9;
		} else if (stage >= 1) {
			healingBonus = 6;
		}
	}

	return healingBonus;
}

int32_t Player::checkWheelOfDestinyTwinBurstByTarget(Creature* target)
{
	if (!target || target == this) {
		return 0;
	}

	int32_t damageBonus = 0;
	uint8_t stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_TWIN_BURST);
	int32_t healthPercent = std::round((static_cast<double>(target->getHealth()) * 100)/static_cast<double>(target->getMaxHealth()));
	if (healthPercent > 60) {
		if (stage >= 3) {
			damageBonus = 60;
		} else if (stage >= 2) {
			damageBonus = 40;
		} else if (stage >= 1) {
			damageBonus = 20;
		}
	}

	return damageBonus;
}

int32_t Player::checkWheelOfDestinyExecutionersThrow(Creature* target)
{
	if (!target || target == this) {
		return 0;
	}

	int32_t damageBonus = 0;
	uint8_t stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_EXECUTIONERS_THROW);
	int32_t healthPercent = std::round((static_cast<double>(target->getHealth()) * 100)/static_cast<double>(target->getMaxHealth()));
	if (healthPercent <= 30) {
		if (stage >= 3) {
			damageBonus = 150;
		} else if (stage >= 2) {
			damageBonus = 125;
		} else if (stage >= 1) {
			damageBonus = 100;
		}
	}

	return damageBonus;
}

int32_t Player::checkWheelOfDestinyBeamMasteryDamage()
{
	int32_t damageBoost = 0;
	uint8_t stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_BEAM_MASTERY);
	if (stage >= 3) {
		damageBoost = 14;
	} else if (stage >= 2) {
		damageBoost = 12;
	} else if (stage >= 1) {
		damageBoost = 10;
	}

	return damageBoost;
}

int32_t Player::checkWheelOfDestinyDrainBodyLeech(Creature* target, skills_t skill)
{
	if (!target || !target->getMonster() || !getWheelOfDestinyInstant("Drain Body")) {
		return 0;
	}

	uint8_t stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_DRAIN_BODY);
	if (skill == SKILL_MANA_LEECH_AMOUNT && target->getBuff(BUFF_DAMAGERECEIVED) > 100) {
		int32_t manaLeechSkill = 0;
		if (stage >= 3) {
			manaLeechSkill = 300;
		} else if (stage >= 2) {
			manaLeechSkill = 200;
		} else if (stage >= 1) {
			manaLeechSkill = 100;
		}
		return manaLeechSkill;
	}

	if (skill == SKILL_LIFE_LEECH_AMOUNT && target->getBuff(BUFF_DAMAGEDEALT) < 100) {
		int32_t lifeLeechSkill = 0;
		if (stage >= 3) {
			lifeLeechSkill = 500;
		} else if (stage >= 2) {
			lifeLeechSkill = 400;
		} else if (stage >= 1) {
			lifeLeechSkill = 300;
		}
		return lifeLeechSkill;
	}

	return 0;
}

int32_t Player::checkWheelOfDestinyBattleHealingAmount()
{
	int32_t amount = getSkillLevel(SKILL_SHIELD) * 0.2;
	uint8_t healthPercent = (getHealth() * 100) / getMaxHealth();
	if (healthPercent <= 30) {
		amount *= 3;
	} else if (healthPercent <= 60) {
		amount *= 2;
	}
	return amount;
}

int32_t Player::checkWheelOfDestinyAvatarSkill(WheelOfDestinyAvatarSkill_t skill) const
{
	if (skill == WHEEL_OF_DESTINY_AVATAR_SKILL_NONE || getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_AVATAR) <= OTSYS_TIME()) {
		return 0;
	}

	uint8_t stage = 0;
	if (getWheelOfDestinyInstant("Avatar of Light")) {
		stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_AVATAR_OF_LIGHT);
	} else if (getWheelOfDestinyInstant("Avatar of Steel")) {
		stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_AVATAR_OF_STEEL);
	} else if (getWheelOfDestinyInstant("Avatar of Nature")) {
		stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_AVATAR_OF_NATURE);
	} else if (getWheelOfDestinyInstant("Avatar of Storm")) {
		stage = getWheelOfDestinyStage(WHEEL_OF_DESTINY_STAGE_AVATAR_OF_STORM);
	} else {
		return 0;
	}

	if (skill == WHEEL_OF_DESTINY_AVATAR_SKILL_DAMAGE_REDUCTION) {
		if (stage >= 3) {
			return 15;
		} else if (stage >= 2) {
			return 10;
		} else if (stage >= 1) {
			return 5;
		}
	} else if (skill == WHEEL_OF_DESTINY_AVATAR_SKILL_CRITICAL_CHANCE) {
		return 100;
	} else if (skill == WHEEL_OF_DESTINY_AVATAR_SKILL_CRITICAL_DAMAGE) {
		if (stage >= 3) {
			return 15;
		} else if (stage >= 2) {
			return 10;
		} else if (stage >= 1) {
			return 5;
		}
	}

	return 0;
}

void Player::onThinkWheelOfDestiny(bool force/* = false*/)
{
	bool updateClient = false;
	wheelOfDestinyCreaturesNearby = 0;
	if (!hasCondition(CONDITION_INFIGHT) || getZone() == ZONE_PROTECTION || 
		(!getWheelOfDestinyInstant("Battle Instinct") && !getWheelOfDestinyInstant("Positional Tatics") && !getWheelOfDestinyInstant("Ballistic Mastery") && 
		!getWheelOfDestinyInstant("Gift of Life") && !getWheelOfDestinyInstant("Combat Mastery") && !getWheelOfDestinyInstant("Divine Empowerment") && getWheelOfDestinyGiftOfCooldown() == 0)) {
			bool mustReset = false;
			for (int i = 0; i < static_cast<int>(WHEEL_OF_DESTINY_MAJOR_COUNT); i++) {
				if (getWheelOfDestinyMajorStat(static_cast<WheelOfDestinyMajor_t>(i)) != 0) {
					mustReset = true;
					break;
				}
			}
			
			if (mustReset) {
				for (int i = 0; i < static_cast<int>(WHEEL_OF_DESTINY_MAJOR_COUNT); i++) {
					setWheelOfDestinyMajorStat(static_cast<WheelOfDestinyMajor_t>(i), 0);
				}
				sendSkills();
				sendStats();
				g_game.reloadCreature(this);
			}
		return;
	}
	// Battle Instinct
	if (getWheelOfDestinyInstant("Battle Instinct") && (force || getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_BATTLE_INSTINCT) < OTSYS_TIME())) {
		if (checkWheelOfDestinyBattleInstinct()) {
			updateClient = true;
		}
	}
	// Positional Tatics
	if (getWheelOfDestinyInstant("Positional Tatics") && (force || getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_POSITIONAL_TATICS) < OTSYS_TIME())) {
		if (checkWheelOfDestinyPositionalTatics()) {
			updateClient = true;
		}
	}
	// Ballistic Mastery
	if (getWheelOfDestinyInstant("Ballistic Mastery") && (force || getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_BALLISTIC_MASTERY) < OTSYS_TIME())) {
		if (checkWheelOfDestinyBallisticMastery()) {
			updateClient = true;
		}
	}
	// Gift of life (Cooldown)
	if (getWheelOfDestinyGiftOfCooldown() > 0/*getWheelOfDestinyInstant("Gift of Life")*/ && getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_GIFT_OF_LIFE) <= OTSYS_TIME()) {
		decreaseWheelOfDestinyGiftOfCooldown(1);
		/*updateClient = true;*/
	}
	// Combat Mastery
	if (getWheelOfDestinyInstant("Combat Mastery") && (force || getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_COMBAT_MASTERY) < OTSYS_TIME())) {
		if (checkWheelOfDestinyCombatMastery()) {
			updateClient = true;
		}
	}
	// Divine Empowerment
	if (getWheelOfDestinyInstant("Divine Empowerment") && (force || getWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_DIVINE_EMPOWERMENT) < OTSYS_TIME())) {
		if (checkWheelOfDestinyDivineEmpowerment()) {
			updateClient = true;
		}
	}
	if (updateClient) {
		sendSkills();
		sendStats();
		//g_game.reloadCreature(this);
	}
}

void Player::reduceAllSpellsCooldownTimer(int32_t value)
{
	for (Condition* condition : this->getConditions(CONDITION_SPELLCOOLDOWN)) {
		if (condition->getTicks() <= value) {
			sendSpellCooldown(condition->getSubId(), 0);
			condition->endCondition(this);
		} else {
			condition->setTicks(condition->getTicks() - value);
			sendSpellCooldown(condition->getSubId(), condition->getTicks());
		}
	}
}

Spell* Player::getWheelOfDestinyCombatDataSpell(CombatDamage& damage, Creature* target)
{
	Spell* spell = nullptr;
	damage.damageMultiplier += getWheelOfDestinyMajorStatConditional("Divine Empowerment", WHEEL_OF_DESTINY_MAJOR_DAMAGE);
	WheelOfDestinySpellGrade_t spellGrade = WHEEL_OF_DESTINY_SPELL_GRADE_NONE;
	if (!(damage.instantSpellName).empty()) {
		spellGrade = getWheelOfDestinySpellUpgrade(damage.instantSpellName);
		spell = g_spells->getInstantSpellByName(damage.instantSpellName);
	} else if (!(damage.runeSpellName).empty()) {
		spell = g_spells->getRuneSpellByName(damage.runeSpellName);
	}
	if (spell) {
		damage.damageMultiplier += checkWheelOfDestinyFocusMasteryDamage();
		if (getWheelOfDestinyHealingLinkUpgrade(spell->getName())) {
			damage.healingLink += 10;
		}
		if (spell->getSecondaryGroup() == SPELLGROUP_FOCUS && getWheelOfDestinyInstant("Focus Mastery")) {
			setWheelOfDestinyOnThinkTimer(WHEEL_OF_DESTINY_ONTHINK_FOCUS_MASTERY, (OTSYS_TIME() + 12000));
		}
		if (spell->getWheelOfDestinyUpgraded()) {
			damage.criticalDamage += spell->getWheelOfDestinyBoost(WHEEL_OF_DESTINY_SPELL_BOOST_CRITICAL_DAMAGE, spellGrade);
			damage.criticalChance += spell->getWheelOfDestinyBoost(WHEEL_OF_DESTINY_SPELL_BOOST_CRITICAL_CHANCE, spellGrade);
			damage.damageMultiplier += spell->getWheelOfDestinyBoost(WHEEL_OF_DESTINY_SPELL_BOOST_DAMAGE, spellGrade);
			damage.damageReductionMultiplier += spell->getWheelOfDestinyBoost(WHEEL_OF_DESTINY_SPELL_BOOST_DAMAGE_REDUCTION, spellGrade);
			damage.healingMultiplier += spell->getWheelOfDestinyBoost(WHEEL_OF_DESTINY_SPELL_BOOST_HEAL, spellGrade);
			damage.manaLeech += spell->getWheelOfDestinyBoost(WHEEL_OF_DESTINY_SPELL_BOOST_MANA_LEECH, spellGrade);
			damage.lifeLeech += spell->getWheelOfDestinyBoost(WHEEL_OF_DESTINY_SPELL_BOOST_LIFE_LEECH, spellGrade);
		}
	}

	return spell;
}

/*******************************************************************************
 * Interfaces
 ******************************************************************************/

error_t Player::SetAccountInterface(account::Account *account) {
  if (account == nullptr) {
    return account::ERROR_NULLPTR;
  }

  account_ = account;
  return account::ERROR_NO;
}

error_t Player::GetAccountInterface(account::Account *account) {
  account = account_;
  return account::ERROR_NO;
}
