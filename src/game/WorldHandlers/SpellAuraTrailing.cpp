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
 * @file SpellAuras.cpp
 * @brief Spell aura implementation
 *
 * This file implements the SpellAura class which handles spell auras:
 * - Aura application and removal
 * - Aura effect processing (stat modifiers, DoTs, HoTs, etc.)
 * - Aura stacking rules
 * - Aura dispelling mechanics
 * - Aura periodic effects
 * - Aura duration management
 * - Aura visual effects
 *
 * Auras are persistent effects applied by spells that modify
 * unit stats, deal damage over time, or provide other benefits.
 *
 * @see SpellAura for the aura class
 * @see Spell for spell casting
 */



#include "SpellAuras.h"
#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Opcodes.h"
#include "Log.h"
#include "UpdateMask.h"
#include "World.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "Unit.h"
#include "Spell.h"
#include "DynamicObject.h"
#include "Group.h"
#include "UpdateData.h"
#include "ObjectAccessor.h"
#include "Policies/Singleton.h"
#include "Totem.h"
#include "Creature.h"
#include "Formulas.h"
#include "BattleGround/BattleGround.h"
#include "OutdoorPvP/OutdoorPvP.h"
#include "CreatureAI.h"
#include "ScriptMgr.h"
#include "Util.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "Language.h"
#include "MapManager.h"

/**
 * @brief Applies or removes prevention of fleeing on feared targets.
 *
 * @param apply True to prevent fleeing; false to restore it.
 * @param Real True when processing the real aura state change.
 */
void Aura::HandlePreventFleeing(bool apply, bool Real)
{
    if (!Real)
    {
        return;
    }

    Unit::AuraList const& fearAuras = GetTarget()->GetAurasByType(SPELL_AURA_MOD_FEAR);
    if (!fearAuras.empty())
    {
        if (apply)
        {
            GetTarget()->SetFeared(false, fearAuras.front()->GetCasterGuid());
        }
        else
        {
            GetTarget()->SetFeared(true);
        }
    }
}

/**
 * @brief Calculates bonus absorb values for mana shield effects.
 *
 * @param apply True to apply the shield; false to remove it.
 * @param Real True when processing the real aura state change.
 */
void Aura::HandleManaShield(bool apply, bool Real)
{
    if (!Real)
    {
        return;
    }

    // prevent double apply bonuses
    if (apply && (GetTarget()->GetTypeId() != TYPEID_PLAYER || !((Player*)GetTarget())->GetSession()->PlayerLoading()))
    {
        if (Unit* caster = GetCaster())
        {
            float DoneActualBenefit = 0.0f;
            switch (GetSpellProto()->SpellFamilyName)
            {
                case SPELLFAMILY_MAGE:
                    if (GetSpellProto()->SpellFamilyFlags & UI64LIT(0x0000000000008000))
                    {
                        // Mana Shield
                        // +50% from +spd bonus
                        DoneActualBenefit = caster->SpellBaseDamageBonusDone(GetSpellSchoolMask(GetSpellProto())) * 0.5f;
                        break;
                    }
                    break;
                default:
                    break;
            }

            DoneActualBenefit *= caster->CalculateLevelPenalty(GetSpellProto());

            m_modifier.m_amount += (int32)DoneActualBenefit;
        }
    }
}

void Aura::HandleArenaPreparation(bool apply, bool Real)
{
    if (!Real)
    {
        return;
    }

    Unit* target = GetTarget();

    target->ApplyModFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PREPARATION, apply);

    if (apply)
    {
        // max regen powers at start preparation
        target->SetHealth(target->GetMaxHealth());
        target->SetPower(POWER_MANA, target->GetMaxPower(POWER_MANA));
        target->SetPower(POWER_ENERGY, target->GetMaxPower(POWER_ENERGY));
    }
    else
    {
        // reset originally 0 powers at start/leave
        target->SetPower(POWER_RAGE, 0);
    }
}

void Aura::HandleAuraMirrorImage(bool apply, bool Real)
{
    if (!Real)
    {
        return;
    }

    // Target of aura should always be creature (ref Spell::CheckCast)
    Creature* pCreature = (Creature*)GetTarget();

    if (apply)
    {
        // Caster can be player or creature, the unit who pCreature will become an clone of.
        Unit* caster = GetCaster();

        pCreature->SetByteValue(UNIT_FIELD_BYTES_0, 0, caster->getRace());
        pCreature->SetByteValue(UNIT_FIELD_BYTES_0, 1, caster->getClass());
        pCreature->SetByteValue(UNIT_FIELD_BYTES_0, 2, caster->getGender());
        pCreature->SetByteValue(UNIT_FIELD_BYTES_0, 3, caster->GetPowerType());

        pCreature->SetFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_CLONED);

        pCreature->SetDisplayId(caster->GetNativeDisplayId());
    }
    else
    {
        const CreatureInfo* cinfo = pCreature->GetCreatureInfo();
        const CreatureModelInfo* minfo = sObjectMgr.GetCreatureModelInfo(pCreature->GetNativeDisplayId());

        pCreature->SetByteValue(UNIT_FIELD_BYTES_0, 0, 0);
        pCreature->SetByteValue(UNIT_FIELD_BYTES_0, 1, cinfo->UnitClass);
        pCreature->SetByteValue(UNIT_FIELD_BYTES_0, 2, minfo->gender);
        pCreature->SetByteValue(UNIT_FIELD_BYTES_0, 3, 0);

        pCreature->RemoveFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_CLONED);

        pCreature->SetDisplayId(pCreature->GetNativeDisplayId());
    }
}

void Aura::HandleAuraSafeFall(bool Apply, bool Real)
{
    // implemented in WorldSession::HandleMovementOpcodes

    // only special case
    if (Apply && Real && GetId() == 32474 && GetTarget()->GetTypeId() == TYPEID_PLAYER)
    {
        ((Player*)GetTarget())->ActivateTaxiPathTo(506, GetId());
    }
}
