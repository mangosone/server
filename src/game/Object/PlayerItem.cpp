/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2025 MaNGOS <https://www.getmangos.eu>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

/**
 * @file PlayerItem.cpp
 * @brief Cohesion split of Player.cpp.
 *        Re-applied onto MangosOne TBC 2.4.3; same class, pure code move,
 *        no behaviour change. CMake file(GLOB) picks this TU up automatically.
 */

#include "Player.h"
#include "Language.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Opcodes.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "UpdateMask.h"
#include "CinematicFlyover.h"
#include "SkillDiscovery.h"
#include "QuestDef.h"
#include "GossipDef.h"
#include "UpdateData.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "MapManager.h"
#include "MapPersistentStateMgr.h"
#include "InstanceData.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Formulas.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Pet.h"
#include "Util.h"
#include "Transports.h"
#include "Weather.h"
#include "BattleGround/BattleGround.h"
#include "BattleGround/BattleGroundMgr.h"
#include "BattleGround/BattleGroundAV.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "ArenaTeam.h"
#include "Chat.h"
#include "revision_data.h"
#include "Database/DatabaseImpl.h"
#include "Spell.h"
#include "ScriptMgr.h"
#include "SocialMgr.h"
#include "Mail.h"
#include "DBCStores.h"
#include "SQLStorages.h"
#include "DisableMgr.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

#ifdef ENABLE_PLAYERBOTS
#include "playerbot.h"
#endif

#include <cmath>

#define ZONE_UPDATE_INTERVAL (1*IN_MILLISECONDS)

#define PLAYER_SKILL_INDEX(x)       (PLAYER_SKILL_INFO_1_1 + ((x)*3))
#define PLAYER_SKILL_VALUE_INDEX(x) (PLAYER_SKILL_INDEX(x)+1)
#define PLAYER_SKILL_BONUS_INDEX(x) (PLAYER_SKILL_INDEX(x)+2)

#define SKILL_VALUE(x)         PAIR32_LOPART(x)
#define SKILL_MAX(x)           PAIR32_HIPART(x)
#define MAKE_SKILL_VALUE(v, m) MAKE_PAIR32(v,m)

#define SKILL_TEMP_BONUS(x)    int16(PAIR32_LOPART(x))
#define SKILL_PERM_BONUS(x)    int16(PAIR32_HIPART(x))
#define MAKE_SKILL_BONUS(t, p) MAKE_PAIR32(t,p)

/*********************************************************/
/***                    STORAGE SYSTEM                 ***/
/*********************************************************/

/**
 * @brief Updates a visible virtual weapon slot and consumes temporary enchant charges when needed.
 *
 * @param i The virtual slot index.
 * @param item The item to reflect in the virtual slot.
 */
void Player::SetVirtualItemSlot(uint8 i, Item* item)
{
    MANGOS_ASSERT(i < 3);
    if (i < 2 && item)
    {
        if (!item->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT))
        {
            return;
        }
        uint32 charges = item->GetEnchantmentCharges(TEMP_ENCHANTMENT_SLOT);
        if (charges == 0)
        {
            return;
        }
        if (charges > 1)
        {
            item->SetEnchantmentCharges(TEMP_ENCHANTMENT_SLOT, charges - 1);
        }
        else if (charges <= 1)
        {
            ApplyEnchantment(item, TEMP_ENCHANTMENT_SLOT, false);
            item->ClearEnchantment(TEMP_ENCHANTMENT_SLOT);
        }
    }
}

/**
 * @brief Updates the player's sheathed weapon state and visible weapon slots.
 *
 * @param sheathed The new sheath state.
 */
void Player::SetSheath(SheathState sheathed)
{
    switch (sheathed)
    {
        case SHEATH_STATE_UNARMED:                          // no prepared weapon
            SetVirtualItemSlot(0, NULL);
            SetVirtualItemSlot(1, NULL);
            SetVirtualItemSlot(2, NULL);
            break;
        case SHEATH_STATE_MELEE:                            // prepared melee weapon
        {
            SetVirtualItemSlot(0, GetWeaponForAttack(BASE_ATTACK, true, true));
            SetVirtualItemSlot(1, GetWeaponForAttack(OFF_ATTACK, true, true));
            SetVirtualItemSlot(2, NULL);
        };  break;
        case SHEATH_STATE_RANGED:                           // prepared ranged weapon
            SetVirtualItemSlot(0, NULL);
            SetVirtualItemSlot(1, NULL);
            SetVirtualItemSlot(2, GetWeaponForAttack(RANGED_ATTACK, true, true));
            break;
        default:
            SetVirtualItemSlot(0, NULL);
            SetVirtualItemSlot(1, NULL);
            SetVirtualItemSlot(2, NULL);
            break;
    }
    Unit::SetSheath(sheathed);                              // this must visualize Sheath changing for other players...
}

/**
 * @brief Finds an appropriate equipment slot for an item prototype.
 *
 * @param proto The item prototype to equip.
 * @param slot The preferred slot, or NULL_SLOT to auto-select.
 * @param swap True to allow replacing an occupied slot.
 * @return The chosen slot, or NULL_SLOT if none fits.
 */
uint8 Player::FindEquipSlot(ItemPrototype const* proto, uint32 slot, bool swap) const
{
    uint8 pClass = getClass();

    uint8 slots[4];
    slots[0] = NULL_SLOT;
    slots[1] = NULL_SLOT;
    slots[2] = NULL_SLOT;
    slots[3] = NULL_SLOT;
    switch (proto->InventoryType)
    {
        case INVTYPE_HEAD:
            slots[0] = EQUIPMENT_SLOT_HEAD;
            break;
        case INVTYPE_NECK:
            slots[0] = EQUIPMENT_SLOT_NECK;
            break;
        case INVTYPE_SHOULDERS:
            slots[0] = EQUIPMENT_SLOT_SHOULDERS;
            break;
        case INVTYPE_BODY:
            slots[0] = EQUIPMENT_SLOT_BODY;
            break;
        case INVTYPE_CHEST:
            slots[0] = EQUIPMENT_SLOT_CHEST;
            break;
        case INVTYPE_ROBE:
            slots[0] = EQUIPMENT_SLOT_CHEST;
            break;
        case INVTYPE_WAIST:
            slots[0] = EQUIPMENT_SLOT_WAIST;
            break;
        case INVTYPE_LEGS:
            slots[0] = EQUIPMENT_SLOT_LEGS;
            break;
        case INVTYPE_FEET:
            slots[0] = EQUIPMENT_SLOT_FEET;
            break;
        case INVTYPE_WRISTS:
            slots[0] = EQUIPMENT_SLOT_WRISTS;
            break;
        case INVTYPE_HANDS:
            slots[0] = EQUIPMENT_SLOT_HANDS;
            break;
        case INVTYPE_FINGER:
            slots[0] = EQUIPMENT_SLOT_FINGER1;
            slots[1] = EQUIPMENT_SLOT_FINGER2;
            break;
        case INVTYPE_TRINKET:
            slots[0] = EQUIPMENT_SLOT_TRINKET1;
            slots[1] = EQUIPMENT_SLOT_TRINKET2;
            break;
        case INVTYPE_CLOAK:
            slots[0] =  EQUIPMENT_SLOT_BACK;
            break;
        case INVTYPE_WEAPON:
        {
            slots[0] = EQUIPMENT_SLOT_MAINHAND;

            // suggest offhand slot only if know dual wielding
            // (this will be replace mainhand weapon at auto equip instead unwonted "you don't known dual wielding" ...
            if (CanDualWield())
            {
                slots[1] = EQUIPMENT_SLOT_OFFHAND;
            }
            break;
        };
        case INVTYPE_SHIELD:
            slots[0] = EQUIPMENT_SLOT_OFFHAND;
            break;
        case INVTYPE_RANGED:
            slots[0] = EQUIPMENT_SLOT_RANGED;
            break;
        case INVTYPE_2HWEAPON:
            slots[0] = EQUIPMENT_SLOT_MAINHAND;
            break;
        case INVTYPE_TABARD:
            slots[0] = EQUIPMENT_SLOT_TABARD;
            break;
        case INVTYPE_WEAPONMAINHAND:
            slots[0] = EQUIPMENT_SLOT_MAINHAND;
            break;
        case INVTYPE_WEAPONOFFHAND:
            slots[0] = EQUIPMENT_SLOT_OFFHAND;
            break;
        case INVTYPE_HOLDABLE:
            slots[0] = EQUIPMENT_SLOT_OFFHAND;
            break;
        case INVTYPE_THROWN:
            slots[0] = EQUIPMENT_SLOT_RANGED;
            break;
        case INVTYPE_RANGEDRIGHT:
            slots[0] = EQUIPMENT_SLOT_RANGED;
            break;
        case INVTYPE_BAG:
            slots[0] = INVENTORY_SLOT_BAG_START + 0;
            slots[1] = INVENTORY_SLOT_BAG_START + 1;
            slots[2] = INVENTORY_SLOT_BAG_START + 2;
            slots[3] = INVENTORY_SLOT_BAG_START + 3;
            break;
        case INVTYPE_RELIC:
        {
            switch (proto->SubClass)
            {
                case ITEM_SUBCLASS_ARMOR_LIBRAM:
                    if (pClass == CLASS_PALADIN)
                    {
                        slots[0] = EQUIPMENT_SLOT_RANGED;
                    }
                    break;
                case ITEM_SUBCLASS_ARMOR_IDOL:
                    if (pClass == CLASS_DRUID)
                    {
                        slots[0] = EQUIPMENT_SLOT_RANGED;
                    }
                    break;
                case ITEM_SUBCLASS_ARMOR_TOTEM:
                    if (pClass == CLASS_SHAMAN)
                    {
                        slots[0] = EQUIPMENT_SLOT_RANGED;
                    }
                    break;
                case ITEM_SUBCLASS_ARMOR_MISC:
                    if (pClass == CLASS_WARLOCK)
                    {
                        slots[0] = EQUIPMENT_SLOT_RANGED;
                    }
                    break;
            }
            break;
        }
        default :
            return NULL_SLOT;
    }

    if (slot != NULL_SLOT)
    {
        if (swap || !GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            for (int i = 0; i < 4; ++i)
            {
                if (slots[i] == slot)
                {
                    return slot;
                }
            }
        }
    }
    else
    {
        // search free slot at first
        for (int i = 0; i < 4; ++i)
        {
            if (slots[i] != NULL_SLOT && !GetItemByPos(INVENTORY_SLOT_BAG_0, slots[i]))
            {
                // in case 2hand equipped weapon offhand slot empty but not free
                if (slots[i] != EQUIPMENT_SLOT_OFFHAND || !IsTwoHandUsed())
                {
                    return slots[i];
                }
            }
        }

        // if not found free and can swap return first appropriate from used
        for (int i = 0; i < 4; ++i)
        {
            if (slots[i] != NULL_SLOT && swap)
            {
                return slots[i];
            }
        }
    }

    // no free position
    return NULL_SLOT;
}

/**
 * @brief Checks whether enough matching items can be unequipped or removed.
 *
 * @param item The item entry to search for.
 * @param count The required total item count.
 * @return The inventory result describing whether the request is allowed.
 */
InventoryResult Player::CanUnequipItems(uint32 item, uint32 count) const
{
    Item* pItem;
    uint32 tempcount = 0;

    InventoryResult res = EQUIP_ERR_OK;

    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem->GetEntry() == item)
        {
            InventoryResult ires = CanUnequipItem(INVENTORY_SLOT_BAG_0 << 8 | i, false);
            if (ires == EQUIP_ERR_OK)
            {
                tempcount += pItem->GetCount();
                if (tempcount >= count)
                {
                    return EQUIP_ERR_OK;
                }
            }
            else
            {
                res = ires;
            }
        }
    }
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem->GetEntry() == item)
        {
            tempcount += pItem->GetCount();
            if (tempcount >= count)
            {
                return EQUIP_ERR_OK;
            }
        }
    }
    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem->GetEntry() == item)
        {
            tempcount += pItem->GetCount();
            if (tempcount >= count)
            {
                return EQUIP_ERR_OK;
            }
        }
    }

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pBag)
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                pItem = GetItemByPos(i, j);
                if (pItem && pItem->GetEntry() == item)
                {
                    tempcount += pItem->GetCount();
                    if (tempcount >= count)
                    {
                        return EQUIP_ERR_OK;
                    }
                }
            }
        }
    }

    // not found req. item count and have unequippable items
    return res;
}

/**
 * @brief Counts matching items owned by the player.
 *
 * @param item The item entry to count.
 * @param inBankAlso True to include bank storage.
 * @param skipItem An item instance to exclude from the count.
 * @return The total matching item count.
 */
uint32 Player::GetItemCount(uint32 item, bool inBankAlso, Item* skipItem) const
{
    uint32 count = 0;
    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem != skipItem &&  pItem->GetEntry() == item)
        {
            count += pItem->GetCount();
        }
    }
    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem != skipItem && pItem->GetEntry() == item)
        {
            count += pItem->GetCount();
        }
    }
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pBag)
        {
            count += pBag->GetItemCount(item, skipItem);
        }
    }

    if (skipItem && skipItem->GetProto()->GemProperties)
    {
        for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        {
            Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
            if (pItem && pItem != skipItem && pItem->GetProto()->Socket[0].Color)
            {
                count += pItem->GetGemCountWithID(item);
            }
        }
    }

    if (inBankAlso)
    {
        for (int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
        {
            Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
            if (pItem && pItem != skipItem && pItem->GetEntry() == item)
            {
                count += pItem->GetCount();
            }
        }
        for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i);
            if (pBag)
            {
                count += pBag->GetItemCount(item, skipItem);
            }
        }

        if (skipItem && skipItem->GetProto()->GemProperties)
        {
            for (int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
            {
                Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
                if (pItem && pItem != skipItem && pItem->GetProto()->Socket[0].Color)
                {
                    count += pItem->GetGemCountWithID(item);
                }
            }
        }
    }

    return count;
}

/**
 * @brief Finds the first owned item matching an entry identifier.
 *
 * @param item The item entry to search for.
 * @return The first matching item, or null if none is found.
 */
Item* Player::GetItemByEntry(uint32 item) const
{
     for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
     {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->GetEntry() == item)
            {
                return pItem;
            }
     }

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (Item* itemPtr = pBag->GetItemByEntry(item))
            {
                return itemPtr;
            }
    }

    return NULL;
}

/**
 * @brief Finds an owned item by GUID across inventory and bank storage.
 *
 * @param guid The item GUID to search for.
 * @return The matching item, or null if none is found.
 */
Item* Player::GetItemByGuid(ObjectGuid guid) const
{
    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->GetObjectGuid() == guid)
            {
                return pItem;
            }
    }

    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->GetObjectGuid() == guid)
            {
                return pItem;
            }
    }

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                    if (pItem->GetObjectGuid() == guid)
                    {
                        return pItem;
                    }
            }
    }

    for (int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->GetObjectGuid() == guid)
            {
                return pItem;
            }
    }

    for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                    if (pItem->GetObjectGuid() == guid)
                    {
                        return pItem;
                    }
            }
    }

    return NULL;
}

/**
 * @brief Gets an item from a packed inventory position.
 *
 * @param pos The packed bag and slot position.
 * @return The item at that position, or null if empty.
 */
Item* Player::GetItemByPos(uint16 pos) const
{
    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;
    return GetItemByPos(bag, slot);
}

/**
 * @brief Gets an item from a specific bag and slot location.
 *
 * @param bag The bag identifier.
 * @param slot The slot within the bag or inventory.
 * @return The item at that position, or null if empty.
 */
Item* Player::GetItemByPos(uint8 bag, uint8 slot) const
{
    if (bag == INVENTORY_SLOT_BAG_0 && (slot < BANK_SLOT_BAG_END || (slot >= KEYRING_SLOT_START && slot < KEYRING_SLOT_END)))
    {
        return m_items[slot];
    }
    else if ((bag >= INVENTORY_SLOT_BAG_START && bag < INVENTORY_SLOT_BAG_END)
             || (bag >= BANK_SLOT_BAG_START && bag < BANK_SLOT_BAG_END))
    {
        Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
        {
            return pBag->GetItemByPos(slot);
        }
    }
    return NULL;
}

uint32 Player::GetItemDisplayIdInSlot(uint8 bag, uint8 slot) const
{
    const Item* pItem = GetItemByPos(bag, slot);

    if (!pItem)
    {
        return 0;
    }

    return pItem->GetProto()->DisplayInfoID;
}

/**
 * @brief Gets the equipped weapon used for a specific attack type.
 *
 * @param attackType The attack type to inspect.
 * @param nonbroken True to reject broken weapons.
 * @param useable True to require the weapon to be currently usable.
 * @return The weapon item, or null if none qualifies.
 */
Item* Player::GetWeaponForAttack(WeaponAttackType attackType, bool nonbroken, bool useable) const
{
    uint8 slot;
    switch (attackType)
    {
        case BASE_ATTACK:   slot = EQUIPMENT_SLOT_MAINHAND; break;
        case OFF_ATTACK:    slot = EQUIPMENT_SLOT_OFFHAND;  break;
        case RANGED_ATTACK: slot = EQUIPMENT_SLOT_RANGED;   break;
        default: return NULL;
    }

    Item* item = GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!item || item->GetProto()->Class != ITEM_CLASS_WEAPON)
    {
        return NULL;
    }

    if (useable && !CanUseEquippedWeapon(attackType))
    {
        return NULL;
    }

    if (nonbroken && item->IsBroken())
    {
        return NULL;
    }

    return item;
}

/**
 * @brief Gets the equipped shield item, if any.
 *
 * @param useable True to require the shield to be usable and not broken.
 * @return The shield item, or null if none qualifies.
 */
Item* Player::GetShield(bool useable) const
{
    Item* item = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
    if (!item || item->GetProto()->Class != ITEM_CLASS_ARMOR)
    {
        return NULL;
    }

    if (!useable)
    {
        return item;
    }

    if (item->IsBroken() || !CanUseEquippedWeapon(OFF_ATTACK))
    {
        return NULL;
    }

    return item;
}

/**
 * @brief Maps an equipment slot to its corresponding attack type.
 *
 * @param slot The equipment slot to inspect.
 * @return The mapped attack type, or MAX_ATTACK if not weapon-related.
 */
uint32 Player::GetAttackBySlot(uint8 slot)
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_MAINHAND: return BASE_ATTACK;
        case EQUIPMENT_SLOT_OFFHAND:  return OFF_ATTACK;
        case EQUIPMENT_SLOT_RANGED:   return RANGED_ATTACK;
        default:                      return MAX_ATTACK;
    }
}

/**
 * @brief Checks whether a bag and slot refer to normal inventory storage.
 *
 * @param bag The bag identifier.
 * @param slot The slot identifier.
 * @return True if the position is an inventory position; otherwise, false.
 */
bool Player::IsInventoryPos(uint8 bag, uint8 slot)
{
    if (bag == INVENTORY_SLOT_BAG_0 && slot == NULL_SLOT)
    {
        return true;
    }
    if (bag == INVENTORY_SLOT_BAG_0 && (slot >= INVENTORY_SLOT_ITEM_START && slot < INVENTORY_SLOT_ITEM_END))
    {
        return true;
    }
    if (bag >= INVENTORY_SLOT_BAG_START && bag < INVENTORY_SLOT_BAG_END)
    {
        return true;
    }
    if (bag == INVENTORY_SLOT_BAG_0 && (slot >= KEYRING_SLOT_START && slot < KEYRING_SLOT_END))
    {
        return true;
    }
    return false;
}

/**
 * @brief Checks whether a bag and slot refer to an equipment position.
 *
 * @param bag The bag identifier.
 * @param slot The slot identifier.
 * @return True if the position is an equipment position; otherwise, false.
 */
bool Player::IsEquipmentPos(uint8 bag, uint8 slot)
{
    if (bag == INVENTORY_SLOT_BAG_0 && (slot < EQUIPMENT_SLOT_END))
    {
        return true;
    }
    if (bag == INVENTORY_SLOT_BAG_0 && (slot >= INVENTORY_SLOT_BAG_START && slot < INVENTORY_SLOT_BAG_END))
    {
        return true;
    }
    return false;
}

/**
 * @brief Checks whether a bag and slot refer to bank storage.
 *
 * @param bag The bag identifier.
 * @param slot The slot identifier.
 * @return True if the position is in bank storage; otherwise, false.
 */
bool Player::IsBankPos(uint8 bag, uint8 slot)
{
    if (bag == INVENTORY_SLOT_BAG_0 && (slot >= BANK_SLOT_ITEM_START && slot < BANK_SLOT_ITEM_END))
    {
        return true;
    }
    if (bag == INVENTORY_SLOT_BAG_0 && (slot >= BANK_SLOT_BAG_START && slot < BANK_SLOT_BAG_END))
    {
        return true;
    }
    if (bag >= BANK_SLOT_BAG_START && bag < BANK_SLOT_BAG_END)
    {
        return true;
    }
    return false;
}

/**
 * @brief Checks whether a packed position refers to a bag slot.
 *
 * @param pos The packed bag and slot position.
 * @return True if the position is a bag slot; otherwise, false.
 */
bool Player::IsBagPos(uint16 pos)
{
    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;
    if (bag == INVENTORY_SLOT_BAG_0 && (slot >= INVENTORY_SLOT_BAG_START && slot < INVENTORY_SLOT_BAG_END))
    {
        return true;
    }
    if (bag == INVENTORY_SLOT_BAG_0 && (slot >= BANK_SLOT_BAG_START && slot < BANK_SLOT_BAG_END))
    {
        return true;
    }
    return false;
}

/**
 * @brief Checks whether a bag and slot form a valid player storage position.
 *
 * @param bag The bag identifier.
 * @param slot The slot identifier.
 * @param explicit_pos True if the caller requires an explicit fixed position.
 * @return True if the position is valid; otherwise, false.
 */
bool Player::IsValidPos(uint8 bag, uint8 slot, bool explicit_pos) const
{
    // post selected
    if (bag == NULL_BAG && !explicit_pos)
    {
        return true;
    }

    if (bag == INVENTORY_SLOT_BAG_0)
    {
        // any post selected
        if (slot == NULL_SLOT && !explicit_pos)
        {
            return true;
        }

        // equipment
        if (slot < EQUIPMENT_SLOT_END)
        {
            return true;
        }

        // bag equip slots
        if (slot >= INVENTORY_SLOT_BAG_START && slot < INVENTORY_SLOT_BAG_END)
        {
            return true;
        }

        // backpack slots
        if (slot >= INVENTORY_SLOT_ITEM_START && slot < INVENTORY_SLOT_ITEM_END)
        {
            return true;
        }

        // keyring slots
        if (slot >= KEYRING_SLOT_START && slot < KEYRING_SLOT_END)
        {
            return true;
        }

        // bank main slots
        if (slot >= BANK_SLOT_ITEM_START && slot < BANK_SLOT_ITEM_END)
        {
            return true;
        }

        // bank bag slots
        if (slot >= BANK_SLOT_BAG_START && slot < BANK_SLOT_BAG_END)
        {
            return true;
        }

        return false;
    }

    // bag content slots
    if (bag >= INVENTORY_SLOT_BAG_START && bag < INVENTORY_SLOT_BAG_END)
    {
        Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!pBag)
        {
            return false;
        }

        // any post selected
        if (slot == NULL_SLOT && !explicit_pos)
        {
            return true;
        }

        return slot < pBag->GetBagSize();
    }

    // bank bag content slots
    if (bag >= BANK_SLOT_BAG_START && bag < BANK_SLOT_BAG_END)
    {
        Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!pBag)
        {
            return false;
        }

        // any post selected
        if (slot == NULL_SLOT && !explicit_pos)
        {
            return true;
        }

        return slot < pBag->GetBagSize();
    }

    // where this?
    return false;
}

/**
 * @brief Checks whether the player owns at least a given count of an item.
 *
 * @param item The item entry to count.
 * @param count The required quantity.
 * @param inBankAlso True to include bank storage in the search.
 * @return True if enough items are present; otherwise, false.
 */
bool Player::HasItemCount(uint32 item, uint32 count, bool inBankAlso) const
{
    uint32 tempcount = 0;
    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem->GetEntry() == item && !pItem->IsInTrade())
        {
            tempcount += pItem->GetCount();
            if (tempcount >= count)
            {
                return true;
            }
        }
    }
    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem->GetEntry() == item && !pItem->IsInTrade())
        {
            tempcount += pItem->GetCount();
            if (tempcount >= count)
            {
                return true;
            }
        }
    }
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                Item* pItem = GetItemByPos(i, j);
                if (pItem && pItem->GetEntry() == item && !pItem->IsInTrade())
                {
                    tempcount += pItem->GetCount();
                    if (tempcount >= count)
                    {
                        return true;
                    }
                }
            }
        }
    }

    if (inBankAlso)
    {
        for (int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
        {
            Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
            if (pItem && pItem->GetEntry() == item && !pItem->IsInTrade())
            {
                tempcount += pItem->GetCount();
                if (tempcount >= count)
                {
                    return true;
                }
            }
        }
        for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    Item* pItem = GetItemByPos(i, j);
                    if (pItem && pItem->GetEntry() == item && !pItem->IsInTrade())
                    {
                        tempcount += pItem->GetCount();
                        if (tempcount >= count)
                        {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

bool Player::HasItemOrGemWithIdEquipped(uint32 item, uint32 count, uint8 except_slot) const
{
    uint32 tempcount = 0;
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (i == int(except_slot))
        {
            continue;
        }

        Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
        if (pItem && pItem->GetEntry() == item)
        {
            tempcount += pItem->GetCount();
            if (tempcount >= count)
            {
                return true;
            }
        }
    }

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(item);
    if (pProto && pProto->GemProperties)
    {
        for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
        {
            if (i == int(except_slot))
            {
                continue;
            }

            Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i);
            if (pItem && pItem->GetProto()->Socket[0].Color)
            {
                tempcount += pItem->GetGemCountWithID(item);
                if (tempcount >= count)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

/**
 * @brief Checks whether the player can carry more copies of a limited item.
 *
 * @param entry The item entry to evaluate.
 * @param count The additional quantity to add.
 * @param pItem An item instance to exclude from current ownership checks.
 * @param no_space_count Optional output for the quantity that exceeds the limit.
 * @return The inventory result describing the carry-limit check.
 */
InventoryResult Player::_CanTakeMoreSimilarItems(uint32 entry, uint32 count, Item* pItem, uint32* no_space_count) const
{
    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(entry);
    if (!pProto)
    {
        if (no_space_count)
        {
            *no_space_count = count;
        }
        return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
    }

    // no maximum
    if (pProto->MaxCount > 0)
    {
        uint32 curcount = GetItemCount(pProto->ItemId, true, pItem);

        if (curcount + count > uint32(pProto->MaxCount))
        {
            if (no_space_count)
            {
                *no_space_count = count + curcount - pProto->MaxCount;
            }
            return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
        }
    }


    return EQUIP_ERR_OK;
}

/**
 * @brief Checks whether item count can be stored in a specific slot.
 *
 * @param bag The destination bag identifier.
 * @param slot The destination slot identifier.
 * @param dest The accumulated destination positions.
 * @param pProto The item prototype being stored.
 * @param count The remaining quantity to place.
 * @param swap True to allow occupying an already used slot.
 * @param pSrcItem The source item being moved.
 * @return The inventory result for the slot check.
 */
InventoryResult Player::_CanStoreItem_InSpecificSlot(uint8 bag, uint8 slot, ItemPosCountVec& dest, ItemPrototype const* pProto, uint32& count, bool swap, Item* pSrcItem) const
{
    Item* pItem2 = GetItemByPos(bag, slot);

    // ignore move item (this slot will be empty at move)
    if (pItem2 == pSrcItem)
    {
        pItem2 = NULL;
    }

    uint32 need_space;

    // empty specific slot - check item fit to slot
    if (!pItem2 || swap)
    {
        if (bag == INVENTORY_SLOT_BAG_0)
        {
            // keyring case
            if (slot >= KEYRING_SLOT_START && slot < KEYRING_SLOT_START + GetMaxKeyringSize() && !(pProto->BagFamily & BAG_FAMILY_KEYS))
            {
                return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
            }

            // prevent cheating
            if ((slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END) || slot >= PLAYER_SLOT_END)
            {
                return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
            }
        }
        else
        {
            Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
            if (!pBag)
            {
                return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
            }

            ItemPrototype const* pBagProto = pBag->GetProto();
            if (!pBagProto)
            {
                return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
            }

            if (slot >= pBagProto->ContainerSlots)
            {
                return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
            }

            if (!ItemCanGoIntoBag(pProto, pBagProto))
            {
                return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
            }
        }

        // non empty stack with space
        need_space = pProto->Stackable;
    }
    // non empty slot, check item type
    else
    {
        // can be merged at least partly
        InventoryResult res  = pItem2->CanBeMergedPartlyWith(pProto);
        if (res != EQUIP_ERR_OK)
        {
            return res;
        }

        need_space = pProto->Stackable - pItem2->GetCount();
    }

    if (need_space > count)
    {
        need_space = count;
    }

    ItemPosCount newPosition = ItemPosCount((bag << 8) | slot, need_space);
    if (!newPosition.isContainedIn(dest))
    {
        dest.push_back(newPosition);
        count -= need_space;
    }
    return EQUIP_ERR_OK;
}

/**
 * @brief Searches a bag for valid storage positions for an item.
 *
 * @param bag The bag identifier to search.
 * @param dest The accumulated destination positions.
 * @param pProto The item prototype being stored.
 * @param count The remaining quantity to place.
 * @param merge True to search existing stacks; false to search empty slots.
 * @param non_specialized True to restrict search to plain containers.
 * @param pSrcItem The source item being moved.
 * @param skip_bag A bag identifier to skip.
 * @param skip_slot A slot identifier to skip.
 * @return The inventory result for the bag search.
 */
InventoryResult Player::_CanStoreItem_InBag(uint8 bag, ItemPosCountVec& dest, ItemPrototype const* pProto, uint32& count, bool merge, bool non_specialized, Item* pSrcItem, uint8 skip_bag, uint8 skip_slot) const
{
    // skip specific bag already processed in first called _CanStoreItem_InBag
    if (bag == skip_bag)
    {
        return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
    }

    // skip nonexistent bag or self targeted bag
    Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
    if (!pBag || pBag == pSrcItem)
    {
        return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
    }

    ItemPrototype const* pBagProto = pBag->GetProto();
    if (!pBagProto)
    {
        return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
    }

    // specialized bag mode or non-specilized
    if (non_specialized != (pBagProto->Class == ITEM_CLASS_CONTAINER && pBagProto->SubClass == ITEM_SUBCLASS_CONTAINER))
    {
        return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
    }

    if (!ItemCanGoIntoBag(pProto, pBagProto))
    {
        return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
    }

    for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
    {
        // skip specific slot already processed in first called _CanStoreItem_InSpecificSlot
        if (j == skip_slot)
        {
            continue;
        }

        Item* pItem2 = GetItemByPos(bag, j);

        // ignore move item (this slot will be empty at move)
        if (pItem2 == pSrcItem)
        {
            pItem2 = NULL;
        }

        // if merge skip empty, if !merge skip non-empty
        if ((pItem2 != NULL) != merge)
        {
            continue;
        }

        uint32 need_space = pProto->GetMaxStackSize();

        if (pItem2)
        {
            // can be merged at least partly
            uint8 res  = pItem2->CanBeMergedPartlyWith(pProto);
            if (res != EQUIP_ERR_OK)
            {
                continue;
            }

            // decrease at current stacksize
            need_space -= pItem2->GetCount();
        }

        if (need_space > count)
        {
            need_space = count;
        }

        ItemPosCount newPosition = ItemPosCount((bag << 8) | j, need_space);
        if (!newPosition.isContainedIn(dest))
        {
            dest.push_back(newPosition);
            count -= need_space;

            if (count == 0)
            {
                return EQUIP_ERR_OK;
            }
        }
    }
    return EQUIP_ERR_OK;
}

/**
 * @brief Searches a range of inventory slots for valid storage positions.
 *
 * @param slot_begin The first slot in the search range.
 * @param slot_end One past the last slot in the search range.
 * @param dest The accumulated destination positions.
 * @param pProto The item prototype being stored.
 * @param count The remaining quantity to place.
 * @param merge True to search existing stacks; false to search empty slots.
 * @param pSrcItem The source item being moved.
 * @param skip_bag A bag identifier to skip.
 * @param skip_slot A slot identifier to skip.
 * @return The inventory result for the slot-range search.
 */
InventoryResult Player::_CanStoreItem_InInventorySlots(uint8 slot_begin, uint8 slot_end, ItemPosCountVec& dest, ItemPrototype const* pProto, uint32& count, bool merge, Item* pSrcItem, uint8 skip_bag, uint8 skip_slot) const
{
    for (uint32 j = slot_begin; j < slot_end; ++j)
    {
        // skip specific slot already processed in first called _CanStoreItem_InSpecificSlot
        if (INVENTORY_SLOT_BAG_0 == skip_bag && j == skip_slot)
        {
            continue;
        }

        Item* pItem2 = GetItemByPos(INVENTORY_SLOT_BAG_0, j);

        // ignore move item (this slot will be empty at move)
        if (pItem2 == pSrcItem)
        {
            pItem2 = NULL;
        }

        // if merge skip empty, if !merge skip non-empty
        if ((pItem2 != NULL) != merge)
        {
            continue;
        }

        uint32 need_space = pProto->GetMaxStackSize();

        if (pItem2)
        {
            // can be merged at least partly
            uint8 res  = pItem2->CanBeMergedPartlyWith(pProto);
            if (res != EQUIP_ERR_OK)
            {
                continue;
            }

            // descrease at current stacksize
            need_space -= pItem2->GetCount();
        }

        if (need_space > count)
        {
            need_space = count;
        }

        ItemPosCount newPosition = ItemPosCount((INVENTORY_SLOT_BAG_0 << 8) | j, need_space);
        if (!newPosition.isContainedIn(dest))
        {
            dest.push_back(newPosition);
            count -= need_space;

            if (count == 0)
            {
                return EQUIP_ERR_OK;
            }
        }
    }
    return EQUIP_ERR_OK;
}

/**
 * @brief Computes valid destinations for storing an item stack in inventory.
 *
 * @param bag The preferred destination bag, or NULL_BAG for auto-placement.
 * @param slot The preferred destination slot, or NULL_SLOT for auto-placement.
 * @param dest The accumulated destination positions.
 * @param entry The item entry being stored.
 * @param count The quantity to store.
 * @param pItem The source item being moved.
 * @param swap True to allow swapping with occupied slots.
 * @param no_space_count Optional output for the quantity that could not be placed.
 * @return The inventory result for the storage search.
 */
InventoryResult Player::_CanStoreItem(uint8 bag, uint8 slot, ItemPosCountVec& dest, uint32 entry, uint32 count, Item* pItem, bool swap, uint32* no_space_count) const
{
    DEBUG_LOG("STORAGE: CanStoreItem bag = %u, slot = %u, item = %u, count = %u", bag, slot, entry, count);

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(entry);
    if (!pProto)
    {
        if (no_space_count)
        {
            *no_space_count = count;
        }
        return swap ? EQUIP_ERR_ITEMS_CANT_BE_SWAPPED : EQUIP_ERR_ITEM_NOT_FOUND;
    }

    if (pItem)
    {
        // item used
        if (pItem->HasTemporaryLoot())
        {
            if (no_space_count)
            {
                *no_space_count = count;
            }
            return EQUIP_ERR_ALREADY_LOOTED;
        }

        if (pItem->IsBindedNotWith(this))
        {
            if (no_space_count)
            {
                *no_space_count = count;
            }
            return EQUIP_ERR_DONT_OWN_THAT_ITEM;
        }
    }

    // check count of items (skip for auto move for same player from bank)
    uint32 no_similar_count = 0;                            // can't store this amount similar items
    InventoryResult res = _CanTakeMoreSimilarItems(entry, count, pItem, &no_similar_count);
    if (res != EQUIP_ERR_OK)
    {
        if (count == no_similar_count)
        {
            if (no_space_count)
            {
                *no_space_count = no_similar_count;
            }
            return res;
        }
        count -= no_similar_count;
    }

    // in specific slot
    if (bag != NULL_BAG && slot != NULL_SLOT)
    {
        res = _CanStoreItem_InSpecificSlot(bag, slot, dest, pProto, count, swap, pItem);
        if (res != EQUIP_ERR_OK)
        {
            if (no_space_count)
            {
                *no_space_count = count + no_similar_count;
            }
            return res;
        }

        if (count == 0)
        {
            if (no_similar_count == 0)
            {
                return EQUIP_ERR_OK;
            }

            if (no_space_count)
            {
                *no_space_count = count + no_similar_count;
            }
            return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
        }
    }

    // not specific slot or have space for partly store only in specific slot

    // in specific bag
    if (bag != NULL_BAG)
    {
        // search stack in bag for merge to
        if (pProto->Stackable > 1)
        {
            if (bag == INVENTORY_SLOT_BAG_0)               // inventory
            {
                res = _CanStoreItem_InInventorySlots(KEYRING_SLOT_START, KEYRING_SLOT_END, dest, pProto, count, true, pItem, bag, slot);
                if (res != EQUIP_ERR_OK)
                {
                    if (no_space_count)
                    {
                        *no_space_count = count + no_similar_count;
                    }
                    return res;
                }

                if (count == 0)
                {
                    if (no_similar_count == 0)
                    {
                        return EQUIP_ERR_OK;
                    }

                    if (no_space_count)
                    {
                        *no_space_count = count + no_similar_count;
                    }
                    return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                }

                res = _CanStoreItem_InInventorySlots(INVENTORY_SLOT_ITEM_START, INVENTORY_SLOT_ITEM_END, dest, pProto, count, true, pItem, bag, slot);
                if (res != EQUIP_ERR_OK)
                {
                    if (no_space_count)
                    {
                        *no_space_count = count + no_similar_count;
                    }
                    return res;
                }

                if (count == 0)
                {
                    if (no_similar_count == 0)
                    {
                        return EQUIP_ERR_OK;
                    }

                    if (no_space_count)
                    {
                        *no_space_count = count + no_similar_count;
                    }
                    return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                }
            }
            else                                            // equipped bag
            {
                // we need check 2 time (specialized/non_specialized), use NULL_BAG to prevent skipping bag
                res = _CanStoreItem_InBag(bag, dest, pProto, count, true, false, pItem, NULL_BAG, slot);
                if (res != EQUIP_ERR_OK)
                {
                    res = _CanStoreItem_InBag(bag, dest, pProto, count, true, true, pItem, NULL_BAG, slot);
                }

                if (res != EQUIP_ERR_OK)
                {
                    if (no_space_count)
                    {
                        *no_space_count = count + no_similar_count;
                    }
                    return res;
                }

                if (count == 0)
                {
                    if (no_similar_count == 0)
                    {
                        return EQUIP_ERR_OK;
                    }

                    if (no_space_count)
                    {
                        *no_space_count = count + no_similar_count;
                    }
                    return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                }
            }
        }

        // search free slot in bag for place to
        if (bag == INVENTORY_SLOT_BAG_0)                    // inventory
        {
            // search free slot - keyring case
            if (pProto->BagFamily & BAG_FAMILY_KEYS)
            {
                uint32 keyringSize = GetMaxKeyringSize();
                res = _CanStoreItem_InInventorySlots(KEYRING_SLOT_START, KEYRING_SLOT_START + keyringSize, dest, pProto, count, false, pItem, bag, slot);
                if (res != EQUIP_ERR_OK)
                {
                    if (no_space_count)
                    {
                        *no_space_count = count + no_similar_count;
                    }
                    return res;
                }

                if (count == 0)
                {
                    if (no_similar_count == 0)
                    {
                        return EQUIP_ERR_OK;
                    }

                    if (no_space_count)
                    {
                        *no_space_count = count + no_similar_count;
                    }
                    return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                }
            }

            res = _CanStoreItem_InInventorySlots(INVENTORY_SLOT_ITEM_START, INVENTORY_SLOT_ITEM_END, dest, pProto, count, false, pItem, bag, slot);
            if (res != EQUIP_ERR_OK)
            {
                if (no_space_count)
                {
                    *no_space_count = count + no_similar_count;
                }
                return res;
            }

            if (count == 0)
            {
                if (no_similar_count == 0)
                {
                    return EQUIP_ERR_OK;
                }

                if (no_space_count)
                {
                    *no_space_count = count + no_similar_count;
                }
                return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
            }
        }
        else                                                // equipped bag
        {
            res = _CanStoreItem_InBag(bag, dest, pProto, count, false, false, pItem, NULL_BAG, slot);
            if (res != EQUIP_ERR_OK)
            {
                res = _CanStoreItem_InBag(bag, dest, pProto, count, false, true, pItem, NULL_BAG, slot);
            }

            if (res != EQUIP_ERR_OK)
            {
                if (no_space_count)
                {
                    *no_space_count = count + no_similar_count;
                }
                return res;
            }

            if (count == 0)
            {
                if (no_similar_count == 0)
                {
                    return EQUIP_ERR_OK;
                }

                if (no_space_count)
                {
                    *no_space_count = count + no_similar_count;
                }
                return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
            }
        }
    }

    // not specific bag or have space for partly store only in specific bag

    // search stack for merge to
    if (pProto->Stackable > 1)
    {
        res = _CanStoreItem_InInventorySlots(KEYRING_SLOT_START, KEYRING_SLOT_END, dest, pProto, count, true, pItem, bag, slot);
        if (res != EQUIP_ERR_OK)
        {
            if (no_space_count)
            {
                *no_space_count = count + no_similar_count;
            }
            return res;
        }

        if (count == 0)
        {
            if (no_similar_count == 0)
            {
                return EQUIP_ERR_OK;
            }

            if (no_space_count)
            {
                *no_space_count = count + no_similar_count;
            }
            return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
        }

        res = _CanStoreItem_InInventorySlots(INVENTORY_SLOT_ITEM_START, INVENTORY_SLOT_ITEM_END, dest, pProto, count, true, pItem, bag, slot);
        if (res != EQUIP_ERR_OK)
        {
            if (no_space_count)
            {
                *no_space_count = count + no_similar_count;
            }
            return res;
        }

        if (count == 0)
        {
            if (no_similar_count == 0)
            {
                return EQUIP_ERR_OK;
            }

            if (no_space_count)
            {
                *no_space_count = count + no_similar_count;
            }
            return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
        }

        if (pProto->BagFamily)
        {
            for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
            {
                res = _CanStoreItem_InBag(i, dest, pProto, count, true, false, pItem, bag, slot);
                if (res != EQUIP_ERR_OK)
                {
                    continue;
                }

                if (count == 0)
                {
                    if (no_similar_count == 0)
                    {
                        return EQUIP_ERR_OK;
                    }

                    if (no_space_count)
                    {
                        *no_space_count = count + no_similar_count;
                    }
                    return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                }
            }
        }

        for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            res = _CanStoreItem_InBag(i, dest, pProto, count, true, true, pItem, bag, slot);
            if (res != EQUIP_ERR_OK)
            {
                continue;
            }

            if (count == 0)
            {
                if (no_similar_count == 0)
                {
                    return EQUIP_ERR_OK;
                }

                if (no_space_count)
                {
                    *no_space_count = count + no_similar_count;
                }
                return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
            }
        }
    }

    // search free slot - special bag case
    if (pProto->BagFamily)
    {
        if (pProto->BagFamily & BAG_FAMILY_KEYS)
        {
            uint32 keyringSize = GetMaxKeyringSize();
            res = _CanStoreItem_InInventorySlots(KEYRING_SLOT_START, KEYRING_SLOT_START + keyringSize, dest, pProto, count, false, pItem, bag, slot);
            if (res != EQUIP_ERR_OK)
            {
                if (no_space_count)
                {
                    *no_space_count = count + no_similar_count;
                }
                return res;
            }

            if (count == 0)
            {
                if (no_similar_count == 0)
                {
                    return EQUIP_ERR_OK;
                }

                if (no_space_count)
                {
                    *no_space_count = count + no_similar_count;
                }
                return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
            }
        }

        for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            res = _CanStoreItem_InBag(i, dest, pProto, count, false, false, pItem, bag, slot);
            if (res != EQUIP_ERR_OK)
            {
                continue;
            }

            if (count == 0)
            {
                if (no_similar_count == 0)
                {
                    return EQUIP_ERR_OK;
                }

                if (no_space_count)
                {
                    *no_space_count = count + no_similar_count;
                }
                return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
            }
        }
    }

    // Normally it would be impossible to autostore not empty bags
    if (pItem && pItem->IsBag() && !((Bag*)pItem)->IsEmpty())
    {
        return EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG;
    }

    // search free slot
    res = _CanStoreItem_InInventorySlots(INVENTORY_SLOT_ITEM_START, INVENTORY_SLOT_ITEM_END, dest, pProto, count, false, pItem, bag, slot);
    if (res != EQUIP_ERR_OK)
    {
        if (no_space_count)
        {
            *no_space_count = count + no_similar_count;
        }
        return res;
    }

    if (count == 0)
    {
        if (no_similar_count == 0)
        {
            return EQUIP_ERR_OK;
        }

        if (no_space_count)
        {
            *no_space_count = count + no_similar_count;
        }
        return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
    }

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        res = _CanStoreItem_InBag(i, dest, pProto, count, false, true, pItem, bag, slot);
        if (res != EQUIP_ERR_OK)
        {
            continue;
        }

        if (count == 0)
        {
            if (no_similar_count == 0)
            {
                return EQUIP_ERR_OK;
            }

            if (no_space_count)
            {
                *no_space_count = count + no_similar_count;
            }
            return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
        }
    }

    if (no_space_count)
    {
        *no_space_count = count + no_similar_count;
    }

    return EQUIP_ERR_INVENTORY_FULL;
}

//////////////////////////////////////////////////////////////////////////
InventoryResult Player::CanStoreItems(Item** pItems, int count) const
{
    Item*    pItem2;

    // fill space table
    int inv_slot_items[INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START];
    int inv_bags[INVENTORY_SLOT_BAG_END - INVENTORY_SLOT_BAG_START][MAX_BAG_SIZE];
    int inv_keys[KEYRING_SLOT_END - KEYRING_SLOT_START];

    memset(inv_slot_items, 0, sizeof(int) * (INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START));
    memset(inv_bags, 0, sizeof(int) * (INVENTORY_SLOT_BAG_END - INVENTORY_SLOT_BAG_START)*MAX_BAG_SIZE);
    memset(inv_keys, 0, sizeof(int) * (KEYRING_SLOT_END - KEYRING_SLOT_START));

    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        pItem2 = GetItemByPos(INVENTORY_SLOT_BAG_0, i);

        if (pItem2 && !pItem2->IsInTrade())
        {
            inv_slot_items[i - INVENTORY_SLOT_ITEM_START] = pItem2->GetCount();
        }
    }

    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        pItem2 = GetItemByPos(INVENTORY_SLOT_BAG_0, i);

        if (pItem2 && !pItem2->IsInTrade())
        {
            inv_keys[i - KEYRING_SLOT_START] = pItem2->GetCount();
        }
    }

    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                pItem2 = GetItemByPos(i, j);
                if (pItem2 && !pItem2->IsInTrade())
                {
                    inv_bags[i - INVENTORY_SLOT_BAG_START][j] = pItem2->GetCount();
                }
            }
        }
    }

    // check free space for all items
    for (int k = 0; k < count; ++k)
    {
        Item*  pItem = pItems[k];

        // no item
        if (!pItem)
        {
            continue;
        }

        DEBUG_LOG("STORAGE: CanStoreItems %i. item = %u, count = %u", k + 1, pItem->GetEntry(), pItem->GetCount());
        ItemPrototype const* pProto = pItem->GetProto();

        // strange item
        if (!pProto)
        {
            return EQUIP_ERR_ITEM_NOT_FOUND;
        }

        // item used
        if (pItem->HasTemporaryLoot())
        {
            return EQUIP_ERR_ALREADY_LOOTED;
        }

        // item it 'bind'
        if (pItem->IsBindedNotWith(this))
        {
            return EQUIP_ERR_DONT_OWN_THAT_ITEM;
        }

        Bag* pBag;
        ItemPrototype const* pBagProto;

        // item is 'one item only'
        InventoryResult res = CanTakeMoreSimilarItems(pItem);
        if (res != EQUIP_ERR_OK)
        {
            return res;
        }

        // search stack for merge to
        if (pProto->Stackable > 1)
        {
            bool b_found = false;

            for (int t = KEYRING_SLOT_START; t < KEYRING_SLOT_END; ++t)
            {
                pItem2 = GetItemByPos(INVENTORY_SLOT_BAG_0, t);
                if (pItem2 && pItem2->CanBeMergedPartlyWith(pProto) == EQUIP_ERR_OK && inv_keys[t - KEYRING_SLOT_START] + pItem->GetCount() <= pProto->GetMaxStackSize())
                {
                    inv_keys[t - KEYRING_SLOT_START] += pItem->GetCount();
                    b_found = true;
                    break;
                }
            }
            if (b_found)
            {
                continue;
            }

            for (int t = INVENTORY_SLOT_ITEM_START; t < INVENTORY_SLOT_ITEM_END; ++t)
            {
                pItem2 = GetItemByPos(INVENTORY_SLOT_BAG_0, t);
                if (pItem2 && pItem2->CanBeMergedPartlyWith(pProto) == EQUIP_ERR_OK && inv_slot_items[t - INVENTORY_SLOT_ITEM_START] + pItem->GetCount() <= pProto->GetMaxStackSize())
                {
                    inv_slot_items[t - INVENTORY_SLOT_ITEM_START] += pItem->GetCount();
                    b_found = true;
                    break;
                }
            }
            if (b_found)
            {
                continue;
            }

            for (int t = INVENTORY_SLOT_BAG_START; !b_found && t < INVENTORY_SLOT_BAG_END; ++t)
            {
                pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, t);
                if (pBag)
                {
                    for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                    {
                        pItem2 = GetItemByPos(t, j);
                        if (pItem2 && pItem2->CanBeMergedPartlyWith(pProto) == EQUIP_ERR_OK && inv_bags[t - INVENTORY_SLOT_BAG_START][j] + pItem->GetCount() <= pProto->GetMaxStackSize())
                        {
                            inv_bags[t - INVENTORY_SLOT_BAG_START][j] += pItem->GetCount();
                            b_found = true;
                            break;
                        }
                    }
                }
            }
            if (b_found)
            {
                continue;
            }
        }

        // special bag case
        if (pProto->BagFamily)
        {
            bool b_found = false;
            if (pProto->BagFamily & BAG_FAMILY_KEYS)
            {
                uint32 keyringSize = GetMaxKeyringSize();
                for (uint32 t = KEYRING_SLOT_START; t < KEYRING_SLOT_START + keyringSize; ++t)
                {
                    if (inv_keys[t - KEYRING_SLOT_START] == 0)
                    {
                        inv_keys[t - KEYRING_SLOT_START] = 1;
                        b_found = true;
                        break;
                    }
                }
            }

            if (b_found)
            {
                continue;
            }

            for (int t = INVENTORY_SLOT_BAG_START; !b_found && t < INVENTORY_SLOT_BAG_END; ++t)
            {
                pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, t);
                if (pBag)
                {
                    pBagProto = pBag->GetProto();

                    // not plain container check
                    if (pBagProto && (pBagProto->Class != ITEM_CLASS_CONTAINER || pBagProto->SubClass != ITEM_SUBCLASS_CONTAINER) &&
                        ItemCanGoIntoBag(pProto, pBagProto))
                    {
                        for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                        {
                            if (inv_bags[t - INVENTORY_SLOT_BAG_START][j] == 0)
                            {
                                inv_bags[t - INVENTORY_SLOT_BAG_START][j] = 1;
                                b_found = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (b_found)
            {
                continue;
            }
        }

        // search free slot
        bool b_found = false;
        for (int t = INVENTORY_SLOT_ITEM_START; t < INVENTORY_SLOT_ITEM_END; ++t)
        {
            if (inv_slot_items[t - INVENTORY_SLOT_ITEM_START] == 0)
            {
                inv_slot_items[t - INVENTORY_SLOT_ITEM_START] = 1;
                b_found = true;
                break;
            }
        }
        if (b_found)
        {
            continue;
        }

        // search free slot in bags
        for (int t = INVENTORY_SLOT_BAG_START; !b_found && t < INVENTORY_SLOT_BAG_END; ++t)
        {
            pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, t);
            if (pBag)
            {
                pBagProto = pBag->GetProto();

                // special bag already checked
                if (pBagProto && (pBagProto->Class != ITEM_CLASS_CONTAINER || pBagProto->SubClass != ITEM_SUBCLASS_CONTAINER))
                {
                    continue;
                }

                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    if (inv_bags[t - INVENTORY_SLOT_BAG_START][j] == 0)
                    {
                        inv_bags[t - INVENTORY_SLOT_BAG_START][j] = 1;
                        b_found = true;
                        break;
                    }
                }
            }
        }

        // no free slot found?
        if (!b_found)
        {
            return EQUIP_ERR_INVENTORY_FULL;
        }
    }

    return EQUIP_ERR_OK;
}

//////////////////////////////////////////////////////////////////////////
InventoryResult Player::CanEquipNewItem(uint8 slot, uint16& dest, uint32 item, bool swap) const
{
    dest = 0;
    Item* pItem = Item::CreateItem(item, 1, this);
    if (pItem)
    {
        InventoryResult result = CanEquipItem(slot, dest, pItem, swap);
        delete pItem;
        return result;
    }

    return EQUIP_ERR_ITEM_NOT_FOUND;
}

/**
 * @brief Checks whether an item can be equipped and resolves its destination slot.
 *
 * @param slot The preferred equipment slot.
 * @param dest Output packed destination slot.
 * @param pItem The item to equip.
 * @param swap True to allow replacing an existing item.
 * @param direct_action True if this is an immediate player action.
 * @return The inventory result for the equip attempt.
 */
InventoryResult Player::CanEquipItem(uint8 slot, uint16& dest, Item* pItem, bool swap, bool direct_action) const
{
    dest = 0;
    if (pItem)
    {
        DEBUG_LOG("STORAGE: CanEquipItem slot = %u, item = %u, count = %u", slot, pItem->GetEntry(), pItem->GetCount());
        ItemPrototype const* pProto = pItem->GetProto();
        if (pProto)
        {
            // item used
            if (pItem->HasTemporaryLoot())
            {
                return EQUIP_ERR_ALREADY_LOOTED;
            }

            if (pItem->IsBindedNotWith(this))
            {
                return EQUIP_ERR_DONT_OWN_THAT_ITEM;
            }

            // check count of items (skip for auto move for same player from bank)
            InventoryResult res = CanTakeMoreSimilarItems(pItem);
            if (res != EQUIP_ERR_OK)
            {
                return res;
            }

            // check this only in game
            if (direct_action)
            {
                // May be here should be more stronger checks; STUNNED checked
                // ROOT, CONFUSED, DISTRACTED, FLEEING this needs to be checked.
                if (hasUnitState(UNIT_STAT_STUNNED))
                {
                    return EQUIP_ERR_YOU_ARE_STUNNED;
                }

                // do not allow equipping gear except weapons, offhands, projectiles, relics in
                // - combat
                // - in-progress arenas
                if (!pProto->CanChangeEquipStateInCombat())
                {
                    if (IsInCombat())
                    {
                        return EQUIP_ERR_NOT_IN_COMBAT;
                    }

                    if (BattleGround* bg = GetBattleGround())
                        if (bg->isArena() && bg->GetStatus() == STATUS_IN_PROGRESS)
                        {
                            return EQUIP_ERR_NOT_DURING_ARENA_MATCH;
                        }
                }

                // prevent equip item in process logout
                if (GetSession()->isLogingOut())
                {
                    return EQUIP_ERR_YOU_ARE_STUNNED;
                }

                if (IsInCombat() && pProto->Class == ITEM_CLASS_WEAPON && m_weaponChangeTimer != 0)
                {
                    return EQUIP_ERR_CANT_DO_RIGHT_NOW; // maybe exist better err
                }

                if (IsNonMeleeSpellCasted(false))
                {
                    return EQUIP_ERR_CANT_DO_RIGHT_NOW;
                }

                // prevent equip item in Spirit of Redemption (Aura: 27827)
                if (HasAuraType(SPELL_AURA_SPIRIT_OF_REDEMPTION))
                {
                    return EQUIP_ERR_CANT_DO_RIGHT_NOW;
                }
            }

            uint8 eslot = FindEquipSlot(pProto, slot, swap);
            if (eslot == NULL_SLOT)
            {
                return EQUIP_ERR_ITEM_CANT_BE_EQUIPPED;
            }

            InventoryResult msg = CanUseItem(pItem , direct_action);
            if (msg != EQUIP_ERR_OK)
            {
                return msg;
            }
            if (!swap && GetItemByPos(INVENTORY_SLOT_BAG_0, eslot))
            {
                return EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE;
            }

            // if swap ignore item (equipped also)
            if (InventoryResult res2 = CanEquipUniqueItem(pItem, swap ? eslot : uint8(NULL_SLOT)))
            {
                return res2;
            }

            // check unique-equipped special item classes
            if (pProto->Class == ITEM_CLASS_QUIVER)
            {
                for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
                {
                    if (Item* pBag = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                    {
                        if (pBag != pItem)
                        {
                            if (ItemPrototype const* pBagProto = pBag->GetProto())
                            {
                                if (pBagProto->Class == pProto->Class && (!swap || pBag->GetSlot() != eslot))
                                {
                                    return (pBagProto->SubClass == ITEM_SUBCLASS_AMMO_POUCH)
                                           ? EQUIP_ERR_CAN_EQUIP_ONLY1_AMMOPOUCH
                                           : EQUIP_ERR_CAN_EQUIP_ONLY1_QUIVER;
                                }
                            }
                        }
                    }
                }
            }

            uint32 type = pProto->InventoryType;

            if (eslot == EQUIPMENT_SLOT_OFFHAND)
            {
                if (type == INVTYPE_WEAPON || type == INVTYPE_WEAPONOFFHAND)
                {
                    if (!CanDualWield())
                    {
                        return EQUIP_ERR_CANT_DUAL_WIELD;
                    }
                }
                else if (type == INVTYPE_2HWEAPON)
                {
                    return EQUIP_ERR_CANT_DUAL_WIELD;
                }

                if (IsTwoHandUsed())
                {
                    return EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED;
                }
            }

            // equip two-hand weapon case (with possible unequip 2 items)
            if (type == INVTYPE_2HWEAPON)
            {
                if (eslot != EQUIPMENT_SLOT_MAINHAND)
                {
                    return EQUIP_ERR_ITEM_CANT_BE_EQUIPPED;
                }

                // offhand item must can be stored in inventory for offhand item and it also must be unequipped
                Item* offItem = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
                ItemPosCountVec off_dest;
                if (offItem && (!direct_action ||
                                CanUnequipItem(uint16(INVENTORY_SLOT_BAG_0) << 8 | EQUIPMENT_SLOT_OFFHAND, false) !=  EQUIP_ERR_OK ||
                                CanStoreItem(NULL_BAG, NULL_SLOT, off_dest, offItem, false) !=  EQUIP_ERR_OK))
                                {
                                    return swap ? EQUIP_ERR_ITEMS_CANT_BE_SWAPPED : EQUIP_ERR_INVENTORY_FULL;
                                }
            }
            dest = ((INVENTORY_SLOT_BAG_0 << 8) | eslot);
            return EQUIP_ERR_OK;
        }
    }

    return !swap ? EQUIP_ERR_ITEM_NOT_FOUND : EQUIP_ERR_ITEMS_CANT_BE_SWAPPED;
}

/**
 * @brief Checks whether an equipped or banked item can be unequipped.
 *
 * @param pos The packed item position.
 * @param swap True if the item is being swapped rather than simply removed.
 * @return The inventory result for the unequip check.
 */
InventoryResult Player::CanUnequipItem(uint16 pos, bool swap) const
{
    // Applied only to equipped items and bank bags
    if (!IsEquipmentPos(pos) && !IsBagPos(pos))
    {
        return EQUIP_ERR_OK;
    }

    Item* pItem = GetItemByPos(pos);

    // Applied only to existing equipped item
    if (!pItem)
    {
        return EQUIP_ERR_OK;
    }

    DEBUG_LOG("STORAGE: CanUnequipItem slot = %u, item = %u, count = %u", pos, pItem->GetEntry(), pItem->GetCount());

    ItemPrototype const* pProto = pItem->GetProto();
    if (!pProto)
    {
        return EQUIP_ERR_ITEM_NOT_FOUND;
    }

    // item used
    if (pItem->HasTemporaryLoot())
    {
        return EQUIP_ERR_ALREADY_LOOTED;
    }

    // do not allow unequipping gear except weapons, offhands, projectiles, relics in
    // - combat
    // - in-progress arenas
    if (!pProto->CanChangeEquipStateInCombat())
    {
        if (IsInCombat())
        {
            return EQUIP_ERR_NOT_IN_COMBAT;
        }

        if (BattleGround* bg = GetBattleGround())
            if (bg->isArena() && bg->GetStatus() == STATUS_IN_PROGRESS)
            {
                return EQUIP_ERR_NOT_DURING_ARENA_MATCH;
            }
    }

    // prevent unequip item in process logout
    if (GetSession()->isLogingOut())
    {
        return EQUIP_ERR_YOU_ARE_STUNNED;
    }

    if (!swap && pItem->IsBag() && !((Bag*)pItem)->IsEmpty())
    {
        return EQUIP_ERR_CAN_ONLY_DO_WITH_EMPTY_BAGS;
    }

    return EQUIP_ERR_OK;
}

/**
 * @brief Checks whether an item can be stored in the bank and resolves destinations.
 *
 * @param bag The preferred destination bag, or NULL_BAG for auto-placement.
 * @param slot The preferred destination slot, or NULL_SLOT for auto-placement.
 * @param dest The accumulated destination positions.
 * @param pItem The item to bank.
 * @param swap True to allow swapping with occupied slots.
 * @param not_loading True when validating an active player action instead of load-time state.
 * @return The inventory result for the bank storage check.
 */
InventoryResult Player::CanBankItem(uint8 bag, uint8 slot, ItemPosCountVec& dest, Item* pItem, bool swap, bool not_loading) const
{
    if (!pItem)
    {
        return swap ? EQUIP_ERR_ITEMS_CANT_BE_SWAPPED : EQUIP_ERR_ITEM_NOT_FOUND;
    }

    uint32 count = pItem->GetCount();

    DEBUG_LOG("STORAGE: CanBankItem bag = %u, slot = %u, item = %u, count = %u", bag, slot, pItem->GetEntry(), pItem->GetCount());
    ItemPrototype const* pProto = pItem->GetProto();
    if (!pProto)
    {
        return swap ? EQUIP_ERR_ITEMS_CANT_BE_SWAPPED : EQUIP_ERR_ITEM_NOT_FOUND;
    }

    // item used
    if (pItem->HasTemporaryLoot())
    {
        return EQUIP_ERR_ALREADY_LOOTED;
    }

    if (pItem->IsBindedNotWith(this))
    {
        return EQUIP_ERR_DONT_OWN_THAT_ITEM;
    }

    // check count of items (skip for auto move for same player from bank)
    InventoryResult res = CanTakeMoreSimilarItems(pItem);
    if (res != EQUIP_ERR_OK)
    {
        return res;
    }

    // in specific slot
    if (bag != NULL_BAG && slot != NULL_SLOT)
    {
        if (slot >= BANK_SLOT_BAG_START && slot < BANK_SLOT_BAG_END)
        {
            if (!pItem->IsBag())
            {
                return EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT;
            }

            if (slot - BANK_SLOT_BAG_START >= GetBankBagSlotCount())
            {
                return EQUIP_ERR_MUST_PURCHASE_THAT_BAG_SLOT;
            }

            res = CanUseItem(pItem, not_loading);
            if (res != EQUIP_ERR_OK)
            {
                return res;
            }
        }

        res = _CanStoreItem_InSpecificSlot(bag, slot, dest, pProto, count, swap, pItem);
        if (res != EQUIP_ERR_OK)
        {
            return res;
        }

        if (count == 0)
        {
            return EQUIP_ERR_OK;
        }
    }

    // not specific slot or have space for partly store only in specific slot

    // in specific bag
    if (bag != NULL_BAG)
    {
        if (pProto->InventoryType == INVTYPE_BAG)
        {
            Bag* pBag = (Bag*)pItem;
            if (pBag && !pBag->IsEmpty())
            {
                return EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG;
            }
        }

        // search stack in bag for merge to
        if (pProto->Stackable > 1)
        {
            if (bag == INVENTORY_SLOT_BAG_0)
            {
                res = _CanStoreItem_InInventorySlots(BANK_SLOT_ITEM_START, BANK_SLOT_ITEM_END, dest, pProto, count, true, pItem, bag, slot);
                if (res != EQUIP_ERR_OK)
                {
                    return res;
                }

                if (count == 0)
                {
                    return EQUIP_ERR_OK;
                }
            }
            else
            {
                res = _CanStoreItem_InBag(bag, dest, pProto, count, true, false, pItem, NULL_BAG, slot);
                if (res != EQUIP_ERR_OK)
                {
                    res = _CanStoreItem_InBag(bag, dest, pProto, count, true, true, pItem, NULL_BAG, slot);
                }

                if (res != EQUIP_ERR_OK)
                {
                    return res;
                }

                if (count == 0)
                {
                    return EQUIP_ERR_OK;
                }
            }
        }

        // search free slot in bag
        if (bag == INVENTORY_SLOT_BAG_0)
        {
            res = _CanStoreItem_InInventorySlots(BANK_SLOT_ITEM_START, BANK_SLOT_ITEM_END, dest, pProto, count, false, pItem, bag, slot);
            if (res != EQUIP_ERR_OK)
            {
                return res;
            }

            if (count == 0)
            {
                return EQUIP_ERR_OK;
            }
        }
        else
        {
            res = _CanStoreItem_InBag(bag, dest, pProto, count, false, false, pItem, NULL_BAG, slot);
            if (res != EQUIP_ERR_OK)
            {
                res = _CanStoreItem_InBag(bag, dest, pProto, count, false, true, pItem, NULL_BAG, slot);
            }

            if (res != EQUIP_ERR_OK)
            {
                return res;
            }

            if (count == 0)
            {
                return EQUIP_ERR_OK;
            }
        }
    }

    // not specific bag or have space for partly store only in specific bag

    // search stack for merge to
    if (pProto->Stackable > 1)
    {
        // in slots
        res = _CanStoreItem_InInventorySlots(BANK_SLOT_ITEM_START, BANK_SLOT_ITEM_END, dest, pProto, count, true, pItem, bag, slot);
        if (res != EQUIP_ERR_OK)
        {
            return res;
        }

        if (count == 0)
        {
            return EQUIP_ERR_OK;
        }

        // in special bags
        if (pProto->BagFamily)
        {
            for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
            {
                res = _CanStoreItem_InBag(i, dest, pProto, count, true, false, pItem, bag, slot);
                if (res != EQUIP_ERR_OK)
                {
                    continue;
                }

                if (count == 0)
                {
                    return EQUIP_ERR_OK;
                }
            }
        }

        for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            res = _CanStoreItem_InBag(i, dest, pProto, count, true, true, pItem, bag, slot);
            if (res != EQUIP_ERR_OK)
            {
                continue;
            }

            if (count == 0)
            {
                return EQUIP_ERR_OK;
            }
        }
    }

    // search free place in special bag
    if (pProto->BagFamily)
    {
        for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            res = _CanStoreItem_InBag(i, dest, pProto, count, false, false, pItem, bag, slot);
            if (res != EQUIP_ERR_OK)
            {
                continue;
            }

            if (count == 0)
            {
                return EQUIP_ERR_OK;
            }
        }
    }

    // search free space
    res = _CanStoreItem_InInventorySlots(BANK_SLOT_ITEM_START, BANK_SLOT_ITEM_END, dest, pProto, count, false, pItem, bag, slot);
    if (res != EQUIP_ERR_OK)
    {
        return res;
    }

    if (count == 0)
    {
        return EQUIP_ERR_OK;
    }

    for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
    {
        res = _CanStoreItem_InBag(i, dest, pProto, count, false, true, pItem, bag, slot);
        if (res != EQUIP_ERR_OK)
        {
            continue;
        }

        if (count == 0)
        {
            return EQUIP_ERR_OK;
        }
    }
    return EQUIP_ERR_BANK_FULL;
}

/**
 * @brief Checks whether a specific item instance can currently be used or equipped.
 *
 * @param pItem The item instance to validate.
 * @param direct_action True if the check is for an immediate player action.
 * @return The inventory result for the use check.
 */
InventoryResult Player::CanUseItem(Item* pItem, bool direct_action) const
{
    if (pItem)
    {
        DEBUG_LOG("STORAGE: CanUseItem item = %u", pItem->GetEntry());

        if (!IsAlive() && direct_action)
        {
            return EQUIP_ERR_YOU_ARE_DEAD;
        }

        // if (isStunned())
        //    return EQUIP_ERR_YOU_ARE_STUNNED;

        ItemPrototype const* pProto = pItem->GetProto();
        if (pProto)
        {
            if (pItem->IsBindedNotWith(this))
            {
                return EQUIP_ERR_DONT_OWN_THAT_ITEM;
            }

            InventoryResult msg = CanUseItem(pProto);
            if (msg != EQUIP_ERR_OK)
            {
                return msg;
            }

            if (pItem->GetSkill() != 0)
            {
                if (GetSkillValue(pItem->GetSkill()) == 0)
                {
                    return EQUIP_ERR_NO_REQUIRED_PROFICIENCY;
                }
            }

            if (pProto->RequiredReputationFaction && uint32(GetReputationRank(pProto->RequiredReputationFaction)) < pProto->RequiredReputationRank)
            {
                return EQUIP_ERR_CANT_EQUIP_REPUTATION;
            }

            return EQUIP_ERR_OK;
        }
    }
    return EQUIP_ERR_ITEM_NOT_FOUND;
}

/**
 * @brief Checks whether an item prototype is usable by the player.
 *
 * @param pProto The item prototype to validate.
 * @param direct_action True if the check is for an immediate player action.
 * @return The inventory result for the use check.
 */
InventoryResult Player::CanUseItem(ItemPrototype const* pProto) const
{
    // Used by group, function NeedBeforeGreed, to know if a prototype can be used by a player

    if (pProto)
    {
        if ((pProto->AllowableClass & getClassMask()) == 0 || (pProto->AllowableRace & getRaceMask()) == 0)
        {
            return EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM;
        }

        if (pProto->RequiredSkill != 0)
        {
            if (GetSkillValue(pProto->RequiredSkill) == 0)
            {
                return EQUIP_ERR_NO_REQUIRED_PROFICIENCY;
            }
            else if (GetSkillValue(pProto->RequiredSkill) < pProto->RequiredSkillRank)
            {
                return EQUIP_ERR_CANT_EQUIP_SKILL;
            }
        }

        if (pProto->RequiredSpell != 0 && !HasSpell(pProto->RequiredSpell))
        {
            return EQUIP_ERR_NO_REQUIRED_PROFICIENCY;
        }

        if (getLevel() < pProto->RequiredLevel)
        {
            return EQUIP_ERR_CANT_EQUIP_LEVEL_I;
        }

#ifdef ENABLE_ELUNA
        if (Eluna* e = GetEluna())
        {
            InventoryResult eres = e->OnCanUseItem(this, pProto->ItemId);
            if (eres != EQUIP_ERR_OK)
            {
                return eres;
            }
        }
#endif

        return EQUIP_ERR_OK;
    }
    return EQUIP_ERR_ITEM_NOT_FOUND;
}

/**
 * @brief Checks whether a specific ammo item can be equipped as ammunition.
 *
 * @param item The ammo item entry.
 * @return The inventory result for the ammo check.
 */
InventoryResult Player::CanUseAmmo(uint32 item) const
{
    DEBUG_LOG("STORAGE: CanUseAmmo item = %u", item);
    if (!IsAlive())
    {
        return EQUIP_ERR_YOU_ARE_DEAD;
    }
    // if ( isStunned() )
    //    return EQUIP_ERR_YOU_ARE_STUNNED;
    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(item);
    if (pProto)
    {
        if (pProto->InventoryType != INVTYPE_AMMO)
        {
            return EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE;
        }

        InventoryResult msg = CanUseItem(pProto);
        if (msg != EQUIP_ERR_OK)
        {
            return msg;
        }

        /*if ( GetReputationMgr().GetReputation() < pProto->RequiredReputation )
        return EQUIP_ERR_CANT_EQUIP_REPUTATION;
        */

        // Requires No Ammo
        if (GetDummyAura(46699))
        {
            return EQUIP_ERR_BAG_FULL6;
        }

        return EQUIP_ERR_OK;
    }
    return EQUIP_ERR_ITEM_NOT_FOUND;
}

/**
 * @brief Sets the player's active ammo item and refreshes ranged bonuses.
 *
 * @param item The ammo item entry to equip.
 */
void Player::SetAmmo(uint32 item)
{
    if (!item)
    {
        return;
    }

    // already set
    if (GetUInt32Value(PLAYER_AMMO_ID) == item)
    {
        return;
    }

    // check ammo
    if (item)
    {
        InventoryResult msg = CanUseAmmo(item);
        if (msg != EQUIP_ERR_OK)
        {
            SendEquipError(msg, NULL, NULL, item);
            return;
        }
    }

    SetUInt32Value(PLAYER_AMMO_ID, item);

    _ApplyAmmoBonuses();
}

/**
 * @brief Clears the player's active ammo and removes ranged ammo bonuses.
 */
void Player::RemoveAmmo()
{
    SetUInt32Value(PLAYER_AMMO_ID, 0);

    m_ammoDPS = 0.0f;

    if (CanModifyStats())
    {
        UpdateDamagePhysical(RANGED_ATTACK);
    }
}

// Return stored item (if stored to stack, it can diff. from pItem). And pItem ca be deleted in this case.
Item* Player::StoreNewItem(ItemPosCountVec const& dest, uint32 item, bool update, int32 randomPropertyId)
{
    uint32 count = 0;
    for (ItemPosCountVec::const_iterator itr = dest.begin(); itr != dest.end(); ++itr)
    {
        count += itr->count;
    }

    Item* pItem = Item::CreateItem(item, count, this, randomPropertyId);
    if (pItem)
    {
        ItemAddedQuestCheck(item, count);
        pItem = StoreItem(dest, pItem, update);
    }
    return pItem;
}

/**
 * @brief Stores an item into one or more resolved destination positions.
 *
 * @param dest The destination positions and quantities.
 * @param pItem The item to store.
 * @param update True to send world updates for the storage change.
 * @return The final stored item instance.
 */
Item* Player::StoreItem(ItemPosCountVec const& dest, Item* pItem, bool update)
{
    if (!pItem)
    {
        return NULL;
    }

    Item* lastItem = pItem;

    for (ItemPosCountVec::const_iterator itr = dest.begin(); itr != dest.end();)
    {
        uint16 pos = itr->pos;
        uint32 count = itr->count;

        ++itr;

        if (itr == dest.end())
        {
            lastItem = _StoreItem(pos, pItem, count, false, update);
            break;
        }

        lastItem = _StoreItem(pos, pItem, count, true, update);
    }

    return lastItem;
}

// Return stored item (if stored to stack, it can diff. from pItem). And pItem ca be deleted in this case.
Item* Player::_StoreItem(uint16 pos, Item* pItem, uint32 count, bool clone, bool update)
{
    if (!pItem)
    {
        return NULL;
    }

    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;

    DEBUG_LOG("STORAGE: StoreItem bag = %u, slot = %u, item = %u, count = %u", bag, slot, pItem->GetEntry(), count);

    Item* pItem2 = GetItemByPos(bag, slot);

    if (!pItem2)
    {
        if (clone)
        {
            pItem = pItem->CloneItem(count, this);
        }
        else
        {
            pItem->SetCount(count);
        }

        if (!pItem)
        {
            return NULL;
        }

        ItemPrototype const* itemProto = pItem->GetProto();
        if (itemProto->Bonding == BIND_WHEN_PICKED_UP ||
            itemProto->Bonding == BIND_QUEST_ITEM ||
            (itemProto->Bonding == BIND_WHEN_EQUIPPED && IsBagPos(pos)))
        {
            pItem->SetBinding(true);
        }

        if (bag == INVENTORY_SLOT_BAG_0)
        {
            m_items[slot] = pItem;
            SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), pItem->GetObjectGuid());
            pItem->SetGuidValue(ITEM_FIELD_CONTAINED, GetObjectGuid());
            pItem->SetGuidValue(ITEM_FIELD_OWNER, GetObjectGuid());

            pItem->SetSlot(slot);
            pItem->SetContainer(NULL);

            if (IsInWorld() && update)
            {
                pItem->AddToWorld();
                pItem->SendCreateUpdateToPlayer(this);
            }

            pItem->SetState(ITEM_CHANGED, this);
        }
        else if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag))
        {
            pBag->StoreItem(slot, pItem);
            if (IsInWorld() && update)
            {
                pItem->AddToWorld();
                pItem->SendCreateUpdateToPlayer(this);
            }
            pItem->SetState(ITEM_CHANGED, this);
            pBag->SetState(ITEM_CHANGED, this);
        }

        AddEnchantmentDurations(pItem);
        AddItemDurations(pItem);

        // at place into not appropriate slot (bank, for example) remove aura
        ApplyItemOnStoreSpell(pItem, IsEquipmentPos(pItem->GetBagSlot(), pItem->GetSlot()) || IsInventoryPos(pItem->GetBagSlot(), pItem->GetSlot()));

        return pItem;
    }
    else
    {
        ItemPrototype const* itemProto = pItem2->GetProto();
        if (itemProto->Bonding == BIND_WHEN_PICKED_UP ||
            itemProto->Bonding == BIND_QUEST_ITEM ||
            (itemProto->Bonding == BIND_WHEN_EQUIPPED && IsBagPos(pos)))
        {
            pItem2->SetBinding(true);
        }

        pItem2->SetCount(pItem2->GetCount() + count);
        if (IsInWorld() && update)
        {
            pItem2->SendCreateUpdateToPlayer(this);
        }

        if (!clone)
        {
            // delete item (it not in any slot currently)
            if (IsInWorld() && update)
            {
                pItem->RemoveFromWorld();
                pItem->DestroyForPlayer(this);
            }

            RemoveEnchantmentDurations(pItem);
            RemoveItemDurations(pItem);

            pItem->SetOwnerGuid(GetObjectGuid());           // prevent error at next SetState in case trade/mail/buy from vendor
            pItem->SetState(ITEM_REMOVED, this);
        }

        // AddItemDurations(pItem2); - pItem2 already have duration listed for player
        AddEnchantmentDurations(pItem2);

        pItem2->SetState(ITEM_CHANGED, this);

        return pItem2;
    }
}

/**
 * @brief Creates and equips a new item into a destination slot.
 *
 * @param pos The packed destination position.
 * @param item The item entry to create.
 * @param update True to send world updates for the equip action.
 * @return The equipped item, or null on failure.
 */
Item* Player::EquipNewItem(uint16 pos, uint32 item, bool update)
{
    if (Item* pItem = Item::CreateItem(item, 1, this))
    {
        ItemAddedQuestCheck(item, 1);
        return EquipItem(pos, pItem, update);
    }

    return NULL;
}

/**
 * @brief Equips an item into the specified destination position.
 *
 * @param pos The packed destination position.
 * @param pItem The item to equip.
 * @param update True to send world updates for the equip action.
 * @return The equipped or merged item instance.
 */
Item* Player::EquipItem(uint16 pos, Item* pItem, bool update)
{
    AddEnchantmentDurations(pItem);
    AddItemDurations(pItem);

    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;

    Item* pItem2 = GetItemByPos(bag, slot);

    if (!pItem2)
    {
        VisualizeItem(slot, pItem);

        if (IsAlive())
        {
            ItemPrototype const* pProto = pItem->GetProto();

            // item set bonuses applied only at equip and removed at unequip, and still active for broken items
            if (pProto && pProto->ItemSet)
            {
                AddItemsSetItem(this, pItem);
            }

            _ApplyItemMods(pItem, slot, true);

            ApplyItemOnStoreSpell(pItem, true);

            // Weapons and also Totem/Relic/Sigil/etc
            if (pProto && IsInCombat() && (pProto->Class == ITEM_CLASS_WEAPON || pProto->InventoryType == INVTYPE_RELIC) && m_weaponChangeTimer == 0)
            {
                uint32 cooldownSpell = SPELL_ID_WEAPON_SWITCH_COOLDOWN_1_5s;

                if (getClass() == CLASS_ROGUE)
                {
                    cooldownSpell = SPELL_ID_WEAPON_SWITCH_COOLDOWN_1_0s;
                }

                SpellEntry const* spellProto = sSpellStore.LookupEntry(cooldownSpell);

                if (!spellProto)
                {
                    sLog.outError("Weapon switch cooldown spell %u couldn't be found in Spell.dbc", cooldownSpell);
                }
                else
                {
                    m_weaponChangeTimer = spellProto->StartRecoveryTime;

                    WorldPacket data(SMSG_SPELL_COOLDOWN, 8 + 1 + 4);
                    data << GetObjectGuid();
                    data << uint8(1);
                    data << uint32(cooldownSpell);
                    data << uint32(0);
                    GetSession()->SendPacket(&data);
                }
            }
        }

        if (IsInWorld() && update)
        {
            pItem->AddToWorld();
            pItem->SendCreateUpdateToPlayer(this);
        }

        ApplyEquipCooldown(pItem);

        if (slot == EQUIPMENT_SLOT_MAINHAND)
        {
            UpdateExpertise(BASE_ATTACK);
        }
        else if (slot == EQUIPMENT_SLOT_OFFHAND)
        {
            UpdateExpertise(OFF_ATTACK);
        }
    }
    else
    {
        pItem2->SetCount(pItem2->GetCount() + pItem->GetCount());
        if (IsInWorld() && update)
        {
            pItem2->SendCreateUpdateToPlayer(this);
        }

        // delete item (it not in any slot currently)
        // pItem->DeleteFromDB();
        if (IsInWorld() && update)
        {
            pItem->RemoveFromWorld();
            pItem->DestroyForPlayer(this);
        }

        RemoveEnchantmentDurations(pItem);
        RemoveItemDurations(pItem);

        pItem->SetOwnerGuid(GetObjectGuid());               // prevent error at next SetState in case trade/mail/buy from vendor
        pItem->SetState(ITEM_REMOVED, this);
        pItem2->SetState(ITEM_CHANGED, this);

        ApplyEquipCooldown(pItem2);

        // Used by Eluna
#ifdef ENABLE_ELUNA
        if (Eluna* e = GetEluna())
        {
            e->OnEquip(this, pItem2, bag, slot); // This is depricated and will be removed in the future
            e->OnItemEquip(this, pItem2, slot);
        }
#endif /* ENABLE_ELUNA */

        return pItem2;
    }
    // Used by Eluna
#ifdef ENABLE_ELUNA
    if (Eluna* e = GetEluna())
    {
        e->OnEquip(this, pItem, bag, slot); // This is depricated and will be removed in the future
        e->OnItemEquip(this, pItem, slot);
    }
#endif /* ENABLE_ELUNA */

    return pItem;
}

/**
 * @brief Equips an item without full validation, typically during loading.
 *
 * @param pos The packed destination position.
 * @param pItem The item to equip.
 */
void Player::QuickEquipItem(uint16 pos, Item* pItem)
{
    if (pItem)
    {
        AddEnchantmentDurations(pItem);
        AddItemDurations(pItem);
        ApplyItemOnStoreSpell(pItem, true);

        uint8 slot = pos & 255;
        VisualizeItem(slot, pItem);

        if (IsInWorld())
        {
            pItem->AddToWorld();
            pItem->SendCreateUpdateToPlayer(this);
        }
    }
}

/**
 * @brief Updates the visible equipment fields for an equipment slot.
 *
 * @param slot The equipment slot to update.
 * @param pItem The item to display, or null to clear the slot.
 */
void Player::SetVisibleItemSlot(uint8 slot, Item* pItem)
{
    if (pItem)
    {
        SetGuidValue(PLAYER_VISIBLE_ITEM_1_CREATOR + (slot * MAX_VISIBLE_ITEM_OFFSET), pItem->GetGuidValue(ITEM_FIELD_CREATOR));

        int VisibleBase = PLAYER_VISIBLE_ITEM_1_0 + (slot * MAX_VISIBLE_ITEM_OFFSET);
        SetUInt32Value(VisibleBase + 0, pItem->GetEntry());

        for (int i = 0; i < MAX_INSPECTED_ENCHANTMENT_SLOT; ++i)
        {
            SetUInt32Value(VisibleBase + 1 + i, pItem->GetEnchantmentId(EnchantmentSlot(i)));
        }

        // Use SetInt16Value to prevent set high part to FFFF for negative value
        SetInt16Value(PLAYER_VISIBLE_ITEM_1_PROPERTIES + (slot * MAX_VISIBLE_ITEM_OFFSET), 0, pItem->GetItemRandomPropertyId());
        SetUInt32Value(PLAYER_VISIBLE_ITEM_1_PROPERTIES + 1 + (slot * MAX_VISIBLE_ITEM_OFFSET), pItem->GetItemSuffixFactor());
    }
    else
    {
        SetGuidValue(PLAYER_VISIBLE_ITEM_1_CREATOR + (slot * MAX_VISIBLE_ITEM_OFFSET), ObjectGuid());

        int VisibleBase = PLAYER_VISIBLE_ITEM_1_0 + (slot * MAX_VISIBLE_ITEM_OFFSET);
        SetUInt32Value(VisibleBase + 0, 0);

        for (int i = 0; i < MAX_INSPECTED_ENCHANTMENT_SLOT; ++i)
        {
            SetUInt32Value(VisibleBase + 1 + i, 0);
        }

        SetUInt32Value(PLAYER_VISIBLE_ITEM_1_PROPERTIES + 0 + (slot * MAX_VISIBLE_ITEM_OFFSET), 0);
        SetUInt32Value(PLAYER_VISIBLE_ITEM_1_PROPERTIES + 1 + (slot * MAX_VISIBLE_ITEM_OFFSET), 0);
    }
}

/**
 * @brief Places an item into an equipment slot and updates visible state.
 *
 * @param slot The destination equipment slot.
 * @param pItem The item to visualize.
 */
void Player::VisualizeItem(uint8 slot, Item* pItem)
{
    if (!pItem)
    {
        return;
    }

    // check also  BIND_WHEN_PICKED_UP and BIND_QUEST_ITEM for .additem or .additemset case by GM (not binded at adding to inventory)
    ItemPrototype const* itemProto = pItem->GetProto();
    if (itemProto->Bonding == BIND_WHEN_EQUIPPED || itemProto->Bonding == BIND_WHEN_PICKED_UP || itemProto->Bonding == BIND_QUEST_ITEM)
    {
        pItem->SetBinding(true);
    }

    DEBUG_LOG("STORAGE: EquipItem slot = %u, item = %u", slot, pItem->GetEntry());

    m_items[slot] = pItem;
    SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), pItem->GetObjectGuid());
    pItem->SetGuidValue(ITEM_FIELD_CONTAINED, GetObjectGuid());
    pItem->SetGuidValue(ITEM_FIELD_OWNER, GetObjectGuid());
    pItem->SetSlot(slot);
    pItem->SetContainer(NULL);

    if (slot < EQUIPMENT_SLOT_END)
    {
        SetVisibleItemSlot(slot, pItem);
    }

    pItem->SetState(ITEM_CHANGED, this);
}

/**
 * @brief Temporarily removes an item from player storage without deleting it.
 *
 * @param bag The bag containing the item.
 * @param slot The slot containing the item.
 * @param update True to send inventory updates to the client.
 */
void Player::RemoveItem(uint8 bag, uint8 slot, bool update)
{
    // note: removeitem does not actually change the item
    // it only takes the item out of storage temporarily
    // note2: if removeitem is to be used for delinking
    // the item must be removed from the player's updatequeue

    Item* pItem = GetItemByPos(bag, slot);
    if (pItem)
    {
        DEBUG_LOG("STORAGE: RemoveItem bag = %u, slot = %u, item = %u", bag, slot, pItem->GetEntry());

        RemoveEnchantmentDurations(pItem);
        RemoveItemDurations(pItem);

        if (bag == INVENTORY_SLOT_BAG_0)
        {
            if (slot < INVENTORY_SLOT_BAG_END)
            {
                ItemPrototype const* pProto = pItem->GetProto();
                // item set bonuses applied only at equip and removed at unequip, and still active for broken items

                if (pProto && pProto->ItemSet)
                {
                    RemoveItemsSetItem(this, pProto);
                }

                _ApplyItemMods(pItem, slot, false);

                // remove item dependent auras and casts (only weapon and armor slots)
                if (slot < EQUIPMENT_SLOT_END)
                {
                    RemoveItemDependentAurasAndCasts(pItem);

                    // remove held enchantments, update expertise
                    if (slot == EQUIPMENT_SLOT_MAINHAND)
                    {
                        if (pItem->GetItemSuffixFactor())
                        {
                            pItem->ClearEnchantment(PROP_ENCHANTMENT_SLOT_3);
                            pItem->ClearEnchantment(PROP_ENCHANTMENT_SLOT_4);
                        }
                        else
                        {
                            pItem->ClearEnchantment(PROP_ENCHANTMENT_SLOT_0);
                            pItem->ClearEnchantment(PROP_ENCHANTMENT_SLOT_1);
                        }

                        UpdateExpertise(BASE_ATTACK);
                    }
                    else if (slot == EQUIPMENT_SLOT_OFFHAND)
                    {
                        UpdateExpertise(OFF_ATTACK);
                    }
                }
            }

            m_items[slot] = NULL;
            SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), ObjectGuid());

            if (slot < EQUIPMENT_SLOT_END)
            {
                SetVisibleItemSlot(slot, NULL);
            }
        }
        else
        {
            Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
            if (pBag)
            {
                pBag->RemoveItem(slot);
            }
        }
        pItem->SetGuidValue(ITEM_FIELD_CONTAINED, ObjectGuid());
        // pItem->SetGuidValue(ITEM_FIELD_OWNER, ObjectGuid()); not clear owner at remove (it will be set at store). This used in mail and auction code
        pItem->SetSlot(NULL_SLOT);

        // ApplyItemOnStoreSpell, for avoid re-apply will remove at _adding_ to not appropriate slot

        if (IsInWorld() && update)
        {
            pItem->SendCreateUpdateToPlayer(this);
        }
    }
}

// Common operation need to remove item from inventory without delete in trade, auction, guild bank, mail....
void Player::MoveItemFromInventory(uint8 bag, uint8 slot, bool update)
{
    if (Item* it = GetItemByPos(bag, slot))
    {
        ItemRemovedQuestCheck(it->GetEntry(), it->GetCount());
        RemoveItem(bag, slot, update);

        // item atStore spell not removed in RemoveItem (for avoid reappaly in slots changes), so do it directly
        if (IsEquipmentPos(bag, slot) || IsInventoryPos(bag, slot))
        {
            ApplyItemOnStoreSpell(it, false);
        }

        it->RemoveFromUpdateQueueOf(this);
        if (it->IsInWorld())
        {
            it->RemoveFromWorld();
            it->DestroyForPlayer(this);
        }
    }
}

// Common operation need to add item from inventory without delete in trade, guild bank, mail....
void Player::MoveItemToInventory(ItemPosCountVec const& dest, Item* pItem, bool update, bool in_characterInventoryDB)
{
    // update quest counters
    ItemAddedQuestCheck(pItem->GetEntry(), pItem->GetCount());

    // store item
    Item* pLastItem = StoreItem(dest, pItem, update);

    // only set if not merged to existing stack (pItem can be deleted already but we can compare pointers any way)
    if (pLastItem == pItem)
    {
        // update owner for last item (this can be original item with wrong owner
        if (pLastItem->GetOwnerGuid() != GetObjectGuid())
        {
            pLastItem->SetOwnerGuid(GetObjectGuid());
        }

        // if this original item then it need create record in inventory
        // in case trade we already have item in other player inventory
        pLastItem->SetState(in_characterInventoryDB ? ITEM_CHANGED : ITEM_NEW, this);
    }
}

/**
 * @brief Permanently destroys an item from player storage.
 *
 * @param bag The bag containing the item.
 * @param slot The slot containing the item.
 * @param update True to send inventory updates to the client.
 */
void Player::DestroyItem(uint8 bag, uint8 slot, bool update)
{
    Item* pItem = GetItemByPos(bag, slot);
    if (pItem)
    {
        DEBUG_LOG("STORAGE: DestroyItem bag = %u, slot = %u, item = %u", bag, slot, pItem->GetEntry());

        // start from destroy contained items (only equipped bag can have its)
        if (pItem->IsBag() && pItem->IsEquipped())          // this also prevent infinity loop if empty bag stored in bag==slot
        {
            for (int i = 0; i < MAX_BAG_SIZE; ++i)
            {
                DestroyItem(slot, i, update);
            }
        }

        if (pItem->HasFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_WRAPPED))
        {
            static SqlStatementID delGifts ;

            SqlStatement stmt = CharacterDatabase.CreateStatement(delGifts, "DELETE FROM `character_gifts` WHERE `item_guid` = ?");
            stmt.PExecute(pItem->GetGUIDLow());
        }

        RemoveEnchantmentDurations(pItem);
        RemoveItemDurations(pItem);

        if (IsEquipmentPos(bag, slot) || IsInventoryPos(bag, slot))
        {
            ApplyItemOnStoreSpell(pItem, false);
        }

        ItemRemovedQuestCheck(pItem->GetEntry(), pItem->GetCount());
#ifdef ENABLE_ELUNA
        if (Eluna* e = GetEluna())
        {
            e->OnRemove(this, pItem);
        }
#endif /* ENABLE_ELUNA */

        if (bag == INVENTORY_SLOT_BAG_0)
        {
            SetGuidValue(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2), ObjectGuid());

            // equipment and equipped bags can have applied bonuses
            if (slot < INVENTORY_SLOT_BAG_END)
            {
                ItemPrototype const* pProto = pItem->GetProto();

                // item set bonuses applied only at equip and removed at unequip, and still active for broken items
                if (pProto && pProto->ItemSet)
                {
                    RemoveItemsSetItem(this, pProto);
                }

                _ApplyItemMods(pItem, slot, false);
            }

            if (slot < EQUIPMENT_SLOT_END)
            {
                // remove item dependent auras and casts (only weapon and armor slots)
                RemoveItemDependentAurasAndCasts(pItem);

                // update expertise
                if (slot == EQUIPMENT_SLOT_MAINHAND)
                {
                    UpdateExpertise(BASE_ATTACK);
                }
                else if (slot == EQUIPMENT_SLOT_OFFHAND)
                {
                    UpdateExpertise(OFF_ATTACK);
                }

                // equipment visual show
                SetVisibleItemSlot(slot, NULL);
            }

            m_items[slot] = NULL;
        }
        else if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, bag))
        {
            pBag->RemoveItem(slot);
        }

        if (IsInWorld() && update)
        {
            pItem->RemoveFromWorld();
            pItem->DestroyForPlayer(this);
        }

        // pItem->SetOwnerGUID(0);
        pItem->SetGuidValue(ITEM_FIELD_CONTAINED, ObjectGuid());
        pItem->SetSlot(NULL_SLOT);
        pItem->SetState(ITEM_REMOVED, this);
    }
}

/**
 * @brief Destroys up to a requested count of an item across player storage.
 *
 * @param item The item entry to destroy.
 * @param count The requested quantity to destroy.
 * @param update True to send inventory updates to the client.
 * @param unequip_check True to validate equipped items before destroying them.
 * @param delete_from_bank True to include bank storage in the search.
 * @param delete_from_buyback True to include vendor buyback slots in the search.
 * @return The number of items removed.
 */
uint32 Player::DestroyItemCount(uint32 item, uint32 count, bool update, bool unequip_check, bool delete_from_bank, bool delete_from_buyback)
{
    DEBUG_LOG("STORAGE: DestroyItemCount item = %u, count = %u", item, count);
    uint32 remcount = 0;

    // Search in default bagpack
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pItem->GetEntry() == item && !pItem->IsInTrade())
            {
                if (pItem->GetCount() + remcount <= count)
                {
                    // all items in inventory can unequipped
                    remcount += pItem->GetCount();
                    DestroyItem(INVENTORY_SLOT_BAG_0, i, update);

                    if (remcount >= count)
                    {
                        return remcount;
                    }
                }
                else
                {
                    ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                    pItem->SetCount(pItem->GetCount() - count + remcount);
                    if (IsInWorld() && update)
                    {
                        pItem->SendCreateUpdateToPlayer(this);
                    }
                    pItem->SetState(ITEM_CHANGED, this);
                    return remcount;
                }
            }
        }
    }

    // Search in keyring slots
    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pItem->GetEntry() == item && !pItem->IsInTrade())
            {
                if (pItem->GetCount() + remcount <= count)
                {
                    // all keys can be unequipped
                    remcount += pItem->GetCount();
                    DestroyItem(INVENTORY_SLOT_BAG_0, i, update);

                    if (remcount >= count)
                    {
                        return remcount;
                    }
                }
                else
                {
                    ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                    pItem->SetCount(pItem->GetCount() - count + remcount);
                    if (IsInWorld() && update)
                    {
                        pItem->SendCreateUpdateToPlayer(this);
                    }
                    pItem->SetState(ITEM_CHANGED, this);
                    return remcount;
                }
            }
        }
    }

    // Search in inventory bags
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                {
                    if (pItem->GetEntry() == item && !pItem->IsInTrade())
                    {
                        // all items in bags can be unequipped
                        if (pItem->GetCount() + remcount <= count)
                        {
                            remcount += pItem->GetCount();
                            DestroyItem(i, j, update);

                            if (remcount >= count)
                            {
                                return remcount;
                            }
                        }
                        else
                        {
                            ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                            pItem->SetCount(pItem->GetCount() - count + remcount);
                            if (IsInWorld() && update)
                            {
                                pItem->SendCreateUpdateToPlayer(this);
                            }
                            pItem->SetState(ITEM_CHANGED, this);
                            return remcount;
                        }
                    }
                }
            }
        }
    }

    // Search in Equiped items
    for (int i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
        {
            if (pItem && pItem->GetEntry() == item && !pItem->IsInTrade())
            {
                if (pItem->GetCount() + remcount <= count)
                {
                    if (!unequip_check || CanUnequipItem(INVENTORY_SLOT_BAG_0 << 8 | i, false) == EQUIP_ERR_OK)
                    {
                        remcount += pItem->GetCount();
                        DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
                        if (remcount >= count)
                        {
                            return remcount;
                        }
                    }
                }
                else
                {
                    ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                    pItem->SetCount(pItem->GetCount() - count + remcount);
                    if (IsInWorld() && update)
                    {
                        pItem->SendCreateUpdateToPlayer(this);
                    }
                    pItem->SetState(ITEM_CHANGED, this);
                    return remcount;
                }
            }
        }
    }

    // Search in bank items
    if (delete_from_bank)
    {
        // Normal bank slots
        for (int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
        {
            if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                if (pItem->GetEntry() == item && !pItem->IsInTrade())
                {
                    if (pItem->GetCount() + remcount <= count)
                    {
                        // all items in inventory can unequipped
                        remcount += pItem->GetCount();
                        DestroyItem(INVENTORY_SLOT_BAG_0, i, update);

                        if (remcount >= count)
                        {
                            return remcount;
                        }
                    }
                    else
                    {
                        ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                        pItem->SetCount(pItem->GetCount() - count + remcount);
                        if (IsInWorld() && update)
                        {
                            pItem->SendCreateUpdateToPlayer(this);
                        }
                        pItem->SetState(ITEM_CHANGED, this);
                        return remcount;
                    }
                }
            }
        }

        // Bank bagslots
        for (int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    if (Item* pItem = pBag->GetItemByPos(j))
                    {
                        if (pItem->GetEntry() == item && !pItem->IsInTrade())
                        {
                            // all items in bags can be unequipped
                            if (pItem->GetCount() + remcount <= count)
                            {
                                remcount += pItem->GetCount();
                                DestroyItem(i, j, update);

                                if (remcount >= count)
                                {
                                    return remcount;
                                }
                            }
                            else
                            {
                                ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                                pItem->SetCount(pItem->GetCount() - count + remcount);
                                if (IsInWorld() && update)
                                {
                                    pItem->SendCreateUpdateToPlayer(this);
                                }
                                pItem->SetState(ITEM_CHANGED, this);
                                return remcount;
                            }
                        }
                    }
                }
            }
        }
    }

    // Search in buyback npcs vendor tab
    if (delete_from_buyback)
    {
        for (int i = BUYBACK_SLOT_START; i < BUYBACK_SLOT_END; ++i)
        {
            if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                if (pItem->GetEntry() == item && !pItem->IsInTrade())
                {
                    if (pItem->GetCount() + remcount <= count)
                    {
                        // all keys can be unequipped
                        remcount += pItem->GetCount();
                        DestroyItem(INVENTORY_SLOT_BAG_0, i, update);

                        if (remcount >= count)
                        {
                            return remcount;
                        }
                    }
                    else
                    {
                        ItemRemovedQuestCheck(pItem->GetEntry(), count - remcount);
                        pItem->SetCount(pItem->GetCount() - count + remcount);
                        if (IsInWorld() && update)
                        {
                            pItem->SendCreateUpdateToPlayer(this);
                        }
                        pItem->SetState(ITEM_CHANGED, this);
                        return remcount;
                    }
                }
            }
        }
    }

    return remcount;
}

/**
 * @brief Destroys items that are no longer valid in the player's current zone or map.
 *
 * @param update True to send inventory updates to the client.
 * @param new_zone The zone identifier to validate against.
 */
void Player::DestroyZoneLimitedItem(bool update, uint32 new_zone)
{
    DEBUG_LOG("STORAGE: DestroyZoneLimitedItem in map %u and area %u", GetMapId(), new_zone);

    // in inventory
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsLimitedToAnotherMapOrZone(GetMapId(), new_zone))
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }

    for (int i = KEYRING_SLOT_START; i < KEYRING_SLOT_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsLimitedToAnotherMapOrZone(GetMapId(), new_zone))
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }

    // in inventory bags
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                    if (pItem->IsLimitedToAnotherMapOrZone(GetMapId(), new_zone))
                    {
                        DestroyItem(i, j, update);
                    }
            }
    }

    // in equipment and bag list
    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsLimitedToAnotherMapOrZone(GetMapId(), new_zone))
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }
}

/**
 * @brief Destroys conjured consumables from player storage.
 *
 * @param update True to send inventory updates to the client.
 */
void Player::DestroyConjuredItems(bool update)
{
    // used when entering arena
    // destroys all conjured items
    DEBUG_LOG("STORAGE: DestroyConjuredItems");

    // in inventory
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsConjuredConsumable())
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }

    // in inventory bags
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                    if (pItem->IsConjuredConsumable())
                    {
                        DestroyItem(i, j, update);
                    }
            }
    }

    // in equipment and bag list
    for (int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->IsConjuredConsumable())
            {
                DestroyItem(INVENTORY_SLOT_BAG_0, i, update);
            }
    }
}

/**
 * @brief Destroys or decrements a specific item instance by a requested count.
 *
 * @param pItem The item instance to modify.
 * @param count The remaining quantity to destroy; updated by the call.
 * @param update True to send inventory updates to the client.
 */
void Player::DestroyItemCount(Item* pItem, uint32& count, bool update)
{
    if (!pItem)
    {
        return;
    }

    DEBUG_LOG("STORAGE: DestroyItemCount item (GUID: %u, Entry: %u) count = %u", pItem->GetGUIDLow(), pItem->GetEntry(), count);

    if (pItem->GetCount() <= count)
    {
        count -= pItem->GetCount();

        DestroyItem(pItem->GetBagSlot(), pItem->GetSlot(), update);
    }
    else
    {
        ItemRemovedQuestCheck(pItem->GetEntry(), count);
        pItem->SetCount(pItem->GetCount() - count);
        count = 0;
        if (IsInWorld() && update)
        {
            pItem->SendCreateUpdateToPlayer(this);
        }
        pItem->SetState(ITEM_CHANGED, this);
    }
}

/**
 * @brief Splits a stack into a new destination position.
 *
 * @param src The packed source position.
 * @param dst The packed destination position.
 * @param count The quantity to split from the source stack.
 */
void Player::SplitItem(uint16 src, uint16 dst, uint32 count)
{
    uint8 srcbag = src >> 8;
    uint8 srcslot = src & 255;

    uint8 dstbag = dst >> 8;
    uint8 dstslot = dst & 255;

    Item* pSrcItem = GetItemByPos(srcbag, srcslot);
    if (!pSrcItem)
    {
        SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pSrcItem, NULL);
        return;
    }

    if (pSrcItem->HasGeneratedLoot())                       // prevent split looting item (stackable items can has only temporary loot and this meaning that loot window open)
    {
        // best error message found for attempting to split while looting
        SendEquipError(EQUIP_ERR_COULDNT_SPLIT_ITEMS, pSrcItem, NULL);
        return;
    }

    // not let split all items (can be only at cheating)
    if (pSrcItem->GetCount() == count)
    {
        SendEquipError(EQUIP_ERR_COULDNT_SPLIT_ITEMS, pSrcItem, NULL);
        return;
    }

    // not let split more existing items (can be only at cheating)
    if (pSrcItem->GetCount() < count)
    {
        SendEquipError(EQUIP_ERR_TRIED_TO_SPLIT_MORE_THAN_COUNT, pSrcItem, NULL);
        return;
    }

    DEBUG_LOG("STORAGE: SplitItem bag = %u, slot = %u, item = %u, count = %u", dstbag, dstslot, pSrcItem->GetEntry(), count);
    Item* pNewItem = pSrcItem->CloneItem(count, this);
    if (!pNewItem)
    {
        SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pSrcItem, NULL);
        return;
    }

    if (IsInventoryPos(dst))
    {
        // change item amount before check (for unique max count check)
        pSrcItem->SetCount(pSrcItem->GetCount() - count);

        ItemPosCountVec dest;
        InventoryResult msg = CanStoreItem(dstbag, dstslot, dest, pNewItem, false);
        if (msg != EQUIP_ERR_OK)
        {
            delete pNewItem;
            pSrcItem->SetCount(pSrcItem->GetCount() + count);
            SendEquipError(msg, pSrcItem, NULL);
            return;
        }

        if (IsInWorld())
        {
            pSrcItem->SendCreateUpdateToPlayer(this);
        }
        pSrcItem->SetState(ITEM_CHANGED, this);
        StoreItem(dest, pNewItem, true);
    }
    else if (IsBankPos(dst))
    {
        // change item amount before check (for unique max count check)
        pSrcItem->SetCount(pSrcItem->GetCount() - count);

        ItemPosCountVec dest;
        InventoryResult msg = CanBankItem(dstbag, dstslot, dest, pNewItem, false);
        if (msg != EQUIP_ERR_OK)
        {
            delete pNewItem;
            pSrcItem->SetCount(pSrcItem->GetCount() + count);
            SendEquipError(msg, pSrcItem, NULL);
            return;
        }

        if (IsInWorld())
        {
            pSrcItem->SendCreateUpdateToPlayer(this);
        }
        pSrcItem->SetState(ITEM_CHANGED, this);
        BankItem(dest, pNewItem, true);
    }
    else if (IsEquipmentPos(dst))
    {
        // change item amount before check (for unique max count check), provide space for splitted items
        pSrcItem->SetCount(pSrcItem->GetCount() - count);

        uint16 dest;
        InventoryResult msg = CanEquipItem(dstslot, dest, pNewItem, false);
        if (msg != EQUIP_ERR_OK)
        {
            delete pNewItem;
            pSrcItem->SetCount(pSrcItem->GetCount() + count);
            SendEquipError(msg, pSrcItem, NULL);
            return;
        }

        if (IsInWorld())
        {
            pSrcItem->SendCreateUpdateToPlayer(this);
        }
        pSrcItem->SetState(ITEM_CHANGED, this);
        EquipItem(dest, pNewItem, true);
        AutoUnequipOffhandIfNeed();
    }
}

/**
 * @brief Moves, merges, or swaps items between two storage positions.
 *
 * @param src The packed source position.
 * @param dst The packed destination position.
 */
void Player::SwapItem(uint16 src, uint16 dst)
{
    uint8 srcbag = src >> 8;
    uint8 srcslot = src & 255;

    uint8 dstbag = dst >> 8;
    uint8 dstslot = dst & 255;

    Item* pSrcItem = GetItemByPos(srcbag, srcslot);
    Item* pDstItem = GetItemByPos(dstbag, dstslot);

    if (!pSrcItem)
    {
        return;
    }

    DEBUG_LOG("STORAGE: SwapItem bag = %u, slot = %u, item = %u", dstbag, dstslot, pSrcItem->GetEntry());

    if (!IsAlive())
    {
        SendEquipError(EQUIP_ERR_YOU_ARE_DEAD, pSrcItem, pDstItem);
        return;
    }

    // SRC checks

    // check unequip potability for equipped items and bank bags
    if (IsEquipmentPos(src) || IsBagPos(src))
    {
        // bags can be swapped with empty bag slots, or with empty bag (items move possibility checked later)
        InventoryResult msg = CanUnequipItem(src, !IsBagPos(src) || IsBagPos(dst) || (pDstItem && pDstItem->IsBag() && ((Bag*)pDstItem)->IsEmpty()));
        if (msg != EQUIP_ERR_OK)
        {
            SendEquipError(msg, pSrcItem, pDstItem);
            return;
        }
    }

    // prevent put equipped/bank bag in self
    if (IsBagPos(src) && srcslot == dstbag)
    {
        SendEquipError(EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG, pSrcItem, pDstItem);
        return;
    }

    // prevent put equipped/bank bag in self
    if (IsBagPos(dst) && dstslot == srcbag)
    {
        SendEquipError(EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG, pDstItem, pSrcItem);
        return;
    }

    // DST checks

    if (pDstItem)
    {
        // check unequip potability for equipped items and bank bags
        if (IsEquipmentPos(dst) || IsBagPos(dst))
        {
            // bags can be swapped with empty bag slots, or with empty bag (items move possibility checked later)
            InventoryResult msg = CanUnequipItem(dst, !IsBagPos(dst) || IsBagPos(src) || (pSrcItem->IsBag() && ((Bag*)pSrcItem)->IsEmpty()));
            if (msg != EQUIP_ERR_OK)
            {
                SendEquipError(msg, pSrcItem, pDstItem);
                return;
            }
        }
    }

    // NOW this is or item move (swap with empty), or swap with another item (including bags in bag possitions)
    // or swap empty bag with another empty or not empty bag (with items exchange)

    // Move case
    if (!pDstItem)
    {
        if (IsInventoryPos(dst))
        {
            ItemPosCountVec dest;
            InventoryResult msg = CanStoreItem(dstbag, dstslot, dest, pSrcItem, false);
            if (msg != EQUIP_ERR_OK)
            {
                SendEquipError(msg, pSrcItem, NULL);
                return;
            }

            RemoveItem(srcbag, srcslot, true);
            StoreItem(dest, pSrcItem, true);
        }
        else if (IsBankPos(dst))
        {
            ItemPosCountVec dest;
            InventoryResult msg = CanBankItem(dstbag, dstslot, dest, pSrcItem, false);
            if (msg != EQUIP_ERR_OK)
            {
                SendEquipError(msg, pSrcItem, NULL);
                return;
            }

            RemoveItem(srcbag, srcslot, true);
            BankItem(dest, pSrcItem, true);
        }
        else if (IsEquipmentPos(dst))
        {
            uint16 dest;
            InventoryResult msg = CanEquipItem(dstslot, dest, pSrcItem, false);
            if (msg != EQUIP_ERR_OK)
            {
                SendEquipError(msg, pSrcItem, NULL);
                return;
            }

            RemoveItem(srcbag, srcslot, true);
            EquipItem(dest, pSrcItem, true);
            AutoUnequipOffhandIfNeed();
        }

        return;
    }

    // attempt merge to / fill target item
    if (!pSrcItem->IsBag() && !pDstItem->IsBag())
    {
        InventoryResult msg;
        ItemPosCountVec sDest;
        uint16 eDest;
        if (IsInventoryPos(dst))
        {
            msg = CanStoreItem(dstbag, dstslot, sDest, pSrcItem, false);
        }
        else if (IsBankPos(dst))
        {
            msg = CanBankItem(dstbag, dstslot, sDest, pSrcItem, false);
        }
        else if (IsEquipmentPos(dst))
        {
            msg = CanEquipItem(dstslot, eDest, pSrcItem, false);
        }
        else
        {
            return;
        }

        // can be merge/fill
        if (msg == EQUIP_ERR_OK)
        {
            ItemPrototype const* itemProto = pSrcItem->GetProto();
            if (pSrcItem->GetCount() + pDstItem->GetCount() <= itemProto->GetMaxStackSize())
            {
                RemoveItem(srcbag, srcslot, true);

                if (IsInventoryPos(dst))
                {
                    StoreItem(sDest, pSrcItem, true);
                }
                else if (IsBankPos(dst))
                {
                    BankItem(sDest, pSrcItem, true);
                }
                else if (IsEquipmentPos(dst))
                {
                    EquipItem(eDest, pSrcItem, true);
                    AutoUnequipOffhandIfNeed();
                }
            }
            else
            {
                pSrcItem->SetCount(pSrcItem->GetCount() + pDstItem->GetCount() - itemProto->GetMaxStackSize());
                pDstItem->SetCount(itemProto->GetMaxStackSize());
                pSrcItem->SetState(ITEM_CHANGED, this);
                pDstItem->SetState(ITEM_CHANGED, this);
                if (IsInWorld())
                {
                    pSrcItem->SendCreateUpdateToPlayer(this);
                    pDstItem->SendCreateUpdateToPlayer(this);
                }
            }
            return;
        }
    }

    // impossible merge/fill, do real swap
    InventoryResult msg = EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM; // Initialize msg with a default value which will never be used

    // check src->dest move possibility
    ItemPosCountVec sDest;
    uint16 eDest = 0;
    if (IsInventoryPos(dst))
    {
        msg = CanStoreItem(dstbag, dstslot, sDest, pSrcItem, true);
    }
    else if (IsBankPos(dst))
    {
        msg = CanBankItem(dstbag, dstslot, sDest, pSrcItem, true);
    }
    else if (IsEquipmentPos(dst))
    {
        msg = CanEquipItem(dstslot, eDest, pSrcItem, true);
        if (msg == EQUIP_ERR_OK)
        {
            msg = CanUnequipItem(eDest, true);
        }
    }

    if (msg != EQUIP_ERR_OK)
    {
        SendEquipError(msg, pSrcItem, pDstItem);
        return;
    }

    // check dest->src move possibility
    ItemPosCountVec sDest2;
    uint16 eDest2 = 0;
    if (IsInventoryPos(src))
    {
        msg = CanStoreItem(srcbag, srcslot, sDest2, pDstItem, true);
    }
    else if (IsBankPos(src))
    {
        msg = CanBankItem(srcbag, srcslot, sDest2, pDstItem, true);
    }
    else if (IsEquipmentPos(src))
    {
        msg = CanEquipItem(srcslot, eDest2, pDstItem, true);
        if (msg == EQUIP_ERR_OK)
        {
            msg = CanUnequipItem(eDest2, true);
        }
    }

    if (msg != EQUIP_ERR_OK)
    {
        SendEquipError(msg, pDstItem, pSrcItem);
        return;
    }

    // Check bag swap with item exchange (one from empty in not bag possition (equipped (not possible in fact) or store)
    if (pSrcItem->IsBag() && pDstItem->IsBag())
    {
        Bag* emptyBag = NULL;
        Bag* fullBag = NULL;
        if (((Bag*)pSrcItem)->IsEmpty() && !IsBagPos(src))
        {
            emptyBag = (Bag*)pSrcItem;
            fullBag  = (Bag*)pDstItem;
        }
        else if (((Bag*)pDstItem)->IsEmpty() && !IsBagPos(dst))
        {
            emptyBag = (Bag*)pDstItem;
            fullBag  = (Bag*)pSrcItem;
        }

        // bag swap (with items exchange) case
        if (emptyBag && fullBag)
        {
            ItemPrototype const* emotyProto = emptyBag->GetProto();

            uint32 count = 0;

            for (uint32 i = 0; i < fullBag->GetBagSize(); ++i)
            {
                Item* bagItem = fullBag->GetItemByPos(i);
                if (!bagItem)
                {
                    continue;
                }

                ItemPrototype const* bagItemProto = bagItem->GetProto();
                if (!bagItemProto || !ItemCanGoIntoBag(bagItemProto, emotyProto))
                {
                    // one from items not go to empty target bag
                    SendEquipError(EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG, pSrcItem, pDstItem);
                    return;
                }

                ++count;
            }

            if (count > emptyBag->GetBagSize())
            {
                // too small targeted bag
                SendEquipError(EQUIP_ERR_ITEMS_CANT_BE_SWAPPED, pSrcItem, pDstItem);
                return;
            }

            // Items swap
            count = 0;                                      // will pos in new bag
            for (uint32 i = 0; i < fullBag->GetBagSize(); ++i)
            {
                Item* bagItem = fullBag->GetItemByPos(i);
                if (!bagItem)
                {
                    continue;
                }

                fullBag->RemoveItem(i);
                emptyBag->StoreItem(count, bagItem);
                bagItem->SetState(ITEM_CHANGED, this);

                ++count;
            }
        }
    }

    // now do moves, remove...
    RemoveItem(dstbag, dstslot, false);
    RemoveItem(srcbag, srcslot, false);

    // add to dest
    if (IsInventoryPos(dst))
    {
        StoreItem(sDest, pSrcItem, true);
    }
    else if (IsBankPos(dst))
    {
        BankItem(sDest, pSrcItem, true);
    }
    else if (IsEquipmentPos(dst))
    {
        EquipItem(eDest, pSrcItem, true);
    }

    // add to src
    if (IsInventoryPos(src))
    {
        StoreItem(sDest2, pDstItem, true);
    }
    else if (IsBankPos(src))
    {
        BankItem(sDest2, pDstItem, true);
    }
    else if (IsEquipmentPos(src))
    {
        EquipItem(eDest2, pDstItem, true);
    }

    AutoUnequipOffhandIfNeed();
}

/**
 * @brief Adds an item to the vendor buyback list.
 *
 * @param pItem The item to place into a buyback slot.
 */
void Player::AddItemToBuyBackSlot(Item* pItem)
{
    if (pItem)
    {
        uint32 slot = m_currentBuybackSlot;
        // if current back slot non-empty search oldest or free
        if (m_items[slot])
        {
            uint32 oldest_time = GetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1);
            uint32 oldest_slot = BUYBACK_SLOT_START;

            for (uint32 i = BUYBACK_SLOT_START + 1; i < BUYBACK_SLOT_END; ++i)
            {
                // found empty
                if (!m_items[i])
                {
                    slot = i;
                    break;
                }

                uint32 i_time = GetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1 + i - BUYBACK_SLOT_START);

                if (oldest_time > i_time)
                {
                    oldest_time = i_time;
                    oldest_slot = i;
                }
            }

            // find oldest
            slot = oldest_slot;
        }

        RemoveItemFromBuyBackSlot(slot, true);
        DEBUG_LOG("STORAGE: AddItemToBuyBackSlot item = %u, slot = %u", pItem->GetEntry(), slot);

        m_items[slot] = pItem;
        time_t base = time(NULL);
        uint32 etime = uint32(base - m_logintime + (30 * 3600));
        uint32 eslot = slot - BUYBACK_SLOT_START;

        SetGuidValue(PLAYER_FIELD_VENDORBUYBACK_SLOT_1 + (eslot * 2), pItem->GetObjectGuid());
        if (ItemPrototype const* pProto = pItem->GetProto())
        {
            SetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, pProto->SellPrice * pItem->GetCount());
        }
        else
        {
            SetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, 0);
        }
        SetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1 + eslot, (uint32)etime);

        // move to next (for non filled list is move most optimized choice)
        if (m_currentBuybackSlot < BUYBACK_SLOT_END - 1)
        {
            ++m_currentBuybackSlot;
        }
    }
}

/**
 * @brief Gets an item from a vendor buyback slot.
 *
 * @param slot The buyback slot index.
 * @return The item in the slot, or null if none exists.
 */
Item* Player::GetItemFromBuyBackSlot(uint32 slot)
{
    DEBUG_LOG("STORAGE: GetItemFromBuyBackSlot slot = %u", slot);
    if (slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END)
    {
        return m_items[slot];
    }
    return NULL;
}

/**
 * @brief Removes an item from a vendor buyback slot.
 *
 * @param slot The buyback slot index.
 * @param del True to mark the removed item for deletion.
 */
void Player::RemoveItemFromBuyBackSlot(uint32 slot, bool del)
{
    DEBUG_LOG("STORAGE: RemoveItemFromBuyBackSlot slot = %u", slot);
    if (slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END)
    {
        Item* pItem = m_items[slot];
        if (pItem)
        {
            pItem->RemoveFromWorld();
            if (del)
            {
                pItem->SetState(ITEM_REMOVED, this);
            }
        }

        m_items[slot] = NULL;

        uint32 eslot = slot - BUYBACK_SLOT_START;
        SetGuidValue(PLAYER_FIELD_VENDORBUYBACK_SLOT_1 + (eslot * 2), ObjectGuid());
        SetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, 0);
        SetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1 + eslot, 0);

        // if current backslot is filled set to now free slot
        if (m_items[m_currentBuybackSlot])
        {
            m_currentBuybackSlot = slot;
        }
    }
}

/**
 * @brief Sends an inventory error packet to the client.
 *
 * @param msg The inventory error code.
 * @param pItem The primary item involved in the error.
 * @param pItem2 The secondary item involved in the error.
 * @param itemid An optional item entry used for some error variants.
 */
void Player::SendEquipError(InventoryResult msg, Item* pItem, Item* pItem2, uint32 itemid /*= 0*/) const
{
    DEBUG_LOG("WORLD: Sent SMSG_INVENTORY_CHANGE_FAILURE (%u)", msg);
    WorldPacket data(SMSG_INVENTORY_CHANGE_FAILURE, (msg == EQUIP_ERR_CANT_EQUIP_LEVEL_I ? 22 : (msg == EQUIP_ERR_OK ? 1 : 18)));
    data << uint8(msg);

    if (msg != EQUIP_ERR_OK)
    {
        if (msg == EQUIP_ERR_CANT_EQUIP_LEVEL_I)
        {
            ItemPrototype const* proto = pItem ? pItem->GetProto() : ObjectMgr::GetItemPrototype(itemid);
            data << uint32(proto ? proto->RequiredLevel : 0);
        }
        data << (pItem ? pItem->GetObjectGuid() : ObjectGuid());
        data << (pItem2 ? pItem2->GetObjectGuid() : ObjectGuid());
        data << uint8(0);                                   // bag type subclass, used with EQUIP_ERR_EVENT_AUTOEQUIP_BIND_CONFIRM and EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG2
    }
    GetSession()->SendPacket(&data);
}

void Player::SendBuyError(BuyResult msg, Creature* pCreature, uint32 item, uint32 param)
{
    DEBUG_LOG("WORLD: Sent SMSG_BUY_FAILED");
    WorldPacket data(SMSG_BUY_FAILED, (8 + 4 + 4 + 1));
    data << (pCreature ? pCreature->GetObjectGuid() : ObjectGuid());
    data << uint32(item);
    if (param > 0)
    {
        data << uint32(param);
    }
    data << uint8(msg);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Sends a sell failure result to the client.
 *
 * @param msg The sell failure code.
 * @param pCreature The vendor involved in the transaction.
 * @param itemGuid The item GUID that failed to sell.
 * @param param Unused extra parameter.
 */
void Player::SendSellError(SellResult msg, Creature* pCreature, ObjectGuid itemGuid, uint32 param)
{
    DEBUG_LOG("WORLD: Sent SMSG_SELL_ITEM");
    WorldPacket data(SMSG_SELL_ITEM, (8 + 8 + (param ? 4 : 0) + 1)); // last check 2.0.10
    data << (pCreature ? pCreature->GetObjectGuid() : ObjectGuid());
    data << ObjectGuid(itemGuid);
    if (param > 0)
    {
        data << uint32(param);
    }
    data << uint8(msg);
    GetSession()->SendPacket(&data);
}

/**
 * @brief Cancels the current trade and clears trade state for both players.
 *
 * @param sendback True to send a cancel notification to this player as well.
 */
void Player::TradeCancel(bool sendback)
{
    if (m_trade)
    {
        Player* trader = m_trade->GetTrader();

        // send yellow "Trade canceled" message to both traders
        if (sendback)
        {
            GetSession()->SendCancelTrade();
        }

        trader->GetSession()->SendCancelTrade();

        // cleanup
        delete m_trade;
        m_trade = NULL;
        delete trader->m_trade;
        trader->m_trade = NULL;
    }
}

/**
 * @brief Updates durations for items tracked by the player.
 *
 * @param time The elapsed time in milliseconds.
 * @param realtimeonly True to update only items using real-time duration tracking.
 */
void Player::UpdateItemDuration(uint32 time, bool realtimeonly)
{
    if (m_itemDuration.empty())
    {
        return;
    }

    DEBUG_LOG("Player::UpdateItemDuration(%u,%u)", time, realtimeonly);

    for (ItemDurationList::const_iterator itr = m_itemDuration.begin(); itr != m_itemDuration.end();)
    {
        Item* item = *itr;
        ++itr;                                              // current element can be erased in UpdateDuration

        if (!(realtimeonly) || (item->GetProto()->ExtraFlags & ITEM_EXTRA_REAL_TIME_DURATION))
        {
            item->UpdateDuration(this, time);
        }
    }
}

/**
 * @brief Updates remaining durations for temporary item enchantments.
 *
 * @param time The elapsed time in milliseconds.
 */
void Player::UpdateEnchantTime(uint32 time)
{
    for (EnchantDurationList::iterator itr = m_enchantDuration.begin(), next; itr != m_enchantDuration.end(); itr = next)
    {
        MANGOS_ASSERT(itr->item);
        next = itr;
        if (!itr->item->GetEnchantmentId(itr->slot))
        {
            next = m_enchantDuration.erase(itr);
        }
        else if (itr->leftduration <= time)
        {
            ApplyEnchantment(itr->item, itr->slot, false, false);
            itr->item->ClearEnchantment(itr->slot);
            next = m_enchantDuration.erase(itr);
        }
        else if (itr->leftduration > time)
        {
            itr->leftduration -= time;
            ++next;
        }
    }
}

/**
 * @brief Registers timed enchantments from an item for duration tracking.
 *
 * @param item The item whose enchantments should be tracked.
 */
void Player::AddEnchantmentDurations(Item* item)
{
    for (int x = 0; x < MAX_ENCHANTMENT_SLOT; ++x)
    {
        if (!item->GetEnchantmentId(EnchantmentSlot(x)))
        {
            continue;
        }

        uint32 duration = item->GetEnchantmentDuration(EnchantmentSlot(x));
        if (duration > 0)
        {
            AddEnchantmentDuration(item, EnchantmentSlot(x), duration);
        }
    }
}

/**
 * @brief Removes an item's enchantments from duration tracking and stores remaining time.
 *
 * @param item The item whose enchantments should be untracked.
 */
void Player::RemoveEnchantmentDurations(Item* item)
{
    for (EnchantDurationList::iterator itr = m_enchantDuration.begin(); itr != m_enchantDuration.end();)
    {
        if (itr->item == item)
        {
            // save duration in item
            item->SetEnchantmentDuration(EnchantmentSlot(itr->slot), itr->leftduration);
            itr = m_enchantDuration.erase(itr);
        }
        else
        {
            ++itr;
        }
    }
}

/**
 * @brief Removes all enchantments of a given slot type from equipped and stored items.
 *
 * @param slot The enchantment slot type to clear.
 */
void Player::RemoveAllEnchantments(EnchantmentSlot slot)
{
    // remove enchantments from equipped items first to clean up the m_enchantDuration list
    for (EnchantDurationList::iterator itr = m_enchantDuration.begin(), next; itr != m_enchantDuration.end(); itr = next)
    {
        next = itr;
        if (itr->slot == slot)
        {
            if (itr->item && itr->item->GetEnchantmentId(slot))
            {
                // remove from stats
                ApplyEnchantment(itr->item, slot, false, false);
                // remove visual
                itr->item->ClearEnchantment(slot);
            }
            // remove from update list
            next = m_enchantDuration.erase(itr);
        }
        else
        {
            ++next;
        }
    }

    // remove enchants from inventory items
    // NOTE: no need to remove these from stats, since these aren't equipped
    // in inventory
    for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
    {
        if (Item* pItem = GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem->GetEnchantmentId(slot))
            {
                pItem->ClearEnchantment(slot);
            }
    }

    // in inventory bags
    for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
    {
        if (Bag* pBag = (Bag*)GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            {
                if (Item* pItem = pBag->GetItemByPos(j))
                    if (pItem->GetEnchantmentId(slot))
                    {
                        pItem->ClearEnchantment(slot);
                    }
            }
    }
}

// duration == 0 will remove item enchant
void Player::AddEnchantmentDuration(Item* item, EnchantmentSlot slot, uint32 duration)
{
    if (!item)
    {
        return;
    }

    if (slot >= MAX_ENCHANTMENT_SLOT)
    {
        return;
    }

    for (EnchantDurationList::iterator itr = m_enchantDuration.begin(); itr != m_enchantDuration.end(); ++itr)
    {
        if (itr->item == item && itr->slot == slot)
        {
            itr->item->SetEnchantmentDuration(itr->slot, itr->leftduration);
            m_enchantDuration.erase(itr);
            break;
        }
    }
    if (item && duration > 0)
    {
        GetSession()->SendItemEnchantTimeUpdate(GetObjectGuid(), item->GetObjectGuid(), slot, uint32(duration / 1000));
        m_enchantDuration.push_back(EnchantDuration(item, slot, duration));
    }
}

/**
 * @brief Applies or removes all enchantments on an equipped item.
 *
 * @param item The item whose enchantments should be processed.
 * @param apply True to apply enchantments; false to remove them.
 */
void Player::ApplyEnchantment(Item* item, bool apply)
{
    for (uint32 slot = 0; slot < MAX_ENCHANTMENT_SLOT; ++slot)
    {
        ApplyEnchantment(item, EnchantmentSlot(slot), apply);
    }
}

/**
 * @brief Applies or removes a specific enchantment slot on an equipped item.
 *
 * @param item The equipped item whose enchantment should be processed.
 * @param slot The enchantment slot to process.
 * @param apply True to apply the enchantment; false to remove it.
 * @param apply_dur True to update tracked enchantment duration state.
 * @param ignore_condition Unused flag for conditional processing.
 */
void Player::ApplyEnchantment(Item* item, EnchantmentSlot slot, bool apply, bool apply_dur, bool ignore_condition)
{
    if (!item)
    {
        return;
    }

    if (!item->IsEquipped())
    {
        return;
    }

    if (slot >= MAX_ENCHANTMENT_SLOT)
    {
        return;
    }

    uint32 enchant_id = item->GetEnchantmentId(slot);
    if (!enchant_id)
    {
        return;
    }

    SpellItemEnchantmentEntry const* pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
    if (!pEnchant)
    {
        return;
    }

    if (!ignore_condition && pEnchant->EnchantmentCondition && !EnchantmentFitsRequirements(pEnchant->EnchantmentCondition, -1))
    {
        return;
    }

    if (!item->IsBroken())
    {
        for (int s = 0; s < 3; ++s)
        {
            uint32 enchant_display_type = pEnchant->type[s];
            uint32 enchant_amount = pEnchant->amount[s];
            uint32 enchant_spell_id = pEnchant->spellid[s];

            switch (enchant_display_type)
            {
                case ITEM_ENCHANTMENT_TYPE_NONE:
                    break;
                case ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL:
                    // processed in Player::CastItemCombatSpell
                    break;
                case ITEM_ENCHANTMENT_TYPE_DAMAGE:
                    if (item->GetSlot() == EQUIPMENT_SLOT_MAINHAND)
                    {
                        HandleStatModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_VALUE, float(enchant_amount), apply);
                    }
                    else if (item->GetSlot() == EQUIPMENT_SLOT_OFFHAND)
                    {
                        HandleStatModifier(UNIT_MOD_DAMAGE_OFFHAND, TOTAL_VALUE, float(enchant_amount), apply);
                    }
                    else if (item->GetSlot() == EQUIPMENT_SLOT_RANGED)
                    {
                        HandleStatModifier(UNIT_MOD_DAMAGE_RANGED, TOTAL_VALUE, float(enchant_amount), apply);
                    }
                    break;
                case ITEM_ENCHANTMENT_TYPE_EQUIP_SPELL:
                {
                    // Flametongue Weapon (Passive), Ranks (used not existed equip spell id in pre-3.x spell.dbc)
                    // See Player::CastItemCombatSpell for workaround implementation
                    if (enchant_spell_id && apply)
                    {
                        switch (enchant_spell_id)
                        {
                            case 10400:                     // Rank 1
                            case 15567:                     // Rank 2
                            case 15568:                     // Rank 3
                            case 15569:                     // Rank 4
                            case 16311:                     // Rank 5
                            case 16312:                     // Rank 6
                            case 16313:                     // Rank 7
                                enchant_spell_id = 0;
                                break;
                            default:
                                break;
                        }
                    }

                    if (enchant_spell_id)
                    {
                        if (apply)
                        {
                            int32 basepoints = 0;
                            // Random Property Exist - try found basepoints for spell (basepoints depends from item suffix factor)
                            if (item->GetItemRandomPropertyId())
                            {
                                ItemRandomSuffixEntry const* item_rand = sItemRandomSuffixStore.LookupEntry(abs(item->GetItemRandomPropertyId()));
                                if (item_rand)
                                {
                                    // Search enchant_amount
                                    for (int k = 0; k < 3; ++k)
                                    {
                                        if (item_rand->enchant_id[k] == enchant_id)
                                        {
                                            basepoints = int32((item_rand->prefix[k] * item->GetItemSuffixFactor()) / 10000);
                                            break;
                                        }
                                    }
                                }
                            }
                            // Cast custom spell vs all equal basepoints got from enchant_amount
                            if (basepoints)
                            {
                                CastCustomSpell(this, enchant_spell_id, &basepoints, &basepoints, &basepoints, true, item);
                            }
                            else
                            {
                                CastSpell(this, enchant_spell_id, true, item);
                            }
                        }
                        else
                        {
                            RemoveAurasDueToItemSpell(item, enchant_spell_id);
                        }
                    }
                    break;
                }
                case ITEM_ENCHANTMENT_TYPE_RESISTANCE:
                    if (!enchant_amount)
                    {
                        ItemRandomSuffixEntry const* item_rand = sItemRandomSuffixStore.LookupEntry(abs(item->GetItemRandomPropertyId()));
                        if (item_rand)
                        {
                            for (int k = 0; k < 3; ++k)
                            {
                                if (item_rand->enchant_id[k] == enchant_id)
                                {
                                    enchant_amount = uint32((item_rand->prefix[k] * item->GetItemSuffixFactor()) / 10000);
                                    break;
                                }
                            }
                        }
                    }

                    HandleStatModifier(UnitMods(UNIT_MOD_RESISTANCE_START + enchant_spell_id), TOTAL_VALUE, float(enchant_amount), apply);
                    break;
                case ITEM_ENCHANTMENT_TYPE_STAT:
                {
                    if (!enchant_amount)
                    {
                        ItemRandomSuffixEntry const* item_rand_suffix = sItemRandomSuffixStore.LookupEntry(abs(item->GetItemRandomPropertyId()));
                        if (item_rand_suffix)
                        {
                            for (int k = 0; k < 3; ++k)
                            {
                                if (item_rand_suffix->enchant_id[k] == enchant_id)
                                {
                                    enchant_amount = uint32((item_rand_suffix->prefix[k] * item->GetItemSuffixFactor()) / 10000);
                                    break;
                                }
                            }
                        }
                    }

                    DEBUG_LOG("Adding %u to stat nb %u", enchant_amount, enchant_spell_id);
                    switch (enchant_spell_id)
                    {
                        case ITEM_MOD_MANA:
                            DEBUG_LOG("+ %u MANA", enchant_amount);
                            HandleStatModifier(UNIT_MOD_MANA, BASE_VALUE, float(enchant_amount), apply);
                            break;
                        case ITEM_MOD_HEALTH:
                            DEBUG_LOG("+ %u HEALTH", enchant_amount);
                            HandleStatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(enchant_amount), apply);
                            break;
                        case ITEM_MOD_AGILITY:
                            DEBUG_LOG("+ %u AGILITY", enchant_amount);
                            HandleStatModifier(UNIT_MOD_STAT_AGILITY, TOTAL_VALUE, float(enchant_amount), apply);
                            ApplyStatBuffMod(STAT_AGILITY, float(enchant_amount), apply);
                            break;
                        case ITEM_MOD_STRENGTH:
                            DEBUG_LOG("+ %u STRENGTH", enchant_amount);
                            HandleStatModifier(UNIT_MOD_STAT_STRENGTH, TOTAL_VALUE, float(enchant_amount), apply);
                            ApplyStatBuffMod(STAT_STRENGTH, float(enchant_amount), apply);
                            break;
                        case ITEM_MOD_INTELLECT:
                            DEBUG_LOG("+ %u INTELLECT", enchant_amount);
                            HandleStatModifier(UNIT_MOD_STAT_INTELLECT, TOTAL_VALUE, float(enchant_amount), apply);
                            ApplyStatBuffMod(STAT_INTELLECT, float(enchant_amount), apply);
                            break;
                        case ITEM_MOD_SPIRIT:
                            DEBUG_LOG("+ %u SPIRIT", enchant_amount);
                            HandleStatModifier(UNIT_MOD_STAT_SPIRIT, TOTAL_VALUE, float(enchant_amount), apply);
                            ApplyStatBuffMod(STAT_SPIRIT, float(enchant_amount), apply);
                            break;
                        case ITEM_MOD_STAMINA:
                            DEBUG_LOG("+ %u STAMINA", enchant_amount);
                            HandleStatModifier(UNIT_MOD_STAT_STAMINA, TOTAL_VALUE, float(enchant_amount), apply);
                            ApplyStatBuffMod(STAT_STAMINA, float(enchant_amount), apply);
                            break;
                        case ITEM_MOD_DEFENSE_SKILL_RATING:
                            ApplyRatingMod(CR_DEFENSE_SKILL, enchant_amount, apply);
                            DEBUG_LOG("+ %u DEFENCE", enchant_amount);
                            break;
                        case  ITEM_MOD_DODGE_RATING:
                            ApplyRatingMod(CR_DODGE, enchant_amount, apply);
                            DEBUG_LOG("+ %u DODGE", enchant_amount);
                            break;
                        case ITEM_MOD_PARRY_RATING:
                            ApplyRatingMod(CR_PARRY, enchant_amount, apply);
                            DEBUG_LOG("+ %u PARRY", enchant_amount);
                            break;
                        case ITEM_MOD_BLOCK_RATING:
                            ApplyRatingMod(CR_BLOCK, enchant_amount, apply);
                            DEBUG_LOG("+ %u SHIELD_BLOCK", enchant_amount);
                            break;
                        case ITEM_MOD_HIT_MELEE_RATING:
                            ApplyRatingMod(CR_HIT_MELEE, enchant_amount, apply);
                            DEBUG_LOG("+ %u MELEE_HIT", enchant_amount);
                            break;
                        case ITEM_MOD_HIT_RANGED_RATING:
                            ApplyRatingMod(CR_HIT_RANGED, enchant_amount, apply);
                            DEBUG_LOG("+ %u RANGED_HIT", enchant_amount);
                            break;
                        case ITEM_MOD_HIT_SPELL_RATING:
                            ApplyRatingMod(CR_HIT_SPELL, enchant_amount, apply);
                            DEBUG_LOG("+ %u SPELL_HIT", enchant_amount);
                            break;
                        case ITEM_MOD_CRIT_MELEE_RATING:
                            ApplyRatingMod(CR_CRIT_MELEE, enchant_amount, apply);
                            DEBUG_LOG("+ %u MELEE_CRIT", enchant_amount);
                            break;
                        case ITEM_MOD_CRIT_RANGED_RATING:
                            ApplyRatingMod(CR_CRIT_RANGED, enchant_amount, apply);
                            DEBUG_LOG("+ %u RANGED_CRIT", enchant_amount);
                            break;
                        case ITEM_MOD_CRIT_SPELL_RATING:
                            ApplyRatingMod(CR_CRIT_SPELL, enchant_amount, apply);
                            DEBUG_LOG("+ %u SPELL_CRIT", enchant_amount);
                            break;
                        case ITEM_MOD_HASTE_SPELL_RATING:
                            ApplyRatingMod(CR_HASTE_SPELL, enchant_amount, apply);
                            break;
                        case ITEM_MOD_HIT_RATING:
                            ApplyRatingMod(CR_HIT_MELEE, enchant_amount, apply);
                            ApplyRatingMod(CR_HIT_RANGED, enchant_amount, apply);
                            DEBUG_LOG("+ %u HIT", enchant_amount);
                            break;
                        case ITEM_MOD_CRIT_RATING:
                            ApplyRatingMod(CR_CRIT_MELEE, enchant_amount, apply);
                            ApplyRatingMod(CR_CRIT_RANGED, enchant_amount, apply);
                            DEBUG_LOG("+ %u CRITICAL", enchant_amount);
                            break;
                        case ITEM_MOD_RESILIENCE_RATING:
                            ApplyRatingMod(CR_CRIT_TAKEN_MELEE, enchant_amount, apply);
                            ApplyRatingMod(CR_CRIT_TAKEN_RANGED, enchant_amount, apply);
                            ApplyRatingMod(CR_CRIT_TAKEN_SPELL, enchant_amount, apply);
                            DEBUG_LOG("+ %u RESILIENCE", enchant_amount);
                            break;
                        case ITEM_MOD_HASTE_RATING:
                            ApplyRatingMod(CR_HASTE_MELEE, enchant_amount, apply);
                            ApplyRatingMod(CR_HASTE_RANGED, enchant_amount, apply);
                            DEBUG_LOG("+ %u HASTE", enchant_amount);
                            break;
                        case ITEM_MOD_EXPERTISE_RATING:
                            ApplyRatingMod(CR_EXPERTISE, enchant_amount, apply);
                            DEBUG_LOG("+ %u EXPERTISE", enchant_amount);
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case ITEM_ENCHANTMENT_TYPE_TOTEM:           // Shaman Rockbiter Weapon
                {
                    if (getClass() == CLASS_SHAMAN)
                    {
                        float addValue = 0.0f;
                        if (item->GetSlot() == EQUIPMENT_SLOT_MAINHAND)
                        {
                            addValue = float(enchant_amount * item->GetProto()->Delay / 1000.0f);
                            HandleStatModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_VALUE, addValue, apply);
                        }
                        else if (item->GetSlot() == EQUIPMENT_SLOT_OFFHAND)
                        {
                            addValue = float(enchant_amount * item->GetProto()->Delay / 1000.0f);
                            HandleStatModifier(UNIT_MOD_DAMAGE_OFFHAND, TOTAL_VALUE, addValue, apply);
                        }
                    }
                    break;
                }
                default:
                    sLog.outError("Unknown item enchantment (id = %d) display type: %d", enchant_id, enchant_display_type);
                    break;
            }                                               /*switch(enchant_display_type)*/
        }                                                   /*for*/
    }

    // visualize enchantment at player and equipped items
    if (slot < MAX_INSPECTED_ENCHANTMENT_SLOT)
    {
        int VisibleBase = PLAYER_VISIBLE_ITEM_1_0 + (item->GetSlot() * MAX_VISIBLE_ITEM_OFFSET);
        SetUInt32Value(VisibleBase + 1 + slot, apply ? item->GetEnchantmentId(slot) : 0);
    }

    if (apply_dur)
    {
        if (apply)
        {
            // set duration
            uint32 duration = item->GetEnchantmentDuration(slot);
            if (duration > 0)
            {
                AddEnchantmentDuration(item, slot, duration);
            }
        }
        else
        {
            // duration == 0 will remove EnchantDuration
            AddEnchantmentDuration(item, slot, 0);
        }
    }
}

/**
 * @brief Sends all active enchantment duration timers to the client.
 */
void Player::SendEnchantmentDurations()
{
    for (EnchantDurationList::const_iterator itr = m_enchantDuration.begin(); itr != m_enchantDuration.end(); ++itr)
    {
        GetSession()->SendItemEnchantTimeUpdate(GetObjectGuid(), itr->item->GetObjectGuid(), itr->slot, uint32(itr->leftduration) / 1000);
    }
}

/**
 * @brief Sends all tracked item duration timers to the client.
 */
void Player::SendItemDurations()
{
    for (ItemDurationList::const_iterator itr = m_itemDuration.begin(); itr != m_itemDuration.end(); ++itr)
    {
        (*itr)->SendTimeUpdate(this);
    }
}

/**
 * @brief Sends an item push notification for a newly received or created item.
 *
 * @param item The item instance being reported.
 * @param count The quantity gained.
 * @param received True if the item was received rather than looted.
 * @param created True if the item was created rather than simply received.
 * @param broadcast True to broadcast the message to the player's group.
 * @param showInChat True to show the gain in chat.
 */
void Player::SendNewItem(Item* item, uint32 count, bool received, bool created, bool broadcast /*=false*/, bool showInChat /*=true*/)
{
    if (!item)                                              // prevent crash
    {
        return;
    }

    // last check 2.0.10
    WorldPacket data(SMSG_ITEM_PUSH_RESULT, (8 + 4 + 4 + 4 + 1 + 4 + 4 + 4 + 4 + 4));
    data << GetObjectGuid();                                // player GUID
    data << uint32(received);                               // 0=looted, 1=from npc
    data << uint32(created);                                // 0=received, 1=created
    data << uint32(showInChat);                             // showInChat
    data << uint8(item->GetBagSlot());                      // bagslot
    // item slot, but when added to stack: 0xFFFFFFFF
    data << uint32((item->GetCount() == count) ? item->GetSlot() : -1);
    data << uint32(item->GetEntry());                       // item id
    data << uint32(item->GetItemSuffixFactor());            // SuffixFactor
    data << uint32(item->GetItemRandomPropertyId());        // random item property id
    data << uint32(count);                                  // count of items
    data << uint32(GetItemCount(item->GetEntry()));         // count of items in inventory

    if (broadcast && GetGroup())
    {
        GetGroup()->BroadcastPacket(&data, true);
    }
    else
    {
        GetSession()->SendPacket(&data);
    }
}
