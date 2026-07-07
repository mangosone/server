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

void Aura::HandleShapeshiftBoosts(bool apply)
{
    uint32 spellId1 = 0;
    uint32 spellId2 = 0;
    uint32 HotWSpellId = 0;

    ShapeshiftForm form = ShapeshiftForm(GetModifier()->m_miscvalue);

    Unit* target = GetTarget();

    switch (form)
    {
        case FORM_CAT:
            spellId1 = 3025;
            HotWSpellId = 24900;
            break;
        case FORM_TREE:
            spellId1 = 5420;
            break;
        case FORM_TRAVEL:
            spellId1 = 5419;
            break;
        case FORM_AQUA:
            spellId1 = 5421;
            break;
        case FORM_BEAR:
            spellId1 = 1178;
            spellId2 = 21178;
            HotWSpellId = 24899;
            break;
        case FORM_DIREBEAR:
            spellId1 = 9635;
            spellId2 = 21178;
            HotWSpellId = 24899;
            break;
        case FORM_BATTLESTANCE:
            spellId1 = 21156;
            break;
        case FORM_DEFENSIVESTANCE:
            spellId1 = 7376;
            break;
        case FORM_BERSERKERSTANCE:
            spellId1 = 7381;
            break;
        case FORM_MOONKIN:
            spellId1 = 24905;
            break;
        case FORM_FLIGHT:
            spellId1 = 33948;
            spellId2 = 34764;
            break;
        case FORM_FLIGHT_EPIC:
            spellId1 = 40122;
            spellId2 = 40121;
            break;
        case FORM_SPIRITOFREDEMPTION:
            spellId1 = 27792;
            spellId2 = 27795;                               // must be second, this important at aura remove to prevent to early iterator invalidation.
            break;
        case FORM_GHOSTWOLF:
        case FORM_AMBIENT:
        case FORM_GHOUL:
        case FORM_SHADOW:
        case FORM_STEALTH:
        case FORM_CREATURECAT:
        case FORM_CREATUREBEAR:
            break;
        default:
            break;
    }

    if (apply)
    {
        if (spellId1)
        {
            target->CastSpell(target, spellId1, true, NULL, this);
        }
        if (spellId2)
        {
            target->CastSpell(target, spellId2, true, NULL, this);
        }

        if (target->GetTypeId() == TYPEID_PLAYER)
        {
            const PlayerSpellMap& sp_list = ((Player*)target)->GetSpellMap();
            for (PlayerSpellMap::const_iterator itr = sp_list.begin(); itr != sp_list.end(); ++itr)
            {
                if (itr->second.state == PLAYERSPELL_REMOVED)
                {
                    continue;
                }
                if (itr->first == spellId1 || itr->first == spellId2)
                {
                    continue;
                }
                SpellEntry const* spellInfo = sSpellStore.LookupEntry(itr->first);
                if (!spellInfo || !IsNeedCastSpellAtFormApply(spellInfo, form))
                {
                    continue;
                }
                target->CastSpell(target, itr->first, true, NULL, this);
            }

            // Leader of the Pack
            if (((Player*)target)->HasSpell(17007))
            {
                SpellEntry const* spellInfo = sSpellStore.LookupEntry(24932);
                if (spellInfo && spellInfo->ShapeshiftMask & (1 << (form - 1)))
                {
                    target->CastSpell(target, 24932, true, NULL, this);
                }
            }

            // Heart of the Wild
            if (HotWSpellId)
            {
                Unit::AuraList const& mModTotalStatPct = target->GetAurasByType(SPELL_AURA_MOD_TOTAL_STAT_PERCENTAGE);
                for (Unit::AuraList::const_iterator i = mModTotalStatPct.begin(); i != mModTotalStatPct.end(); ++i)
                {
                    if ((*i)->GetSpellProto()->SpellIconID == 240 && (*i)->GetModifier()->m_miscvalue == 3)
                    {
                        int32 HotWMod = (*i)->GetModifier()->m_amount;
                        if (GetModifier()->m_miscvalue == FORM_CAT)
                        {
                            HotWMod /= 2;
                        }

                        target->CastCustomSpell(target, HotWSpellId, &HotWMod, NULL, NULL, true, NULL, this);
                        break;
                    }
                }
            }
        }
    }
    else
    {
        if (spellId1)
        {
            target->RemoveAurasDueToSpell(spellId1);
        }
        if (spellId2)
        {
            target->RemoveAurasDueToSpell(spellId2);
        }

        Unit::SpellAuraHolderMap& tAuras = target->GetSpellAuraHolderMap();
        for (Unit::SpellAuraHolderMap::iterator itr = tAuras.begin(); itr != tAuras.end();)
        {
            if (itr->second->IsRemovedOnShapeLost())
            {
                target->RemoveAurasDueToSpell(itr->second->GetId());
                itr = tAuras.begin();
            }
            else
            {
                ++itr;
            }
        }
    }
}

/**
 * @brief Applies or removes the empathy special-info flag for supported targets.
 *
 * @param apply True to apply the flag; false to remove it.
 */
void Aura::HandleAuraEmpathy(bool apply, bool /*Real*/)
{
    if (GetTarget()->GetTypeId() != TYPEID_UNIT)
    {
        return;
    }

    CreatureInfo const* ci = ObjectMgr::GetCreatureTemplate(GetTarget()->GetEntry());
    if (ci && ci->CreatureType == CREATURE_TYPE_BEAST)
    {
        GetTarget()->ApplyModUInt32Value(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_SPECIALINFO, apply);
    }
}

/**
 * @brief Applies or removes the untrackable unit byte flag.
 *
 * @param apply True to apply the flag; false to remove it.
 */
void Aura::HandleAuraUntrackable(bool apply, bool /*Real*/)
{
    if (apply)
    {
        GetTarget()->SetByteFlag(UNIT_FIELD_BYTES_1, 3, UNIT_BYTE1_FLAG_UNTRACKABLE);
    }
    else
    {
        GetTarget()->RemoveByteFlag(UNIT_FIELD_BYTES_1, 3, UNIT_BYTE1_FLAG_UNTRACKABLE);
    }
}

/**
 * @brief Applies or removes the pacified unit flag.
 *
 * @param apply True to pacify; false to remove pacify.
 */
void Aura::HandleAuraModPacify(bool apply, bool /*Real*/)
{
    if (apply)
    {
        GetTarget()->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED);
    }
    else
    {
        GetTarget()->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED);
    }
}

/**
 * @brief Applies or removes both pacify and silence effects together.
 *
 * @param apply True to apply the effects; false to remove them.
 * @param Real True when processing the real aura state change.
 */
void Aura::HandleAuraModPacifyAndSilence(bool apply, bool Real)
{
    HandleAuraModPacify(apply, Real);
    HandleAuraModSilence(apply, Real);
}

/**
 * @brief Applies or removes the ghost player flag.
 *
 * @param apply True to apply the flag; false to remove it.
 */
void Aura::HandleAuraGhost(bool apply, bool /*Real*/)
{
    if (GetTarget()->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    if (apply)
    {
        GetTarget()->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);
    }
    else
    {
        GetTarget()->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);
    }
}

void Aura::HandleAuraAllowFlight(bool apply, bool Real)
{
    // all applied/removed only at real aura add/remove
    if (!Real)
    {
        return;
    }

    GetTarget()->SetCanFly(apply);
}

void Aura::HandleModRating(bool apply, bool Real)
{
    // spells required only Real aura add/remove
    if (!Real)
    {
        return;
    }

    if (GetTarget()->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    for (uint32 rating = 0; rating < MAX_COMBAT_RATING; ++rating)
    {
        if (m_modifier.m_miscvalue & (1 << rating))
        {
            ((Player*)GetTarget())->ApplyRatingMod(CombatRating(rating), m_modifier.m_amount, apply);
        }
    }
}

void Aura::HandleForceMoveForward(bool apply, bool Real)
{
    if (!Real)
    {
        return;
    }

    if (apply)
    {
        GetTarget()->SetFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FORCE_MOVE);
    }
    else
    {
        GetTarget()->RemoveFlag(UNIT_FIELD_FLAGS_2, UNIT_FLAG2_FORCE_MOVE);
    }
}

void Aura::HandleAuraModExpertise(bool /*apply*/, bool /*Real*/)
{
    if (GetTarget()->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    ((Player*)GetTarget())->UpdateExpertise(BASE_ATTACK);
    ((Player*)GetTarget())->UpdateExpertise(OFF_ATTACK);
}

void Aura::HandleModTargetResistance(bool apply, bool Real)
{
    // spells required only Real aura add/remove
    if (!Real)
    {
        return;
    }
    Unit* target = GetTarget();
    // applied to damage as HandleNoImmediateEffect in Unit::CalculateAbsorbAndResist and Unit::CalcArmorReducedDamage
    // show armor penetration
    if (target->GetTypeId() == TYPEID_PLAYER && (m_modifier.m_miscvalue & SPELL_SCHOOL_MASK_NORMAL))
    {
        target->ApplyModInt32Value(PLAYER_FIELD_MOD_TARGET_PHYSICAL_RESISTANCE, m_modifier.m_amount, apply);
    }

    // show as spell penetration only full spell penetration bonuses (all resistances except armor and holy
    if (target->GetTypeId() == TYPEID_PLAYER && (m_modifier.m_miscvalue & SPELL_SCHOOL_MASK_SPELL) == SPELL_SCHOOL_MASK_SPELL)
    {
        target->ApplyModInt32Value(PLAYER_FIELD_MOD_TARGET_RESISTANCE, m_modifier.m_amount, apply);
    }
}

/**
 * @brief Preserves or removes combo points retained by the aura.
 *
 * @param apply True to apply the retention aura; false to remove it.
 * @param Real True when processing the real aura state change.
 */
void Aura::HandleAuraRetainComboPoints(bool apply, bool Real)
{
    // spells required only Real aura add/remove
    if (!Real)
    {
        return;
    }

    if (GetTarget()->GetTypeId() != TYPEID_PLAYER)
    {
        return;
    }

    Player* target = (Player*)GetTarget();

    // combo points was added in SPELL_EFFECT_ADD_COMBO_POINTS handler
    // remove only if aura expire by time (in case combo points amount change aura removed without combo points lost)
    if (!apply && m_removeMode == AURA_REMOVE_BY_EXPIRE && target->GetComboTargetGuid())
        if (Unit* unit = sObjectAccessor.GetUnit(*GetTarget(), target->GetComboTargetGuid()))
        {
            target->AddComboPoints(unit, -m_modifier.m_amount);
        }
}

/**
 * @brief Applies or removes the non-attackable state.
 *
 * @param Apply True to make the target unattackable; false to remove the state.
 * @param Real True when processing the real aura state change.
 */
void Aura::HandleModUnattackable(bool Apply, bool Real)
{
    if (Real && Apply)
    {
        GetTarget()->CombatStop();
        GetTarget()->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_IMMUNE_OR_LOST_SELECTION);
    }
    GetTarget()->ApplyModFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE, Apply);
}

/**
 * @brief Handles Spirit of Redemption setup and forced death on expiration.
 *
 * @param apply True to enter the spirit state; false to end it.
 * @param Real True when processing the real aura state change.
 */
void Aura::HandleSpiritOfRedemption(bool apply, bool Real)
{
    // spells required only Real aura add/remove
    if (!Real)
    {
        return;
    }

    Unit* target = GetTarget();

    // prepare spirit state
    if (apply)
    {
        if (target->GetTypeId() == TYPEID_PLAYER)
        {
            // disable breath/etc timers
            ((Player*)target)->StopMirrorTimers();

            // set stand state (expected in this form)
            if (!target->IsStandState())
            {
                target->SetStandState(UNIT_STAND_STATE_STAND);
            }
        }

        // interrupt casting when entering Spirit of Redemption
        if (target->IsNonMeleeSpellCasted(false))
        {
            target->InterruptNonMeleeSpells(false);
        }

        // set health and mana to maximum
        target->SetHealth(target->GetMaxHealth());
        target->SetPower(POWER_MANA, target->GetMaxPower(POWER_MANA));
    }
    // die at aura end
    else
    {
        target->DealDamage(target, target->GetHealth(), NULL, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, GetSpellProto(), false);
    }
}

/**
 * @brief Calculates absorb shield bonuses for school absorb effects.
 *
 * @param apply True to apply the absorb aura; false to remove it.
 * @param Real True when processing the real aura state change.
 */
void Aura::HandleSchoolAbsorb(bool apply, bool Real)
{
    if (!Real)
    {
        return;
    }

    Unit* caster = GetCaster();
    if (!caster)
    {
        return;
    }

    Unit* target = GetTarget();
    SpellEntry const* spellProto = GetSpellProto();
    if (apply)
    {
        // prevent double apply bonuses
        if (target->GetTypeId() != TYPEID_PLAYER || !((Player*)target)->GetSession()->PlayerLoading())
        {
            float DoneActualBenefit = 0.0f;
            switch (spellProto->SpellClassSet)
            {
                case SPELLFAMILY_PRIEST:
                    // Power Word: Shield
                    if (spellProto->SpellClassMask & UI64LIT(0x0000000000000001))
                    {
                        //+30% from +healing bonus
                        DoneActualBenefit = caster->SpellBaseHealingBonusDone(GetSpellSchoolMask(spellProto)) * 0.3f;
                        break;
                    }
                    break;
                case SPELLFAMILY_MAGE:
                    // Frost Ward, Fire Ward
                    if (spellProto->IsFitToFamilyMask(UI64LIT(0x0000000100080108)))
                        //+10% from +spell bonus
                    {
                        DoneActualBenefit = caster->SpellBaseDamageBonusDone(GetSpellSchoolMask(spellProto)) * 0.1f;
                    }
                    break;
                case SPELLFAMILY_WARLOCK:
                    // Shadow Ward
                    if (!spellProto->SpellClassMask)
                        //+10% from +spell bonus
                    {
                        DoneActualBenefit = caster->SpellBaseDamageBonusDone(GetSpellSchoolMask(spellProto)) * 0.1f;
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
