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
 * @file Spell.cpp
 * @brief Spell casting and effect implementation
 *
 * This file implements the Spell class which handles spell casting:
 * - Spell validation and casting requirements
 * - Spell effect execution (damage, healing, summon, etc.)
 * - Spell targeting and area effects
 * - Spell cooldowns and resource costs
 * - Spell interruption and pushback
 * - Spell aura application
 * - Spell hit/miss calculations
 *
 * Spells are the primary combat mechanic in WoW, encompassing
 * abilities, talents, and item effects.
 *
 * @see Spell for the spell class
 * @see SpellAura for spell auras
 * @see SpellMgr for spell management
 */

#include "Spell.h"
#include "Database/DatabaseEnv.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Opcodes.h"
#include "Log.h"
#include "World.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "Pet.h"
#include "Unit.h"
#include "DynamicObject.h"
#include "Group.h"
#include "UpdateData.h"
#include "ObjectAccessor.h"
#include "CellImpl.h"
#include "Policies/Singleton.h"
#include "SharedDefines.h"
#include "LootMgr.h"
#include "VMapFactory.h"
#include "BattleGround/BattleGround.h"
#include "Util.h"
#include "Chat.h"
#include "SQLStorages.h"
#include "DisableMgr.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

extern pEffect SpellEffects[TOTAL_SPELL_EFFECTS];

/**
 * @brief Checks whether a spell matches the quest tame spell pattern.
 *
 * @param spellId The spell identifier to test.
 * @return True if the spell is a quest tame spell; otherwise, false.
 */
bool IsQuestTameSpell(uint32 spellId)
{
    SpellEntry const* spellproto = sSpellStore.LookupEntry(spellId);
    if (!spellproto)
    {
        return false;
    }

    return spellproto->Effect[EFFECT_INDEX_0] == SPELL_EFFECT_THREAT
           && spellproto->Effect[EFFECT_INDEX_1] == SPELL_EFFECT_APPLY_AURA && spellproto->EffectApplyAuraName[EFFECT_INDEX_1] == SPELL_AURA_DUMMY;
}

SpellCastTargets::SpellCastTargets()
{
    m_unitTarget = NULL;
    m_itemTarget = NULL;
    m_GOTarget   = NULL;

    m_itemTargetEntry  = 0;

    m_srcX = m_srcY = m_srcZ = m_destX = m_destY = m_destZ = 0.0f;
    m_strTarget.clear();
    m_targetMask = 0;
}

SpellCastTargets::~SpellCastTargets()
{
}

/**
 * @brief Sets a unit target and copies its current position as the destination.
 *
 * @param target The unit target.
 */
void SpellCastTargets::setUnitTarget(Unit* target)
{
    if (!target)
    {
        return;
    }

    m_destX = target->GetPositionX();
    m_destY = target->GetPositionY();
    m_destZ = target->GetPositionZ();
    m_unitTarget = target;
    m_unitTargetGUID = target->GetObjectGuid();
    m_targetMask |= TARGET_FLAG_UNIT;
}

/**
 * @brief Sets the destination coordinates for the cast.
 *
 * @param x The destination X coordinate.
 * @param y The destination Y coordinate.
 * @param z The destination Z coordinate.
 */
void SpellCastTargets::setDestination(float x, float y, float z)
{
    m_destX = x;
    m_destY = y;
    m_destZ = z;
    m_targetMask |= TARGET_FLAG_DEST_LOCATION;
}

/**
 * @brief Sets the source coordinates for the cast.
 *
 * @param x The source X coordinate.
 * @param y The source Y coordinate.
 * @param z The source Z coordinate.
 */
void SpellCastTargets::setSource(float x, float y, float z)
{
    m_srcX = x;
    m_srcY = y;
    m_srcZ = z;
    m_targetMask |= TARGET_FLAG_SOURCE_LOCATION;
}

/**
 * @brief Sets the game object target for the cast.
 *
 * @param target The game object target.
 */
void SpellCastTargets::setGOTarget(GameObject* target)
{
    m_GOTarget = target;
    m_GOTargetGUID = target->GetObjectGuid();
    //    m_targetMask |= TARGET_FLAG_OBJECT;
}

/**
 * @brief Sets the item target for the cast.
 *
 * @param item The item target.
 */
void SpellCastTargets::setItemTarget(Item* item)
{
    if (!item)
    {
        return;
    }

    m_itemTarget = item;
    m_itemTargetGUID = item->GetObjectGuid();
    m_itemTargetEntry = item->GetEntry();
    m_targetMask |= TARGET_FLAG_ITEM;
}

/**
 * @brief Sets the current trade slot as the item target.
 *
 * @param caster The player performing the cast.
 */
void SpellCastTargets::setTradeItemTarget(Player* caster)
{
    m_itemTargetGUID = ObjectGuid(uint64(TRADE_SLOT_NONTRADED));
    m_itemTargetEntry = 0;
    m_targetMask |= TARGET_FLAG_TRADE_ITEM;

    Update(caster);
}

/**
 * @brief Sets the corpse target for the cast.
 *
 * @param corpse The corpse target.
 */
void SpellCastTargets::setCorpseTarget(Corpse* corpse)
{
    m_CorpseTargetGUID = corpse->GetObjectGuid();
}

/**
 * @brief Resolves stored target GUIDs into live object pointers.
 *
 * @param caster The casting unit used to resolve map-relative targets.
 */
void SpellCastTargets::Update(Unit* caster)
{
    m_GOTarget   = m_GOTargetGUID ? caster->GetMap()->GetGameObject(m_GOTargetGUID) : NULL;
    m_unitTarget = m_unitTargetGUID ?
                   (m_unitTargetGUID == caster->GetObjectGuid() ? caster : sObjectAccessor.GetUnit(*caster, m_unitTargetGUID)) :
                       NULL;

    m_itemTarget = NULL;
    if (caster->GetTypeId() == TYPEID_PLAYER)
    {
        Player* player = ((Player*)caster);

        if (m_targetMask & TARGET_FLAG_ITEM)
        {
            m_itemTarget = player->GetItemByGuid(m_itemTargetGUID);
        }
        else if (m_targetMask & TARGET_FLAG_TRADE_ITEM)
        {
            if (TradeData* pTrade = player->GetTradeData())
            {
                if (m_itemTargetGUID.GetRawValue() < TRADE_SLOT_COUNT)
                {
                    m_itemTarget = pTrade->GetTraderData()->GetItem(TradeSlots(m_itemTargetGUID.GetRawValue()));
                }
            }
        }

        if (m_itemTarget)
        {
            m_itemTargetEntry = m_itemTarget->GetEntry();
        }
    }
}

/**
 * @brief Deserializes spell cast targets from a packet buffer.
 *
 * @param data The packet buffer to read.
 * @param caster The casting unit.
 */
void SpellCastTargets::read(ByteBuffer& data, Unit* caster)
{
    data >> m_targetMask;

    if (m_targetMask == TARGET_FLAG_SELF)
    {
        m_destX = caster->GetPositionX();
        m_destY = caster->GetPositionY();
        m_destZ = caster->GetPositionZ();
        m_unitTarget = caster;
        m_unitTargetGUID = caster->GetObjectGuid();
        return;
    }

    // TARGET_FLAG_UNK2 is used for non-combat pets, maybe other?
    if (m_targetMask & (TARGET_FLAG_UNIT | TARGET_FLAG_UNK2))
    {
        data >> m_unitTargetGUID.ReadAsPacked();
    }

    if (m_targetMask & (TARGET_FLAG_OBJECT | TARGET_FLAG_OBJECT_UNK | TARGET_FLAG_GAMEOBJECT_ITEM))
    {
        data >> m_GOTargetGUID.ReadAsPacked();
    }

    if ((m_targetMask & (TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM)) && caster->GetTypeId() == TYPEID_PLAYER)
    {
        data >> m_itemTargetGUID.ReadAsPacked();
    }

    if (m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
    {
        data >> m_srcX >> m_srcY >> m_srcZ;
        if (!MaNGOS::IsValidMapCoord(m_srcX, m_srcY, m_srcZ))
        {
            throw ByteBufferException(false, data.rpos(), 0, data.size());
        }
    }

    if (m_targetMask & TARGET_FLAG_DEST_LOCATION)
    {
        data >> m_destX >> m_destY >> m_destZ;
        if (!MaNGOS::IsValidMapCoord(m_destX, m_destY, m_destZ))
        {
            throw ByteBufferException(false, data.rpos(), 0, data.size());
        }
    }

    if (m_targetMask & TARGET_FLAG_STRING)
    {
        data >> m_strTarget;
    }

    if (m_targetMask & (TARGET_FLAG_CORPSE | TARGET_FLAG_PVP_CORPSE))
    {
        data >> m_CorpseTargetGUID.ReadAsPacked();
    }

    // find real units/GOs
    Update(caster);
}

/**
 * @brief Serializes spell cast targets into a packet buffer.
 *
 * @param data The packet buffer to write.
 */
void SpellCastTargets::write(ByteBuffer& data) const
{
    data << uint32(m_targetMask);

    if (m_targetMask & (TARGET_FLAG_UNIT | TARGET_FLAG_PVP_CORPSE | TARGET_FLAG_OBJECT | TARGET_FLAG_CORPSE | TARGET_FLAG_UNK2))
    {
        if (m_targetMask & TARGET_FLAG_UNIT)
        {
            if (m_unitTarget)
            {
                data << m_unitTarget->GetPackGUID();
            }
            else
            {
                data << uint8(0);
            }
        }
        else if (m_targetMask & (TARGET_FLAG_OBJECT | TARGET_FLAG_OBJECT_UNK))
        {
            if (m_GOTarget)
            {
                data << m_GOTarget->GetPackGUID();
            }
            else
            {
                data << uint8(0);
            }
        }
        else if (m_targetMask & (TARGET_FLAG_CORPSE | TARGET_FLAG_PVP_CORPSE))
        {
            data << m_CorpseTargetGUID.WriteAsPacked();
        }
        else
        {
            data << uint8(0);
        }
    }

    if (m_targetMask & (TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM))
    {
        if (m_itemTarget)
        {
            data << m_itemTarget->GetPackGUID();
        }
        else
        {
            data << uint8(0);
        }
    }

    if (m_targetMask & TARGET_FLAG_SOURCE_LOCATION)
    {
        data << m_srcX << m_srcY << m_srcZ;
    }

    if (m_targetMask & TARGET_FLAG_DEST_LOCATION)
    {
        data << m_destX << m_destY << m_destZ;
    }

    if (m_targetMask & TARGET_FLAG_STRING)
    {
        data << m_strTarget;
    }
}

Spell::Spell(Unit* caster, SpellEntry const* info, bool triggered, ObjectGuid originalCasterGUID, SpellEntry const* triggeredBy)
{
    MANGOS_ASSERT(caster != NULL && info != NULL);
    MANGOS_ASSERT(info == sSpellStore.LookupEntry(info->Id));   // `info` must be pointer to sSpellStore element

    m_spellInfo = info;
    m_triggeredBySpellInfo = triggeredBy;
    m_caster = caster;
    m_selfContainer = NULL;
    m_referencedFromCurrentSpell = false;
    m_executedCurrently = false;
    m_delayStart = 0;
    m_delayAtDamageCount = 0;

    m_applyMultiplierMask = 0;

    // Get data for type of attack
    m_attackType = GetWeaponAttackType(m_spellInfo);

    m_spellSchoolMask = GetSpellSchoolMask(info);           // Can be override for some spell (wand shoot for example)

    if (m_attackType == RANGED_ATTACK)
    {
        // wand case
        if ((m_caster->getClassMask() & CLASSMASK_WAND_USERS) != 0 && m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            if (Item* pItem = ((Player*)m_caster)->GetWeaponForAttack(RANGED_ATTACK))
            {
                m_spellSchoolMask = SpellSchoolMask(1 << pItem->GetProto()->Damage[0].DamageType);
            }
        }
    }
    // Set health leech amount to zero
    m_healthLeech = 0;

    m_originalCasterGUID = originalCasterGUID ? originalCasterGUID : m_caster->GetObjectGuid();

    UpdateOriginalCasterPointer();

    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        m_currentBasePoints[i] = m_spellInfo->CalculateSimpleValue(SpellEffectIndex(i));
    }

    m_spellState = SPELL_STATE_PREPARING;

    m_castPositionX = m_castPositionY = m_castPositionZ = 0;
    m_TriggerSpells.clear();
    m_preCastSpells.clear();
    m_IsTriggeredSpell = triggered;
    // m_AreaAura = false;
    m_CastItem = NULL;

    unitTarget = NULL;
    itemTarget = NULL;
    gameObjTarget = NULL;
    focusObject = NULL;
    m_cast_count = 0;
    m_triggeredByAuraSpell  = NULL;

    // Auto Shot & Shoot (wand)
    m_autoRepeat = IsAutoRepeatRangedSpell(m_spellInfo);

    m_powerCost = 0;                                        // setup to correct value in Spell::prepare, don't must be used before.
    m_casttime = 0;                                         // setup to correct value in Spell::prepare, don't must be used before.
    m_timer = 0;                                            // will set to cast time in prepare
    m_duration = 0;

    m_needAliveTargetMask = 0;

    // determine reflection
    m_canReflect = false;

    if (m_spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MAGIC && !m_spellInfo->HasAttribute(SPELL_ATTR_EX2_IGNORE_LOS))
    {
        for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
        {
            if (m_spellInfo->Effect[j] == 0)
            {
                continue;
            }

            if (!IsPositiveTarget(m_spellInfo->EffectImplicitTargetA[j], m_spellInfo->EffectImplicitTargetB[j]))
            {
                m_canReflect = true;
            }
            else
            {
                m_canReflect = m_spellInfo->HasAttribute(SPELL_ATTR_EX_CANT_BE_REFLECTED);
            }

            if (m_canReflect)
            {
                continue;
            }
            else
            {
                break;
            }
        }
    }

    CleanupTargetList();
}

Spell::~Spell()
{
}

/**
 * @brief Builds the spell target lists for each active effect.
 */
void Spell::FillTargetMap()
{
    // TODO: ADD the correct target FILLS!!!!!!

    UnitList tmpUnitLists[MAX_EFFECT_INDEX];                // Stores the temporary Target Lists for each effect
    uint8 effToIndex[MAX_EFFECT_INDEX] = {0, 1, 2};         // Helper array, to link to another tmpUnitList, if the targets for both effects match
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        // not call for empty effect.
        // Also some spells use not used effect targets for store targets for dummy effect in triggered spells
        if (m_spellInfo->Effect[i] == SPELL_EFFECT_NONE)
        {
            continue;
        }

        // targets for TARGET_SCRIPT_COORDINATES (A) and TARGET_SCRIPT
        // for TARGET_FOCUS_OR_SCRIPTED_GAMEOBJECT (A) all is checked in Spell::CheckCast and in Spell::CheckItem
        // filled in Spell::CheckCast call
        if (m_spellInfo->EffectImplicitTargetA[i] == TARGET_SCRIPT_COORDINATES ||
            m_spellInfo->EffectImplicitTargetA[i] == TARGET_FOCUS_OR_SCRIPTED_GAMEOBJECT ||
            (m_spellInfo->EffectImplicitTargetA[i] == TARGET_SCRIPT && m_spellInfo->EffectImplicitTargetB[i] != TARGET_SELF) ||
            (m_spellInfo->EffectImplicitTargetB[i] == TARGET_SCRIPT && m_spellInfo->EffectImplicitTargetA[i] != TARGET_SELF))
        {
            continue;
        }

        // TODO: find a way so this is not needed?
        // for area auras always add caster as target (needed for totems for example)
        if (IsAreaAuraEffect(m_spellInfo->Effect[i]))
        {
            AddUnitTarget(m_caster, SpellEffectIndex(i));
        }

        // no double fill for same targets
        for (int j = 0; j < i; ++j)
        {
            // Check if same target, but handle i.e. AreaAuras different
            if (m_spellInfo->EffectImplicitTargetA[i] == m_spellInfo->EffectImplicitTargetA[j] && m_spellInfo->EffectImplicitTargetB[i] == m_spellInfo->EffectImplicitTargetB[j]
                && m_spellInfo->Effect[j] != SPELL_EFFECT_NONE
                && !IsAreaAuraEffect(m_spellInfo->Effect[i]) && !IsAreaAuraEffect(m_spellInfo->Effect[j]))
                // Add further conditions here if required
            {
                effToIndex[i] = j;                          // effect i has same targeting list as effect j
                break;
            }
        }

        if (effToIndex[i] == i)                             // New target combination
        {
            // TargetA/TargetB dependent from each other, we not switch to full support this dependences
            // but need it support in some know cases
            switch (m_spellInfo->EffectImplicitTargetA[i])
            {
                case TARGET_NONE:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                            if (m_caster->GetObjectGuid().IsPet())
                            {
                                SetTargetMap(SpellEffectIndex(i), TARGET_SELF, tmpUnitLists[i /*==effToIndex[i]*/]);
                            }
                            else
                            {
                                SetTargetMap(SpellEffectIndex(i), TARGET_EFFECT_SELECT, tmpUnitLists[i /*==effToIndex[i]*/]);
                            }
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_SELF:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:                   // Fill Target based on A only
                        case TARGET_EFFECT_SELECT:
                        case TARGET_SCRIPT:                 // B-target only used with CheckCast here
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_AREAEFFECT_INSTANT:     // use B case that not dependent from A in fact
                            if ((m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION) == 0)
                            {
                                m_targets.setDestination(m_caster->GetPositionX(), m_caster->GetPositionY(), m_caster->GetPositionZ());
                            }
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_BEHIND_VICTIM:          // use B case that not dependent from A in fact
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_EFFECT_SELECT:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                        case TARGET_EFFECT_SELECT:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        // dest point setup required
                        case TARGET_AREAEFFECT_INSTANT:
                        case TARGET_AREAEFFECT_CUSTOM:
                        case TARGET_ALL_ENEMY_IN_AREA:
                        case TARGET_ALL_ENEMY_IN_AREA_INSTANT:
                        case TARGET_ALL_ENEMY_IN_AREA_CHANNELED:
                        case TARGET_ALL_FRIENDLY_UNITS_IN_AREA:
                        case TARGET_AREAEFFECT_GO_AROUND_DEST:
                        case TARGET_RANDOM_NEARBY_DEST:
                            // triggered spells get dest point from default target set, ignore it
                            if (!(m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION) || m_IsTriggeredSpell)
                            {
                                if (WorldObject* castObject = GetCastingObject())
                                {
                                    m_targets.setDestination(castObject->GetPositionX(), castObject->GetPositionY(), castObject->GetPositionZ());
                                }
                            }
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                            // target pre-selection required
                        case TARGET_INNKEEPER_COORDINATES:
                        case TARGET_TABLE_X_Y_Z_COORDINATES:
                        case TARGET_CASTER_COORDINATES:
                        case TARGET_SCRIPT_COORDINATES:
                        case TARGET_CURRENT_ENEMY_COORDINATES:
                        case TARGET_DUELVSPLAYER_COORDINATES:
                        case TARGET_DYNAMIC_OBJECT_COORDINATES:
                        case TARGET_POINT_AT_NORTH:
                        case TARGET_POINT_AT_SOUTH:
                        case TARGET_POINT_AT_EAST:
                        case TARGET_POINT_AT_WEST:
                        case TARGET_POINT_AT_NE:
                        case TARGET_POINT_AT_NW:
                        case TARGET_POINT_AT_SE:
                        case TARGET_POINT_AT_SW:
                            // need some target for processing
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_CASTER_COORDINATES:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_ALL_ENEMY_IN_AREA:
                            // Note: this hack with search required until GO casting not implemented
                            // environment damage spells already have around enemies targeting but this not help in case nonexistent GO casting support
                            // currently each enemy selected explicitly and self cast damage
                            if (m_spellInfo->Effect[i] == SPELL_EFFECT_ENVIRONMENTAL_DAMAGE)
                            {
                                if (m_targets.getUnitTarget())
                                {
                                    tmpUnitLists[i /*==effToIndex[i]*/].push_back(m_targets.getUnitTarget());
                                }
                            }
                            else
                            {
                                SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                                SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            }
                            break;
                        case TARGET_NONE:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            tmpUnitLists[i /*==effToIndex[i]*/].push_back(m_caster);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_TABLE_X_Y_Z_COORDINATES:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);

                            // need some target for processing
                            SetTargetMap(SpellEffectIndex(i), TARGET_EFFECT_SELECT, tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_AREAEFFECT_INSTANT:     // All 17/7 pairs used for dest teleportation, A processed in effect code
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_SELF2:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                        case TARGET_EFFECT_SELECT:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_AREAEFFECT_CUSTOM:
                            // triggered spells get dest point from default target set, ignore it
                            if (!(m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION) || m_IsTriggeredSpell)
                            {
                                if (WorldObject* castObject = GetCastingObject())
                                {
                                    m_targets.setDestination(castObject->GetPositionX(), castObject->GetPositionY(), castObject->GetPositionZ());
                                }
                            }
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                            // most A/B target pairs is self->negative and not expect adding caster to target list
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_DUELVSPLAYER_COORDINATES:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                        case TARGET_EFFECT_SELECT:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            if (Unit* currentTarget = m_targets.getUnitTarget())
                            {
                                tmpUnitLists[i /*==effToIndex[i]*/].push_back(currentTarget);
                            }
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
                case TARGET_SCRIPT:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_SELF:
                            // Fill target based on B only, A is only used with CheckCast here.
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    switch (m_spellInfo->EffectImplicitTargetB[i])
                    {
                        case TARGET_NONE:
                        case TARGET_EFFECT_SELECT:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        case TARGET_SCRIPT_COORDINATES:     // B case filled in CheckCast but we need fill unit list base at A case
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                        default:
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetA[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            SetTargetMap(SpellEffectIndex(i), m_spellInfo->EffectImplicitTargetB[i], tmpUnitLists[i /*==effToIndex[i]*/]);
                            break;
                    }
                    break;
            }
        }

        if (m_caster->GetTypeId() == TYPEID_PLAYER)
        {
            Player* me = (Player*)m_caster;
            for (UnitList::const_iterator itr = tmpUnitLists[effToIndex[i]].begin(); itr != tmpUnitLists[effToIndex[i]].end(); ++itr)
            {
                Player* targetOwner = (*itr)->GetCharmerOrOwnerPlayerOrPlayerItself();
                if (targetOwner && targetOwner != me && targetOwner->IsPvP() && !me->IsInDuelWith(targetOwner))
                {
                    me->UpdatePvP(true);
                    me->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);
                    break;
                }
            }
        }

        for (UnitList::iterator itr = tmpUnitLists[effToIndex[i]].begin(); itr != tmpUnitLists[effToIndex[i]].end();)
        {
            if (!CheckTarget(*itr, SpellEffectIndex(i)))
            {
                itr = tmpUnitLists[effToIndex[i]].erase(itr);
                continue;
            }
            else
            {
                ++itr;
            }
        }

        for (UnitList::const_iterator iunit = tmpUnitLists[effToIndex[i]].begin(); iunit != tmpUnitLists[effToIndex[i]].end(); ++iunit)
        {
            AddUnitTarget((*iunit), SpellEffectIndex(i));
        }
    }
}

/**
 * @brief Prepares proc-trigger metadata for the current spell cast.
 */
void Spell::prepareDataForTriggerSystem()
{
    //==========================================================================================
    // Now fill data for trigger system, need know:
    // an spell trigger another or not ( m_canTrigger )
    // Create base triggers flags for Attacker and Victim ( m_procAttacker and  m_procVictim)
    //==========================================================================================
    // Fill flag can spell trigger or not
    // TODO: possible exist spell attribute for this
    m_canTrigger = false;

    if (m_CastItem)
    {
        m_canTrigger = false;                                // Do not trigger from item cast spell
    }
    else if (!m_IsTriggeredSpell)
    {
        m_canTrigger = true;                                 // Normal cast - can trigger
    }
    else if (!m_triggeredByAuraSpell)
    {
        m_canTrigger = true;                                 // Triggered from SPELL_EFFECT_TRIGGER_SPELL - can trigger
    }

    if (!m_canTrigger)                                      // Exceptions (some periodic triggers)
    {
        switch (m_spellInfo->SpellFamilyName)
        {
            case SPELLFAMILY_MAGE:
                // Arcane Missiles / Blizzard triggers need do it
                if (m_spellInfo->IsFitToFamilyMask(UI64LIT(0x0000000000200080)))
                {
                    m_canTrigger = true;
                }
                break;
            case SPELLFAMILY_WARLOCK:
                // For Hellfire Effect / Rain of Fire / Seed of Corruption triggers need do it
                if (m_spellInfo->IsFitToFamilyMask(UI64LIT(0x0000800000000060)))
                {
                    m_canTrigger = true;
                }
                break;
            case SPELLFAMILY_HUNTER:
                // Hunter Explosive Trap Effect/Immolation Trap Effect/Frost Trap Aura/Snake Trap Effect
                if (m_spellInfo->IsFitToFamilyMask(UI64LIT(0x0000200000000014)))
                {
                    m_canTrigger = true;
                }
                break;
            case SPELLFAMILY_PALADIN:
                // For Holy Shock triggers need do it
                if (m_spellInfo->IsFitToFamilyMask(UI64LIT(0x0001000000200000)))
                {
                    m_canTrigger = true;
                }
                break;
            default:
                break;
        }
    }

    // Get data for type of attack and fill base info for trigger
    switch (m_spellInfo->DmgClass)
    {
        case SPELL_DAMAGE_CLASS_MELEE:
            m_procAttacker = PROC_FLAG_SUCCESSFUL_MELEE_SPELL_HIT;
            if (m_attackType == OFF_ATTACK)
            {
                m_procAttacker |= PROC_FLAG_SUCCESSFUL_OFFHAND_HIT;
            }
            m_procVictim   = PROC_FLAG_TAKEN_MELEE_SPELL_HIT;
            break;
        case SPELL_DAMAGE_CLASS_RANGED:
            // Auto attack
            if (m_spellInfo->HasAttribute(SPELL_ATTR_EX2_AUTOREPEAT_FLAG))
            {
                m_procAttacker = PROC_FLAG_SUCCESSFUL_RANGED_HIT;
                m_procVictim   = PROC_FLAG_TAKEN_RANGED_HIT;
            }
            else // Ranged spell attack
            {
                m_procAttacker = PROC_FLAG_SUCCESSFUL_RANGED_SPELL_HIT;
                m_procVictim   = PROC_FLAG_TAKEN_RANGED_SPELL_HIT;
            }
            break;
        default:
            if (IsPositiveSpell(m_spellInfo->Id))           // Check for positive spell
            {
                m_procAttacker = PROC_FLAG_SUCCESSFUL_POSITIVE_SPELL;
                m_procVictim   = PROC_FLAG_TAKEN_POSITIVE_SPELL;
            }
            else if (m_spellInfo->HasAttribute(SPELL_ATTR_EX2_AUTOREPEAT_FLAG))   // Wands auto attack
            {
                m_procAttacker = PROC_FLAG_SUCCESSFUL_RANGED_HIT;
                m_procVictim   = PROC_FLAG_TAKEN_RANGED_HIT;
            }
            else                                           // Negative spell
            {
                m_procAttacker = PROC_FLAG_SUCCESSFUL_NEGATIVE_SPELL_HIT;
                m_procVictim   = PROC_FLAG_TAKEN_NEGATIVE_SPELL_HIT;
            }
            break;
    }

    // some negative spells have positive effects to another or same targets
    // avoid triggering negative hit for only positive targets
    m_negativeEffectMask = 0x0;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (!IsPositiveEffect(m_spellInfo, SpellEffectIndex(i)))
        {
            m_negativeEffectMask |= (1 << i);
        }
    }

    // Hunter traps spells (for Entrapment trigger)
    // Gives your Immolation Trap, Frost Trap, Explosive Trap, and Snake Trap ....
    if (m_spellInfo->SpellFamilyName == SPELLFAMILY_HUNTER && (m_spellInfo->SpellFamilyFlags & UI64LIT(0x000020000000001C)))
    {
        m_procAttacker |= PROC_FLAG_ON_TRAP_ACTIVATION;
    }
}

/**
 * @brief Clears all accumulated target lists and delay tracking.
 */
void Spell::CleanupTargetList()
{
    m_UniqueTargetInfo.clear();
    m_UniqueGOTargetInfo.clear();
    m_UniqueItemInfo.clear();
    m_delayMoment = 0;
}

/**
 * @brief Adds a unit target entry for a spell effect.
 *
 * @param pVictim The unit target.
 * @param effIndex The effect index being applied.
 */
void Spell::AddUnitTarget(Unit* pVictim, SpellEffectIndex effIndex)
{
    if (m_spellInfo->Effect[effIndex] == 0)
    {
        return;
    }

    // Check for effect immune skip if immuned
    bool immuned = pVictim->IsImmuneToSpellEffect(m_spellInfo, effIndex, pVictim == m_caster);

    ObjectGuid targetGUID = pVictim->GetObjectGuid();

    // Lookup target in already in list
    for (TargetList::iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (targetGUID == ihit->targetGUID)                 // Found in list
        {
            if (!immuned)
            {
                ihit->effectMask |= 1 << effIndex;          // Add only effect mask if not immuned
            }
            return;
        }
    }

    // This is new target calculate data for him

    // Get spell hit result on target
    TargetInfo target;
    target.targetGUID = targetGUID;                         // Store target GUID
    target.effectMask = immuned ? 0 : (1 << effIndex);      // Store index of effect if not immuned
    target.processed  = false;                              // Effects not applied on target

    // Calculate hit result
    target.missCondition = m_caster->SpellHitResult(pVictim, m_spellInfo, m_canReflect);

    // spell fly from visual cast object
    WorldObject* affectiveObject = GetAffectiveCasterObject();

    // Spell have speed (possible inherited from triggering spell) - need calculate incoming time
    float speed = m_spellInfo->speed == 0.0f && m_triggeredBySpellInfo ? m_triggeredBySpellInfo->speed : m_spellInfo->speed;
    if (speed > 0.0f && affectiveObject && (pVictim != affectiveObject || (m_targets.m_targetMask & (TARGET_FLAG_SOURCE_LOCATION | TARGET_FLAG_DEST_LOCATION))))
    {
        // calculate spell incoming interval
        float dist;                                         // distance to impact
        if (pVictim == affectiveObject)                     // Calculate dist to destination target also for self-cast spells
        {
            if (m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION)
            {
                dist = affectiveObject->GetDistance(m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ);
            }
            else                                            // Must have Source Target
            {
                dist = affectiveObject->GetDistance(m_targets.m_srcX, m_targets.m_srcY, m_targets.m_srcZ);
            }
        }
        else                                                // normal unit target, take distance
        {
            dist = affectiveObject->GetDistance(pVictim->GetPositionX(), pVictim->GetPositionY(), pVictim->GetPositionZ());
        }

        if (dist < 5.0f)
        {
            dist = 5.0f;
        }
        target.timeDelay = (uint64) floor(dist / speed * 1000.0f);

        // Calculate minimum incoming time
        if (m_delayMoment == 0 || m_delayMoment > target.timeDelay)
        {
            m_delayMoment = target.timeDelay;
        }
    }
    else
    {
        target.timeDelay = UI64LIT(0);
    }

    // If target reflect spell back to caster
    if (target.missCondition == SPELL_MISS_REFLECT)
    {
        // Calculate reflected spell result on caster
        target.reflectResult =  m_caster->SpellHitResult(m_caster, m_spellInfo, m_canReflect);

        if (target.reflectResult == SPELL_MISS_REFLECT)     // Impossible reflect again, so simply deflect spell
        {
            target.reflectResult = SPELL_MISS_PARRY;
        }

        // Increase time interval for reflected spells by 1.5
        target.timeDelay += target.timeDelay >> 1;
    }
    else
    {
        target.reflectResult = SPELL_MISS_NONE;
    }

    // Add target to list
    m_UniqueTargetInfo.push_back(target);
}

/**
 * @brief Resolves and adds a unit target by guid for a spell effect.
 *
 * @param unitGuid The unit guid to resolve.
 * @param effIndex The effect index being applied.
 */
void Spell::AddUnitTarget(ObjectGuid unitGuid, SpellEffectIndex effIndex)
{
    if (Unit* unit = m_caster->GetObjectGuid() == unitGuid ? m_caster : sObjectAccessor.GetUnit(*m_caster, unitGuid))
    {
        AddUnitTarget(unit, effIndex);
    }
}

/**
 * @brief Adds a game object target entry for a spell effect.
 *
 * @param pVictim The game object target.
 * @param effIndex The effect index being applied.
 */
void Spell::AddGOTarget(GameObject* pVictim, SpellEffectIndex effIndex)
{
    if (m_spellInfo->Effect[effIndex] == 0)
    {
        return;
    }

    ObjectGuid targetGUID = pVictim->GetObjectGuid();

    // Lookup target in already in list
    for (GOTargetList::iterator ihit = m_UniqueGOTargetInfo.begin(); ihit != m_UniqueGOTargetInfo.end(); ++ihit)
    {
        if (targetGUID == ihit->targetGUID)                 // Found in list
        {
            ihit->effectMask |= (1 << effIndex);            // Add only effect mask
            return;
        }
    }

    // This is new target calculate data for him

    GOTargetInfo target;
    target.targetGUID = targetGUID;
    target.effectMask = (1 << effIndex);
    target.processed  = false;                              // Effects not apply on target

    // spell fly from visual cast object
    WorldObject* affectiveObject = GetAffectiveCasterObject();

    // Spell can have speed - need calculate incoming time
    float speed = m_spellInfo->speed == 0.0f && m_triggeredBySpellInfo ? m_triggeredBySpellInfo->speed : m_spellInfo->speed;
    if (speed > 0.0f && affectiveObject && pVictim != affectiveObject)
    {
        // calculate spell incoming interval
        float dist = affectiveObject->GetDistance(pVictim->GetPositionX(), pVictim->GetPositionY(), pVictim->GetPositionZ());
        if (dist < 5.0f)
        {
            dist = 5.0f;
        }
        target.timeDelay = (uint64) floor(dist / speed * 1000.0f);
        if (m_delayMoment == 0 || m_delayMoment > target.timeDelay)
        {
            m_delayMoment = target.timeDelay;
        }
    }
    else
    {
        target.timeDelay = UI64LIT(0);
    }

    // Add target to list
    m_UniqueGOTargetInfo.push_back(target);
}

/**
 * @brief Resolves and adds a game object target by guid for a spell effect.
 *
 * @param goGuid The game object guid to resolve.
 * @param effIndex The effect index being applied.
 */
void Spell::AddGOTarget(ObjectGuid goGuid, SpellEffectIndex effIndex)
{
    if (GameObject* go = m_caster->GetMap()->GetGameObject(goGuid))
    {
        AddGOTarget(go, effIndex);
    }
}

/**
 * @brief Adds an item target entry for a spell effect.
 *
 * @param pitem The item target.
 * @param effIndex The effect index being applied.
 */
void Spell::AddItemTarget(Item* pitem, SpellEffectIndex effIndex)
{
    if (m_spellInfo->Effect[effIndex] == 0)
    {
        return;
    }

    // Lookup target in already in list
    for (ItemTargetList::iterator ihit = m_UniqueItemInfo.begin(); ihit != m_UniqueItemInfo.end(); ++ihit)
    {
        if (pitem == ihit->item)                            // Found in list
        {
            ihit->effectMask |= (1 << effIndex);            // Add only effect mask
            return;
        }
    }

    // This is new target add data

    ItemTargetInfo target;
    target.item       = pitem;
    target.effectMask = (1 << effIndex);
    m_UniqueItemInfo.push_back(target);
}

/**
 * @brief Checks whether required alive targets are present in the current target list.
 *
 * @return True if all required effects have a valid alive target; otherwise, false.
 */
bool Spell::IsAliveUnitPresentInTargetList()
{
    // Not need check return true
    if (m_needAliveTargetMask == 0)
    {
        return true;
    }

    uint8 needAliveTargetMask = m_needAliveTargetMask;

    for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (ihit->missCondition == SPELL_MISS_NONE && (needAliveTargetMask & ihit->effectMask))
        {
            Unit* unit = m_caster->GetObjectGuid() == ihit->targetGUID ? m_caster : sObjectAccessor.GetUnit(*m_caster, ihit->targetGUID);

            // either unit is alive and normal spell, or unit dead and deathonly-spell
            if (unit && (unit->IsAlive() != IsDeathOnlySpell(m_spellInfo)))
            {
                needAliveTargetMask &= ~ihit->effectMask;   // remove from need alive mask effect that have alive target
            }
        }
    }

    // is all effects from m_needAliveTargetMask have alive targets
    return needAliveTargetMask == 0;
}

// Helper for targets furthest away to the spell target

/**
 * @brief Prepares the spell cast, validates conditions, and starts cast processing.
 *
 * @param targets The resolved spell cast targets.
 * @param triggeredByAura The triggering aura, if this spell was aura-triggered.
 */
void Spell::prepare(SpellCastTargets const* targets, Aura* triggeredByAura)
{
    m_targets = *targets;

    m_spellState = SPELL_STATE_PREPARING;

    m_castPositionX = m_caster->GetPositionX();
    m_castPositionY = m_caster->GetPositionY();
    m_castPositionZ = m_caster->GetPositionZ();
    m_castOrientation = m_caster->GetOrientation();

    if (triggeredByAura)
    {
        m_triggeredByAuraSpell = triggeredByAura->GetSpellProto();
    }

    // create and add update event for this spell
    SpellEvent* Event = new SpellEvent(this);
    m_caster->m_Events.AddEvent(Event, m_caster->m_Events.CalculateTime(1));

    // Prevent casting at cast another spell (ServerSide check)
    if (m_caster->IsNonMeleeSpellCasted(false, true, true) && m_cast_count)
    {
        SendCastResult(SPELL_FAILED_SPELL_IN_PROGRESS);
        finish(false);
        return;
    }

    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, m_spellInfo->Id, m_caster))
    {
        SendCastResult(SPELL_FAILED_SPELL_UNAVAILABLE);
        finish(false);
        return;
    }

    // Fill cost data
    m_powerCost = CalculatePowerCost(m_spellInfo, m_caster, this, m_CastItem);

    SpellCastResult result = CheckCast(true);
    if (result != SPELL_CAST_OK && !IsAutoRepeat())         // always cast autorepeat dummy for triggering
    {
        if (triggeredByAura)
        {
            SendChannelUpdate(0);
            triggeredByAura->GetHolder()->SetAuraDuration(0);
        }
        SendCastResult(result);
        finish(false);
        return;
    }

    // Prepare data for triggers
    prepareDataForTriggerSystem();

    // calculate cast time (calculated after first CheckCast check to prevent charge counting for first CheckCast fail)
    m_casttime = GetSpellCastTime(m_spellInfo, this);
    m_duration = CalculateSpellDuration(m_spellInfo, m_caster);

    // set timer base at cast time
    ReSetTimer();

    // stealth must be removed at cast starting (at show channel bar)
    // skip triggered spell (item equip spell casting and other not explicit character casts/item uses)
    if (!m_IsTriggeredSpell && isSpellBreakStealth(m_spellInfo))
    {
        m_caster->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
        m_caster->RemoveSpellsCausingAura(SPELL_AURA_FEIGN_DEATH);
    }

    // add non-triggered (with cast time and without) or triggered channeled
    if (!m_IsTriggeredSpell || IsChanneledSpell(m_spellInfo))
    {
        // add to cast type slot
        m_caster->SetCurrentCastedSpell(this);

        // will show cast bar
        SendSpellStart();

        TriggerGlobalCooldown();
    }
    // execute triggered without cast time explicitly in call point
    else if (m_timer == 0)
    {
        cast(true);
    }
    // else triggered with cast time will execute execute at next tick or later
    // without adding to cast type slot
    // will not show cast bar but will show effects at casting time etc
}

/**
 * @brief Consumes or updates the cast item after spell use when required.
 */
void Spell::TakeCastItem()
{
    if (!m_CastItem || m_caster->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    // not remove cast item at triggered spell (equipping, weapon damage, etc)
    if (m_IsTriggeredSpell && !(m_targets.m_targetMask & TARGET_FLAG_TRADE_ITEM))
    {
        return;
    }

    ItemPrototype const* proto = m_CastItem->GetProto();

    if (!proto)
    {
        // This code is to avoid a crash
        // I'm not sure, if this is really an error, but I guess every item needs a prototype
        sLog.outError("Cast item (%s) has no item prototype", m_CastItem->GetGuidStr().c_str());
        return;
    }

    bool expendable = false;
    bool withoutCharges = false;

    for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
    {
        if (proto->Spells[i].SpellId)
        {
            // item has limited charges
            if (proto->Spells[i].SpellCharges)
            {
                if (proto->Spells[i].SpellCharges < 0 && !(proto->ExtraFlags & ITEM_EXTRA_NON_CONSUMABLE))
                {
                    expendable = true;
                }

                int32 charges = m_CastItem->GetSpellCharges(i);

                // item has charges left
                if (charges)
                {
                    (charges > 0) ? --charges : ++charges;  // abs(charges) less at 1 after use
                    if (proto->Stackable == 1)
                    {
                        m_CastItem->SetSpellCharges(i, charges);
                    }
                    m_CastItem->SetState(ITEM_CHANGED, (Player*)m_caster);
                }

                // all charges used
                withoutCharges = (charges == 0);
            }
        }
    }

    if (expendable && withoutCharges)
    {
        uint32 count = 1;
        ((Player*)m_caster)->DestroyItemCount(m_CastItem, count, true);

        // prevent crash at access to deleted m_targets.getItemTarget
        ClearCastItem();
    }
}

/**
 * @brief Deducts the spell power cost from the caster.
 */
void Spell::TakePower()
{
    if (m_CastItem || m_triggeredByAuraSpell || m_IsTriggeredSpell)
    {
        return;
    }

    // health as power used
    if (m_spellInfo->powerType == POWER_HEALTH)
    {
        m_caster->ModifyHealth(-(int32)m_powerCost);
        return;
    }

    if (m_spellInfo->powerType >= MAX_POWERS)
    {
        sLog.outError("Spell::TakePower: Unknown power type '%d'", m_spellInfo->powerType);
        return;
    }

    Powers powerType = Powers(m_spellInfo->powerType);

    m_caster->ModifyPower(powerType, -(int32)m_powerCost);

    // Set the five second timer
    if (powerType == POWER_MANA && m_powerCost > 0)
    {
        m_caster->SetLastManaUse();
    }
}


/**
 * @brief Consumes spell reagents from the player caster inventory.
 */
void Spell::TakeReagents()
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    if (IgnoreItemRequirements())                           // reagents used in triggered spell removed by original spell or don't must be removed.
    {
        return;
    }

    Player* p_caster = (Player*)m_caster;
    if (p_caster->CanNoReagentCast(m_spellInfo))
    {
        return;
    }

    for (uint32 x = 0; x < MAX_SPELL_REAGENTS; ++x)
    {
        if (m_spellInfo->Reagent[x] <= 0)
        {
            continue;
        }

        uint32 itemid = m_spellInfo->Reagent[x];
        uint32 itemcount = m_spellInfo->ReagentCount[x];

        // if CastItem is also spell reagent
        if (m_CastItem)
        {
            ItemPrototype const* proto = m_CastItem->GetProto();
            if (proto && proto->ItemId == itemid)
            {
                for (int s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
                {
                    // CastItem will be used up and does not count as reagent
                    int32 charges = m_CastItem->GetSpellCharges(s);
                    if (proto->Spells[s].SpellCharges < 0 && abs(charges) < 2)
                    {
                        ++itemcount;
                        break;
                    }
                }

                m_CastItem = NULL;
            }
        }

        // if getItemTarget is also spell reagent
        if (m_targets.getItemTargetEntry() == itemid)
        {
            m_targets.setItemTarget(NULL);
        }

        p_caster->DestroyItemCount(itemid, itemcount, true);
    }
}

/**
 * @brief Applies additional configured threat from spell_threat data.
 */
void Spell::HandleThreatSpells()
{
    if (m_UniqueTargetInfo.empty())
    {
        return;
    }

    SpellThreatEntry const* threatEntry = sSpellMgr.GetSpellThreatEntry(m_spellInfo->Id);

    if (!threatEntry || (!threatEntry->threat && threatEntry->ap_bonus == 0.0f))
    {
        return;
    }

    float threat = threatEntry->threat;
    if (threatEntry->ap_bonus != 0.0f)
    {
        threat += threatEntry->ap_bonus * m_caster->GetTotalAttackPowerValue(GetWeaponAttackType(m_spellInfo));
    }

    bool positive = true;
    uint8 effectMask = 0;
    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        if (m_spellInfo->Effect[i])
        {
            effectMask |= (1 << i);
        }
    }

    if (m_negativeEffectMask & effectMask)
    {
        // can only handle spells with clearly defined positive/negative effect, check at spell_threat loading probably not perfect
        // so abort when only some effects are negative.
        if ((m_negativeEffectMask & effectMask) != effectMask)
        {
            DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u, rank %u, is not clearly positive or negative, ignoring bonus threat", m_spellInfo->Id, sSpellMgr.GetSpellRank(m_spellInfo->Id));
            return;
        }
        positive = false;
    }

    // since 2.0.1 threat from positive effects also is distributed among all targets, so the overall caused threat is at most the defined bonus
    threat /= m_UniqueTargetInfo.size();

    for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if (ihit->missCondition != SPELL_MISS_NONE)
        {
            continue;
        }

        Unit* target = m_caster->GetObjectGuid() == ihit->targetGUID ? m_caster : sObjectAccessor.GetUnit(*m_caster, ihit->targetGUID);
        if (!target)
        {
            continue;
        }

        // positive spells distribute threat among all units that are in combat with target, like healing
        if (positive)
        {
            target->GetHostileRefManager().threatAssist(m_caster /*real_caster ??*/, threat, m_spellInfo);
        }
        // for negative spells threat gets distributed among affected targets
        else
        {
            if (!target->CanHaveThreatList())
            {
                continue;
            }

            target->AddThreat(m_caster, threat, false, GetSpellSchoolMask(m_spellInfo), m_spellInfo);
        }
    }

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u added an additional %f threat for %s %zu target(s)", m_spellInfo->Id, threat, positive ? "assisting" : "harming", m_UniqueTargetInfo.size());
}

/**
 * @brief Dispatches one spell effect against the current resolved targets.
 *
 * @param pUnitTarget The unit target, if any.
 * @param pItemTarget The item target, if any.
 * @param pGOTarget The game object target, if any.
 * @param i The effect index to process.
 * @param DamageMultiplier The damage multiplier to apply for the effect.
 */
void Spell::HandleEffects(Unit* pUnitTarget, Item* pItemTarget, GameObject* pGOTarget, SpellEffectIndex i, float DamageMultiplier)
{
    unitTarget = pUnitTarget;
    itemTarget = pItemTarget;
    gameObjTarget = pGOTarget;

    uint8 eff = m_spellInfo->Effect[i];

    damage = int32(CalculateDamage(i, unitTarget) * DamageMultiplier);

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u Effect%d : %u Targets: %s, %s, %s",
                     m_spellInfo->Id, i, eff,
                     unitTarget ? unitTarget->GetGuidStr().c_str() : "-",
                     itemTarget ? itemTarget->GetGuidStr().c_str() : "-",
                     gameObjTarget ? gameObjTarget->GetGuidStr().c_str() : "-");

    if (eff < TOTAL_SPELL_EFFECTS)
    {
        (*this.*SpellEffects[eff])(i);
    }
    else
    {
        sLog.outError("WORLD: Spell FX %d > TOTAL_SPELL_EFFECTS ", eff);
    }
}

/**
 * @brief Queues a spell to be triggered after successful completion.
 *
 * @param spellId The triggered spell identifier.
 */
void Spell::AddTriggeredSpell(uint32 spellId)
{
    SpellEntry const* spellInfo = sSpellStore.LookupEntry(spellId);

    if (!spellInfo)
    {
        sLog.outError("Spell::AddTriggeredSpell: unknown spell id %u used as triggred spell for spell %u)", spellId, m_spellInfo->Id);
        return;
    }

    m_TriggerSpells.push_back(spellInfo);
}

/**
 * @brief Queues a spell to be cast before applying effects to each target.
 *
 * @param spellId The precast spell identifier.
 */
void Spell::AddPrecastSpell(uint32 spellId)
{
    SpellEntry const* spellInfo = sSpellStore.LookupEntry(spellId);

    if (!spellInfo)
    {
        sLog.outError("Spell::AddPrecastSpell: unknown spell id %u used as pre-cast spell for spell %u)", spellId, m_spellInfo->Id);
        return;
    }

    m_preCastSpells.push_back(spellInfo);
}

/**
 * @brief Casts spells queued to trigger after the main spell completes successfully.
 */
void Spell::CastTriggerSpells()
{
    for (SpellInfoList::const_iterator si = m_TriggerSpells.begin(); si != m_TriggerSpells.end(); ++si)
    {
        Spell* spell = new Spell(m_caster, (*si), true, m_originalCasterGUID);
        spell->prepare(&m_targets);                         // use original spell original targets
    }
}

/**
 * @brief Casts queued precast spells on the provided target.
 *
 * @param target The unit target for the precast spells.
 */
void Spell::CastPreCastSpells(Unit* target)
{
    for (SpellInfoList::const_iterator si = m_preCastSpells.begin(); si != m_preCastSpells.end(); ++si)
    {
        m_caster->CastSpell(target, (*si), true, m_CastItem);
    }
}

/**
 * @brief Gets the first queued unit target guid for an effect, falling back to the explicit target guid.
 *
 * @param effIndex The effect index to inspect.
 * @return The matching unit target guid, or the explicit unit target guid when none is queued.
 */
Unit* Spell::GetPrefilledUnitTargetOrUnitTarget(SpellEffectIndex effIndex) const
{
    for (TargetList::const_iterator itr = m_UniqueTargetInfo.begin(); itr != m_UniqueTargetInfo.end(); ++itr)
    {
        if (itr->effectMask & (1 << effIndex))
        {
            return m_caster->GetMap()->GetUnit(itr->targetGUID);
        }
    }

    return m_targets.getUnitTarget();
}

/**
 * @brief Applies spell pushback delay to a currently casting player spell.
 */
void Spell::Delayed()
{
    if (!m_caster || m_caster->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    if (m_spellState == SPELL_STATE_DELAYED)
    {
        return;                                              // spell is active and can't be time-backed
    }

    // spells not losing casting time ( slam, dynamites, bombs.. )
    if (!(m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_DAMAGE))
    {
        return;
    }

    // check resist chance
    int32 resistChance = 100;                               // must be initialized to 100 for percent modifiers
    ((Player*)m_caster)->ApplySpellMod(m_spellInfo->Id, SPELLMOD_NOT_LOSE_CASTING_TIME, resistChance, this);
    resistChance += m_caster->GetTotalAuraModifier(SPELL_AURA_RESIST_PUSHBACK) - 100;
    if (roll_chance_i(resistChance))
    {
        return;
    }

    int32 delaytime = GetNextDelayAtDamageMsTime();

    if (int32(m_timer) + delaytime > m_casttime)
    {
        delaytime = m_casttime - m_timer;
        m_timer = m_casttime;
    }
    else
    {
        m_timer += delaytime;
    }

    DETAIL_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u partially interrupted for (%d) ms at damage", m_spellInfo->Id, delaytime);

    WorldPacket data(SMSG_SPELL_DELAYED, 8 + 4);
    data << m_caster->GetPackGUID();
    data << uint32(delaytime);

    m_caster->SendMessageToSet(&data, true);
}

/**
 * @brief Applies pushback to an active channeled spell and linked aura durations.
 */
void Spell::DelayedChannel()
{
    if (!m_caster || m_caster->GetTypeId() != TYPEID_PLAYER || getState() != SPELL_STATE_CASTING)
    {
        return;
    }

    // check resist chance
    int32 resistChance = 100;                               // must be initialized to 100 for percent modifiers
    ((Player*)m_caster)->ApplySpellMod(m_spellInfo->Id, SPELLMOD_NOT_LOSE_CASTING_TIME, resistChance, this);
    resistChance += m_caster->GetTotalAuraModifier(SPELL_AURA_RESIST_PUSHBACK) - 100;
    if (roll_chance_i(resistChance))
    {
        return;
    }

    int32 delaytime = GetNextDelayAtDamageMsTime();

    if (int32(m_timer) < delaytime)
    {
        delaytime = m_timer;
        m_timer = 0;
    }
    else
    {
        m_timer -= delaytime;
    }

    DEBUG_FILTER_LOG(LOG_FILTER_SPELL_CAST, "Spell %u partially interrupted for %i ms, new duration: %u ms", m_spellInfo->Id, delaytime, m_timer);

    for (TargetList::const_iterator ihit = m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end(); ++ihit)
    {
        if ((*ihit).missCondition == SPELL_MISS_NONE)
        {
            if (Unit* unit = m_caster->GetObjectGuid() == ihit->targetGUID ? m_caster : sObjectAccessor.GetUnit(*m_caster, ihit->targetGUID))
            {
                unit->DelaySpellAuraHolder(m_spellInfo->Id, delaytime, unit->GetObjectGuid());
            }
        }
    }

    for (int j = 0; j < MAX_EFFECT_INDEX; ++j)
    {
        // partially interrupt persistent area auras
        if (DynamicObject* dynObj = m_caster->GetDynObject(m_spellInfo->Id, SpellEffectIndex(j)))
        {
            dynObj->Delay(delaytime);
        }
    }

    SendChannelUpdate(m_timer);
}

/**
 * @brief Refreshes the cached original caster pointer from the stored guid.
 */
void Spell::UpdateOriginalCasterPointer()
{
    if (m_originalCasterGUID == m_caster->GetObjectGuid())
    {
        m_originalCaster = m_caster;
    }
    else if (m_originalCasterGUID.IsGameObject())
    {
        GameObject* go = m_caster->IsInWorld() ? m_caster->GetMap()->GetGameObject(m_originalCasterGUID) : NULL;
        m_originalCaster = go ? go->GetOwner() : NULL;
    }
    else
    {
        Unit* unit = sObjectAccessor.GetUnit(*m_caster, m_originalCasterGUID);
        m_originalCaster = unit && unit->IsInWorld() ? unit : NULL;
    }
}

/**
 * @brief Refreshes cached caster and target pointers from stored guids.
 */
void Spell::UpdatePointers()
{
    UpdateOriginalCasterPointer();

    m_targets.Update(m_caster);
}

/**
 * @brief Checks whether a target matches the spell's creature type restrictions.
 *
 * @param target The target being validated.
 * @return True if the target type is allowed; otherwise, false.
 */
bool Spell::CheckTargetCreatureType(Unit* target) const
{
    uint32 spellCreatureTargetMask = m_spellInfo->TargetCreatureType;

    // Curse of Doom : not find another way to fix spell target check :/
    if (m_spellInfo->IsFitToFamily(SPELLFAMILY_WARLOCK, UI64LIT(0x0000000200000000)))
    {
        // not allow cast at player
        if (target->GetTypeId() == TYPEID_PLAYER)
        {
            return false;
        }

        spellCreatureTargetMask = 0x7FF;
    }

    // Dismiss Pet, Taming Lesson and Control Robot skipped
    if (m_spellInfo->Id == 2641 || m_spellInfo->Id == 23356 || m_spellInfo->Id == 30009)
    {
        spellCreatureTargetMask =  0;
    }

    if (spellCreatureTargetMask)
    {
        uint32 TargetCreatureType = target->GetCreatureTypeMask();

        return !TargetCreatureType || (spellCreatureTargetMask & TargetCreatureType);
    }
    return true;
}

/**
 * @brief Gets the current spell container slot used by this spell.
 *
 * @return The current spell container type.
 */
CurrentSpellTypes Spell::GetCurrentContainer()
{
    if (IsNextMeleeSwingSpell())
    {
        return (CURRENT_MELEE_SPELL);
    }
    else if (IsAutoRepeat())
    {
        return (CURRENT_AUTOREPEAT_SPELL);
    }
    else if (IsChanneledSpell(m_spellInfo))
    {
        return (CURRENT_CHANNELED_SPELL);
    }
    else
    {
        return (CURRENT_GENERIC_SPELL);
    }
}

/**
 * @brief Validates whether a candidate target is acceptable for a specific effect.
 *
 * @param target The target being checked.
 * @param eff The effect index being validated.
 * @return True if the target is valid for the effect; otherwise, false.
 */
bool Spell::CheckTarget(Unit* target, SpellEffectIndex eff)
{
    // Check targets for creature type mask and remove not appropriate (skip explicit self target case, maybe need other explicit targets)
    if (m_spellInfo->EffectImplicitTargetA[eff] != TARGET_SELF)
    {
        if (!CheckTargetCreatureType(target))
        {
            return false;
        }
    }

    // Check targets for not_selectable unit flag and remove
    // A player can cast spells on his pet (or other controlled unit) though in any state
    if (target != m_caster && target->GetCharmerOrOwnerGuid() != m_caster->GetObjectGuid())
    {
        // any unattackable target skipped
        if (target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE))
        {
            return false;
        }

        // unselectable targets skipped in all cases except TARGET_SCRIPT targeting
        // in case TARGET_SCRIPT target selected by server always and can't be cheated
        if ((!m_IsTriggeredSpell || target != m_targets.getUnitTarget()) &&
            target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE) &&
            m_spellInfo->EffectImplicitTargetA[eff] != TARGET_SCRIPT &&
            m_spellInfo->EffectImplicitTargetB[eff] != TARGET_SCRIPT &&
            m_spellInfo->EffectImplicitTargetA[eff] != TARGET_AREAEFFECT_INSTANT &&
            m_spellInfo->EffectImplicitTargetB[eff] != TARGET_AREAEFFECT_INSTANT &&
            m_spellInfo->EffectImplicitTargetA[eff] != TARGET_AREAEFFECT_CUSTOM &&
            m_spellInfo->EffectImplicitTargetB[eff] != TARGET_AREAEFFECT_CUSTOM &&
            m_spellInfo->EffectImplicitTargetA[eff] != TARGET_NARROW_FRONTAL_CONE &&
            m_spellInfo->EffectImplicitTargetB[eff] != TARGET_NARROW_FRONTAL_CONE)
        {
            return false;
        }
    }

    // Check player targets and remove if in GM mode or GM invisibility (for not self casting case)
    if (target != m_caster && target->GetTypeId() == TYPEID_PLAYER)
    {
        if (((Player*)target)->GetVisibility() == VISIBILITY_OFF)
        {
            return false;
        }

        if (((Player*)target)->isGameMaster() && !IsPositiveSpell(m_spellInfo->Id))
        {
            return false;
        }
    }

    // Check targets for LOS visibility (except spells without range limitations )
    if (!DisableMgr::IsDisabledFor(DISABLE_TYPE_SPELL, m_spellInfo->Id, NULL, SPELL_DISABLE_LOS))
    {
        switch (m_spellInfo->Effect[eff])
        {
            case SPELL_EFFECT_SUMMON_PLAYER:                    // from anywhere
                break;
            case SPELL_EFFECT_DUMMY:
                if (m_spellInfo->Id != 20577)                   // Cannibalize
                {
                    break;
                }
                // fall through
            case SPELL_EFFECT_RESURRECT_NEW:
                // player far away, maybe his corpse near?
                if (target != m_caster && !target->IsWithinLOSInMap(m_caster))
                {
                    if (!m_targets.getCorpseTargetGuid())
                    {
                        return false;
                    }

                    Corpse* corpse = m_caster->GetMap()->GetCorpse(m_targets.getCorpseTargetGuid());
                    if (!corpse)
                    {
                        return false;
                    }

                    if (target->GetObjectGuid() != corpse->GetOwnerGuid())
                    {
                        return false;
                    }

                    if (!corpse->IsWithinLOSInMap(m_caster))
                    {
                        return false;
                    }
                }

                // all ok by some way or another, skip normal check
                break;
            default:                                            // normal case
            {
                // Get GO cast coordinates if original caster -> GO
                if (target != m_caster)
                {
                    if (WorldObject* caster = GetCastingObject())
                    {
                        if (!target->IsWithinLOSInMap(caster))
                        {
                            return false;
                        }
                    }
                }
                break;
            }
        }
    }

    if (target->GetTypeId() != TYPEID_PLAYER && m_spellInfo->HasAttribute(SPELL_ATTR_EX3_TARGET_ONLY_PLAYER)
        && m_spellInfo->EffectImplicitTargetA[eff] != TARGET_SCRIPT && m_spellInfo->EffectImplicitTargetA[eff] != TARGET_SELF)
    {
        return false;
    }

    switch (m_spellInfo->Id)
    {
        case 37433:                                         // Spout (The Lurker Below), only players affected if its not in water
            if (target->GetTypeId() != TYPEID_PLAYER || target->IsInWater())
            {
                return false;
            }
            break;
        default:
            break;
    }

    return true;
}

/**
 * @brief Checks whether this spell cast should produce client-visible packets.
 *
 * @return True if packets should be sent to clients; otherwise, false.
 */
bool Spell::IsNeedSendToClient() const
{
    return m_spellInfo->SpellVisual != 0 || IsChanneledSpell(m_spellInfo) ||
           m_spellInfo->speed > 0.0f || (!m_triggeredByAuraSpell && !m_IsTriggeredSpell);
}

/**
 * @brief Checks whether the triggered spell still requires redundant cast-time handling.
 *
 * @return True if redundant cast-time handling is needed; otherwise, false.
 */
bool Spell::IsTriggeredSpellWithRedundentCastTime() const
{
    return m_IsTriggeredSpell && (m_spellInfo->manaCost || m_spellInfo->ManaCostPercentage);
}

/**
 * @brief Checks whether any queued target entry contains a given effect.
 *
 * @param effect The effect index to look for.
 * @return True if at least one target has the effect queued; otherwise, false.
 */
bool Spell::HaveTargetsForEffect(SpellEffectIndex effect) const
{
    for (TargetList::const_iterator itr = m_UniqueTargetInfo.begin(); itr != m_UniqueTargetInfo.end(); ++itr)
    {
        if (itr->effectMask & (1 << effect))
        {
            return true;
        }
    }

    for (GOTargetList::const_iterator itr = m_UniqueGOTargetInfo.begin(); itr != m_UniqueGOTargetInfo.end(); ++itr)
    {
        if (itr->effectMask & (1 << effect))
        {
            return true;
        }
    }

    for (ItemTargetList::const_iterator itr = m_UniqueItemInfo.begin(); itr != m_UniqueItemInfo.end(); ++itr)
    {
        if (itr->effectMask & (1 << effect))
        {
            return true;
        }
    }

    return false;
}

SpellEvent::SpellEvent(Spell* spell) : BasicEvent()
{
    m_Spell = spell;
}

SpellEvent::~SpellEvent()
{
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
    {
        m_Spell->cancel();
    }

    if (m_Spell->IsDeletable())
    {
        delete m_Spell;
    }
    else
    {
        sLog.outError("~SpellEvent: %s %u tried to delete non-deletable spell %u. Was not deleted, causes memory leak.",
                      (m_Spell->GetCaster()->GetTypeId() == TYPEID_PLAYER ? "Player" : "Creature"), m_Spell->GetCaster()->GetGUIDLow(), m_Spell->m_spellInfo->Id);
    }
}

/**
 * @brief Advances spell execution within the event queue.
 *
 * @param e_time The event execution time.
 * @param p_time The elapsed update time in milliseconds.
 * @return True when the event is complete and can be removed; otherwise, false.
 */
bool SpellEvent::Execute(uint64 e_time, uint32 p_time)
{
    // update spell if it is not finished
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
    {
        m_Spell->update(p_time);
    }

    // check spell state to process
    switch (m_Spell->getState())
    {
        case SPELL_STATE_FINISHED:
        {
            // spell was finished, check deletable state
            if (m_Spell->IsDeletable())
            {
                // check, if we do have unfinished triggered spells
                return true;                                // spell is deletable, finish event
            }
            // event will be re-added automatically at the end of routine)
            break;
        }
        case SPELL_STATE_CASTING:
        {
            // this spell is in channeled state, process it on the next update
            // event will be re-added automatically at the end of routine)
            break;
        }
        case SPELL_STATE_DELAYED:
        {
            // first, check, if we have just started
            if (m_Spell->GetDelayStart() != 0)
            {
                // no, we aren't, do the typical update
                // check, if we have channeled spell on our hands
                if (IsChanneledSpell(m_Spell->m_spellInfo))
                {
                    // evented channeled spell is processed separately, casted once after delay, and not destroyed till finish
                    // check, if we have casting anything else except this channeled spell and autorepeat
                    if (m_Spell->GetCaster()->IsNonMeleeSpellCasted(false, true, true))
                    {
                        // another non-melee non-delayed spell is casted now, abort
                        m_Spell->cancel();
                    }
                    else
                    {
                        // do the action (pass spell to channeling state)
                        m_Spell->handle_immediate();
                    }
                    // event will be re-added automatically at the end of routine)
                }
                else
                {
                    // run the spell handler and think about what we can do next
                    uint64 t_offset = e_time - m_Spell->GetDelayStart();
                    uint64 n_offset = m_Spell->handle_delayed(t_offset);
                    if (n_offset)
                    {
                        // re-add us to the queue
                        m_Spell->GetCaster()->m_Events.AddEvent(this, m_Spell->GetDelayStart() + n_offset, false);
                        return false;                       // event not complete
                    }
                    // event complete
                    // finish update event will be re-added automatically at the end of routine)
                }
            }
            else
            {
                // delaying had just started, record the moment
                m_Spell->SetDelayStart(e_time);
                // re-plan the event for the delay moment
                m_Spell->GetCaster()->m_Events.AddEvent(this, e_time + m_Spell->GetDelayMoment(), false);
                return false;                               // event not complete
            }
            break;
        }
        default:
        {
            // all other states
            // event will be re-added automatically at the end of routine)
            break;
        }
    }

    // spell processing not complete, plan event on the next update interval
    m_Spell->GetCaster()->m_Events.AddEvent(this, e_time + 1, false);
    return false;                                           // event not complete
}

/**
 * @brief Aborts the queued spell event and cancels the spell if needed.
 */
void SpellEvent::Abort(uint64 /*e_time*/)
{
    // oops, the spell we try to do is aborted
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
    {
        m_Spell->cancel();
    }
}

/**
 * @brief Checks whether the underlying spell can be deleted.
 *
 * @return True if the spell is deletable; otherwise, false.
 */
bool SpellEvent::IsDeletable() const
{
    return m_Spell->IsDeletable();
}

/**
 * @brief Validates whether the caster can open a lock with this spell effect.
 *
 * @param effIndex The effect index performing the open-lock action.
 * @param lockId The lock identifier.
 * @param skillId Receives the required skill type.
 * @param reqSkillValue Receives the required skill value.
 * @param skillValue Receives the caster's effective skill value.
 * @return The resulting cast status.
 */
SpellCastResult Spell::CanOpenLock(SpellEffectIndex effIndex, uint32 lockId, SkillType& skillId, int32& reqSkillValue, int32& skillValue)
{
    if (!lockId)                                            // possible case for GO and maybe for items.
    {
        return SPELL_CAST_OK;
    }

    // Get LockInfo
    LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);

    if (!lockInfo)
    {
        return SPELL_FAILED_BAD_TARGETS;
    }

    bool reqKey = false;                                    // some locks not have reqs

    for (int j = 0; j < 8; ++j)
    {
        switch (lockInfo->Type[j])
        {
                // check key item (many fit cases can be)
            case LOCK_KEY_ITEM:
            {
                if (lockInfo->Index[j] && m_CastItem && m_CastItem->GetEntry() == lockInfo->Index[j])
                {
                    return SPELL_CAST_OK;
                }
                reqKey = true;
                break;
                // check key skill (only single first fit case can be)
            }
            case LOCK_KEY_SKILL:
            {
                reqKey = true;

                // wrong locktype, skip
                if (uint32(m_spellInfo->EffectMiscValue[effIndex]) != lockInfo->Index[j])
                {
                    continue;
                }

                skillId = SkillByLockType(LockType(lockInfo->Index[j]));

                if (skillId != SKILL_NONE)
                {
                    // skill bonus provided by casting spell (mostly item spells)
                    // add the damage modifier from the spell casted (cheat lock / skeleton key etc.) (use m_currentBasePoints, CalculateDamage returns wrong value)
                    uint32 spellSkillBonus = uint32(m_currentBasePoints[effIndex]);
                    reqSkillValue = lockInfo->Skill[j];

                    // castitem check: rogue using skeleton keys. the skill values should not be added in this case.
                    skillValue = m_CastItem || m_caster->GetTypeId() != TYPEID_PLAYER ?
                                 0 : ((Player*)m_caster)->GetSkillValue(skillId);

                    skillValue += spellSkillBonus;

                    if (skillValue < reqSkillValue)
                    {
                        return SPELL_FAILED_LOW_CASTLEVEL;
                    }
                }

                return SPELL_CAST_OK;
            }
        }
    }

    if (reqKey)
    {
        return SPELL_FAILED_BAD_TARGETS;
    }

    return SPELL_CAST_OK;
}

/**
 * Fill target list by units around (x,y) points at radius distance

 * @param targetUnitMap        Reference to target list that filled by function
 * @param radius               Radius around (x,y) for target search
 * @param pushType             Additional rules for target area selection (in front, angle, etc)
 * @param spellTargets         Additional rules for target selection base at hostile/friendly state to original spell caster
 */
void Spell::FillAreaTargets(UnitList& targetUnitMap, float radius, SpellNotifyPushType pushType, SpellTargets spellTargets, WorldObject* originalCaster /*=NULL*/)
{
    MaNGOS::SpellNotifierCreatureAndPlayer notifier(*this, targetUnitMap, radius, pushType, spellTargets, originalCaster);
    Cell::VisitAllObjects(notifier.GetCenterX(), notifier.GetCenterY(), m_caster->GetMap(), notifier, radius);
}

/**
 * @brief Fills a target list with party or raid members around a reference unit.
 *
 * @param targetUnitMap The target list being populated.
 * @param member The reference member.
 * @param radius The search radius.
 * @param raid True to include the whole raid; false to limit to the subgroup.
 * @param withPets True to include pets.
 * @param withcaster True to include the caster when applicable.
 */
void Spell::FillRaidOrPartyTargets(UnitList& targetUnitMap, Unit* member, float radius, bool raid, bool withPets, bool withcaster)
{
    Player* pMember = member->GetCharmerOrOwnerPlayerOrPlayerItself();
    Group* pGroup = pMember ? pMember->GetGroup() : NULL;

    if (pGroup)
    {
        uint8 subgroup = pMember->GetSubGroup();

        for (GroupReference* itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
        {
            Player* Target = itr->getSource();

            // IsHostileTo check duel and controlled by enemy
            if (Target && (raid || subgroup == Target->GetSubGroup())
                && !m_caster->IsHostileTo(Target))
            {
                if ((Target == m_caster && withcaster) ||
                    (Target != m_caster && m_caster->IsWithinDistInMap(Target, radius)))
                {
                    targetUnitMap.push_back(Target);
                }

                if (withPets)
                {
                    if (Pet* pet = Target->GetPet())
                    {
                        if ((pet == m_caster && withcaster) ||
                            (pet != m_caster && m_caster->IsWithinDistInMap(pet, radius)))
                        {
                            targetUnitMap.push_back(pet);
                        }
                    }
                }
            }
        }
    }
    else
    {
        Unit* ownerOrSelf = pMember ? pMember : member->GetCharmerOrOwnerOrSelf();
        if ((ownerOrSelf == m_caster && withcaster) ||
            (ownerOrSelf != m_caster && m_caster->IsWithinDistInMap(ownerOrSelf, radius)))
        {
            targetUnitMap.push_back(ownerOrSelf);
        }

        if (withPets)
        {
            if (Pet* pet = ownerOrSelf->GetPet())
            {
                if ((pet == m_caster && withcaster) ||
                    (pet != m_caster && m_caster->IsWithinDistInMap(pet, radius)))
                {
                    targetUnitMap.push_back(pet);
                }
            }
        }
    }
}

/**
 * @brief Gets the world object that should be used as the effective spell origin.
 *
 * @return The effective caster world object.
 */
WorldObject* Spell::GetAffectiveCasterObject() const
{
    if (!m_originalCasterGUID)
    {
        return m_caster;
    }

    if (m_originalCasterGUID.IsGameObject() && m_caster->IsInWorld())
    {
        return m_caster->GetMap()->GetGameObject(m_originalCasterGUID);
    }
    return m_originalCaster;
}

/**
 * @brief Gets the world object used for cast-position and line-of-sight calculations.
 *
 * @return The casting world object.
 */
WorldObject* Spell::GetCastingObject() const
{
    if (m_originalCasterGUID.IsGameObject())
    {
        return m_caster->IsInWorld() ? m_caster->GetMap()->GetGameObject(m_originalCasterGUID) : NULL;
    }
    else
    {
        return m_caster;
    }
}

/**
 * @brief Clears the accumulated effect damage and healing counters.
 */
void Spell::ResetEffectDamageAndHeal()
{
    m_damage = 0;
    m_healing = 0;
}

void Spell::SelectMountByAreaAndSkill(Unit* target, SpellEntry const* parentSpell, uint32 spellId75, uint32 spellId150, uint32 spellId225, uint32 spellId300, uint32 spellIdSpecial)
{
    if (!target || target->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    // Prevent stacking of mounts
    target->RemoveSpellsCausingAura(SPELL_AURA_MOUNTED);
    uint16 skillval = ((Player*)target)->GetSkillValue(SKILL_RIDING);
    if (!skillval)
    {
        return;
    }

    if (skillval >= 225 && (spellId300 > 0 || spellId225 > 0))
    {
        uint32 spellid = skillval >= 300 ? spellId300 : spellId225;
        SpellEntry const* pSpell = sSpellStore.LookupEntry(spellid);
        if (!pSpell)
        {
            sLog.outError("SelectMountByAreaAndSkill: unknown spell id %i by caster: %s", spellid, target->GetGuidStr().c_str());
            return;
        }

        // zone check
        uint32 zone, area;
        target->GetZoneAndAreaId(zone, area);

        SpellCastResult locRes = sSpellMgr.GetSpellAllowedInLocationError(pSpell, target->GetMapId(), zone, area, target->GetCharmerOrOwnerPlayerOrPlayerItself());
        if (locRes != SPELL_CAST_OK)
        {
            target->CastSpell(target, spellId150, true, NULL, NULL, ObjectGuid(), parentSpell);
        }
        else if (spellIdSpecial > 0)
        {
            for (PlayerSpellMap::const_iterator iter = ((Player*)target)->GetSpellMap().begin(); iter != ((Player*)target)->GetSpellMap().end(); ++iter)
            {
                if (iter->second.state != PLAYERSPELL_REMOVED)
                {
                    SpellEntry const* spellInfo = sSpellStore.LookupEntry(iter->first);
                    for (int i = 0; i < MAX_EFFECT_INDEX; ++i)
                    {
                        if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_FLIGHT_SPEED_MOUNTED)
                        {
                            int32 mountSpeed = spellInfo->CalculateSimpleValue(SpellEffectIndex(i));

                            // speed higher than 280 replace it
                            if (mountSpeed > 280)
                            {
                                target->CastSpell(target, spellIdSpecial, true, NULL, NULL, ObjectGuid(), parentSpell);
                                return;
                            }
                        }
                    }
                }
            }
            target->CastSpell(target, pSpell, true, NULL, NULL, ObjectGuid(), parentSpell);
        }
        else
        {
            target->CastSpell(target, pSpell, true, NULL, NULL, ObjectGuid(), parentSpell);
        }
    }
    else if (skillval >= 150 && spellId150 > 0)
    {
        target->CastSpell(target, spellId150, true, NULL, NULL, ObjectGuid(), parentSpell);
    }
    else if (spellId75 > 0)
    {
        target->CastSpell(target, spellId75, true, NULL, NULL, ObjectGuid(), parentSpell);
    }

    return;
}

/**
 * @brief Clears the cached cast item and unlinks it from target data when necessary.
 */
void Spell::ClearCastItem()
{
    if (m_CastItem == m_targets.getItemTarget())
    {
        m_targets.setItemTarget(NULL);
    }

    m_CastItem = NULL;
}

/**
 * @brief Checks whether the spell is currently blocked by the global cooldown.
 *
 * @return True if global cooldown is active; otherwise, false.
 */
bool Spell::HasGlobalCooldown()
{
    // global cooldown have only player or controlled units
    if (m_caster->GetCharmInfo())
    {
        return m_caster->GetCharmInfo()->GetGlobalCooldownMgr().HasGlobalCooldown(m_spellInfo);
    }
    else if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        return ((Player*)m_caster)->GetGlobalCooldownMgr().HasGlobalCooldown(m_spellInfo);
    }
    else
    {
        return false;
    }
}

/**
 * @brief Starts the global cooldown for the caster when applicable.
 */
void Spell::TriggerGlobalCooldown()
{
    int32 gcd = m_spellInfo->StartRecoveryTime;
    if (!gcd)
    {
        return;
    }

    // global cooldown can't leave range 1..1.5 secs (if it it)
    // exist some spells (mostly not player directly casted) that have < 1 sec and > 1.5 sec global cooldowns
    // but its as test show not affected any spell mods.
    if (gcd >= 1000 && gcd <= 1500)
    {
        // apply haste rating
        gcd = int32(float(gcd) * m_caster->GetFloatValue(UNIT_MOD_CAST_SPEED));

        if (gcd < 1000)
        {
            gcd = 1000;
        }
        else if (gcd > 1500)
        {
            gcd = 1500;
        }
    }

    // global cooldown have only player or controlled units
    if (m_caster->GetCharmInfo())
    {
        m_caster->GetCharmInfo()->GetGlobalCooldownMgr().AddGlobalCooldown(m_spellInfo, gcd);
    }
    else if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)m_caster)->GetGlobalCooldownMgr().AddGlobalCooldown(m_spellInfo, gcd);
    }
}

/**
 * @brief Cancels the global cooldown started by the current generic spell cast.
 */
void Spell::CancelGlobalCooldown()
{
    if (!m_spellInfo->StartRecoveryTime)
    {
        return;
    }

    // cancel global cooldown when interrupting current cast
    if (m_caster->GetCurrentSpell(CURRENT_GENERIC_SPELL) != this)
    {
        return;
    }

    // global cooldown have only player or controlled units
    if (m_caster->GetCharmInfo())
    {
        m_caster->GetCharmInfo()->GetGlobalCooldownMgr().CancelGlobalCooldown(m_spellInfo);
    }
    else if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)m_caster)->GetGlobalCooldownMgr().CancelGlobalCooldown(m_spellInfo);
    }
}

/**
 * @brief Resolves effective radius, chain target count, and target cap modifiers for an effect.
 *
 * @param effIndex The effect index being evaluated.
 * @param radius Receives the effective radius.
 * @param EffectChainTarget Receives the effective chain target count.
 * @param unMaxTargets Receives the effective maximum affected target count.
 */
void Spell::GetSpellRangeAndRadius(SpellEffectIndex effIndex, float& radius, uint32& EffectChainTarget, uint32& unMaxTargets) const
{
    if (m_spellInfo->EffectRadiusIndex[effIndex])
    {
        radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[effIndex]));
    }
    else
    {
        radius = GetSpellMaxRange(sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex));
    }

    if (Unit* realCaster = GetAffectiveCaster())
    {
        if (Player* modOwner = realCaster->GetSpellModOwner())
        {
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_RADIUS, radius, this);
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_JUMP_TARGETS, EffectChainTarget, this);
        }
    }

    // custom target amount cases
    switch (m_spellInfo->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
        {
            switch (m_spellInfo->Id)
            {
                case 802:                                   // Mutate Bug (AQ40, Emperor Vek'nilash)
                case 804:                                   // Explode Bug (AQ40, Emperor Vek'lor)
                case 23138:                                 // Gate of Shazzrah (MC, Shazzrah)
                case 28560:                                 // Summon Blizzard (Naxx, Sapphiron)
                case 30541:                                 // Blaze (Magtheridon)
                case 30572:                                 // Quake (Magtheridon)
                case 30769:                                 // Pick Red Riding Hood (Karazhan, Big Bad Wolf)
                case 30835:                                 // Infernal Relay (Karazhan, Prince Malchezaar)
                case 31347:                                 // Doom (Hyjal Summit, Azgalor)
                case 32312:                                 // Move 1 (Karazhan, Chess Event)
                case 33711:                                 // Murmur's Touch (Shadow Labyrinth, Murmur)
                case 37388:                                 // Move 2 (Karazhan, Chess Event)
                case 38794:                                 // Murmur's Touch (h) (Shadow Labyrinth, Murmur)
                case 39338:                                 // Karazhan - Chess, Medivh CHEAT: Hand of Medivh, Target Horde
                case 39342:                                 // Karazhan - Chess, Medivh CHEAT: Hand of Medivh, Target Alliance
                case 40834:                                 // Agonizing Flames (BT, Illidan Stormrage)
                case 41537:                                 // Summon Enslaved Soul (BT, Reliquary of Souls)
                case 44869:                                 // Spectral Blast (SWP, Kalecgos)
                case 45391:                                 // Summon Demonic Vapor (SWP, Felmyst)
                case 45785:                                 // Sinister Reflection Clone (SWP, Kil'jaeden)
                case 45892:                                 // Sinister Reflection (SWP, Kil'jaeden)
                case 45976:                                 // Open Portal (SWP, M'uru)
                case 46372:                                 // Ice Spear Target Picker (Slave Pens, Ahune)
                    unMaxTargets = 1;
                    break;
                case 10258:                                 // Awaken Vault Warder (Uldaman)
                case 28542:                                 // Life Drain (Naxx, Sapphiron)
                    unMaxTargets = 2;
                    break;
                case 30004:                                 // Flame Wreath (Karazhan, Shade of Aran)
                case 31298:                                 // Sleep (Hyjal Summit, Anetheron)
                case 39341:                                 // Karazhan - Chess, Medivh CHEAT: Fury of Medivh, Target Horde
                case 39344:                                 // Karazhan - Chess, Medivh CHEAT: Fury of Medivh, Target Alliance
                case 39992:                                 // Needle Spine Targeting (BT, Warlord Najentus)
                case 40869:                                 // Fatal Attraction (BT, Mother Shahraz)
                case 41303:                                 // Soul Drain (BT, Reliquary of Souls)
                case 41376:                                 // Spite (BT, Reliquary of Souls)
                    unMaxTargets = 3;
                    break;
                case 37676:                                 // Insidious Whisper (SSC, Leotheras the Blind)
                case 38028:                                 // Watery Grave (SSC, Morogrim Tidewalker)
                case 46650:                                 // Open Brutallus Back Door (SWP, Felmyst)
                    unMaxTargets = 4;
                    break;
                case 30843:                                 // Enfeeble (Karazhan, Prince Malchezaar)
                case 40243:                                 // Crushing Shadows (BT, Teron Gorefiend)
                case 42005:                                 // Bloodboil (BT, Gurtogg Bloodboil)
                case 45641:                                 // Fire Bloom (SWP, Kil'jaeden)
                    unMaxTargets = 5;
                    break;
                case 28796:                                 // Poison Bolt Volley (Naxx, Faerlina)
                case 29213:                                 // Curse of the Plaguebringer (Naxx, Noth the Plaguebringer)
                    unMaxTargets = 10;
                    break;
                case 25991:                                 // Poison Bolt Volley (AQ40, Pincess Huhuran)
                    unMaxTargets = 15;
                    break;
                case 46771:                                 // Flame Sear (SWP, Grand Warlock Alythess)
                    unMaxTargets = urand(3, 5);
                    break;
            }
            break;
        }
        case SPELLFAMILY_MAGE:
        {
            if (m_spellInfo->Id == 38194)                   // Blink
            {
                unMaxTargets = 1;
            }
            break;
        }
        default:
            break;
    }

    // custom radius cases
    switch (m_spellInfo->SpellFamilyName)
    {
        case SPELLFAMILY_GENERIC:
        {
            switch (m_spellInfo->Id)
            {
                case 24811:                                 // Draw Spirit (Lethon)
                {
                    if (effIndex == EFFECT_INDEX_0)         // Copy range from EFF_1 to 0
                    {
                        radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[EFFECT_INDEX_1]));
                    }
                    break;
                }
                case 28241:                                 // Poison (Naxxramas, Grobbulus Cloud)
                {
                    if (SpellAuraHolder* auraHolder = m_caster->GetSpellAuraHolder(28158))
                    {
                        radius = 0.5f * (60000 - auraHolder->GetAuraDuration()) * 0.001f;
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }
}
