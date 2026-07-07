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



#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "WorldPacket.h"
#include "Opcodes.h"
#include "Log.h"
#include "UpdateMask.h"
#include "World.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "SkillExtraItems.h"
#include "Unit.h"
#include "Spell.h"
#include "DynamicObject.h"
#include "SpellAuras.h"
#include "Group.h"
#include "UpdateData.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "SharedDefines.h"
#include "Pet.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "Creature.h"
#include "Totem.h"
#include "CreatureAI.h"
#include "BattleGround/BattleGroundMgr.h"
#include "BattleGround/BattleGround.h"
#include "BattleGround/BattleGroundEY.h"
#include "BattleGround/BattleGroundWS.h"
#include "Language.h"
#include "SocialMgr.h"
#include "VMapFactory.h"
#include "Util.h"
#include "TemporarySummon.h"
#include "ScriptMgr.h"
#include "SkillDiscovery.h"
#include "Formulas.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "G3D/Vector3.h"
#ifdef ENABLE_ELUNA
#include "LuaEngine.h"
#endif /* ENABLE_ELUNA */

/**
 * @brief Opens or sends loot for the specified object guid.
 *
 * @param guid The loot source guid.
 * @param loottype The loot type to open.
 * @param lockType The lock interaction type.
 */
void Spell::SendLoot(ObjectGuid guid, LootType loottype, LockType lockType)
{
    if (gameObjTarget)
    {
        switch (gameObjTarget->GetGoType())
        {
            case GAMEOBJECT_TYPE_DOOR:
            case GAMEOBJECT_TYPE_BUTTON:
            case GAMEOBJECT_TYPE_QUESTGIVER:
            case GAMEOBJECT_TYPE_SPELL_FOCUS:
            case GAMEOBJECT_TYPE_GOOBER:
                gameObjTarget->Use(m_caster);
                return;

            case GAMEOBJECT_TYPE_CHEST:
                gameObjTarget->Use(m_caster);
                // Don't return, let loots been taken
                break;

            case GAMEOBJECT_TYPE_TRAP:
                if (lockType == LOCKTYPE_DISARM_TRAP)
                {
                    gameObjTarget->SetLootState(GO_JUST_DEACTIVATED);
                    return;
                }
                sLog.outError("Spell::SendLoot unhandled locktype %u for GameObject trap (entry %u) for spell %u.", lockType, gameObjTarget->GetEntry(), m_spellInfo->ID);
                return;
            default:
                sLog.outError("Spell::SendLoot unhandled GameObject type %u (entry %u) for spell %u.", gameObjTarget->GetGoType(), gameObjTarget->GetEntry(), m_spellInfo->ID);
                return;
        }
    }

    if (m_caster->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    // Send loot
    ((Player*)m_caster)->SendLoot(guid, loottype);
}

/**
 * @brief Opens a locked game object or item and awards related skill progress.
 *
 * @param eff_idx The open-lock effect index.
 */
void Spell::EffectOpenLock(SpellEffectIndex eff_idx)
{
    if (!m_caster || m_caster->GetTypeId() != TYPEID_PLAYER)
    {
        DEBUG_LOG("WORLD: Open Lock - No Player Caster!");
        return;
    }

    Player* player = (Player*)m_caster;

    uint32 lockId = 0;
    ObjectGuid guid;

    // Get lockId
    if (gameObjTarget)
    {
        GameObjectInfo const* goInfo = gameObjTarget->GetGOInfo();
        // Arathi Basin banner opening !
        if ((goInfo->type == GAMEOBJECT_TYPE_BUTTON && goInfo->button.noDamageImmune) ||
            (goInfo->type == GAMEOBJECT_TYPE_GOOBER && goInfo->goober.losOK))
        {
            // CanUseBattleGroundObject() already called in CheckCast()
            // in battleground check
            if (BattleGround* bg = player->GetBattleGround())
            {
                // check if it's correct bg
                if (bg->GetTypeID() == BATTLEGROUND_AB || bg->GetTypeID() == BATTLEGROUND_AV)
                {
                    bg->EventPlayerClickedOnFlag(player, gameObjTarget);
                }
                return;
            }
        }
        else if (goInfo->type == GAMEOBJECT_TYPE_FLAGSTAND)
        {
            // CanUseBattleGroundObject() already called in CheckCast()
            // in battleground check
            if (BattleGround* bg = player->GetBattleGround())
            {
                if (bg->GetTypeID() == BATTLEGROUND_EY)
                {
                    bg->EventPlayerClickedOnFlag(player, gameObjTarget);
                }
                return;
            }
        }
        lockId = goInfo->GetLockId();
        guid = gameObjTarget->GetObjectGuid();
    }
    else if (itemTarget)
    {
        lockId = itemTarget->GetProto()->LockID;
        guid = itemTarget->GetObjectGuid();
    }
    else
    {
        DEBUG_LOG("WORLD: Open Lock - No GameObject/Item Target!");
        return;
    }

    SkillType skillId = SKILL_NONE;
    int32 reqSkillValue = 0;
    int32 skillValue;

    SpellCastResult res = CanOpenLock(eff_idx, lockId, skillId, reqSkillValue, skillValue);
    if (res != SPELL_CAST_OK)
    {
        SendCastResult(res);
        return;
    }

    // mark item as unlocked
    if (itemTarget)
    {
        itemTarget->SetFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_UNLOCKED);
    }

    SendLoot(guid, LOOT_SKINNING, LockType(m_spellInfo->EffectMiscValue[eff_idx]));

    // not allow use skill grow at item base open
    if (!m_CastItem && skillId != SKILL_NONE)
    {
        // update skill if really known
        if (uint32 pureSkillValue = player->GetPureSkillValue(skillId))
        {
            if (gameObjTarget)
            {
                // Allow one skill-up until respawned
                if (!gameObjTarget->IsInSkillupList(player) &&
                    player->UpdateGatherSkill(skillId, pureSkillValue, reqSkillValue))
                    {
                        gameObjTarget->AddToSkillupList(player);
                    }
            }
            else if (itemTarget)
            {
                // Do one skill-up
                player->UpdateGatherSkill(skillId, pureSkillValue, reqSkillValue);
            }
        }
    }
}

/**
 * @brief Replaces the cast item with another item entry.
 *
 * @param eff_idx The effect index defining the replacement item.
 */
void Spell::EffectSummonChangeItem(SpellEffectIndex eff_idx)
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    Player* player = (Player*)m_caster;

    // applied only to using item
    if (!m_CastItem)
    {
        return;
    }

    // ... only to item in own inventory/bank/equip_slot
    if (m_CastItem->GetOwnerGuid() != player->GetObjectGuid())
    {
        return;
    }

    uint32 newitemid = m_spellInfo->EffectItemType[eff_idx];
    if (!newitemid)
    {
        return;
    }

    Item* oldItem = m_CastItem;

    // prevent crash at access and unexpected charges counting with item update queue corrupt
    ClearCastItem();

    player->ConvertItem(oldItem, newitemid);
}

/**
 * @brief Grants weapon or armor proficiency to a player target.
 */
void Spell::EffectProficiency(SpellEffectIndex /*eff_idx*/)
{
    if (!unitTarget || unitTarget->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }
    Player* p_target = (Player*)unitTarget;

    uint32 subClassMask = m_spellInfo->EquippedItemSubclass;
    if (m_spellInfo->EquippedItemClass == ITEM_CLASS_WEAPON && !(p_target->GetWeaponProficiency() & subClassMask))
    {
        p_target->AddWeaponProficiency(subClassMask);
        p_target->SendProficiency(ITEM_CLASS_WEAPON, p_target->GetWeaponProficiency());
    }
    if (m_spellInfo->EquippedItemClass == ITEM_CLASS_ARMOR && !(p_target->GetArmorProficiency() & subClassMask))
    {
        p_target->AddArmorProficiency(subClassMask);
        p_target->SendProficiency(ITEM_CLASS_ARMOR, p_target->GetArmorProficiency());
    }
}

/**
 * @brief Creates and attaches an area aura for the current unit target.
 *
 * @param eff_idx The area aura effect index.
 */
void Spell::EffectApplyAreaAura(SpellEffectIndex eff_idx)
{
    if (!unitTarget)
    {
        return;
    }
    if (!unitTarget->IsAlive())
    {
        return;
    }

    AreaAura* Aur = new AreaAura(m_spellInfo, eff_idx, &m_currentBasePoints[eff_idx], m_spellAuraHolder, unitTarget, m_caster, m_CastItem);
    m_spellAuraHolder->AddAura(Aur, eff_idx);
}

void Spell::EffectSummonType(SpellEffectIndex eff_idx)
{
    uint32 prop_id = m_spellInfo->EffectMiscValueB[eff_idx];
    SummonPropertiesEntry const* summon_prop = sSummonPropertiesStore.LookupEntry(prop_id);
    if (!summon_prop)
    {
        sLog.outError("EffectSummonType: Unhandled summon type %u", prop_id);
        return;
    }

    switch (summon_prop->Group)
    {
            // faction handled later on, or loaded from template
        case SUMMON_PROP_GROUP_WILD:
        case SUMMON_PROP_GROUP_FRIENDLY:
        {
            switch (summon_prop->Title)
            {
                case UNITNAME_SUMMON_TITLE_NONE:
                {
                    // those are classical totems - effectbasepoints is their hp and not summon ammount!
                    // UNITNAME_SUMMON_TITLE_TOTEM = 121: 23035, battlestands
                    if (prop_id == 121)
                    {
                        DoSummonTotem(eff_idx);
                    }
                    else
                    {
                        DoSummonWild(eff_idx, summon_prop->FactionId);
                    }
                    break;
                }
                case UNITNAME_SUMMON_TITLE_PET:
                    DoSummonGuardian(eff_idx, summon_prop->FactionId);
                    break;
                case UNITNAME_SUMMON_TITLE_GUARDIAN:
                {
                    if (prop_id == 61)                      // mixed guardians, totems, statues
                    {
                        // * Stone Statue, etc  -- fits much better totem AI
                        if (m_spellInfo->SpellIconID == 2056)
                        {
                            DoSummonTotem(eff_idx);
                        }
                        else
                        {
                            // possible sort totems/guardians only by summon creature type
                            CreatureInfo const* cInfo = ObjectMgr::GetCreatureTemplate(m_spellInfo->EffectMiscValue[eff_idx]);

                            if (!cInfo)
                            {
                                return;
                            }

                            // FIXME: not all totems and similar cases seelcted by this check...
                            if (cInfo->CreatureType == CREATURE_TYPE_TOTEM)
                            {
                                DoSummonTotem(eff_idx);
                            }
                            else
                            {
                                DoSummonGuardian(eff_idx, summon_prop->FactionId);
                            }
                        }
                    }
                    else
                    {
                        DoSummonGuardian(eff_idx, summon_prop->FactionId);
                    }
                    break;
                }
                case UNITNAME_SUMMON_TITLE_TOTEM:
                {
                    DoSummonTotem(eff_idx, summon_prop->Slot);
                    break;
                }
                case UNITNAME_SUMMON_TITLE_COMPANION:
                {
                    DoSummonCritter(eff_idx, summon_prop->FactionId);
                    break;
                }
                default:
                    sLog.outError("EffectSummonType: Unhandled summon title %u", summon_prop->Title);
                    break;
            }
            break;
        }
        case SUMMON_PROP_GROUP_PETS:
        {
            DoSummon(eff_idx);
            break;
        }
        case SUMMON_PROP_GROUP_CONTROLLABLE:
        {
            DoSummonPossessed(eff_idx, summon_prop->FactionId);
            break;
        }
        default:
            sLog.outError("EffectSummonType: Unhandled summon group type %u", summon_prop->Group);
            break;
    }
}

/**
 * @brief Summons a temporary wild creature defined by the spell effect.
 *
 * @param eff_idx The summon effect index.
 */
void Spell::DoSummonWild(SpellEffectIndex eff_idx, uint32 forceFaction)
{
    uint32 creature_entry = m_spellInfo->EffectMiscValue[eff_idx];
    if (!creature_entry)
    {
        return;
    }

    uint32 level = m_caster->getLevel();

    // level of creature summoned using engineering item based at engineering skill level
    if (m_caster->GetTypeId() == TYPEID_PLAYER && m_CastItem)
    {
        ItemPrototype const* proto = m_CastItem->GetProto();
        if (proto && proto->RequiredSkill == SKILL_ENGINEERING)
        {
            uint16 skill202 = ((Player*)m_caster)->GetSkillValue(SKILL_ENGINEERING);
            if (skill202)
            {
                level = skill202 / 5;
            }
        }
    }

    // select center of summon position
    float center_x = m_targets.m_destX;
    float center_y = m_targets.m_destY;
    float center_z = m_targets.m_destZ;

    float radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[eff_idx]));
    int32 duration = GetSpellDuration(m_spellInfo);
    TempSpawnType summonType = (duration == 0) ? TEMPSPAWN_DEAD_DESPAWN : TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN;

    int32 amount = damage > 0 ? damage : 1;

    for (int32 count = 0; count < amount; ++count)
    {
        float px, py, pz;
        // If dest location if present
        if (m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION)
        {
            // Summon 1 unit in dest location
            if (count == 0)
            {
                px = m_targets.m_destX;
                py = m_targets.m_destY;
                pz = m_targets.m_destZ;
            }
            // Summon in random point all other units if location present
            else
            {
                m_caster->GetRandomPoint(center_x, center_y, center_z, radius, px, py, pz);
            }
        }
        // Summon if dest location not present near caster
        else
        {
            if (radius > 0.0f)
            {
                // not using bounding radius of caster here
                m_caster->GetClosePoint(px, py, pz, 0.0f, radius);
            }
            else
            {
                // EffectRadiusIndex 0 or 36
                px = m_caster->GetPositionX();
                py = m_caster->GetPositionY();
                pz = m_caster->GetPositionZ();
            }
        }

        if (Creature* summon = m_caster->SummonCreature(creature_entry, px, py, pz, m_caster->GetOrientation(), summonType, duration))
        {
            summon->SetUInt32Value(UNIT_CREATED_BY_SPELL, m_spellInfo->ID);

            // UNIT_FIELD_CREATEDBY are not set for these kind of spells.
            // Does exceptions exist? If so, what are they?
            // summon->SetCreatorGuid(m_caster->GetObjectGuid());

            if (forceFaction)
            {
                summon->setFaction(forceFaction);
            }

            // Notify original caster if not done already
            if (m_originalCaster && m_originalCaster != m_caster && m_originalCaster->GetTypeId() == TYPEID_UNIT && ((Creature*)m_originalCaster)->AI())
            {
                ((Creature*)m_originalCaster)->AI()->JustSummoned(summon);
            }
#ifdef ENABLE_ELUNA
            if (m_originalCaster)
                if (Unit* summoner = m_originalCaster->ToUnit())
                {
                    if (Eluna* e = summoner->GetEluna())
                    {
                        e->OnSummoned(summon, summoner);
                    }
                }
#endif /* ENABLE_ELUNA */
        }
    }
}

/**
 * @brief Summons one or more guardian pets for the caster.
 *
 * @param eff_idx The summon effect index.
 */
void Spell::DoSummonGuardian(SpellEffectIndex eff_idx, uint32 forceFaction)
{
    uint32 pet_entry = m_spellInfo->EffectMiscValue[eff_idx];
    if (!pet_entry)
    {
        return;
    }

    CreatureInfo const* cInfo = ObjectMgr::GetCreatureTemplate(pet_entry);
    if (!cInfo)
    {
        sLog.outErrorDb("Spell::DoSummonGuardian: creature entry %u not found for spell %u.", pet_entry, m_spellInfo->ID);
        return;
    }

    // in another case summon new
    uint32 level = m_caster->getLevel();

    // level of pet summoned using engineering item based at engineering skill level
    if (m_caster->GetTypeId() == TYPEID_PLAYER && m_CastItem)
    {
        ItemPrototype const* proto = m_CastItem->GetProto();
        if (proto && proto->RequiredSkill == SKILL_ENGINEERING)
        {
            uint16 skill202 = ((Player*)m_caster)->GetSkillValue(SKILL_ENGINEERING);
            if (skill202)
            {
                level = skill202 / 5;
            }
        }
    }

    // select center of summon position
    float center_x = m_targets.m_destX;
    float center_y = m_targets.m_destY;
    float center_z = m_targets.m_destZ;

    float radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[eff_idx]));

    int32 amount = damage > 0 ? damage : 1;

    for (int32 count = 0; count < amount; ++count)
    {
        Pet* spawnCreature = new Pet(GUARDIAN_PET);

        // If dest location if present
        // Summon 1 unit in dest location
        CreatureCreatePos pos(m_caster->GetMap(), m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ, m_caster->GetOrientation() + M_PI_F);

        if (m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION)
        {
            // Summon in random point all other units if location present
            if (count > 0)
            {
                float x, y, z;
                m_caster->GetRandomPoint(center_x, center_y, center_z, radius, x, y, z);
                pos = CreatureCreatePos(m_caster->GetMap(), x, y, z, m_caster->GetOrientation());
            }
        }
        // Summon if dest location not present near caster
        else
        {
            pos = CreatureCreatePos(m_caster, m_caster->GetOrientation());
        }

        Map* map = m_caster->GetMap();
        uint32 pet_number = sObjectMgr.GeneratePetNumber();
        if (!spawnCreature->Create(map->GenerateLocalLowGuid(HIGHGUID_PET), pos, cInfo, pet_number))
        {
            sLog.outError("Spell::DoSummonGuardian: can't create creature entry %u for spell %u.", pet_entry, m_spellInfo->ID);
            delete spawnCreature;
            return;
        }

        spawnCreature->SetRespawnCoord(pos);

        if (m_duration > 0)
        {
            spawnCreature->SetDuration(m_duration);
        }

        // spawnCreature->SetName("");                      // generated by client
        spawnCreature->SetOwnerGuid(m_caster->GetObjectGuid());
        spawnCreature->SetPowerType(POWER_MANA);
        spawnCreature->SetUInt32Value(UNIT_NPC_FLAGS, spawnCreature->GetCreatureInfo()->NpcFlags);
        spawnCreature->setFaction(forceFaction ? forceFaction : m_caster->getFaction());
        spawnCreature->SetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP, 0);
        spawnCreature->SetCreatorGuid(m_caster->GetObjectGuid());
        spawnCreature->SetUInt32Value(UNIT_CREATED_BY_SPELL, m_spellInfo->ID);

        spawnCreature->InitStatsForLevel(level, m_caster);
        spawnCreature->GetCharmInfo()->SetPetNumber(pet_number, false);

        m_caster->AddGuardian(spawnCreature);

        map->Add((Creature*)spawnCreature);
        spawnCreature->AIM_Initialize();

        // Notify Summoner
        if (m_caster->GetTypeId() == TYPEID_UNIT && ((Creature*)m_caster)->AI())
        {
            ((Creature*)m_caster)->AI()->JustSummoned(spawnCreature);
        }
        if (m_originalCaster && m_originalCaster != m_caster && m_originalCaster->GetTypeId() == TYPEID_UNIT && ((Creature*)m_originalCaster)->AI())
        {
            ((Creature*)m_originalCaster)->AI()->JustSummoned(spawnCreature);
        }
#ifdef ENABLE_ELUNA
        if (Unit* summoner = m_caster->ToUnit())
        {
            if (Eluna* e = summoner->GetEluna())
            {
                e->OnSummoned(spawnCreature, summoner);
            }
        }
        if (m_originalCaster)
            if (Unit* summoner = m_originalCaster->ToUnit())
            {
                if (Eluna* e = summoner->GetEluna())
                {
                    e->OnSummoned(spawnCreature, summoner);
                }
            }
#endif /* ENABLE_ELUNA */
    }
}

/**
 * @brief Summons a totem into the appropriate totem slot.
 *
 * @param eff_idx The totem summon effect index.
 */
void Spell::DoSummonTotem(SpellEffectIndex eff_idx, uint8 slot_dbc)
{
    // DBC store slots starting from 1, with no slot 0 value)
    int slot = slot_dbc ? slot_dbc - 1 : TOTEM_SLOT_NONE;

    // unsummon old totem
    if (slot < MAX_TOTEM_SLOT)
        if (Totem* OldTotem = m_caster->GetTotem(TotemSlot(slot)))
        {
            OldTotem->UnSummon();
        }

    // FIXME: Setup near to finish point because GetObjectBoundingRadius set in Create but some Create calls can be dependent from proper position
    // if totem have creature_template_addon.auras with persistent point for example or script call
    float angle = slot < MAX_TOTEM_SLOT ? M_PI_F / MAX_TOTEM_SLOT - (slot * 2 * M_PI_F / MAX_TOTEM_SLOT) : 0;

    CreatureCreatePos pos(m_caster, m_caster->GetOrientation(), 2.0f, angle);

    CreatureInfo const* cinfo = ObjectMgr::GetCreatureTemplate(m_spellInfo->EffectMiscValue[eff_idx]);
    if (!cinfo)
    {
        sLog.outErrorDb("Creature entry %u does not exist but used in spell %u totem summon.", m_spellInfo->ID, m_spellInfo->EffectMiscValue[eff_idx]);
        return;
    }

    Totem* pTotem = new Totem;

    if (!pTotem->Create(m_caster->GetMap()->GenerateLocalLowGuid(HIGHGUID_UNIT), pos, cinfo, m_caster))
    {
        delete pTotem;
        return;
    }

    pTotem->SetRespawnCoord(pos);

    if (slot < MAX_TOTEM_SLOT)
    {
        m_caster->_AddTotem(TotemSlot(slot), pTotem);
    }

    // pTotem->SetName("");                                 // generated by client
    pTotem->SetOwner(m_caster);
    pTotem->SetTypeBySummonSpell(m_spellInfo);              // must be after Create call where m_spells initialized

    pTotem->SetDuration(m_duration);

    if (damage)                                             // if not spell info, DB values used
    {
        pTotem->SetMaxHealth(damage);
        pTotem->SetHealth(damage);
    }

    pTotem->SetUInt32Value(UNIT_CREATED_BY_SPELL, m_spellInfo->ID);

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        pTotem->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP_ATTACKABLE);
    }

    if (m_caster->IsPvP())
    {
        pTotem->SetPvP(true);
    }

    // sending SMSG_TOTEM_CREATED before add to map (done in Summon)
    if (slot < MAX_TOTEM_SLOT && m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        WorldPacket data(SMSG_TOTEM_CREATED, 1 + 8 + 4 + 4);
        data << uint8(slot);
        data << pTotem->GetObjectGuid();
        data << uint32(m_duration);
        data << uint32(m_spellInfo->ID);
        ((Player*)m_caster)->SendDirectMessage(&data);
    }

    pTotem->Summon(m_caster);
}

/**
 * @brief Summons a possessed creature and transfers control to the caster.
 *
 * @param eff_idx The summon effect index.
 */
bool Spell::DoSummonPossessed(SpellEffectIndex eff_idx, uint32 forceFaction)
{
    uint32 creatureEntry = m_spellInfo->EffectMiscValue[eff_idx];
    CreatureInfo const* cInfo = ObjectMgr::GetCreatureTemplate(creatureEntry);
    if (!cInfo)
    {
        sLog.outErrorDb("Spell::DoSummonPossessed: creature entry %u not found for spell %u.", creatureEntry, m_spellInfo->ID);
        return false;
    }

    Creature* spawnCreature = m_caster->SummonCreature(creatureEntry, m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ, m_caster->GetOrientation(), TEMPSPAWN_CORPSE_DESPAWN, 0);
    if (!spawnCreature)
    {
        sLog.outError("Spell::DoSummonPossessed: creature entry %u for spell %u could not be summoned.", creatureEntry, m_spellInfo->ID);
        return false;
    }

    // Changes to be sent
    spawnCreature->SetCharmerGuid(m_caster->GetObjectGuid());
    spawnCreature->SetCreatorGuid(m_caster->GetObjectGuid());
    spawnCreature->SetUInt32Value(UNIT_CREATED_BY_SPELL, m_spellInfo->ID);
    spawnCreature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);

    spawnCreature->SetLevel(m_caster->getLevel());

    spawnCreature->SetWalk(m_caster->IsWalking());
    // TODO: Set Fly

    // Internal changes
    spawnCreature->addUnitState(UNIT_STAT_CONTROLLED);

    // Changes to owner
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        Player* player = (Player*)m_caster;

        player->GetCamera().SetView(spawnCreature);

        player->SetCharm(spawnCreature);
        player->SetClientControl(spawnCreature, 1);
        player->SetMover(spawnCreature);

        if (CharmInfo* charmInfo = spawnCreature->InitCharmInfo(spawnCreature))
        {
            charmInfo->InitPossessCreateSpells();
        }
        player->PossessSpellInitialize();
    }

    // Notify Summoner
    if (m_originalCaster && m_originalCaster != m_caster && m_originalCaster->GetTypeId() == TYPEID_UNIT && ((Creature*)m_originalCaster)->AI())
    {
        ((Creature*)m_originalCaster)->AI()->JustSummoned(spawnCreature);
    }
#ifdef ENABLE_ELUNA
    if (Unit* summoner = m_originalCaster->ToUnit())
    {
        if (Eluna* e = summoner->GetEluna())
        {
            e->OnSummoned(spawnCreature, summoner);
        }
    }
#endif /* ENABLE_ELUNA */
    return true;
}

/**
 * @brief Summons or toggles the caster's vanity critter companion.
 *
 * @param eff_idx The summon effect index.
 */
void Spell::DoSummonCritter(SpellEffectIndex eff_idx, uint32 forceFaction)
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }
    Player* player = (Player*)m_caster;

    uint32 pet_entry = m_spellInfo->EffectMiscValue[eff_idx];
    if (!pet_entry)
    {
        return;
    }

    CreatureInfo const* cInfo = ObjectMgr::GetCreatureTemplate(pet_entry);
    if (!cInfo)
    {
        sLog.outErrorDb("Spell::DoSummonCritter: creature entry %u not found for spell %u.", pet_entry, m_spellInfo->ID);
        return;
    }

    Pet* old_critter = player->GetMiniPet();

    // for same pet just despawn
    if (old_critter && old_critter->GetEntry() == pet_entry)
    {
        player->RemoveMiniPet();
        return;
    }

    // despawn old pet before summon new
    if (old_critter)
    {
        player->RemoveMiniPet();
    }

    CreatureCreatePos pos(m_caster->GetMap(), m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ, m_caster->GetOrientation());
    if (!(m_targets.m_targetMask & TARGET_FLAG_DEST_LOCATION))
    {
        pos = CreatureCreatePos(m_caster, m_caster->GetOrientation());
    }

    // summon new pet
    Pet* critter = new Pet(MINI_PET);

    Map* map = m_caster->GetMap();
    uint32 pet_number = sObjectMgr.GeneratePetNumber();
    if (!critter->Create(map->GenerateLocalLowGuid(HIGHGUID_PET), pos, cInfo, pet_number))
    {
        sLog.outError("Spell::EffectSummonCritter, spellid %u: no such creature entry %u", m_spellInfo->ID, pet_entry);
        delete critter;
        return;
    }

    critter->SetRespawnCoord(pos);

    // critter->SetName("");                                // generated by client
    critter->SetOwnerGuid(m_caster->GetObjectGuid());
    critter->SetCreatorGuid(m_caster->GetObjectGuid());
    critter->SetUInt32Value(UNIT_CREATED_BY_SPELL, m_spellInfo->ID);
    critter->setFaction(forceFaction ? forceFaction : m_caster->getFaction());
    critter->SelectLevel();       // some summoned creaters have different from 1 DB data for level/hp
    critter->SetUInt32Value(UNIT_NPC_FLAGS, critter->GetCreatureInfo()->NpcFlags);
    // some mini-pets have quests

    // set timer for unsummon
    int32 duration = GetSpellDuration(m_spellInfo);
    if (duration > 0)
    {
        critter->SetDuration(duration);
    }

    player->_SetMiniPet(critter);

    map->Add((Creature*)critter);
    critter->AIM_Initialize();
    critter->InitPetCreateSpells();                         // e.g. disgusting oozeling has a create spell as critter...

    // Notify Summoner
    if (m_caster->GetTypeId() == TYPEID_UNIT && ((Creature*)m_caster)->AI())
    {
        ((Creature*)m_caster)->AI()->JustSummoned(critter);
    }
    if (m_originalCaster && m_originalCaster != m_caster && m_originalCaster->GetTypeId() == TYPEID_UNIT && ((Creature*)m_originalCaster)->AI())
    {
        ((Creature*)m_originalCaster)->AI()->JustSummoned(critter);
    }
#ifdef ENABLE_ELUNA
    if (Unit* summoner = m_caster->ToUnit())
    {
        if (Eluna* e = summoner->GetEluna())
        {
            e->OnSummoned(critter, summoner);
        }
    }
    if (m_originalCaster)
        if (Unit* summoner = m_originalCaster->ToUnit())
        {
            if (Eluna* e = summoner->GetEluna())
            {
                e->OnSummoned(critter, summoner);
            }
        }
#endif /* ENABLE_ELUNA */
}
